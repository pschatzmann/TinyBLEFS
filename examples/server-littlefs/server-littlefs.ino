// Shares the LittleFS file system of an ESP32 or a Raspberry Pi Pico W over BLE.
// Clients see it as "/<name>/..." (name: "littlefs").
//
// TinyBLEFS/transports/BLETransport.h selects the BLE adapter of the platform.
// - ESP32: NimBLE-Arduino
// - Pico W: select Tools -> IP/Bluetooth Stack -> "IPv4 + Bluetooth" and a
//   flash size with a file system (Tools -> Flash Size)
// Boards without LittleFS (UNO R4, Nano 33 BLE, ...): see server-tinyfat.
#include <LittleFS.h>

#include "TinyBLEFS.h"
#include "TinyBLEFS/transports/BLETransport.h"
#include "TinyBLEFS/fs/ArduinoFS.h"

BLEServerTransport transport;
ArduinoFsBackend<FS> backend(LittleFS);
FileServer server(backend, transport, "littlefs", "/");

void setup() {
  Serial.begin(115200);
#if defined(ESP32)
  LittleFS.begin(true);  // format if mounting fails
#else
  LittleFS.begin();
#endif
  // write a demo file
  File f = LittleFS.open("/hello.txt", "w");
  f.println("Hello from the BLE file server");
  f.close();

  // NimBLE: pass true as 2nd arg to require pairing
  transport.begin("TinyFS FileServer");
  // server.setReadOnly(true);
  Serial.print("TinyBLEFS server running on ");
  Serial.println(TINYBLEFS_TRANSPORT_NAME);
}

void loop() { server.poll(); }
