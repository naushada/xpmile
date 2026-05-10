# Design: Password Hashing & Secure Agent Communication

## Problem summary

Three security issues exist in the current system:

| # | Issue | Severity |
|---|-------|----------|
| 1 | Passwords stored as plain text in MongoDB (`loginCredentials.accountPassword`) | Critical |
| 2 | Login sends password as a GET query parameter (appears in URL, logs, browser history) | Critical |
| 3 | wsdbagent→Heroku→app traffic is decrypted inside Heroku's network after SSL termination | High |

---

## 1. Password hashing

### Algorithm: PBKDF2-HMAC-SHA256

Use OpenSSL's `PKCS5_PBKDF2_HMAC` (already linked). No new dependency needed.

**Why PBKDF2 over bcrypt/scrypt:**
- OpenSSL ships it — zero new dependencies
- Configurable iterations (start at 600k, tune later)
- NIST-approved, well-understood

**Stored format** (modular crypt style, stored in a new field `passwordHash`):

```
$pbkdf2-sha256$i=600000$<base64-salt>$<base64-hash>
```

### Schema change

Each account document gains one field and eventually drops another:

```json
{
  "loginCredentials": {
    "accountCode": "admin"
    // "accountPassword" removed — only passwordHash is stored
  },
  "passwordHash": "$pbkdf2-sha256$i=600000$<salt>$<hash>"
}
```

### New login flow

```
Client                     Server                        MongoDB
  │                          │                              │
  │  POST /api/v1/account/   │                              │
  │  login                    │                              │
  │  {userId, password}      │                              │
  │ ─────────────────────────→                              │
  │                          │  findOne({                   │
  │                          │    loginCredentials          │
  │                          │    .accountCode: userId})    │
  │                          │ ─────────────────────────────→
  │                          │  ← account doc (with hash)    │
  │                          │                              │
  │                          │  PBKDF2(password, salt)      │
  │                          │  == stored hash?             │
  │                          │                              │
  │  ← 200 + account data    │                              │
  │  ← 401 Unauthorized      │                              │
```

Key changes:
- Login moves from `GET /api/v1/account/account?userId=&password=` to `POST /api/v1/account/login` with JSON body
- Server fetches account by `accountCode` only (no longer matches on password)
- Server compares hash server-side
- Failed login returns 401 (not 400 with "Invalid Credentials")

### Account creation (POST /api/v1/account/account)

Hash the `loginCredentials.accountPassword` field before insert, store result in `passwordHash`. Do not store plain text.

### Account update (PUT /api/v1/account/account)

If the update body contains a new `loginCredentials.accountPassword`, hash it and write to `passwordHash`.

### Migration strategy

**Phase 1 — Dual-read (deploy):**
1. Start writing `passwordHash` on CREATE and password-change PUT
2. On login: if `passwordHash` exists, verify against it; otherwise fall back to plain-text `accountPassword`
3. Log a warning on every plain-text fallback so we know who hasn't been migrated

**Phase 2 — Backfill (one-time script or startup hook):**
- Iterate all account documents
- For each doc where `passwordHash` is missing but `accountPassword` exists: hash `accountPassword` → write `passwordHash`
- Can be a standalone binary or a `--migrate-passwords` flag on uniservice

**Phase 3 — Enforce (subsequent deploy):**
- Drop the plain-text fallback path
- Run `$unset: { "loginCredentials.accountPassword": "" }` across the collection
- Remove `accountPassword` from seed data (`mongo-init.js`)

### Files changed

