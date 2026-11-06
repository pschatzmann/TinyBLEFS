#pragma once
// Internal: serves a file system over ONE point-to-point link (the virtual link
// of a session, see Server.h). Applications use FileServer.

#include <string.h>

#include "FsBackend.h"
#include "Locks.h"
#include "Logger.h"
#include "Protocol.h"
#include "Transport.h"

namespace tinyblefs {
namespace detail {

class LinkServer {
 public:
  /// @param fs         file system to share
  /// @param transport  link to the client
  /// @param name       name announced to clients (mount name)
  /// @param root       directory of fs that is exposed as "/" (path jail)
  /// @param maxHandles maximum number of simultaneously open files
  LinkServer(FsBackend& fs, Transport& transport, const char* name,
             const char* root = "/", size_t maxHandles = 4)
      : fs_(fs), t_(transport), name_(name), slots_(maxHandles) {
    setRoot(root);
  }
  ~LinkServer() { closeAll(); }

  void setReadOnly(bool ro) { read_only_ = ro; }
  /// Largest answer to a batched directory listing (default 2048 bytes)
  void setMaxListBytes(size_t n) { max_list_bytes_ = n < 32 ? 32 : n; }
  bool isReadOnly() const { return read_only_; }
  const char* name() const { return name_.c_str(); }

  /// Processes all pending requests; returns true if something was handled
  bool poll() {
    t_.poll();
    if (!t_.connected()) {
      if (was_connected_) onDisconnect();
      return false;
    }
    if (!was_connected_) TINYBLEFS_LOGI("server", "%s: client connected", name_.c_str());
    was_connected_ = true;
    bool any = false;
    Message req;
    while (rx_.poll(t_, req)) {
      handle(req);
      any = true;
    }
    return any;
  }

  /// Uses a lock table shared with other servers (one table for all
  /// sessions of a FileServer). Call before the first file is opened.
  void setLocks(FileLocks* locks) { locks_ = locks ? locks : &own_locks_; }

  /// Closes all open files and drops partial messages (e.g. on disconnect)
  void closeAll() {
    for (auto& s : slots_) releaseSlot(s);
  }

 private:
  struct Slot {
    FsFile* file = nullptr;
    uint8_t flags = 0;
    std::string path;  // for the lock table
    bool write = false;
  };

  /// Closes the file of a slot and releases its lock
  void releaseSlot(Slot& s) {
    if (!s.file) return;
    delete s.file;
    s.file = nullptr;
    locks_->release(s.path, s.write);
    s.path.clear();
  }

  void onDisconnect() {
    TINYBLEFS_LOGI("server", "%s: client disconnected", name_.c_str());
    was_connected_ = false;
    closeAll();
    rx_.reset();
  }

  void setRoot(const char* root) {
    std::string r;
    std::string norm;
    if (resolveRelative(root ? root : "/", norm)) r = norm;
    else TINYBLEFS_LOGE("server", "invalid root directory '%s' (contains ..): using /", root);
    root_ = "/" + r;
    if (root_.size() > 1 && root_.back() == '/') root_.pop_back();
  }

  /// Normalises a path and rejects "..": result has no leading slash
  static bool resolveRelative(const std::string& p, std::string& out) {
    out.clear();
    size_t i = 0;
    while (i <= p.size()) {
      size_t j = p.find('/', i);
      if (j == std::string::npos) j = p.size();
      std::string seg = p.substr(i, j - i);
      if (seg == "..") return false;
      if (!seg.empty() && seg != ".") {
        if (!out.empty()) out += '/';
        out += seg;
      }
      i = j + 1;
    }
    return true;
  }

  /// Maps a client path to a path of the backend (below the root)
  bool resolve(const std::string& p, std::string& out) {
    std::string rel;
    if (!resolveRelative(p, rel)) return false;
    out = root_;
    if (!rel.empty()) {
      if (out.back() != '/') out += '/';
      out += rel;
    }
    return true;
  }

  static std::string str(const std::vector<uint8_t>& v, size_t from) {
    return from >= v.size() ? std::string()
                            : std::string((const char*)&v[from], v.size() - from);
  }

  void handle(const Message& req) {
    Message rsp;
    rsp.op = req.op | kResponseFlag;
    rsp.reqId = req.reqId;
    rsp.status = (uint8_t)dispatch((Op)req.op, req.payload, rsp.payload);
    // "not found", end of a listing and "is a directory" are normal answers
    Status st = (Status)rsp.status;
    if (st == Status::Ok || st == Status::NotFound || st == Status::EndOfList ||
        st == Status::IsDirectory || st == Status::Busy)
      TINYBLEFS_LOGD("server", "%s -> %s (%u bytes)", opName(req.op), statusName(st),
                  (unsigned)rsp.payload.size());
    else
      TINYBLEFS_LOGW("server", "%s failed: %s", opName(req.op), statusName(st));
    if (rsp.status != (uint8_t)Status::Ok) rsp.payload.clear();
    if (!sendMessage(t_, rsp))
      TINYBLEFS_LOGE("server", "cannot send response to %s", opName(req.op));
  }

