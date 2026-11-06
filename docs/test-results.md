# Test results

Status of version 0.1 (proof of concept), tested on 2026-11-06. Three levels of testing exist:

1. **PC tests**: automated, no hardware (`ctest`).
2. **Compile checks**: the examples build for the board with `arduino-cli`.
3. **Hardware tests**: boards and a Linux PC (BlueZ, SimpleBLE) were connected by hand.

## PC tests

`cmake -S . -B build && cmake --build build && ctest --test-dir build`: all 4 pass.

| Test | Covers |
|---|---|
| `test_loopback` | Protocol, fragmentation (20-byte packets), server, client, mounts, copy, locking, read-only, path jail, logger, batched directory listing (150 entries in 7 requests instead of 151) and its fallback to a server without it; several clients on one server: 3 clients in threads on a shared link (replies broadcast, sender unknown): concurrent transfers, refused 4th client, private file handles, expiry and rejoin, keep-alive, locks across sessions, concurrent appenders |
| `test_cli` | All commands of the Linux tool over a loopback link with two servers |
| `test_arduinofs` | [`ArduinoFsBackend`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1ArduinoFsBackend.html) with a mock `FS`: POSIX fast path vs generic API (121 entries: about 1-2 ms and no `File` opened, against about 100-150 ms and 7,600 opens) |
| `test_tinyfat` | [`TinyFatBackend`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1TinyFatBackend.html) on a TinyFATFS RAM disk through the full server/client path |

## Compile checks

Everything below builds without errors with the final code (`arduino-cli compile`; ArduinoBLE 2.1.0, NimBLE-Arduino 2.5.2, arduino-pico 6.3.2, esp32 3.3.12).

| Board | Built |
|---|---|
| **ESP32** | examples `server-littlefs`, `client-list`, `server-tinyfat`; the file systems LittleFS, SPIFFS, FFat, SD (SPI), SD_MMC with `ArduinoFsBackend`; TinyFATFS RAM disk and SD over SPI with `TinyFatBackend`; the NimBLE test server |
| **ESP32-S3** | the NimBLE test server and the NimBLE test client |
| **Pico W** | examples `server-picow`, `client-picow`; LittleFS, FatFS and SDFS with `ArduinoFsBackend` |
| **UNO R4 WiFi** | example `client-arduinoble`; an ArduinoBLE server and client sketch |
| **Nano 33 BLE** | the same ArduinoBLE sketch |

## Hardware tests

The Linux PC runs the `tinyblefs` / `tinyblefs-server` tools (SimpleBLE 1.4.0, BlueZ). "Identical" means the SHA-256 of the downloaded file equals the uploaded one. Rows marked † ran before the library was reduced to one server and one client class (plain packets without session id, except the sessions row, which used the same packets as now); they need a repeat.

