# ws-db-agent: WebSocket DB Agent

The `wsdbagent` binary runs on the MongoDB machine and connects **outbound** to the
`uniservice` on `/ws/db`. All traffic between the two sides is protected by **mutual
TLS (mTLS)** — both the server and the agent present certificates signed by a shared
private CA, so neither side accepts an unknown peer.

---

## Topology

### Heroku (Heroku-edge TLS, no mTLS)

Heroku terminates TLS at its router. `uniservice` uses the existing
`on_agent_connected` path (WebConnection hands the socket to WsDbServer after the
WebSocket upgrade on the main HTTP port). mTLS is not available here because Heroku
does not forward client certificates to the dyno.

```
┌──────────────────────────────────────────────────────────┐
│  Heroku Dyno                                             │
│  uniservice  --remote-db                                 │
│  └─ WsDbServer  /ws/db  (no dedicated port)              │
└──────────────────────────────────────────────────────────┘
          ▲  WSS  wss://myapp.herokuapp.com/ws/db
          │  (TLS terminated by Heroku edge)
┌─────────┴────────────────────────────────────────────────┐
│  MongoDB Machine (behind NAT)                            │
│  wsdbagent  --server-host myapp.herokuapp.com            │
└──────────────────────────────────────────────────────────┘
```

### Self-hosted (mTLS, dedicated agent port)

`uniservice` binds a second port exclusively for agent connections. The port uses
`ACE_SSL_SOCK_Acceptor` and requires a client certificate signed by the shared CA
(`SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT`). `wsdbagent` loads the CA cert
(server verification) and its own client cert/key before connecting.

```
┌──────────────────────────────────────────────────────────┐
│  Self-hosted server                                      │
│  uniservice  --remote-db                                 │
│              --agent-port 8443                           │
│              --tls-cert certs/server.crt                 │
│              --tls-key  certs/server.key                 │
│              --tls-ca   certs/ca.crt                     │
│  └─ WsDbServer  mTLS acceptor on :8443                   │
└──────────────────────────────────────────────────────────┘
          ▲  TLS (mTLS — both sides present certs)
          │
┌─────────┴────────────────────────────────────────────────┐
│  MongoDB Machine (behind NAT)                            │
│  wsdbagent  --server-host myserver.example.com           │
│             --server-port 8443                           │
│             --tls-ca   certs/ca.crt                      │
│             --tls-cert certs/client.crt                  │
│             --tls-key  certs/client.key                  │
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
certs/
├── generate.sh             OpenSSL script — used only for local dev (manual rotation)
└── cloud-issued/innertls/  Rotated client cert family from the latest published
                            xpmile-uniservice image (gitignored, populated by
                            ./run-agent.sh refresh-certs)
docker/
└── Dockerfile.wsdbagent    Standalone container image
```

---

## Certificates

### Rotation model (production)

`docker/Dockerfile` mints a fresh CA + server + client cert family **every build**:
the CA private key is born and purged inside a single `RUN` so it never lands in
any image layer. The server family is baked into the runtime image at
`/opt/xAPP/granada/certs/`; the matching client family is staged at
`/opt/xAPP/granada/agent-certs/` for extraction.

Because every Heroku deploy rolls the CA, on-prem `wsdbagent` instances must
refresh their cert pair in lockstep — otherwise the next reconnect fails with
`tls_process_client_certificate verify failed`.

### Bringing the stack up on the MongoDB machine

First-time, one-shot:

```sh
cp .env.agent .env             # set SERVER_HOST=marvel-…herokuapp.com
./run-agent.sh start           # auto-refresh certs + start mongodb + wsdbagent + cert-watcher
```

`./run-agent.sh start` auto-invokes `refresh-certs` when
`./certs/cloud-issued/innertls/` is missing or empty. The wsdbagent image
is pulled from Docker Hub (`docker.io/naushada/xpmile-wsdbagent:latest`,
published per deploy by CI) — no local build needed. Pin to a specific
build by setting `WSDBAGENT_IMAGE=docker.io/naushada/xpmile-wsdbagent:<sha>`
in `.env`.

