# Linux tools

Two programs in `tools/tinyblefs-cli/`, both requiring BlueZ (`bluetoothd`) and [SimpleBLE](https://github.com/OpenBluetoothToolbox/SimpleBLE):

- `tinyblefs`: client. Scans, mounts every TinyBLEFS server it finds and runs a file command.
- `tinyblefs-server`: shares a directory of this computer over BLE (see [below](#server-tinyblefs-server)).

## Build

```bash
cmake -S . -B build -DTINYBLEFS_BUILD_TOOLS=ON && cmake --build build   # from the project root
# or standalone:
cmake -S tools/tinyblefs-cli -B build && cmake --build build
```

CMake builds both tools. It uses an installed SimpleBLE or downloads v1.4.0.

## Usage

```
tinyblefs [options] <command> [args]
tinyblefs [options] shell        # interactive: connects once, supports cd and relative paths
tinyblefs [options] scan         # list servers in range
```

| Option | Description |
|---|---|
| `-p <prefix>` | Only servers whose BLE name starts with the prefix |
| `-a <address>` | Only this BLE address (repeatable) |
| `-t <ms>` / `-T <ms>` | Scan time (5000) / request timeout (10000) |
| `-c <bytes>` | Data per read/write request and size of a directory listing batch (default 400); larger values need fewer round trips |
| `-l <level>` | Log level on stderr: `none`, `error` (default), `warning`, `info`, `debug` |
| `-v` | Same as `-l info` |

With several Bluetooth adapters the tools use the first powered one. To choose, set `TINYBLEFS_ADAPTER` to its name or address: `TINYBLEFS_ADAPTER=hci1 tinyblefs ls /` (`bluetoothctl list`, `hciconfig` show them). This applies to `tinyblefs-server` too.

## Commands

Paths are `/<mount>/<path>`; `/` lists the mounts.

| Command | Description |
|---|---|
| `mounts` | Mounted servers, protocol, open-file limit, access |
| `ls [-l] [-R] [path...]` | List |
| `stat`, `cat <path...>` | Type/size, print file |
| `get [-r] <remote> <local>` | Download file (`-r`: tree) |
| `put [-r] <local> <remote>` | Upload file (`-r`: tree) |
| `cp [-r] <src> <dst>` | Copy, also between servers |
| `mv <src> <dst>` | Move; between servers it copies then deletes |
| `rm [-r] <path...>` | Delete |
| `mkdir [-p]`, `rmdir`, `touch` | Directories, empty file |
| `write <file> <text...>`, `append <file> <text...>` | Write text |
| `pwd`, `cd <dir>`, `help` | Shell helpers |

If the destination of `cp`, `mv`, `put` or `get` is an existing directory, the file is placed inside it.

## Example

```bash
tinyblefs -p TinyFS mounts
tinyblefs -p TinyFS put firmware.bin /sensor1/firmware.bin
tinyblefs -p TinyFS cp -r /sensor1/logs /sensor2/backup
tinyblefs shell
```

## Bluetooth settings

The speed through a PC depends mostly on its Bluetooth controller (see the [performance matrix](test-results.md#performance-matrix)): about 9 KB/s with a Bluetooth 4.0 controller, 17 KB/s with a Bluetooth 5 USB dongle. With BlueZ's defaults the dongle reached only about 12 KB/s and stalled more often; asking for a 7.5 ms connection interval helped. In `/etc/bluetooth/main.conf` (units of 1.25 ms):

```ini
[LE]
MinConnectionInterval=6
MaxConnectionInterval=6
ConnectionLatency=0
```

then `sudo systemctl restart bluetooth`. If an adapter stays `DOWN` afterwards, check `rfkill list bluetooth` (`rfkill unblock bluetooth`) and `bluetoothctl power on`.

## Server: tinyblefs-server

```bash
bluetoothctl system-alias "TinyFS PC"     # BLE name seen by client scans
tinyblefs-server -n pc /srv/share            # mount name "pc"
```

| Option | Description |
|---|---|
| `-n <name>` | Name announced to clients, the mount name (default: host name) |
| `-r` | Read-only |
| `-f <n>` | Max simultaneously open files (default 8) |
| `-P <bytes>` | Bytes per notification (default 20) |
| `-D <ms>` | Pause after each notification (default 10) |
| `-l <level>` | Log level on stderr (default `info`) |

Clients (boards or `tinyblefs`) then see the directory as `/pc/...`. Only that directory is reachable; the server serves one client at a time. It needs an adapter and BlueZ version that can advertise and host a GATT application, and permission to use it on D-Bus (normally any desktop user).

SimpleBLE can neither set the BLE name nor read the connection's MTU, hence the alias and the small default packets. If the client negotiated a larger MTU, raise `-P` (up to MTU − 3) and lower `-D`.
