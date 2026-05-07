# Design: VPN DB Proxy — Remote MongoDB via OpenVPN Tunnel

## Problem

MongoDB is deployed on a machine behind NAT (not directly roachable from the internet).
The `uniservice` container runs on a machine with a public IP. A direct `mongodb://` URI
from the container to the MongoDB host is therefore impossible.

## Solution overview

Establish an OpenVPN tunnel between the two machines. Run a lightweight TCP proxy
protocol over the tunnel so `MicroService` workers can issue database calls that are
transparently forwarded to the remote MongoDB.

---

## Topology

```
┌──────────────────────────────────────────────────┐
│  Container (public IP)                           │
│                                                  │
│  ┌─────────────────────────────────────────┐     │
│  │  uniservice                             │     │
│  │  ├─ WebServer / MicroService            │     │
│  │  └─ VpnDbServer  (ACE_Task)             │     │
│  │       listens on 10.8.0.1:9000 (tun0)  │     │
│  └─────────────────────────────────────────┘     │
│                                                  │
│  openvpnd  (server mode)                         │
│  tun0 = 10.8.0.1                                 │
│  listens on 0.0.0.0:1194/UDP  ◄──────────────────┼── exposed via docker-compose
└──────────────────────────────────────────────────┘
                     │  OpenVPN tunnel (TLS over UDP)
                     ▼
┌──────────────────────────────────────────────────┐
│  MongoDB Machine (behind NAT)                    │
│                                                  │
│  openvpnd  (client mode)                         │
│  tun0 = 10.8.0.2  ── initiates to container:1194 │
│                                                  │
│  vpn-db-agent  (new binary, implemented later)   │
│  ├─ connects to 10.8.0.1:9000 on tunnel up       │
│  └─ MongodbClient → mongod (localhost:27017)     │
└──────────────────────────────────────────────────┘
```

### Connection lifecycle

1. OpenVPN client on the MongoDB machine dials the container's public IP on port 1194/UDP.
2. TLS handshake → tunnel established; both ends get tun0 IPs (10.8.0.1 / 10.8.0.2).
3. `vpn-db-agent` (our client binary) connects to `VpnDbServer` at **10.8.0.1:9000** (TCP over tun0).
4. This single TCP connection is kept alive. `VpnDbServer` dispatches DB requests on it;
   `vpn-db-agent` executes them against local MongoDB and sends responses back.

### Why client → server for the DB control connection

The MongoDB machine is behind NAT. Even with the VPN tunnel up, initiating *outbound* from
the NAT side is reliable; the tun interface gives the client a routable IP but keeping the
application-level connection direction as client → server avoids requiring any additional
firewall rules on the server container side.

---

## Container changes (docker-compose.yml / Dockerfile)

To host OpenVPN inside the container, the `app` service needs:

```yaml
cap_add:
  - NET_ADMIN        # create/manage tun device
devices:
  - /dev/net/tun     # tun character device
ports:
  - "1194:1194/udp"  # OpenVPN
  - "8080:8080"      # existing HTTP
```

OpenVPN server config (`docker/openvpn-server.conf`) and PKI (CA cert, server cert/key,
DH params, ta.key) are baked into the image or mounted as a volume. The VPN subnet
(10.8.0.0/24), tun device name, and cipher suite are fixed in that config file.

---

## C++ design — server side (this phase)

### 1. `IMongodbClient` — abstract interface

Extracted from the current `MongodbClient` public API. Both the existing local client and
the new remote proxy implement this interface.

**Location:** `modules/module/mongodb/inc/mongodbc.hpp`

```cpp
class IMongodbClient {
public:
  virtual ~IMongodbClient() = default;

  virtual std::string create_document(const std::string &db,
                                      const std::string &coll,
                                      const std::string &json) = 0;

  virtual std::int32_t create_bulk_document(const std::string &db,
                                            const std::string &coll,
                                            const std::string &json) = 0;

  virtual bool update_collection(const std::string &coll,
                                 const std::string &filter,
                                 const std::string &update) = 0;

  virtual std::int32_t update_bulk_document(const std::string &coll,
                                            const std::string &filters,
                                            const std::string &values) = 0;

  virtual bool delete_document(const std::string &coll,
                               const std::string &filter) = 0;

  virtual std::string get_document(const std::string &coll,
                                   const std::string &query,
                                   const std::string &projection) = 0;

  virtual std::string get_documents(const std::string &coll,
                                    const std::string &query,
                                    const std::string &projection) = 0;

  virtual std::string get_documents(const std::string &coll,
                                    const std::string &projection) = 0;

  virtual std::string next_awbno(const std::string &prefix) = 0;

  virtual std::string store_file(const std::string &name,
                                 const std::string &mime,
                                 const std::string &bytes) = 0;

  virtual std::string fetch_file(const std::string &name) = 0;
  virtual std::string fetch_file_by_id(const std::string &oid) = 0;
  virtual bool        delete_file(const std::string &oid) = 0;

  virtual std::string get_database() = 0;
};
```