  Slot* slot(const std::vector<uint8_t>& in) {
    if (in.empty() || in[0] >= slots_.size() || !slots_[in[0]].file)
      return nullptr;
    return &slots_[in[0]];
  }

  Status dispatch(Op op, const std::vector<uint8_t>& in,
                  std::vector<uint8_t>& out) {
    std::string path, path2;
    FsEntry e;
    switch (op) {
      case Op::Info:
        out.push_back(kProtocolVersion);
        out.push_back((uint8_t)slots_.size());
        out.push_back(read_only_ ? 1 : 0);
        putStr(out, name_);
        return Status::Ok;

      case Op::Open: {
        if (in.size() < 2) return badRequest("open: message too short");
        if (!resolve(str(in, 1), path)) return badPath("open", str(in, 1));
        uint8_t flags = in[0];
        bool wr = flags & (kWrite | kAppend | kCreate | kTrunc);
        if (wr && read_only_) return denied("open for writing", path);
        bool exists = fs_.stat(path.c_str(), e);
        if (exists && e.isDir) return Status::IsDirectory;
        if (!exists && !(flags & (kCreate | kTrunc))) return Status::NotFound;
        Slot* free_slot = nullptr;
        uint8_t handle = 0;
        for (size_t i = 0; i < slots_.size(); i++) {
          if (!slots_[i].file) {
            free_slot = &slots_[i];
            handle = (uint8_t)i;
            break;
          }
        }
        if (!free_slot) {
          TINYBLEFS_LOGW("server", "open %s: all %u file handles are in use",
                      path.c_str(), (unsigned)slots_.size());
          return Status::NoHandle;
        }
        bool truncate = flags & kTrunc;
        if (!locks_->acquire(path, wr, truncate)) return busy("open", path);
        FsFile* f = fs_.open(path.c_str(), flags);
        if (!f) {
          locks_->release(path, wr);
          TINYBLEFS_LOGE("server", "cannot open %s", path.c_str());
          return Status::IoError;
        }
        free_slot->file = f;
        free_slot->flags = flags;
        free_slot->path = path;
        free_slot->write = wr;
        out.push_back(handle);
        put32(out, f->size());
        return Status::Ok;
      }

      case Op::Close: {
        Slot* s = slot(in);
        if (!s) return badHandle("close", in);
        releaseSlot(*s);
        return Status::Ok;
      }

      case Op::Read: {
        Slot* s = slot(in);
        if (!s) return badHandle("read", in);
        if (in.size() < 7) return badRequest("read: message too short");
        size_t len = get16(&in[5]);
        out.resize(len);
        int n = len ? s->file->read(get32(&in[1]), out.data(), len) : 0;
        if (n < 0) {
          TINYBLEFS_LOGE("server", "read error at offset %u", (unsigned)get32(&in[1]));
          return Status::IoError;
        }
        out.resize(n);
        return Status::Ok;
      }

      case Op::Write: {
        Slot* s = slot(in);
        if (!s) return badHandle("write", in);
        if (in.size() < 5) return badRequest("write: message too short");
        if (read_only_ || !(s->flags & (kWrite | kAppend | kCreate | kTrunc)))
          return denied("write to a file opened read-only or on a read-only server", "");
        size_t len = in.size() - 5;
        int n = len ? s->file->write(get32(&in[1]), &in[5], len) : 0;
        if (n < 0) {
          TINYBLEFS_LOGE("server", "write error at offset %u", (unsigned)get32(&in[1]));
          return Status::IoError;
        }
        if ((size_t)n < len)
          TINYBLEFS_LOGW("server", "short write: %d of %u bytes (file system full?)", n,
                      (unsigned)len);
        put16(out, (uint16_t)n);
        return Status::Ok;
      }

      case Op::Stat:
        if (!resolve(str(in, 0), path)) return badPath("stat", str(in, 0));
        if (!fs_.stat(path.c_str(), e)) return Status::NotFound;
        out.push_back(e.isDir ? 1 : 0);
        put32(out, e.size);
        return Status::Ok;

      case Op::List:
        if (in.size() < 2) return badRequest("list: message too short");
        if (!resolve(str(in, 2), path)) return badPath("list", str(in, 2));
        if (!fs_.list(path.c_str(), get16(&in[0]), e))
          return Status::EndOfList;
        out.push_back(e.isDir ? 1 : 0);
        put32(out, e.size);
        putStr(out, e.name);
        return Status::Ok;

      case Op::ListBatch: {
        if (in.size() < 4) return badRequest("list: message too short");
        if (!resolve(str(in, 4), path)) return badPath("list", str(in, 4));
        uint32_t index = get16(&in[0]);
        size_t budget = get16(&in[2]);
        if (budget > max_list_bytes_) budget = max_list_bytes_;
        out.push_back(0);  // flags, set below
        bool end = false;
        while (true) {
          if (!fs_.list(path.c_str(), index, e)) {
            end = true;
            break;
          }
          if (e.name.size() > 255) e.name.resize(255);
          size_t need = 6 + e.name.size();
          // always return at least one entry, else a small budget would never progress
          if (out.size() > 1 && out.size() + need > budget) break;
          out.push_back(e.isDir ? 1 : 0);
          put32(out, e.size);
          out.push_back((uint8_t)e.name.size());
          putStr(out, e.name);
          index++;
        }
        out[0] = end ? kListEnd : 0;
        return Status::Ok;
      }

      case Op::Remove:
      case Op::Mkdir:
      case Op::Rmdir:
        if (!resolve(str(in, 0), path)) return badPath(opName((uint8_t)op), str(in, 0));
        if (read_only_) return denied(opName((uint8_t)op), path);
        if (op == Op::Mkdir) {
          if (fs_.stat(path.c_str(), e)) return Status::Exists;
          if (fs_.mkdir(path.c_str())) return Status::Ok;
          TINYBLEFS_LOGE("server", "mkdir %s failed", path.c_str());
          return Status::IoError;
        }
        if (!fs_.stat(path.c_str(), e)) return Status::NotFound;
        if (locks_->isBusy(path)) return busy(opName((uint8_t)op), path);
        if (op == Op::Remove) {
          if (e.isDir) {
            TINYBLEFS_LOGW("server", "remove %s: is a directory (use rmdir)", path.c_str());
            return Status::Error;
          }
          if (fs_.remove(path.c_str())) return Status::Ok;
          TINYBLEFS_LOGE("server", "remove %s failed", path.c_str());
          return Status::Error;
        }
        if (!e.isDir) {
          TINYBLEFS_LOGW("server", "rmdir %s: not a directory", path.c_str());
          return Status::Error;
        }
        if (fs_.rmdir(path.c_str())) return Status::Ok;
        TINYBLEFS_LOGW("server", "rmdir %s failed (directory not empty?)", path.c_str());
        return Status::Error;

      case Op::Rename: {
        if (in.size() < 2) return badRequest("rename: message too short");
        size_t n = get16(&in[0]);
        if (in.size() < 2 + n) return badRequest("rename: message too short");
        std::string from((const char*)&in[2], n);
        if (!resolve(from, path)) return badPath("rename", from);
        if (!resolve(str(in, 2 + n), path2)) return badPath("rename", str(in, 2 + n));
        if (read_only_) return denied("rename", path);
        if (!fs_.stat(path.c_str(), e)) return Status::NotFound;
        if (locks_->isBusy(path)) return busy("rename", path);
        if (fs_.stat(path2.c_str(), e)) return Status::Exists;
        if (fs_.rename(path.c_str(), path2.c_str())) return Status::Ok;
        TINYBLEFS_LOGE("server", "rename %s to %s failed", path.c_str(), path2.c_str());
        return Status::IoError;
      }
    }
    return badRequest("unknown operation");
  }

