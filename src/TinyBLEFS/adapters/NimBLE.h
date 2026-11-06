#pragma once
// Transport adapters for NimBLE-Arduino (ESP32 and other NimBLE targets).
//
//   Server:  tinyblefs::NimBLEServerTransport  (peripheral, one client at a time)
//   Client:  tinyblefs::NimBLEClientTransport  (central, one server per object;
//            use one object + one tinyblefs::Client per remote server)

#include <NimBLEDevice.h>

#include <deque>
#include <mutex>

#include "../Logger.h"
#include "../Transport.h"

namespace tinyblefs {

/// An ATT attribute value is at most 512 bytes, so a notification or write
/// longer than that fails even if the MTU (517) would allow 514 bytes.
static constexpr size_t kNimBleMaxPacket = 512;

namespace detail {
/// Thread safe packet queue (NimBLE callbacks run in the host task)
class PacketQueue {
 public:
  void push(const uint8_t* d, size_t n) {
    std::lock_guard<std::mutex> g(m_);
    if (q_.size() < 64) q_.emplace_back(d, d + n);
    else dropped_++;
  }
  /// Packets lost because the queue was full, since the last call
  uint32_t takeDropped() {
    std::lock_guard<std::mutex> g(m_);
    uint32_t n = dropped_;
    dropped_ = 0;
    return n;
  }
  size_t pop(uint8_t* buf, size_t max) {
    std::lock_guard<std::mutex> g(m_);
    if (q_.empty()) return 0;
    size_t n = q_.front().size() < max ? q_.front().size() : max;
    memcpy(buf, q_.front().data(), n);
    q_.pop_front();
    return n;
  }
  void clear() {
    std::lock_guard<std::mutex> g(m_);
    q_.clear();
  }

