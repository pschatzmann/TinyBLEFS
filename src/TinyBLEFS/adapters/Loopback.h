#pragma once
// In-memory transport pair for host tests and examples without BLE hardware.

#include <deque>
#include <memory>
#include <mutex>
#include <string.h>

#include "../Transport.h"

namespace tinyblefs {

class LoopbackPair {
 public:
  /// @param mtu packet size, small values (e.g. 20) exercise fragmentation
  explicit LoopbackPair(size_t mtu = 20)
      : client_(mtu, &to_server_, &to_client_),
        server_(mtu, &to_client_, &to_server_) {}

  Transport& client() { return client_; }
  Transport& server() { return server_; }
  void setConnected(bool c) {
    client_.up = c;
    server_.up = c;
  }

 private:
  using Queue = std::deque<std::vector<uint8_t>>;

  class End : public Transport {
   public:
    End(size_t mtu, Queue* tx, Queue* rx) : mtu_(mtu), tx_(tx), rx_(rx) {}
    size_t mtu() const override { return mtu_; }
    bool send(const uint8_t* d, size_t n) override {
      if (!up || n > mtu_) return false;
      tx_->emplace_back(d, d + n);
      return true;
    }
    size_t receive(uint8_t* buf, size_t max) override {
      if (rx_->empty()) return 0;
      size_t n = std::min(max, rx_->front().size());
      memcpy(buf, rx_->front().data(), n);
      rx_->pop_front();
      return n;
    }
    bool connected() const override { return up; }
    bool up = true;

   private:
    size_t mtu_;
    Queue *tx_, *rx_;
  };

  Queue to_server_, to_client_;
  End client_, server_;
};

/// In-memory link with several clients that behaves like ArduinoBLE or the
/// Pico W: the server cannot tell which client wrote a packet and every packet
/// it sends reaches ALL connected clients. Thread safe, so clients can run in
/// their own threads.
class LoopbackBroadcastHub {
 public:
  LoopbackBroadcastHub(size_t clients, size_t mtu = 20)
      : server_(this, mtu), mtu_(mtu) {
    for (size_t i = 0; i < clients; i++)
      clients_.emplace_back(new ClientEnd(this, i, mtu));
  }
  Transport& client(size_t i) { return *clients_[i]; }
  Transport& server() { return server_; }
  void setConnected(size_t i, bool up) {
    std::lock_guard<std::mutex> g(m_);
    clients_[i]->up = up;
  }

 private:
  using Packet = std::vector<uint8_t>;

  class ClientEnd : public Transport {
   public:
    ClientEnd(LoopbackBroadcastHub* h, size_t, size_t mtu) : hub_(h), mtu_(mtu) {}
    size_t mtu() const override { return mtu_; }
    bool send(const uint8_t* d, size_t n) override {
      std::lock_guard<std::mutex> g(hub_->m_);
      if (!up || n > mtu_) return false;
      hub_->to_server_.emplace_back(d, d + n);
      return true;
    }
    size_t receive(uint8_t* buf, size_t max) override {
      std::lock_guard<std::mutex> g(hub_->m_);
      if (rx.empty()) return 0;
      size_t n = std::min(max, rx.front().size());
      memcpy(buf, rx.front().data(), n);
      rx.pop_front();
      return n;
    }
    bool connected() const override { return up; }
    bool up = true;
    std::deque<Packet> rx;

   private:
    LoopbackBroadcastHub* hub_;
    size_t mtu_;
  };

  class ServerEnd : public Transport {
   public:
    ServerEnd(LoopbackBroadcastHub* h, size_t mtu) : hub_(h), mtu_(mtu) {}
    size_t mtu() const override { return mtu_; }
    bool send(const uint8_t* d, size_t n) override {  // reaches every client
      std::lock_guard<std::mutex> g(hub_->m_);
      if (n > mtu_) return false;
      for (auto& c : hub_->clients_)
        if (c->up) c->rx.emplace_back(d, d + n);
      return true;
    }
    size_t receive(uint8_t* buf, size_t max) override {
      std::lock_guard<std::mutex> g(hub_->m_);
      if (hub_->to_server_.empty()) return 0;
      size_t n = std::min(max, hub_->to_server_.front().size());
      memcpy(buf, hub_->to_server_.front().data(), n);
      hub_->to_server_.pop_front();
      return n;
    }
    bool connected() const override {
      std::lock_guard<std::mutex> g(hub_->m_);
      for (auto& c : hub_->clients_)
        if (c->up) return true;
      return false;
    }

   private:
    LoopbackBroadcastHub* hub_;
    size_t mtu_;
  };

  mutable std::mutex m_;
  std::deque<Packet> to_server_;
  std::vector<std::unique_ptr<ClientEnd>> clients_;
  ServerEnd server_;
  size_t mtu_;
};

}  // namespace tinyblefs
