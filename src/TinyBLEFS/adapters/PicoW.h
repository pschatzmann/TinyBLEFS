#pragma once
// Transport adapters for the Raspberry Pi Pico W / Pico 2 W using the BLE
// library of the arduino-pico core (BTstack based).
// Select Tools -> IP/Bluetooth Stack -> "IPv4 + Bluetooth" in the IDE
// (fqbn option ipbtstack=ipv4btcble).
//
//   Server:  tinyblefs::PicoWServerTransport (several clients: see Session.h)
//   Client:  tinyblefs::PicoWClientTransport  -- the core's BLE library has one
//            global BLEClient, so only ONE remote server can be connected.
//
// The core's BLECharacteristic::setValue() notification cannot be used to send
// a sequence of packets (it keeps a single pending send request that is
// overwritten by the next call). The server therefore sends its notifications
// directly with BTstack's att_server_notify(), waiting until the ACL buffers
// are free. This uses protected members of BLECharacteristic (a subclass).

#include <BLE.h>
#include <BluetoothLock.h>
#include <btstack.h>

#include <vector>

#include "../Logger.h"
#include "../Transport.h"
#include "RingQueue.h"

// The Pico accepts writes of up to ATT MTU 517 - 3 bytes: the receive queue must
// hold packets of that size, else the packets of a client with a large MTU are
// dropped.
#ifndef TINYBLEFS_PICO_RX_PACKET
#define TINYBLEFS_PICO_RX_PACKET 514
#endif

namespace tinyblefs {

namespace detail {
/// TX characteristic that can send raw notification packets with flow control
class PicoWNotifyCharacteristic : public BLECharacteristic {
 public:
  PicoWNotifyCharacteristic(BLEUUID uuid)
      : BLECharacteristic(uuid, BLERead | BLENotify) {}

  /// Sends one notification packet to a connection and waits until the
  /// Bluetooth buffers are free
  bool sendPacket(uint16_t handle, const uint8_t* data, size_t len,
                  uint32_t timeoutMs = 2000) {
    uint32_t start = millis();
    while (millis() - start < timeoutMs) {
      {
        BluetoothLock lock;
        if (att_server_can_send_packet_now(handle)) {
          uint8_t err = att_server_notify(handle, _valueHandle, (uint8_t*)data, len);
          if (err != ERROR_CODE_SUCCESS)
            TINYBLEFS_LOGE("picow", "att_server_notify failed (error 0x%02x)", err);
          return err == ERROR_CODE_SUCCESS;
        }
      }
      delay(1);
    }
    TINYBLEFS_LOGW("picow", "notification not sent within %u ms: Bluetooth buffers full",
                (unsigned)timeoutMs);
    return false;
  }
  /// True once a client has enabled notifications
  bool subscribed() const { return _notificationEnabled; }
  void setSubscribed(bool on) { _notificationEnabled = on; }

 protected:
  // The core would switch notifications off when ANY client disconnects; the
  // transport does that when the last one has gone.
  void disconnected() override {}
};
}  // namespace detail

/// Peripheral side: advertises the TinyBLEFS service.
///
/// With maxConnections > 1 several clients can connect. They share the link,
/// give the FileServer maxSessions > 1.
class PicoWServerTransport : public Transport, public BLECharacteristicCallbacks {
 public:
  PicoWServerTransport()
      : service_(BLEUUID(kServiceUUID)),
        rx_(BLEUUID(kRxUUID), BLEWrite | BLEWriteWithoutResponse),
        tx_(BLEUUID(kTxUUID)) {}

  /// Connection interval the server asks the client for, in units of 1.25 ms
  /// (default 6..6 = 7.5 ms). Call before begin().
  void setConnectionInterval(uint16_t minUnits, uint16_t maxUnits) {
    conn_min_ = minUnits;
    conn_max_ = maxUnits;
  }

  /// Starts BLE and advertising
  /// @param deviceName     BLE device name (used by the client scan prefix)
  /// @param maxConnections clients connected at the same time (1 .. 4); the
  ///        controller / BTstack configuration may allow fewer
  bool begin(const char* deviceName, size_t maxConnections = 1) {
    max_ = maxConnections < 1 ? 1 : (maxConnections > kMaxConnections ? kMaxConnections : maxConnections);
    self() = this;
    BLE.begin(deviceName);
    rx_.setCallbacks(this);
    service_.addCharacteristic(&rx_);
    service_.addCharacteristic(&tx_);
    BLE.server()->addService(&service_);
    BLE.startAdvertising();
    // The core's BLEServer remembers only the latest connection: track all of
    // them ourselves.
    hci_reg_.callback = &PicoWServerTransport::onHci;
    {
      BluetoothLock lock;
      hci_add_event_handler(&hci_reg_);
    }
    TINYBLEFS_LOGI("picow", "advertising as '%s' (up to %u client(s))", deviceName, (unsigned)max_);
    return true;
  }