| File | Change |
|------|--------|
| `modules/module/webservice/src/webservice.cpp` | Rewrite `handle_account_GET` login path; add `handle_account_login_POST`; hash on create/update |
| `modules/module/webservice/inc/webservice.hpp` | Declare `handle_account_login_POST` |
| `modules/module/mongodb/inc/mongodbc.hpp` | Add `hash_password()` and `verify_password()` static helpers |
| `modules/module/mongodb/src/mongodbc.cpp` | Implement PBKDF2 helpers |
| `docker/mongo-init.js` | Hash the bootstrap admin password |
| `ui/src/common/app-globals.ts` | Add `from_web_login` → `/api/v1/account/login` URI mapping |
| `ui/src/common/httpsvc.service.ts` | Change `getAccountInfo` from GET with query params to POST with JSON body |
| `ui/src/app/login/login.component.ts` | Update to call new login endpoint, remove dead code |

---

## 2. SSL/mTLS always-on between wsdbagent and wsdbproxy

### Current state

- `WsDbAgent` has a `--no-ssl` flag that disables TLS entirely
- `WsDbServer` in Heroku mode accepts plain WebSocket upgrades (no TLS at all)
- Self-hosted mTLS mode exists but is optional

### Change

**Remove the plain-text path completely:**

1. **wsdbagent**: Remove `--no-ssl`. TLS is always on. mTLS (client cert) is required in production.
2. **WsDbServer (Heroku mode)**: After WebSocket upgrade, require the agent to present a client certificate via a second TLS handshake inside the WebSocket channel (see §3).
3. **WsDbServer (self-hosted mTLS mode)**: Already correct — no change needed.

| File | Change |
|------|--------|
| `modules/module/wsdbagent/src/wsdbagent_main.cpp` | Remove `--no-ssl` flag; make mTLS args required |
| `modules/module/wsdbagent/src/wsdbagent.cpp` | Remove `m_ssl` branching; always use SSL stream |

---

## 3. End-to-end encryption through Heroku

### The problem

```
wsdbagent ──[TLS]──→ Heroku Router ──[plain HTTP]──→ App container (WebConnection → WsDbServer)
                         │
                    TLS terminates here.
                    Data is plaintext from here to the app.
```

Heroku's routing mesh does not support TLS pass-through to the container. The WebSocket upgrade request arrives at `WebConnection` as plain HTTP with `X-Forwarded-Proto: https` set.

### Option A: TLS over WebSocket (recommended)

After the WebSocket upgrade completes, the wsdbagent and WsDbServer perform an **additional TLS handshake** whose bytes travel as WebSocket binary frames. This creates an encrypted tunnel inside the WebSocket channel that Heroku cannot inspect.

```
wsdbagent                          Heroku Router        WsDbServer
  │                                    │                    │
  │ ──── WSS connect ────────────────→ │                    │
  │                                    │ ── plain HTTP ──→  │
  │ ←─── 101 Upgrade ──────────────────│←─────────────────── │
  │                                    │                    │
  │ === TLS-over-WS handshake ======== ║ === opaque ====== →│
  │    (ClientHello in WS frame)       ║                    │
  │ ←==== ServerHello in WS frame ==== ║ ================== │
  │ === encrypted DB traffic ========= ║ ================== │
```

**Implementation** (see `modules/module/security/` and `modules/module/wsdbproxy/`):
- `WebSocketTransport` (`wstransport.hpp`) adapts WebSocket frame send/recv to the `ITransport` interface, transparently handling ping/pong
- After WebSocket upgrade, `WsDbAgent::setup_inner_tls()` creates an `InnerTlsClient` wrapping the transport and calls `handshake()`
- After agent connects, `WsDbServer::setup_inner_tls()` creates an `InnerTlsServer` wrapping the transport and calls `accept()`
- Both use OpenSSL memory BIOs (`BIO_s_mem()`) layered over `ITransport`
- `InnerTlsClient`/`InnerTlsServer` hold `std::unique_ptr<SSL_CTX>` and `std::unique_ptr<SSL>` with custom deleters — no raw owning pointers
- Once the inner TLS handshake completes (`m_innerTlsReady` flag set), all subsequent DB traffic is encrypted with this inner session
- `dispatch()` (called from MicroService threads) encrypts requests via `InnerTlsServer::send()`; `run_session()` decrypts responses via `InnerTlsServer::recv()`
- The inner TLS context reuses the same mTLS certificates already in `certs/`
- In Heroku mode, `--tls-cert` and `--tls-key` are required when `--remote-db` is set

