# Design: WebSocket DB Proxy — Remote MongoDB over WebSocket Tunnel

## Problem

MongoDB is deployed on a machine behind NAT. The `uniservice` container runs on Heroku,
which exposes exactly **one TCP port** (`$PORT`) via an HTTP-aware router. Raw TCP/UDP
(including OpenVPN) is not reachable from outside. The MongoDB machine must be able to
initiate an outbound connection that works through NAT and through Heroku's router.

## Solution

Use a **persistent WebSocket connection** as the tunnel. WebSocket starts as an HTTP
`Upgrade` request, so it passes transparently through Heroku's router. The MongoDB-side
agent initiates the connection outbound (works through any NAT). All DB operations are
framed as WebSocket binary messages containing BSON envelopes — identical to the original
BSON protocol design.

---

## Topology

```
┌──────────────────────────────────────────────────────────┐
│  Heroku Dyno                                             │
│                                                          │
│  uniservice  (single process, single port $PORT)         │
│  ├─ WebServer / MicroService  (existing HTTP)            │
│  └─ WsDbServer  (ACE_Task)                               │
│       WebSocket endpoint: GET /ws/db HTTP/1.1            │
│       Upgrade: websocket                                 │
└──────────────────────────────────────────────────────────┘
          ▲
          │  WSS  wss://yourapp.herokuapp.com/ws/db
          │  (HTTP Upgrade → Heroku router passes it through)
          │
┌─────────┴────────────────────────────────────────────────┐
│  MongoDB Machine (behind NAT)                            │
│                                                          │
│  ws-db-agent  (new binary, implemented in next phase)    │
│  ├─ WebSocket client → connects to Heroku URL on start   │
│  └─ MongodbClient → mongod (localhost:27017)             │
└──────────────────────────────────────────────────────────┘
```

### Why WebSocket fits Heroku perfectly

| Constraint | OpenVPN | WebSocket |
|---|---|---|
| Heroku single TCP port | Fails — needs separate UDP 1194 | Works — same port as HTTP |
| HTTP-aware router | Fails — not HTTP | Works — starts as HTTP Upgrade |
| NAT traversal | Requires client-initiates model | Client initiates by design |
| TLS | Own PKI, certs, ta.key | Heroku edge terminates TLS (free) |
| Docker privileges | `NET_ADMIN`, `/dev/net/tun` | None |

---

## WebSocket protocol (RFC 6455 subset we implement)

### Handshake

The `ws-db-agent` sends a standard WebSocket upgrade request to `/ws/db`:

```
GET /ws/db HTTP/1.1\r\n
Host: yourapp.herokuapp.com\r\n
Upgrade: websocket\r\n
Connection: Upgrade\r\n
Sec-WebSocket-Key: <base64-16-random-bytes>\r\n
Sec-WebSocket-Version: 13\r\n
\r\n
```

`WebConnection::handle_input()` detects `Upgrade: websocket` + URI `/ws/db`,
computes the accept key, and sends:

```
HTTP/1.1 101 Switching Protocols\r\n
Upgrade: websocket\r\n
Connection: Upgrade\r\n
Sec-WebSocket-Accept: <Base64(SHA1(key + GUID))>\r\n
\r\n
```

Accept key formula: `Base64( SHA1( Sec-WebSocket-Key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11" ) )`

OpenSSL (`SHA1()` + `BIO_f_base64`) is already linked — no new dependencies.

### Frame format

```
 Byte 0          Byte 1          Bytes 2-9 (optional)
 ┌───────────────┬───────────────┬─────────────────────────────────┐
 │FIN RSV opcode │MASK paylen    │ Extended length + Masking-key   │
 └───────────────┴───────────────┴─────────────────────────────────┘
```

Rules for our implementation:
- `FIN = 1` always (no fragmentation — one frame per BSON message)
- `opcode = 0x2` (binary frame)
- `MASK = 1` for agent→server frames (RFC 6455 requirement for clients)
- `MASK = 0` for server→agent frames
- Payload length encoding:
  - `len < 126` → 1 byte
  - `126 ≤ len < 65536` → byte `126` + uint16 BE
  - `len ≥ 65536` → byte `127` + uint64 BE (needed for file operations)

### Ping/Pong

`WsDbServer` sends a Ping frame (opcode `0x9`) every 30 s. The agent replies with
Pong (opcode `0xA`). If three consecutive pings go unanswered, the connection is
considered dead and `WsDbServer` drops it and waits for the agent to reconnect.

---

## C++ design — server side

