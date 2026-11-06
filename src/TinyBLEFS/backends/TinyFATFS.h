#pragma once
// Server backend for the TinyFATFS library (FatFs by ChaN as header-only
// Arduino library): share an SD card (SPI / SDMMC), a RAM disk, a USB stick,
// ... over BLE.
// https://github.com/pschatzmann/TinyFATFS
//
//   RamIO drive{200, 512};            // or the SD card driver
//   TinyFatBackend backend(SD);       // SD: the global object of TinyFATFS
//   FileServer server(backend, transport, "sd");
//   void setup() { SD.begin(drive); transport.begin("TinyFS SD"); }
//
// SD.begin() must succeed before the first client connects. Data is written to
// the medium when the client closes the file.
//
// If FF_FS_LOCK is used in ffconf.h of TinyFATFS it must be at least the
// number of simultaneously open files of the server (maxHandles).

#include <fatfs.h>

#include <memory>

#include "../FsBackend.h"
#include "../Logger.h"
#include "../Protocol.h"

namespace tinyblefs {

class TinyFatBackend : public FsBackend {
 public:
  explicit TinyFatBackend(fatfs::SDClass& sd) : sd_(sd) {}

  FsFile* open(const char* path, uint8_t flags) override {
    bool write = flags & (kWrite | kAppend | kCreate | kTrunc);
    uint8_t mode = FA_READ;
    if (write) {
      mode |= FA_WRITE;
      if (flags & kTrunc) mode |= FA_CREATE_ALWAYS;      // create or truncate
      else if (flags & kCreate) mode |= FA_OPEN_ALWAYS;  // create, keep content
      else mode |= FA_OPEN_EXISTING;
    }
    std::unique_ptr<TinyFatFile> f(new TinyFatFile(sd_, path, mode));
    if (!f->isOpen()) {
      TINYBLEFS_LOGE("tinyfat", "cannot open %s (mode 0x%02x)", path, mode);
      return nullptr;
    }
    return f.release();
  }

  bool stat(const char* path, FsEntry& e) override {
    if (isRoot(path)) {  // FatFs has no FILINFO for the root directory
      e.name.clear();
      e.isDir = true;
      e.size = 0;
      return true;
    }
    fatfs::FILINFO info;
    fatfs::FRESULT rc = sd_.getFatFs()->f_stat(path, &info);
    if (rc != fatfs::FR_OK) {
      TINYBLEFS_LOGD("tinyfat", "stat %s: FatFs error %d", path, (int)rc);
      return false;
    }
    fill(info, e);
    return true;
  }

  bool list(const char* path, uint32_t index, FsEntry& e) override {
    fatfs::FatFs* fs = sd_.getFatFs();
    fatfs::DIR dir;
    fatfs::FRESULT rc = fs->f_opendir(&dir, path);
    if (rc != fatfs::FR_OK) {
      TINYBLEFS_LOGW("tinyfat", "list %s: FatFs error %d", path, (int)rc);
      return false;
    }
    bool found = false;
    fatfs::FILINFO info;
    for (uint32_t i = 0;; i++) {
      if (fs->f_readdir(&dir, &info) != fatfs::FR_OK || info.fname[0] == 0) break;
      if (i == index) {
        fill(info, e);
        found = true;
        break;
      }
    }
    fs->f_closedir(&dir);
    return found;
  }

  bool remove(const char* path) override { return ok(sd_.remove(path), "remove", path); }
  bool mkdir(const char* path) override { return ok(sd_.mkdir(path), "mkdir", path); }
  bool rmdir(const char* path) override { return ok(sd_.rmdir(path), "rmdir", path); }
  bool rename(const char* from, const char* to) override {
    fatfs::FRESULT rc = sd_.getFatFs()->f_rename(from, to);
    if (rc != fatfs::FR_OK) TINYBLEFS_LOGW("tinyfat", "rename %s to %s: FatFs error %d", from, to, (int)rc);
    return rc == fatfs::FR_OK;
  }

 private:
  /// An open file of TinyFATFS; the destructor closes it
  class TinyFatFile : public FsFile {
   public:
    TinyFatFile(fatfs::SDClass& sd, const char* path, uint8_t mode)
        : f_(sd.open(path, mode)) {}
    bool isOpen() { return (bool)f_; }
    int read(uint32_t offset, uint8_t* buf, size_t len) override {
      if (!f_.seek(offset)) {
        TINYBLEFS_LOGE("tinyfat", "seek to %u failed", (unsigned)offset);
        return -1;
      }
      return (int)f_.readBytes(buf, len);  // 0 at the end of the file
    }
    int write(uint32_t offset, const uint8_t* buf, size_t len) override {
      if (!f_.seek(offset)) {
        TINYBLEFS_LOGE("tinyfat", "seek to %u failed", (unsigned)offset);
        return -1;
      }
      size_t n = f_.write(buf, len);
      if (n < len)
        TINYBLEFS_LOGE("tinyfat", "wrote %u of %u bytes (disk full?)", (unsigned)n, (unsigned)len);
      return (int)n;
    }
    uint32_t size() override { return f_.size(); }

   private:
    fatfs::File f_;
  };

  static bool isRoot(const char* path) {
    return path == nullptr || path[0] == 0 || (path[0] == '/' && path[1] == 0);
  }
  static void fill(const fatfs::FILINFO& info, FsEntry& e) {
    e.name = info.fname;
    e.isDir = (info.fattrib & AM_DIR) != 0;
    e.size = e.isDir ? 0 : (uint32_t)info.fsize;
  }
  static bool ok(bool result, const char* op, const char* path) {
    if (!result) TINYBLEFS_LOGW("tinyfat", "%s %s failed", op, path);
    return result;
  }

  fatfs::SDClass& sd_;
};

}  // namespace tinyblefs

#if defined(ARDUINO) && !defined(TINYBLEFS_NO_USING_NAMESPACE)
using tinyblefs::TinyFatBackend;
#endif