**Advantages:**
- No new infrastructure (no VPN server, no extra ports)
- Works through Heroku's routing mesh unchanged
- Reuses existing mTLS certificates
- About 200 lines of code
- Standard TLS — auditable, well-understood

**Disadvantages:**
- Adds one RTT during connection setup (TLS handshake inside WS)
- Slightly higher CPU (double encryption at the TLS layer, but WebSocket frames are small)

### Option B: OpenVPN tunnel

Run an OpenVPN server inside (or alongside) the Heroku app container. The wsdbagent connects to the VPN first, then communicates with the app over the VPN's private network.

```
wsdbagent ──[OpenVPN]──→ VPN Server (Heroku container, port 1194)
                              │
                              ├── tun0: 10.8.0.0/24
                              │
wsdbagent (10.8.0.2) ─────────┘
  │
  │  WSS to 10.8.0.1:8080 (through VPN tunnel, end-to-end encrypted)
  └──────────────────────────────────────────→ App (10.8.0.1)
```

**Advantages:**
- Well-known technology
- Entirely separate from the application — no code changes to wsdbproxy/wsdbagent for encryption
- Can protect other traffic if needed later

**Disadvantages:**
- Heroku containers have an **ephemeral filesystem** — OpenVPN config must be baked into the Docker image or set via env vars
- Heroku **only exposes HTTP(S)** on its router. OpenVPN would need to run on a separate port, which Heroku does not expose. Workaround: run OpenVPN over WebSocket (tunnel TCP over WS), which brings us back to Option A complexity.
- Must manage OpenVPN PKI separately from the existing mTLS certs
- More moving parts to debug: VPN link state, routing, MTU, keepalive
- Adds ~50 MB to the image (OpenVPN + easy-rsa)

**Verdict:** OpenVPN on Heroku is impractical because Heroku only routes HTTP/HTTPS. Running OpenVPN over WebSocket reduces to Option A with extra overhead. **Option A is the clear choice.**

### Recommendation

**Implement Option A (TLS over WebSocket).** It solves the end-to-end encryption problem with minimal code, no new infrastructure, and reuses the existing certificate bundle.

---

### MITM attack analysis

The central question: can an attacker who controls Heroku's internal network (or any hop between Heroku's router and the app container) read or modify the DB traffic?

#### Data path after Phase 8

```
wsdbagent (MongoDB LAN)              Heroku Router         WsDbServer (app container)
  │                                      │                      │
  │── Outer TLS ClientHello ────────────→│                      │
  │←── Outer TLS ServerHello (Heroku) ──│                      │   ← Heroku's cert
  │══ Outer TLS encrypted ═════════════→│                      │
  │                                      │── plain HTTP GET ──→│   ← Heroku decrypts
  │                                      │   /ws/db            │
  │                                      │   Upgrade: websocket│
  │                                      │←── 101 Switching ───│
  │←══ Outer TLS encrypted 101 ════════─│                      │
  │                                                                  ═══ WebSocket established ═══
  │══ WS frame: Inner TLS ClientHello ════════════════════════════→│   ← opaque bytes
  │                                      │   (unreadable)      │       to Heroku
  │←══ WS frame: Inner TLS ServerHello ═══════════════════════════│
  │←══ WS frame: Server cert ════════════════════════════════════│
  │══ WS frame: Client cert ════════════════════════════════════→│   ← mTLS
  │                                                                  ═══ Inner TLS established ═══
  │══ WS frame: inner-TLS-encrypted DB query ════════════════════→│   ← E2E encrypted
  │←══ WS frame: inner-TLS-encrypted DB rsp ═════════════════════│
```

Two layers of TLS, each with a different purpose:

