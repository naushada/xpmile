# Codebase guide — xpmile

Logistics management platform. C++ HTTP server (ACE + MongoDB) serving an Angular SPA, deployed as a two-container Docker stack.

---

## Repository layout

```
.
├── CMakeLists.txt          Root build — uniservice + wsdbagent binaries + test suite
├── Doxyfile                Doxygen configuration
├── test/                   Off-target googletest runner
│   ├── main.cc
│   └── CMakeLists.txt
├── modules/module/         Deep module tree — one directory per concern
│   ├── webservice/         TCP server, reactor, request routing
│   ├── http/               HTTP/1.1 request parser
│   ├── email/              SMTP client (TLS, FSM-driven)
│   ├── mongodb/            MongoDB connection-pool client
│   ├── wsdbproxy/          WebSocket DB server (WsDbServer) + proxy (WsMongodbProxy)
│   ├── wsdbagent/          WebSocket DB agent — runs on the MongoDB machine
│   ├── oauth2/             OAuth2 stub
│   ├── whatsapp/           WhatsApp stub
│   └── thirdparty/         nlohmann/json (header-only)
├── ui/                     Angular frontend (served from /webui/)
├── certs/                  mTLS certificate bundle (CA, server, client)
│   ├── generate.sh         OpenSSL script — regenerates all certs
│   ├── ca.crt              Shared CA certificate (committed)
│   ├── server.crt          uniservice certificate (committed)
│   └── client.crt          wsdbagent certificate (committed)
└── docker/
    ├── Dockerfile           Multi-stage build (uniservice + Angular)
    ├── Dockerfile.wsdbagent Standalone wsdbagent container image
    ├── Dockerfile.mongo     mongo:7 + init script baked in
    └── mongo-init.js        Creates app DB user + seeds admin account
```

Each module follows the same layout: `inc/` for the public header, `src/` for the implementation, `test/` for unit tests (where they exist).

---

## Process entry point

`modules/module/webservice/src/webservice_main.cpp`

Parses command-line flags with `ACE_Get_Opt`, then:
1. Constructs a `WebServer` with the listen port, worker count, and MongoDB URI.
2. Populates the `SMTP::Account` singleton with email-from credentials.
3. Calls `WebServer::start()` — blocks in the ACE reactor loop until stopped.

**Flags**

| Flag | Default | Purpose |
|---|---|---|
| `--server-ip` | (all interfaces) | Bind address |
| `--server-port` | 8080 | TCP listen port |
| `--server-worker` | 10 | Worker thread count |
| `--mongo-db-uri` | — | Full MongoDB URI (local mode only) |
| `--mongo-db-connection-pool` | 50 | Pool size |
| `--mongo-db-name` | — | Database name |
| `--email-from-name/id/password` | — | Outgoing email credentials |
| `--remote-db` | off | Use WebSocket DB proxy (ws-db-agent) instead of local MongoDB |
| `--agent-port` | — | Dedicated mTLS port for wsdbagent (self-hosted only) |
| `--tls-cert` | — | Server certificate PEM (mTLS mode) |
| `--tls-key` | — | Server private key PEM (mTLS mode) |
| `--tls-ca` | — | CA cert to verify agent client cert (mTLS mode) |
| `--help` | — | Print usage and exit 0 |

When `--remote-db` is set without `--agent-port` / TLS flags, `WsDbServer` runs in Heroku mode — it waits for an upgraded WebSocket handed off from `WebConnection`. When all four TLS flags are provided, it binds its own `ACE_SSL_SOCK_Acceptor` on `--agent-port` and performs the WebSocket handshake itself.

**CLI implementation** — options are stored in a `std::array<std::string, N>` indexed by `CommandArgumentName` enum (defined in `webservice.hpp`). A `constexpr idx(Arg)` helper casts the enum to `std::size_t` for clean indexing. Parsing is table-driven: a `constexpr kOptMap[]` of `(char, Arg)` pairs maps each short option to its enum slot. `opt_int()` converts string → int with a fallback default for `port` and `worker`.

