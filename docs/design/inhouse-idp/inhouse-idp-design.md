# Design: In-house OIDC identity provider

> Status: **Draft for review.** TDD plan follows once approved.

## Problem summary

The v1.0 SSO federates against *external* IdPs — Okta, Entra, partner SAML. That works for customers who already have a corporate IdP, but it has two real costs:

1. **Operational dependency on a third party.** Every login round-trip touches the IdP. If the IdP is degraded, the customer can't sign in.
2. **No good story for customers without an IdP.** They're stuck on the existing password-only login, which doesn't share the cookie/session model SSO uses, lacks self-service password reset, and isn't a path other apps could federate against later.

This design adds an **in-house OIDC identity provider** hosted on its own dedicated Heroku app — `https://idp-63c97365e6ef.herokuapp.com` — built **out of the parts xpmile already has**: the same `uniservice` C++ binary used by marvel (ACE + our `Http` parser + nlohmann/json), the same `wsdbagent`/InnerTLS pattern for on-prem connectivity, and a small new Angular project for the branded login portal. Customer credentials and the JWT signing private key stay behind the on-prem NAT.

The xpmile app (marvel) federates against the in-house IdP using the **existing v1.0 `OidcProvider` client code, unchanged** — the IdP is just one more entry in `sso_config`. Once the user authenticates at the IdP, the v1.0 OIDC code-exchange path mints the existing `xpmile_session` cookie on marvel, and the user can use the xpmile app exactly as today.

## Goals

- Stand up an **OIDC identity provider** on its own Heroku app, reusing the `uniservice` binary — just add the IdP HTTP handlers and deploy with a different Angular dist.
- All user credentials stay on the on-prem MongoDB (the same one marvel already talks to via `wsdbagent`). Cloud never sees a plaintext password.
- The JWT signing **private key never leaves the on-prem side.** Signing happens on-prem via a new `wsdbagent` op (`DbOp::SIGN_JWT`); the cloud only ever holds the public key.
- **Zero client-side code changes** to the marvel SPA. The IdP is configured in `sso_config` like any other OIDC provider; the existing `OidcProvider` handles it.
- **Self-service password reset** via email-token, reusing the existing email module.
- Single registered client (the marvel SPA) for v1; architected so other clients can be added later by registering them, no code change.
- Coexists with the v1.0 federated SSO — Okta/Entra/SAML providers stay as-is; the in-house IdP is an additional `sso_config` entry.

## Non-goals

- Multi-factor authentication (TOTP / WebAuthn) — explicit follow-up.
- Self-service registration — accounts continue to be admin-created via the on-prem Vaadin Accounts view.
- General OAuth2 authorization server for third-party API access — this is an IdP for *sign-in*, not for delegating API access.
- IdP-initiated SSO (only RP-initiated authorization code flow).
- Multi-tenancy at the cloud layer — per-customer Heroku deployment continues; one customer = one marvel app + one idp app + one on-prem MongoDB.

---

## 1. Architecture — two Heroku apps, one on-prem MongoDB

```
                     ┌─────────────────────────────────┐
                     │     on-prem (behind NAT)        │
                     │                                 │
                     │   ┌──────────────────────────┐  │
                     │   │   MongoDB (single)       │  │
                     │   │  account, sessions,      │  │
                     │   │  idp_signing_keys,       │  │
                     │   │  idp_clients, ...        │  │
                     │   └──────┬────────────┬──────┘  │
                     │          │            │         │
                     │   ┌──────┴──────┐ ┌──┴───────┐  │
                     │   │ wsdbagent   │ │wsdbagent │  │
                     │   │  -marvel    │ │  -idp    │  │   ← same binary, two
                     │   │ (existing)  │ │  (new)   │  │     containers, different
                     │   └──────┬──────┘ └──┬───────┘  │     SERVER_HOST
                     └──────────│───────────│──────────┘
                          InnerTLS    InnerTLS
                          /ws/db      /ws/db (incl. SIGN_JWT)
                                │           │
        ┌───────────────────────│───────────│────────────────────┐
        │                       ▼           ▼                    │
        │  marvel-3a78bd953f5f             idp-63c97365e6ef      │
        │  .herokuapp.com                  .herokuapp.com        │
        │                                                        │
        │  uniservice                      uniservice            │
        │   (xpmile API +                   (IdP endpoints +     │
        │    SSO client v1.0)               OIDC discovery,      │
        │  + Angular xpmile SPA            + Angular branded     │
        │                                    login portal)       │
        │                                                        │
        │       ▲                              ▲                 │
        │       │                              │ user authenticates
        │       │                              │ at the portal,
        │       │                              │ /authorize redirects
        │       │                              │ to /api/v1/sso/
        │       │                              │ callback on marvel
        │       │      OIDC federation         │                 │
        │       └──────────────────────────────┘                 │
        │       (marvel's existing OidcProvider client code,     │
        │        configured with                                 │
        │        issuer=https://idp-63c97365e6ef.herokuapp.com   │
        │               /api/v1/idp)                             │
        │                                                        │
        └────────────────────────────────────────────────────────┘
                                ▲
                                │
                            Browser
```

**The same uniservice binary runs on both Heroku apps.** Routes under `/api/v1/sso/*` (the OIDC *client* side, v1.0) are exercised on marvel when a user signs in; routes under `/api/v1/idp/*` (the OIDC *server* side, this design) are exercised on idp. Neither host's *Angular* uses the other host's API surface, so there's no UX confusion. The marvel host can technically respond to `/api/v1/idp/*` URLs too — it would happily return discovery JSON — but with the wrong `issuer` field, so any real OIDC client would reject it (issuer-URL/fetched-URL mismatch is a spec rejection per OIDC Discovery §4.3). Acceptable for v1; tightening this requires Host-header gating.

**The Angular dist differs per host.** The existing `ui/` produces the full xpmile SPA for marvel; a new `ui-idp/` produces a small (login + password reset) Angular app for idp. Same Dockerfile pattern (`docker/Dockerfile.idp` mirrors `docker/Dockerfile` but builds and packages `ui-idp` instead).

**The on-prem stack runs two `wsdbagent` containers**, both reading the same MongoDB. They're the same binary with different `SERVER_HOST` env. The existing `docker-compose.agent.yml` is extended with a `wsdbagent-idp` service (and a matching cert-watcher rule, since each agent has its own client-cert family from its host's CA).

**One InnerTLS CA per Heroku app.** Each uniservice deploy mints a fresh CA at build time (per `docker/Dockerfile`); the corresponding wsdbagent client cert family must match. So the on-prem `./run-agent.sh refresh-certs` is extended to refresh *both* families — one extracted from `docker.io/naushada/xpmile-uniservice:latest` (still used by marvel), one from `docker.io/naushada/xpmile-uniservice-idp:latest` (new image for idp). Or, if both Heroku apps deploy the *same* image — easier — they share one CA and one cert family. (See §11 open question.)

### Database namespaces — `xpmile` and `idp`, account split between them

The same on-prem MongoDB instance hosts **two databases**. The existing `account` collection is **split** along the auth-vs-business boundary:

