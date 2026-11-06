#pragma once
// Transports for Linux (and other desktop OSes) based on SimpleBLE:
// https://github.com/OpenBluetoothToolbox/SimpleBLE
//
// Link with the SimpleBLE library (target simpleble::simpleble in CMake).
// On Linux this talks to BlueZ via D-Bus, so bluetoothd must be running.
//
// Client:
//   auto found = SimpleBLEClientTransport::scan(5000, "TinyFS");
//   SimpleBLEClientTransport transport;
//   transport.connect(found[0]);
//   FileClient client(transport);
//   Mounts mounts;
//   mounts.mount(client);
//
// Server (shares a directory):
//   StdFilesystemBackend backend;
//   SimpleBLEServerTransport transport;
//   FileServer server(backend, transport, "pc", "/srv/share");
//   transport.begin();
//   while (true) server.poll();

#include <simpleble/SimpleBLE.h>

#include <atomic>
#include <deque>
#include <mutex>

#include "../Logger.h"
#include "../Transport.h"

namespace tinyblefs {

namespace detail {
/// Thread safe packet queue (SimpleBLE calls back from its own threads)
class LockedQueue {
 public:
  /// Adds a packet (thread safe)
  void push(const uint8_t* d, size_t n) {
    std::lock_guard<std::mutex> g(m_);
    if (q_.size() < 64) q_.emplace_back(d, d + n);
    else dropped_++;
  }
  /// Takes the oldest packet; returns its size or 0
  size_t pop(uint8_t* buf, size_t max) {
    std::lock_guard<std::mutex> g(m_);
    if (q_.empty()) return 0;
    size_t n = std::min(max, q_.front().size());
    memcpy(buf, q_.front().data(), n);
    q_.pop_front();
    return n;
  }
  /// Removes all packets
  void clear() {
    std::lock_guard<std::mutex> g(m_);
    q_.clear();
  }
  /// Packets lost because the queue was full, since the last call
  uint32_t takeDropped() {
    std::lock_guard<std::mutex> g(m_);
    uint32_t n = dropped_;
    dropped_ = 0;
    return n;
  }

 private:
  std::mutex m_;
  uint32_t dropped_ = 0;
  std::deque<std::vector<uint8_t>> q_;
};
}  // namespace detail

/// Central side: connects to one TinyBLEFS server
class SimpleBLEClientTransport : public Transport {
 public:
  ~SimpleBLEClientTransport() override { disconnect(); }

  /// Scans for devices that advertise the TinyBLEFS service.
  /// @param namePrefix only return devices whose advertised BLE device name
  ///        starts with this prefix (empty = all)
  static std::vector<SimpleBLE::Peripheral> scan(
      int durationMs = 5000, const std::string& namePrefix = "") {
    std::vector<SimpleBLE::Peripheral> found;
    std::vector<SimpleBLE::Peripheral> results;
    try {
      auto adapters = SimpleBLE::Adapter::get_adapters();
      if (adapters.empty()) {
        TINYBLEFS_LOGE("simpleble", "no Bluetooth adapter found (is bluetoothd running?)");
        return found;
      }
      SimpleBLE::Adapter adapter = adapters[0];
      adapter.scan_for(durationMs);
      results = adapter.scan_get_results();
    } catch (const std::exception& e) {
      TINYBLEFS_LOGE("simpleble", "scan failed: %s", e.what());
      return found;
    }
    for (auto& p : results) {
      bool tinyblefs = false;
      for (auto& s : p.services())
        if (equalsNoCase(s.uuid(), kServiceUUID)) tinyblefs = true;
      if (!tinyblefs) continue;
      if (!namePrefix.empty() && p.identifier().compare(0, namePrefix.size(), namePrefix) != 0)
        continue;
      found.push_back(p);
    }
    return found;
  }