```
CommandArgumentName enum:
  SERVER_IP, SERVER_PORT, SERVER_WORKER_NODE,
  DB_URI, DB_CONN_POOL, DB_NAME,
  EMAIL_FROM_NAME, EMAIL_FROM_ID, EMAIL_FROM_PASSWORD,
  REMOTE_DB, AGENT_PORT, TLS_CERT, TLS_KEY, TLS_CA,
  MAX_CMD_ARG
```

---

## webservice module

**Files:** `modules/module/webservice/inc/webservice.hpp`, `src/webservice.cpp`, `src/webservice_main.cpp`

Three cooperating classes built on the ACE framework:

### WebServer

`ACE_Event_Handler` subclass. Owns the listening socket (`ACE_SOCK_Acceptor`) and runs the ACE reactor event loop.

- `handle_input()` — called by the reactor when the listening socket becomes readable; accepts the connection, wraps it in a `WebConnection`, registers it with the reactor.
- `handle_timeout()` — periodic heartbeat / stale-connection cleanup.
- Owns the shared `MongodbClient` (`mMongodbc`) — one pool for the whole process.
- Owns the `MicroService` worker pool (`m_workerPool`) and advances `m_currentWorker` in round-robin for each incoming request.
- `connectionPool()` — `map<ACE_HANDLE, unique_ptr<WebConnection>>` of live sockets.

### WebConnection

`ACE_Event_Handler` registered per accepted socket.

- `handle_input()` — reads bytes into `m_recvBuf`, calls `Http::message_length()` to detect a complete HTTP message, then enqueues an `ACE_Message_Block` on the next available worker's message queue.
- `handle_close()` — deregisters from the reactor and removes itself from `WebServer::connectionPool()`.
- Buffers partial reads across multiple reactor callbacks until a full HTTP/1.1 message is assembled.

### MicroService

`ACE_Task<ACE_MT_SYNCH>` — one thread per instance, reading from a synchronized message queue.

- `svc()` — dequeue loop: pulls `ACE_Message_Block` items, unpacks the embedded `WorkCtx*`, calls `process_request(ctx->handle, ctx->request, *ctx->db)`.
- `process_request()` — parses the raw HTTP bytes into an `Http` object, dispatches on method to `handle_GET / handle_POST / handle_PUT / handle_DELETE / handle_OPTIONS`, then writes the response via `http_send()`. All routing logic lives here in `MicroService`.
- One `MongodbClient` reference is shared across workers; `mongocxx::pool::acquire()` is thread-safe so no extra lock is needed around DB calls. The `ACE_Semaphore` in `WebServer` is a startup barrier only — each worker calls `semaphore().release()` as its first act in `svc()` so the constructor can confirm each thread is running before moving on.
- Default constructor (`MicroService()`) sets `m_parent` to `nullptr` — safe to use in unit tests for response-builder and content-type methods, which never call `webServer()`.

### WorkCtx

Anonymous struct (defined at file scope in `webservice.cpp`) that carries the work item from `WebConnection` to `MicroService` through an `ACE_Message_Block`:

```cpp
struct WorkCtx {
  ACE_HANDLE handle;      // socket descriptor to write the response to
  MongodbClient *db;      // pointer to the shared MongodbClient (never null)
  std::string request;    // full raw HTTP request bytes
};
```

`WebConnection::handle_input()` heap-allocates a `WorkCtx`, wraps the pointer as the `ACE_Message_Block`'s data pointer, and enqueues it on the chosen `MicroService`. `MicroService::svc()` casts back with `reinterpret_cast<WorkCtx*>` and deletes after use.

**Routing table** (method → URI prefix → handler):

