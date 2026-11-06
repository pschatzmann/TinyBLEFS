#pragma once
// The server: shares a file system over a Transport (see FileServer below).

#include <string.h>

#include <deque>
#include <map>
#include <memory>

#include "LinkServer.h"
#include "Logger.h"
#include "Platform.h"
#include "Protocol.h"
#include "Session.h"
#include "Transport.h"

namespace tinyblefs {

/// Serves a file system (any FsBackend, given in the constructor) over a
/// Transport. Call poll() regularly from loop().
///
///   ArduinoFsBackend<fs::LittleFSFS> backend(LittleFS);
///   FileServer server(backend, transport, "sensor1");        // one client
///   FileServer server(backend, transport, "sensor1", "/", 3); // up to 3 clients
///   void loop() { server.poll(); }
///
/// Every client works in its own session (own open files); the clients share
/// the file system and its locks. maxSessions limits the clients at the same
/// time (a further client is refused); with 1 the server serves one client
/// at a time.
class FileServer {
 public:
  /// @param fs          file system to share
  /// @param raw         the BLE adapter
  /// @param name        mount name announced to clients
  /// @param root        directory of fs shown to clients as "/" (path jail)
  /// @param maxSessions clients at the same time (default 1)
  /// @param maxHandles  open files per session (default 4)
  FileServer(FsBackend& fs, Transport& raw, const char* name,
             const char* root = "/", size_t maxSessions = 1,
             size_t maxHandles = 4)
      : fs_(fs), raw_(raw), name_(name), root_(root ? root : "/"),
        max_sessions_(maxSessions < 1 ? 1 : maxSessions), max_handles_(maxHandles),
        buf_(TINYBLEFS_SESSION_PACKET) {}

  /// Refuses all changes when true
  void setReadOnly(bool ro) {
    read_only_ = ro;
    for (auto& kv : sessions_) kv.second->server.setReadOnly(ro);
  }
  /// True when changes are refused
  bool isReadOnly() const { return read_only_; }
  /// Largest answer to a batched directory listing (default 2048 bytes)
  void setMaxListBytes(size_t n) {
    max_list_bytes_ = n;
    for (auto& kv : sessions_) kv.second->server.setMaxListBytes(n);
  }
  /// A session without any packet for this long is closed (default 60 s).
  /// Clients call FileClient::keepAlive() when they are idle.
  void setSessionTimeout(uint32_t ms) { timeout_ = ms; }
  /// Clients connected right now
  size_t sessionCount() const { return sessions_.size(); }
  /// Name announced to the clients (mount name)
  const char* name() const { return name_.c_str(); }
  /// Closes all sessions (and their open files)
  void closeAll() { sessions_.clear(); }

  /// Processes all pending packets and requests; returns true if any
  bool poll() {
    raw_.poll();
    if (!raw_.connected()) {
      if (!sessions_.empty()) {
        TINYBLEFS_LOGI("session", "link down: closing %u session(s)", (unsigned)sessions_.size());
        sessions_.clear();
      }
      return false;
    }
    bool any = false;
    size_t n;
    while ((n = raw_.receive(buf_.data(), buf_.size())) > 0) {
      any = true;
      route(buf_.data(), n);
    }
    uint32_t now = nowMs();
    for (auto it = sessions_.begin(); it != sessions_.end();) {
      if (now - it->second->lastSeen > timeout_) {
        TINYBLEFS_LOGI("session", "session %u expired (no packets for %u ms)",
                    (unsigned)it->first, (unsigned)timeout_);
        it = sessions_.erase(it);
      } else {
        if (it->second->server.poll()) any = true;
        ++it;
      }
    }
    return any;
  }

 private:
  /// Virtual point-to-point link of one session
  class Link : public Transport {
   public:
    Link(Transport& raw, uint8_t sid) : raw_(raw), sid_(sid) {}
    size_t mtu() const override {
      size_t m = raw_.mtu();
      return m > 1 ? m - 1 : 0;
    }
    bool send(const uint8_t* d, size_t n) override {
      std::vector<uint8_t> p;
      p.reserve(n + 1);
      p.push_back(sid_);
      p.insert(p.end(), d, d + n);
      return raw_.send(p.data(), p.size());
    }
    size_t receive(uint8_t* buf, size_t max) override {
      if (q_.empty()) return 0;
      size_t n = q_.front().size() < max ? q_.front().size() : max;
      memcpy(buf, q_.front().data(), n);
      q_.pop_front();
      return n;
    }
    bool connected() const override { return true; }
    void push(const uint8_t* d, size_t n) {
      if (q_.size() < 256) q_.emplace_back(d, d + n);
      else TINYBLEFS_LOGW("session", "session %u: receive queue full, packet dropped", (unsigned)sid_);
    }

