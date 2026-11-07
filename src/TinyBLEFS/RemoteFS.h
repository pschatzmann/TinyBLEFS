#pragma once
// Arduino style File / FS facade for one remote server. Stand-alone: it does
// not depend on any Arduino core file system class.
//
//   tinyblefs::RemoteFS fs(client);
//   File f = fs.open("/log.txt", "a");
//   f.println("hello");   // see File::print* helpers
//   f.close();

#include <string.h>

#include <memory>

#include "Client.h"

namespace tinyblefs {

class RemoteFS;

/// File on a remote server with the usual Arduino File API (read, write, seek, list, ...); reads ahead and batches directory listings
class RemoteFile {
 public:
  RemoteFile() = default;
  /// True if the file or directory was opened
  explicit operator bool() const { return s_ && s_->valid; }

  // ---- reading ---------------------------------------------------------
  /// Reads up to len bytes; returns the number of bytes read
  size_t read(uint8_t* dst, size_t len) {
    if (!alive() || s_->isDir) {
      TINYBLEFS_LOGW("file", "read from a closed file or a directory");
      return 0;
    }
    State& s = *s_;
    size_t total = 0;
    while (total < len) {
      if (s.bufValid && s.pos >= s.bufStart &&
          s.pos < s.bufStart + s.buf.size()) {
        size_t off = s.pos - s.bufStart;
        size_t n = std::min(len - total, s.buf.size() - off);
        memcpy(dst + total, &s.buf[off], n);
        total += n;
        s.pos += (uint32_t)n;
      } else {
        std::vector<uint8_t> chunk;
        Status st = s.c->read(s.handle, s.pos, s.c->chunkSize(), chunk);
        if (st != Status::Ok) {
          TINYBLEFS_LOGW("file", "read of %s at offset %u failed: %s", s.path.c_str(),
                      (unsigned)s.pos, statusName(st));
          break;
        }
        if (chunk.empty()) break;  // end of file
        s.buf.swap(chunk);
        s.bufStart = s.pos;
        s.bufValid = true;
      }
    }
    return total;
  }
  /// Reads one byte (-1 at the end)
  int read() {
    uint8_t b;
    return read(&b, 1) == 1 ? b : -1;
  }
  /// Next byte without consuming it (-1 at the end)
  int peek() {
    int b = read();
    if (b >= 0) s_->pos--;
    return b;
  }
  /// Number of bytes left to read
  int available() {
    if (!*this || s_->isDir) return 0;
    return s_->size > s_->pos ? (int)(s_->size - s_->pos) : 0;
  }

  // ---- writing ---------------------------------------------------------
  /// Writes len bytes; returns the number of bytes written
  size_t write(const uint8_t* src, size_t len) {
    if (!alive() || s_->isDir) {
      TINYBLEFS_LOGW("file", "write to a closed file or a directory");
      return 0;
    }
    State& s = *s_;
    s.bufValid = false;
    size_t total = 0;
    while (total < len) {
      size_t n = std::min<size_t>(len - total, s.c->chunkSize());
      uint16_t written = 0;
      Status st = s.c->write(s.handle, s.pos, src + total, n, written);
      if (st != Status::Ok) {
        TINYBLEFS_LOGW("file", "write to %s at offset %u failed: %s", s.path.c_str(),
                    (unsigned)s.pos, statusName(st));
        break;
      }
      total += written;
      s.pos += written;
      if (s.pos > s.size) s.size = s.pos;
      if (written < n) {
        TINYBLEFS_LOGW("file", "short write to %s: %u of %u bytes", s.path.c_str(),
                    (unsigned)written, (unsigned)n);
        break;
      }
    }
    return total;
  }
  /// Writes one byte
  size_t write(uint8_t b) { return write(&b, 1); }
  /// Writes a string
  size_t print(const char* str) { return write((const uint8_t*)str, strlen(str)); }
  /// Writes a string and a line end
  size_t println(const char* str) { return print(str) + print("\n"); }
  /// Sends buffered data to the server
  void flush() {}  // writes are synchronous

  // ---- position --------------------------------------------------------
  /// Sets the read/write position
  bool seek(uint32_t pos) {
    if (!*this || s_->isDir) {
      TINYBLEFS_LOGW("file", "seek in a closed file or a directory");
      return false;
    }
    s_->pos = pos;
    return true;
  }
  /// Current read/write position
  uint32_t position() const { return *this ? s_->pos : 0; }
  /// File size in bytes
  uint32_t size() const { return *this ? s_->size : 0; }

  /// Closes the file
  void close() {
    if (s_) s_->closeNow();
    s_.reset();
  }

