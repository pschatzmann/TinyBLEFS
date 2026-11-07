# Getting started

## Installation

Copy or clone the library into your Arduino `libraries` folder. It is header-only: include `TinyBLEFS.h` (core) plus the BLE transport (`TinyBLEFS/BLETransport.h`, which selects the adapter of your platform, or one adapter `TinyBLEFS/adapters/<ble>.h` directly, see [Transports](transports.md)) and, for a server, one file system backend (`TinyBLEFS/backends/<fs>.h`, see [File systems](filesystems.md)). Adapters and backends are separate includes so that unused libraries are never required.

On Arduino the public classes ([`FileServer`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1FileServer.html), [`FileClient`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1FileClient.html), [`Mounts`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1Mounts.html), [`RemoteFS`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1RemoteFS.html), [`RemoteFile`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1RemoteFile.html), `LogLevel`, the adapters and backends) are available without the `tinyblefs::` prefix. Only these names are imported, not the whole namespace, so internal names cannot clash with other libraries (SdFat, for example, has its own [`FsFile`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1FsFile.html)). The classes are named so that they do not clash with the `File`, `Client` and `Server` classes of the Arduino cores. Everything else needs `tinyblefs::` (e.g. `tinyblefs::FsFile`, `tinyblefs::Status`). Define `TINYBLEFS_NO_USING_NAMESPACE` before including the library to import nothing.

## Requirements

- 32-bit board (ESP32, RP2040, nRF52, SAMD, Renesas) or Linux, C++11 or newer
- The BLE library of your adapter: [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino), [ArduinoBLE](https://www.arduino.cc/reference/en/libraries/arduinoble/), the arduino-pico `BLE` library (Tools → IP/Bluetooth Stack → *IPv4 + Bluetooth*) or SimpleBLE

## How it works

```
 client sketch ── Mounts ── RemoteFS ── FileClient ── Transport ~~ BLE ~~ Transport ── FileServer ── FsBackend ── SD/LittleFS
```

A **server** advertises one BLE service and answers file requests. A **client** connects to a server, asks for its name and mounts it as `/<name>`. Several servers can be mounted at once (one transport + `FileClient` per server, kept alive while mounted, see `examples/client-list`). The adapters on both sides can differ, e.g. a NimBLE server and a Linux client.

## Examples

The examples use `TinyBLEFS/BLETransport.h`, so they compile for every supported board without changes.

| Example | Board | Shows |
|---|---|---|
| `server-littlefs` | ESP32, Pico W | Share LittleFS |
| `server-tinyfat` | all (UNO R4: make the RAM disk smaller) | Share a FAT RAM disk (TinyFATFS) |
| `client-list` | all (Pico W: one server at a time) | Mount all servers in range, list, read, write |

Pico W: select Tools → IP/Bluetooth Stack → *IPv4 + Bluetooth*.

If a sketch misbehaves, switch on [logging](logging.md): `TinyBLEFSLogger.begin(Serial, LogLevel::Debug);`
