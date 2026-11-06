# Getting started

## Installation

Copy or clone the library into your Arduino `libraries` folder. It is header-only: include `TinyBLEFS.h` (core) plus one BLE adapter (`TinyBLEFS/adapters/<ble>.h`, see [Transports](transports.md)) and, for a server, one file system backend (`TinyBLEFS/backends/<fs>.h`, see [File systems](filesystems.md)). Adapters and backends are separate includes so that unused libraries are never required.

On Arduino the public classes (`FileServer`, `FileClient`, `Mounts`, `RemoteFS`, `RemoteFile`, `LogLevel`, the adapters and backends) are available without the `tinyblefs::` prefix. Only these names are imported, not the whole namespace, so internal names cannot clash with other libraries (SdFat, for example, has its own `FsFile`). The classes are named so that they do not clash with the `File`, `Client` and `Server` classes of the Arduino cores. Everything else needs `tinyblefs::` (e.g. `tinyblefs::FsFile`, `tinyblefs::Status`). Define `TINYBLEFS_NO_USING_NAMESPACE` before including the library to import nothing.

## Requirements

- 32-bit board (ESP32, RP2040, nRF52, SAMD, Renesas) or Linux, C++11 or newer
- The BLE library of your adapter: [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino), [ArduinoBLE](https://www.arduino.cc/reference/en/libraries/arduinoble/), the arduino-pico `BLE` library (Tools → IP/Bluetooth Stack → *IPv4 + Bluetooth*) or SimpleBLE

## How it works

```
 client sketch ── Mounts ── RemoteFS ── FileClient ── Transport ~~ BLE ~~ Transport ── FileServer ── FsBackend ── SD/LittleFS
```

A **server** advertises one BLE service and answers file requests. A **client** connects to a server, asks for its name and mounts it as `/<name>`. Several servers can be mounted at once (one transport + `FileClient` per server, kept alive while mounted, see `examples/client-list`). The adapters on both sides can differ, e.g. a NimBLE server and a Linux client.

## Examples

| Example | Board | Shows |
|---|---|---|
| `server-littlefs` | ESP32 | Share LittleFS |
| `client-list` | ESP32 | Mount all servers in range, list, read, write |
| `server-picow` | Pico W | Share LittleFS |
| `client-picow` | Pico W | Mount a server, list, read |
| `server-tinyfat` | ESP32 | Share a FAT RAM disk (TinyFATFS) |
| `client-arduinoble` | UNO R4 WiFi, Nano 33 BLE | Mount all servers in range |

If a sketch misbehaves, switch on [logging](logging.md): `TinyBLEFSLogger.begin(Serial, LogLevel::Debug);`
