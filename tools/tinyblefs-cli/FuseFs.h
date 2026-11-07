#pragma once
// FUSE (libfuse3) operations on top of a tinyblefs::Mounts object. No BLE code
// in here, so it can be tested over a loopback transport (tests/test_fuse.cpp).
//
// The root directory lists the mounted servers, every server is a subdirectory.
// The protocol has no permissions, owners or time stamps: fixed values are
// reported, chmod / chown / utimens are accepted and ignored. There is no
// truncate request: truncating to 0 (or to the current size) works, other
// sizes fail with ENOTSUP. FUSE must run single threaded (the client allows
// one request in flight); fuse_main() is called with -s.

#ifndef FUSE_USE_VERSION
#define FUSE_USE_VERSION 31
#endif
#include <fuse.h>

#include <cerrno>
#include <cstring>
#include <ctime>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <unistd.h>

#include "TinyBLEFS.h"

namespace tinyblefs {

class FuseFs {
 public:
  explicit FuseFs(Mounts& mounts) : m_(mounts), start_(time(nullptr)) {}

  /// The operations table; pass a FuseFs* as user_data to fuse_main()
  static const fuse_operations& operations() {
    static const fuse_operations ops = make();
    return ops;
  }

 private:
  struct Handle {
    RemoteFile file;
    std::string path;
  };

  static FuseFs& self() { return *static_cast<FuseFs*>(fuse_get_context()->private_data); }

  static fuse_operations make() {
    fuse_operations o;
    memset(&o, 0, sizeof(o));
    o.init = [](fuse_conn_info*, fuse_config* cfg) -> void* {
      cfg->attr_timeout = 2.0;
      cfg->entry_timeout = 2.0;
      cfg->negative_timeout = 1.0;
      cfg->use_ino = 0;
      return fuse_get_context()->private_data;
    };
    o.getattr = [](const char* p, struct stat* st, fuse_file_info*) { return self().getattr(p, st); };
    o.readdir = [](const char* p, void* buf, fuse_fill_dir_t fill, off_t, fuse_file_info*,
                   fuse_readdir_flags) { return self().readdir(p, buf, fill); };
    o.open = [](const char* p, fuse_file_info* fi) { return self().open(p, fi); };
    o.create = [](const char* p, mode_t, fuse_file_info* fi) { return self().create(p, fi); };
    o.read = [](const char*, char* buf, size_t n, off_t off, fuse_file_info* fi) {
      return self().read(buf, n, off, fi);
    };
    o.write = [](const char*, const char* buf, size_t n, off_t off, fuse_file_info* fi) {
      return self().write(buf, n, off, fi);
    };
    o.release = [](const char*, fuse_file_info* fi) { return self().release(fi); };
    o.flush = [](const char*, fuse_file_info*) { return 0; };  // writes are synchronous
    o.fsync = [](const char*, int, fuse_file_info*) { return 0; };
    o.truncate = [](const char* p, off_t size, fuse_file_info* fi) { return self().truncate(p, size, fi); };
    o.unlink = [](const char* p) { return self().unlink(p); };
    o.mkdir = [](const char* p, mode_t) { return self().mkdir(p); };
    o.rmdir = [](const char* p) { return self().rmdir(p); };
    o.rename = [](const char* a, const char* b, unsigned flags) { return self().rename(a, b, flags); };
    o.statfs = [](const char*, struct statvfs* s) {
      memset(s, 0, sizeof(*s));
      s->f_bsize = s->f_frsize = 512;
      s->f_namemax = 255;
      return 0;
    };
    // not supported by the protocol: accept silently so that touch, cp -p, tar ... work
    o.chmod = [](const char*, mode_t, fuse_file_info*) { return 0; };
    o.chown = [](const char*, uid_t, gid_t, fuse_file_info*) { return 0; };
    o.utimens = [](const char*, const timespec[2], fuse_file_info*) { return 0; };
    return o;
  }

  // ---- helpers ---------------------------------------------------------
  bool statPath(const char* path, bool& isDir, uint32_t& size) { return m_.stat(path, isDir, size); }

  void fill(struct stat* st, bool isDir, uint32_t size) {
    memset(st, 0, sizeof(*st));
    st->st_mode = isDir ? (S_IFDIR | 0755) : (S_IFREG | 0644);
    st->st_nlink = isDir ? 2 : 1;
    st->st_size = isDir ? 0 : size;
    st->st_blksize = 512;
    st->st_blocks = (st->st_size + 511) / 512;
    st->st_uid = getuid();
    st->st_gid = getgid();
    st->st_atime = st->st_mtime = st->st_ctime = start_;
  }

  // ---- operations ------------------------------------------------------
  int getattr(const char* path, struct stat* st) {
    bool isDir = false;
    uint32_t size = 0;
    if (!statPath(path, isDir, size)) return -ENOENT;
    fill(st, isDir, size);
    return 0;
  }

  int readdir(const char* path, void* buf, fuse_fill_dir_t filler) {
    bool isDir = false;
    uint32_t size = 0;
    if (!statPath(path, isDir, size)) return -ENOENT;
    if (!isDir) return -ENOTDIR;
    std::vector<DirEntry> entries;
    if (!m_.list(path, entries)) return -EIO;
    filler(buf, ".", nullptr, 0, (fuse_fill_dir_flags)0);
    filler(buf, "..", nullptr, 0, (fuse_fill_dir_flags)0);
    for (const DirEntry& e : entries) {
      struct stat st;
      fill(&st, e.isDir, e.size);
      if (filler(buf, e.name.c_str(), &st, 0, FUSE_FILL_DIR_PLUS)) break;  // buffer full
    }
    return 0;
  }

