# Tiny BLE FS


[![arduino-library-badge](https://img.shields.io/badge/Arduino-Library-00979D?logo=arduino&logoColor=white)](https://www.arduino.cc/reference/en/libraries/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

> **Proof of concept.** This library is an early prototype to explore the idea. The protocol, the API and the file formats may still change without notice, and it has had only **first tests on real hardware** (see [Test results](docs/test-results.md)): everything else is verified by automated tests on a PC (loopback transport) and by compiling the examples for the supported boards. Do not use it for anything important, and expect bugs.

A header-only C++ library that lets an Arduino sketch (or a Linux program) use the files of one or more **remote BLE devices** like local files.

- Servers share any file system (SD, LittleFS, SPIFFS, FFat, `std::filesystem`).
- Clients mount each server as `/<name>/...`. The name is announced by the server.
- Arduino `File`-like API: `open`, `read`, `write`, `seek`, `list`, `remove`, `mkdir`, `rename`, `copy`.
- Works with any BLE library through a tiny transport interface; adapters for NimBLE-Arduino, ArduinoBLE, Pico W and SimpleBLE (Linux) are included.
- Linux command line tools: `tinyblefs` (client), `tinyblefs-fuse` (mounts the servers as a directory, so `ls`, `cp`, `cat` ... work) and `tinyblefs-server` (shares a directory).

## Why this library?

The goal is to use the files of another microcontroller like local ones (`open`, `read`, `write`, `list`, `rename` with the Arduino `File` API), from a board or a Linux PC, with several devices mounted as `/<name>`, on the BLE library the sketch already uses. We looked for something to build on and did not find it:

- **Bluetooth Object Transfer Service (OTS)**, the SIG standard for moving objects over BLE, is built around selecting an object and transferring it, over an L2CAP channel. A file system API (directories, rename, several open files, locking, mounting) would need conventions on top, and every device would have to implement a large service. In practice the Arduino BLE libraries do not offer it: ArduinoBLE and the arduino-pico `BLE` library have no L2CAP channels at all, NimBLE-Arduino has channels but no OTS. It would work on one stack at best.
- **Serial-over-BLE services** (such as the Nordic UART Service) only give a byte stream: the file protocol would still have to be written.
- **Upload-only tools** (firmware update and file push protocols, one-off sketches) copy a file in one direction; they are not a mountable file system, and mostly tied to one board family.
- **Network file systems** (NFS, SMB, SSHFS, WebDAV) need IP networking, which BLE-only devices do not have.

So TinyBLEFS defines a small protocol on plain GATT (one write and one notify characteristic, which every BLE library supports, down to 20-byte packets) and keeps the BLE library behind a tiny transport interface. Requests are offset based and safe to repeat, the library is header-only and small enough for boards with 32 KB of RAM, and the same code runs on the Linux PC.

The price: it is a custom protocol, so standard apps and tools cannot talk to it (you need this library or the `tinyblefs` tool on the other side), and its speed depends on the BLE hardware: tens of KB/s between ESP32 boards, about 9 KB/s through an old Bluetooth 4.0 PC adapter and 17 KB/s through a Bluetooth 5 USB dongle. Nothing prevents adding an OTS or L2CAP based transport later for the stacks that support it.

## Quick start

`TinyBLEFS/adapters/BLETransport.h` picks the BLE adapter of your board (ESP32: NimBLE, Pico W: core BLE library, other Arduino boards: ArduinoBLE, Linux: SimpleBLE), so the same sketch runs on all of them.

Server (sharing LittleFS on an ESP32 or Pico W):

```cpp
#include <LittleFS.h>
#include "TinyBLEFS.h"
#include "TinyBLEFS/adapters/BLETransport.h"
#include "TinyBLEFS/backends/ArduinoFS.h"

BLEServerTransport transport;
ArduinoFsBackend<FS> backend(LittleFS);
FileServer server(backend, transport, "sensor1");   // mount name

void setup() { LittleFS.begin(); transport.begin("TinyFS Sensor"); }
void loop()  { server.poll(); }
```

Client:

```cpp
#include "TinyBLEFS.h"
#include "TinyBLEFS/adapters/BLETransport.h"

BLEClientTransport transport;
FileClient client(transport);
Mounts mounts;

void setup() {
  auto found = BLEClientTransport::scan(5000, "TinyFS");  // name prefix
  transport.connect(found[0]);
  mounts.mount(client);                              // -> "/sensor1"

  RemoteFile f = mounts.open("/sensor1/log.txt");
  while (f.available()) Serial.write(f.read());
}
```

## Documentation

| Chapter | Content |
|---|---|
| [Getting started](docs/getting-started.md) | Installation, requirements, examples |
| [Client API](docs/client-api.md) | [`Mounts`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1Mounts.html), [`RemoteFS`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1RemoteFS.html), [`RemoteFile`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1RemoteFile.html), `copy()` |
| [Server API](docs/server-api.md) | [`FileServer`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1FileServer.html), file system backends, security |
| [File systems](docs/filesystems.md) | LittleFS, SPIFFS, FFat, SD, SD_MMC, FatFS, TinyFATFS, Linux: code per platform |
| [Several clients](docs/sessions.md) | `maxSessions`, sessions, keep-alive, limits |
| [Transports](docs/transports.md) | BLE adapters, limits, writing your own |
| [Linux tools](docs/tools.md) | `tinyblefs` client, `tinyblefs-fuse` (mount as a directory) and `tinyblefs-server` |
| [Logging](docs/logging.md) | `TinyBLEFSLogger` |
| [Protocol](docs/protocol.md) | Wire format |
| [Test results](docs/test-results.md) | What was tested on a PC and on hardware, problems found |
| [Class documentation](https://pschatzmann.github.io/TinyBLEFS/annotated.html) | Doxygen API reference

## Supported platforms

| Role | Adapter | Boards |
|---|---|---|
| Server + client | `adapters/NimBLE.h` | ESP32 (NimBLE-Arduino) |
| Server + client | `adapters/ArduinoBLE.h` | UNO R4 WiFi, Nano 33 BLE, other ArduinoBLE boards |
| Server + client | `adapters/PicoW.h` | Raspberry Pi Pico W / Pico 2 W (client: one server at a time) |
| Server + client | `adapters/SimpleBLE.h` | Linux (BlueZ), via [SimpleBLE](https://github.com/OpenBluetoothToolbox/SimpleBLE) |
| Server + client | `adapters/BLETransport.h` | any of the above: selects the adapter of the platform |
| Tests | `adapters/Loopback.h` | any (in memory) |

`TinyBLEFS/adapters/BLETransport.h` includes the right adapter for the platform and names its classes `BLEServerTransport` and `BLEClientTransport` ([Transports](docs/transports.md#common-transport-names)).

## Status

Version 0.1. Tested on a PC with a loopback transport (4 automated tests), by compiling the examples for ESP32, ESP32-S3, Pico W, UNO R4 WiFi and Nano 33 BLE, and by hand on a Linux PC against a **Pico W, UNO R4 WiFi, a classic ESP32 and an ESP32-S3**. The final code (one server class with sessions) has been run on hardware with the two ESP32 boards mounted together; the other boards were tested just before that change. Not tested on hardware: the ArduinoBLE client, several simultaneous real clients. Details and the problems found: [Test results](docs/test-results.md).

## Future improvements

- **Throughput.** Between an ESP32-S3 and an ESP32 about 30 KB/s with 1400-byte and 50-55 KB/s with 4000 to 8000-byte chunks (`setChunkSize`), enough for 128 kbps MP3 (16 KB/s). Through the Linux PC the controller is the limit: about 9 KB/s with a Bluetooth 4.0 controller (27-byte link layer packets), enough for MP3 up to about 64 kbps, and 13-17 KB/s with a Bluetooth 5.1 USB dongle and a 7.5 ms connection interval set in BlueZ ([Bluetooth settings](docs/tools.md#bluetooth-settings)), enough for 128 kbps; that cheap dongle sometimes stalls for 25 s. See the [performance matrix](docs/test-results.md#performance-matrix). Not tuned or measured with a Bluetooth 5 partner yet: the Pico W and ArduinoBLE adapters.
- Hardware tests of the ESP32, the ArduinoBLE client, several simultaneous clients and file locking between real clients.
- A transport on L2CAP channels (NimBLE-Arduino offers them, but disabled by default: `CONFIG_BT_NIMBLE_L2CAP_COC_MAX_NUM` in `nimconfig.h`) or the Object Transfer Service for stacks that support it, announced through a GATT characteristic with a fallback to GATT. Worth trying once the GATT path is tuned.
- A ready-made RAM file system backend for boards without a file system (the UNO R4 WiFi test used a small one in the sketch).
- Listing of the Linux (`std::filesystem`) backend re-reads the directory for each entry.

## Limits

32-bit file sizes and offsets, one request in flight per link, 32-bit targets only (no AVR).

## Development

```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build   # host tests
```

From another CMake project: `add_subdirectory(TinyBLEFS)` or `find_package(TinyBLEFS)`, then link `TinyBLEFS::TinyBLEFS`. The Linux tools are built by default when TinyBLEFS is the top-level project (`-DTINYBLEFS_BUILD_TOOLS=OFF` disables them, `ON` forces them when used as a subproject); see in [Linux tools](docs/tools.md).

The API documentation is generated with Doxygen (`doxygen Doxyfile`, output in `docs/html`). A GitHub Action rebuilds it on every push to `main` and publishes it to the `doxygen` branch (GitHub Pages, [https://pschatzmann.github.io/TinyBLEFS/](https://pschatzmann.github.io/TinyBLEFS/)).

## License

[MIT License](LICENSE.txt). The tools in `tools/` link the [SimpleBLE](https://github.com/OpenBLEcosystem/SimpleBLE) library, which has its own license terms (check them before distributing binaries).
