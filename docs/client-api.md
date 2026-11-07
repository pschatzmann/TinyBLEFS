# Client API

## Connecting

```cpp
BLEClientTransport transport;      // TinyBLEFS/BLETransport.h, or an adapter class
FileClient client(transport, 5000);    // request timeout in ms
transport.connect(BLEClientTransport::scan(5000, "TinyFS")[0]);
```

[`FileClient`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1FileClient.html) settings: `setTimeout(ms)`, `setChunkSize(bytes)` (data per request and read-ahead size, default 400; larger values are much faster on a good link: 4000 gave 50 KB/s between two ESP32 boards, if the server has the RAM for the answer), `setIdleCallback(fn, ctx)` (called while waiting for an answer).

## Mounts

[`Mounts`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1Mounts.html) maps `/<name>/path` to the right server.

```cpp
Mounts mounts;
std::string name = mounts.mount(client);        // name announced by the server
mounts.mount(client2, "alias");                 // or choose your own
```

Names are limited to 16 characters of `A-Z a-z 0-9 - _` (others become `_`). A duplicate gets a suffix: `sensor1`, `sensor1-2`, ... `mount()` returns an empty string if the server does not answer.

| Method | Description |
|---|---|
| `open(path, mode="r")` | Returns a [`RemoteFile`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1RemoteFile.html) (modes `r`, `r+`, `w`, `a`) |
| `openWait(path, mode, timeoutMs=5000, retryMs=100)` | Like `open`, but retries while the file is locked by another client (see below) |
| `exists`, `stat(path, isDir, size)` | Type and size |
| `list(path, entries)` | `std::vector<DirEntry>` without opening the entries, fetched in batches (one request returns about 28 entries with the default chunk size); `"/"` lists the mounts |
| `remove`, `mkdir`, `rmdir` | Return `true` on success |
| `rename(from, to)` | Both paths must be on the same server |
| `copy(from, to)` | File or directory tree, also between servers; overwrites files |
| `names()`, `get(name)`, `unmount(name)` | Manage mounts |

[`RemoteFS`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1RemoteFS.html) offers the same methods for a single server without the `/<name>` prefix. To use several servers, mount one `FileClient` per server in the same `Mounts`.

## File

Arduino-like: `read()`, `read(buf, len)`, `peek()`, `available()`, `write(byte)`, `write(buf, len)`, `print(str)`, `println(str)`, `seek(pos)`, `position()`, `size()`, `close()`, `isDirectory()`, `name()`, `path()`, `openNextFile()`, `rewindDirectory()`.

- Copies share the same open file; `close()` closes it for all copies. A file is also closed when the last copy goes out of scope.
- Reads are buffered (read-ahead); writes are sent immediately.
- Mode `a` starts at the end of the file.

```cpp
RemoteFile f = mounts.open("/sensor1/log.txt", "a");
if (f) f.println("hello");
```

## Errors and locking

Operations return `false`, an invalid `RemoteFile` (converts to `false`) or `0` bytes; the reason (not found, denied, timeout, ...) is written to the [log](logging.md). `FileClient` methods return a `Status`; `statusName(status)` gives a readable text.

The server allows one writer per file ([file locking](server-api.md#file-locking)): `open(path, "a")` fails with "busy" while another client writes the file. For files that several clients append to, use `openWait`:

```cpp
RemoteFile f = mounts.openWait("/sensor1/log.txt", "a", 5000);  // retries while busy
if (f) f.println("value");
f.close();                                                       // releases the lock
```

Keep write handles short-lived so that others do not wait long.
