# File systems and platforms

A server shares **one file system**, chosen by the backend you give to its constructor:

```cpp
FileServer server(backend, transport, "name");
```

| Backend (`TinyBLEFS/backends/`) | Shares | Platforms |
|---|---|---|
| `ArduinoFS.h`: [`ArduinoFsBackend<FS>`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1ArduinoFsBackend.html) | Any Arduino `FS` class with string open modes: **LittleFS, SPIFFS, FFat, SD, SD_MMC** (ESP32), **LittleFS, FatFS, SDFS** (RP2040 / Pico W) | ESP32, RP2040 |
| `TinyFATFS.h`: [`TinyFatBackend`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1TinyFatBackend.html) | A FAT volume of the [TinyFATFS](https://github.com/pschatzmann/TinyFATFS) library: **RAM disk, SD card (SPI), SD_MMC (experimental), disk image file, ...** | Boards supported by TinyFATFS (ESP32, RP2040, ...), Linux |
| `StdFilesystem.h`: [`StdFilesystemBackend`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1StdFilesystemBackend.html) | A directory of the host (`std::filesystem`) | Linux, macOS, Windows |
| your own [`FsBackend`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1FsBackend.html) | Anything else (see [below](#your-own-file-system)) | any |

## Verification status

"Host test" means an automated test on a PC (`tests/`), "compiles" that it builds for the board with `arduino-cli`. Only the LittleFS rows have run on real hardware (see [Test results](test-results.md)):

| File system | Platform | Backend | Verified |
|---|---|---|---|
| LittleFS | ESP32 | `ArduinoFsBackend<fs::FS>` | **tested on hardware** (ESP32-S3, Linux tool: file operations, 100 KB transfer); backend logic: host test with a mock `FS` |
| SPIFFS | ESP32 | `ArduinoFsBackend<fs::FS>` | compiles |
| FFat | ESP32 | `ArduinoFsBackend<fs::FS>` | compiles |
| SD (SPI) | ESP32 | `ArduinoFsBackend<fs::FS>` | compiles |
| SD_MMC | ESP32 | `ArduinoFsBackend<fs::FS>` | compiles |
| LittleFS | Pico W | `ArduinoFsBackend<FS>` | **tested on hardware** with the Linux tool (mount, list, read, write, append, mkdir, copy, move, delete, 100 KB transfer); compiles |
| FatFS (flash) | Pico W | `ArduinoFsBackend<FS>` | compiles |
| SDFS (SD card) | Pico W | `ArduinoFsBackend<FS>` | compiles |
| RAM disk | any with TinyFATFS | `TinyFatBackend` | host test (full server/client path on a RAM disk); compiles for ESP32 |
| SD (SPI) | ESP32 | `TinyFatBackend` | compiles |
| directory | Linux | `StdFilesystemBackend` | host test |

The BLE adapter and the file system are independent: any adapter can share any backend. The boards of the ArduinoBLE adapter (UNO R4 WiFi, Nano 33 BLE) are mostly clients. As servers they need TinyFATFS or your own backend, because their own file system classes do not use string open modes; a server with a tiny RAM backend of ~50 lines was tested on a UNO R4 WiFi.

---

## ESP32

All ESP32 file system classes derive from `fs::FS`, so one template argument fits all of them:

```cpp
ArduinoFsBackend<fs::FS> backend(LittleFS);   // or SPIFFS, FFat, SD, SD_MMC
```

Common to all sketches:

```cpp
#include "TinyBLEFS.h"
#include "TinyBLEFS/adapters/NimBLE.h"
#include "TinyBLEFS/backends/ArduinoFS.h"

NimBLEServerTransport transport;
FileServer server(backend, transport, "esp32");   // backend: see below

void setup() {
  // start the file system first (see below), then:
  transport.begin("TinyFS ESP32");
}
void loop() { server.poll(); }
```

Declare the backend after the `#include` of the file system and before the server.

**Open files.** The ESP32 limits the number of simultaneously open files of each file system (the `maxOpenFiles` argument of `begin()`: 10 for LittleFS, SPIFFS and FFat, **5 for SD and SD_MMC**). A [`FileServer`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1FileServer.html) can hold up to `maxSessions × maxHandles` files (default 1 × 4), and one more is used while a directory is listed. Raise the limit of the file system or lower the server's.

**Fast directory listing.** On the ESP32 the backend lists directories with the POSIX functions of the virtual file system (`opendir`/`readdir`/`stat` below `FS::mountpoint()`) and keeps the directory open between the requests of a listing, while the generic Arduino API opens a `File` object for every entry it skips (a mock file system listed 121 entries about 70 times faster). Define `TINYBLEFS_ARDUINOFS_NO_POSIX` before the include to switch this off.

### LittleFS

```cpp
#include <LittleFS.h>
ArduinoFsBackend<fs::FS> backend(LittleFS);

void setup() {
  LittleFS.begin(true);          // true: format if mounting fails
  transport.begin("TinyFS ESP32");
}
```

Needs a partition scheme with a `spiffs` data partition (the default schemes have one). Power-fail safe and wear leveling: the best choice for internal flash.

### SPIFFS

```cpp
#include <SPIFFS.h>
ArduinoFsBackend<fs::FS> backend(SPIFFS);

void setup() { SPIFFS.begin(true); transport.begin("TinyFS ESP32"); }
```

SPIFFS has **no real directories** (paths are just long names, limited to 31 characters): `mkdir` / `rmdir` do not work as clients expect and listing a directory shows files below the path. Deprecated by Espressif; prefer LittleFS.

### FFat (FAT in internal flash)

```cpp
#include <FFat.h>
ArduinoFsBackend<fs::FS> backend(FFat);

void setup() { FFat.begin(true); transport.begin("TinyFS ESP32"); }
```

Needs a partition scheme with an `ffat` partition (Tools → Partition Scheme → "16MB Flash (3MB APP/9.9MB FATFS)" or similar). The file is readable by a PC if you export the partition.

### SD card over SPI (`SD`)

```cpp
#include <SD.h>
#include <SPI.h>
ArduinoFsBackend<fs::FS> backend(SD);

const int CS = 5;
void setup() {
  SPI.begin(18, 19, 23, CS);                       // SCK, MISO, MOSI, CS of your board
  if (!SD.begin(CS, SPI, 4000000, "/sd", 8)) {     // 8: open files (default 5)
    Serial.println("no card");
    return;
  }
  transport.begin("TinyFS SD");
}
```

FAT32 or exFAT cards; file names are case insensitive.

### SD card over SD_MMC (SDIO)

```cpp
#include <SD_MMC.h>
ArduinoFsBackend<fs::FS> backend(SD_MMC);

void setup() {
  // ESP32-S3 and other chips with free pin mapping: SD_MMC.setPins(clk, cmd, d0);
  if (!SD_MMC.begin("/sdcard", true, false, 20000, 8)) {   // 1-bit mode, 20 MHz, 8 open files
    Serial.println("no card");
    return;
  }
  transport.begin("TinyFS SD");
}
```

Faster than SPI. On the classic ESP32 the SD_MMC pins are fixed (CLK 14, CMD 15, D0 2; GPIO 12 and the other data pins are used in 4-bit mode, which may conflict with boot pins).

---

## Raspberry Pi Pico W / Pico 2 W (arduino-pico)

The file system classes of arduino-pico are all of the type `FS`:

```cpp
#include "TinyBLEFS.h"
#include "TinyBLEFS/adapters/PicoW.h"      // Tools -> IP/Bluetooth Stack -> "IPv4 + Bluetooth"
#include "TinyBLEFS/backends/ArduinoFS.h"

PicoWServerTransport transport;
ArduinoFsBackend<FS> backend(LittleFS);   // or FatFS, SDFS
FileServer server(backend, transport, "picow");

void setup() { LittleFS.begin(); transport.begin("TinyFS Pico"); }
void loop()  { server.poll(); }
```

There is no POSIX fast path on the Pico: the generic Arduino API is used and a directory listing opens each entry. Keep directories small.

### LittleFS (internal flash)

```cpp
#include <LittleFS.h>
ArduinoFsBackend<FS> backend(LittleFS);
// setup: LittleFS.begin();
```

Select a flash size with a file system (Tools → Flash Size, e.g. "2MB (Sketch: 1MB, FS: 1MB)"). Note: this LittleFS removes a directory automatically when its last file is deleted, so a later `rmdir` of that directory reports "not found".

### FatFS (internal flash)

```cpp
#include <FatFS.h>
ArduinoFsBackend<FS> backend(FatFS);
// setup: FatFS.begin();
```

FAT in the flash partition: readable from a PC with `FatFSUSB`.

### SD card (`SDFS`)

```cpp
#include <SDFS.h>
ArduinoFsBackend<FS> backend(SDFS);

void setup() {
  SDFS.setConfig(SDFSConfig(17));        // chip select pin, SPI0 (default pins)
  // SDIO instead of SPI: SDFS.setConfig(SDFSConfig(clkPin, cmdPin, dat0Pin));
  SDFS.begin();
  transport.begin("TinyFS Pico");
}
```

Use `SDFS`, not the `SD` object of arduino-pico: `SD` is a different class (Arduino SD API) that the generic backend cannot use.

---

## TinyFATFS (FAT on any storage)

[TinyFATFS](https://github.com/pschatzmann/TinyFATFS) is a header-only port of ChaN's FatFs with exchangeable drivers. Use it where the Arduino file system classes do not fit (UNO R4 WiFi, Nano 33 BLE), where you want a RAM disk, or one FAT code base on all boards.

```cpp
#include "fatfs.h"                        // TinyFATFS: SD, RamIO, ArduinoSpiIO, ...
#include "TinyBLEFS.h"
#include "TinyBLEFS/adapters/NimBLE.h"
#include "TinyBLEFS/backends/TinyFATFS.h"

NimBLEServerTransport transport;
TinyFatBackend backend(SD);               // SD: the global object of TinyFATFS
FileServer server(backend, transport, "fat");
```

### RAM disk (no hardware)

```cpp
RamIO drive{200, 512};                    // 200 sectors of 512 bytes = 100 KB of RAM

void setup() {
  SD.begin(drive);
  transport.begin("TinyFS RAM");
}
```

The content is lost at reset. The automated test (`tests/test_tinyfat.cpp`) runs against a RAM disk on a PC.

### SD card over SPI

```cpp
#include <SPI.h>
ArduinoSpiIO drive{13, SPI};              // chip select, SPI bus

void setup() {
  SPI.begin(14, 2, 15);                   // SCK, MISO, MOSI
  SD.begin(drive);
  transport.begin("TinyFS SD");
}
```

Other drivers of TinyFATFS: `FileIO` (a disk image in a file on a PC), `StreamIO`, `MultiIO` (several drives), `NBDClientIO` (network block device). Their documentation: [TinyFATFS drivers](https://github.com/pschatzmann/TinyFATFS/blob/main/docs/driver-overview.md).

### SD card over SD_MMC

TinyFATFS has a driver for the SDMMC controller (`Esp32SdmmcIO`), but it is marked **experimental** by its author (initialisation fails with some ESP-IDF 5 versions). For SD_MMC use the Arduino `SD_MMC` library with `ArduinoFsBackend` as shown for the ESP32 above.

### Notes

- Directory listing is efficient: the backend reads the directory with `f_readdir` and does not open the entries.
- If `FF_FS_LOCK` is set in the `ffconf.h` of TinyFATFS it must be at least the number of simultaneously open files of the server.
- FAT file names are case insensitive.

---

## Linux, macOS, Windows: a directory

```cpp
#include "TinyBLEFS.h"
#include "TinyBLEFS/adapters/SimpleBLE.h"
#include "TinyBLEFS/backends/StdFilesystem.h"

StdFilesystemBackend backend;
SimpleBLEServerTransport transport;
FileServer server(backend, transport, "pc", "/srv/share");   // shared directory

int main() {
  transport.begin();
  while (true) server.poll();
}
```

The shared directory is the 4th constructor argument; clients cannot leave it. The command line tool `tinyblefs-server` does exactly this ([Linux tools](cli.md)).

Directory listings of this backend re-read the directory for each entry on the server (a PC does that in milliseconds), which is not optimised yet.

---

## Your own file system

Derive from `FsBackend` (`TinyBLEFS/FsBackend.h`) and from [`FsFile`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1FsFile.html) for open files:

```cpp
class MyFile : public FsFile {
 public:
  int read(uint32_t offset, uint8_t* buf, size_t len) override;        // bytes read, 0 at the end, -1 on error
  int write(uint32_t offset, const uint8_t* buf, size_t len) override; // bytes written, -1 on error
  uint32_t size() override;
  ~MyFile() override;                                                   // closes the file
};

class MyBackend : public FsBackend {
 public:
  FsFile* open(const char* path, uint8_t flags) override;   // nullptr on failure
  bool stat(const char* path, FsEntry& entry) override;
  bool list(const char* path, uint32_t index, FsEntry& entry) override;
  bool remove(const char* path) override;
  bool mkdir(const char* path) override;
  bool rmdir(const char* path) override;
  bool rename(const char* from, const char* to) override;
};
```

The contract:

- **Paths** are absolute and start with `/`; the shared root of the server is already added and `..` is already rejected.
- **`open` flags** (`Protocol.h`): `kRead`, `kWrite`, `kCreate` (create if missing), `kTrunc` (create or empty the file), `kAppend` (only a hint: the client writes at the end by offset). Without `kCreate`/`kTrunc` the file must exist. `open` is never called for directories.
- **`read` / `write`** use absolute offsets, there is no cursor. Reading at or beyond the end returns 0.
- **`stat`** must also answer for the root directory `/`.
- **`list`** returns the `index`-th entry of a directory (0, 1, 2, ...) and `false` after the last one. The server asks for the entries in order, so keep the directory open between calls if opening is expensive (see `ArduinoFS.h`). `.` and `..` must not be returned.
- **Locking** is done by the server: a backend never sees two writers for the same file.
- Log failures with `TINYBLEFS_LOGE` / `TINYBLEFS_LOGW` ([Logging](logging.md)).

## Limits that apply to all file systems

- File sizes and offsets are 32 bit (4 GB).
- A directory listing returns many entries per request (about 28 per 400-byte answer), but every BLE request still costs a connection interval or more: listing 100 entries took about 2 s on an ESP32. Keep directories small.
- Start the file system (`begin()`) before clients connect. Data reaches the medium when the client closes the file.
- Names are shown as the file system reports them (FAT is case insensitive, LittleFS is not).
