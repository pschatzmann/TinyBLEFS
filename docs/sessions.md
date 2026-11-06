# Several clients

[`FileServer`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1FileServer.html) serves as many clients at the same time as its `maxSessions` argument allows (default 1):

```cpp
FileServer server(backend, transport, "picow", "/", 2);   // up to 2 clients at the same time
//                                    name,   root, maxSessions, maxHandles = 4

void setup() { /* ... */ transport.begin("TinyBLEFS Pico", 2); }   // 2 BLE connections
void loop()  { server.poll(); }
```

With 1 session a second client is refused ("the server has no free session"); it can try again when the first has gone. The BLE adapter must accept as many connections: `begin(name, maxConnections)` on the ArduinoBLE and Pico W server adapters (the hardware may allow fewer, 3 on the Nano 33 BLE). NimBLE and SimpleBLE servers accept one connection.

The client needs nothing special: [`FileClient`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1FileClient.html) joins a session by itself with its first request.

## Sessions

Every client works in its own **session**: its own open files and message buffer. The clients share the file system and its [locks](server-api.md#file-locking). The session id is the first byte of every packet, which is what lets several clients share one BLE link, also on stacks that can neither tell which client sent a packet nor address a reply to one client (ArduinoBLE, Pico W).

- The server closes a session, and with it the open files, after 60 s without packets (`setSessionTimeout(ms)`). A client that is idle calls `client.keepAlive()` at least once in that time.
- If the session was closed, the next request fails at once and the one after joins again with a new session. Files that were open are invalid afterwards ([`RemoteFile`](https://pschatzmann.github.io/TinyBLEFS/classtinyblefs_1_1RemoteFile.html) notices and never uses a file of the new session).
- When the BLE link goes down, all sessions end.
- `server.sessionCount()` tells how many clients are connected.

## Limits

- **No privacy between clients.** The stack broadcasts every reply to all connected clients, so each client sees the other clients' file contents. Pairing/encryption does not change this. Use more than one session only for clients that trust each other.
- Throughput per client drops with the number of clients, and every packet carries one more byte.
- Tested with a loopback hub that behaves like these stacks (`tests/test_loopback.cpp`) and, with one client, on hardware. Several simultaneous real clients, and whether a controller accepts a second connection while advertising, are not tested yet.

## Wire format

Packets are `[session id][data]`; id 0 marks control packets:

| Packet | Direction | Meaning |
|---|---|---|
| `[0][1][nonce:4]` | client → server | Hello: asks for a session |
| `[0][2][nonce:4][id]` | server → client | Welcome: the client keeps the one with its nonce |
| `[0][3][nonce:4]` | server → client | Full: `maxSessions` reached |
| `[0][4][id]` | server → client | Expired: unknown session |
| `[0][5][id]` | client → server | Keep alive |

A repeated hello (lost welcome) gets the same session. Ids are handed out round robin so that an id is not reused soon after a session ended.
