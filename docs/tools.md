# Linux tools

Three programs in `tools/tinyblefs-cli/`, all requiring BlueZ (`bluetoothd`) and [SimpleBLE](https://github.com/OpenBluetoothToolbox/SimpleBLE):

- `tinyblefs`: client. Scans, mounts every TinyBLEFS server it finds and runs a file command.
- `tinyblefs-fuse`: mounts the servers as a normal directory, so the regular file commands (`ls`, `cp`, `cat`, editors ...) work on the remote files (see [below](#mounting-as-a-directory-tinyblefs-fuse)). Also needs libfuse3.
- `tinyblefs-server`: shares a directory of this computer over BLE (see [below](#server-tinyblefs-server)).

## Build

```bash
cmake -S . -B build && cmake --build build   # from the project root: tools are built by default
cmake -S . -B build -DTINYBLEFS_BUILD_TOOLS=OFF   # tests only, no SimpleBLE download
# or standalone:
cmake -S tools/tinyblefs-cli -B build && cmake --build build
```

CMake builds all tools. It uses an installed SimpleBLE or downloads v1.4.0.

`tinyblefs-fuse` is built only if libfuse3 is found (`sudo apt install libfuse3-dev`; re-run cmake after installing it). `-DTINYBLEFS_BUILD_FUSE=OFF` skips it.

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

`tinyblefs-fuse` shows the BLE servers as a directory of this computer (using [FUSE](https://github.com/libfuse/libfuse)). Every program can then use the remote files like local ones, without `tinyblefs` commands.

### Requirements

- libfuse3 to build (`libfuse3-dev`) and the `fuse3` package to run (`fusermount3`).
- Your user must be allowed to use FUSE (access to `/dev/fuse`; on most desktop distributions this is the default).
- `-o allow_other` (other users may access the mount) needs `user_allow_other` in `/etc/fuse.conf`.

### Usage

```
tinyblefs-fuse [options] <mountpoint> [fuse options]
```

| Option | Description |
|---|---|
| `--prefix=<prefix>` | Only servers whose BLE name starts with the prefix |
| `--address=<address>` | Only this BLE address (repeatable) |
| `--scan-time=<ms>` / `--timeout=<ms>` | Scan time (5000) / request timeout (10000) |
| `--chunk=<bytes>` | Data per read/write request (default 400), like `-c` of `tinyblefs` |
| `--log=<level>` | Log level on stderr: `none`, `error` (default), `warning`, `info`, `debug` |
| `-f` | Stay in the foreground (messages stay visible; stop with Ctrl-C) |
| `-d` | FUSE debug output, implies `-f` |
| `-o <opts>` | FUSE mount options, e.g. `allow_other` |
| `-h` | Help |

The mount point must be an existing, empty directory. Unmount with `fusermount3 -u <mountpoint>`.

### Example

```bash
mkdir -p ~/ble
tinyblefs-fuse --prefix=TinyFS ~/ble        # scans, connects, goes to the background
ls ~/ble                                    # sensor1  sensor2   (one directory per server)
ls -l ~/ble/sensor1
cat ~/ble/sensor1/log.txt
cp ~/ble/sensor1/log.txt .                  # download
cp firmware.bin ~/ble/sensor1/              # upload
echo "hello" >> ~/ble/sensor1/notes.txt     # append
mv ~/ble/sensor1/a.txt ~/ble/sensor1/b.txt  # rename
rm ~/ble/sensor1/old.txt
mkdir ~/ble/sensor1/backup
cp -r ~/ble/sensor1/logs ~/ble/sensor2/     # between two servers
fusermount3 -u ~/ble
```

Use `-f --log=info` to watch what happens while debugging:

```bash
tinyblefs-fuse -f --log=info --prefix=TinyFS ~/ble
```

### How it behaves

- The mount point (`/`) lists the servers by their mount names, the same names as in `tinyblefs` (`/<name>/...`). It only contains the servers; files and directories cannot be created there.
- The BLE scan and connect run after the program went into the background, so the first access can wait a few seconds (scan time plus connecting). If no server is found the mount ends again; run with `-f` to see the message.
- The servers are found once, at start; a server that appears or disconnects later is not picked up. Unmount and mount again.
- It is single threaded: one request is in flight per link and all programs share the link, so parallel accesses are served one after the other.

### Limits

- **Speed.** The same as with `tinyblefs` (see [Bluetooth settings](#bluetooth-settings)): tens of KB/s at best, about 9 KB/s with an old controller. Tools that read many files (`grep -r`, thumbnails, indexing, file manager previews) are slow; copy the files first.
- **No permissions, owners or time stamps.** Fixed values are reported (directories 755, files 644, the time the mount started). `chmod`, `chown` and `touch` succeed but change nothing. A read-only server shows up as writable; writes fail with a permission error.
- **No truncate request in the protocol.** `> file`, `open(O_TRUNC)` and `truncate -s 0` work; any other size fails with `ENOTSUP` (operation not supported).
- **Open files.** An open file uses one of the server's open-file slots (default 8). Opening more fails. A file that another client has open for writing is locked and cannot be opened (see [file locking](server-api.md#file-locking)).
- **Sizes** are 32 bit (4 GB).
- No symbolic links, hard links or special files.

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
