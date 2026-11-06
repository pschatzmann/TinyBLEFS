#pragma once
// Minimal transport abstraction. A BLE adapter (ArduinoBLE, NimBLE, SimpleBLE,
// ...) only has to implement this interface; the rest of the library is BLE
// library agnostic.

#include <algorithm>

#include "Logger.h"
#include "Platform.h"
#include "Protocol.h"

namespace tinyblefs {

/// Packet oriented, reliable, in-order link between one client and one server
class Transport {
 public:
  virtual ~Transport() = default;
  /// Maximum number of bytes per packet (ATT MTU - 3)
  virtual size_t mtu() const = 0;
  /// Sends one packet (<= mtu()); returns false on failure
  virtual bool send(const uint8_t* data, size_t len) = 0;
  /// Non blocking: copies the next received packet and returns its length, or
  /// 0 when nothing is available
  virtual size_t receive(uint8_t* buf, size_t max) = 0;
  /// True while the link is up
  virtual bool connected() const { return true; }
  /// Called regularly by Server::poll() and while a Client waits for a
  /// response. Polled BLE stacks (e.g. ArduinoBLE) run their event processing
  /// here; event driven stacks don't need to override it.
  virtual void poll() {}
  /// Changes whenever the server side state (open files) was lost, e.g. when a
  /// session had to be re-established. Files opened before are then invalid.
  virtual uint32_t generation() const { return 0; }
};

/// Sends a message, fragmenting it into mtu() sized packets
inline bool sendMessage(Transport& t, const Message& m) {
  std::vector<uint8_t> data = encode(m);
  size_t mtu = t.mtu();
  if (mtu == 0) {
    TINYBLEFS_LOGE("transport", "packet size (mtu) is 0: cannot send");
    return false;
  }
  if (m.payload.size() > kMaxPayload) {
    TINYBLEFS_LOGE("transport", "message of %u bytes is too large",
                (unsigned)m.payload.size());
    return false;
  }
  for (size_t off = 0; off < data.size(); off += mtu) {
    size_t n = std::min(mtu, data.size() - off);
    if (!t.send(&data[off], n)) {
      TINYBLEFS_LOGW("transport", "sending packet at offset %u of %u failed",
                  (unsigned)off, (unsigned)data.size());
      return false;
    }
  }
  return true;
}

/// Polls a transport and yields complete messages
class Receiver {
 public:
  /// Receives packets and returns true when a complete message is available
  bool poll(Transport& t, Message& m) {
    // the buffer is a member, not a local: small MCUs have only ~1-4 KB of stack
    if (tmp_.empty()) tmp_.resize(kPacketBuffer);
    while (true) {
      if (asm_.next(m)) return true;
      size_t n = t.receive(tmp_.data(), tmp_.size());
      if (n == 0) return false;
      asm_.feed(tmp_.data(), n);
    }
  }
  /// Discards partial data
  void reset() { asm_.reset(); }

 private:
  static constexpr size_t kPacketBuffer = 1024;  // largest packet a transport delivers
  Reassembler asm_;
  std::vector<uint8_t> tmp_;
};

}  // namespace tinyblefs