`MongodbClient` inherits from `IMongodbClient` (all existing methods become `override`).

### 2. Propagate `IMongodbClient*` through the stack

| Location | Change |
|---|---|
| `WebServer::mMongodbc` | `unique_ptr<MongodbClient>` → `unique_ptr<IMongodbClient>` |
| `WebServer::mongodbcInst()` | Returns `IMongodbClient*` |
| `WorkCtx::db` | `MongodbClient*` → `IMongodbClient*` |
| `MicroService::process_request()` | Argument type `MongodbClient&` → `IMongodbClient&` |

No changes to call sites inside the handlers — they call the same method names.

### 3. Wire protocol — BSON envelope

All communication between `VpnDbServer` and `vpn-db-agent` uses length-prefixed BSON
documents. BSON's first 4 bytes are the document length (little-endian uint32), so the
read loop is: read 4 bytes, read `length - 4` more bytes.

#### Operation codes

```cpp
// modules/module/vpnservice/inc/dbproto.hpp
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

#### Request envelope (BSON document)

| Field | BSON type | Purpose |
|---|---|---|
| `reqid` | int32 | Monotonically incrementing request ID for response correlation |
| `op` | int32 | `DbOp` value |
| `db` | string | Database name |
| `coll` | string | Collection name |
| `doc` | binary | Primary BSON payload (query / filter / document body) |
| `doc2` | binary | Secondary BSON payload (projection / update document) |
| `sval` | string | String parameter (AWB prefix, file name, OID, MIME type) |

Fields not needed for a given op are omitted.

#### Response envelope (BSON document)

| Field | BSON type | Purpose |
|---|---|---|
| `reqid` | int32 | Echoes the request's `reqid` |
| `ok` | bool | `true` on success |
| `sval` | string | String result (OID, AWB number, JSON string) |
| `ival` | int32 | Integer result (inserted/modified count) |
| `bval` | bool | Boolean result (update/delete success) |
| `data` | binary | Binary result (file bytes for fetch operations) |
| `errmsg` | string | Non-empty only when `ok == false` |

#### Op → fields mapping

| Op | Request fields used | Response fields used |
|---|---|---|
| CREATE_DOCUMENT | op, db, coll, doc | ok, sval (OID) |
| CREATE_BULK_DOCUMENT | op, db, coll, doc | ok, ival (count) |
| UPDATE_COLLECTION | op, coll, doc (filter), doc2 (update) | ok, bval |
| UPDATE_BULK_DOCUMENT | op, coll, doc (filters JSON), doc2 (values JSON) | ok, ival |
| DELETE_DOCUMENT | op, coll, doc (filter) | ok, bval |
| GET_DOCUMENT | op, coll, doc (query), doc2 (projection) | ok, sval (JSON) |
| GET_DOCUMENTS_QUERIED | op, coll, doc (query), doc2 (projection) | ok, sval (JSON array) |
| GET_DOCUMENTS_ALL | op, coll, doc2 (projection) | ok, sval (JSON array) |
| NEXT_AWBNO | op, sval (prefix) | ok, sval (AWB string) |
| STORE_FILE | op, sval (name\|mime), data | ok, sval (OID) |
| FETCH_FILE | op, sval (name) | ok, data |
| FETCH_FILE_BY_ID | op, sval (OID) | ok, data |
| DELETE_FILE | op, sval (OID) | ok, bval |

### 4. `VpnDbServer` — ACE_Task in uniservice

**Location:** `modules/module/vpnservice/inc/vpnservice.hpp`,
             `modules/module/vpnservice/src/vpnservice.cpp`

```cpp
class VpnDbServer : public ACE_Task<ACE_MT_SYNCH> {
public:
  VpnDbServer(std::string listen_ip, ACE_UINT16 listen_port);
  int open(void *args = 0) override;
  int svc() override;
  int close(u_long flags = 0) override;

  // Called by VpnMongodbProxy to queue a request and block until response arrives.
  // Thread-safe. Returns the response envelope BSON bytes.
  std::vector<uint8_t> dispatch(const std::vector<uint8_t> &request_bson);

private:
  void run_session(ACE_SOCK_Stream &client_stream);
  bool send_bson(ACE_SOCK_Stream &s, const std::vector<uint8_t> &doc);
  bool recv_bson(ACE_SOCK_Stream &s, std::vector<uint8_t> &doc);

  std::string    m_listenIp;
  ACE_UINT16     m_listenPort;
  ACE_SOCK_Acceptor m_acceptor;