### 1. `IMongodbClient` — abstract interface  *(unchanged from original design)*

Extracted from the current `MongodbClient` public API.

```cpp
// modules/module/mongodb/inc/mongodbc.hpp
class IMongodbClient {
public:
  virtual ~IMongodbClient() = default;
  virtual std::string  create_document(const std::string &db, const std::string &coll, const std::string &json) = 0;
  virtual std::int32_t create_bulk_document(const std::string &db, const std::string &coll, const std::string &json) = 0;
  virtual bool         update_collection(const std::string &coll, const std::string &filter, const std::string &update) = 0;
  virtual std::int32_t update_bulk_document(const std::string &coll, const std::string &filters, const std::string &values) = 0;
  virtual bool         delete_document(const std::string &coll, const std::string &filter) = 0;
  virtual std::string  get_document(const std::string &coll, const std::string &query, const std::string &projection) = 0;
  virtual std::string  get_documents(const std::string &coll, const std::string &query, const std::string &projection) = 0;
  virtual std::string  get_documents(const std::string &coll, const std::string &projection) = 0;
  virtual std::string  next_awbno(const std::string &prefix) = 0;
  virtual std::string  store_file(const std::string &name, const std::string &mime, const std::string &bytes) = 0;
  virtual std::string  fetch_file(const std::string &name) = 0;
  virtual std::string  fetch_file_by_id(const std::string &oid) = 0;
  virtual bool         delete_file(const std::string &oid) = 0;
  virtual std::string  get_database() = 0;
};
```

`MongodbClient` inherits `IMongodbClient`; all existing methods become `override`.

### 2. Propagate `IMongodbClient*` through the stack

| Location | Current type | New type |
|---|---|---|
| `WebServer::mMongodbc` | `unique_ptr<MongodbClient>` | `unique_ptr<IMongodbClient>` |
| `WebServer::mongodbcInst()` return | `MongodbClient*` | `IMongodbClient*` |
| `WorkCtx::db` | `MongodbClient*` | `IMongodbClient*` |
| `MicroService::process_request()` arg | `MongodbClient&` | `IMongodbClient&` |

No changes inside the handlers — they call identical method names.

### 3. BSON wire protocol  *(unchanged from original design)*

All messages between server and agent are WebSocket **binary** frames whose payload is
a single BSON document.

#### Operation codes

```cpp
// modules/module/wsdbproxy/inc/dbproto.hpp
enum class DbOp : std::int32_t {
  CREATE_DOCUMENT       = 0,
  CREATE_BULK_DOCUMENT  = 1,
  UPDATE_COLLECTION     = 2,
  UPDATE_BULK_DOCUMENT  = 3,
  DELETE_DOCUMENT       = 4,
  GET_DOCUMENT          = 5,
  GET_DOCUMENTS_QUERIED = 6,
  GET_DOCUMENTS_ALL     = 7,
  NEXT_AWBNO            = 8,
  STORE_FILE            = 9,
  FETCH_FILE            = 10,
  FETCH_FILE_BY_ID      = 11,
  DELETE_FILE           = 12,
};
```

#### Request envelope (BSON document, carried as WebSocket binary frame payload)

| Field | BSON type | Purpose |
|---|---|---|
| `reqid` | int32 | Monotonically incrementing — used to correlate response to caller |
| `op` | int32 | `DbOp` value |
| `db` | string | Database name |
| `coll` | string | Collection name |
| `doc` | binary | Primary BSON payload (query / filter / insert document) |
| `doc2` | binary | Secondary BSON payload (projection / update document) |
| `sval` | string | String parameter (AWB prefix, file name, OID, MIME type) |

Fields not needed for a given op are omitted from the BSON document.

#### Response envelope (BSON document, carried as WebSocket binary frame payload)

| Field | BSON type | Purpose |
|---|---|---|
| `reqid` | int32 | Echoes the request's `reqid` |
| `ok` | bool | `true` on success |
| `sval` | string | String result (OID, AWB number, JSON string) |
| `ival` | int32 | Integer result (inserted/modified count) |
| `bval` | bool | Boolean result (update/delete success) |
| `data` | binary | Binary result (file bytes for fetch operations) |
| `errmsg` | string | Non-empty only when `ok == false` |

#### Op → field mapping