### Manual cert refresh (between deploys)

```sh
./run-agent.sh refresh-certs   # podman pull + extract → certs/cloud-issued/innertls/
```

Pulls `docker.io/naushada/xpmile-uniservice:latest`, runs `podman cp
/opt/xAPP/granada/agent-certs/.` into `./certs/cloud-issued/innertls/`. Pin to
a specific deploy by setting `UNISERVICE_IMAGE=...:<sha>`.

The `xpmile-cert-watcher` sidecar (in `docker-compose.agent.yml`) md5sums that
dir every `CERT_WATCH_POLL_SECONDS` (default 5) and POSTs
`/libpod/containers/agent-wsdbagent/restart` via the host podman socket on any
change. End-to-end rotation latency from `refresh-certs` to a fresh wsdbagent
handshake ≈ 15 s.

For continuous rotation, run `refresh-certs` on a systemd timer or cron:
```sh
echo '*/15 * * * * cd /path/to/xpmile && ./run-agent.sh refresh-certs >/dev/null' | crontab -
```

### Local dev (no rotation)

```sh
cd certs && ./generate.sh
```

Produces a once-and-done CA + server + client family at the repo root. Useful
when running uniservice locally against wsdbagent on the same machine. Not
used by the production deploy path.

| File | Used by | Keep secret? |
|------|---------|--------------|
| `ca.crt` | both sides | no — distribute freely |
| `server.crt` | uniservice | no |
| `server.key` | uniservice | **yes** (gitignored) |
| `client.crt` | wsdbagent | no |
| `client.key` | wsdbagent | **yes** (gitignored) |

### How mTLS is enforced

**Server (`InnerTlsServer` constructor):**
```
SSL_CTX_new(TLS_server_method())
  SSL_CTX_use_certificate_file()  ← server.crt
  SSL_CTX_use_PrivateKey_file()   ← server.key
  SSL_CTX_load_verify_locations() ← ca.crt
  SSL_CTX_add_client_CA()         ← ca.crt (populates CertificateRequest CA list)
SSL_CTX_set_verify → SSL_VERIFY_PEER
```
The server verifies the client cert if one is presented, but does not require it. The agent's identity is already authenticated by the outer Heroku TLS + WebSocket upgrade path; inner TLS provides encryption and server authentication.

**Client (`WsDbAgent::connect_and_handshake()`):**
```
ACE_SSL_Context → SSLv23_client mode
  load_trusted_ca()← ca.crt   (verify server cert)
  certificate()    ← client.crt
  private_key()    ← client.key
SSL_CTX_set_verify → SSL_VERIFY_PEER
```
The agent refuses to connect if the server's cert is not signed by the CA.

---

## CLI

### wsdbagent

```
wsdbagent [OPTIONS]

Required:
  --server-host  <host>    Hostname to connect to
  --mongo-db-uri <uri>     MongoDB connection URI
  --mongo-db-name <name>   Database name

Optional:
  --server-port  <n>       Port (default: 443)
  --tls-ca       <path>    CA certificate — enables server cert verification
  --tls-cert     <path>    Client certificate (inner TLS mTLS)
  --tls-key      <path>    Client private key  (inner TLS mTLS)
  --tls-hostname <name>    Expected server CN/SAN (default: skip CN check; CA chain still verified)
  --mongo-db-connection-pool <n>  Pool size (default: 10)
  --backoff  <secs>        Base reconnect wait in seconds (default: 5; doubles per consecutive failure, capped at 60 s)
  --help
```

### uniservice (remote-db mode)

```
uniservice --remote-db [OPTIONS]

mTLS agent port (all four required together):
  --agent-port <n>      Dedicated port WsDbServer listens on for agent connections
  --tls-cert   <path>   Server certificate (PEM)
  --tls-key    <path>   Server private key  (PEM)
  --tls-ca     <path>   CA cert used to verify the agent's client cert (PEM)
```