| Database | Owner | Collections |
|---|---|---|
| `xpmile` (existing) | marvel uniservice | `account` *(business fields only after split — `accountCode`, `awbPrefix`, `eventLocation`, `personalInfo`, ...)*, `shipping`, `inventory`, `sessions` (marvel's), `sso_config`, `counters`, `fs.files`/`fs.chunks`, ... |
| `idp` (new) | idp uniservice | `account` *(auth fields only — `accountCode`, `passwordHash`, `email`, `name`, `role`)*, `idp_signing_keys`, `idp_clients`, `idp_codes`, `idp_access_tokens`, `idp_pending_auth`, `password_resets`, `sessions` (the IdP's own — separate from marvel's) |

**The collections link by `accountCode`.** It's the same field that's already the primary identifier today; nothing references it by Mongo `_id`. Splitting along auth/business means each uniservice talks primarily to its *own* database:

- **idp uniservice → `idp` database only.** `/login` reads `idp.account`; `/password-reset/confirm` writes `idp.account.passwordHash`; the IdP-specific collections (signing keys, codes, etc.) all live in `idp`.
- **marvel uniservice → `xpmile` database for everything except the legacy login fallback.** `handle_shipment_POST` reads `xpmile.account.awbPrefix`; sessions/shipping/inventory all stay in `xpmile`.

**The one narrow exception** — the legacy `POST /api/v1/account/login` endpoint. We deliberately keep this endpoint working as a fallback (Q12 resolution: customer can sign in directly if the IdP is unreachable, or for admin recovery). Since the only password hash now lives in `idp.account`, marvel's `handle_account_login_POST` does a single cross-DB read — `db("idp").collection("account").findOne({accountCode})` — only for this one endpoint. No other marvel code path touches `idp.*`. This is one cross-DB read confined to one endpoint, not a sprinkled-cross-DB architecture.

`docker/mongo-init.js` is extended to create the `idp` database alongside `xpmile`, with the same `MONGO_APP_USER` granted `readWrite` on both. A **one-time migration step** at deploy time copies the auth fields out of every existing `xpmile.account` document into a matching `idp.account` document (keyed by `accountCode`) and then deletes those auth fields from `xpmile.account`. The migration is idempotent (gated by a `schema_version` doc) and runs **sequentially** with the deploy (§12 Q11): IdP code goes out first, then the migration script runs on demand, then everything continues. See §11 Phase pre-A.

---

## 2. The on-prem signing service — new `wsdbagent` op `SIGN_JWT`

The point of the on-prem-signs decision: **the JWT private key never appears on the cloud, in any form.** Not in env vars, not in memory, not in a config var, not on disk. The cloud holds only the public key (for the JWKS endpoint). To produce a signature, the cloud sends the JWS to-be-signed bytes through `wsdbagent`; the on-prem side reads the private key from MongoDB, signs, and returns just the signature.

This op is invoked by the **idp uniservice** at the `/api/v1/idp/token` endpoint. The marvel uniservice never calls it.

### Wire protocol extension

Add one operation to `dbproto.hpp`:

```cpp
enum class DbOp : std::uint8_t {
  // ... existing 13 ops ...
  SIGN_JWT = 14,
};
```

Request shape (BSON):
```json
{
  "op":      "SIGN_JWT",
  "reqid":   42,
  "kid":     "current",                  // or a specific key id
  "alg":     "RS256",
  "to_sign": "<base64url-header>.<base64url-payload>"
}
```

Response:
```json
{
  "reqid":     42,
  "ok":        true,
  "signature": "<base64url-signature>",
  "kid":       "kid-2026-05-23"          // the key actually used (so the cloud can stamp the right kid in the header)
}
```

### Cloud side — `WsMongodbProxy::sign_jwt`

```cpp
struct SignJwtResult {
  bool        ok = false;
  std::string error;
  std::string signature;   // base64url
  std::string kid;
};

class WsMongodbProxy : public IMongodbClient {
public:
  // ... existing methods ...
  SignJwtResult sign_jwt(const std::string &kid_hint,
                         const std::string &alg,
                         const std::string &to_sign);
};
```

One new method on the proxy; reuses the existing BSON encode/decode + WebSocket framing. No new dependency.

### On-prem side — `wsdbagent` dispatcher

`wsdbagent.cpp`'s dispatch switch gets a new case:

```cpp
case DbOp::SIGN_JWT: {
  auto resp = sign_jwt_on_prem(*m_db, req);
  reply(resp);
  break;
}
```

`sign_jwt_on_prem()`:
1. Read `idp.idp_signing_keys` by `kid` (or the active one if `kid == "current"`). The db name comes from the incoming `DbRequest` (set to `"idp"` by the cloud-side caller).
2. Parse the PEM, load private key with `EVP_PKEY` (OpenSSL — already linked into wsdbagent for InnerTLS).
3. SHA-256 the `to_sign` bytes; sign with RSA-PKCS#1 v1.5 (RS256).
4. base64url-encode the signature; return.

No new dependency — OpenSSL is already linked for InnerTLS.

### The `idp.idp_signing_keys` collection

```json
{
  "_id":           "kid-2026-05-23-7f3a",       // also the JWT kid
  "alg":           "RS256",
  "privateKeyPem": "-----BEGIN PRIVATE KEY----- ...",
  "publicKeyPem":  "-----BEGIN PUBLIC KEY----- ...",
  "createdAt":     { "$date": "2026-05-23T..." },
  "notAfter":      { "$date": "2027-05-23T..." },  // when this key stops being eligible for signing (verification continues until token expiry)
  "active":        true                          // exactly one doc has active: true at any time
}
```

- **One active key at a time** for signing. Multiple non-active keys may exist to keep verifying tokens still in flight (their `kid` is in JWKS until `notAfter` + max token TTL).
- **Generated and rotated by the on-prem Vaadin admin** (new view, §6). Cloud never sees or generates private key material.

---

## 3. The OIDC endpoints (idp uniservice)

All under `/api/v1/idp/`, wired into `process_request()` with the existing URI-prefix dispatch pattern (`handle_idp`, alongside the existing `handle_sso`).

| Method | Path | Purpose |
|--------|------|---------|
| `GET`  | `/api/v1/idp/.well-known/openid-configuration` | discovery document — issuer URL, endpoint URLs, supported claims/scopes/algs |
| `GET`  | `/api/v1/idp/jwks` | JSON Web Key Set — public keys (one entry per non-expired row in `idp_signing_keys`) |
| `GET`  | `/api/v1/idp/authorize` | authorization endpoint — validates the request, redirects to the login portal or issues a code |
| `POST` | `/api/v1/idp/login` | credential POST — invoked by the login portal form; on success sets `xpmile_idp_session` and redirects back to `/authorize` |
| `POST` | `/api/v1/idp/token` | token endpoint — exchanges code (+ PKCE verifier) for id_token + access_token. Calls `WsMongodbProxy::sign_jwt`. |
| `GET`  | `/api/v1/idp/userinfo` | (optional) returns user claims for a valid access_token |
| `POST` | `/api/v1/idp/end_session` | RP-initiated logout — revokes the IdP session, deletes `xpmile_idp_session`, redirects to `post_logout_redirect_uri` |
| `POST` | `/api/v1/idp/password-reset/start` | initiate password reset — invoked by `/password-reset`; sends email |
| `POST` | `/api/v1/idp/password-reset/confirm` | confirm password reset — invoked by `/password-reset/confirm`; updates the hashed password |

### Issuer URL

`https://idp-63c97365e6ef.herokuapp.com/api/v1/idp` — the discovery document is at `<issuer>/.well-known/openid-configuration`, which a spec-compliant client will compute by appending. The marvel-side `OidcProvider` will fetch this when it first encounters the in-house IdP in `sso_config`.

### Discovery document

```json
{
  "issuer":                                  "https://idp-63c97365e6ef.herokuapp.com/api/v1/idp",
  "authorization_endpoint":                  "https://idp-.../api/v1/idp/authorize",
  "token_endpoint":                          "https://idp-.../api/v1/idp/token",
  "jwks_uri":                                "https://idp-.../api/v1/idp/jwks",
  "userinfo_endpoint":                       "https://idp-.../api/v1/idp/userinfo",
  "end_session_endpoint":                    "https://idp-.../api/v1/idp/end_session",
  "response_types_supported":                ["code"],
  "subject_types_supported":                 ["public"],
  "id_token_signing_alg_values_supported":   ["RS256"],
  "token_endpoint_auth_methods_supported":   ["client_secret_post"],
  "scopes_supported":                        ["openid", "email", "profile"],
  "claims_supported":                        ["sub", "iss", "aud", "exp", "iat", "nonce", "email", "name", "role"],
  "code_challenge_methods_supported":        ["S256"]
}
```

Static given `publicBaseUrl` (idp host) — built once at startup, hot-reloaded if config changes. Served with a short Cache-Control.

### JWKS endpoint

```json
{
  "keys": [
    {
      "kty": "RSA",
      "use": "sig",
      "alg": "RS256",
      "kid": "kid-2026-05-23-7f3a",
      "n":   "...",         // base64url RSA modulus
      "e":   "AQAB"
    }
  ]
}
```

Cloud-side cache built from `idp_signing_keys` (public-key fields only); hot-reloaded every ~60 s on the same background thread that already polls `sso_config` for v1.0.

### `/authorize` (the main entry from the marvel SPA)

Inputs (query string per OIDC):

`response_type=code` (only value supported) · `client_id=<registered>` · `redirect_uri=<exact-registered>` · `scope=openid [email] [profile]` · `state=<rp-csrf-token>` · `nonce=<rp-replay-defense>` · `code_challenge=<base64url-S256-of-verifier>` · `code_challenge_method=S256`

Behavior:

1. Look up `client_id` in `idp.idp_clients`. Reject if unknown.
2. Validate `redirect_uri` is **exactly** in the client's registered list. (No prefix matching.)
3. Validate `response_type == "code"`, `code_challenge_method == "S256"`, `scope` contains `"openid"`.
4. Check for the `xpmile_idp_session` cookie (resolved against `idp.sessions`).
   - **No / invalid session:** persist the authorization request in an `idp.idp_pending_auth` document keyed by a random `req_token`; set a short-lived `xpmile_idp_pending` cookie with that token; redirect (302) to `/login` (Angular route on idp host).
   - **Valid session:** resolve to the user; go to step 5.
5. Generate an authorization `code` (32 bytes CSPRNG, base64url). Persist in `idp.idp_codes`: `{ _id: code, client_id, user_sub, redirect_uri, nonce, code_challenge, scope, expiresAt: now+30s }`.
6. Redirect (302) to `<redirect_uri>?code=<code>&state=<state>`. Done.

### `/login` (the credential check)

Invoked by the branded portal form (POST). Validates `xpmile_idp_pending` (so we know which authorization request to resume — looked up in `idp.idp_pending_auth`); authenticates against **`idp.account`** by `accountCode` — re-using the same password-hashing comparison that `handle_account_login_POST` uses today, but pointed at the new database location. Creates the `xpmile_idp_session` cookie (HttpOnly, Secure, SameSite=Lax, `Path=/api/v1/idp/`) backed by a row in `idp.sessions`; redirects (302) back to `/api/v1/idp/authorize?...` with the original parameters from the pending-auth document. The user finishes the flow exactly as if they'd had a session to begin with.

#### Flow — federated login end-to-end

The full path from "user clicks Sign in on marvel" to "user is back at marvel with a valid session". Server-side ops are listed under the receiving uniservice; the column on the right shows the on-prem MongoDB writes/reads.

```
  Browser            marvel uniservice           idp uniservice           on-prem MongoDB
                     (xpmile DB)                 (idp DB)

  click "Sign in"
  on /main
  ────────────────►  OidcProvider.begin_login()
                       state/nonce/PKCE
                       persist sso_trans                              ─►  xpmile.sso_transactions
  ◄── 302  idp-.../api/v1/idp/authorize?response_type=code
                                          &client_id=xpmile-spa
                                          &redirect_uri=...
                                          &state=&nonce=
                                          &code_challenge=&code_challenge_method=S256

  GET idp-.../api/v1/idp/authorize?...
  ──────────────────────────────────────►  validate client_id        ─►  idp.idp_clients
                                           validate redirect_uri
                                           no IdP session yet
                                           persist pending_auth      ─►  idp.idp_pending_auth
  ◄── 302  /login + Set-Cookie xpmile_idp_pending=<rt>; HttpOnly; Secure

  GET /login   [branded portal renders]
  ──────────────────────────────────────►  serve index.html (Angular)
  ◄── 200 index.html

  POST /api/v1/idp/login {user, pass}        (xpmile_idp_pending sent)
  ──────────────────────────────────────►  resolve req_token         ─►  idp.idp_pending_auth
                                           lookup account            ─►  idp.account
                                           verify passwordHash
                                           insert IdP session        ─►  idp.sessions
  ◄── 302  /api/v1/idp/authorize?... + Set-Cookie xpmile_idp_session=<sid>; HttpOnly; Secure

  GET /api/v1/idp/authorize?...              (xpmile_idp_session sent)
  ──────────────────────────────────────►  validate session          ─►  idp.sessions
                                           mint authz code           ─►  idp.idp_codes
  ◄── 302  marvel-.../api/v1/sso/callback/xpmile?code=<c>&state=<s>

  GET marvel-.../api/v1/sso/callback/xpmile?code=<c>&state=<s>
  ────────────────►  OidcProvider.handle_callback()
                       atomic claim sso_trans                        ─►  xpmile.sso_transactions
                       POST idp-.../api/v1/idp/token  ╶─╴ see Flow 2 ╶─╴
                       verify id_token vs cached JWKS
                       resolve_account                               ─►  xpmile.account
                       create marvel session                         ─►  xpmile.sessions
  ◄── 302  /main + Set-Cookie xpmile_session=<sid>; HttpOnly; Secure
```

### `/token`

Inputs (form-urlencoded body per OIDC):

`grant_type=authorization_code` · `code=<from /authorize>` · `redirect_uri=<must match what was registered with the code>` · `client_id` + (optional) `client_secret` if the client is confidential · `code_verifier=<PKCE verifier matching the stored challenge>`

Behavior:

1. **Atomically claim** the code from `idp.idp_codes` (a guarded `update_collection` with filter `consumed: {$exists: false}` and `$set: { consumed: true }` — same atomic primitive `next_awbno` uses). Reject on miss / replay / expiry.
2. Validate PKCE: `S256(code_verifier) == stored.code_challenge`.
3. Validate `client_id` (+ `client_secret` for confidential clients).
4. Validate `redirect_uri` matches the value stored with the code.
5. Build the id_token header + payload:
   - header: `{ alg: "RS256", typ: "JWT", kid: <current-kid> }`
   - payload: `{ iss, sub: <accountCode>, aud: <client_id>, exp: <now+1h>, iat: <now>, nonce: <stored>, email, name, role }`
6. `base64url(header) + "." + base64url(payload)` → `to_sign`.
7. `WsMongodbProxy::sign_jwt("current", "RS256", to_sign)` → goes over the **idp** wsdbagent connection, returns `signature` + `kid`.
8. id_token = `to_sign + "." + signature`. Backfill the header `kid` if a different key was used.
9. access_token = random opaque 32 bytes; insert in `idp.idp_access_tokens` with TTL.
10. Respond JSON: `{ access_token, token_type: "Bearer", expires_in: 3600, id_token, scope: <granted> }`.

#### Flow — /token with on-prem SIGN_JWT round-trip

Server-to-server (no browser). This is the inner step of the federated-login flow above.

```
  marvel uniservice         idp uniservice               wsdbagent-idp      on-prem MongoDB

  POST idp-.../api/v1/idp/token
       grant_type=authorization_code
       code=<c>&code_verifier=<v>
       client_id=&client_secret=&redirect_uri=
  ─────────────────────────►  atomic claim code                              ─►  idp.idp_codes
                              (guarded $set consumed=true)
                              validate PKCE: S256(verifier) == stored.challenge
                              validate client_secret + redirect_uri match
                              load active signing kid
                              build header + payload:
                                hdr     = {alg:RS256, typ:JWT, kid:current}
                                payload = {iss, sub, aud, exp, iat, nonce, email, name, role}
                                to_sign = b64u(hdr) + "." + b64u(payload)

                              WsMongodbProxy.sign_jwt(kid="current", alg=RS256, to_sign)
                              ────────────────────────►  load active key      ─►  idp.idp_signing_keys
                                                         SHA256 + RSA-PKCS#1 v1.5
                              ◄────── signature + kid ──

                              id_token     = to_sign + "." + signature
                              access_token = CSPRNG(32)                       ─►  idp.idp_access_tokens

  ◄── 200 { access_token, token_type:"Bearer", expires_in:3600,
            id_token: <hdr>.<payload>.<signature>, scope:"openid email profile" }

  OidcProvider verifies id_token against JWKS (cached from prior GET
  idp-.../api/v1/idp/jwks). On valid: continues into the last block of Flow 1
  to mint the marvel session.
```

### `/userinfo`, `/end_session`

`/userinfo`: validates `Authorization: Bearer <access_token>` against `idp.idp_access_tokens`; returns the user's claims as JSON.

`/end_session`: validates the optional `id_token_hint`; deletes the `xpmile_idp_session` cookie + the matching row in `idp.sessions`; redirects to `post_logout_redirect_uri` (must be in the client's registered list).

#### Flow — RP-initiated logout

```
  Browser            marvel uniservice          idp uniservice           on-prem MongoDB

  click "Sign out" on /main
  ──────────────►   POST /api/v1/sso/logout
                      delete marvel session                            ─►  xpmile.sessions
                      purge local LRU cache entry
                      if provider == in-house:
                        build IdP end_session URL
  ◄── 200 {
        ok: true,
        idp_logout_url: "https://idp-.../api/v1/idp/end_session?
                          id_token_hint=<t>&
                          post_logout_redirect_uri=marvel-.../login"
      }
  (SPA clears local state, then navigates to idp_logout_url)

  GET idp-.../api/v1/idp/end_session?id_token_hint=...&post_logout_redirect_uri=...
  ────────────────────────────────────────►   validate id_token_hint
                                              validate post_logout_redirect_uri
                                                in client's registered list
                                              delete IdP session row    ─►  idp.sessions
  ◄── 302 marvel-.../login + Set-Cookie xpmile_idp_session=; Max-Age=0
```

---

## 4. The idp Angular project — `ui-idp/`

A new, small Angular project sibling to the existing `ui/`. Lives at `ui-idp/`. **Not** part of the xpmile SPA; this is the IdP's own UI.

Why a separate project rather than reusing `ui/`:
- The branded portal must look like the *IdP's* page, not the xpmile app's — different layout, no navbar, no app chrome.
- The IdP UI doesn't need the bulk of the xpmile SPA's dependencies (Clarity components for shipments, accounts, pdfMake, etc.). Smaller bundle = faster first paint on a login page.
- Keeps the projects' deployment lifecycles independent — bumping a Clarity version in the xpmile SPA doesn't touch the IdP UI.

Structure (mirrors `ui/` patterns):
```
ui-idp/
├── package.json
├── angular.json
├── tsconfig.json
├── src/
│   ├── index.html
│   ├── main.ts
│   ├── styles.css
│   ├── assets/
│   │   └── images/xpmile-logo.svg        (the brand)
│   └── app/
│       ├── app.module.ts
│       ├── app-routing.module.ts
│       ├── login/
│       │   ├── login.component.{ts,html,css}
│       └── password-reset/
│           ├── password-reset.component.{ts,html,css}
│           └── password-reset-confirm.component.{ts,html,css}
└── proxy.conf.json
```

Routes:

| Path | Component | Notes |
|------|-----------|-------|
| `/login` | `LoginComponent` | Branded login form (logo, username, password, "Forgot password" link). POST to `/api/v1/idp/login`. On success, server returns 302 to `/api/v1/idp/authorize?...`. |
| `/password-reset` | `PasswordResetStartComponent` | "Enter your email." POST to `/api/v1/idp/password-reset/start`. Always shows "Check your email" (no enumeration). |
| `/password-reset/confirm` | `PasswordResetConfirmComponent` | Reads `?token=` from URL, shows new-password form. POST to `/api/v1/idp/password-reset/confirm`. On success, redirects to `/login` with a success flash. |
| `*` (fallback) | redirect to `/login` | |

The Angular dist is packaged by a new `docker/Dockerfile.idp` (built from `ui-idp/`), parallel to the existing `docker/Dockerfile` (built from `ui/`). The C++ uniservice binary in the runtime stage is the same — it serves whatever sits at `../webgui/webui/`.

---

## 5. Password reset flow

Three collections, all in `idp`:

- `idp.account` — holds the hashed password (the auth half of the post-migration split). The IdP writes the new hash here.
- `idp.password_resets` — new. `{ _id: <random-32B-base64url>, accountCode, expiresAt }` with a TTL index on `expiresAt`.
- `idp.sessions` — the IdP's own session store. A successful reset followed by login mints a fresh row here. `xpmile.sessions` (marvel's RP-side session store, unchanged from v1.0) is touched on the *marvel* side after the federated callback completes — but that's the marvel uniservice's normal v1.0 behaviour, not part of the IdP's reset flow.

Flow:

1. User → `https://idp-.../password-reset` → enters email.
2. `POST /api/v1/idp/password-reset/start` with `{ email }`:
   - Look up account by email in `idp.account`.
   - **Same response whether found or not** (no enumeration leak).
   - If found: generate token, insert `idp.password_resets` doc (TTL 30 min), send email via the existing email module.
   - Email body: a short message + a link to `https://idp-.../password-reset/confirm?token=<token>`.
3. User clicks link → `/password-reset/confirm?token=...` → enters new password (twice).
4. `POST /api/v1/idp/password-reset/confirm` with `{ token, new_password }`:
   - Look up `idp.password_resets` by token; reject if missing / expired / already consumed.
   - Hash the new password (using the same scheme the rest of the app uses); `update_collection` `idp.account`.
   - Delete the `idp.password_resets` doc.
   - Invalidate all existing `idp.sessions` for this account (so the IdP forces a re-auth at the next `/authorize`).
5. Redirect to `/login` with a success flash.

Reuses: the existing email module (SMTP::User FSM), the existing password-hashing function, the existing session-revocation code (`SessionManager::revoke`).

#### Flow — password reset

```
  Browser              idp uniservice           on-prem MongoDB            SMTP

  GET /password-reset   [Angular renders email form]
  ─────────────────►   ◄── 200 index.html

  POST /api/v1/idp/password-reset/start {email}
  ─────────────────►   lookup account by email                    ─►   idp.account
                       if found:
                         generate 32B CSPRNG token
                         insert reset doc (TTL 30 min)            ─►   idp.password_resets
                         compose + send email                                    ─►  SMTP::User → user inbox
                       (response identical whether email found or not)
  ◄── 200 "If an account exists for that email, a reset link has been sent."

  user reads email, clicks https://idp-.../password-reset/confirm?token=<t>

  GET /password-reset/confirm?token=<t>   [Angular renders new-password form]
  ─────────────────►   ◄── 200 index.html

  POST /api/v1/idp/password-reset/confirm {token, new_password}
  ─────────────────►   look up reset doc by token                 ─►   idp.password_resets
                       reject if missing / expired / consumed
                       hash new_password
                       update                                     ─►   idp.account.passwordHash
                       delete reset doc                           ─►   idp.password_resets
                       revoke all sessions for account            ─►   idp.sessions
  ◄── 302 /login (success flash)
```

---

## 6. Vaadin admin views (on-prem)

Two new views in the existing Vaadin app (`onprem/src/main/java/com/xpmile/onprem/ui/idp/`):

- **`IdpSigningKeysView` (`/idp-keys`)** — list keys (kid, alg, createdAt, notAfter, active flag); **"Generate new key"** button (creates RSA-2048 keypair in Java via `KeyPairGenerator`, PEM-encodes both halves, inserts into `idp.idp_signing_keys`, optionally activates it and deactivates the previous active key); **"Deactivate"** / **"Delete expired"** actions.

- **`IdpClientsView` (`/idp-clients`)** — list registered RP clients (from `idp.idp_clients`); per-client fields: `client_id`, `client_name`, `redirect_uris[]`, `post_logout_redirect_uris[]`, `client_secret` (write-only, hashed in storage), `grant_types[]` (initial: just `authorization_code`), `scopes[]`. The marvel SPA gets a pre-seeded client (`xpmile-spa`).

Both write directly to the on-prem MongoDB — same trust model as the existing `SsoConfigView` (the Vaadin console sits behind the customer's physical/network access controls; no internet-facing config-write endpoint).

---

## 7. The on-prem stack now runs two wsdbagent containers

The existing `docker-compose.agent.yml` is extended. After the change:

| Service | Image | Connects to | Cert family |
|---------|-------|-------------|-------------|
| `mongodb` | `xpmile-mongo:latest` (built locally) | — | — |
| `agent-wsdbagent-marvel` | `docker.io/naushada/xpmile-wsdbagent:latest` | `marvel-3a78bd953f5f.herokuapp.com` | `./certs/cloud-issued/innertls-marvel/` |
| `agent-wsdbagent-idp` | `docker.io/naushada/xpmile-wsdbagent:latest` | `idp-63c97365e6ef.herokuapp.com` | `./certs/cloud-issued/innertls-idp/` |
| `xpmile-cert-watcher` | `docker.io/library/alpine:3.19` | — (watches both cert dirs) | — |

The same `xpmile-wsdbagent` image runs both agents — no code change to the wsdbagent binary (other than adding the `SIGN_JWT` case in the dispatcher, which is harmless on either connection — only the idp connection ever sees the op).

The cert-watcher is taught about both cert dirs and restarts the corresponding agent on change:

```sh
# pseudocode for the watcher
for dir in /watch/marvel /watch/idp; do
  md5=$(md5sum "$dir"/*.crt "$dir"/*.key | sort | md5sum)
  ...
  if changed -> POST restart to agent-wsdbagent-<which>
done
```

The `./run-agent.sh refresh-certs` command extends to refresh both families. Simplification possible if both Heroku apps deploy the **same** uniservice image (see §11 open question on CI strategy) — then there's one CA, one cert family, and the on-prem story stays single-agent-ish.

---

## 8. Coexistence with v1.0 federated SSO

The in-house IdP appears as **one more entry in `sso_config`** on the marvel side, with `protocol: "oidc"`:

```jsonc
{
  "publicBaseUrl": "https://marvel-3a78bd953f5f.herokuapp.com",
  "providers": [
    {
      "id":          "xpmile",                                       // the in-house IdP
      "displayName": "Sign in",
      "protocol":    "oidc",
      "issuer":      "https://idp-63c97365e6ef.herokuapp.com/api/v1/idp",
      "clientId":    "xpmile-spa",                                   // registered with the IdP via IdpClientsView
      "clientSecret":"...",                                          // registered with the IdP via IdpClientsView
      "scopes":      ["openid", "email", "profile"],
      "defaultRole": "Customer",
      "allowedEmailDomains": []                                      // our own users — no domain restriction
    },
    {
      "id":          "corp",                                         // an external IdP (existing v1.0)
      "displayName": "Acme Corp SSO",
      "protocol":    "oidc",
      "issuer":      "https://acme.okta.com/oauth2/default",
      // ...
    }
  ]
}
```

**The v1.0 `OidcProvider` client code handles the in-house IdP identically to Okta** — discovery fetch + token exchange + JWT verify against JWKS. No client-side changes. The login page lists all configured providers; the in-house "Sign in" sits alongside any external ones the customer has set up.

If a customer wants the in-house IdP to be the *only* option, they remove the others from `sso_config`. If they want it default, they put it first.

---

## 9. Reuse map — what the existing stack gives us, what's new

What we reuse, unchanged:

| Need | Code |
|------|------|
| HTTP request parsing | `Http` parser (`modules/module/http/`) |
| HTTP response building (200 / 302 / 401) | `MicroService::build_responseOK`, `build_redirect`, `build_responseERROR`, `attach_set_cookie` |
| Routing for the new `/api/v1/idp/*` endpoints | `MicroService::process_request` URI-prefix dispatch (mirrors `handle_sso`) |
| JSON serialization | `nlohmann::json` |
| Cookie issue/parse | `sso_cookie.*` (extend with the IdP cookie name) |
| Session storage + LRU cache | `sso_session.*` (a second `SessionManager` instance on idp host) |
| CSPRNG / base64url codecs | `sso_util.*` |
| JWT *verify* (the v1.0 client side, on marvel) | `sso_jwt.*` (unchanged) |
| OIDC *client* (marvel federating against the in-house IdP) | `OidcProvider` + `sso_endpoints.*` (unchanged — federates against the in-house IdP the same way it federates against Okta) |
| Hot-reload of config / JWKS | the existing `sso_config` hot-reload thread (extended on idp host to also poll `idp_signing_keys`) |
| Outbound HTTP client (for marvel's discovery fetch against the in-house IdP) | `sso_http_client.*` (unchanged — just fetches the discovery doc from the new host) |
| MongoDB access from cloud (always via on-prem agent) | `IMongodbClient` / `WsMongodbProxy` — one instance per uniservice, defaulted to its own database. **Idp uniservice: `idp` only.** **Marvel uniservice: `xpmile` for everything except the legacy `handle_account_login_POST`,** which does a single cross-DB `idp.account` read for the fallback login path. |
| WebSocket DB proxy protocol | `dbproto` + `wsframe` — *extended with the new `SIGN_JWT` op* |
| Inner-TLS encryption of the wsdbagent tunnel | `security/innertls.*` (unchanged) |
| Password verification | the existing password-hashing comparison logic from `handle_account_login_POST` — extracted into a small reusable helper called by both `handle_account_login_POST` (legacy fallback, cross-DB reads `idp.account`) and `handle_idp_login_POST` (IdP main path, reads `idp.account` directly). The legacy handler **keeps working as a fallback** (Q12 resolution); it just sources its hash from the new location. |
| Email delivery for the reset flow | `SMTP::User` (the existing email module) |
| Admin UI for keys + clients | the existing Vaadin app — same patterns as `SsoConfigView` |
| C++ binary | the *same* `uniservice` binary deploys to both marvel and idp Heroku apps |
| wsdbagent binary | the *same* `xpmile-wsdbagent` image runs the marvel and idp agents on-prem |
| Inner-TLS, cert rotation, cert-watcher | extended (two agents, two cert families) but the mechanism is unchanged |

What's genuinely new — the scope of the implementation work:

1. **`SIGN_JWT` op end-to-end** — cloud proxy method, dbproto schema, wsdbagent dispatcher, tests.
2. **OIDC server endpoints** in a new `modules/module/inhouseidp/` — ~8 handler functions + the helpers they call.
3. **A new `idp` database** with six collections (TTL indexes where appropriate), seeded by `mongo-init.js`: `idp_signing_keys`, `idp_clients`, `idp_codes`, `idp_access_tokens`, `idp_pending_auth`, `password_resets`, plus the IdP's own `sessions` collection. The same `MONGO_APP_USER` is granted `readWrite` on both `xpmile` and `idp`.
4. **A small JWT *signer*** (cloud side) that builds `to_sign`, delegates the signature to `WsMongodbProxy::sign_jwt`, and assembles the final compact JWT.
5. **Two Vaadin admin views** (`IdpSigningKeysView`, `IdpClientsView`).
6. **`ui-idp/`** — a small Angular project (login + password reset).
7. **`docker/Dockerfile.idp`** — mirrors `docker/Dockerfile`, but packages `ui-idp` dist.
8. **`docker-compose.agent.yml`** extended — add `agent-wsdbagent-idp` + cert-watcher rule.
9. **CI workflow** extended — build + push the IdP image to `registry.heroku.com/idp/web` after the test gate (parallel to the existing marvel push). One image build, two registry pushes.
10. **`./run-agent.sh refresh-certs`** extended to refresh both cert families (or kept single if both Heroku apps share the same image — open question §11).

---

## 10. Security analysis

| Threat | Mitigation |
|--------|------------|
| **Authorization-code interception** | PKCE (`S256`) required on every `/authorize`; stolen code is useless without the verifier, which never leaves the RP. |
| **Code replay** | Codes are one-time use (atomic `update_collection` to mark consumed) and short-lived (TTL 30 s in `idp_codes`). |
| **CSRF on `/authorize` callback** | The RP's `state` round-trips and is validated by the v1.0 client code (unchanged). |
| **id_token replay** | `nonce` is bound to the authorization request and embedded in the id_token; the v1.0 client verifies it (unchanged). |
| **JWT algorithm-confusion** | Server signs only RS256 (no per-request alg negotiation); JWKS only advertises RS256; the client (v1.0) already enforces an `alg` allow-list. |
| **Private-key compromise on the cloud** | **The private key is never on the cloud.** Cloud holds only the public key. Compromise of either Heroku app (read of disk, env, RAM) yields no signing capability. |
| **Forged signing key** | Public keys come from `idp_signing_keys`, written only by the on-prem Vaadin admin. The RP fetches them through HTTPS to a URL pinned by the issuer config. TLS verifies the cloud's cert; the JWKS endpoint is no more forgeable than any other cloud endpoint. |
| **Open redirect via `redirect_uri`** | Matched **exactly** against the client's registered list (no prefix / wildcard / case-insensitive matching). |
| **Open redirect via `post_logout_redirect_uri`** | Same exact-match rule. |
| **Open redirect via password-reset link** | Link is fixed to `/password-reset/confirm?token=...` on the IdP host; no externally-controlled redirect target. |
| **Password-reset token replay / log leak** | Tokens are CSPRNG, single-use (deleted on consumption), short TTL (30 min); stored hashed in `password_resets` to limit log-exposure damage. |
| **Email enumeration on password reset** | Same response (and as far as feasible, same timing) whether the email exists or not. |
| **Session cookie theft via XSS** | `HttpOnly` + `Secure` + `SameSite=Lax` on `xpmile_idp_session` and `xpmile_idp_pending`. |
| **`Host`-header poisoning of issuer URL / redirect URL** | Both pinned to `publicBaseUrl` from `sso_config` / IdP config (same rule as v1.0); never derived from request headers. |
| **Brute-force on `/api/v1/idp/login`** | Per-account exponential backoff stored in the account doc (`lastFailedAt`, `failedCount`). v1 ships with backoff only; per-IP rate limiting is a v2 follow-up. |
| **Brute-force on `/api/v1/idp/password-reset/start`** | Stateless rate-limit per requester IP (in-memory ring buffer) + same-response policy (an attacker can't tell which addresses exist). |
| **Tampered authorization request between `/authorize` and `/login`** | The pending request is stored server-side in `idp_pending_auth`, keyed by an opaque `req_token` cookie. The browser only carries the cookie; the request parameters are not in the URL after the redirect. |
| **Marvel host accidentally serves IdP endpoints** | The discovery doc returned would carry an `issuer` field pointing at idp host; standard OIDC clients reject on the issuer-URL/fetched-URL mismatch. Belt-and-braces follow-up: Host-header gating for the `/api/v1/idp/*` routes. |

---

## 11. Phased delivery

All eleven phases ship in v1; phasing is build order. Phase pre-A runs **sequentially** with the IdP deploy (Q11): deploy first, then run the migration, then continue.

| Phase | Scope |
|-------|-------|
| **Pre-A. Account split migration** | A one-time idempotent migration script that creates `idp.account` documents from `xpmile.account` (copying `accountCode`, `passwordHash`, `email`, `name`, `role`) and then `$unset`s those fields from `xpmile.account`. Gated by a `schema_version` doc so it's a no-op on a second run. Bundled into `docker/mongo-init.js` as a self-check, plus a standalone `scripts/migrate-account-split.py` for existing deployments where `mongo-init.js` only fires on first volume creation. **Flow diagram below.** |

#### Flow — pre-A account split migration (one-shot, idempotent)

```
  scripts/migrate-account-split.py   (or docker/mongo-init.js on first volume creation)

  ┌─────────────────────────────────────────────────────────────┐
  │  read xpmile.schema_version                                 │
  │    if >= 2 → exit "already migrated"                        │
  │                                                             │
  │  for each doc D in xpmile.account:                          │
  │                                                             │
  │      if not exists idp.account.{accountCode: D.accountCode}:│
  │          insert into idp.account:                           │
  │            { accountCode:   D.accountCode,                  │
  │              passwordHash:  D.passwordHash,                 │
  │              email:         D.email,                        │
  │              name:          D.name,                         │
  │              role:          D.role }                        │
  │                                                             │
  │      $unset on xpmile.account.{_id: D._id}:                 │
  │            { passwordHash, email, name, role }              │
  │      (xpmile.account doc keeps accountCode + business       │
  │       fields: awbPrefix, eventLocation, personalInfo, …)    │
  │                                                             │
  │  set xpmile.schema_version = 2                              │
  └─────────────────────────────────────────────────────────────┘

  Re-running the script is a no-op: the schema_version guard exits
  early on the second run; even without the guard, the lookup +
  $unset are individually idempotent.
```
| **A. On-prem signing service** | New `DbOp::SIGN_JWT`; `WsMongodbProxy::sign_jwt`; wsdbagent dispatcher; `idp.idp_signing_keys` collection seed. Unit-testable through `MockMongodbClient` on both sides. |
| **B. Signing-key admin** | `IdpSigningKeysView` Vaadin view — generate / activate / list / deactivate. RSA-2048 keypair generation in Java via `KeyPairGenerator`; insert both halves PEM-encoded into `idp_signing_keys`. |
| **C. JWKS + discovery endpoints** | Static-ish responses driven by hot-reloaded `idp_signing_keys` + IdP config. Unit tests against `MockMongodbClient`. |
| **D. `/authorize` + `/login` + IdP session** | Full happy path: pending-auth storage, login form POST, credential check (reuses existing path), session cookie issuance, code generation, redirect. Negative paths: bad client_id, bad redirect_uri, bad credentials, replayed pending request. |
| **E. `/token` (with `SIGN_JWT` round-trip)** | Code consumption, PKCE validation, id_token assembly, JWT signing via wsdbagent, response. End-to-end test: the resulting token verifies under the JWKS endpoint's keys. |
| **F. `ui-idp/` Angular project** | New project; `LoginComponent`, `PasswordResetStart/ConfirmComponent`. Manual verification (the project has no Karma setup, per the v1.0 SSO design's Phase D). |
| **G. Password reset (backend)** | `password_resets` collection, the two backend endpoints, the email template. Backend unit tests; manual + UI verification via `ui-idp/`. |
| **H. `docker/Dockerfile.idp` + CI publish** | New Dockerfile packaging `ui-idp` dist + the existing uniservice binary. CI extended to push the idp image to `registry.heroku.com/idp/web` after the marvel push. Single build, two Heroku releases. |
| **I. On-prem two-agent stack** | `docker-compose.agent.yml` gains `agent-wsdbagent-idp` + cert-watcher rule for the new cert dir. `./run-agent.sh refresh-certs` extended. End-to-end smoke test from the cloud idp app through to MongoDB on-prem. |
| **J. Coexistence wiring** | Add the in-house IdP to `sso_config` on marvel (initially as an opt-in entry). Register the marvel SPA as `xpmile-spa` in `IdpClientsView`. Manual end-to-end test: user clicks "Sign in" on marvel → redirected to idp host → enters credentials → redirected back to marvel → has a valid `xpmile_session`. |
| **K. Repoint legacy `/api/v1/account/login` to the new auth source** | The legacy endpoint **stays as a fallback** (Q12 resolution). Repoint `handle_account_login_POST` to cross-DB read `idp.account.passwordHash` (one cross-DB read, scoped to this single endpoint) using the shared password-verification helper. The marvel SPA keeps its username/password form alongside the SSO "Sign in" button — users can choose either path. |

`/userinfo` and `/end_session` come along with Phase E (small).

---

## 12. Decisions and open questions

### Resolved (from the design intake)

- **Full OIDC provider** (Authorization Code + PKCE), single registered RP at v1.
- **Coexists with v1.0 federated SSO** — appears as one more entry in `sso_config`.
- **v1 auth features:** username + password + self-service password reset. No MFA, no self-registration.
- **JWT signing happens on-prem** via a new `wsdbagent` SIGN_JWT op; the private key never leaves the on-prem MongoDB.
- **Passwords are already hashed** in `account.passwordHash` (per design intake); no migration needed.
- **Issuer URL:** `https://idp-63c97365e6ef.herokuapp.com/api/v1/idp`.
- **The IdP is hosted on its own Heroku app** (idp-63c97365e6ef.herokuapp.com) — distinct from the existing marvel app. The same `uniservice` binary deploys to both; the Angular dist differs per host.

### Newly resolved (from this iteration)

- **Migration sequencing (was Q11):** **two-step / sequential.** Deploy the IdP code first (Phases A–J without K), validate the IdP path manually, *then* run `scripts/migrate-account-split.py` against the on-prem MongoDB, *then* deploy Phase K which repoints the legacy endpoint to its new auth source.
- **Legacy `POST /api/v1/account/login` deprecation (was Q12):** **no deprecation.** The legacy endpoint stays as a fallback for IdP-unreachable scenarios and admin recovery. It gets repointed (Phase K) to cross-DB read `idp.account` — the *single* cross-DB access in the system, scoped to this endpoint only. The marvel SPA keeps its username/password form alongside the SSO "Sign in" button.
- **Vaadin AccountsView after the split (was Q13):** **joined view.** Single screen, two backing services (`IdpUserService` + existing `AccountService`), linked by `accountCode`. Operators continue to think of an account as one logical thing.

### Still open

1. **Same image to both Heroku apps, or separate builds?** Same image (one CI build, two `heroku container:release`) → both apps always share the same InnerTLS CA → one cert family on-prem → no extension to refresh-certs needed. Separate builds → independent release cadence → two CAs → two cert families on-prem. Default: **same image**.
2. **`idp_clients` storage shape.** Separate `idp_clients` collection (multi-client-ready, what this doc assumes) vs inline into the IdP config (simpler while there's exactly one client). Default: **separate collection**.
3. **`/api/v1/idp/*` vs root-level OIDC paths on the idp host.** /api/v1/idp/* matches the existing convention. Root-level (`/authorize`, `/token`, `/jwks`) is more conventional for a dedicated IdP host. Default: **`/api/v1/idp/*`** to match the existing routing pattern.
4. **Brute-force protection on `/login`.** v1 lean: per-account exponential backoff stored in the account doc. v1 thorough: separate `auth_attempts` collection + IP-level rate limit. Default: **minimal (per-account exponential backoff)**.
5. **Refresh tokens.** Not in v1 (sessions handle the "stay logged in" UX via the RP-side `xpmile_session` cookie). Confirm.
6. **Key rotation cadence.** Manual via Vaadin only for v1; no scheduler. Confirm.
7. **`/userinfo` scope.** Include in v1 for protocol completeness — but the marvel SPA doesn't need it (claims live in the id_token). Confirm "build it but don't actively use it" is fine.
8. **Account lockout policy.** v1: no hard lockout, just exponential backoff. Vaadin Accounts view gets a "reset failed attempts" button. Confirm.
9. **Logging out at the IdP when the RP logs out.** When `POST /api/v1/sso/logout` fires on marvel, should it also call the IdP's `/end_session`? Default: **yes** — call `/end_session` for the in-house provider on SPA logout (matches user expectation that "log out" = "log out").
10. **Host-header gating** for IdP routes on the marvel host. v1 default: **don't gate** (accept the spec-rejected-but-not-exploitable mismatch); revisit if we find a real attack vector.

---

## 13. Files — new and changed

**New module — `modules/module/inhouseidp/`** (a sibling to `modules/module/sso/`; keeps the IdP server code separate from the SSO client code that lives in `sso/`):

| File | Contents |
|------|----------|
| `inc/idp_endpoints.hpp` / `src/idp_endpoints.cpp` | The eight `handle_idp_*` adapter functions + `IdpHttpResult` |
| `inc/idp_authorize.hpp` / `src/idp_authorize.cpp` | `/authorize` logic — request validation, pending-auth storage, code generation |
| `inc/idp_token.hpp` / `src/idp_token.cpp` | `/token` logic — code consumption, PKCE check, id_token assembly, signing call |
| `inc/idp_jwks.hpp` / `src/idp_jwks.cpp` | JWKS document construction from `idp_signing_keys` |
| `inc/idp_discovery.hpp` / `src/idp_discovery.cpp` | `.well-known/openid-configuration` builder |
| `inc/idp_client_registry.hpp` / `src/idp_client_registry.cpp` | Load/lookup registered clients from `idp_clients` |
| `inc/idp_password_reset.hpp` / `src/idp_password_reset.cpp` | Reset start + confirm logic; email template |
| `inc/idp_session.hpp` / `src/idp_session.cpp` | A second `SessionManager` instance scoped to the IdP cookie |
| `test/inhouseidp_test.cc` | Unit tests (see TDD plan) |

**Extended existing modules:**

| File | Change |
|------|--------|
| `modules/module/sso/inc/sso_jwt.hpp` / `src/sso_jwt.cpp` | Add `make_jwt_unsigned(header, payload) → to_sign` and `assemble_jwt(to_sign, signature) → string` helpers |
| `modules/module/wsdbproxy/inc/wsdbproxy.hpp` / `src/wsdbproxy.cpp` | Add `WsMongodbProxy::sign_jwt()` |
| `modules/module/wsdbproxy/inc/dbproto.hpp` / `src/dbproto.cpp` | Add `DbOp::SIGN_JWT` + request/response BSON schema |
| `modules/module/wsdbagent/src/wsdbagent.cpp` | Add `SIGN_JWT` dispatcher case + `sign_jwt_on_prem()` helper |
| `modules/module/webservice/inc/webservice.hpp` / `src/webservice.cpp` | Route `/api/v1/idp/*` to a new `handle_idp` adapter (mirrors `handle_sso`); extract the credential-check helper for reuse between `handle_account_login_POST` and `handle_idp_login_POST` |
| `docker/mongo-init.js` | Create the new `idp` database and its eight collections (`account`, `idp_signing_keys`, `idp_clients`, `idp_codes`, `idp_access_tokens`, `idp_pending_auth`, `password_resets`, `sessions`) with TTL indexes where appropriate; grant `MONGO_APP_USER` `readWrite` on `idp` in addition to the existing `xpmile` grant; run the account-split migration if `schema_version < 2` |
| `scripts/migrate-account-split.py` (new) | Standalone idempotent migration — copies auth fields from existing `xpmile.account` docs to `idp.account` (keyed by `accountCode`), then `$unset`s them from `xpmile.account`. Bumps `schema_version` to 2. For deployments where `mongo-init.js` doesn't fire (existing volumes) |
| `CMakeLists.txt` | `add_executable(uniservice ...)` globs the new `inhouseidp` module |
| `test/CMakeLists.txt` | Add the new `inhouseidp` test sources |

**Deployment:**

| File | Change |
|------|--------|
| `docker/Dockerfile.idp` (new) | Mirrors `docker/Dockerfile`; uses `ui-idp/` for the Angular stage; uses the same C++ uniservice binary in the runtime stage |
| `docker-compose.agent.yml` | Add `agent-wsdbagent-idp` service (same `xpmile-wsdbagent` image, different `SERVER_HOST`, separate cert bind-mount); extend `xpmile-cert-watcher` to watch both cert dirs |
| `run-agent.sh` | Extend `refresh-certs` to also extract certs for the idp image (if §11 Q1 resolves to "separate images"); extend `start`/`stop`/`status`/`logs` to enumerate both agents |
| `.github/workflows/publish-images.yml` | Add a parallel `heroku container:release` step targeting `idp` after the existing `marvel` release. **Or** (if single image): also push the existing uniservice image to `registry.heroku.com/idp/web` |

**Angular — new project:**

| File | Change |
|------|--------|
| `ui-idp/package.json`, `angular.json`, `tsconfig.json`, ... | Standalone Angular project at `ui-idp/`. Small dependency set — Angular core, Clarity (or vanilla CSS), Forms |
| `ui-idp/src/app/login/login.component.{ts,html,css}` | Branded login form |
| `ui-idp/src/app/password-reset/...component.{ts,html,css}` | Reset start + confirm components |
| `ui-idp/src/assets/images/xpmile-logo.svg` | Brand asset |

**On-prem Vaadin:**

| File | Change |
|------|--------|
| `onprem/src/main/java/com/xpmile/onprem/ui/idp/IdpSigningKeysView.java` (new) | Admin view: generate / list / activate / delete signing keys |
| `onprem/src/main/java/com/xpmile/onprem/ui/idp/IdpClientsView.java` (new) | Admin view: register / list / edit RP clients |
| `onprem/src/main/java/com/xpmile/onprem/service/IdpSigningKeyService.java` (new) | MongoDB CRUD for `idp_signing_keys`; RSA-2048 keypair generation |
| `onprem/src/main/java/com/xpmile/onprem/service/IdpClientService.java` (new) | MongoDB CRUD for `idp_clients` |
| `onprem/src/main/java/com/xpmile/onprem/ui/MainLayout.java` | Add the two new side-nav items |

**Docs:**

| File | Change |
|------|--------|
| `codebase.md` | New section for `modules/module/inhouseidp/` and the new `DbOp::SIGN_JWT`; new collections in the collections table |
| `CLAUDE.md` | New section "In-house IdP" — working conventions for the new module |
| `docs/design/sso/sso-design.md` | One sentence linking forward: "An in-house IdP is also available; see `inhouse-idp-design.md`." |
| `docs/operator-guide.md` (on release/v1.x) | New section on configuring the in-house IdP — signing-key rotation, registering an RP, the two-agent on-prem layout |

---

## 14. Risks and known limitations

- **The on-prem signing round-trip adds latency to token issuance.** Tokens are issued at login + (with refresh) at refresh — not per-request. Empirically the wsdbagent round-trip is a few ms on a healthy connection.
- **Two agent reconnects on a CI deploy.** Each Heroku app's deploy rotates *its* CA. If we go with the "same image to both" option (§11 Q1), there's still only one CA in play. If we go with "separate builds", the on-prem operator runs through two cert refreshes per release cycle.
- **Single registered client at v1 is a soft limit.** The schema is multi-client-ready; the Vaadin view is multi-client-ready; only the documentation calls out "v1 ships with one". Adding a second is configuration only.
- **Brute-force resistance is minimal in v1** (per-account exponential backoff only). A distributed attacker can still test passwords faster than a single source. Phase 2 should add `auth_attempts` with per-IP limits and a real lockout policy.
- **No MFA.** Federated customers can require MFA at their IdP; in-house customers can't until we ship MFA. Largest follow-up.
- **The IdP UI is a separate Angular project.** Two `package.json`s, two `npm install`s in CI. Acceptable for the cleanness win, but worth noting.
- **The marvel host can technically serve `/api/v1/idp/*`.** Returns a discovery doc with the wrong `issuer` field; standard clients reject. Not exploitable but is awkward. Belt-and-braces: Host-header gating (§11 Q10), tracked as a follow-up.
