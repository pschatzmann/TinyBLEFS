# Transports

A transport carries packets between one client and one server. The library never uses a BLE library directly; only the adapters do.

## Common transport names

`TinyBLEFS/BLETransport.h` includes the adapter of the current platform and gives its classes platform independent names, so one sketch compiles everywhere:

```cpp
#include "TinyBLEFS.h"
#include "TinyBLEFS/BLETransport.h"

BLEServerTransport server;                         // server side
server.begin("TinyFS Sensor");

auto found = BLEClientTransport::scan(5000, "TinyFS");  // client side
BLEClientTransport client;
client.connect(found[0]);
```

| Name | Meaning |
|---|---|
| `BLEServerTransport` | server (peripheral) transport of the platform |
| `BLEClientTransport` | client (central) transport of the platform |
| `BLEScanResult` | element type returned by `BLEClientTransport::scan()` |
| `TINYBLEFS_TRANSPORT_NAME` | name of the selected adapter, e.g. `"NimBLE"` |

| Platform | Selected adapter |
|---|---|
| ESP32 (`ARDUINO_ARCH_ESP32`, `ESP_PLATFORM`) | `NimBLE.h` |
| Raspberry Pi Pico W / Pico 2 W (`ARDUINO_ARCH_RP2040`) | `PicoW.h` |
| any other Arduino board | `ArduinoBLE.h` |
| desktop (no `ARDUINO`) | `SimpleBLE.h` |

To choose another adapter (e.g. ArduinoBLE on an ESP32), define one of `TINYBLEFS_TRANSPORT_NIMBLE`, `TINYBLEFS_TRANSPORT_ARDUINOBLE`, `TINYBLEFS_TRANSPORT_PICOW` or `TINYBLEFS_TRANSPORT_SIMPLEBLE` before the include. Only the common API below is the same on every platform; the adapter specific extras (`setPacketSize`, `maxConnections`, ...) remain available but make a sketch platform specific.

## Adapters

All server adapters have `begin(deviceName)`. All client adapters have `scan(durationMs, namePrefix)` and `connect(device)`. The prefix filters on the **BLE device name** given to `begin()` (not on the mount name, which is only known after connecting).

| Adapter (`TinyBLEFS/adapters/`) | Classes | Notes |
|---|---|---|
| `NimBLE.h` | [`NimBLEServerTransport`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1NimBLEServerTransport.html), [`NimBLEClientTransport`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1NimBLEClientTransport.html) | Server tested on an ESP32-S3. Negotiates MTU 517 and asks for the data length extension (251 bytes) and a 7.5-15 ms connection interval, packets of at most 512 bytes (the ATT limit). Between two ESP32 boards this gave 13 KB/s with 400-byte chunks, 30 KB/s with 1400 and about 50 KB/s with 4000 (`FileClient::setChunkSize`); `begin(name, true)` requires encryption; several client connections (limit: NimBLE config, default 3) |
| `ArduinoBLE.h` | [`ArduinoBLEServerTransport`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1ArduinoBLEServerTransport.html), [`ArduinoBLEClientTransport`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1ArduinoBLEClientTransport.html) | Server tested on a UNO R4 WiFi. That board has 32 KB of RAM: keep files, chunk sizes and the queue small, because running out of memory crashes it, and after a crash its radio chip needs a power cycle (unplug/replug) before it advertises again. Polled stack (requests and `server.poll()` run `BLE.poll()`). The MTU cannot be read, so packets are 20 bytes; use `setPacketSize(244)` if both sides negotiate MTU 247. `begin(name, maxConnections)`: for several clients also give the [`FileServer`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1FileServer.html) `maxSessions` ([Several clients](sessions.md)) |
| `PicoW.h` | [`PicoWServerTransport`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1PicoWServerTransport.html), [`PicoWClientTransport`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1PicoWClientTransport.html) | The core has a single `BLEClient`, so a client mounts **one** server at a time. The server tracks its connections itself and sends with BTstack `att_server_notify`. `begin(name, maxConnections)`: for several clients also give the `FileServer` `maxSessions` ([Several clients](sessions.md)) |
| `SimpleBLE.h` | [`SimpleBLEClientTransport`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1SimpleBLEClientTransport.html), [`SimpleBLEServerTransport`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1SimpleBLEServerTransport.html) | Desktop via SimpleBLE, aimed at Linux/BlueZ (other OSes untested). The server has `begin()` without a name (BlueZ advertises the adapter alias; `begin(name)` exists for the common API and ignores the name), packets of 20 bytes (`setPacketSize`) and paced notifications (`setPacingMs`, default 10 ms) because SimpleBLE has no flow control or MTU information. Details: [Linux tools](tools.md). Tested with a Pico W as the other end |
| `Loopback.h` | [`LoopbackPair`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1LoopbackPair.html) | In-memory link for tests: `client()` / `server()` |

Received packets are buffered (`TINYBLEFS_RX_SLOTS` = 24 packets of up to `TINYBLEFS_RX_PACKET` = 244 bytes; define before including to change). Lost packets are reported as warnings.

## Writing an adapter

Derive from `tinyblefs::Transport` (`Transport.h`):

| Method | Description |
|---|---|
| `size_t mtu() const` | Max bytes per packet (ATT MTU − 3) |
| `bool send(data, len)` | Send one packet (write without response on the client, notification on the server) |
| `size_t receive(buf, max)` | Non-blocking; copy the next received packet, return 0 if none |
| `bool connected() const` | Link state (default `true`) |
| `void poll()` | Optional: run the BLE event loop of polled stacks |

Use the GATT layout from `Protocol.h`: service `kServiceUUID`, RX characteristic `kRxUUID` (client → server, write / write without response), TX characteristic `kTxUUID` (server → client, notify). Callbacks must only store packets; `RingQueue.h` is a ready-made allocation-free queue. Messages are fragmented and reassembled by the library.

## Performance

Throughput depends on the MTU, the connection interval and the stack; with 20-byte packets expect only a few KB/s. Prefer adapters that negotiate a large MTU (NimBLE, Pico W server).

Measured so far (details: the performance matrix in [Test results](test-results.md#performance-matrix)):

| Link | Speed |
|---|---|
| ESP32-S3 client ↔ ESP32 server, chunk 400 / 1400 / 4000 / 8000 | 13 / 30 / 51 / 56 KB/s |
| Linux PC (Bluetooth 4.0 controller) ↔ ESP32-S3, chunk 1400 or more | 8-9 KB/s |
| Linux PC (Bluetooth 5.1 USB dongle, BlueZ interval 7.5 ms) ↔ ESP32-S3, chunk 4000 or more | 13-17.7 KB/s |

The speed depends mostly on the weakest side: a controller without the data length extension (Bluetooth 4.2) sends only 27 bytes per link layer packet, and the server's requested connection interval counts (the adapters ask for 7.5 ms). 128 kbps MP3 needs 16 KB/s.