 private:
  std::mutex m_;
  uint32_t dropped_ = 0;
  std::deque<std::vector<uint8_t>> q_;
};
}  // namespace detail

/// Peripheral side: advertises the TinyBLEFS service and talks to one client
class NimBLEServerTransport : public Transport,
                              public NimBLEServerCallbacks,
                              public NimBLECharacteristicCallbacks {
 public:
  /// Connection interval the server asks the client for, in units of 1.25 ms
  /// (default 6..6 = 7.5 ms, the shortest). The central decides, and a
  /// Bluetooth 4.0 PC controller picks the upper value of the range: with
  /// 6..12 it gave 4.7 KB/s, with 6..6 10.5 KB/s. A long interval saves power.
  /// Call before begin().
  void setConnectionInterval(uint16_t minUnits, uint16_t maxUnits) {
    conn_min_ = minUnits;
    conn_max_ = maxUnits;
  }

  /// Initialises NimBLE (unless already done), creates the service and starts
  /// advertising.
  /// @param deviceName BLE device name
  /// @param secure     require an encrypted (bonded) link for all access
  bool begin(const char* deviceName, bool secure = false) {
    if (!NimBLEDevice::isInitialized()) NimBLEDevice::init(deviceName);
    NimBLEDevice::setMTU(517);
    if (secure) NimBLEDevice::setSecurityAuth(true, false, true);

    server_ = NimBLEDevice::createServer();
    server_->setCallbacks(this, false);
    NimBLEService* svc = server_->createService(kServiceUUID);
    if (!svc) {
      TINYBLEFS_LOGE("nimble", "cannot create the TinyBLEFS service");
      return false;
    }
    uint32_t enc = secure ? (NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::WRITE_ENC) : 0;
    rx_ = svc->createCharacteristic(
        kRxUUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | enc);
    tx_ = svc->createCharacteristic(
        kTxUUID, NIMBLE_PROPERTY::NOTIFY | (secure ? NIMBLE_PROPERTY::READ_ENC : 0));
    rx_->setCallbacks(this);
    svc->start();

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(kServiceUUID);
    // The 128 bit service UUID fills most of the 31 byte advertisement, and
    // NimBLE-Arduino 2.x does not add the name by itself: put it into the scan
    // response so that scanners (and the name prefix of scan()) can see it.
    NimBLEAdvertisementData scanResponse;
    scanResponse.setName(deviceName);
    adv->setScanResponseData(scanResponse);
    adv->enableScanResponse(true);
    if (!adv->start()) {
      TINYBLEFS_LOGE("nimble", "cannot start advertising");
      return false;
    }
    TINYBLEFS_LOGI("nimble", "advertising as '%s'", deviceName);
    return true;
  }

  // ---- Transport -------------------------------------------------------
  size_t mtu() const override {
    if (conn_ == BLE_HS_CONN_HANDLE_NONE) return 20;
    uint16_t m = server_->getPeerMTU(conn_);
    size_t n = m > 23 ? m - 3 : 20;
    return n > kNimBleMaxPacket ? kNimBleMaxPacket : n;
  }
  bool send(const uint8_t* data, size_t len) override {
    for (int retry = 0; retry < 200; retry++) {  // mbufs may be exhausted
      if (conn_ == BLE_HS_CONN_HANDLE_NONE) return false;
      if (tx_->notify(data, len, conn_)) return true;
      delay(2);
    }
    TINYBLEFS_LOGW("nimble", "notification of %u bytes failed (no free buffers or "
                "client not subscribed)", (unsigned)len);
    return false;
  }
  size_t receive(uint8_t* buf, size_t max) override {
    reportDropped();
    return rx_q_.pop(buf, max);
  }
  bool connected() const override { return conn_ != BLE_HS_CONN_HANDLE_NONE; }

 private:
  void onConnect(NimBLEServer*, NimBLEConnInfo& info) override {
    if (conn_ != BLE_HS_CONN_HANDLE_NONE) {
      // one client at a time: a second one would mix up the first one's data
      TINYBLEFS_LOGW("nimble", "second client refused: the server handles one client at a time");
      server_->disconnect(info.getConnHandle());
      return;
    }
    conn_ = info.getConnHandle();
    TINYBLEFS_LOGI("nimble", "client connected");
    rx_q_.clear();
    // fast connection interval for throughput (7.5 .. 15 ms)
    server_->updateConnParams(conn_, conn_min_, conn_max_, 0, 200);
    // data length extension: 251 bytes per link layer packet instead of 27
    server_->setDataLen(conn_, 251);
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo& info, int reason) override {
    if (info.getConnHandle() != conn_) {  // a refused client
      NimBLEDevice::startAdvertising();
      return;
    }
    TINYBLEFS_LOGI("nimble", "client disconnected (reason %d)", reason);
    conn_ = BLE_HS_CONN_HANDLE_NONE;
    NimBLEDevice::startAdvertising();
  }
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    NimBLEAttValue v = c->getValue();
    rx_q_.push(v.data(), v.size());
  }

  void reportDropped() {
    uint32_t n = rx_q_.takeDropped();
    if (n) TINYBLEFS_LOGW("nimble", "%u received packet(s) dropped (queue full or packet too large)", (unsigned)n);
  }

  uint16_t conn_min_ = 6, conn_max_ = 6;
  NimBLEServer* server_ = nullptr;
  NimBLECharacteristic *rx_ = nullptr, *tx_ = nullptr;
  volatile uint16_t conn_ = BLE_HS_CONN_HANDLE_NONE;
  detail::PacketQueue rx_q_;
};

/// Central side: connects to one TinyBLEFS server
class NimBLEClientTransport : public Transport {
 public:
  ~NimBLEClientTransport() override { disconnect(); }

