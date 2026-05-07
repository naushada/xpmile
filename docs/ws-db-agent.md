# ws-db-agent: WebSocket DB Agent

The `wsdbagent` binary runs on the MongoDB machine and connects **outbound** to the
Heroku-hosted `uniservice` on `/ws/db`. It is the client-side counterpart to the
`WsDbServer` described in `vpn-db-proxy-design.md`.

---

## Topology recap

```
┌──────────────────────────────────────────────────────────┐
│  Heroku Dyno                                             │
│  uniservice  (--remote-db flag set)                      │
│  └─ WsDbServer   WebSocket endpoint: GET /ws/db          │
└──────────────────────────────────────────────────────────┘
          ▲
          │  WSS   wss://yourapp.herokuapp.com/ws/db
          │
┌─────────┴────────────────────────────────────────────────┐
│  MongoDB Machine (behind NAT)                            │
│  wsdbagent                                               │
│  ├─ WebSocket client → connects to Heroku on start       │
│  └─ MongodbClient   → mongod (localhost:27017)           │
└──────────────────────────────────────────────────────────┘
```

---

## Source layout

```
modules/module/wsdbagent/
├── inc/
│   └── wsdbagent.hpp       WsDbAgent class
└── src/
    ├── wsdbagent.cpp       Implementation
    └── wsdbagent_main.cpp  CLI entry point
docker/
└── Dockerfile.wsdbagent   Standalone container image
```

---

## CLI

```
wsdbagent [OPTIONS]

Required:
  --server-host  <host>   Heroku hostname (e.g. myapp.herokuapp.com)
  --mongo-db-uri <uri>    MongoDB connection URI
  --mongo-db-name <name>  Database name

Optional:
  --server-port  <n>      Port (default: 443 with SSL, 8080 without)
  --no-ssl                Use plain TCP instead of TLS
  --mongo-db-connection-pool <n>  Pool size (default: 10)
  --backoff      <secs>   Reconnect wait in seconds (default: 5)
  --help
```

Examples:

```sh
# Production — TLS on port 443
wsdbagent \
  --server-host myapp.herokuapp.com \
  --mongo-db-uri "mongodb://localhost:27017" \
  --mongo-db-name xpmile

# Local dev — plain TCP, no TLS
wsdbagent \
  --server-host localhost \
  --server-port 8080 \
  --no-ssl \
  --mongo-db-uri "mongodb://localhost:27017" \
  --mongo-db-name xpmile_dev
```

---

## Runtime behaviour

### Connect loop

`WsDbAgent::run(backoff_secs)` loops indefinitely:

1. Call `connect_and_handshake()`.
2. On success → enter `run_session()`.
3. On failure or session end → sleep `backoff_secs` seconds → retry.
4. Exit only when `stop()` is called.

### Handshake (`connect_and_handshake`)

1. Open a TCP (or TLS) connection to `--server-host:--server-port`.
2. Send a standard RFC 6455 upgrade request to `/ws/db` with a random 16-byte
   `Sec-WebSocket-Key`.
3. Read the HTTP response headers until `\r\n\r\n`.
4. Verify status `101` and `Sec-WebSocket-Accept` value
   (`Base64(SHA1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"))`).
5. Return `true` to enter the session loop.

### Session loop (`run_session`)

Reads WebSocket frames and dispatches them:

| Opcode | Action |
|--------|--------|
| `0x02` Binary | Parse BSON → `DbRequest` → `dispatch()` → send BSON response |
| `0x09` Ping | Send Pong frame |
| `0x08` Close | Send Close frame → exit session loop |

On any recv error the loop exits and the outer `run()` reconnects after backoff.

### Dispatch (`dispatch`)

Receives a `dbproto::DbRequest` and calls the appropriate `MongodbClient` method.
Returns a BSON-encoded `dbproto::DbResponse`.