| Op | Request fields | Response field |
|---|---|---|
| CREATE_DOCUMENT | op, db, coll, doc | sval (OID) |
| CREATE_BULK_DOCUMENT | op, db, coll, doc | ival (count) |
| UPDATE_COLLECTION | op, coll, doc (filter), doc2 (update) | bval |
| UPDATE_BULK_DOCUMENT | op, coll, doc (filter array JSON), doc2 (update array JSON) | ival |
| DELETE_DOCUMENT | op, coll, doc (filter) | bval |
| GET_DOCUMENT | op, coll, doc (query), doc2 (projection) | sval (JSON) |
| GET_DOCUMENTS_QUERIED | op, coll, doc (query), doc2 (projection) | sval (JSON array) |
| GET_DOCUMENTS_ALL | op, coll, doc2 (projection) | sval (JSON array) |
| NEXT_AWBNO | op, sval (prefix) | sval (AWB string) |
| STORE_FILE | op, sval ("name\|mime"), data (bytes) | sval (OID) |
| FETCH_FILE | op, sval (name) | data (bytes) |
| FETCH_FILE_BY_ID | op, sval (OID) | data (bytes) |
| DELETE_FILE | op, sval (OID) | bval |

### 4. `WsDbServer` — ACE_Task in uniservice

**Location:** `modules/module/wsdbproxy/inc/wsdbproxy.hpp`,
             `modules/module/wsdbproxy/src/wsdbproxy.cpp`

```cpp
class WsDbServer : public ACE_Task<ACE_MT_SYNCH> {
public:
  WsDbServer();

  // Called by WebConnection after a successful WebSocket handshake on /ws/db.
  // Takes ownership of the socket. Replaces any previously connected agent.
  void on_agent_connected(ACE_HANDLE handle);

  // Called by WsMongodbProxy from any MicroService thread.
  // Blocks until the matching response arrives. Returns empty on timeout/disconnect.
  std::vector<uint8_t> dispatch(const std::vector<uint8_t> &request_bson);

  int open(void *args = 0) override;
  int svc() override;
  int close(u_long flags = 0) override;

private:
  // WebSocket framing helpers
  std::vector<uint8_t> ws_encode(const std::vector<uint8_t> &payload, bool mask = false);
  bool                 ws_decode(std::vector<uint8_t> &buf, std::vector<uint8_t> &payload);
  bool                 ws_send(const std::vector<uint8_t> &frame);
  bool                 ws_recv(std::vector<uint8_t> &payload);

  void run_session();  // reads response frames, dispatches to pending callers

  struct PendingRequest {
    std::vector<uint8_t>    response;
    bool                    ready{false};
    std::condition_variable cv;
  };

  ACE_HANDLE              m_agentHandle{ACE_INVALID_HANDLE};
  std::mutex              m_socketMu;    // guards writes to m_agentHandle socket
  std::mutex              m_pendingMu;   // guards m_pending map
  std::map<int32_t, std::shared_ptr<PendingRequest>> m_pending;
  std::atomic<int32_t>    m_nextReqId{0};
  std::atomic<bool>       m_connected{false};
};
```

**Lifecycle:**
1. `WebServer` constructs `WsDbServer` at startup (when `--remote-db` flag set) and calls `open()`.
2. `svc()` loops, calling `run_session()` when `m_connected == true`, otherwise sleeps briefly.
3. `run_session()` reads WebSocket frames in a loop:
   - Binary frame → parse `reqid` from BSON → look up `PendingRequest` → store response → `cv.notify_one()`
   - Ping frame → send Pong
   - Close frame / socket error → set `m_connected = false`, drain `m_pending` with error responses
4. `on_agent_connected(handle)` — stores the handle, sets `m_connected = true`. Called from the reactor thread.
5. `dispatch(bson)` — assigns `reqid`, inserts `PendingRequest`, sends WebSocket binary frame, waits on `cv` with a configurable timeout (default 30 s).

### 5. `WebConnection` changes — WebSocket upgrade detection

`WebConnection::handle_input()` already buffers HTTP bytes. After `Http::message_length()`
returns non-zero, check before enqueuing to a MicroService worker:

```
if method == GET
   and uri == "/ws/db"
   and header contains "Upgrade: websocket"
→  perform WebSocket handshake (send 101)
→  call webServer().wsDbServer().on_agent_connected(handle)
→  do NOT enqueue to MicroService
→  remove self from connectionPool (the socket is now owned by WsDbServer)
```

The handshake accept key is computed as:
```cpp
std::string ws_accept_key(const std::string &sec_key) {
  static const std::string GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  std::string combined = sec_key + GUID;
  unsigned char hash[20];
  SHA1(reinterpret_cast<const unsigned char*>(combined.data()), combined.size(), hash);
  // Base64-encode hash[20] using BIO_f_base64 (OpenSSL already linked)
  return base64_encode(hash, 20);
}
```