| Method | URI prefix | Handler |
|---|---|---|
| OPTIONS | * | `handle_OPTIONS` — returns 200 + CORS headers |
| GET | `/api/v1/shipment` | `handle_shipment_GET` |
| GET | `/api/v1/account` | `handle_account_GET` |
| GET | `/api/v1/inventory` | `handle_inventory_GET` |
| GET | `/api/v1/email` | `handle_email_GET` |
| GET | `/api/v1/document` | `handle_document_GET` |
| GET | `/api/v1/config` | `handle_config_GET` |
| GET | (static) | serve Angular dist files from `../webgui/webui/` |
| POST | `/api/v1/shipment` | `handle_shipment_POST` |
| POST | `/api/v1/account` | `handle_account_POST` |
| POST | `/api/v1/inventory` | `handle_inventory_POST` |
| POST | `/api/v1/email` | `handle_email_POST` |
| POST | `/api/v1/document` | `handle_document_POST` |
| POST | `/api/v1/config` | `handle_config_POST` |
| PUT | `/api/v1/shipment` | `handle_shipment_PUT` |
| PUT | `/api/v1/inventory` | `handle_inventory_PUT` |
| PUT | `/api/v1/account` | `handle_account_PUT` |
| PUT | `/api/v1/shipment` (alt-ref) | `handle_altref_update_shipment_PUT` |
| DELETE | `/api/v1/*` | `handle_DELETE` |

**Response builders** (no DB dependency):

- `build_responseOK(body, content_type)` → `HTTP/1.1 200 OK`
- `build_responseCreated()` → `HTTP/1.1 201 Created` (no body)
- `build_responseERROR(body, error)` → `HTTP/1.1 <error>` with optional body
- `get_contentType(ext)` → maps file extension to MIME type; falls back to `text/html`

**Static file serving:** GET requests that do not match `/api/` are treated as Angular asset requests. The working directory is `/opt/xAPP/granada/` at runtime, so `../webgui/webui/<path>` resolves the Angular `dist/` output.


---

## http module

**Files:** `modules/module/http/inc/http_parser.hpp`, `src/http_parser.cpp`

`class Http` — parses a complete HTTP/1.1 request string into structured fields.

**Construction:** `Http h(raw_string)` — internally calls `get_header()`, `parse_uri()`, `parse_mime_header()`, then `get_body()`.

**Key accessors:**
- `method()` — `"GET"`, `"POST"`, etc.
- `uri()` — percent-decoded path (no query string), e.g. `"/api/v1/shipment"`
- `get_element(key)` — looks up a MIME header or query-string parameter by name
- `body()` — decoded and decompressed body bytes
- `header()` — raw header section including the trailing `\r\n\r\n`

**Body extraction rules** (applied in order):
1. `Transfer-Encoding: chunked` — RFC 7230 §4.1 chunk decoding
2. `Content-Length: N` — plain slice of N bytes after the header separator
3. `multipart/form-data` — delimited by the closing `--boundary--` marker
4. No framing header — body is empty

**Compression:** After framing is resolved, if `Content-Encoding: gzip` or `deflate` is present the body is decompressed with zlib `inflateInit2`.

**`message_length(buf)`** — static utility for the read loop in `WebConnection::handle_input()`. Returns the total wire length of the HTTP message or 0 if more bytes are needed.

---

## email module

**Files:** `modules/module/email/inc/emailservice.hpp`, `src/emailservice.cpp`, `src/emailservice_fsm.cpp`

Implements an SMTP client that connects to Gmail on port 25, negotiates STARTTLS, authenticates with `AUTH LOGIN`, and sends mail with optional Base64-encoded attachments.

### Key types (all in the `SMTP::` namespace)

**`Account`** — process-singleton (`Account::instance()`). Stores outgoing email credentials (`from_name`, `from_email`, `from_password`) and the current message parameters (`to_email`, `email_subject`, `email_body`, `attachment`). Set at startup from CLI flags; updated per-request for each send.

**`User`** — owns a `Client` (TCP/TLS connection) and a `Tls` wrapper. Drives the FSM via `fsm().set_state(...)` and `rx(server_response)`.

**`Client`** — `ACE_Task<ACE_MT_SYNCH>` active object. Manages the TCP socket to `smtp.gmail.com:25` and the TLS handshake. Methods: `tx()` (send), `rx()` (receive), `start()` / `stop()`.

**`Tls`** — wraps OpenSSL `SSL*` / `SSL_CTX*` over the `Client`'s `ACE_SOCK_Stream`. `start(handle)` performs the TLS handshake after STARTTLS is negotiated at the SMTP layer.