| `DbOp` | `MongodbClient` call | Response field |
|--------|----------------------|----------------|
| `CREATE_DOCUMENT` | `create_document(db, coll, json)` | `sval` (OID) |
| `CREATE_BULK_DOCUMENT` | `create_bulk_document(db, coll, json)` | `ival` (count) |
| `UPDATE_COLLECTION` | `update_collection(coll, filter, update)` | `bval` |
| `UPDATE_BULK_DOCUMENT` | `update_bulk_document(coll, filters[], values[])` | `ival` |
| `DELETE_DOCUMENT` | `delete_document(coll, filter)` | `bval` |
| `GET_DOCUMENT` | `get_document(coll, query, projection)` | `sval` (JSON) |
| `GET_DOCUMENTS_QUERIED` | `get_documents(coll, query, projection)` | `sval` (JSON array) |
| `GET_DOCUMENTS_ALL` | `get_documents(coll, projection)` | `sval` (JSON array) |
| `NEXT_AWBNO` | `next_awbno(prefix)` | `sval` (AWB string) |
| `STORE_FILE` | `store_file(name, mime, bytes)` | `sval` (OID) |
| `FETCH_FILE` | `fetch_file(name)` | `data` (bytes) |
| `FETCH_FILE_BY_ID` | `fetch_file_by_id(oid)` | `data` (bytes) |
| `DELETE_FILE` | `delete_file(oid)` | `bval` |

**Serialisation notes** (mirror of `WsMongodbProxy` encoding):

- `CREATE_BULK_DOCUMENT`: `req.doc` carries the raw JSON array string bytes
  (`std::string(req.doc.begin(), req.doc.end())`).
- `UPDATE_BULK_DOCUMENT`: `req.doc` and `req.doc2` each carry a JSON-array string
  (`std::vector<std::string>`) serialised with `nlohmann::json::dump()`. The agent
  parses them back with `json::parse(...).get<std::vector<std::string>>()`.
- `STORE_FILE`: `req.sval` is `"filename|content_type"` (pipe-separated). `req.doc`
  carries the raw file bytes.

### Frame encoding

- Agent → server frames are **masked** (`MASK = 1`) per RFC 6455 §5.3.
- Server → agent frames are **unmasked**.
- Both sides use binary frames (`opcode = 0x2`) for all DB traffic.
- `wsframe::encode()` / `wsframe::decode()` (shared with `WsDbServer`) handle framing.

---

## Building

The agent is a CMake target in the root `CMakeLists.txt`:

```cmake
file(GLOB MODULE_WSDBAGENT_SOURCES "modules/module/wsdbagent/src/*.cpp")

add_executable(wsdbagent
    ${MODULE_WSDBAGENT_SOURCES}
    modules/module/wsdbproxy/src/dbproto.cpp
    modules/module/wsdbproxy/src/wsframe.cpp
)
target_link_libraries(wsdbagent pthread ACE ACE_SSL ssl crypto mongodbcxx z)
```

`mongodbcxx` (static lib built by `modules/module/mongodb/CMakeLists.txt`) provides
`MongodbClient`. `dbproto.cpp` and `wsframe.cpp` are compiled directly into the binary
rather than shared with `uniservice` to keep the agent self-contained.

---

## Docker deployment

Build and run the agent container on the MongoDB machine:

```sh
# Build
podman build -f docker/Dockerfile.wsdbagent -t wsdbagent .

# Run (production — TLS)
podman run -d --name wsdbagent \
  -e ARGS="--server-host myapp.herokuapp.com \
           --mongo-db-uri mongodb://host.containers.internal:27017 \
           --mongo-db-name xpmile" \
  wsdbagent
```

The `CMD` in `Dockerfile.wsdbagent` is:

```dockerfile
CMD /opt/wsdbagent/wsdbagent ${ARGS}
```

All flags are passed via the `ARGS` environment variable.

---

## Sequence diagram

```
wsdbagent                  WsDbServer (Heroku)       MicroService thread
    │                             │                          │
    │  TCP/TLS connect            │                          │
    │────────────────────────────►│                          │
    │  GET /ws/db HTTP/1.1        │                          │
    │  Upgrade: websocket         │                          │
    │────────────────────────────►│                          │
    │◄── HTTP/1.1 101 ────────────│                          │
    │                             │                          │
    │         (session open)      │                          │
    │                             │   MicroService.get_document()
    │                             │◄─────────────────────────│
    │◄── binary frame (DbRequest) │                          │
    │                             │                          │
    │  MongodbClient.get_document()                          │
    │                             │                          │
    │─── binary frame (DbResponse)►│                         │
    │                             │──────────────────────────►│
    │                             │   returns JSON string     │
```