  /// Connects to a device found by scan() and discovers the file service
  bool connect(SimpleBLE::Peripheral peripheral) {
    disconnect();
    try {
      peripheral_ = peripheral;
      peripheral_.connect();
      has_peripheral_ = true;
      TINYBLEFS_LOGI("simpleble", "connected, packets of %u bytes (ATT MTU %u)",
                  (unsigned)mtu(), (unsigned)peripheral_.mtu());
      // after connect() BlueZ has resolved the services and negotiated the MTU
      peripheral_.notify(kServiceUUID, kTxUUID, [this](SimpleBLE::ByteArray payload) {
        q_.push((const uint8_t*)payload.data(), payload.size());
      });
      return true;
    } catch (const std::exception& e) {
      TINYBLEFS_LOGW("simpleble", "connect failed: %s", e.what());
      disconnect();
      return false;
    }
  }

  /// Disconnects from the server
  void disconnect() {
    if (has_peripheral_) {
      try {
        if (peripheral_.is_connected()) peripheral_.disconnect();
      } catch (const std::exception& e) {
        TINYBLEFS_LOGD("simpleble", "disconnect: %s", e.what());
      }
      has_peripheral_ = false;
    }
    q_.clear();
  }

  // ---- Transport -------------------------------------------------------
  size_t mtu() const override {
    if (!has_peripheral_) return 20;
    try {
      uint16_t m = const_cast<SimpleBLE::Peripheral&>(peripheral_).mtu();
      // ATT allows at most 517; BlueZ reports nonsense (65533) for some devices
      if (m > 517) m = 517;
      return m > 23 ? m - 3 : 20;
    } catch (const std::exception& e) {
      TINYBLEFS_LOGD("simpleble", "cannot read the MTU: %s", e.what());
      return 20;
    }
  }
  bool send(const uint8_t* data, size_t len) override {
    if (!has_peripheral_) return false;
    try {
      peripheral_.write_command(kServiceUUID, kRxUUID,
                                SimpleBLE::ByteArray((const char*)data, len));
      return true;
    } catch (const std::exception& e) {
      TINYBLEFS_LOGW("simpleble", "write of %u bytes failed: %s", (unsigned)len, e.what());
      return false;
    }
  }
  size_t receive(uint8_t* buf, size_t max) override {
    uint32_t d = q_.takeDropped();
    if (d) TINYBLEFS_LOGW("simpleble", "%u received packet(s) dropped (queue full or packet too large)", (unsigned)d);
    return q_.pop(buf, max);
  }
  bool connected() const override {
    if (!has_peripheral_) return false;
    try {
      return const_cast<SimpleBLE::Peripheral&>(peripheral_).is_connected();
    } catch (const std::exception& e) {
      TINYBLEFS_LOGD("simpleble", "connection state unknown: %s", e.what());
      return false;
    }
  }

 private:
  static bool equalsNoCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
      if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
        return false;
    return true;
  }

  SimpleBLE::Peripheral peripheral_;
  bool has_peripheral_ = false;
  detail::LockedQueue q_;
};

/// Peripheral side: advertises the TinyBLEFS service via BlueZ (GATT server) and
/// talks to one client.
///
/// SimpleBLE can't set the BLE device name: BlueZ advertises the alias of the
/// adapter. Set it with `bluetoothctl system-alias "TinyFS PC"` (this is the
/// name the client scan prefix is compared with).
class SimpleBLEServerTransport : public Transport {
 public:
  ~SimpleBLEServerTransport() override { end(); }