**`Transaction<States>`** — FSM engine templated on the `States` variant. `set_state(new_state)` calls `onExit()` on the current state and `onEntry()` on the next. `onRx(in, out, new_state, parent)` dispatches `onResponse()` on the active state via `std::visit`.

### FSM states (in protocol order)

```
GREETING → HELO → MAIL → RCPT → DATA → BODY → QUIT
```

Each state class implements:
- `onEntry()` / `onExit()` — side-effecting setup/teardown
- `onResponse(in, out, new_state, parent)` — parses the SMTP server reply code, composes the next command into `out`, and sets `new_state`

**MAIL state** additionally manages the `AUTH LOGIN` challenge sequence (`AUTH_INIT → AUTH_USRNAME → AUTH_PASSWORD → AUTH_SUCCESS`), Base64-encoding the username and password.

Stub states (`RESET`, `VRFY`, `NOOP`, `EXPN`, `HELP`) return `REMAIN_IN_SAME_STATE` and send no command.

---

## mongodb module

**Files:** `modules/module/mongodb/inc/mongodbc.hpp`, `src/mongodbc.cpp`, `CMakeLists.txt`, `README.md`

`class MongodbClient` wraps a `mongocxx::pool` so worker threads share one connection pool without synchronization overhead. Each method acquires a connection entry from the pool, performs the operation, and releases it on return.

**Construction:** `MongodbClient(uri)` — parses the URI, initializes `mongocxx::instance` (process singleton required by the driver), and creates the pool.

**CRUD surface:**
- `create_document(db, coll, json)` → OID string
- `create_bulk_document(db, coll, json)` → count inserted
- `update_collection(coll, filter, update)` → bool
- `update_bulk_document(coll, filters, values)` → count modified
- `delete_document(coll, filter)` → bool
- `get_document(coll, query, projection)` → JSON string
- `get_documents(coll, query, projection)` → JSON array string
- `get_documents(coll, projection)` → all documents

**AWB number generation:** `next_awbno(prefix)` — atomically increments a `counters` collection document (`{"_id":"awbno","seq":<n>}`) using `findOneAndUpdate` with `$inc` and upsert. Returns a zero-padded 9-digit string like `AWB000000042`.

**GridFS:** `store_file(name, mime, bytes)` / `fetch_file(name)` / `fetch_file_by_id(oid)` / `delete_file(oid)` — for document (PDF/image) storage.

**`from_json(json_obj, key)`** — JSON-only utility (no DB connection needed). Returns a `JsonExtract` variant:
- `std::string` for a UTF-8 field
- `JsonStrVec` (`vector<string>`) for a string array
- `JsonDocList` (`vector<tuple<string,string>>`) for a `file-name`/`file-content` document array
- `std::monostate` if the key is absent or the type is unsupported

**MongoDB collections (in `xpmile` database):**

| Collection | Purpose |
|---|---|
| `account` | User accounts and login credentials |
| `shipping` | Shipment records (both single and bulk) |
| `inventory` | Inventory items |
| `config` | Application configuration |
| `counters` | AWB sequence counter (`{"_id":"awbno","seq":N}`) |
| `fs.files` / `fs.chunks` | GridFS document storage |

Note: the collection is named `shipping`, not `shipment`. The URL path is `/api/v1/shipment/...` but the MongoDB collection is `shipping`.

---

## wsdbproxy module

**Files:** `modules/module/wsdbproxy/inc/wsdbproxy.hpp`, `src/wsdbproxy.cpp`, `src/dbproto.cpp`, `src/wsframe.cpp`

Provides the server-side of the remote-DB WebSocket bridge. Two classes:

### WsDbServer

`ACE_Task<ACE_MT_SYNCH>` — owns the connection from `wsdbagent` and dispatches `DbRequest` messages to the local `IMongodbClient`.

**Two operating modes:**

| Mode | When | How agent connects |
|---|---|---|
| Heroku (no-TLS) | `WsDbServer()` default constructor | WebSocket upgrade handed off from `WebConnection` via `on_agent_connected()` |
| Self-hosted mTLS | `WsDbServer(TlsConfig)` constructor | Binds its own `ACE_SSL_SOCK_Acceptor` on `tls.port`; accepts and upgrades the connection itself |

