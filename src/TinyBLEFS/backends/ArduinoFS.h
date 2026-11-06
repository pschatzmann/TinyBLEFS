#pragma once
// Server backend for any Arduino style file system class (ESP32 / RP2040
// fs::FS: SD, SD_MMC, LittleFS, SPIFFS, FFat, ...).
//
//   ArduinoFsBackend<fs::LittleFSFS> backend(LittleFS);
//   FileServer server(backend, transport, "sensor1");
//
// Requires the FS to accept string open modes ("r", "w", "r+") like the ESP32
// and RP2040 cores do. Not usable with SdFat style O_* flags.
//
// On the ESP32 the directory listing and stat use the POSIX API of the virtual
// file system (opendir / readdir / stat below FS::mountpoint()) instead of
// opening a File object for every entry, which is much faster. The directory
// is kept open between consecutive list() calls. Define
// TINYBLEFS_ARDUINOFS_NO_POSIX to switch this off; TINYBLEFS_ARDUINOFS_POSIX enables
// it on other systems whose FS has a mountpoint() (e.g. for tests).

#include <string.h>

#include <string>
#include <utility>

#if defined(ESP32) && !defined(TINYBLEFS_ARDUINOFS_NO_POSIX) && !defined(TINYBLEFS_ARDUINOFS_POSIX)
#define TINYBLEFS_ARDUINOFS_POSIX 1
#endif
#if defined(TINYBLEFS_ARDUINOFS_POSIX)
#include <dirent.h>
#include <sys/stat.h>
#endif

#include "../FsBackend.h"
#include "../Logger.h"
#include "../Protocol.h"

namespace tinyblefs {

/// FsBackend for any Arduino file system (LittleFS, SPIFFS, FFat, SD, SD_MMC, ...); uses the POSIX API for fast listing on the ESP32
template <class FS>
class ArduinoFsBackend : public FsBackend {
 public:
  /// File type of the wrapped file system
  using FileT = decltype(std::declval<FS&>().open("/", "r"));

  /// Serves the given file system (e.g. LittleFS, SD)
  explicit ArduinoFsBackend(FS& fs) : fs_(fs) {}
  ~ArduinoFsBackend() override { closeCursor(); }

  FsFile* open(const char* path, uint8_t flags) override {
    bool write = flags & (kWrite | kAppend | kCreate | kTrunc);
    if (write) closeCursor();
    bool exists = fs_.exists(path);
    const char* mode = "r";
    if (write) {
      if (!exists && !(flags & (kCreate | kTrunc))) return nullptr;
      mode = (!exists || (flags & kTrunc)) ? "w" : "r+";
    } else if (!exists) {
      return nullptr;
    }
    FileT f = fs_.open(path, mode);
    if (!f || f.isDirectory()) {
      TINYBLEFS_LOGE("arduinofs", "cannot open %s (mode %s)", path, mode);
      return nullptr;
    }
    return new ArduinoFile(f);
  }

  /// Reports whether the path exists and its type and size
  bool stat(const char* path, FsEntry& e) override {
#if defined(TINYBLEFS_ARDUINOFS_POSIX)
    const char* mp = mountOf(fs_, 0);
    if (mp && *mp) return statPosix(posixPath(mp, path), path, e);
#endif
    FileT f = fs_.open(path, "r");
    if (!f) return false;
    e.name = baseName(f.name());
    e.isDir = f.isDirectory();
    e.size = e.isDir ? 0 : (uint32_t)f.size();
    f.close();
    return true;
  }

  bool list(const char* path, uint32_t index, FsEntry& e) override {
#if defined(TINYBLEFS_ARDUINOFS_POSIX)
    const char* mp = mountOf(fs_, 0);
    if (mp && *mp) return listPosix(posixPath(mp, path), index, e);
#endif
    FileT dir = fs_.open(path, "r");
    if (!dir || !dir.isDirectory()) return false;
    for (uint32_t i = 0;; i++) {
      FileT item = dir.openNextFile();
      if (!item) break;
      if (i == index) {
        e.name = baseName(item.name());
        e.isDir = item.isDirectory();
        e.size = e.isDir ? 0 : (uint32_t)item.size();
        item.close();
        dir.close();
        return true;
      }
      item.close();
    }
    dir.close();
    return false;
  }

  /// Deletes a file
  bool remove(const char* path) override {
    closeCursor();
    return ok(fs_.remove(path), "remove", path);
  }
  /// Creates a directory
  bool mkdir(const char* path) override {
    closeCursor();
    return ok(fs_.mkdir(path), "mkdir", path);
  }
  /// Removes an empty directory
  bool rmdir(const char* path) override {
    closeCursor();
    return ok(fs_.rmdir(path), "rmdir", path);
  }
  /// Renames or moves a file or directory
  bool rename(const char* from, const char* to) override {
    closeCursor();
    return ok(fs_.rename(from, to), "rename", from);
  }

