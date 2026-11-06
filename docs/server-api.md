# Server API

```cpp
FileServer server(backend, transport, "sensor1", "/", 1, 4);
//                file system, link, mount name, shared root, max sessions, max open files
void loop() { server.poll(); }
```

| Argument | Meaning |
|---|---|
| `backend` | The file system to share (see below) |
| `transport` | The BLE adapter (`begin(deviceName)` must be called on it) |
| `name` | Name announced to clients; becomes the mount name `/<name>` |
| `root` | Directory of the file system that clients see as `/` (default `/`) |
| `maxSessions` | Clients at the same time (default 1), see [Several clients](sessions.md) |
| `maxHandles` | Open files per client (default 4) |

Call `server.poll()` often in `loop()`; with ArduinoBLE it also drives the BLE stack. Other methods: `setReadOnly(bool)`, `isReadOnly()`, `setMaxListBytes(n)` (largest answer to a batched directory listing, default 2048 bytes), `setSessionTimeout(ms)`, `sessionCount()`, `closeAll()`.

## File system backends

The file system is given to the constructor, so any file system can be shared: LittleFS, SPIFFS, FFat, SD, SD_MMC, FatFS, TinyFATFS volumes, a Linux directory or your own. See [File systems](filesystems.md) for the backends, code examples per platform and how to write your own.

## Security

- **Path jail**: paths are normalised and any `..` is rejected, so clients cannot leave `root`.
- **Read-only**: `server.setReadOnly(true)` rejects all changes.
- **Encryption**: on NimBLE use `transport.begin(name, true)` to require an encrypted (bonded) link. The other adapters do not enable pairing yet; anyone in range can otherwise read and write the shared files.
- A client's files are closed when it disconnects or its session ends. With the default `maxSessions = 1` the server serves one client at a time; for several clients see [Several clients](sessions.md).

## File locking

The server protects files from concurrent access (all clients of a `FileServer` share one lock table):

- any number of readers, **one writer at a time** (open for write, append, create or truncate);
- opening with mode `w` (truncate) needs a file that is not open at all;
- `remove`, `rename` and `rmdir` fail while the file (or any file below the directory) is open.

A refused request answers `Status::Busy`. Locks end when the file is closed, the client disconnects or its session ends. They also protect the file system itself: FAT must not have a file open twice for writing.
