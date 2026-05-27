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
│   │   ├── inc/webservice.hpp
│   │   ├── src/webservice.cpp + webservice_main.cpp
│   │   └── test/webservice_test.cc
│   ├── http/               HTTP/1.1 request parser
│   │   ├── inc/http_parser.hpp
│   │   ├── src/http_parser.cpp
│   │   └── test/httpparser_test.cc
│   ├── email/              SMTP client (TLS, FSM-driven)
│   │   ├── inc/emailservice.hpp + emailservice_fsm.hpp
│   │   ├── src/emailservice.cpp + emailservice_fsm.cpp
│   │   └── test/emailservice_test.cc
│   ├── mongodb/            MongoDB connection-pool client
│   │   ├── inc/mongodbc.hpp
│   │   ├── src/mongodbc.cpp
│   │   └── test/mongodbc_test.cc
│   ├── wsdbproxy/          WebSocket DB server (WsDbServer) + proxy (WsMongodbProxy)
│   │   ├── inc/
│   │   │   ├── wsdbproxy.hpp     WsDbServer + WsMongodbProxy
│   │   │   ├── dbproto.hpp       DbRequest / DbResponse BSON framing
│   │   │   └── wsframe.hpp       WebSocket frame encode/decode (RFC 6455)
│   │   ├── src/
│   │   │   ├── wsdbproxy.cpp     accept loop, dispatch, pending-request map
│   │   │   ├── dbproto.cpp       BSON build/parse + make_error_response
│   │   │   └── wsframe.cpp       masking, fragmentation, close-frame handling
│   │   └── test/
│   │       ├── dbproto_test.cc / .hpp
│   │       ├── wsdbserver_test.cc / .hpp
│   │       ├── wsframe_test.cc / .hpp
│   │       └── wsproxy_test.cc / .hpp
│   ├── wsdbagent/          WebSocket DB agent — runs on the MongoDB machine
│   │   ├── inc/wsdbagent.hpp     WsDbAgent class
│   │   └── src/
│   │       ├── wsdbagent.cpp     connect/handshake/run_session/dispatch
│   │       └── wsdbagent_main.cpp  CLI entry; ACE_Get_Opt
│   ├── security/           Inner-TLS layer + WebSocket transport adapter
│   │   ├── inc/
│   │   │   ├── innertls.hpp      ITransport + InnerTlsClient/Server
│   │   │   └── wstransport.hpp   WebSocketTransport (ITransport adapter)
│   │   ├── src/innertls.cpp
│   │   └── test/innertls_test.cc
│   ├── sso/                SSO — server-side sessions, OIDC, SAML (sso:: namespace)
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

- `handle_input()` — reads bytes into `m_recvBuf`, calls `Http::message_length()` to detect a complete HTTP message, then enqueues an `ACE_Message_Block` on the next available worker's message queue. On WebSocket upgrade to `/ws/db`, calls `reactor()->remove_handler(this, READ_MASK | DONT_CALL)` *before* clearing `m_handle` (so the reactor can find the fd), then hands the raw fd to `WsDbServer::on_agent_connected()` and erases itself from the pool (which deletes `this`). Returns 0 in all close/hand-off cases; pool erase is the only cleanup site.
- `handle_close()` — called by the reactor on normal disconnect. Closes the fd (unless `m_handedOff` is true, in which case the fd now belongs to `WsDbServer`). Does **not** erase from `connectionPool()`.
- Destructor — calls `remove_handler(this, READ_MASK | SIGNAL_MASK)` only when `m_handedOff` is false (prevents re-deregistering an already-handed-off fd).
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
| GET | `/api/v1/sso` | `handle_sso` — SSO endpoints (`providers`, `login`, `callback/<id>`, `session`) |
| GET | `/api/v1/shipment` | `handle_shipment_GET` |
| GET | `/api/v1/account` | `handle_account_GET` |
| GET | `/api/v1/inventory` | `handle_inventory_GET` |
| GET | `/api/v1/email` | `handle_email_GET` |
| GET | `/api/v1/document` | `handle_document_GET` |
| GET | `/api/v1/config` | `handle_config_GET` |
| GET | (static) | serve Angular dist files from `../webgui/webui/` |
| POST | `/api/v1/sso` | `handle_sso` — SAML ACS callback (`callback/<id>`) + `logout` |
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
- `get_element(key)` — looks up a MIME header or query-string parameter by name (case-insensitive — keys are lowercased on store and lookup; Heroku normalises `Sec-WebSocket-Key` → `Sec-Websocket-Key`)
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
| `sessions` | Server-side login/SSO sessions; TTL index on `expiresAt` |
| `sso_transactions` | One-time login correlators (OIDC `state`, SAML `InResponseTo`); TTL-expired |
| `sso_config` | Single document — SSO provider configuration (see the sso module) |
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