  // ---- info / directories ----------------------------------------------
  /// True for a directory
  bool isDirectory() const { return *this && s_->isDir; }
  /// File name without path
  const char* name() const { return *this ? s_->name.c_str() : ""; }
  /// Full path
  const char* path() const { return *this ? s_->path.c_str() : ""; }

  /// Next entry of a directory (invalid File at the end)
  RemoteFile openNextFile(const char* mode = "r") {
    if (!isDirectory()) return RemoteFile();
    State& s = *s_;
    size_t idx = s.dirIndex;
    if (idx < s.dirBufStart || idx >= s.dirBufStart + s.dirBuf.size()) {
      // the entry is not in the buffered batch: fetch the batch that starts here
      if (s.dirEnd && idx >= s.dirBufStart + s.dirBuf.size()) return RemoteFile();
      Status st = s.c->listBatch(s.path, s.dirIndex, s.c->chunkSize(), s.dirBuf, s.dirEnd);
      s.dirBufStart = s.dirIndex;
      if (st != Status::Ok) {
        TINYBLEFS_LOGW("file", "listing of %s failed: %s", s.path.c_str(), statusName(st));
        s.dirBuf.clear();
        return RemoteFile();
      }
      if (s.dirBuf.empty()) return RemoteFile();
    }
    const DirEntry& e = s.dirBuf[idx - s.dirBufStart];
    s.dirIndex++;
    std::string child = s.path;
    if (child.empty() || child.back() != '/') child += '/';
    child += e.name;
    return make(*s.c, child, modeFlags(mode));
  }
  /// Restarts the directory listing
  void rewindDirectory() {
    if (!isDirectory()) return;
    s_->dirIndex = 0;
    s_->dirBuf.clear();
    s_->dirBufStart = 0;
    s_->dirEnd = false;
  }

 private:
  friend class RemoteFS;

  /// Shared state of a RemoteFile (copies of the object refer to the same file)
  struct State {
    FileClient* c = nullptr;
    std::string path, name;
    uint8_t handle = 0;
    uint32_t gen = 0;  // FileClient::generation() when the file was opened
    bool valid = false, isDir = false;
    uint32_t size = 0, pos = 0;
    uint16_t dirIndex = 0;
    std::vector<DirEntry> dirBuf;  // directory entries fetched in one batch
    uint16_t dirBufStart = 0;      // index of dirBuf[0]
    bool dirEnd = false;           // dirBuf ends with the last entry of the directory
    std::vector<uint8_t> buf;  // read ahead
    uint32_t bufStart = 0;
    bool bufValid = false;

    void closeNow() {
      // after a lost session the handle may belong to another file now
      if (valid && !isDir && c && gen == c->generation()) c->close(handle);
      valid = false;
    }
    ~State() { closeNow(); }
  };

  explicit RemoteFile(std::shared_ptr<State> s) : s_(std::move(s)) {}

  /// False if the file is closed or was lost together with the server session
  bool alive() {
    if (!*this) return false;
    if (!s_->isDir && s_->gen != s_->c->generation()) {
      TINYBLEFS_LOGW("file", "%s was closed: the server session was lost", s_->path.c_str());
      s_->valid = false;
      return false;
    }
    return true;
  }

  static uint8_t modeFlags(const char* mode) {
    if (!mode || mode[0] == 'r') {
      return (mode && strchr(mode, '+')) ? (kRead | kWrite) : kRead;
    }
    if (mode[0] == 'w') return kWrite | kCreate | kTrunc;
    if (mode[0] == 'a') return kWrite | kCreate | kAppend;
    return kRead;
  }

  static RemoteFile make(FileClient& c, const std::string& path, uint8_t flags,
                         Status* status = nullptr) {
    uint8_t handle = 0;
    uint32_t size = 0;
    Status st = c.open(path, flags, handle, size);
    if (status) *status = st;
    if (st == Status::Busy) {
      // with openWait() the caller retries and reports; a plain open reports
      if (status) TINYBLEFS_LOGD("file", "%s is busy", path.c_str());
      else TINYBLEFS_LOGW("file", "cannot open %s: busy (open for writing by another client)", path.c_str());
      return RemoteFile();
    }
    if (st == Status::NotFound) {
      TINYBLEFS_LOGD("file", "%s does not exist", path.c_str());
      return RemoteFile();
    }
    if (st != Status::Ok && st != Status::IsDirectory) {
      TINYBLEFS_LOGW("file", "cannot open %s: %s", path.c_str(), statusName(st));
      return RemoteFile();
    }
    auto s = std::make_shared<State>();
    s->c = &c;
    s->gen = c.generation();
    s->path = path;
    size_t p = path.find_last_of('/');
    s->name = p == std::string::npos ? path : path.substr(p + 1);
    if (st == Status::IsDirectory) {
      s->isDir = true;
    } else {
      s->handle = handle;
      s->size = size;
      s->pos = (flags & kAppend) ? size : 0;
    }
    s->valid = true;
    return RemoteFile(s);
  }

