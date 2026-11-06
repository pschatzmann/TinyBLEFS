#pragma once
// File system abstraction used by the Server. Pass any implementation to the
// Server constructor: see backends/ArduinoFS.h (SD, LittleFS, SPIFFS, FFat...)
// and backends/StdFilesystem.h (Linux / host).

#include <stdint.h>

#include <string>

namespace tinyblefs {

/// One directory entry returned by FsBackend::list()
struct FsEntry {
  /// Name without path
  std::string name;  // name without path
  /// True for a directory
  bool isDir = false;
  /// Size in bytes (0 for directories)
  uint32_t size = 0;
};

/// An open file. Destroying the object closes the file.
class FsFile {
 public:
  virtual ~FsFile() = default;
  /// Reads up to len bytes at offset; returns bytes read or -1 on error
  virtual int read(uint32_t offset, uint8_t* buf, size_t len) = 0;
  /// Writes len bytes at offset; returns bytes written or -1 on error
  virtual int write(uint32_t offset, const uint8_t* buf, size_t len) = 0;
  /// File size in bytes
  virtual uint32_t size() = 0;
};

/// File system used by the FileServer: implement it to serve any storage (see backends/)
class FsBackend {
 public:
  virtual ~FsBackend() = default;
  /// Opens a file (flags: OpenFlags). Returns nullptr on failure.
  virtual FsFile* open(const char* path, uint8_t flags) = 0;
  /// Reports whether the path exists and its type and size
  virtual bool stat(const char* path, FsEntry& entry) = 0;
  /// Provides the index-th entry of a directory; false at the end
  virtual bool list(const char* path, uint32_t index, FsEntry& entry) = 0;
  /// Deletes a file
  virtual bool remove(const char* path) = 0;
  /// Creates a directory
  virtual bool mkdir(const char* path) = 0;
  /// Removes an empty directory
  virtual bool rmdir(const char* path) = 0;
  /// Renames or moves a file or directory
  virtual bool rename(const char* from, const char* to) = 0;
};

}  // namespace tinyblefs