| Layer | Endpoints | Certificates | What it protects against |
|-------|-----------|-------------|--------------------------|
| Outer TLS | wsdbagent ↔ Heroku router | Heroku's cert (public CA) | Eavesdropping on the public internet |
| Inner TLS (mTLS) | wsdbagent ↔ wsdbproxy | Private CA (`certs/ca.crt`) | Eavesdropping/modification inside Heroku's network |

#### Attack scenario 1: Passive eavesdropper on Heroku's network

An attacker captures packets between Heroku's router and the app container.

**What they see:**
- Plain HTTP headers (GET /ws/db, 101 Switching Protocols)
- WebSocket frames containing TLS records from the inner TLS session

**Can they read DB queries?** No. The inner TLS records are encrypted with ephemeral session keys established during the inner TLS handshake. The attacker sees only ciphertext.

**Can they correlate timing/size?** Yes — this is inherent to any tunnel. They can see when requests happen and their approximate size. TLS record padding can mitigate size correlation if needed.

**Verdict: Safe.** Passive eavesdropping is fully defeated.

#### Attack scenario 2: Active MITM on Heroku's network — strip the inner TLS

An attacker intercepts the WebSocket frames and removes or blocks the inner TLS handshake, hoping both sides fall back to plain text.

**What they can try:**
- Drop the inner TLS ClientHello frame → wsdbproxy never sees it
- Send fake frames pretending inner TLS is not supported

**Why it fails:**
- Both sides REQUIRE inner TLS before any DB traffic is sent
- `WsDbServer::run_session()` calls `inner_tls_accept()` immediately after WebSocket upgrade. If the first frame is not a valid TLS ClientHello, the connection is dropped.
- `WsDbAgent::connect_and_handshake()` calls `inner_tls_handshake()` immediately after upgrade. If it doesn't complete, the session exits and the agent reconnects.
- There is no downgrade path — no code path exists to send DB traffic without inner TLS.

**Verdict: Safe.** Downgrade is prevented by implementation (no code path for unencrypted DB traffic).

#### Attack scenario 3: Active MITM — impersonate the server

An attacker on Heroku's network intercepts the inner TLS ClientHello and tries to complete the handshake themselves, impersonating wsdbproxy.

**What they need:**
- A valid server certificate signed by the private CA (`ca.key`)
- The server's private key (`server.key`)

**Why it fails:**
- `ca.key` is stored only on the machine that ran `certs/generate.sh` — it is never deployed to Heroku, never committed to git (`.gitignore` excludes `*.key`), and never placed on the MongoDB machine
- Without `ca.key`, the attacker cannot sign a certificate that wsdbagent will trust
- wsdbagent calls `SSL_CTX_set_verify(ctx->context(), SSL_VERIFY_PEER, nullptr)` — it rejects any server certificate not signed by the trusted CA
- Even if the attacker stole `server.key` from Heroku (the only place it exists in deployment), the mTLS check on the client certificate (see scenario 4) still prevents impersonation because the attacker must also present a valid client cert

**Verdict: Safe.** Server impersonation requires the CA private key, which is offline.

#### Attack scenario 4: Active MITM — impersonate the client

An attacker on Heroku's network intercepts the inner TLS ServerHello and tries to complete the handshake as a fake wsdbagent, hoping to send malicious DB queries.

**What they need:**
- A valid client certificate signed by the private CA
- The client's private key (`client.key`)

**Why it fails:**
- `client.key` is stored only on the MongoDB machine (with wsdbagent) — never on Heroku, never committed
- WsDbServer sets `SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT` — it rejects any connection that doesn't present a valid client cert
- Without `ca.key`, the attacker cannot create a client cert that wsdbproxy will accept

**Verdict: Safe.** Client impersonation requires the client private key (on the MongoDB machine) or the CA private key (offline).

#### Attack scenario 5: Compromised Heroku — full control