   private:
    Transport& raw_;
    uint8_t sid_;
    std::deque<std::vector<uint8_t>> q_;
  };

  /// State of one client session (open files, message buffer)
  struct Session {
    Session(FsBackend& fs, Transport& raw, uint8_t sid, uint32_t nonce_,
            const std::string& name, const std::string& root, size_t handles,
            FileLocks* locks)
        : link(raw, sid), server(fs, link, name.c_str(), root.c_str(), handles),
          nonce(nonce_), lastSeen(nowMs()) {
      server.setLocks(locks);
    }
    Link link;  // must be declared before the server that uses it
    detail::LinkServer server;
    uint32_t nonce;
    uint32_t lastSeen;
  };

  void route(const uint8_t* p, size_t n) {
    if (p[0] == 0) {
      control(p, n);
      return;
    }
    auto it = sessions_.find(p[0]);
    if (it == sessions_.end()) {
      TINYBLEFS_LOGD("session", "packet for unknown session %u", (unsigned)p[0]);
      sendControl(kSessionExpired, p[0]);
      return;
    }
    it->second->lastSeen = nowMs();
    if (n > 1) it->second->link.push(p + 1, n - 1);
  }

  void control(const uint8_t* p, size_t n) {
    if (n < 2) return;
    if (p[1] == kSessionHello && n >= 6) {
      uint32_t nonce = get32(&p[2]);
      for (auto& kv : sessions_) {  // repeated hello: same client, same session
        if (kv.second->nonce == nonce) {
          kv.second->lastSeen = nowMs();
          sendWelcome(nonce, kv.first);
          return;
        }
      }
      if (sessions_.size() >= max_sessions_) {
        TINYBLEFS_LOGW("session", "no free session (%u sessions): hello refused", (unsigned)max_sessions_);
        std::vector<uint8_t> r{0, kSessionFull};
        put32(r, nonce);
        raw_.send(r.data(), r.size());
        return;
      }
      uint8_t sid = nextId();
      auto s = std::make_unique<Session>(fs_, raw_, sid, nonce, name_, root_, max_handles_, &locks_);
      s->server.setReadOnly(read_only_);
      s->server.setMaxListBytes(max_list_bytes_);
      sessions_[sid] = std::move(s);
      TINYBLEFS_LOGI("session", "session %u opened (%u of %u)", (unsigned)sid,
                  (unsigned)sessions_.size(), (unsigned)max_sessions_);
      sendWelcome(nonce, sid);
    } else if (p[1] == kSessionKeepAlive && n >= 3) {
      auto it = sessions_.find(p[2]);
      if (it != sessions_.end()) it->second->lastSeen = nowMs();
      else sendControl(kSessionExpired, p[2]);
    }
  }

  /// Ids are handed out round robin so that an id is not reused soon after a
  /// session ended (a stale client could otherwise end up in a foreign session)
  uint8_t nextId() {
    for (int i = 0; i < 255; i++) {
      last_id_ = (uint8_t)(last_id_ % 255 + 1);
      if (!sessions_.count(last_id_)) return last_id_;
    }
    return 0;  // not reached: size < max_sessions_ <= 255
  }

  void sendWelcome(uint32_t nonce, uint8_t sid) {
    std::vector<uint8_t> r{0, kSessionWelcome};
    put32(r, nonce);
    r.push_back(sid);
    if (!raw_.send(r.data(), r.size()))
      TINYBLEFS_LOGW("session", "cannot send welcome for session %u", (unsigned)sid);
  }
  void sendControl(uint8_t type, uint8_t sid) {
    uint8_t r[3] = {0, type, sid};
    raw_.send(r, sizeof(r));
  }

  FsBackend& fs_;
  Transport& raw_;
  std::string name_, root_;
  size_t max_sessions_, max_handles_;
  std::vector<uint8_t> buf_;
  FileLocks locks_;  // shared by all sessions; declared before them: outlives them
  std::map<uint8_t, std::unique_ptr<Session>> sessions_;
  size_t max_list_bytes_ = 2048;
  uint32_t timeout_ = 60000;
  uint8_t last_id_ = 0;
  bool read_only_ = false;
};

}  // namespace tinyblefs