  /// Scans for devices that advertise the TinyBLEFS service.
  /// @param namePrefix only return devices whose advertised BLE device name
  ///        starts with this prefix (empty = all). This is the name given to
  ///        NimBLEServerTransport::begin(), which is not necessarily the
  ///        mount name announced by the tinyblefs::Server.
  static std::vector<NimBLEAddress> scan(uint32_t durationMs = 5000,
                                         const char* namePrefix = "") {
    size_t prefixLen = namePrefix ? strlen(namePrefix) : 0;
    if (!NimBLEDevice::isInitialized()) NimBLEDevice::init("");
    std::vector<NimBLEAddress> found;
    NimBLEScan* sc = NimBLEDevice::getScan();
    sc->setActiveScan(true);
    NimBLEScanResults res = sc->getResults(durationMs);
    NimBLEUUID uuid(kServiceUUID);
    for (int i = 0; i < res.getCount(); i++) {
      const NimBLEAdvertisedDevice* d = res.getDevice(i);
      if (!d->isAdvertisingService(uuid)) continue;
      if (prefixLen && d->getName().compare(0, prefixLen, namePrefix) != 0)
        continue;
      found.push_back(d->getAddress());
    }
    return found;
  }

  bool connect(const NimBLEAddress& address) {
    disconnect();
    if (!NimBLEDevice::isInitialized()) NimBLEDevice::init("");
    NimBLEDevice::setMTU(517);
    client_ = NimBLEDevice::createClient();
    if (!client_) return fail("cannot create a BLE client (too many connections?)");
    if (!client_->connect(address)) return fail("cannot connect to the device");
    client_->setDataLen(251);                  // data length extension
    client_->updateConnParams(6, 12, 0, 200);  // 7.5 - 15 ms connection interval
    NimBLERemoteService* svc = client_->getService(kServiceUUID);
    if (!svc) return fail("device has no TinyBLEFS service");
    rx_ = svc->getCharacteristic(kRxUUID);
    NimBLERemoteCharacteristic* tx = svc->getCharacteristic(kTxUUID);
    if (!rx_ || !tx) return fail("TinyBLEFS characteristics not found");
    detail::PacketQueue* q = &rx_q_;
    if (!tx->subscribe(true, [q](NimBLERemoteCharacteristic*, uint8_t* d,
                                 size_t n, bool) { q->push(d, n); }))
      return fail("cannot subscribe to notifications");
    return true;
  }

  void disconnect() {
    if (client_) {
      client_->disconnect();
      NimBLEDevice::deleteClient(client_);
      client_ = nullptr;
    }
    rx_ = nullptr;
    rx_q_.clear();
  }

  // ---- Transport -------------------------------------------------------
  size_t mtu() const override {
    uint16_t m = client_ ? client_->getMTU() : 23;
    size_t n = m > 23 ? m - 3 : 20;
    return n > kNimBleMaxPacket ? kNimBleMaxPacket : n;
  }
  bool send(const uint8_t* data, size_t len) override {
    if (rx_ && rx_->writeValue(data, len, false)) return true;
    TINYBLEFS_LOGW("nimble", "write of %u bytes failed", (unsigned)len);
    return false;
  }
  size_t receive(uint8_t* buf, size_t max) override {
    uint32_t n = rx_q_.takeDropped();
    if (n) TINYBLEFS_LOGW("nimble", "%u received packet(s) dropped (queue full or packet too large)", (unsigned)n);
    return rx_q_.pop(buf, max);
  }
  bool connected() const override { return client_ && client_->isConnected(); }

 private:
  bool fail(const char* why) {
    TINYBLEFS_LOGW("nimble", "connect failed: %s", why);
    disconnect();
    return false;
  }
  NimBLEClient* client_ = nullptr;
  NimBLERemoteCharacteristic* rx_ = nullptr;
  detail::PacketQueue rx_q_;
};

}  // namespace tinyblefs

#if defined(ARDUINO) && !defined(TINYBLEFS_NO_USING_NAMESPACE)
using tinyblefs::NimBLEServerTransport;
using tinyblefs::NimBLEClientTransport;
#endif