When `--agent-port` and all three TLS flags are provided, `WsDbServer` binds its own
`ACE_SSL_SOCK_Acceptor` on that port and handles the WebSocket upgrade itself. Without
them, `WsDbServer` falls back to the `on_agent_connected` path (Heroku mode).

---

## Runtime behaviour

### Connect loop

`WsDbAgent::run(backoff_secs)` loops indefinitely:

1. Call `connect_and_handshake()`.
2. On success → `setup_inner_tls()` → `run_session()`.
3. On failure (including a `503` stale-eviction response) or session end → sleep current backoff → retry.
4. Exit only when `stop()` is called.

**Connect timeout (10 s).** `ACE_SSL_SOCK_Connector::connect()` and the plain `ACE_SOCK_Connector::connect()` are called with an `ACE_Time_Value(10)`. Without this, a wedged Heroku dyno (router accepts the SYN, dyno never completes the TLS handshake) would block the connect syscall for ~3 minutes per attempt. The 10 s timeout makes every retry fail fast and lets the backoff schedule do its job. The error log includes `errno` so `ETIMEDOUT` (110) is distinguishable from `ECONNREFUSED`, `EHOSTUNREACH`, etc.

**Exponential backoff.** The sleep between retries starts at `backoff_secs` (default 5 s) and **doubles after each consecutive failure**, capped at 60 s:

| Attempt | Sleep before next try |
|---------|-----------------------|
| 1 (initial)         | 5 s  |
| 2 (still failing)   | 10 s |
| 3                   | 20 s |
| 4                   | 40 s |
| 5+                  | 60 s (capped) |

A **successful session resets the backoff** to the base value before the next retry, so a single transient disconnect doesn't push the next reconnect to 60 s. Combined with the 10 s connect timeout, sustained outages settle into a steady ~70 s cycle (10 s connect timeout + 60 s sleep) instead of the unbounded ~3 min hang per attempt seen before.

**503 during reconnect:** If the agent reconnects while the server is still tearing down the previous stale session, the server replies `503 Retry-After: 2`. The agent treats this as a connect failure (it counts toward the backoff schedule). Within one or two cycles the server's `run_session()` will have exited and the reconnect succeeds — at which point the backoff resets.

### Handshake (`connect_and_handshake`)

1. Configure `ACE_SSL_Context` with CA / client cert / key (if provided).
2. Open a TLS (or plain TCP) connection to `--server-host:--server-port` **with a 10 s timeout**.
3. Send a standard RFC 6455 upgrade request to `/ws/db`.
4. Read the HTTP response headers until `\r\n\r\n`.
5. Verify status `101` and `Sec-WebSocket-Accept`.
6. Return `true`, after which `setup_inner_tls()` performs the inner TLS handshake over WebSocket frames.

### Reconnect path (when the dyno crashes or restarts)

There is **no separate fallback transport** — every connection attempt uses the same outer-HTTPS-then-inner-TLS path. The recovery sequence when the remote dyno dies mid-session:

```
agent in run_session()
      │
      ▼
recv_ready(fd, 30s) returns POLLIN on closed socket
      │
      ▼
m_innerTls->recv() → m_transport.recv() reads 0 bytes (EOF)
      │            or peer sent Close frame (opcode 0x08)
      ▼
"inner TLS recv failed — disconnecting"   ← logged
      │
      ▼
disconnect()  →  stream_close()  →  break out of run_session()
      │
      ▼
sleep(cur_backoff)   ← exponential schedule above
      │
      ▼
fresh connect_and_handshake() over outer HTTPS (port 443)
      │  ├─ if dyno still wedged: SSL connect times out in 10 s → next backoff slot
      │  └─ if dyno is back:     101 Switching Protocols + inner TLS handshake → session resumes
      ▼
backoff resets to base after first successful session

### Session loop (`run_session`)

Before each frame recv, `poll(fd, POLLIN, 30 000 ms)` is called:
- **Data ready** → call `ws_recv_frame()` normally.
- **Timeout (30 s idle)** → send a Ping frame to verify liveness. If the send fails, the connection is dead → exit.

| Opcode | Action |
|--------|--------|
| `0x02` Binary | Parse BSON → `DbRequest` → `dispatch()` → send BSON response |
| `0x09` Ping | Send Pong frame |
| `0x0A` Pong | No-op (keepalive reply from server) |
| `0x08` Close | Send Close frame → exit session loop |

On any recv error, or if a Ping cannot be sent, the loop exits and `run()` reconnects after backoff.

**Why the agent sends Pings (not the server):** The agent is the only side that knows when the connection is idle from its perspective. Using `poll` with a 30-second timeout on the recv fd is simpler than a separate ping thread and works correctly even when the server is busy dispatching DB responses.

### Dispatch table

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

**Serialisation notes:**
- `CREATE_BULK_DOCUMENT`: `req.doc` carries raw JSON string bytes.
- `UPDATE_BULK_DOCUMENT`: `req.doc`/`req.doc2` are JSON-array bytes from `nlohmann::json::dump()`.
- `STORE_FILE`: `req.sval` is `"filename|content_type"` (pipe-separated).

### Frame encoding

- Agent → server frames are **masked** (`MASK = 1`) per RFC 6455 §5.3.
- Server → agent frames are **unmasked**.
- `wsframe::encode()` / `wsframe::decode()` handle framing on both sides.

### Large response framing (inner TLS recv contract)

One inner-TLS message = one BSON `DbRequest`/`DbResponse`. The proxy and agent rely on `InnerTls{Client,Server}::recv()` returning the **full plaintext** that a single peer `send()` produced — even when the plaintext spans multiple TLS records.

`SSL_read` returns at most one TLS record (~16 KB) per call, so a 60 KB shipment-list response produces ~4 records. The recv path:

1. `m_transport.recv()` returns the ciphertext of all records in one WebSocket binary frame.
2. `BIO_write` feeds it into the read BIO.
3. `recv()` loops `SSL_read` until `SSL_ERROR_WANT_READ`, appending each decrypted record's plaintext into one contiguous buffer.
4. Returns the full BSON payload in a single call.

**Historical bug (fixed 2026-05-12):** the loop wasn't there — `recv()` did one `SSL_read` into a 16384-byte buffer, silently truncating responses larger than that. `parse_response` then iterated a BSON view whose `size` didn't match its encoded length and produced no elements, leaving `rsp.reqid = -1, rsp.ok = false`. The proxy logged `no pending request for reqid=-1`, the response was dropped, and the browser request hit Heroku's 30 s H12 timeout. Surfaced by `ShipmentStatsService` polling the entire shipment history every 60 s. See the `Recv_ReturnsFullPlaintext_InSingleCall` regression test in `modules/module/security/test/innertls_test.cc`.

---

## Building

```cmake
file(GLOB MODULE_WSDBAGENT_SOURCES "modules/module/wsdbagent/src/*.cpp")