  /// Creates the TinyBLEFS service and starts advertising
  bool begin() {
    try {
      auto adapters = SimpleBLE::Adapter::get_adapters();
      if (adapters.empty()) {
        TINYBLEFS_LOGE("simpleble", "no Bluetooth adapter found (is bluetoothd running?)");
        return false;
      }
      using Cap = SimpleBLE::Local::CharacteristicCapability;
      peripheral_ = adapters[0].create_local_peripheral();
      peripheral_.add_advertised_service(kServiceUUID);
      SimpleBLE::Local::Service svc = peripheral_.add_service(kServiceUUID);
      rx_ = svc.add_characteristic(kRxUUID, {Cap::WRITE_REQUEST, Cap::WRITE_COMMAND});
      tx_ = svc.add_characteristic(kTxUUID, {Cap::NOTIFY});

      rx_.set_callback_on_write([this](SimpleBLE::ByteArray v) {
        q_.push((const uint8_t*)v.data(), v.size());
      });
      tx_.set_callback_on_subscribed([this]() {
        subscribed_ = true;
        TINYBLEFS_LOGD("simpleble", "client subscribed to notifications");
      });
      tx_.set_callback_on_unsubscribed([this]() { subscribed_ = false; });
      peripheral_.set_callback_on_client_connected([this](SimpleBLE::BluetoothAddress a) {
        q_.clear();
        connected_ = true;
        TINYBLEFS_LOGI("simpleble", "client %s connected", a.c_str());
      });
      peripheral_.set_callback_on_client_disconnected([this](SimpleBLE::BluetoothAddress a) {
        connected_ = false;
        subscribed_ = false;
        TINYBLEFS_LOGI("simpleble", "client %s disconnected", a.c_str());
      });
      peripheral_.start();
      started_ = true;
      TINYBLEFS_LOGI("simpleble", "advertising the TinyBLEFS service");
      return true;
    } catch (const std::exception& e) {
      TINYBLEFS_LOGE("simpleble", "cannot start the GATT server: %s", e.what());
      return false;
    }
  }

  /// Stops advertising
  void end() {
    if (!started_) return;
    started_ = false;
    try {
      rx_.set_callback_on_write({});
      peripheral_.stop();
    } catch (const std::exception& e) {
      TINYBLEFS_LOGD("simpleble", "stop: %s", e.what());
    }
  }

  /// Bytes per notification (default 20: SimpleBLE can't tell the MTU of the
  /// connection; raise it if the client negotiated a larger one)
  void setPacketSize(size_t n) { packet_ = n < 20 ? 20 : (n > 512 ? 512 : n); }
  /// Pause after every notification (default 10 ms): SimpleBLE has no flow
  /// control, so this keeps the BlueZ notification queue from overflowing
  void setPacingMs(uint32_t ms) { pacing_ms_ = ms; }

  // ---- Transport -------------------------------------------------------
  size_t mtu() const override { return packet_; }
  bool send(const uint8_t* data, size_t len) override {
    if (!connected_ || !subscribed_) {
      TINYBLEFS_LOGW("simpleble", "cannot notify: no client connected or not subscribed");
      return false;
    }
    try {
      tx_.set_value(SimpleBLE::ByteArray((const char*)data, len));  // notifies
    } catch (const std::exception& e) {
      TINYBLEFS_LOGW("simpleble", "notification of %u bytes failed: %s", (unsigned)len, e.what());
      return false;
    }
    if (pacing_ms_) std::this_thread::sleep_for(std::chrono::milliseconds(pacing_ms_));
    return true;
  }
  size_t receive(uint8_t* buf, size_t max) override {
    uint32_t d = q_.takeDropped();
    if (d) TINYBLEFS_LOGW("simpleble", "%u received packet(s) dropped (queue full or packet too large)", (unsigned)d);
    return q_.pop(buf, max);
  }
  bool connected() const override { return connected_; }

 private:
  SimpleBLE::Local::Peripheral peripheral_;
  SimpleBLE::Local::Characteristic rx_, tx_;
  detail::LockedQueue q_;
  size_t packet_ = 20;
  uint32_t pacing_ms_ = 10;
  std::atomic<bool> connected_{false}, subscribed_{false}, started_{false};
};

}  // namespace tinyblefs

#if defined(ARDUINO) && !defined(TINYBLEFS_NO_USING_NAMESPACE)
using tinyblefs::SimpleBLEClientTransport;
using tinyblefs::SimpleBLEServerTransport;
#endif
