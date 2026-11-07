# Linux tools

Two programs in `tools/tinyblefs-cli/`, both requiring BlueZ (`bluetoothd`) and [SimpleBLE](https://github.com/OpenBluetoothToolbox/SimpleBLE):

- `tinyblefs`: client. Scans, mounts every TinyBLEFS server it finds and runs a file command.
- `tinyblefs-fuse`: mounts the servers as a normal directory (see [below](#mounting-as-a-directory-tinyblefs-fuse)); also needs libfuse3.
- `tinyblefs-server`: shares a directory of this computer over BLE (see [below](#server-tinyblefs-server)).

## Build

```bash
cmake -S . -B build && cmake --build build   # from the project root: tools are built by default
cmake -S . -B build -DTINYBLEFS_BUILD_TOOLS=OFF   # tests only, no SimpleBLE download
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

## Mounting as a directory: tinyblefs-fuse

```bash
sudo apt install libfuse3-dev fuse3        # build and run (re-run cmake after installing)
mkdir -p ~/ble
tinyblefs-fuse --prefix=TinyFS ~/ble       # scans, connects, goes to the background
ls ~/ble                                   # sensor1  sensor2
cp ~/ble/sensor1/log.txt .
echo hi >> ~/ble/sensor1/notes.txt
fusermount3 -u ~/ble                       # unmount
```

The mount point lists the servers as subdirectories and all the usual file commands work. Options: `--prefix=`, `--address=`, `--scan-time=`, `--timeout=`, `--chunk=` and `--log=` (same meaning as for `tinyblefs`), plus the FUSE options (`-f` foreground, `-d` debug, `-o allow_other`). The BLE scan runs after the program went into the background, so the first access waits a few seconds; if no server is found the mount ends again (use `-f` to see the message).

Limits:

- BLE speed: tens of KB/s at best; tools that read many files (`grep -r`, thumbnails) are slow.
- No permissions, owners or time stamps: fixed values are reported; `chmod`, `chown` and `touch` are accepted and ignored.
- The protocol has no truncate request: `> file` and `truncate -s 0` work, other sizes fail (`ENOTSUP`).
- An open file uses one of the server's open-file slots; too many open files fail.
- Runs single threaded (one request in flight per link). The `/` directory only holds the mounts: you cannot create files or directories there.

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