  // The handle is kept as long as the file is open (the server has a limit of
  // open files: EMFILE when it is reached)
  int store(RemoteFile&& f, const char* path, fuse_file_info* fi) {
    uint64_t id = ++lastId_;
    handles_[id] = Handle{std::move(f), path};
    fi->fh = id;
    return 0;
  }

  int open(const char* path, fuse_file_info* fi) {
    bool isDir = false;
    uint32_t size = 0;
    if (!statPath(path, isDir, size)) return -ENOENT;
    if (isDir) return -EISDIR;
    int acc = fi->flags & O_ACCMODE;
    // O_TRUNC: libfuse passes it to open() (atomic_o_trunc)
    if ((fi->flags & O_TRUNC) && acc != O_RDONLY && size > 0) {
      RemoteFile t = m_.open(path, "w");  // truncates
      if (!t) return -EACCES;
    }
    RemoteFile f = m_.open(path, acc == O_RDONLY ? "r" : "r+");
    if (!f) return acc == O_RDONLY ? -EIO : -EACCES;
    return store(std::move(f), path, fi);
  }

  int create(const char* path, fuse_file_info* fi) {
    bool isDir = false;
    uint32_t size = 0;
    if (statPath(path, isDir, size)) return isDir ? -EISDIR : -EEXIST;
    if (!parentExists(path)) return -ENOENT;
    RemoteFile f = m_.open(path, "w");  // creates, write only
    if (!f) return -EACCES;             // read-only server, denied, no space ...
    f.close();
    f = m_.open(path, "r+");            // read and write like the caller expects
    if (!f) return -EACCES;
    return store(std::move(f), path, fi);
  }

  int read(char* buf, size_t n, off_t off, fuse_file_info* fi) {
    auto it = handles_.find(fi->fh);
    if (it == handles_.end()) return -EBADF;
    RemoteFile& f = it->second.file;
    if (off < 0) return -EINVAL;
    if ((uint64_t)off >= 0xFFFFFFFFull) return 0;
    if (!f.seek((uint32_t)off)) return -EIO;
    size_t got = f.read((uint8_t*)buf, n);
    if (got == 0 && (uint32_t)off < f.size()) return -EIO;
    return (int)got;
  }

  int write(const char* buf, size_t n, off_t off, fuse_file_info* fi) {
    auto it = handles_.find(fi->fh);
    if (it == handles_.end()) return -EBADF;
    RemoteFile& f = it->second.file;
    if (off < 0 || (uint64_t)off + n > 0xFFFFFFFFull) return -EFBIG;
    if (!f.seek((uint32_t)off)) return -EIO;
    size_t put = f.write((const uint8_t*)buf, n);
    if (put == 0 && n > 0) return -EIO;
    return (int)put;
  }

  int release(fuse_file_info* fi) {
    handles_.erase(fi->fh);  // RemoteFile closes the server handle
    return 0;
  }

  int truncate(const char* path, off_t size, fuse_file_info* fi) {
    bool isDir = false;
    uint32_t cur = 0;
    auto it = fi ? handles_.find(fi->fh) : handles_.end();
    if (it != handles_.end()) {
      cur = it->second.file.size();
    } else {
      if (!statPath(path, isDir, cur)) return -ENOENT;
      if (isDir) return -EISDIR;
    }
    if (size < 0) return -EINVAL;
    if ((uint64_t)size == cur) return 0;
    if (size != 0) return -ENOTSUP;  // the protocol cannot shrink or extend a file
    // truncate to 0: the open mode "w" does it. An open handle is replaced.
    if (it != handles_.end()) it->second.file.close();
    {
      RemoteFile t = m_.open(path, "w");
      if (!t) return -EACCES;
    }
    if (it != handles_.end()) {
      it->second.file = m_.open(path, "r+");
      if (!it->second.file) return -EIO;
    }
    return 0;
  }

  int unlink(const char* path) {
    bool isDir = false;
    uint32_t size = 0;
    if (!statPath(path, isDir, size)) return -ENOENT;
    if (isDir) return -EISDIR;
    return m_.remove(path) ? 0 : -EACCES;
  }

  int mkdir(const char* path) {
    bool isDir = false;
    uint32_t size = 0;
    if (statPath(path, isDir, size)) return -EEXIST;
    if (!parentExists(path)) return -ENOENT;
    return m_.mkdir(path) ? 0 : -EACCES;
  }

  int rmdir(const char* path) {
    bool isDir = false;
    uint32_t size = 0;
    if (!statPath(path, isDir, size)) return -ENOENT;
    if (!isDir) return -ENOTDIR;
    if (m_.rmdir(path)) return 0;
    std::vector<DirEntry> entries;
    if (m_.list(path, entries) && !entries.empty()) return -ENOTEMPTY;
    return -EACCES;
  }

  int rename(const char* from, const char* to, unsigned flags) {
    if (flags) return -EINVAL;  // RENAME_NOREPLACE / RENAME_EXCHANGE
    bool isDir = false;
    uint32_t size = 0;
    if (!statPath(from, isDir, size)) return -ENOENT;
    if (!parentExists(to)) return -ENOENT;
    // POSIX: an existing file is replaced
    bool toDir = false;
    uint32_t toSize = 0;
    if (statPath(to, toDir, toSize) && !toDir && !isDir) m_.remove(to);
    return m_.rename(from, to) ? 0 : -EACCES;
  }

  bool parentExists(const char* path) {
    std::string p(path);
    size_t pos = p.find_last_of('/');
    if (pos == std::string::npos) return false;
    if (pos == 0) return true;
    bool isDir = false;
    uint32_t size = 0;
    return statPath(p.substr(0, pos).c_str(), isDir, size) && isDir;
  }

  Mounts& m_;
  time_t start_;
  uint64_t lastId_ = 0;
  std::map<uint64_t, Handle> handles_;
};

}  // namespace tinyblefs
