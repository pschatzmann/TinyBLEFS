// Shares the LittleFS file system of an ESP32 over BLE.
// Clients see it as "/<name>/..." (name defaults to "esp32fs").
#include <LittleFS.h>

#include "TinyBLEFS.h"
#include "TinyBLEFS/adapters/NimBLE.h"
#include "TinyBLEFS/backends/ArduinoFS.h"

NimBLEServerTransport transport;
ArduinoFsBackend<fs::LittleFSFS> backend(LittleFS);
FileServer server(backend, transport, "esp32fs", "/");

void setup() {
  Serial.begin(115200);
  LittleFS.begin(true);
  // write a demo file
  File f = LittleFS.open("/hello.txt", "w");
  f.println("Hello from the BLE file server");
  f.close();

  transport.begin("TinyFS FileServer");  // pass true as 2nd arg to require pairing
  // server.setReadOnly(true);
  Serial.println("TinyBLEFS server running");
}

void loop() { server.poll(); }