  // ---- logged error answers ---------------------------------------------
  Status badRequest(const char* why) {
    TINYBLEFS_LOGW("server", "bad request: %s", why);
    return Status::BadRequest;
  }
  Status badPath(const char* op, const std::string& p) {
    TINYBLEFS_LOGW("server", "%s: path '%s' leaves the shared directory", op, p.c_str());
    return Status::BadRequest;
  }
  Status badHandle(const char* op, const std::vector<uint8_t>& in) {
    TINYBLEFS_LOGW("server", "%s: invalid file handle %d", op, in.empty() ? -1 : (int)in[0]);
    return Status::NoHandle;
  }
  Status busy(const char* op, const std::string& path) {
    TINYBLEFS_LOGD("server", "%s %s refused: busy (open by a client)", op, path.c_str());
    return Status::Busy;
  }
  Status denied(const char* what, const std::string& path) {
    TINYBLEFS_LOGW("server", "%s %s denied: read-only", what, path.c_str());
    return Status::Denied;
  }

  FsBackend& fs_;
  Transport& t_;
  Receiver rx_;
  std::string name_;
  std::string root_ = "/";
  FileLocks own_locks_;
  FileLocks* locks_ = &own_locks_;
  std::vector<Slot> slots_;
  size_t max_list_bytes_ = 2048;  // largest ListBatch answer (RAM of the server)
  bool read_only_ = false;
  bool was_connected_ = false;
};

}  // namespace detail
}  // namespace tinyblefs