In mTLS mode, `open()` configures `ACE_SSL_Context` with the server cert/key and CA, sets `SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT`, and binds `m_ssl_acceptor`. `svc()` calls `m_ssl_acceptor.accept()`, rejects a second simultaneous agent, runs `ws_upgrade_server()`, then enters `run_session()`.

`run_session()` loop:

| Opcode | Action |
|---|---|
| `0x02` Binary | BSON-decode → `DbRequest` → `dispatch()` → BSON-encode response → send |
| `0x09` Ping | Send Pong |
| `0x08` Close | Send Close → exit |

`ws_send()` and the `read_n` lambda in `ws_recv_frame()` route to either the plain `m_agentStream` (Heroku) or `m_ssl_agentStream` (mTLS), transparently.

**`TlsConfig` struct:**
```cpp
struct TlsConfig {
  std::uint16_t port {0};
  std::string   cert;   // path to server.crt
  std::string   key;    // path to server.key
  std::string   ca;     // path to ca.crt
};
```

### WsMongodbProxy

`IMongodbClient` implementation that forwards every MongoDB operation as a `DbRequest` BSON message over the WebSocket to `wsdbagent`, then blocks waiting for the `DbResponse`. Constructed with a `WsDbServer&` reference and the database name.

### Wire protocol (dbproto)

`DbRequest` / `DbResponse` are BSON documents. `DbOp` enum covers 13 operations (CREATE_DOCUMENT through DELETE_FILE). `wsframe` handles RFC 6455 framing — client frames are masked, server frames are unmasked.

---

## wsdbagent module

**Files:** `modules/module/wsdbagent/inc/wsdbagent.hpp`, `src/wsdbagent.cpp`, `src/wsdbagent_main.cpp`

Standalone binary (`wsdbagent`) that runs **on the MongoDB machine** (typically behind NAT). Connects outbound to `uniservice /ws/db`, then forwards all DB operations to a local `MongodbClient`.

**`WsDbAgent`** constructor:
```cpp
WsDbAgent(server_host, server_port, use_ssl,
          db_uri, db_pool, db_name,
          tls_ca="", tls_cert="", tls_key="");
```

**Connect loop** (`run(backoff_secs)`):
1. `connect_and_handshake()` — configure `ACE_SSL_Context` (if TLS args provided), open TCP/TLS connection, send RFC 6455 upgrade request to `/ws/db`, verify `101` + `Sec-WebSocket-Accept`.
2. On success → `run_session()` (same dispatch table as WsDbServer).
3. On failure or session end → sleep `backoff_secs` → retry indefinitely.

**mTLS on the client side:**
```cpp
ACE_SSL_Context *ctx = ACE_SSL_Context::instance();
ctx->set_mode(ACE_SSL_Context::SSLv23_client);
ctx->load_trusted_ca(tls_ca.c_str());     // verify server cert
ctx->certificate(tls_cert.c_str(), ...);  // present client cert
ctx->private_key(tls_key.c_str(), ...);
SSL_CTX_set_verify(ctx->context(), SSL_VERIFY_PEER, nullptr);
```

**CLI flags:**

| Flag | Required | Default |
|---|---|---|
| `--server-host` | yes | — |
| `--mongo-db-uri` | yes | — |
| `--mongo-db-name` | yes | — |
| `--server-port` | no | 443 (SSL) / 8080 (plain) |
| `--no-ssl` | no | TLS on |
| `--tls-ca` | no | — |
| `--tls-cert` | no | — |
| `--tls-key` | no | — |
| `--backoff` | no | 5 s |

---

## mTLS certificates

`certs/generate.sh` produces a private CA and two leaf certs (server + client), all 4096-bit RSA with 10-year validity. Private keys are git-ignored (`certs/.gitignore: *.key`). Public certs are committed.

| File | Used by |
|---|---|
| `ca.crt` | both sides — distributed freely |
| `server.crt` / `server.key` | uniservice |
| `client.crt` / `client.key` | wsdbagent |

Re-run `generate.sh` to rotate. Both sides must be updated together.

---

## oauth2 and whatsapp modules

