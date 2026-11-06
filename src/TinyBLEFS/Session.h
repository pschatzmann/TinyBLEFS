#pragma once
// Sessions: every client works in its own session on the server (own open
// files, own message buffer), so that several clients can share one BLE link,
// also on BLE stacks that can neither tell which client sent a packet nor
// address a reply to one client (ArduinoBLE, Pico W). The session id is the
// first byte of every packet.
//
// Applications do not use this file directly: FileServer(..., maxSessions) and
// FileClient handle the sessions.
//
// All connected clients receive all replies (the stack broadcasts them), so
// they must be trusted with each other's data.
//
// Packet format: [sessionId:1][data...]. Session id 0 is a control packet:
//   [0][1][nonce:4]          Hello       client -> server
//   [0][2][nonce:4][id:1]    Welcome     server -> client
//   [0][3][nonce:4]          Full        server -> client (no free session)
//   [0][4][id:1]             Expired     server -> client (unknown session)
//   [0][5][id:1]             KeepAlive   client -> server

#include <string.h>

#include <vector>

#include "Logger.h"
#include "Platform.h"
#include "Protocol.h"
#include "Transport.h"

#ifndef TINYBLEFS_SESSION_PACKET
#define TINYBLEFS_SESSION_PACKET 520  // largest packet of the shared link
#endif

namespace tinyblefs {

/// Control packet types (session id 0)
enum SessionControl : uint8_t {
  kSessionHello = 1,
  kSessionWelcome = 2,
  kSessionFull = 3,
  kSessionExpired = 4,
  kSessionKeepAlive = 5
};

namespace detail {

/// Client side (internal, owned by FileClient): a point-to-point Transport on
/// top of the shared link, tagging every packet with the session id
class SessionLink : public Transport {
 public:
  /// @param raw       the BLE client adapter (must be connected)
  /// @param timeoutMs how long join() waits for the server
  SessionLink(Transport& raw, uint32_t timeoutMs)
      : raw_(raw), timeout_(timeoutMs), buf_(TINYBLEFS_SESSION_PACKET) {}

  /// Asks the server for a session. Called automatically by the first send()
  /// and again after the server dropped the session.
  bool join() {
    uint32_t nonce = randomU32();
    uint32_t start = nowMs(), lastTry = 0;
    bool sent = false;
    sid_ = 0;
    while (nowMs() - start < timeout_) {
      if (!raw_.connected()) {
        TINYBLEFS_LOGW("session", "join: link is down");
        return false;
      }
      if (!sent || nowMs() - lastTry > 700) {  // the hello may get lost
        std::vector<uint8_t> h{0, kSessionHello};
        put32(h, nonce);
        if (!raw_.send(h.data(), h.size())) TINYBLEFS_LOGW("session", "cannot send hello");
        lastTry = nowMs();
        sent = true;
      }
      if (idle_) idle_(idle_ctx_);
      raw_.poll();
      size_t n;
      while ((n = raw_.receive(buf_.data(), buf_.size())) > 0) {
        if (n >= 6 && buf_[0] == 0 && get32(&buf_[2]) == nonce) {
          if (buf_[1] == kSessionWelcome && n >= 7) {
            sid_ = buf_[6];
            expired_ = false;
            gen_++;  // files of an earlier session are gone
            TINYBLEFS_LOGI("session", "joined as session %u", (unsigned)sid_);
            return true;
          }
          if (buf_[1] == kSessionFull) {
            TINYBLEFS_LOGW("session", "the server has no free session");
            return false;
          }
        }
      }
      yieldCpu();
    }
    TINYBLEFS_LOGW("session", "no answer to hello after %u ms", (unsigned)timeout_);
    return false;
  }

  /// Keeps the session alive while the application is idle (call at least
  /// once per session timeout of the server, 60 s by default)
  void keepAlive() {
    if (!sid_ || expired_) return;
    uint8_t p[3] = {0, kSessionKeepAlive, sid_};
    raw_.send(p, sizeof(p));
  }

  /// Session id (0 = not joined)
  uint8_t id() const { return sid_; }
  /// Sets how long join() waits for the server (ms)
  void setTimeout(uint32_t ms) { timeout_ = ms; }
  /// Called while join() waits for the answer of the server
  void setIdleCallback(void (*cb)(void*), void* ctx) {
    idle_ = cb;
    idle_ctx_ = ctx;
  }
  uint32_t generation() const override { return gen_; }

  // ---- Transport -------------------------------------------------------
  size_t mtu() const override {
    size_t m = raw_.mtu();
    return m > 1 ? m - 1 : 0;
  }
  bool send(const uint8_t* d, size_t n) override {
    if ((!sid_ || expired_) && !join()) return false;
    std::vector<uint8_t> p;
    p.reserve(n + 1);
    p.push_back(sid_);
    p.insert(p.end(), d, d + n);
    return raw_.send(p.data(), p.size());
  }
  size_t receive(uint8_t* out, size_t max) override {
    size_t n;
    while ((n = raw_.receive(buf_.data(), buf_.size())) > 0) {
      if (buf_[0] == 0) {
        if (n >= 3 && buf_[1] == kSessionExpired && buf_[2] == sid_ && sid_) {
          TINYBLEFS_LOGW("session", "session %u was closed by the server (idle too long?)",
                      (unsigned)sid_);
          expired_ = true;
        }
        continue;
      }
      if (buf_[0] != sid_ || n < 2) continue;  // reply for another client
      size_t len = n - 1 < max ? n - 1 : max;
      memcpy(out, buf_.data() + 1, len);
      return len;
    }
    return 0;
  }
  /// False when the link is down or the server dropped the session; the next
  /// request then joins again with a new session (open files are lost)
  bool connected() const override { return raw_.connected() && !expired_; }
  void poll() override { raw_.poll(); }

 private:
  Transport& raw_;
  uint32_t timeout_;
  std::vector<uint8_t> buf_;
  uint8_t sid_ = 0;
  uint32_t gen_ = 0;
  bool expired_ = false;
  void (*idle_)(void*) = nullptr;
  void* idle_ctx_ = nullptr;
};

}  // namespace detail
}  // namespace tinyblefs
