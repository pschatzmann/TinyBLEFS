#pragma once
// Transport adapters for the ArduinoBLE library (UNO R4 WiFi, Nano 33 BLE,
// Nano 33 IoT, MKR WiFi 1010, Nicla, ...).
//
//   Server:  tinyblefs::ArduinoBLEServerTransport  (one client at a time)
//   Client:  tinyblefs::ArduinoBLEClientTransport  (one object per remote server)
//
// ArduinoBLE is polled: Server::poll() and the Client call Transport::poll(),
// which runs BLE.poll(). Don't block in loop() for long.
//
// ArduinoBLE can't tell the negotiated ATT MTU, so the packet size defaults to
// the safe 20 bytes. If both sides negotiate a larger MTU (e.g. 247) call
// setPacketSize(244) for much better throughput.

#include <ArduinoBLE.h>
#include <utility/L2CAPSignaling.h>

#include <vector>

#include "../Logger.h"
#include "../Transport.h"
#include "RingQueue.h"

namespace tinyblefs {

namespace detail {
/// BLE.begin() must run exactly once
inline bool beginArduinoBLE() {
  static bool ok = BLE.begin();
  return ok;
}
}  // namespace detail

/// Peripheral side: advertises the TinyBLEFS service.
///
/// With maxConnections > 1 several clients can connect. They share the link
/// (every reply reaches all of them): give the FileServer maxSessions > 1.
class ArduinoBLEServerTransport : public Transport {
 public:
  ArduinoBLEServerTransport()
      : service_(kServiceUUID),
        rx_(kRxUUID, BLEWrite | BLEWriteWithoutResponse, TINYBLEFS_RX_PACKET),
        tx_(kTxUUID, BLENotify, TINYBLEFS_RX_PACKET) {}

  /// Connection interval the server asks the client for, in units of 1.25 ms
  /// (default 6..6 = 7.5 ms). Call before begin().
  void setConnectionInterval(uint16_t minUnits, uint16_t maxUnits) {
    conn_min_ = minUnits;
    conn_max_ = maxUnits;
  }

  /// Starts BLE and advertising
  /// @param deviceName     BLE device name (used by the client scan prefix)
  /// @param maxConnections clients connected at the same time (the BLE
  ///        hardware may allow fewer: 3 on Nano 33 BLE)
  bool begin(const char* deviceName, size_t maxConnections = 1) {
    max_ = maxConnections < 1 ? 1 : maxConnections;
    if (!detail::beginArduinoBLE()) {
      TINYBLEFS_LOGE("arduinoble", "BLE.begin() failed: no BLE hardware?");
      return false;
    }
    self() = this;
    L2CAPSignaling.setConnectionInterval(conn_min_, conn_max_);
    BLE.setLocalName(deviceName);
    BLE.setDeviceName(deviceName);
    service_.addCharacteristic(rx_);
    service_.addCharacteristic(tx_);
    BLE.addService(service_);
    BLE.setAdvertisedService(service_);
    rx_.setEventHandler(BLEWritten, onWritten);
    BLE.setEventHandler(BLEConnected, onConnected);
    BLE.setEventHandler(BLEDisconnected, onDisconnected);
    if (!BLE.advertise()) {
      TINYBLEFS_LOGE("arduinoble", "cannot start advertising");
      return false;
    }
    TINYBLEFS_LOGI("arduinoble", "advertising as '%s' (up to %u client(s))", deviceName,
                (unsigned)max_);
    return true;
  }

  /// Bytes per packet (default 20 = safe with the default ATT MTU of 23)
  void setPacketSize(size_t n) {
    packet_ = n > TINYBLEFS_RX_PACKET ? TINYBLEFS_RX_PACKET : (n < 20 ? 20 : n);
  }

  // ---- Transport -------------------------------------------------------
  size_t mtu() const override { return packet_; }
  bool send(const uint8_t* data, size_t len) override {
    if (connections_ == 0) return false;
    for (int retry = 0; retry < 100; retry++) {
      if (tx_.writeValue(data, (int)len)) return true;
      BLE.poll();
      delay(1);
    }
    TINYBLEFS_LOGW("arduinoble", "notification of %u bytes failed (client not subscribed?)",
                (unsigned)len);
    return false;
  }
  size_t receive(uint8_t* buf, size_t max) override {
    uint32_t n = q_.takeDropped();
    if (n) TINYBLEFS_LOGW("arduinoble", "%u received packet(s) dropped (queue full or packet too large)", (unsigned)n);
    return q_.pop(buf, max);
  }
  bool connected() const override { return connections_ > 0; }
  void poll() override { BLE.poll(); }

 private:
  static void onWritten(BLEDevice, BLECharacteristic c) {
    if (!self()) return;
    static uint8_t tmp[TINYBLEFS_RX_PACKET];  // static: this runs deep inside BLE.poll() on a small stack
    int n = c.readValue(tmp, sizeof(tmp));
    if (n > 0) self()->q_.push(tmp, n);  // overflow is reported by receive()
  }
  static void onConnected(BLEDevice) {
    ArduinoBLEServerTransport* t = self();
    if (!t) return;
    if (t->connections_ == 0) t->q_.clear();
    t->connections_++;
    TINYBLEFS_LOGI("arduinoble", "client connected (%u of %u)", (unsigned)t->connections_,
                (unsigned)t->max_);
    if (t->connections_ < t->max_) BLE.advertise();  // room for more clients
  }
  static void onDisconnected(BLEDevice) {
    ArduinoBLEServerTransport* t = self();
    if (!t) return;
    if (t->connections_ > 0) t->connections_--;
    TINYBLEFS_LOGI("arduinoble", "client disconnected (%u left)", (unsigned)t->connections_);
    BLE.advertise();  // ArduinoBLE stops advertising while connected
  }