Both are stubs — empty `.hpp`/`.cpp` pairs compiled into `uniservice` but containing no logic. Placeholders for future OAuth2 and WhatsApp notification integrations.

---

## thirdparty module

`modules/module/thirdparty/json.hpp` — nlohmann/json single-header library (vendored). Used by `MongodbClient::from_json()` and throughout `webservice.cpp` for JSON serialisation/deserialisation.

---

## Infrastructure

### Docker — main app image (`docker/Dockerfile`)

Three build stages:

**Stage 1 (`cpp-builder` / `ubuntu:focal`):**
- Builds ACE/TAO 7.0.0 from source (make install, SSL enabled)
- Builds mongo-c-driver 1.19.1 and mongo-cxx-driver v3.6 from source
- Builds googletest
- Compiles `uniservice` and the `offtarget` test binary via CMake (`-j2` to avoid OOM; `-fconcepts` for GCC 9)

**Stage 2 (`ui-builder` / `node:18-alpine`):**
- `npm ci` then `ng build --configuration production`
- `ARG UI_BUST` forces a cache-bust without invalidating the C++ stage: `UI_BUST=$(date +%s) podman compose up --build app`

**Stage 3 (`runtime` / `ubuntu:focal`):**
- Copies only: ACE/TAO `.so` files, MongoDB driver `.so` files, `uniservice` binary, Angular `dist/webui/`
- Working directory: `/opt/xAPP/granada/` — relative path `../webgui/webui/` resolves to Angular output

### Docker — wsdbagent image (`docker/Dockerfile.wsdbagent`)

Two build stages, similar cpp-builder base:
- Passes `-DBUILD_TESTS=OFF` so GTest is not required
- Builds only the `wsdbagent` target (`make -j2 wsdbagent`)
- Runtime stage copies the binary and shared libs; all flags passed via `ENV ARGS=""`
- No volume needed — agent connects outbound only; certs mounted read-only via `-v /path/to/certs:/certs:ro`

### docker-compose.yml

Two services on an isolated bridge network (`xpmile-net`):

- **`mongodb`** — custom image with `mongo-init.js` baked in; runs as UID 999 (MongoDB system user) to avoid `/proc/1/fd` permission issues on some Linux hosts; data persisted in named volume `mongo-data`.
- **`app`** — depends on `mongodb` with `condition: service_healthy`; passes the MongoDB URI with app credentials via `ARGS` env var.

### mongo-init.js

Runs once on first container start (when `mongo-data` volume is empty):
1. Creates `xpmile` app user with `readWrite` on the `xpmile` database.
2. Inserts a bootstrap admin document into `xpmile.account`:
   - `accountCode: "admin"` / `accountPassword: "admin@123"`
   - Role: `"Admin"`, event location: `"UAE"`

---

## Test suite

**Runner:** `test/main.cc` + `test/CMakeLists.txt` — builds the `offtarget` binary, discovered and run by `ctest`.

**Module test files:**

| Module | Location | What is tested |
|---|---|---|
| `http` | `modules/module/http/test/` | `Http` parser: URI, query strings, headers, body (Content-Length, chunked, gzip, combined), `header()` boundary |
| `webservice` | `modules/module/webservice/test/` | `MicroService`: response builders (200, 201, 4xx, 5xx), `get_contentType()`, OPTIONS handler |
| `email` | `modules/module/email/test/` | `SMTP::User` FSM: GREETING state transition via `rx()`, `SMTP::Account` population from JSON |

**46 tests, 0 failures** (as of last run).

Note: `EmailServiceTest` is a `testing::Test` subclass with a custom constructor that accepts a JSON string and initializes `mMongodbc`/`mUser` directly, because `SetUp()` is only invoked by the gtest fixture machinery — not when the object is constructed directly in a `TEST()` body.

---

## Build system

