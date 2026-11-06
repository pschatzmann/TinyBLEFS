// Shares the LittleFS file system of a Raspberry Pi Pico W over BLE.
// Select Tools -> IP/Bluetooth Stack -> "IPv4 + Bluetooth" and a flash size
// with a file system (Tools -> Flash Size).
#include <LittleFS.h>

#include "TinyBLEFS.h"
#include "TinyBLEFS/adapters/PicoW.h"
#include "TinyBLEFS/backends/ArduinoFS.h"

PicoWServerTransport transport;
ArduinoFsBackend<FS> backend(LittleFS);
FileServer server(backend, transport, "picow", "/");

void setup() {
  Serial.begin(115200);
  LittleFS.begin();
  File f = LittleFS.open("/hello.txt", "w");
  f.println("Hello from the Pico W");
  f.close();

  transport.begin("TinyFS Pico");
  Serial.println("TinyBLEFS server running");
}

void loop() { server.poll(); }
