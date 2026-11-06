#pragma once
// Fixed size, allocation free packet queue for single producer (BLE callback)
// / single consumer (loop) use. Suitable for callbacks that must not allocate.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef TINYBLEFS_RX_SLOTS
#define TINYBLEFS_RX_SLOTS 24  // packets that can be buffered
#endif
#ifndef TINYBLEFS_RX_PACKET
#define TINYBLEFS_RX_PACKET 244  // max bytes of one packet (ATT MTU 247 - 3)
#endif

namespace tinyblefs {

/// Slots: number of packets that can be buffered, Size: bytes of the largest packet
template <size_t Slots, size_t Size>
class RingQueueT {
 public:
  /// Returns false (packet dropped) when full or too large
  bool push(const uint8_t* d, size_t n) {
    size_t h = head_, next = (h + 1) % Slots;
    if (n > Size || next == tail_) {
      dropped_ = dropped_ + 1;  // reported by takeDropped() from the main loop
      return false;
    }
    memcpy(buf_[h], d, n);
    len_[h] = (uint16_t)n;
    head_ = next;
    return true;
  }
  /// Copies the oldest packet; returns its length, or 0 if empty
  size_t pop(uint8_t* out, size_t max) {
    size_t t = tail_;
    if (t == head_) return 0;
    size_t n = len_[t] < max ? len_[t] : max;
    memcpy(out, buf_[t], n);
    tail_ = (t + 1) % Slots;
    return n;
  }
  void clear() { tail_ = head_; }
  /// Number of packets lost since the last call (queue full / packet too
  /// large). Callbacks must not log, so the transports report this in loop().
  uint32_t takeDropped() {
    uint32_t n = dropped_;
    dropped_ = 0;
    return n;
  }

 private:
  uint8_t buf_[Slots][Size];
  uint16_t len_[Slots];
  volatile size_t head_ = 0, tail_ = 0;
  volatile uint32_t dropped_ = 0;
};

/// Default queue (small packets: ArduinoBLE)
using RingQueue = RingQueueT<TINYBLEFS_RX_SLOTS, TINYBLEFS_RX_PACKET>;

}  // namespace tinyblefs