  std::shared_ptr<State> s_;
};

/// Copies the content of src (from its current position) to dst, in chunks.
/// Works between files of different servers. Returns false on any error.
inline bool copyFile(RemoteFile& src, RemoteFile& dst, size_t bufSize = 512) {
  if (!src || !dst || src.isDirectory() || dst.isDirectory()) {
    TINYBLEFS_LOGW("copy", "source or destination is not an open file");
    return false;
  }
  std::vector<uint8_t> buf(bufSize);
  while (true) {
    size_t n = src.read(buf.data(), buf.size());
    if (n == 0) break;
    if (dst.write(buf.data(), n) != n) {
      TINYBLEFS_LOGE("copy", "write to %s failed", dst.path());
      return false;
    }
  }
  // read() returns 0 both at the end and on a failed request
  if (src.position() < src.size()) {
    TINYBLEFS_LOGE("copy", "read of %s failed at %u of %u", src.path(),
                   (unsigned)src.position(), (unsigned)src.size());
    return false;
  }
  return true;
}

/// The file system of one remote server
class RemoteFS {
 public:
  /// Creates a file system on the server behind the client
  explicit RemoteFS(FileClient& c) : c_(&c) {}

  /// mode: "r", "r+", "w", "a" (like the Arduino FS API). Directories can be
  /// opened with "r".
  RemoteFile open(const char* path, const char* mode = "r") {
    return RemoteFile::make(*c_, path, RemoteFile::modeFlags(mode));
  }
  /// Like open(), but while the file is locked by another client (busy) it
  /// retries every retryMs, up to timeoutMs. Use it for appending to a shared
  /// file or for writing a file others may read.
  RemoteFile openWait(const char* path, const char* mode = "r",
                      uint32_t timeoutMs = 5000, uint32_t retryMs = 100) {
    uint32_t start = nowMs();
    while (true) {
      Status st = Status::Ok;
      RemoteFile f = RemoteFile::make(*c_, path, RemoteFile::modeFlags(mode), &st);
      if (f || st != Status::Busy) return f;
      if (nowMs() - start >= timeoutMs) {
        TINYBLEFS_LOGW("fs", "%s is still busy after %u ms", path, (unsigned)timeoutMs);
        return f;
      }
      sleepMs(retryMs);
    }
  }
  /// True if the path exists
  bool exists(const char* path) {
    bool d;
    uint32_t s;
    return c_->stat(path, d, s) == Status::Ok;
  }
  /// Type and size of a file or directory, w/o opening it
  bool stat(const char* path, bool& isDir, uint32_t& size) {
    return c_->stat(path, isDir, size) == Status::Ok;
  }
  /// Lists a directory w/o opening its entries (no server file handles are
  /// used). Returns false if the directory could not be read.
  bool list(const char* path, std::vector<DirEntry>& entries) {
    entries.clear();
    uint16_t index = 0;
    while (true) {  // one request returns many entries
      std::vector<DirEntry> part;
      bool end = false;
      Status st = c_->listBatch(path, index, c_->chunkSize(), part, end);
      if (st != Status::Ok) {
        TINYBLEFS_LOGW("fs", "list %s failed: %s", path, statusName(st));
        return false;
      }
      index += (uint16_t)part.size();
      for (auto& e : part) entries.push_back(std::move(e));
      if (end) return true;
      if (part.empty()) {  // cannot happen with a correct server: avoid looping
        TINYBLEFS_LOGW("fs", "list %s: the server returned no entries", path);
        return false;
      }
    }
  }
  /// Deletes a file
  bool remove(const char* path) { return check("remove", path, c_->remove(path)); }
  /// Creates a directory
  bool mkdir(const char* path) { return check("mkdir", path, c_->mkdir(path)); }
  /// Removes an empty directory
  bool rmdir(const char* path) { return check("rmdir", path, c_->rmdir(path)); }
  /// Renames or moves a file or directory
  bool rename(const char* from, const char* to) {
    return check("rename", from, c_->rename(from, to));
  }
  /// The underlying client
  FileClient& client() { return *c_; }

 private:
  bool check(const char* op, const char* path, Status st) {
    if (st == Status::Ok) return true;
    TINYBLEFS_LOGW("fs", "%s %s failed: %s", op, path, statusName(st));
    return false;
  }
  FileClient* c_;
};

}  // namespace tinyblefs
