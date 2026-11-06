// Shares a FAT volume managed by the TinyFATFS library over BLE.
// This example uses a RAM disk, so it needs no hardware. For an SD card
// replace the RamIO by SD.begin(CS_PIN, SPI) (see the TinyFATFS examples).
#include "fatfs.h"  // TinyFATFS: provides SD, RamIO, ...

#include "TinyBLEFS.h"
#include "TinyBLEFS/adapters/NimBLE.h"
#include "TinyBLEFS/backends/TinyFATFS.h"

RamIO drive{200, 512};  // 100 KB RAM disk (200 sectors of 512 bytes)
NimBLEServerTransport transport;
TinyFatBackend backend(SD);
FileServer server(backend, transport, "fat");

void setup() {
  Serial.begin(115200);
  TinyBLEFSLogger.begin(Serial, LogLevel::Info);

  if (!SD.begin(drive)) {
    Serial.println("SD.begin() failed");
    return;
  }
  File f = SD.open("hello.txt", FILE_WRITE);
  f.println("Hello from a FAT volume");
  f.close();

  transport.begin("TinyFS FAT");
}

void loop() { server.poll(); }