### 6. `WsMongodbProxy` — `IMongodbClient` implementation

```cpp
class WsMongodbProxy : public IMongodbClient {
public:
  WsMongodbProxy(WsDbServer &server, std::string db_name);

  std::string  create_document(const std::string &db, const std::string &coll,
                               const std::string &json) override;
  // ... all IMongodbClient overrides ...
  std::string  get_database() override;

private:
  std::vector<uint8_t> build_request(DbOp op,
                                     const std::string &db   = {},
                                     const std::string &coll = {},
                                     const std::string &doc  = {},
                                     const std::string &doc2 = {},
                                     const std::string &sval = {});
  WsDbServer  &m_server;
  std::string  m_dbName;
};
```

Each method:
1. Calls `build_request()` → BSON bytes.
2. Calls `m_server.dispatch(bson)` — blocks until response or timeout.
3. Parses response BSON → extracts the relevant result field.
4. On `ok == false` or timeout → logs error, returns empty/false (same contract as `MongodbClient`).

### 7. `webservice_main.cpp` changes

New CLI flags added to `CommandArgumentName` enum and `kOptMap`:

| Flag | Default | Purpose |
|---|---|---|
| `--remote-db` | (absent = local) | Enable WebSocket proxy mode |
| `--ws-db-path` | `/ws/db` | WebSocket endpoint path |

Startup logic:

```cpp
if (!opt[idx(Arg::REMOTE_DB)].empty()) {
  auto wsServer = std::make_unique<WsDbServer>();
  wsServer->open();
  auto proxy = std::make_unique<WsMongodbProxy>(*wsServer, opt[idx(Arg::DB_NAME)]);
  inst = WebServer(ip, port, workers, std::move(proxy), std::move(wsServer));
} else {
  auto db = std::make_unique<MongodbClient>(uri, pool, db_name);
  inst = WebServer(ip, port, workers, std::move(db));
}
```

`WebServer` gains an optional `WsDbServer*` member (null in local mode) exposed via
`wsDbServer()` so `WebConnection` can call `on_agent_connected()`.

---

## New module layout

```
modules/module/wsdbproxy/
├── inc/
│   ├── wsdbproxy.hpp     WsDbServer, WsMongodbProxy
│   └── dbproto.hpp       DbOp enum, build_request(), parse_response()
└── src/
    ├── wsdbproxy.cpp
    └── dbproto.cpp
```

Added to root `CMakeLists.txt` source glob for `uniservice`.

---

## docker-compose changes

No additional container capabilities are needed. Just ensure `$PORT` is passed through
(already done via the `PORT` env var):

```yaml
app:
  environment:
    PORT: "${PORT:-8080}"
    ARGS: >-
      --remote-db 1
      --mongo-db-name ${MONGO_DB:-xpmile}
      --server-worker ${SERVER_WORKERS:-5}
      --ws-db-path /ws/db
  ports:
    - "${HOST_PORT:-8080}:${PORT:-8080}"
  # No cap_add, no devices — nothing extra vs today
```

The MongoDB-side `docker-compose` (separate machine, implemented later) runs only
`mongod` + `ws-db-agent`.

---

## Sequence diagram — single DB call in WebSocket proxy mode

```
MicroService thread     WsMongodbProxy       WsDbServer          ws-db-agent
      │                       │                   │                    │
      │  get_document(...)     │                   │                    │
      │──────────────────────►│                   │                    │
      │                       │  build_request()  │                    │
      │                       │  dispatch(bson) ─►│                    │
      │                       │                   │  ws_send(frame) ──►│
      │                       │  (blocks on cv)   │                    │  MongodbClient.get_document()
      │                       │                   │◄── ws_send(frame)  │
      │                       │                   │  cv.notify_one()   │
      │                       │◄── response bson ─│                    │
      │                       │  parse_response() │                    │
      │◄── JSON string ───────│                   │                    │
```

---

## Resolved design decisions

| Question | Decision |
|---|---|
| `dispatch()` timeout | 30 s. Unblocks all waiting callers with an error response; MicroService returns 503 to the browser. No retry. |
| Agent disconnect (in-flight) | Immediately wake all pending `dispatch()` callers with error. Wait for agent to reconnect before accepting new requests. |
| WebSocket path | Hardcoded `/ws/db`. |
| Multiple agents | Reject second connection with `HTTP/1.1 409 Conflict` while a live agent is connected. |