  // ---- Transport -------------------------------------------------------
  size_t mtu() const override {
    uint16_t h[kMaxConnections];
    size_t n = snapshot(h);
    size_t best = TINYBLEFS_PICO_RX_PACKET;
    if (n == 0) return 20;
    for (size_t i = 0; i < n; i++) {
      uint16_t m = att_server_get_mtu(h[i]);
      size_t v = m > 23 ? m - 3 : 20;
      if (v < best) best = v;
    }
    return best;
  }
  /// Sends the packet to every connected client (the sessions of a FileServer
  /// keep them apart)
  bool send(const uint8_t* data, size_t len) override {
    uint16_t h[kMaxConnections];
    size_t n = snapshot(h);
    if (n == 0 || !tx_.subscribed()) {
      TINYBLEFS_LOGW("picow", "cannot notify: no client connected or not subscribed");
      return false;
    }
    bool ok = false;
    for (size_t i = 0; i < n; i++) ok = tx_.sendPacket(h[i], data, len) || ok;
    TINYBLEFS_LOGD("picow", "sent packet of %u bytes (ATT MTU %u)", (unsigned)len,
                (unsigned)att_server_get_mtu(h[0]));
    return ok;
  }
  size_t receive(uint8_t* buf, size_t max) override {
    if (clear_pending_) {
      clear_pending_ = false;
      q_.clear();
    }
    uint32_t n = q_.takeDropped();
    if (n) TINYBLEFS_LOGW("picow", "%u received packet(s) dropped (queue full or packet too large)", (unsigned)n);
    size_t len = q_.pop(buf, max);
    if (len) TINYBLEFS_LOGD("picow", "received packet of %u bytes", (unsigned)len);
    return len;
  }
  bool connected() const override { return count_ > 0; }

 private:
  static constexpr size_t kMaxConnections = 4;

  static PicoWServerTransport*& self() {
    static PicoWServerTransport* p = nullptr;
    return p;
  }
  size_t snapshot(uint16_t* out) const {
    BluetoothLock lock;
    for (size_t i = 0; i < count_; i++) out[i] = handles_[i];
    return count_;
  }
  // BLECharacteristicCallbacks (runs in the Bluetooth context: no allocation)
  void onWrite(BLECharacteristic* c) override {
    if (c == &rx_) q_.push((const uint8_t*)c->valueData(), c->valueLen());
  }
  // HCI events (Bluetooth context)
  static void onHci(uint8_t type, uint16_t, uint8_t* packet, uint16_t) {
    PicoWServerTransport* t = self();
    if (!t || type != HCI_EVENT_PACKET) return;
    switch (hci_event_packet_get_type(packet)) {
      case HCI_EVENT_META_GAP:
        if (hci_event_gap_meta_get_subevent_code(packet) == GAP_SUBEVENT_LE_CONNECTION_COMPLETE)
          t->added(gap_subevent_le_connection_complete_get_connection_handle(packet));
        break;
      case HCI_EVENT_DISCONNECTION_COMPLETE:
        t->removed(hci_event_disconnection_complete_get_connection_handle(packet));
        break;
      default:
        break;
    }
  }
  void added(uint16_t handle) {
    if (count_ == 0) clear_pending_ = true;
    if (count_ < max_) handles_[count_++] = handle;
    gap_request_connection_parameter_update(handle, conn_min_, conn_max_, 0, 200);
    if (count_ < max_) gap_advertisements_enable(1);  // room for more clients
  }
  void removed(uint16_t handle) {
    for (size_t i = 0; i < count_; i++) {
      if (handles_[i] != handle) continue;
      handles_[i] = handles_[--count_];
      if (count_ == 0) tx_.setSubscribed(false);
      gap_advertisements_enable(1);  // BTstack stops advertising while connected
      return;
    }
  }