Root `CMakeLists.txt`:
- `add_subdirectory(modules/module/mongodb)` — builds `mongodbcxx` static library
- `option(BUILD_TESTS "Build test suite" ON)` — guards `add_subdirectory(test)`; pass `-DBUILD_TESTS=OFF` when building `wsdbagent` without GTest
- `add_executable(uniservice ...)` — globs all `*.cpp` from webservice, http, email, oauth2, whatsapp `src/` directories; also links in wsdbproxy sources
- `add_executable(wsdbagent ...)` — globs `wsdbagent/src/*.cpp`, adds `wsdbproxy/src/dbproto.cpp` and `wsdbproxy/src/wsframe.cpp`; links `pthread ACE ACE_SSL ssl crypto mongodbcxx z`

`modules/module/mongodb/CMakeLists.txt` — builds `mongodbcxx` as a separate static library target.

`test/CMakeLists.txt` — explicit source list (not globbed) for the test binary.

Standard: C++20 (`-std=c++2a`) for `uniservice` / `offtarget`; C++17 for `mongodbcxx`.

---

## Request lifecycle (end to end)

```
Client TCP connect
  └─ WebServer::handle_input()          accepts, creates WebConnection, registers with reactor
       └─ WebConnection::handle_input()  accumulates bytes until Http::message_length() > 0
            └─ enqueue ACE_Message_Block on next MicroService (round-robin)
                 └─ MicroService::svc()  dequeues, calls MicroService::process_request()
                      ├─ Http h(raw)                             parse request
                      ├─ route on method → handle_GET/POST/PUT/DELETE/OPTIONS
                      ├─ handler(h, dbInst)                      DB ops via MongodbClient
                      └─ http_send(handle, response)             write HTTP response bytes
```

The email send path is triggered by `handle_email_POST`: it populates `SMTP::Account`, constructs an `SMTP::User`, and drives the FSM through GREETING → HELO → MAIL → RCPT → DATA → BODY → QUIT against `smtp.gmail.com:25` over TLS.

---

## AWB number generation flow

AWB numbers are generated server-side in both the single and bulk shipment POST handlers. The logic is identical:

```
shipment.isAutoGenerate == true?
  ├─ YES: look up account document by senderInformation.accountNo
  │         → project only { awbPrefix: true }
  │         → use account's awbPrefix if present, else fall back to "AWB"
  │       → call dbInst.next_awbno(prefix)
  │         → findOneAndUpdate on "counters" { _id: "awbno" }
  │           with { $inc: { seq: 1 } }, upsert: true, returnAfter: true
  │         → format result: prefix + zero-padded 9-digit seq
  │       → set shipment["awbno"] = generated AWB
  │       → re-serialise JSON body with updated AWB
  └─ NO:  if shipment["awbno"] exists, use it as-is (customer-supplied)
```

For bulk: the loop runs over every element in the JSON object before `create_bulk_document` is called, so all AWBs are populated in the stored documents. The array of generated AWB numbers is returned in the response as `{ createdShipments: N, awbNumbers: [...] }`.

---

## Design decisions

**One `MongodbClient` per process, not per connection.**
`mongocxx::instance` is a process singleton — only one may exist. `mongocxx::pool::acquire()` is fully thread-safe, so a single pool shared across all `MicroService` workers is both correct and efficient. Creating a client per connection would be incorrect (second `mongocxx::instance` → abort).

**`ACE_Semaphore` is a startup barrier, not a DB mutex.**
`WebServer` creates a semaphore with initial count 0. After spawning N workers, it calls `semaphore().acquire()` N times to block until every thread has called `semaphore().release()` in its first `svc()` tick. This guarantees all workers are ready before the reactor starts accepting connections. It is never used to serialise database access.

**`create_bulk_document` uses `insert_many`, not BSON array-as-object.**
The original implementation called `bsoncxx::from_json` on a JSON array, which BSON treated as an object with numeric string keys — fragile and incorrect. The current implementation parses with nlohmann/json, iterates elements, converts each to `bsoncxx::document::value`, and calls `collection.insert_many(views, opts)` with `ordered: false` so a single invalid row does not abort the batch.

**`WebServiceEntry` was removed.**
It was a verbatim copy of `MicroService`'s routing logic used only in older tests. All tests now instantiate `MicroService` directly via its default constructor (which sets `m_parent = nullptr`). The response-builder and content-type methods tested do not call `webServer()`, so the null parent is safe.
