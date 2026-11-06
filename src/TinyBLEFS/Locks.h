#pragma once
// Server side file locks: protect the file system (a file must not be opened
// twice for writing: on FAT this corrupts the directory) and the data (two
// appending clients would overwrite each other).
//
// Policy, per file:
//   - any number of readers
//   - one writer at a time (open with write, append, create or truncate)
//   - truncating (mode "w") needs a file that is not open at all
//   - remove / rename / rmdir need a path that is not open (nor any file below it)
// Locks are released when the file is closed, the session ends or the client
// disconnects.

#include <stdint.h>

#include <map>
#include <string>

namespace tinyblefs {

/// Shared file locks: any number of readers or one writer per file
class FileLocks {
 public:
  /// Registers an open file; returns false if it conflicts with a lock
  bool acquire(const std::string& path, bool write, bool truncate) {
    Entry& e = entries_[path];
    bool ok = !write || (e.writers == 0 && (!truncate || e.readers == 0));
    if (ok) {
      if (write) e.writers++;
      else e.readers++;
    } else if (e.readers == 0 && e.writers == 0) {
      entries_.erase(path);
    }
    return ok;
  }

  /// Releases a lock taken with acquire()
  void release(const std::string& path, bool write) {
    auto it = entries_.find(path);
    if (it == entries_.end()) return;
    uint16_t& n = write ? it->second.writers : it->second.readers;
    if (n > 0) n--;
    if (it->second.readers == 0 && it->second.writers == 0) entries_.erase(it);
  }

  /// True if the file or any file below the path is open
  bool isBusy(const std::string& path) const {
    auto it = entries_.find(path);
    if (it != entries_.end()) return true;
    std::string prefix = path;
    if (prefix.empty() || prefix.back() != '/') prefix += '/';
    auto lb = entries_.lower_bound(prefix);
    return lb != entries_.end() && lb->first.compare(0, prefix.size(), prefix) == 0;
  }

  /// Number of files with a lock
  size_t openFiles() const { return entries_.size(); }

 private:
  /// Lock state of one file
  struct Entry {
    uint16_t readers = 0, writers = 0;
  };
  std::map<std::string, Entry> entries_;
};

}  // namespace tinyblefs