  static ArduinoBLEServerTransport*& self() {
    static ArduinoBLEServerTransport* p = nullptr;
    return p;
  }
  BLEService service_;
  BLECharacteristic rx_, tx_;
  RingQueue q_;
  size_t packet_ = 20;
  size_t max_ = 1;
  uint16_t conn_min_ = 6, conn_max_ = 6;
  volatile size_t connections_ = 0;
};

/// Central side: connects to one TinyBLEFS server
class ArduinoBLEClientTransport : public Transport {
 public:
  ~ArduinoBLEClientTransport() override { disconnect(); }

  /// Scans for devices that advertise the TinyBLEFS service.
  /// @param namePrefix only return devices whose advertised BLE device name
  ///        starts with this prefix (empty = all)
  static std::vector<BLEDevice> scan(uint32_t durationMs = 5000,
                                     const char* namePrefix = "") {
    std::vector<BLEDevice> found;
    if (!detail::beginArduinoBLE()) {
      TINYBLEFS_LOGE("arduinoble", "BLE.begin() failed: no BLE hardware?");
      return found;
    }
    BLE.scanForUuid(kServiceUUID);
    uint32_t start = millis();
    while (millis() - start < durationMs) {
      BLEDevice d = BLE.available();
      if (!d) continue;
      if (namePrefix && *namePrefix && !d.localName().startsWith(namePrefix))
        continue;
      bool known = false;  // devices are reported repeatedly
      for (auto& f : found) known = known || f.address() == d.address();
      if (!known) found.push_back(d);
    }
    BLE.stopScan();
    return found;
  }

  bool connect(BLEDevice device) {
    disconnect();
    if (!detail::beginArduinoBLE()) return fail("BLE.begin() failed");
    device_ = device;
    if (!device_.connect()) return fail("cannot connect to the device");
    if (!device_.discoverAttributes()) return fail("service discovery failed");
    rx_ = device_.characteristic(kRxUUID);
    tx_ = device_.characteristic(kTxUUID);
    if (!rx_ || !tx_) return fail("device has no TinyBLEFS characteristics");
    if (!tx_.canSubscribe()) return fail("notifications are not supported");
    tx_.setEventHandler(BLEUpdated, onUpdated);
    if (!tx_.subscribe()) return fail("cannot subscribe to notifications");
    instances().push_back(this);
    registered_ = true;
    return true;
  }

  void disconnect() {
    if (registered_) {
      auto& v = instances();
      for (size_t i = 0; i < v.size(); i++)
        if (v[i] == this) { v.erase(v.begin() + i); break; }
      registered_ = false;
    }
    if (device_ && device_.connected()) device_.disconnect();
    q_.clear();
  }

  /// Bytes per packet (default 20 = safe with the default ATT MTU of 23)
  void setPacketSize(size_t n) {
    packet_ = n > TINYBLEFS_RX_PACKET ? TINYBLEFS_RX_PACKET : (n < 20 ? 20 : n);
  }

  // ---- Transport -------------------------------------------------------
  size_t mtu() const override { return packet_; }
  bool send(const uint8_t* data, size_t len) override {
    if (rx_ && rx_.writeValue(data, (int)len, false)) return true;
    TINYBLEFS_LOGW("arduinoble", "write of %u bytes failed", (unsigned)len);
    return false;
  }
  size_t receive(uint8_t* buf, size_t max) override {
    uint32_t n = q_.takeDropped();
    if (n) TINYBLEFS_LOGW("arduinoble", "%u received packet(s) dropped (queue full or packet too large)", (unsigned)n);
    return q_.pop(buf, max);
  }
  bool connected() const override { return device_ && device_.connected(); }
  void poll() override { BLE.poll(); }

 private:
  static std::vector<ArduinoBLEClientTransport*>& instances() {
    static std::vector<ArduinoBLEClientTransport*> v;
    return v;
  }
  /// Notification of a remote: route to the transport of that device
  static void onUpdated(BLEDevice device, BLECharacteristic c) {
    static uint8_t tmp[TINYBLEFS_RX_PACKET];  // static: small stack, see onWritten
    int n = c.readValue(tmp, sizeof(tmp));
    if (n <= 0) return;
    for (auto* t : instances())
      if (t->device_.address() == device.address()) t->q_.push(tmp, n);
  }
  bool fail(const char* why) {
    TINYBLEFS_LOGW("arduinoble", "connect failed: %s", why);
    disconnect();
    return false;
  }

  BLEDevice device_;
  BLECharacteristic rx_, tx_;
  RingQueue q_;
  size_t packet_ = 20;
  bool registered_ = false;
};

}  // namespace tinyblefs

#if defined(ARDUINO) && !defined(TINYBLEFS_NO_USING_NAMESPACE)
using tinyblefs::ArduinoBLEServerTransport;
using tinyblefs::ArduinoBLEClientTransport;
#endif