  BLEService service_;
  BLECharacteristic rx_;
  detail::PicoWNotifyCharacteristic tx_;
  RingQueueT<16, TINYBLEFS_PICO_RX_PACKET> q_;
  btstack_packet_callback_registration_t hci_reg_;
  uint16_t handles_[kMaxConnections] = {};
  volatile size_t count_ = 0;
  size_t max_ = 1;
  uint16_t conn_min_ = 6, conn_max_ = 6;
  volatile bool clear_pending_ = false;
};

/// Central side: connects to one TinyBLEFS server
class PicoWClientTransport : public Transport {
 public:
  ~PicoWClientTransport() override { disconnect(); }

  /// Scans for devices that advertise the TinyBLEFS service.
  /// @param namePrefix only return devices whose advertised BLE device name
  ///        starts with this prefix (empty = all)
  static std::vector<BLEAdvertising> scan(uint32_t durationMs = 5000,
                                          const char* namePrefix = "") {
    std::vector<BLEAdvertising> found;
    BLE.begin();
    auto* report = BLE.scan(BLEUUID(kServiceUUID), (durationMs + 999) / 1000);
    if (!report) {
      TINYBLEFS_LOGE("picow", "scan failed (Bluetooth stack not enabled? see Tools -> IP/Bluetooth Stack)");
      return found;
    }
    size_t prefixLen = namePrefix ? strlen(namePrefix) : 0;
    for (auto& item : *report) {
      if (prefixLen && strncmp(item.getName(), namePrefix, prefixLen) != 0)
        continue;
      found.push_back(item);
    }
    BLE.clearScan();
    return found;
  }

  bool connect(BLEAdvertising& device, uint32_t timeoutSec = 10) {
    disconnect();
    BLEClient* c = BLE.client();
    if (!c->connect(device, timeoutSec)) {
      TINYBLEFS_LOGW("picow", "connect failed: cannot connect to the device");
      return false;
    }
    BLERemoteService* svc = c->service(BLEUUID(kServiceUUID));
    if (!svc) return fail("device has no TinyBLEFS service");
    rx_ = svc->characteristic(BLEUUID(kRxUUID));
    BLERemoteCharacteristic* tx = svc->characteristic(BLEUUID(kTxUUID));
    if (!rx_ || !tx) return fail("TinyBLEFS characteristics not found");
    if (!rx_->canWrite()) return fail("the TinyBLEFS write characteristic is not writable");
    self() = this;
    q_.clear();
    tx->onNotify(&PicoWClientTransport::onNotify);
    if (!tx->enableNotifications()) return fail("cannot enable notifications");
    return true;
  }

  void disconnect() {
    if (rx_) {
      BLE.client()->disconnect();
      rx_ = nullptr;
    }
    if (self() == this) self() = nullptr;
  }

  // ---- Transport -------------------------------------------------------
  // Writes are done with response, so they may be larger than the MTU
  // (BTstack uses long writes); notifications of the server are limited by
  // the ATT MTU, which the server side honours.
  size_t mtu() const override { return 20; }
  bool send(const uint8_t* data, size_t len) override {
    if (rx_ && rx_->setValue((uint8_t*)data, len)) return true;
    TINYBLEFS_LOGW("picow", "write of %u bytes failed", (unsigned)len);
    return false;
  }
  size_t receive(uint8_t* buf, size_t max) override {
    uint32_t n = q_.takeDropped();
    if (n) TINYBLEFS_LOGW("picow", "%u received packet(s) dropped (queue full or packet too large)", (unsigned)n);
    return q_.pop(buf, max);
  }
  bool connected() const override { return rx_ && BLE.client()->connected(); }

 private:
  static PicoWClientTransport*& self() {
    static PicoWClientTransport* p = nullptr;
    return p;
  }
  static void onNotify(BLERemoteCharacteristic*, const uint8_t* data, uint32_t len) {
    if (self()) self()->q_.push(data, len);
  }
  bool fail(const char* why) {
    TINYBLEFS_LOGW("picow", "connect failed: %s", why);
    BLE.client()->disconnect();
    rx_ = nullptr;
    return false;
  }

  BLERemoteCharacteristic* rx_ = nullptr;
  RingQueueT<16, TINYBLEFS_PICO_RX_PACKET> q_;
};

}  // namespace tinyblefs

#if defined(ARDUINO) && !defined(TINYBLEFS_NO_USING_NAMESPACE)
using tinyblefs::PicoWServerTransport;
using tinyblefs::PicoWClientTransport;
#endif