 private:
  static bool ok(bool result, const char* op, const char* path) {
    if (!result) TINYBLEFS_LOGW("arduinofs", "%s %s failed", op, path);
    return result;
  }

  /// Open file of an Arduino file system
  class ArduinoFile : public FsFile {
   public:
    explicit ArduinoFile(FileT f) : f_(f) {}
    ~ArduinoFile() override { f_.close(); }
    int read(uint32_t offset, uint8_t* buf, size_t len) override {
      if (!f_.seek(offset)) return 0;  // beyond end of file
      return (int)f_.read(buf, len);
    }
    int write(uint32_t offset, const uint8_t* buf, size_t len) override {
      if (!f_.seek(offset)) {
        TINYBLEFS_LOGE("arduinofs", "seek to %u failed", (unsigned)offset);
        return -1;
      }
      int n = (int)f_.write(buf, len);
      if (n < (int)len)
        TINYBLEFS_LOGE("arduinofs", "wrote %d of %u bytes (file system full?)", n, (unsigned)len);
      return n;
    }
    uint32_t size() override { return (uint32_t)f_.size(); }

   private:
    FileT f_;
  };

  static std::string baseName(const char* n) {
    std::string s(n ? n : "");
    size_t p = s.rfind('/');
    return p == std::string::npos ? s : s.substr(p + 1);
  }

#if defined(TINYBLEFS_ARDUINOFS_POSIX)
  // ---- POSIX fast path ---------------------------------------------------
  // FS::mountpoint() (e.g. "/littlefs") is the prefix of the file system in the
  // virtual file system of the ESP32. Resolved on each call: the file system
  // may be started after the backend was created.
  template <class T>
  static auto mountOf(T& fs, int) -> decltype((const char*)fs.mountpoint()) {
    return fs.mountpoint();
  }
  template <class T>
  static const char* mountOf(T&, long) {
    return nullptr;  // this FS class has no mountpoint(): use the Arduino API
  }

  static std::string posixPath(const char* mountpoint, const char* path) {
    std::string s(mountpoint);
    if (path[0] != '/') s += '/';
    return s + path;
  }

  static bool statPosix(const std::string& full, const char* path, FsEntry& e) {
    if (path[0] == 0 || (path[0] == '/' && path[1] == 0)) {  // the root directory
      e.name.clear();
      e.isDir = true;
      e.size = 0;
      return true;
    }
    struct stat st;
    if (::stat(full.c_str(), &st) != 0) return false;
    e.name = baseName(path);
    e.isDir = S_ISDIR(st.st_mode);
    e.size = e.isDir ? 0 : (uint32_t)st.st_size;
    return true;
  }

  /// Next entry of the open directory (without "." and "..")
  struct dirent* nextEntry() {
    struct dirent* de;
    while ((de = readdir(cursor_)) != nullptr) {
      if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0) break;
    }
    return de;
  }

  bool listPosix(const std::string& dir, uint32_t index, FsEntry& e) {
    // The server asks for entry 0, 1, 2, ...: continue where we stopped
    if (!cursor_ || cursor_next_ != index || cursor_path_ != dir) {
      closeCursor();
      cursor_ = opendir(dir.c_str());
      if (!cursor_) {
        TINYBLEFS_LOGD("arduinofs", "list %s: cannot open directory", dir.c_str());
        return false;
      }
      cursor_path_ = dir;
      cursor_next_ = 0;
      while (cursor_next_ < index) {  // not sequential: skip the first entries
        if (!nextEntry()) {
          closeCursor();
          return false;
        }
        cursor_next_++;
      }
    }
    struct dirent* de = nextEntry();
    if (!de) {  // end of the directory
      closeCursor();
      return false;
    }
    cursor_next_++;
    e.name = de->d_name;
    std::string full = dir;
    if (full.empty() || full.back() != '/') full += '/';
    full += de->d_name;
    struct stat st;
    bool have = ::stat(full.c_str(), &st) == 0;  // the size is not in dirent
    e.isDir = de->d_type == DT_DIR || (de->d_type == DT_UNKNOWN && have && S_ISDIR(st.st_mode));
    e.size = (e.isDir || !have) ? 0 : (uint32_t)st.st_size;
    return true;
  }

  void closeCursor() {
    if (cursor_) {
      closedir(cursor_);
      cursor_ = nullptr;
    }
  }

  DIR* cursor_ = nullptr;
  std::string cursor_path_;
  uint32_t cursor_next_ = 0;
#else
  void closeCursor() {}
#endif

  FS& fs_;
};

}  // namespace tinyblefs

#if defined(ARDUINO) && !defined(TINYBLEFS_NO_USING_NAMESPACE)
using tinyblefs::ArduinoFsBackend;
#endif