add_executable(wsdbagent
    ${MODULE_WSDBAGENT_SOURCES}
    modules/module/wsdbproxy/src/dbproto.cpp
    modules/module/wsdbproxy/src/wsframe.cpp
)
target_link_libraries(wsdbagent pthread ACE ACE_SSL ssl crypto mongodbcxx z)
```

Pass `-DBUILD_TESTS=OFF` when building without GTest (as `Dockerfile.wsdbagent` does).

---

## Docker deployment

The MongoDB machine behind NAT runs two containers. Only outbound connections are
needed — no inbound ports need to be opened on the NAT firewall.

```
MongoDB machine (behind NAT)
├── mongodb container   — mongod, data on a named volume
└── wsdbagent container — connects outbound, stateless (no volume)
```

### Quick start — build and run both containers (Heroku)

Use `run-agent.sh` from the **repo root** on the MongoDB machine. It wraps
`podman-compose`, handles `.env` setup, and waits for the MongoDB healthcheck.

```
./run-agent.sh build    # build both images (20–30 min on first run)
./run-agent.sh start    # start MongoDB + wsdbagent
./run-agent.sh stop     # stop containers (data volume preserved)
./run-agent.sh restart  # stop then start
./run-agent.sh logs     # follow live logs from both containers
./run-agent.sh status   # show container status
./run-agent.sh clean    # stop containers AND delete the MongoDB data volume
```

#### First run

```sh
# 1. Make the script executable (once)
chmod +x run-agent.sh

# 2. Build both images
./run-agent.sh build
#   → prompts for SERVER_HOST if .env is missing, then copies .env.agent → .env

# 3. Start
./run-agent.sh start
#   → waits for MongoDB to become healthy, then prints container status
```

#### What to look for

After `start`, both containers should show `Up`:

```
NAMES              STATUS                   IMAGE
agent-mongo        Up X minutes (healthy)   xpmile-mongo:latest
agent-wsdbagent    Up X minutes             wsdbagent:latest
```

In the wsdbagent log (`./run-agent.sh logs` or `podman logs -f agent-wsdbagent`):

```
[WsDbAgent] connecting to marvel.herokuapp.com:443 (ssl=1)
[WsDbAgent] session started
```

On the Heroku side (`heroku logs --tail --app marvel`):

```
[WsDbServer] agent connected
```

#### Environment variables

`run-agent.sh build` / `start` will prompt for `SERVER_HOST` if `.env` is
missing or still set to the placeholder. To set everything non-interactively,
edit `.env` directly (copy from `.env.agent`):

| Variable | Default | Description |
|---|---|---|
| `SERVER_HOST` | *(required)* | Heroku hostname, e.g. `marvel.herokuapp.com` |
| `SERVER_PORT` | `443` | Port to connect to |
| `MONGO_ROOT_USER` | `root` | MongoDB root username |
| `MONGO_ROOT_PASS` | `changeme` | MongoDB root password |
| `MONGO_APP_USER` | `xpmile` | App DB username |
| `MONGO_APP_PASS` | `xpmile_pass` | App DB password |
| `MONGO_DB` | `xpmile` | Database name |
| `MONGO_POOL` | `10` | MongoDB connection pool size |
| `BACKOFF` | `5` | Base reconnect wait in seconds. Doubles per consecutive failure (5 → 10 → 20 → 40 → 60), capped at 60 s. Resets to base after a successful session. |

---

### Compose stack — production (Heroku)

`docker-compose.agent.yml` at the repo root runs both MongoDB and wsdbagent
together. Copy `.env.agent` to `.env`, set `SERVER_HOST`, then:

```sh
podman-compose -f docker-compose.agent.yml up --build -d
```

Containers reach each other by service name (`mongodb`) on the `agent-net`
bridge — no `--network host` needed. MongoDB data is persisted in the
`mongo-data` named volume.

For mTLS (self-hosted uniservice), uncomment the `volumes` and append the TLS
flags in `docker-compose.agent.yml` as shown in the inline comments.

### Compose stack — local development

Run the full remote-db stack on one machine for local testing. Two terminals:

**Terminal 1 — uniservice with `--remote-db`:**
```sh
REMOTE_DB=1 podman-compose up --build
```
uniservice listens on `localhost:8080` and waits for wsdbagent to connect on
`/ws/db`. The `mongodb` service in this compose still starts (it's harmless;
uniservice ignores it in `--remote-db` mode).

**Terminal 2 — wsdbagent + its own MongoDB:**
```sh
SERVER_HOST=host.containers.internal \
SERVER_PORT=8080 \
NO_SSL=1 \
podman-compose -f docker-compose.agent.yml up --build
```

`host.containers.internal` resolves to the host machine from inside a Podman
container on macOS. On Linux, use the Docker/Podman bridge gateway IP
(typically `172.17.0.1`) or start the agent with `--network host` instead.

Both stacks share the same image builds (CMake/Angular cache), so only changed
source files trigger a recompile.

### Manual run (without Compose)

#### Build the image

```sh
podman build -f docker/Dockerfile.wsdbagent -t wsdbagent .
```

#### Run MongoDB with a persistent volume

```sh
podman run -d --name mongodb \
  -v mongo-data:/data/db \
  -e MONGO_INITDB_ROOT_USERNAME=root \
  -e MONGO_INITDB_ROOT_PASSWORD=changeme \
  mongo:latest
