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
└── generate.sh             OpenSSL script — generates CA, server, client certs
docker/
└── Dockerfile.wsdbagent   Standalone container image
```

---

## Certificates

### Generate

```sh
cd certs && ./generate.sh
```

Produces:

| File | Used by | Keep secret? |
|------|---------|--------------|
| `ca.crt` | both sides | no — distribute freely |
| `server.crt` | uniservice | no |
| `server.key` | uniservice | **yes** |
| `client.crt` | wsdbagent | no |
| `client.key` | wsdbagent | **yes** |

Certs are valid for 10 years. Re-run `generate.sh` to rotate — both sides must be
updated together.

### How mTLS is enforced

**Server (`WsDbServer::open()`):**
```
ACE_SSL_Context → SSLv23_server mode
  certificate()    ← server.crt
  private_key()    ← server.key
  load_trusted_ca()← ca.crt
SSL_CTX_set_verify → SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT
```
Any agent connecting without a CA-signed client cert is rejected at the TLS handshake.

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
  --server-port  <n>       Port (default: 443 with SSL, 8080 without)
  --no-ssl                 Use plain TCP instead of TLS
  --tls-ca   <path>        CA certificate — enables server cert verification
  --tls-cert <path>        Client certificate (mTLS)
  --tls-key  <path>        Client private key  (mTLS)
  --mongo-db-connection-pool <n>  Pool size (default: 10)
  --backoff  <secs>        Reconnect wait in seconds (default: 5)
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
2. On success → enter `run_session()`.
3. On failure (including a `503` stale-eviction response) or session end → sleep `backoff_secs` seconds → retry.
4. Exit only when `stop()` is called.

**503 during reconnect:** If the agent reconnects while the server is still tearing down the previous stale session, the server replies `503 Retry-After: 2`. The agent treats this as a connect failure and retries after `backoff_secs`. Within one or two cycles the server's `run_session()` will have exited and the reconnect succeeds.

### Handshake (`connect_and_handshake`)

1. Configure `ACE_SSL_Context` with CA / client cert / key (if provided).
2. Open a TLS (or plain TCP) connection to `--server-host:--server-port`.
3. Send a standard RFC 6455 upgrade request to `/ws/db`.
4. Read the HTTP response headers until `\r\n\r\n`.
5. Verify status `101` and `Sec-WebSocket-Accept`.
6. Return `true` to enter the session loop.

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
| `BACKOFF` | `5` | Reconnect wait in seconds |

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

**Heroku (Heroku-edge TLS, no mTLS):**

```sh
podman run -d --name wsdbagent \
  --network host \
  -e ARGS="--server-host myapp.herokuapp.com \
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

## Sequence diagram — mTLS connect

```
wsdbagent                        WsDbServer (self-hosted)
    │                                     │
    │  TCP connect to :8443               │
    │────────────────────────────────────►│
    │◄── TLS ServerHello + server.crt ────│
    │  verify server.crt against ca.crt   │
    │  send client.crt                   ►│
    │                                     │  verify client.crt against ca.crt
    │                                     │  (SSL_VERIFY_FAIL_IF_NO_PEER_CERT)
    │       TLS handshake complete        │
    │                                     │
    │  GET /ws/db HTTP/1.1               ►│  ws_upgrade_server()
    │◄── HTTP/1.1 101 ────────────────────│
    │                                     │
    │      (mTLS WebSocket session)       │
    │◄── binary frame (DbRequest) ────────│
    │─── binary frame (DbResponse) ──────►│
```
