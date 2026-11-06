# Protocol

Definitions are in `src/TinyBLEFS/Protocol.h` and `Session.h`. Version 1.

## GATT

One service `6e1f0001-7a3b-4c5d-8e9f-0a1b2c3d4e5f` with two characteristics:

| UUID | Direction | Properties |
|---|---|---|
| `6e1f0002-...` (RX) | client → server | write, write without response |
| `6e1f0003-...` (TX) | server → client | notify |

## Message

BLE delivers packets reliably and in order, so a message is just cut into packets of at most the MTU and reassembled with the length field.

```
[op:1][reqId:1][status:1][payloadLen:2 LE][payload...]
```

Responses repeat `reqId`, set `op | 0x80` and carry a status. There is one request in flight per client.

Every packet on the link starts with the **session id** of the client (`[sessionId:1][message bytes...]`, so the message is cut into packets of MTU − 1 bytes). A client first joins a session with control packets (session id 0), see [Several clients](sessions.md#wire-format).

## Operations

All numbers are little endian. File access is **offset based**, so a repeated read or write is harmless (info, read, write, stat and list are retried once after a timeout).

| Op | Request payload | Response payload |
|---|---|---|
| 1 Info | – | version, maxHandles, readOnly, name |
| 2 Open | flags, path | handle, size:4 |
| 3 Close | handle | – |
| 4 Read | handle, offset:4, len:2 | data |
| 5 Write | handle, offset:4, data | written:2 |
| 6 Stat | path | isDir, size:4 |
| 7 List | index:2, path | isDir, size:4, name (one entry) |
| 8 Remove / 9 Mkdir / 10 Rmdir | path | – |
| 11 Rename | fromLen:2, from, to | – |
| 12 ListBatch | index:2, maxBytes:2, path | flags (bit 0: last entry included), then entries `isDir:1, size:4, nameLen:1, name` as many as fit into maxBytes (at least one). A client falls back to op 7 if the server does not know op 12. |

Open flags: 1 read, 2 write, 4 append (client side), 8 create, 16 truncate.

## Status codes

0 ok, 1 error, 2 not found, 3 bad request, 4 no handle, 5 denied, 6 exists, 7 I/O error, 8 end of list, 9 is a directory, 10 busy (locked). `Open` of a directory answers 9.

## Locking

The server answers *busy* when a request conflicts with its file locks (rules: [Server API](server-api.md#file-locking)).

## Limits

Payload ≤ 65535 bytes, offsets and sizes 32 bit, paths are relative to the server's shared root and may not contain `..`.