```

#### Run wsdbagent

**Heroku (Heroku-edge TLS, inner TLS with CA verification):**

```sh
podman run -d --name wsdbagent \
  --network host \
  -v /path/to/certs:/certs:ro \
  -e ARGS="--server-host myapp.herokuapp.com \
           --tls-ca /certs/ca.crt \
           --tls-cert /certs/client.crt \
           --tls-key /certs/client.key \
           --tls-hostname marvel.xpmile.com \
           --mongo-db-uri mongodb://root:changeme@localhost:27017 \
           --mongo-db-name xpmile" \
  wsdbagent
```

**Self-hosted with mTLS** — mount the cert directory and pass the TLS flags:

```sh
podman run -d --name wsdbagent \
  --network host \
  -v /path/to/certs:/certs:ro \
  -e ARGS="--server-host myserver.example.com \
           --server-port 8443 \
           --tls-ca   /certs/ca.crt \
           --tls-cert /certs/client.crt \
           --tls-key  /certs/client.key \
           --tls-hostname myserver.example.com \
           --mongo-db-uri mongodb://root:changeme@localhost:27017 \
           --mongo-db-name xpmile" \
  wsdbagent
```

`--network host` lets the agent reach `mongod` on `localhost:27017`. Alternatively
use a shared podman network and reference MongoDB by container name.

### Run uniservice in mTLS mode (self-hosted)

```sh
podman run -d --name uniservice \
  -v /path/to/certs:/certs:ro \
  -p 8080:8080 -p 8443:8443 \
  -e PORT=8080 \
  -e ARGS="--remote-db \
           --agent-port 8443 \
           --tls-cert /certs/server.crt \
           --tls-key  /certs/server.key \
           --tls-ca   /certs/ca.crt \
           --mongo-db-name xpmile \
           --server-worker 5" \
  xpmile
```

Port `8080` serves browser traffic; port `8443` is the dedicated mTLS agent port.

---

## Sequence diagram — inner TLS connect

The agent initiates the inner TLS handshake after the WebSocket upgrade.
Inner TLS runs *inside* WebSocket frames — the agent is the TLS client, the
server is the TLS acceptor.

```
wsdbagent (InnerTlsClient)              WsDbServer (InnerTlsServer)
    │                                          │
    │  (outer) TCP connect + TLS + WS upgrade  │  (ACE_SSL_SOCK_Acceptor or Heroku)
    │─────────────────────────────────────────►│
    │◄── HTTP/1.1 101 ─────────────────────────│
    │                                          │
    │  ═══ inner TLS handshake (agent initiates) ═══
    │  SSL_connect()                           │
    │  ── ClientHello (over WS frame) ────────►│  SSL_accept()
    │◄── ServerHello + server.crt ─────────────│
    │  verify server.crt against ca.crt        │
    │  ── client.crt (if loaded) ────────────►│  verify against ca.crt
    │                                          │  (SSL_VERIFY_PEER)
    │  ═══ inner TLS established ═══════════════
    │                                          │
    │◄── binary frame (DbRequest, encrypted) ──│
    │─── binary frame (DbResponse, encrypted)─►│
```
