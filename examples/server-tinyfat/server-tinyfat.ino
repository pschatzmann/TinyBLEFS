// Shares a FAT volume managed by the TinyFATFS library over BLE.
// This example uses a RAM disk, so it needs no hardware. For an SD card
// replace the RamIO by SD.begin(CS_PIN, SPI) (see the TinyFATFS examples).
// Runs on every supported board: TinyBLEFS/adapters/BLETransport.h selects the BLE
// adapter of the platform. On boards with little RAM (UNO R4: 32 KB) make the
// RAM disk smaller, e.g. RamIO drive{32, 512}.
#include "fatfs.h"  // TinyFATFS: provides SD, RamIO, ...

#include "TinyBLEFS.h"
#include "TinyBLEFS/adapters/BLETransport.h"
#include "TinyBLEFS/backends/TinyFATFS.h"

RamIO drive{200, 512};  // 100 KB RAM disk (200 sectors of 512 bytes)
BLEServerTransport transport;
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
