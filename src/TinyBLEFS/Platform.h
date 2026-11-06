#pragma once
// Small platform helpers (time, yielding) shared by the library.

#include <stdint.h>

#if defined(ARDUINO)
#include <Arduino.h>
#else
#include <chrono>
#include <random>
#include <thread>
#endif

namespace tinyblefs {

/// Milliseconds since start
inline uint32_t nowMs() {
#if defined(ARDUINO)
  return millis();
#else
  using namespace std::chrono;
  return (uint32_t)duration_cast<milliseconds>(
             steady_clock::now().time_since_epoch())
      .count();
#endif
}

/// Random 32 bit number (session nonces; not for cryptography)
inline uint32_t randomU32() {
#if defined(ARDUINO)
  return ((uint32_t)random(0x10000) << 16) ^ (uint32_t)random(0x10000) ^
         (uint32_t)micros();
#else
  static std::random_device rd;
  return rd();
#endif
}

/// Waits (other tasks keep running on systems with a scheduler)
inline void sleepMs(uint32_t ms) {
#if defined(ARDUINO)
  delay(ms);
#else
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#endif
}

/// Give other tasks a chance while waiting
inline void yieldCpu() {
#if defined(ARDUINO)
  yield();
#else
  std::this_thread::sleep_for(std::chrono::microseconds(50));
#endif
}

}  // namespace tinyblefs
