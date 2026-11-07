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

The Linux PC runs the `tinyblefs` / `tinyblefs-server` tools (SimpleBLE 1.4.0, BlueZ 5.72). "Identical" means the SHA-256 of the downloaded file equals the uploaded one. Only runs with the current code are listed (one server class with sessions, packets of at most 512 bytes).

| Server / client | Adapter, file system | Tested | Result |
|---|---|---|---|
| **ESP32 (classic) and ESP32-S3** servers, one Linux client session | `NimBLE`, LittleFS | mount both boards at once (`/esp32`, `/esp32s3`); list, read on both; write, append, mkdir; copy within a board and **between the boards** (small file and 8 KB); download back; delete | Works. The 8 KB file copied from one board to the other came back identical |
| **ESP32-S3 client, classic ESP32 server** (no PC) | `NimBLE` both sides, LittleFS | read a 60 KB file with chunk sizes 400 to 8000 | Works, up to **55.6 KB/s** (see the performance matrix) |
| **ESP32-S3** server, Linux client | `NimBLE`, LittleFS | read a 60 KB file with chunk sizes 400 to 8000, through the built-in Bluetooth 4.0 controller and through a Bluetooth 5.1 USB dongle | Works; every download identical. Up to 9 KB/s (built-in) and 17.7 KB/s (dongle) |

## Performance matrix

Download speed in KB/s for a sequential read of a 60 KB file (`FileClient::setChunkSize` = the column), with the current adapter defaults (7.5 ms connection interval requested; NimBLE also asks for the data length extension, 251 bytes). Measured on 2026-11-06.

| Client (Bluetooth controller) | Server | 400 | 1400 | 4000 | 8000 |
|---|---|---|---|---|---|
| **Linux PC**, built-in Broadcom (**Bluetooth 4.0**: 27-byte link layer packets, 1M PHY) | ESP32-S3 | 6.5 | 8.2 | 8.8 | 9.0 |
| Linux PC, same controller | Pico W (no data length extension; measured with the code before sessions, needs a repeat) | 6.3 | 8.3 | 9.0 | 9.1 |
| **Linux PC**, USB dongle "USB2.0-BT" (Barrot 8041a02, sold as CSR, **Bluetooth 5.1**), BlueZ connection interval set to 7.5 ms (see below) | ESP32-S3 | n/a | 13.3 | 17.7 | 17.1 |
| **ESP32-S3** (Bluetooth 5.0) | classic ESP32 (Bluetooth 4.2) | 13.2 | 30.1 | 50.6 | 55.6 |

The dongle row shows the typical runs; runs hit by a stall (below) reach only about 1.7 KB/s. With the BlueZ defaults the same dongle gave at best 9.2 / 11.4 / 13.6 KB/s (chunk 1400 / 4000 / 8000), and about 40 % of the runs stalled. With `MinConnectionInterval=6`, `MaxConnectionInterval=6` and `ConnectionLatency=0` in the `[LE]` section of `/etc/bluetooth/main.conf` (see [Linux tools](tools.md#bluetooth-settings)) 10 of 12 runs were clean (13-17.7 KB/s); each of the 2 others stalled once for 25 s and finished after the retry.

What it shows:

- **The PC's controller is the limit.** Through the built-in Bluetooth 4.0 controller the ESP32-S3 (and, with the earlier code, the Pico W) gives 9 KB/s; the Bluetooth 5.1 dongle doubles that to 17 KB/s, and two ESP32 boards reach 55 KB/s with the same protocol.
- **The connection interval matters.** BlueZ and the controllers take the upper end of the range the server asks for; the adapters therefore ask for exactly 7.5 ms, and on the PC side setting the BlueZ default to 7.5 ms brought the dongle from about 12 to 17 KB/s.
- **Chunk size** helps until the link is saturated: 400 → 1400 bytes brings about 30-100 %, beyond 4000 bytes little or nothing.
- **The dongle is a cheap clone**: the kernel reports "Couldn't suspend the device for our Barrot 8041a02 receive-issue workaround"; writes sometimes stall (BlueZ "D-Bus call timed out"), and once the dongle switched itself off and stayed off until it was replugged (that run had USB autosuspend disabled). A dongle with a genuine chip (Realtek RTL8761B, Intel) is likely more reliable; not tried.

MP3 needs: 64 kbps = 8 KB/s, 96 kbps = 12, 128 kbps = 16, 192 kbps = 24, 320 kbps = 40 KB/s.

| Link | Fastest measured | Fits MP3 up to |
|---|---|---|
| Linux PC, built-in Bluetooth 4.0 controller | 9 KB/s | 64 kbps (tight) |
| Linux PC, Bluetooth 5.1 USB dongle, tuned BlueZ | 17.7 KB/s | 128 kbps (tight, stalls possible) |
| ESP32-S3 ↔ ESP32 | 55 KB/s | 320 kbps |
| Pico W, UNO R4 WiFi with a Bluetooth 5 partner | not measured | – |

## Open problems

Also listed under [Future improvements](../README.md#future-improvements):

- **Throughput through the PC** is limited by its Bluetooth controller: 9 KB/s with the built-in Bluetooth 4.0 controller, 17 KB/s with a Bluetooth 5.1 dongle (see the performance matrix). The Pico W and ArduinoBLE adapters have not been measured with a Bluetooth 5 partner.
- **SimpleBLE / BlueZ**: write calls sometimes time out ("D-Bus call timed out", 25 s), with both controllers, more often with the cheap dongle and when the PC is busy. The retry recovers; the cause is unknown (the dongle not reporting sent packets is likely, a `btmon` trace would show it).
- **Stale BlueZ connection**: if a tool run is killed, BlueZ may keep the link open; a NimBLE server (one connection) then stops advertising and no scan finds it until `bluetoothctl disconnect <address>`.
- The LittleFS of the Pico removes a directory when its last file is deleted, so a following `rmdir` reports "not found".

## Not tested with the current code

| What | Why |
|---|---|
| **UNO R4 WiFi** (ArduinoBLE server) | only tested before the library was reduced to one server and one client class; then running out of memory crashed the board and left its radio chip stuck until replugged, with a fixed buffer 3.5 KB transfers worked. Needs a repeat |
| **Pico W** file operations, sessions, the Pico W client, the Linux server (`tinyblefs-server`) | same: only tested with the earlier code (all worked). Needs a repeat |
| ESP32 (classic), first board | USB power browns out when the radio starts, even with the brownout reset disabled; the board reboots and drops off the USB bus (a second classic ESP32 works) |
| UNO R4 Minima | has no Bluetooth |

Not tested on any hardware: the ArduinoBLE *client*, several simultaneous real clients, file locking between real clients, SD / SD_MMC / FFat / SPIFFS / FatFS / SDFS / TinyFATFS file systems on real media.
