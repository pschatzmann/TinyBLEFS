#pragma once
// Selects the BLE transport adapter of the current platform and makes it
// available under platform independent names, so that the same sketch or
// program compiles everywhere:
//
//   tinyblefs::BLEServerTransport  server (peripheral) transport
//   tinyblefs::BLEClientTransport  client (central) transport
//   tinyblefs::BLEScanResult       element type returned by
//                                  BLEClientTransport::scan()
//
// Example:
//   #include "TinyBLEFS.h"
//   #include "TinyBLEFS/transports/BLETransport.h"
//
//   BLEServerTransport transport;
//   transport.begin("TinyFS Sensor");
//
//   auto found = BLEClientTransport::scan(5000, "TinyFS");
//   BLEClientTransport client;
//   client.connect(found[0]);
//
// Default selection:
//   ESP32 (Arduino or ESP-IDF)   -> NimBLE.h     (NimBLE-Arduino)
//   Raspberry Pi Pico W / 2 W    -> PicoW.h      (arduino-pico BLE)
//   any other Arduino board      -> ArduinoBLE.h (ArduinoBLE)
//   desktop (Linux, ...)         -> SimpleBLE.h  (SimpleBLE)
//
// Override it by defining one of these before including this header:
//   TINYBLEFS_TRANSPORT_NIMBLE, TINYBLEFS_TRANSPORT_ARDUINOBLE,
//   TINYBLEFS_TRANSPORT_PICOW, TINYBLEFS_TRANSPORT_SIMPLEBLE
// (e.g. to use ArduinoBLE on an ESP32).
//
// Only the common API is the same on all platforms: server begin(deviceName),
// client scan(durationMs, namePrefix), connect(device) and disconnect(). The
// adapter specific extras (setPacketSize, maxConnections, ...) are still there,
// see docs/transports.md. TINYBLEFS_TRANSPORT_NAME is a string naming the
// selected adapter.

#if !defined(TINYBLEFS_TRANSPORT_NIMBLE) &&     \
    !defined(TINYBLEFS_TRANSPORT_ARDUINOBLE) && \
    !defined(TINYBLEFS_TRANSPORT_PICOW) && !defined(TINYBLEFS_TRANSPORT_SIMPLEBLE)
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)
#define TINYBLEFS_TRANSPORT_NIMBLE
#elif defined(ARDUINO_ARCH_RP2040)
#define TINYBLEFS_TRANSPORT_PICOW
#elif defined(ARDUINO)
#define TINYBLEFS_TRANSPORT_ARDUINOBLE
#else
#define TINYBLEFS_TRANSPORT_SIMPLEBLE
#endif
#endif

#if defined(TINYBLEFS_TRANSPORT_NIMBLE)
#include "NimBLE.h"
#define TINYBLEFS_TRANSPORT_NAME "NimBLE"
namespace tinyblefs {
using BLEServerTransport = NimBLEServerTransport;
using BLEClientTransport = NimBLEClientTransport;
using BLEScanResult = NimBLEAddress;
}  // namespace tinyblefs

#elif defined(TINYBLEFS_TRANSPORT_ARDUINOBLE)
#include "ArduinoBLE.h"
#define TINYBLEFS_TRANSPORT_NAME "ArduinoBLE"
namespace tinyblefs {
using BLEServerTransport = ArduinoBLEServerTransport;
using BLEClientTransport = ArduinoBLEClientTransport;
using BLEScanResult = BLEDevice;
}  // namespace tinyblefs

#elif defined(TINYBLEFS_TRANSPORT_PICOW)
#include "PicoW.h"
#define TINYBLEFS_TRANSPORT_NAME "PicoW"
namespace tinyblefs {
using BLEServerTransport = PicoWServerTransport;
using BLEClientTransport = PicoWClientTransport;
using BLEScanResult = BLEAdvertising;
}  // namespace tinyblefs

#elif defined(TINYBLEFS_TRANSPORT_SIMPLEBLE)
#include "SimpleBLE.h"
#define TINYBLEFS_TRANSPORT_NAME "SimpleBLE"
namespace tinyblefs {
using BLEServerTransport = SimpleBLEServerTransport;
using BLEClientTransport = SimpleBLEClientTransport;
using BLEScanResult = SimpleBLE::Peripheral;
}  // namespace tinyblefs

#endif

#if defined(ARDUINO) && !defined(TINYBLEFS_NO_USING_NAMESPACE)
using tinyblefs::BLEClientTransport;
using tinyblefs::BLEScanResult;
using tinyblefs::BLEServerTransport;
#endif