In Heroku (no-TLS) mode, `svc()` blocks on `m_connectCv` waiting for `on_agent_connected()` to be called from the reactor thread. `on_agent_connected()` accepts the first agent; if a second agent arrives while the first is still registered, it force-shuts the stale socket (`shutdown(SHUT_RDWR)` so `run_session()`'s blocked `recv_n` returns), clears `m_connected`, and returns a `503 Retry-After: 2` so the agent reconnects on the next backoff cycle.

`run_session()` loop:

| Opcode | Action |
|---|---|
| `0x02` Binary | BSON-decode → `DbRequest` → `dispatch()` → BSON-encode response → send |
| `0x09` Ping | Send Pong |
| `0x0A` Pong | No-op (keepalive reply from agent) |
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
1. `connect_and_handshake()` — configure `ACE_SSL_Context` (if TLS args provided), open TCP/TLS connection, send RFC 6455 upgrade request to `/ws/db`, verify `101` + `Sec-WebSocket-Accept`. A `503` response (stale-agent eviction in progress) is treated the same as a failed connection and triggers the backoff sleep.
2. On success → `run_session()`.
3. On session end → sleep `backoff_secs` → retry indefinitely.

**Keepalive / dead-connection detection** (`run_session()`):
- Uses `poll(fd, POLLIN, 30 000 ms)` before each `ws_recv_frame()` call.
- If `poll` times out (no data for 30 s), sends a WS Ping frame. If the send fails, the connection is declared dead and the session exits.
- This prevents half-open connections (where the server's `recv_n` would otherwise block indefinitely) and keeps Heroku's idle-connection watchdog from silently killing the socket.

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

## security module

`modules/module/security/` — inner-TLS encryption layer that runs *inside* an already-open transport. Both `wsdbproxy` and `wsdbagent` use it to encrypt DB traffic over the WebSocket tunnel.

### Components

| Class | File | Role |
|---|---|---|
| `ITransport` | `innertls.hpp` | Abstract `send(bytes)`/`recv(bytes)` interface. Decouples TLS from any specific transport. |
| `InnerTlsClient` | `innertls.cpp` | Client-side TLS session — initiates handshake, optional CN/SAN check + custom CA. |
| `InnerTlsServer` | `innertls.cpp` | Server-side TLS session — loads cert + key, optional mTLS via `set_ca()`. |
| `WebSocketTransport` | `wstransport.hpp` | `ITransport` adapter for WebSocket binary frames; auto-replies to ping, ignores pong, surfaces close as `recv` returning false. |

### Mechanics

- **Memory BIOs:** `SSL` reads from `rbio` (`BIO_s_mem`, fed by `m_transport.recv`) and writes to `wbio` (drained via `flush_wbio` → `m_transport.send`). There are no real sockets at the OpenSSL level — the transport handles all I/O.
- **Handshake loop:** `SSL_connect`/`SSL_accept` is called in a loop. Each iteration flushes `wbio` first (SSL may have queued output even when returning `WANT_READ`), then reads more wire bytes when `WANT_READ` is signalled, until success.
- **Post-handshake flush:** an extra `flush_wbio` after success delivers the TLS 1.3 `Finished` message (or any other trailing flight).
- **Single-frame send:** `send()` calls `SSL_write` once with the full plaintext, then `flush_wbio` writes all pending ciphertext to the transport in one chunk. For WebSocket transport, that means one binary frame per application message.

### Message-preserving `recv` contract

One `recv()` call returns the full plaintext that a single peer `send()` produced — even when the plaintext spans multiple TLS records (~16 KB each).

```cpp
plaintext.clear();
std::uint8_t buf[16384];
for (;;) {
    int ret = SSL_read(m_ssl.get(), buf, sizeof(buf));
    if (ret > 0)                              { plaintext.insert(end, buf, buf + ret); continue; }
    if (SSL_get_error(...) == SSL_ERROR_WANT_READ) return true;
    return false;
}
```

`wsdbproxy` and `wsdbagent` rely on this: one inner-TLS message = one BSON `DbRequest`/`DbResponse`. If `recv` returned only the first record, the BSON parser would silently produce a document with default fields (`reqid = -1`, `ok = false`) and the response would be dropped. See `test/innertls_test.cc :: Recv_ReturnsFullPlaintext_InSingleCall` and `docs/design/security/README.md` for the longer write-up.

### Tests

`modules/module/security/test/innertls_test.cc` — 8 tests covering handshake success/failure, encryption (plaintext ≠ ciphertext), 64 KB roundtrip, multi-message ordering, single-call full-plaintext delivery, and two MITM scenarios (plaintext injection, frame tampering). Run via the `offtarget` binary:

```sh
./offtarget --gtest_filter='InnerTls*'
```

Tests skip gracefully if `/src/certs/server.{crt,key}` aren't present (e.g. when running the binary outside the Docker image).

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

## sso module — single sign-on

`modules/module/sso/` implements the SSO feature: server-side sessions, OIDC, and SAML 2.0. Every type lives in the **`sso::` namespace**. The full design is `docs/design/sso/sso-design.md`; the test-first plan is `sso-tdd-plan.md`.

**Model — backend-for-frontend (BFF).** The C++ backend is the OAuth/SAML client and runs the entire IdP handshake; IdP tokens never reach the browser. Every login — federated *or* password — mints a server-side session in the `sessions` collection, handed to the browser as an opaque `HttpOnly` cookie (`xpmile_session`). The cookie is a random id, not a JWT, so the session is revocable.

### Files

| File pair | Role |
|---|---|
| `sso_util.*` | base64 / base64url codecs; CSPRNG `random_token()` |
| `sso_cookie.*` | build/parse the `xpmile_session` cookie (`HttpOnly; Secure; SameSite=Lax`) |
| `sso_session.*` | `SessionManager` (create/lookup/revoke), `SessionCache` (TTL'd LRU), `AuthContext`, `IClock` / `SystemClock` |
| `sso_config.*` | `parse_sso_config()` → `SsoConfig` / `ProviderConfig` from the `sso_config` JSON document |
| `sso_registry.*` | `ProviderRegistry` — holds the live config, hot-reloads it, keeps last-good on a bad parse |
| `sso_identity.hpp` | `IIdentityProvider` interface + `AuthnRequest` / `IdentityClaims` — OIDC and SAML behind one shape |
| `sso_http_client.*` | `IHttpClient` + `HttpClient` (ACE_SOCK + OpenSSL outbound HTTPS), `encode_form()` |
| `sso_jwt.*` | `Jwks` parsing + `verify_jwt()` — RS256-only `id_token` verification |
| `sso_oidc.*` | `OidcProvider`, PKCE helpers (`make_code_verifier` / `code_challenge`), `fetch_discovery()` |
| `sso_provisioning.*` | `resolve_account()` — hybrid match-by-subject / match-by-email / JIT-create |
| `sso_endpoints.*` | transport-agnostic logic for the six `/api/v1/sso/*` endpoints → `SsoHttpResult` |
| `sso_csrf.*` | double-submit CSRF token (`XSRF-TOKEN` cookie) — Phase F |
| `sso_authz.*` | `sso_requires_session()` / `sso_authorize()` predicate — decision logic only, **not yet wired as live 401 enforcement** (design §14 Q1) |
| `saml_provider.*` | `SamlProvider`; SAML deflate/inflate for the HTTP-Redirect binding |
| `saml_response.*` | decode/parse a `SAMLResponse`; `validate_saml_conditions()` |
| `saml_signature.*` | `verify_saml_signature()` — XML-DSig via libxml2 + xmlsec1 |

### Sessions

A session is an ordinary document in the `sessions` collection keyed by a 256-bit opaque id (the cookie value); a TTL index on `expiresAt` expires stale ones. `SessionManager` is shared across `MicroService` workers, and a per-process `SessionCache` (TTL'd LRU, ~60 s) fronts the DB lookup so a hot session resolves without a `wsdbagent` round trip. `revoke()` purges the cache entry **synchronously** so a logout takes effect at once. The store is reached through `IMongodbClient`, so it works unchanged in `--remote-db` mode.

### The `IIdentityProvider` abstraction

`OidcProvider` and `SamlProvider` both implement `sso::IIdentityProvider` — `begin_login()` returns the IdP redirect, `handle_callback()` returns `IdentityClaims`. The endpoint handlers depend only on the interface, so a third protocol could be added without touching callers.

- **OIDC** — discovery-driven (no IdP-specific code), Authorization Code flow + PKCE (S256). `handle_callback()` atomically claims the one-time `sso_transactions` document by `state` (a guarded `update_collection`, the same atomic-document primitive `next_awbno` uses), exchanges the `code` at the token endpoint, then verifies the `id_token`. `verify_jwt()` accepts **RS256 only** — `none` and every HMAC variant are rejected before any signature work, closing the algorithm-confusion attack class.
- **SAML 2.0** — SP-initiated. `AuthnRequest` via HTTP-Redirect (deflate + base64), `Response` via HTTP-POST to the ACS. `verify_saml_signature()` checks the XML-DSig against the configured IdP cert **only** — an embedded `<KeyInfo>` is ignored, and a signature-wrapping decoy is caught by requiring exactly one `<Signature>` and one `<Assertion>`. XML parsing is XXE-safe (no network, no DTD, no external entities). Needs **libxml2 + xmlsec1** — added to the bootstrap toolchain image and linked by the root `CMakeLists.txt`.

### Configuration — the `sso_config` collection

SSO config is secret-bearing (OIDC `clientSecret`) and lives in the `sso_config` MongoDB collection as a single document — **not** in git or a Heroku config var. It is authored by the on-prem Vaadin admin UI (`onprem/.../SsoConfigView.java` + `SsoConfigService.java`), which writes directly to its co-located MongoDB; there is no internet-facing config-write endpoint. The C++ backend reads it over the wsdbagent channel and **hot-reloads** every ~60 s on a background thread (`WebServer::init_sso()` / `reload_sso()`). A document that fails to parse is rejected and the last-good `ProviderRegistry` is kept, so a bad operator edit can never take down login.

### Provisioning — hybrid

`resolve_account()` resolves a verified `IdentityClaims` to an `account`: (1) match an account already linked via `ssoIdentities: [{provider, subject}]`; (2) else, within the provider's `allowedEmailDomains`, match by **verified** email and link the identity; (3) else JIT-create an account with a configurable default role. `email_verified == true` is required for the email-match path; a matched account keeps its existing DB role — the IdP never overrides it.

### Endpoints

`MicroService::process_request()` routes the `/api/v1/sso/` URI prefix to `handle_sso()`, which parses the request, calls the transport-agnostic `sso_endpoints.hpp` functions, and renders the returned `SsoHttpResult` onto the wire.

| Method | Path | Purpose |
|---|---|---|
| GET | `/api/v1/sso/providers` | configured providers for the login screen |
| GET | `/api/v1/sso/login?provider=&return_to=` | 302 to the IdP |
| GET | `/api/v1/sso/callback/<id>` | OIDC redirect callback → session + 302 |
| POST | `/api/v1/sso/callback/<id>` | SAML ACS (HTTP-POST `SAMLResponse`) → session + 302 |
| GET | `/api/v1/sso/session` | current account for the cookie, or 401 |
| POST | `/api/v1/sso/logout` | revoke session, expire cookie |

`handle_account_login_POST` also mints a session (`authMethod: "password"`), so the whole app uses one session model regardless of how the user authenticated. Because a credentialed (cookie-bearing) request is incompatible with `Access-Control-Allow-Origin: *`, the response builders now echo a specific allowed origin (`MicroService::cors_allowed_origin()`, validated against an allow-list) and add `Access-Control-Allow-Credentials: true`.

### Tests

`modules/module/sso/test/sso_test.cc` — 114 unit tests; the matching session/CORS/middleware tests live in `webservice_test.cc`. None touch a live MongoDB or the network — they run against mocks behind `IMongodbClient` / `IHttpClient`, inside the standard `offtarget` binary.

---

## inhouseidp module — in-house OIDC identity provider

`modules/module/inhouseidp/` is an OIDC IdP that xpmile runs itself, so federated login does not require a third-party Okta / Auth0 subscription. Every type lives in the **`idp::` namespace**. The full design is `docs/design/inhouse-idp/inhouse-idp-design.md`; the test-first plan is `inhouse-idp-tdd-plan.md` (the *Implementation status* table tracks what has shipped vs what is still pending).

**Model — two-Heroku-app, one-binary architecture.** The same `uniservice` binary serves both the marvel app (xpmile UI + API) and a second `idp` Heroku app (login portal). Which posture a dyno takes is decided at deploy time by the `IDP_ISSUER` env var — when unset (marvel dyno) every `/api/v1/idp/*` route returns 503; when set (idp dyno) the IdP routes activate. Both apps share *one* on-prem MongoDB via *two* `wsdbagent` instances behind the same NAT (Phase I — pending).

**Model — on-prem JWT signing.** The cloud-side IdP never holds the RSA private key. When it needs to sign an `id_token` it sends a `SIGN_JWT` request over the existing dbproto channel to `wsdbagent`, which reads the active key from `idp.idp_signing_keys`, signs, and returns the base64url signature plus the resolved kid. `idp::WsdbJwtSigner` is the cloud-side proxy implementing `idp::IJwtSigner`; `agent::sign_jwt_on_prem` is the only code path that ever sees private key bytes.

### Files

| File pair | Role |
|---|---|
| `idp_session.*` | `IdpSessionManager` (create / lookup / revoke / revoke_all_for_account) over `idp.sessions` |
| `idp_cookie.*` | build/parse `xpmile_idp_session` (12 h) + `xpmile_idp_pending` (10 min), both `Path=/api/v1/idp/` |
| `idp_client_registry.*` | `IdpClientRegistry` — registered RPs (clientId, redirect/post-logout URIs, scopes, grant types) loaded from `idp.idp_clients`; exact-byte URI match |
| `idp_discovery.*` | `build_discovery(issuer)` + `handle_idp_discovery_GET` — the static OIDC discovery doc (response_type=code only, RS256 only, S256 only) |
| `idp_jwks.*` | `jwks_from_keys` extracts modulus + exponent via OpenSSL; `handle_idp_jwks_GET` filters notAfter-expired keys |
| `idp_authorize.*` | `idp::authorize` — validates query (PKCE + state + scope + registry); either 302 to `/login` with `xpmile_idp_pending` cookie (no session) or mints a one-time `idp.idp_codes` doc and 302s to the RP (session valid) |
| `idp_login.*` | `idp::login` — resolves pending cookie, checks credentials via `IPasswordVerifier`, mints session, 302s back to `/authorize` with the original params (url-encoded) |
| `idp_token.*` | `idp::token` — atomic claim of the code (`update_collection` with `consumed:{$exists:false}` filter), PKCE check, signs via `IJwtSigner`, mints opaque access_token |
| `idp_userinfo.*` | `idp::userinfo` — Bearer-validates against `idp.idp_access_tokens`, returns claims (case-insensitive scheme) |
| `idp_end_session.*` | `idp::end_session` — registry-validated `post_logout_redirect_uri`, revokes session, expires cookie |
| `idp_password_reset.*` | `reset_request` (always 200 — no enumeration) + `reset_confirm` (validates TTL, hashes, updates idp.account, revokes all sessions) |
| `idp_password.hpp` | `IPasswordVerifier` — abstract; production wrap in Phase K |
| `idp_password_hasher.hpp` | `IPasswordHasher` — abstract; production wrap in Phase K |
| `idp_email_sender.hpp` | `IEmailSender` — abstract `{to, subject, body}`; production wrap of `EmailService` in slice 3 |
| `jwt_signer.hpp` | `IJwtSigner` interface + `SignJwtResult` |
| `wsdb_jwt_signer.*` | `WsdbJwtSigner` — production `IJwtSigner` over the dbproto `SIGN_JWT` op; rejects non-RS256 *before* wire I/O |

### Wire adapter (`MicroService::handle_idp`)

`process_request()` routes both `/api/v1/idp/*` and the bare `/.well-known/openid-configuration` URI to `handle_idp()`. The shape mirrors `handle_sso` — parse the `Http`, dispatch, render `SsoHttpResult` onto the wire — and reuses the same `SsoHttpResult` type because the contract is identical. `IDP_ISSUER` is read from the env on every request rather than threaded through `WebServer`, which keeps the marvel ↔ idp posture a pure deploy-time decision with zero state to plumb.

| Method | Path | Slice | Status |
|---|---|---|---|
| GET | `/.well-known/openid-configuration` | 1 | ✅ wired |
| GET | `/api/v1/idp/jwks` | 1 | ✅ wired |
| GET | `/api/v1/idp/authorize` | 2 | ✅ wired |
| GET | `/api/v1/idp/userinfo` | 2 | ✅ wired |
| POST | `/api/v1/idp/end_session` | 2 | ✅ wired |
| POST | `/api/v1/idp/login` | 3a | ✅ wired — `PbkdfPasswordVerifier` (wraps `MongodbClient::verify_password`) |
| POST | `/api/v1/idp/token` | 3a | ✅ wired — `WsdbJwtSigner` over `WebServer::wsDbServer()`; 503 in local-DB mode |
| POST | `/api/v1/idp/password/*` | 3b | 501 — pending `PbkdfPasswordHasher` + `SmtpEmailSender` impls |

The `IdpClientRegistry` is hot-reloaded from `idp.idp_clients` every ~60 s on a dedicated thread (`WebServer::init_idp` / `reload_idp`), mirroring the SSO config hot-reload. `WebServer::idpClientRegistry()` returns a value snapshot so concurrent reloads can't tear the read.

### Databases

The `idp` database (separate from `xpmile`) is the IdP's sole store:

| Collection | Purpose |
|---|---|
| `account` | passwordHash + email + name + role — split out of `xpmile.account` by `scripts/migrate-account-split.py` |
| `sessions` | live IdP login sessions, keyed by the `xpmile_idp_session` cookie value |
| `idp_clients` | registered RPs — authored by the on-prem Vaadin `IdpClientsView` (Phase J — pending) |
| `idp_signing_keys` | RSA keypairs — authored by the on-prem Vaadin signing-key admin (Phase B — pending) |
| `idp_pending_auth` | 10 min TTL — ties `/login` POST back to the originating `/authorize` GET |
| `idp_codes` | 30 s TTL — one-time OIDC authorization codes (atomic-claim) |
| `idp_access_tokens` | 1 h TTL — opaque bearer tokens used by `/userinfo` |
| `password_reset_tokens` | 1 h TTL — one-time reset tokens |
| `schema_version` | migration gating doc — only `xpmile` actually carries it; `idp` is the migration target |

The only intentional cross-DB read is the legacy `POST /api/v1/account/login` fallback (Phase K — pending), which reads `idp.account` from the marvel-side `webservice.cpp`. Q12 of the design kept legacy password login as the fallback rather than deprecating it.

### Tests

118 GTest under the `Idp*` / `Jwks` / `PasswordReset*` / `SignJwt*` prefixes across nine files in `modules/module/inhouseidp/test/` + `modules/module/wsdbagent/test/sign_jwt_dispatch_test.cc`. Plus 12 pytest for the migration script. All against mocks behind `IMongodbClient` — no live MongoDB. The `IdpToken.EndToEnd_RealRsaSign_VerifyWithJwks` test signs a real id_token with a build-time-generated RSA-2048 fixture key (CMake `idp_test_keys` custom target, the same one Phase A's `SignJwtDispatch*` tests use) and verifies it with the existing `sso::verify_jwt` against a JWKS built from the matching public key. Migration pytest tests run inside an ephemeral podman container — nothing installs on the host (`scripts/Dockerfile.test` + `scripts/run-script-tests.sh`).

---

## whatsapp module

A stub — empty `.hpp`/`.cpp` pair compiled into `uniservice` but containing no logic. Placeholder for a future WhatsApp notification integration.

---

## thirdparty module

`modules/module/thirdparty/json.hpp` — nlohmann/json single-header library (vendored). Used by `MongodbClient::from_json()` and throughout `webservice.cpp` for JSON serialisation/deserialisation.

---

## Infrastructure

### Docker — shared toolchain image (`docker/Dockerfile.bootstrap`)

Single-stage `ubuntu:focal` image that pre-builds the entire C++ toolchain (ACE/TAO 7.0.0 with SSL, mongo-c-driver 1.19.1, mongo-cxx-driver v3.6, googletest) under `/usr/local`. Every downstream Dockerfile starts with:

```dockerfile
ARG BUILDER_IMAGE=localhost/xpmile-cpp-builder:bootstrap
FROM ${BUILDER_IMAGE} AS cpp-builder
```

so the ~30 min toolchain compile is done once and reused across `uniservice`, `wsdbagent`, and `offtarget`. CI publishes the same image to `docker.io/naushada/xpmile-cpp-builder:bootstrap` (multi-arch) and downstream jobs override `BUILDER_IMAGE` to point at the Docker Hub tag. `run.sh build-bootstrap` / `deploy-heroku.sh build-bootstrap` build the local copy.

### Docker — main app image (`docker/Dockerfile`)

Three build stages:

**Stage 1 (`cpp-builder` / `FROM ${BUILDER_IMAGE}`):**
- Inherits the pre-built ACE/TAO + mongo-cxx + gtest from the bootstrap image
- Generates a per-build Heroku server cert signed by the committed CA (`certs/ca.crt`)
- Compiles `uniservice` and the `offtarget` test binary via CMake (`-j2` to avoid OOM; `-fconcepts` for GCC 9)

**Stage 2 (`ui-builder` / `node:18-alpine`):**
- `npm ci` then `ng build --configuration production`
- `ARG UI_BUST` forces a cache-bust without invalidating the C++ stage: `UI_BUST=$(date +%s) podman compose up --build app`

**Stage 3 (`runtime` / `ubuntu:focal`):**
- Copies only: ACE/TAO `.so` files, MongoDB driver `.so` files, `uniservice` binary, Angular `dist/webui/`
- Working directory: `/opt/xAPP/granada/` — relative path `../webgui/webui/` resolves to Angular output

### Docker — wsdbagent image (`docker/Dockerfile.wsdbagent`)

Two build stages; cpp-builder is `FROM ${BUILDER_IMAGE}`:
- `apt-get install`s `libxmlsec1-dev` in the build stage — the root `CMakeLists.txt`'s `pkg_check_modules(XMLSEC REQUIRED ...)` is resolved at configure time even for a `wsdbagent`-only build, though the binary never links xmlsec1
- Passes `-DBUILD_TESTS=OFF` so GTest is not required at compile time
- Builds only the `wsdbagent` target (`make -j2 wsdbagent`)
- Runtime stage copies the binary and shared libs; all flags passed via `ENV ARGS=""`
- Certs (rotated CA + client pair) bind-mounted at `/certs` — written by `./run-agent.sh refresh-certs`, watched by the cert-watcher sidecar; see "Agent cert rotation" below.

### Docker — test image (`docker/Dockerfile.test`)

Single-stage `FROM ${BUILDER_IMAGE}` image. Builds `offtarget` with `-DBUILD_TESTS=ON`, copies `certs/` for the InnerTLS handshake tests, and sets `CMD ["./offtarget", "--gtest_color=yes", "--gtest_filter=-..."]` excluding three Mongo-dependent tests. CI's `test` job builds this image, loads it into the local docker daemon (`docker/build-push-action@v5` with `load: true`), then runs `docker run --rm xpmile-test:ci` as the quality gate before any publish or release.

### docker-compose.yml

Two services on an isolated bridge network (`xpmile-net`):

- **`mongodb`** — custom image with `mongo-init.js` baked in; runs as UID 999 (MongoDB system user) to avoid `/proc/1/fd` permission issues on some Linux hosts; data persisted in named volume `mongo-data`.
- **`app`** — depends on `mongodb` with `condition: service_healthy`; passes the MongoDB URI with app credentials via `ARGS` env var.

### docker-compose.agent.yml

Three services on `agent-net`, intended for the on-prem MongoDB machine behind NAT:

- **`mongodb`** — same custom image as the main stack; container name `agent-mongo`.
- **`wsdbagent`** — outbound-only WS DB proxy to uniservice; mounts `${CERTS_DIR:-./certs/cloud-issued/innertls}` at `/certs:ro`.
- **`xpmile-cert-watcher`** — alpine sidecar that md5sums the cert dir every 5 s and POSTs `/libpod/containers/agent-wsdbagent/restart` via the host podman socket on any change. Bind-mounts `${PODMAN_SOCKET:-/run/podman/podman.sock}`.

### Agent cert rotation

`docker/Dockerfile` mints a fresh CA per uniservice build (see "Docker — main app image"). The matching client cert family is staged at `/opt/xAPP/granada/agent-certs/` inside the image. Two entry points pull it out:

| Trigger | Command | Source image | Used when |
|---|---|---|---|
| Laptop deploy | `./deploy-heroku.sh extract-agent-certs` (auto-invoked by `deploy`) | Locally built image | Operator deploys directly from a dev machine |
| On-prem refresh | `./run-agent.sh refresh-certs` | `docker.io/naushada/xpmile-uniservice:latest` (override with `UNISERVICE_IMAGE=...`) | CI deployed; operator wants to follow up |

Both write the family to `./certs/cloud-issued/innertls/` (gitignored — canonical source is the published image). The `xpmile-cert-watcher` sidecar then restarts `agent-wsdbagent`. End-to-end refresh-to-reconnect: ~15 s (5 s detect + 5–10 s restart + 1–2 s handshake). Tune the watcher's poll interval with `CERT_WATCH_POLL_SECONDS`; switch the socket path with `PODMAN_SOCKET` for rootless setups.

### mongo-init.js

Runs once on first container start (when `mongo-data` volume is empty):
1. Creates `xpmile` app user with `readWrite` on the `xpmile` database.
2. Inserts a bootstrap admin document into `xpmile.account`:
   - `accountCode: "admin"` / `accountPassword: "admin@123"`
   - Role: `"Admin"`, event location: `"UAE"`

### Account schema — where each field lives

After the Phase pre-A split, each account is split across two collections in two databases:

| Collection | Field | Notes |
|---|---|---|
| `xpmile.account` | `loginCredentials.accountCode` | Login key (e.g. `admin`); guaranteed non-empty. Used as the navbar fallback when `personalInfo.name` is blank. |
| `xpmile.account` | `loginCredentials.passwordHash` | Legacy field — present pre-migration; **moved to `idp.account` by `scripts/migrate-account-split.py`**. Cross-DB fallback in `webservice.cpp` reads `idp.account` if absent here (Phase K). |
| `xpmile.account` | `awbPrefix`, `isAccountCodeAutoGen` | Business fields for AWB numbering. |
| `xpmile.account` | `personalInfo.name` | What the marvel navbar renders next to the user icon. Required by Create Account / Update Account forms (PR #48). |
| `xpmile.account` | `personalInfo.role` | `Admin` / `User` / `Customer` / etc. — drives UI menu visibility and the navbar's name-vs-companyName branch. |
| `xpmile.account` | `personalInfo.email`, `contact`, `address`, `city`, `state`, `postalCode`, `eventLocation` | Operator-facing metadata. |
| `xpmile.account` | **`personalInfo.photoBase64`** *(optional)* | **Profile photo as a base64 data URL** (`data:image/jpeg;base64,…`) — added by PR #49 + #51. Client-side resized to ≤256×256 + JPEG @ 0.85 before upload → ~30-50 KB per photo. Stored inline rather than GridFS or a separate collection because every login already fetches the full account doc in one round trip; adding ~50 KB to that payload is cheaper than a second query, and well under mongo's 16 MB BSON limit + the dbproto message budget. The marvel navbar renders this directly as the `<img>` `src`; absent → falls back to `<clr-icon shape="user">`. Three upload entry-points: **Create Account** form, **Update Account** form, and the **My Profile** modal accessed from the navbar's user dropdown. |
| `xpmile.account` | `customerInfo.{companyName, quotedAmount, tradingLicense, vat, currency, bankAccountNumber, iban}` | Only meaningful when `personalInfo.role === 'Customer'`. |
| `idp.account` | `passwordHash` | PBKDF2-SHA256 post-migration. Both legacy login (Phase K fallback) and `/api/v1/idp/login` read here. |
| `idp.account` | `email`, `name`, `role` | Mirrored from `personalInfo` at migration time so the IdP can serve OIDC `userinfo` without crossing back to `xpmile`. |

Profile-photo storage deserves an explicit callout because operators reasonably expect `mongofiles` / GridFS for image storage — we deliberately don't, because the cost calculus favoured a single-round-trip account fetch (the photo IS account metadata viewed at every login) over a separate file collection that would need its own access-control, indexing, and per-photo cleanup.

---

## Test suite

**Runner:** `test/main.cc` + `test/CMakeLists.txt` — builds the `offtarget` binary, discovered and run by `ctest`.

**Module test files:**

| Module | Location | What is tested |
|---|---|---|
| `http` | `modules/module/http/test/` | `Http` parser: URI, query strings, headers, body (Content-Length, chunked, gzip, combined), `header()` boundary, response `status()` |
| `webservice` | `modules/module/webservice/test/` | `MicroService`: response builders (200, 201, 302, 4xx, 5xx), `get_contentType()`, OPTIONS handler, CORS origin echo, session middleware, password-login session minting |
| `email` | `modules/module/email/test/` | `SMTP::User` FSM: GREETING state transition via `rx()`, `SMTP::Account` population from JSON |
| `sso` | `modules/module/sso/test/` | SSO (`sso_test.cc`): cookies, sessions + LRU cache, config parse + hot-reload, OIDC discovery/PKCE/JWKS/JWT, SAML parse/conditions/XML-DSig, hybrid provisioning, endpoint logic, CSRF |

The SSO phases added **127 tests** (`docs/design/sso/sso-tdd-plan.md`); zero failures is the baseline across the whole `offtarget` suite. None of the SSO tests touch a live MongoDB or the network.

Note: `EmailServiceTest` is a `testing::Test` subclass with a custom constructor that accepts a JSON string and initializes `mMongodbc`/`mUser` directly, because `SetUp()` is only invoked by the gtest fixture machinery — not when the object is constructed directly in a `TEST()` body.

### Shipment-creation benchmark

`scripts/bench-shipments.py` (stdlib-only, Python 3) times shipment
creation end-to-end against any running backend (default: Heroku).
For each batch size N it runs `POST /api/v1/shipment/shipping` N
times sequentially (**SINGLE**) and then `POST
/api/v1/shipment/bulk/shipping` once with an N-item array (**BULK**),
printing a side-by-side table of wall-clock total + per-shipment
latency. Default sweep: 10, 50, 100, 500, 1000. Useful for spotting
regressions in AWB counter throughput, `insert_many` performance, or
Heroku router latency. Caveats and usage details in
[`docs/bench-shipments.md`](docs/bench-shipments.md).

---

## Build system

Root `CMakeLists.txt`:
- `add_subdirectory(modules/module/mongodb)` — builds `mongodbcxx` static library
- `option(BUILD_TESTS "Build test suite" ON)` — guards `add_subdirectory(test)`; pass `-DBUILD_TESTS=OFF` when building `wsdbagent` without GTest
- `pkg_check_modules(XMLSEC REQUIRED xmlsec1-openssl)` — pulls in `libxml2` + `xmlsec1`, linked into `uniservice` for SAML XML-DSig verification (the sso module). `wsdbagent` does not use it.
- `add_executable(uniservice ...)` — globs all `*.cpp` from webservice, http, email, sso, whatsapp `src/` directories; also links in wsdbproxy sources
- `add_executable(wsdbagent ...)` — globs `wsdbagent/src/*.cpp`, adds `wsdbproxy/src/dbproto.cpp` and `wsdbproxy/src/wsframe.cpp`; links `pthread ACE ACE_SSL ssl crypto mongodbcxx z`

`modules/module/mongodb/CMakeLists.txt` — builds `mongodbcxx` as a separate static library target.

`test/CMakeLists.txt` — explicit source list (not globbed) for the test binary.

Standard: C++20 (`-std=c++2a`) for `uniservice` / `offtarget`; C++17 for `mongodbcxx`.

---

## Build & deploy timing

Container build times on an M1/M2 Mac with podman, fibre internet, healthy `~/.local/share/containers` storage:

| Path | What runs | Time (cached) | Time (cold) |
|---|---|---|---|
| `./run.sh build-bootstrap` / `./deploy-heroku.sh build-bootstrap` | ACE/TAO + mongo-c/cxx + gtest into `xpmile-cpp-builder:bootstrap` | n/a (one-shot) | **25–35 min** |
| `./run.sh build-ui` / `./deploy-heroku.sh deploy` | Angular `ng build` only; bootstrap + cpp-builder + base layers reused | **2–4 min** | n/a (UI rebuilds only) |
| `./run.sh build` / `./deploy-heroku.sh deploy full` | uniservice compile + Angular (bootstrap reused) | **5–8 min** | **5–8 min** + bootstrap cold if missing |
| `./deploy-heroku.sh push` | Push image to `registry.heroku.com` | **3–6 min** (network-bound) | — |
| `./deploy-heroku.sh release` | Heroku activates the new image; dyno restarts | **<1 min** | — |

Bootstrap is a one-time cost per host. Subsequent `build` / `build-ui` / `deploy` runs reuse it.

Inside the cold bootstrap build, the dominant costs are:

| Step | Time | Notes |
|---|---|---|
| ACE/TAO compile from source | ~15 min | Single-threaded for many sub-targets; biggest single contributor |
| mongo-cxx-driver + bsoncxx | ~5 min | CMake reconfigure + compile |
| googletest | ~30 s | Header + library compile |

Inside a per-service warm build (bootstrap already cached):

| Step | Time | Notes |
|---|---|---|
| `uniservice` + `offtarget` link | ~3 min | Includes all C++ modules + the test binary |
| `npm ci` (Angular deps) | ~3 min | 1 100+ packages from npm |
| `ng build --configuration production --aot` | ~3 min | With `NODE_OPTIONS=--max_old_space_size=1536` to avoid OOM on vfs_fonts |

### The cache-eviction trap

`podman rmi $(podman images -f "dangling=true" -q)` (the cleanup snippet in this file) frees disk *and* drops the per-service `cpp-builder` intermediate images. The next deploy then has nothing to reuse from the *uniservice compile* layer and re-links from scratch (~3–5 min extra).

The `xpmile-cpp-builder:bootstrap` tag is named, not dangling, so dangling-prune leaves it alone. A broader `podman image prune -a` or a manual `podman rmi xpmile-cpp-builder:bootstrap` does drop it, after which the next `build` / `build-ui` / `deploy` triggers `cmd_build_bootstrap` and rebuilds the toolchain from scratch (~25–35 min).

**Symptom:** you run `./deploy-heroku.sh deploy` expecting a 3-minute UI redeploy, and instead see `STEP N: RUN make install ssl=1 ...` from ACE/TAO.

**Avoid by:**
- Pruning only when `podman system df` shows >80 % reclaimable space.
- Pulling the cached bootstrap from Docker Hub when CI has a fresher one: `podman pull docker.io/naushada/xpmile-cpp-builder:bootstrap && podman tag ... localhost/xpmile-cpp-builder:bootstrap`.
- Reserving aggressive prunes for off-hours when a 30-minute rebuild is acceptable.

### Deploy disk space

Heroku container builds need ~15 GB free under `~/.local/share/containers` on macOS (podman VM disk). Symptoms of running low:

```
error: write /ui/node_modules/esbuild-linux-64/bin/esbuild: no space left on device
```

`podman system df` reports current usage; `podman system reset` (destructive — wipes everything) is the nuclear option if a prune isn't enough.

---

## Continuous integration

`.github/workflows/publish-images.yml` is the single CI/CD entry point. Triggers:

- **`push` to `main`** on path matches (full pipeline including Docker Hub publish + Heroku release)
- **`pull_request` against `main`** on the same path matches (gating mode — runs `bootstrap` + `test` only; publish + release jobs guarded by `if: github.event_name != 'pull_request'`)
- **`workflow_dispatch`** for manual reruns / branch validation

Path filter: `docker/Dockerfile*`, `CMakeLists.txt`, `modules/**`, `test/**`, `ui/**`, `certs/**`, or the workflow file itself.

### Pipeline shape

```
push to main:
  bootstrap  ─┬─►  test  ─┬─►  wsdbagent     (Docker Hub, multi-arch)
              │            │
              │            └─►  uniservice    (Docker Hub amd64 + registry.heroku.com/marvel/web)
              │                     │
              │                     └─► heroku container:release web → marvel dyno
              └─ (concurrency block cancels older runs on same ref)

PR against main:
  bootstrap  ───►  test       ──► [wsdbagent + uniservice skipped]
                       │
                       └─► status check `Run offtarget GTest suite` reported to PR
                           (branch protection blocks merge if missing/failing)
```

| Job | Runner | Platforms | Output | Skipped on PR? |
|---|---|---|---|---|
| **bootstrap** | `ubuntu-latest` | linux/amd64, linux/arm64 | `docker.io/naushada/xpmile-cpp-builder:{bootstrap,<sha>}` + `:buildcache` | no |
| **test** | `ubuntu-latest` | linux/amd64 | `xpmile-test:ci` (loaded into local docker daemon, then `docker run --rm`); 3 Mongo-dependent tests skipped by `--gtest_filter` | no |
| **wsdbagent** | `ubuntu-latest` | linux/amd64, linux/arm64 | `docker.io/naushada/xpmile-wsdbagent:{latest,<sha>}` | **yes** |
| **uniservice** | `ubuntu-latest` | linux/amd64 | `docker.io/naushada/xpmile-uniservice:{latest,<sha>}` + `registry.heroku.com/marvel/web`, then `heroku container:release web --app marvel` | **yes** |

### Branch protection (gating)

Configured on `main` via `gh api -X PUT repos/naushada/xpmile/branches/main/protection`:

| Setting | Value | Why |
|---|---|---|
| Required check | `Run offtarget GTest suite` | The test job — fails → merge blocked |
| `strict` | `true` | PR branch must be up-to-date with main before merge (forces re-run if main moved) |
| `enforce_admins` | `false` | Repo admins keep an emergency override for CI outages |
| `allow_force_pushes` / `allow_deletions` | `false` | main history is append-only |
| Required PR reviews | none | self-merge OK; revisit when a collaborator joins |

Inspect / update with:
```sh
gh api repos/naushada/xpmile/branches/main/protection | jq .
gh api -X PUT repos/naushada/xpmile/branches/main/protection --input protection.json
```

### Required repo secrets

| Secret | Source | Use |
|---|---|---|
| `DOCKERHUB_USERNAME` | `naushada` | Docker Hub login |
| `DOCKERHUB_TOKEN` | https://hub.docker.com/settings/security (write scope) | Docker Hub push |
| `HEROKU_API_KEY` | `heroku auth:token` | Heroku registry push + `heroku container:release` |

### Caveats

- **`provenance: false` + `outputs: type=image,oci-mediatypes=false`** on the uniservice build — Heroku's registry rejects buildx attestation manifests and is picky about OCI mediatypes; together these tell buildx to emit a plain Docker v2s2 manifest (matching `podman push --format=v2s2` in `deploy-heroku.sh`). Mirrors onprem-pbx publish-cloud.
- **Heroku release uses the official CLI** (`heroku container:release web --app marvel`), installed one-shot inside the uniservice job. An earlier attempt at `PATCH /apps/marvel/formation` with the buildx-reported digest hit `404 record_not_found` because Heroku's registry stores its own digest distinct from the OCI manifest digest; the CLI re-resolves it correctly.
- **Concurrency block** cancels older in-flight runs only for runs queued *after* the block was added. Runs queued before continue to completion.
- **Mongo-dependent tests** (`AccountLoginTest.ValidCredentials_*`, `AccountLoginTest.ResponseBody_*`, `WsDbServer.SecondAgentRejected_When_FirstAlive`) are excluded by `Dockerfile.test`'s CMD filter. Wire a Mongo sidecar into the `test` job and drop the filter to include them.
- **`deploy-heroku.sh`** is still the manual escape hatch — useful for hotfixes from a feature branch, offline deploys, or when CI is down. Admin-exempt branch protection means a force-push from the dev's laptop still works (but `allow_force_pushes: false` blocks it on main anyway, so the escape hatch is really "build locally + push to Heroku registry directly").

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