| Server / client | Adapter, file system | Tested | Result |
|---|---|---|---|
| **ESP32 (classic) and ESP32-S3** servers, one Linux client session (final code) | `NimBLE`, LittleFS | mount both boards at once (`/esp32`, `/esp32s3`); list, read on both; write, append, mkdir; copy within a board and **between the boards** (small file and 8 KB); download back; delete | Works. The 8 KB file copied from one board to the other came back identical |
| **ESP32-S3 client, classic ESP32 server** (no PC) | `NimBLE` both sides, LittleFS | read a 60 KB file with different chunk sizes | Works. Chunk 400: 3.9 KB/s with the adapters as first written, **13.2 KB/s** with the data length extension and a 7.5-15 ms connection interval. With packets limited to 512 bytes: chunk 1400 **30.1 KB/s**, chunk 4000 **50.6 KB/s**, chunk 8000 **55.6 KB/s** |
| **Pico W** server, Linux client † | `PicoW`, LittleFS | scan, mount, list, read, write, append, stat, mkdir, copy, move, delete, rmdir; 100 KB upload and download | Works. 100 KB identical (chunk 400: 1.6 KB/s up, 0.8 KB/s down while the PC was loaded; chunk 4000: 1.9 / 2.0 KB/s) |
| **Pico W** server with sessions, Linux client | `PicoW` (then still with a separate multi-client server class) | handshake, list, read, 20 KB upload and download, idle expiry | Works. 20 KB identical (11.5 s each way). After 40 s idle the session was closed, the next request failed at once and the following one joined again as session 2 with the file intact |
| **Pico W** client, Linux server † | `PicoW` client, `SimpleBLE` server, `std::filesystem` | list, read 2 files, write a new file | Works. Verified by the server log and the written file on the PC |
| **UNO R4 WiFi** server, Linux client † | `ArduinoBLE`, small RAM backend | list, read, write, append, stat, copy, move, delete; 3.5 KB upload and download | Works. 3.5 KB identical (chunk 400: 2.4 s up, 3.7 s down) |
| **ESP32** (classic, 2nd board) server, Linux client † | `NimBLE`, LittleFS (POSIX fast path) | directory listing of 100 files, old tool (one request per entry) vs new tool (batched) | Works. Old: 4.1 s (once 34 s because of a BlueZ stall). New: 2.0 s with the default batch (about 5 requests), 1.5 s with `-c 1500`, 4.2 s with `-c 100`. Other operations were not repeated on this board |
| **ESP32-S3** server, Linux client † | `NimBLE`, LittleFS (POSIX fast path) | list (30 entries about 3.5 s), read, write, append, stat, mkdir, copy, move, delete, rmdir; 100 KB upload and download | File operations work. 100 KB identical with chunk 400 (4.7 KB/s up, 1.9 KB/s down, one D-Bus timeout retried). **Chunk 4000 failed** (requests timed out) |

## Performance matrix

Download speed in KB/s for a sequential read of a 60 KB file (`FileClient::setChunkSize` = the column), measured on 2026-11-06. The first read after a connect is sometimes hit by a BlueZ "D-Bus call timed out" stall (outliers of about 1.5 KB/s are not listed).

| Client (Bluetooth controller) | Server | Server adapter settings | 400 | 1400 | 4000 | 8000 |
|---|---|---|---|---|---|---|
| **Linux PC** (Broadcom, **Bluetooth 4.0**: 27-byte link layer packets, 1M PHY) | ESP32-S3 | interval 7.5-15 ms requested (first version) | 3.8 | 4.4 | 4.7 | n/a |
| Linux PC (same) | ESP32-S3 | **7.5 ms** requested (now the default) | 6.5 | 8.2 | 8.8 | 9.0 |
| Linux PC (same) | Pico W | **7.5 ms** requested, no data length extension | 6.3 | 8.3 | 9.0 | 9.1 |
| Linux PC (same) | Pico W | no interval request (first version) | about 2 for all chunk sizes (100 KB upload and download) | | | |
| Linux PC (same) | UNO R4 WiFi (ArduinoBLE) | 20-byte packets | about 1.5 (3.5 KB in 2.4 s up, 3.7 s down) | | | |
| **ESP32-S3** (Bluetooth 5.0) | classic ESP32 (Bluetooth 4.2) | first version | 3.9 | fails (packets of 514 bytes) | fails | fails |
| ESP32-S3 | classic ESP32 | data length extension 251, 7.5-15 ms | 13.2 | fails | fails | fails |
| ESP32-S3 | classic ESP32 | and packets of at most 512 bytes (now) | 13.2 | 30.1 | 50.6 | 55.6 |

What it shows:

- **The PC's controller is the limit** (about 9 KB/s). The Pico W and the ESP32-S3 give the same speed through it, whatever the chunk size above 1400 bytes, and two ESP32 boards reach 55 KB/s with the same protocol. A Bluetooth 4.0 controller sends 27 bytes per link layer packet, a few packets per 7.5 ms connection event.
- **The connection interval matters**: that controller takes the upper end of the requested range, so asking for 7.5-15 ms gave 15 ms (4.7 KB/s) and asking for exactly 7.5 ms gave 9 KB/s.
- **Chunk size** helps until the link is saturated: 400 → 1400 bytes brings about 30-100 %, beyond 4000 bytes nothing.
- A Bluetooth 4.2 or 5 controller on the PC (data length extension, 2M PHY) is expected to remove the limit; this has not been tried. A different BLE library on the PC (Bleak, Qt, direct D-Bus) would not help: it talks to the same BlueZ and controller, and only about 18 notifications per second arrive at 9 KB/s.