Heroku (or an attacker with root on Heroku's infrastructure) has full access to the app container, its filesystem, and its memory.

**What they have:**
- `server.key` (on disk in the container)
- `ca.crt` (public — committed, on disk)
- Access to process memory (could extract session keys)

**What they still don't have:**
- `client.key` — only on the MongoDB machine
- `ca.key` — offline

**Can they decrypt inner TLS traffic?** If they dump process memory, yes — they can extract the ephemeral session keys for that connection. This is true of any TLS endpoint: if the host is compromised, TLS doesn't help.

**Mitigation:** This is the same threat as any server compromise. Standard hardening applies: minimal image, no shell access, regular rotation of `server.key`, monitoring for unexpected process behavior.

**Verdict: Equivalent risk to any TLS-terminating server.** The inner TLS protects against network-level attacks, not host compromise.

#### Attack scenario 6: Replay attack

An attacker captures inner TLS frames and replays them later.

**Why it fails:**
- TLS handshake includes random nonces from both sides (ClientHello.random, ServerHello.random)
- Session keys are derived from these nonces plus the premaster secret
- Replaying a ClientHello results in a different ServerHello (new server random), so the session keys differ
- Replaying encrypted application data frames fails because the session keys from the capture don't match the current session
- TCP sequence numbers also prevent replay at the transport layer

**Verdict: Safe.** Standard TLS anti-replay properties apply.

#### Attack scenario 7: Key compromise — what if server.key is leaked?

If `server.key` is leaked (e.g., from a Heroku config var or a compromised container):

- The attacker can decrypt the outer TLS (but that's Heroku's key, separate concern)
- The attacker can decrypt the **inner TLS server-to-client** direction IF they also captured the traffic AND can derive the session keys (requires the client random from the handshake)
- The attacker still cannot impersonate the client because they don't have `client.key`

**Mitigation:**
- Rotate certificates regularly using `certs/generate.sh`
- Keep `server.key` in a Heroku config var (not in the image) — already the current practice via `ARGS` env var
- Monitor for unexpected wsdbagent disconnects (could indicate MITM attempt)

#### Attack scenario 8: First-contact key compromise

When wsdbagent first connects, it must trust the Heroku router's certificate (outer TLS). An attacker who controls a trusted public CA could issue a fake certificate for `myapp.herokuapp.com` and MITM the outer TLS.

**Why this is mitigated:**
- Even if the outer TLS is compromised, the inner TLS handshake proceeds independently
- The attacker would need to also compromise the inner TLS (scenarios 3 and 4)
- The outer TLS and inner TLS use completely separate PKIs — compromising one doesn't affect the other

**Verdict: Safe.** Defense in depth — the inner TLS is independent of the outer TLS.

#### Summary

| Attack | Blocked by | Risk |
|--------|-----------|------|
| Passive eavesdropping (Heroku network) | Inner TLS encryption | None |
| Active MITM — strip inner TLS | Code enforcement (no downgrade path) | None |
| Active MITM — impersonate server | CA-signed server cert + client verifies | None |
| Active MITM — impersonate client | CA-signed client cert + server verifies (mTLS) | None |
| Heroku host compromise | N/A (host owns the endpoint) | Same as any TLS server |
| Replay attack | TLS nonces, ephemeral session keys | None |
| Leaked server.key | Roll certs; can't impersonate client | Low |
| Compromised public CA (outer TLS) | Inner TLS is independent PKI | None |
| Leaked CA private key | Offline storage; re-generate all certs | Low (if CA key stays offline) |

#### Hard requirements for MITM resistance

These must be enforced in implementation:

1. **Inner TLS is not optional.** Both sides exit the session (and the agent reconnects) if the inner TLS handshake fails. No DB traffic flows without it.
2. **mTLS on the inner TLS.** `SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT` on both sides.
3. **CA private key stays offline.** It is only used during `certs/generate.sh`. Never deployed, never in a config var, never in a Docker image.
4. **No code path exists for unencrypted DB traffic after inner TLS is implemented.** The unencrypted `ws_send`/`ws_recv_frame` methods become private or are removed from the DB data path.

---

## 4. Implementation plan

### Step 1: Password hashing infrastructure
- Add `hash_password()` and `verify_password()` to `MongodbClient`
- Unit tests for both

### Step 2: New login endpoint
- Add `POST /api/v1/account/login` handler
- Fetch by accountCode, verify hash, return account data or 401
- Keep old GET path working (with plain-text fallback) during migration

### Step 3: Hash on write
- `handle_account_POST`: hash password before insert
- `handle_account_PUT`: hash password if present in update body

### Step 4: Angular changes
- Change `getAccountInfo()` to POST to `/api/v1/account/login` with JSON body
- Remove password from URL

### Step 5: Seed data
- Update `mongo-init.js` to store `passwordHash` instead of `accountPassword`

### Step 6: Migration tool
- Add `--migrate-passwords` flag to uniservice
- On startup, backfill all plain-text passwords to hashes

### Step 7: Enforce SSL on agent link ✅ (complete)
- Remove `--no-ssl` from wsdbagent
- Make TLS required

### Step 8: TLS-over-WebSocket ✅ (complete)
- `WebSocketTransport` adapts WebSocket frame I/O to `ITransport` interface
- `WsDbAgent::setup_inner_tls()` creates `InnerTlsClient` after WebSocket upgrade
- `WsDbServer::setup_inner_tls()` creates `InnerTlsServer` after agent connects (both Heroku and mTLS modes)
- `dispatch()` encrypts requests via inner TLS; `run_session()` decrypts responses
- All DB frames encrypted with the inner session

### Step 9: Remove plain-text fallback ✅ (complete)
- Drop `accountPassword` field from all documents
- Remove fallback comparison from login handler
- Remove `accountPassword` from mongo-init.js

---

## 5. Files summary

| File | Action |
|------|--------|
| `modules/module/mongodb/inc/mongodbc.hpp` | Add `hash_password`, `verify_password` |
| `modules/module/mongodb/src/mongodbc.cpp` | PBKDF2 implementation |
| `modules/module/webservice/inc/webservice.hpp` | Declare `handle_account_login_POST` |
| `modules/module/webservice/src/webservice.cpp` | New login handler; hash on create/update; dual-read fallback |
| `modules/module/webservice/test/webservice_test.cc` | Tests for hash/verify |
| `modules/module/security/inc/innertls.hpp` | ITransport, InnerTlsClient, InnerTlsServer (smart pointers) |
| `modules/module/security/src/innertls.cpp` | OpenSSL memory BIO TLS implementation |
| `modules/module/security/inc/wstransport.hpp` | WebSocketTransport adapter — ITransport over WS frames |
| `modules/module/security/test/innertls_test.cc` | 7 tests with MockTransport double |
| `modules/module/wsdbagent/inc/wsdbagent.hpp` | Add InnerTlsClient member, setup_inner_tls() declaration |
| `modules/module/wsdbagent/src/wsdbagent.cpp` | Inner TLS handshake after WS upgrade; run_session over inner TLS |
| `modules/module/wsdbagent/src/wsdbagent_main.cpp` | Remove `--no-ssl` |
| `modules/module/wsdbproxy/inc/wsdbproxy.hpp` | Inner TLS cert/key/ca params, InnerTlsServer member, ready flag |
| `modules/module/wsdbproxy/src/wsdbproxy.cpp` | Inner TLS accept after agent connect; dispatch/recv over inner TLS |
| `modules/module/webservice/src/webservice_main.cpp` | Pass TLS certs to WsDbServer in Heroku mode; require certs for --remote-db |
| `CMakeLists.txt` | Already links security/innertls to uniservice and wsdbagent targets |
| `docker/mongo-init.js` | Hash bootstrap password |
| `ui/src/common/httpsvc.service.ts` | POST-based login |
| `ui/src/app/login/login.component.ts` | Update login call |
| `certs/` | No change — existing certs reused for inner TLS |
