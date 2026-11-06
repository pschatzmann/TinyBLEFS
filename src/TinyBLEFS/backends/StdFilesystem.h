#pragma once
// Server backend on top of std::filesystem (Linux / host).
// The server's root directory is passed to the Server; this backend works
// with absolute host paths.

#include <filesystem>
#include <fstream>

#include <cerrno>
#include <cstring>

#include "../FsBackend.h"
#include "../Logger.h"
#include "../Protocol.h"

namespace tinyblefs {

class StdFile : public FsFile {
 public:
  StdFile(const std::string& path, std::ios::openmode mode) {
    f_.open(path, mode | std::ios::binary);
  }
  bool isOpen() { return f_.is_open(); }
  int read(uint32_t offset, uint8_t* buf, size_t len) override {
    f_.clear();
    f_.seekg(offset);
    f_.read((char*)buf, len);
    int n = (int)f_.gcount();
    if (f_.bad()) {
      TINYBLEFS_LOGE("stdfs", "read error at offset %u", (unsigned)offset);
      return -1;
    }
    f_.clear();
    return n;
  }
  int write(uint32_t offset, const uint8_t* buf, size_t len) override {
    f_.clear();
    f_.seekp(offset);
    f_.write((const char*)buf, len);
    f_.flush();
    if (!f_) {
      TINYBLEFS_LOGE("stdfs", "write error at offset %u", (unsigned)offset);
      return -1;
    }
    return (int)len;
  }
  uint32_t size() override {
    f_.clear();
    f_.seekg(0, std::ios::end);
    return (uint32_t)f_.tellg();
  }

 private:
  std::fstream f_;
};

class StdFilesystemBackend : public FsBackend {
 public:
  FsFile* open(const char* path, uint8_t flags) override {
    namespace fs = std::filesystem;
    std::error_code ec;
    bool exists = fs::is_regular_file(path, ec);
    bool write = flags & (kWrite | kAppend | kCreate | kTrunc);
    std::ios::openmode mode = std::ios::in;
    if (write) {
      if (!exists) {
        if (!(flags & (kCreate | kTrunc))) return nullptr;
        std::ofstream create(path, std::ios::binary);  // create empty file
        if (!create) {
          TINYBLEFS_LOGE("stdfs", "cannot create %s: %s", path, strerror(errno));
          return nullptr;
        }
      } else if (flags & kTrunc) {
        std::ofstream trunc(path, std::ios::binary | std::ios::trunc);
        if (!trunc) {
          TINYBLEFS_LOGE("stdfs", "cannot truncate %s: %s", path, strerror(errno));
          return nullptr;
        }
      }
      mode |= std::ios::out;
    } else if (!exists) {
      return nullptr;
    }
    StdFile* f = new StdFile(path, mode);
    if (!f->isOpen()) {
      TINYBLEFS_LOGE("stdfs", "cannot open %s: %s", path, strerror(errno));
      delete f;
      return nullptr;
    }
    return f;
  }

  bool stat(const char* path, FsEntry& e) override {
    namespace fs = std::filesystem;
    std::error_code ec;
    auto st = fs::status(path, ec);
    if (ec || !fs::exists(st)) {
      if (ec) TINYBLEFS_LOGD("stdfs", "stat %s: %s", path, ec.message().c_str());
      return false;
    }
    e.name = fs::path(path).filename().string();
    e.isDir = fs::is_directory(st);
    e.size = e.isDir ? 0 : (uint32_t)fs::file_size(path, ec);
    return true;
  }

  bool list(const char* path, uint32_t index, FsEntry& e) override {
    namespace fs = std::filesystem;
    std::error_code ec;
    uint32_t i = 0;
    fs::directory_iterator it(path, ec), end;
    if (ec) {
      TINYBLEFS_LOGW("stdfs", "list %s: %s", path, ec.message().c_str());
      return false;
    }
    for (; !ec && it != end; it.increment(ec), ++i) {
      if (i == index) {
        e.name = it->path().filename().string();
        e.isDir = it->is_directory(ec);
        e.size = e.isDir ? 0 : (uint32_t)it->file_size(ec);
        return true;
      }
    }
    return false;
  }

  bool remove(const char* path) override {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) ||
        !std::filesystem::remove(path, ec)) {
      TINYBLEFS_LOGW("stdfs", "remove %s: %s", path, ec ? ec.message().c_str() : "not a file");
      return false;
    }
    return true;
  }
  bool mkdir(const char* path) override {
    std::error_code ec;
    if (!std::filesystem::create_directory(path, ec)) {
      TINYBLEFS_LOGW("stdfs", "mkdir %s: %s", path, ec ? ec.message().c_str() : "failed");
      return false;
    }
    return true;
  }
  bool rmdir(const char* path) override {
    std::error_code ec;
    if (!std::filesystem::is_directory(path, ec) ||
        !std::filesystem::remove(path, ec)) {
      TINYBLEFS_LOGW("stdfs", "rmdir %s: %s", path, ec ? ec.message().c_str() : "not a directory");
      return false;
    }
    return true;
  }
  bool rename(const char* from, const char* to) override {
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    if (ec) {
      TINYBLEFS_LOGW("stdfs", "rename %s to %s: %s", from, to, ec.message().c_str());
      return false;
    }
    return true;
  }
};

}  // namespace tinyblefs