MP3 needs: 64 kbps = 8 KB/s, 96 kbps = 12, 128 kbps = 16, 192 kbps = 24, 320 kbps = 40 KB/s.

| Link | Fastest measured | Fits MP3 up to |
|---|---|---|
| Linux PC with this Bluetooth 4.0 controller | 9 KB/s | 64 kbps (tight) |
| ESP32-S3 ↔ ESP32 | 55 KB/s | 320 kbps |
| Pico W, UNO R4 WiFi with a Bluetooth 5 partner | not measured | – |

## Problems found by the hardware tests

Fixed:

| Problem | Fix |
|---|---|
| Pico W dropped every packet larger than 244 bytes (the host negotiates 511), so writes of more than one packet failed | Receive queue sized for the BLE maximum (514 bytes) |
| NimBLE servers did not advertise their name, so `scan(..., namePrefix)` and `tinyblefs -p` never found them | Name added to the scan response |
| BlueZ reports a bogus ATT MTU of 65533 for the ESP32-S3; the Linux client sent 4 KB as one packet | MTU limited to 517 |
| A transient send failure (D-Bus timeout) aborted a whole transfer | Idempotent requests are retried once after a failed send |
| [`FsFile`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1FsFile.html) of the library clashed with SdFat's `FsFile` (Pico W SD variant did not build) | Only the public class names are imported into the global scope |
| NimBLE sent 514-byte packets, but an ATT attribute value is at most 512 bytes, so every message longer than 512 bytes (chunks of about 500 bytes or more) failed with a timeout | Packets limited to 512 bytes |
| Large stack buffers in `Receiver::poll()` and the ArduinoBLE handlers (UNO R4 has a very small stack) | Moved off the stack |
| A NimBLE server stays invisible to scans when the PC holds a stale connection to it (it accepts one connection and stops advertising) | Not a library bug: the tool disconnects when it exits cleanly; after an interrupted run use `bluetoothctl disconnect <address>` (see below) |

Open (also listed under [Future improvements](../README.md#future-improvements)):

- **Throughput through the PC is limited to about 9 KB/s** by its Bluetooth 4.0 controller (see the performance matrix). A Bluetooth 4.2/5 dongle is expected to fix it. The Pico W and ArduinoBLE adapters have not been measured with a Bluetooth 5 partner.
- **SimpleBLE / BlueZ**: write calls sometimes time out ("D-Bus call timed out", 25 s), especially when the PC is busy (for example during builds). Retrying helps, the cause is unknown.
- **UNO R4 WiFi**: running out of memory crashes the board, and its radio chip then stays stuck until the board is unplugged and replugged. An earlier failure was only the test sketch's RAM file system growing a string; with a fixed buffer 3.5 KB transfers work.
- **Stale BlueZ connection**: if a tool run is killed, BlueZ may keep the link open; a NimBLE server (one connection) then stops advertising and no scan finds it until `bluetoothctl disconnect <address>`.
- The LittleFS of the Pico removes a directory when its last file is deleted, so a following `rmdir` reports "not found".

## Could not be tested

| Board | Why |
|---|---|
| ESP32 (classic), first board | USB power browns out when the radio starts, even with the brownout reset disabled; the board reboots and drops off the USB bus (a second classic ESP32 works) |
| UNO R4 Minima | has no Bluetooth |
| Pico W | not connected during the run with the final code |

Not tested on any hardware: the ArduinoBLE *client*, sessions over ArduinoBLE, several simultaneous real clients, file locking between real clients, SD / SD_MMC / FFat / SPIFFS / FatFS / SDFS / TinyFATFS file systems on real media.
