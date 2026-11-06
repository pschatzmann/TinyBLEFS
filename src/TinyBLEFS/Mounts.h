#pragma once
// Mounts remote servers below "/<name>/": the name is announced by the server
// (or overridden with an alias), collisions get a "-2", "-3", ... suffix.
//
//   tinyblefs::Mounts mounts;
//   mounts.mount(clientA);               // -> "/sensor1"
//   mounts.mount(clientB);               // -> "/sensor2"
//   File f = mounts.open("/sensor1/log.txt");

#include <map>

#include "Logger.h"
#include "RemoteFS.h"

namespace tinyblefs {

/// Registry of remote servers mounted under /<name>: scans, mounts and opens files by path, copies files between servers
class Mounts {
 public:
  /// Mounts the server behind the client. Returns the mount name, or an empty
  /// string when the server could not be reached.
  std::string mount(FileClient& client, const char* alias = nullptr) {
    ServerInfo si;
    if (client.info(si) != Status::Ok) {
      TINYBLEFS_LOGW("mounts", "server does not answer to info request");
      return "";
    }
    std::string base = sanitize(alias && *alias ? alias : si.name.c_str());
    std::string name = base;
    for (int i = 2; fs_.count(name); i++) name = base + "-" + std::to_string(i);
    fs_.emplace(name, RemoteFS(client));
    TINYBLEFS_LOGI("mounts", "mounted '%s' as /%s%s", si.name.c_str(), name.c_str(),
                si.readOnly ? " (read-only)" : "");
    return name;
  }
  /// Removes a server from the mounts
  bool unmount(const char* name) {
    if (fs_.erase(name) > 0) return true;
    TINYBLEFS_LOGW("mounts", "unmount: nothing is mounted as /%s", name ? name : "");
    return false;
  }
  /// Returns the file system mounted under the name (or nullptr)
  RemoteFS* get(const char* name) {
    auto it = fs_.find(name);
    return it == fs_.end() ? nullptr : &it->second;
  }
  /// Names of all mounted servers
  std::vector<std::string> names() const {
    std::vector<std::string> v;
    for (auto& kv : fs_) v.push_back(kv.first);
    return v;
  }

  // ---- file API using "/<mount>/<path>" -------------------------------
  /// Opens a file or directory by path (/name/...); mode: "r", "w" or "a"
  RemoteFile open(const char* path, const char* mode = "r") {
    std::string rest;
    RemoteFS* f = route(path, rest);
    return f ? f->open(rest.c_str(), mode) : RemoteFile();
  }
  /// open() that waits while the file is locked by another client (see
  /// RemoteFS::openWait)
  RemoteFile openWait(const char* path, const char* mode = "r",
                      uint32_t timeoutMs = 5000, uint32_t retryMs = 100) {
    std::string rest;
    RemoteFS* f = route(path, rest);
    return f ? f->openWait(rest.c_str(), mode, timeoutMs, retryMs) : RemoteFile();
  }
  /// True if the path exists
  bool exists(const char* path) {
    std::string rest;
    RemoteFS* f = route(path, rest);
    return f && f->exists(rest.c_str());
  }
  /// Type and size of a file or directory; "/" is the (virtual) directory of
  /// all mounts
  bool stat(const char* path, bool& isDir, uint32_t& size) {
    if (path && !strcmp(path, "/")) {
      isDir = true;
      size = 0;
      return true;
    }
    std::string rest;
    RemoteFS* f = route(path, rest);
    return f && f->stat(rest.c_str(), isDir, size);
  }
  /// Lists a directory without opening the entries; "/" lists the mounts
  bool list(const char* path, std::vector<DirEntry>& entries) {
    if (path && !strcmp(path, "/")) {
      entries.clear();
      for (auto& kv : fs_) {
        DirEntry e;
        e.name = kv.first;
        e.isDir = true;
        entries.push_back(e);
      }
      return true;
    }
    std::string rest;
    RemoteFS* f = route(path, rest);
    return f && f->list(rest.c_str(), entries);
  }
  /// Deletes a file
  bool remove(const char* path) {
    std::string rest;
    RemoteFS* f = route(path, rest);
    return f && f->remove(rest.c_str());
  }
  /// Creates a directory
  bool mkdir(const char* path) {
    std::string rest;
    RemoteFS* f = route(path, rest);
    return f && f->mkdir(rest.c_str());
  }
  /// Removes an empty directory
  bool rmdir(const char* path) {
    std::string rest;
    RemoteFS* f = route(path, rest);
    return f && f->rmdir(rest.c_str());
  }
  /// Both paths must be on the same server
  bool rename(const char* from, const char* to) {
    std::string r1, r2;
    RemoteFS* f1 = route(from, r1);
    RemoteFS* f2 = route(to, r2);
    if (f1 && f2 && f1 != f2)
      TINYBLEFS_LOGW("mounts", "rename %s to %s: paths are on different servers", from, to);
    return f1 && f1 == f2 && f1->rename(r1.c_str(), r2.c_str());
  }

  /// Copies a file, or a whole directory tree, to another path. The paths may
  /// be on the same or on different servers. An existing destination file is
  /// overwritten. Returns false on the first error.
  bool copy(const char* from, const char* to) {
    RemoteFile src = open(from);
    if (!src) {
      TINYBLEFS_LOGW("mounts", "copy: cannot open source %s", from);
      return false;
    }
    if (!src.isDirectory()) {
      RemoteFile dst = open(to, "w");
      if (!dst) {
        TINYBLEFS_LOGW("mounts", "copy: cannot create destination %s", to);
        return false;
      }
      return copyFile(src, dst);
    }
    // directory: create target (if missing) and copy the entries
    std::string toDir = to;
    std::string fromDir = std::string(from) + "/";
    if ((toDir + "/").compare(0, fromDir.size(), fromDir) == 0) {
      TINYBLEFS_LOGW("mounts", "copy: cannot copy %s into itself (%s)", from, to);
      return false;
    }
    if (!exists(to) && !mkdir(to)) {
      TINYBLEFS_LOGE("mounts", "copy: cannot create directory %s", to);
      return false;
    }
    if (toDir.back() != '/') toDir += '/';
    src.close();
    std::vector<DirEntry> entries;
    if (!list(from, entries)) {
      TINYBLEFS_LOGW("mounts", "copy: cannot list %s", from);
      return false;
    }
    for (const DirEntry& e : entries) {
      std::string fromPath = std::string(from) + (from[strlen(from) - 1] == '/' ? "" : "/") + e.name;
      if (!copy(fromPath.c_str(), (toDir + e.name).c_str())) return false;
    }
    return true;
  }

 private:
  static std::string sanitize(const char* n) {
    std::string s;
    for (; *n && s.size() < 16; n++) {
      char c = *n;
      bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') || c == '-' || c == '_';
      s += ok ? c : '_';
    }
    return s.empty() ? "server" : s;
  }

  /// Splits "/name/rest" into the file system and "/rest"
  RemoteFS* route(const char* path, std::string& rest) {
    std::string p(path ? path : "");
    if (p.empty() || p[0] != '/') {
      TINYBLEFS_LOGW("mounts", "path '%s' must start with /<mount>", path ? path : "");
      return nullptr;
    }
    size_t end = p.find('/', 1);
    std::string name = p.substr(1, end == std::string::npos ? end : end - 1);
    rest = end == std::string::npos ? "/" : p.substr(end);
    RemoteFS* fs = get(name.c_str());
    if (!fs) TINYBLEFS_LOGW("mounts", "no server mounted as /%s", name.c_str());
    return fs;
  }

  std::map<std::string, RemoteFS> fs_;
};

}  // namespace tinyblefs