  // Pending request queue: reqid → (bson bytes, condition_variable, response slot)
  std::mutex     m_mu;
  std::map<std::int32_t, PendingRequest> m_pending;
  std::atomic<std::int32_t> m_nextReqId{0};
  bool           m_clientConnected{false};
};
```

**Lifecycle:**
1. `open()` — binds and listens on `listen_ip:listen_port`.
2. `svc()` loop — calls `accept()` (blocking). When `vpn-db-agent` connects, calls
   `run_session()`.
3. `run_session()` — reads responses from the connected client stream and wakes the
   corresponding waiting `dispatch()` caller via its condition variable.
   Concurrently, `dispatch()` callers send their requests directly onto the stream.
4. When the client disconnects, `run_session()` returns and `svc()` goes back to
   `accept()` (waits for reconnect).

**`dispatch()` — called by `VpnMongodbProxy` from any MicroService thread:**
1. Assigns a unique `reqid` (atomic increment).
2. Inserts a `PendingRequest` in `m_pending` (mutex-protected).
3. Sends the BSON request on the live client stream (mutex for the write).
4. Blocks on the `PendingRequest`'s condition variable.
5. When `run_session()` reads the matching response (same `reqid`), it stores it and
   notifies the CV.
6. Returns the response BSON to the caller.

### 5. `VpnMongodbProxy` — `IMongodbClient` implementation

**Location:** same `vpnservice.hpp / vpnservice.cpp`

```cpp
class VpnMongodbProxy : public IMongodbClient {
public:
  explicit VpnMongodbProxy(VpnDbServer &server, std::string db_name);

  std::string create_document(const std::string &db,
                              const std::string &coll,
                              const std::string &json) override;
  // ... all other IMongodbClient overrides ...
  std::string get_database() override;

private:
  std::vector<uint8_t> build_request(DbOp op, const std::string &db,
                                     const std::string &coll,
                                     const std::string &doc  = {},
                                     const std::string &doc2 = {},
                                     const std::string &sval = {});

  VpnDbServer &m_server;
  std::string  m_dbName;
};
```

Each method:
1. Calls `build_request()` to serialise a BSON request envelope.
2. Calls `m_server.dispatch(bson)` — blocks until response arrives.
3. Deserialises the response envelope and returns the appropriate value.
4. If `ok == false`, logs the error and returns an empty/false result (matching the
   existing `MongodbClient` error-return conventions).

### 6. `webservice_main.cpp` changes

New CLI flags:

| Flag | Purpose |
|---|---|
| `--remote-db` | Enable VPN proxy mode (no `--mongo-db-uri` needed) |
| `--vpn-listen-ip` | IP to bind `VpnDbServer` on (default: `10.8.0.1`) |
| `--vpn-listen-port` | Port for `VpnDbServer` (default: `9000`) |

Startup logic:

```cpp
if (opt[idx(Arg::REMOTE_DB)] == "1") {
  auto server = std::make_unique<VpnDbServer>(vpn_ip, vpn_port);
  server->open();
  auto proxy  = std::make_unique<VpnMongodbProxy>(*server, db_name);
  inst = WebServer(ip, port, workers, std::move(proxy), std::move(server));
} else {
  auto db = std::make_unique<MongodbClient>(uri, pool, db_name);
  inst = WebServer(ip, port, workers, std::move(db));
}
```

---

## New module layout

```
modules/module/vpnservice/
├── inc/
│   ├── vpnservice.hpp      VpnDbServer, VpnMongodbProxy
│   └── dbproto.hpp         DbOp enum, build_request(), parse_response()
└── src/
    ├── vpnservice.cpp
    └── dbproto.cpp
```

`modules/module/vpnservice` is added to `CMakeLists.txt`'s `uniservice` source glob.

---

## Sequence diagram — single DB call in VPN mode

```
MicroService thread          VpnMongodbProxy       VpnDbServer          vpn-db-agent
      │                            │                    │                     │
      │  get_document(coll,q,p)    │                    │                     │
      │──────────────────────────► │                    │                     │
      │                            │  build_request()   │                     │
      │                            │  dispatch(bson) ──►│                     │
      │                            │                    │  send BSON request ►│
      │                            │                    │                     │  execute MongoDB query
      │                            │                    │◄ send BSON response │
      │                            │◄── return bson ────│                     │
      │                            │  parse_response()  │                     │
      │◄── JSON string ────────────│                    │                     │
```

---

## What is NOT in this phase (client side, designed separately)

- `vpn-db-agent` binary — connects to VpnDbServer, owns a local `MongodbClient`
- `VpnDbAgent` class — reads BSON requests, dispatches to local MongodbClient, writes BSON responses
- OpenVPN PKI setup scripts
- `vpn-db-agent` Dockerfile / docker-compose entry for the MongoDB-side machine

---

## Open questions (to confirm before TDD)

1. Should `VpnDbServer::dispatch()` have a timeout? (What if `vpn-db-agent` disconnects mid-request?)
2. Should `VpnDbServer` reject new requests when no client is connected, or queue them until the client reconnects?
3. Is the default VPN subnet `10.8.0.0/24` acceptable, or should tun IPs be configurable?
4. Does the container image need to embed OpenVPN config + PKI, or will these be mounted as a volume at runtime?
