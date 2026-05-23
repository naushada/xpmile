# Design: In-house OIDC identity provider

> Status: **Draft for review.** TDD plan follows once approved.

## Problem summary

The v1.0 SSO federates against *external* IdPs — Okta, Entra, partner SAML systems. That works for customers who already have a corporate IdP, but it has two real costs:

1. **Operational dependency on a third party.** Every login round-trip touches the IdP. If the IdP is degraded, the customer can't sign in.
2. **No good story for customers without an IdP.** They're stuck on the existing password-only login, which doesn't share the cookie/session model SSO uses, lacks self-service password reset, and isn't a path other apps could federate against later.

This design adds an **in-house OIDC identity provider** hosted on the same xpmile infrastructure. The cloud `uniservice` (ACE + our `Http` parser + Angular) exposes the OIDC protocol surface and a branded login portal. The customer's credentials and the JWT signing private key stay behind the on-prem NAT — the cloud talks to them only through `wsdbagent` + InnerTLS, the same channel the rest of the app already uses.

## Goals

- Stand up an **OIDC identity provider** on the existing cloud uniservice — full discovery / authorize / token / JWKS endpoint surface, Authorization Code + PKCE.
- All user credentials stay on the on-prem MongoDB. The cloud never sees a plaintext password.
- The JWT signing **private key never leaves the on-prem side.** Signing happens on-prem via a new `wsdbagent` op; the cloud only ever holds the public key.
- The xpmile SPA federates against this IdP using the **same `OidcProvider` client code** already shipped in v1.0 — zero client-side code changes; the in-house IdP is just one more entry in `sso_config`.
- **Self-service password reset** via email-token (reuses the existing email module).
- Single registered client (the xpmile SPA) for v1; architected so a second client can be added later by registering it, without code changes.
- Coexists with the v1.0 federated SSO — Okta/Entra/SAML providers stay as-is; the in-house IdP is an additional `sso_config` entry.

## Non-goals

- Multi-factor authentication (TOTP / WebAuthn) — explicit follow-up.
- Self-service registration — accounts continue to be created admin-only via the on-prem Vaadin Accounts view.
- Acting as a general OAuth2 authorization server for third-party API access — this is an IdP for *sign-in*, not for delegating API access.
- IdP-initiated SSO (only RP-initiated authorization code flow).
- Multi-tenancy at the cloud layer — per-customer Heroku deployment continues; one xpmile cloud = one in-house IdP for that customer's users.

---

## 1. Architecture

The cloud is the public face; everything secret-bearing — user passwords *and* the JWT signing key — stays on-prem.

```
 Browser (Angular)              Cloud uniservice              wsdbagent          On-prem MongoDB
                                (ACE + Http parser)           (InnerTLS)
   │                                  │                            │                    │
   │  redirect from xpmile SPA        │                            │                    │
   │  to /api/v1/idp/authorize?...    │                            │                    │
   │ ────────────────────────────────►│                            │                    │
   │                                  │  no IdP session yet —      │                    │
   │  302 to /sso/login               │  store the auth request    │                    │
   │ ◄────────────────────────────────│                            │                    │
   │                                  │                            │                    │
   │  GET /sso/login                  │                            │                    │
   │  (branded portal, xpmile logo)   │                            │                    │
   │ ────────────────────────────────►│                            │                    │
   │                                  │                            │                    │
   │  POST /api/v1/idp/login          │  verify password against   │                    │
   │       {user, pass}               │  account.passwordHash      │                    │
   │ ────────────────────────────────►│ ──── DB.findOne(...) ───── │ ─────────────────► │
   │                                  │ ◄────── account doc ────── │ ◄───────────────── │
   │                                  │  create xpmile_idp_session │                    │
   │  302 back to /api/v1/idp/        │                            │                    │
   │  authorize  (with cookie)        │                            │                    │
   │  Set-Cookie: xpmile_idp_session  │                            │                    │
   │ ◄────────────────────────────────│                            │                    │
   │                                  │                            │                    │
   │  GET /api/v1/idp/authorize       │  user has IdP session;     │                    │
   │  (cookie sent automatically)     │  mint authz code; store    │                    │
   │ ────────────────────────────────►│  in idp_codes              │                    │
   │  302 to <RP-redirect_uri>?       │                            │                    │
   │       code=...&state=...         │                            │                    │
   │ ◄────────────────────────────────│                            │                    │
   │                                  │                            │                    │
   │  back at xpmile SPA's callback   │                            │                    │
   │  (handled by existing v1.0       │                            │                    │
   │   OidcProvider client code)      │                            │                    │
   │                                  │                            │                    │
   │  server-side POST                │  build header.payload      │                    │
   │  /api/v1/idp/token               │  for id_token              │                    │
   │  (from RP, with code + PKCE)     │                            │                    │
   │                                  │  WsMongodbProxy.sign_jwt → │                    │
   │                                  │ ─────────── SIGN_JWT ─────►│  load privateKey   │
   │                                  │                            │  by kid; RS256-sign│
   │                                  │ ◄────────── signature ──── │  the to-be-signed  │
   │                                  │  assemble final JWT;       │                    │
   │                                  │  return id_token+access_   │                    │
   │                                  │  token in the response     │                    │
   │                                  │                            │                    │
   │  (existing v1.0 client code      │                            │                    │
   │   verifies id_token via JWKS,    │                            │                    │
   │   mints xpmile_session cookie,   │                            │                    │
   │   redirects browser to /main)    │                            │                    │
```

**The two-cookie story** (worth being explicit about because they look similar):
- `xpmile_idp_session` — the **IdP's** session cookie. Says "this browser has authenticated at the IdP" so `/authorize` can issue an authz code without prompting for credentials again. Scoped to `/api/v1/idp/`.
- `xpmile_session` — the **RP's** (xpmile SPA's) session cookie. Exists today, unchanged. Says "this browser is signed in to the xpmile app".

When the xpmile SPA federates against the in-house IdP, **both** are set — `xpmile_idp_session` by the IdP after credential verification, `xpmile_session` by the SPA's callback handler after id_token verification. If the user then logs into another (hypothetical) RP, the existing `xpmile_idp_session` lets the IdP issue a token without re-prompting for credentials — that's the SSO experience.

---

## 2. The on-prem signing service — new `wsdbagent` op `SIGN_JWT`

The point of the on-prem-signs decision is: **the JWT private key never appears on the cloud, in any form.** Not in env vars, not in memory, not in a config var, not on disk. The cloud only ever holds the public key (for the JWKS endpoint and id_token verification). To produce a signature, the cloud sends the JWS to-be-signed bytes through `wsdbagent`; the on-prem side reads the private key from MongoDB, signs, and returns just the signature bytes.

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
  "op":     "SIGN_JWT",
  "reqid":  42,
  "kid":    "current",                  // or a specific key id
  "alg":    "RS256",
  "to_sign": "<base64url-header>.<base64url-payload>"
}
```

Response:
```json
{
  "reqid":     42,
  "ok":        true,
  "signature": "<base64url-signature>",
  "kid":       "kid-2026-05-23"         // the key actually used (so cloud can stamp the right kid in the header)
}
```

On error (no active key, bad alg, etc.): `{ ok: false, error: "<reason>" }` — same shape as other ops.

### Cloud side — `WsMongodbProxy::sign_jwt`

```cpp
// modules/module/wsdbproxy/inc/wsdbproxy.hpp
class WsMongodbProxy : public IMongodbClient {
public:
  // ... existing methods ...
  SignJwtResult sign_jwt(const std::string &kid_hint,
                         const std::string &alg,
                         const std::string &to_sign);
};

struct SignJwtResult {
  bool        ok = false;
  std::string error;
  std::string signature;   // base64url
  std::string kid;
};
```

Adds one method to the proxy; reuses the existing BSON encode/decode + WebSocket framing. Same pattern as every other DB op.

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
1. Read `idp_signing_keys` by `kid` (or the active one if `kid == "current"`).
2. Parse PEM, load private key with `EVP_PKEY` (OpenSSL — already linked).
3. SHA-256 the `to_sign` bytes; sign with RSA-PKCS#1 v1.5 (RS256).
4. Base64url the signature; return.

No new dependencies — OpenSSL is already linked into `wsdbagent` for InnerTLS.

### The `idp_signing_keys` collection

```json
{
  "_id":           "kid-2026-05-23-7f3a",       // also the JWT kid
  "alg":           "RS256",
  "privateKeyPem": "-----BEGIN PRIVATE KEY----- ...",
  "publicKeyPem":  "-----BEGIN PUBLIC KEY----- ...",
  "createdAt":     { "$date": "2026-05-23T..." },
  "notAfter":      { "$date": "2027-05-23T..." },  // when this key stops being eligible for *signing* (verification continues until token expiry)
  "active":        true                          // exactly one doc has active: true at any time
}
```

- **One active key at a time** for signing. Multiple non-active keys may exist to keep verifying tokens still in flight (their `kid` is in JWKS until `notAfter` + max_token_ttl).
- **Generated and rotated by the on-prem Vaadin admin** (new view, see §6). Cloud never sees or generates private key material.

---

## 3. The OIDC endpoints (cloud uniservice)

All under `/api/v1/idp/`, wired into `process_request()` with the existing URI-prefix dispatch pattern.

| Method | Path | Purpose |
|--------|------|---------|
| `GET`  | `/api/v1/idp/.well-known/openid-configuration` | discovery document — issuer URL, all endpoint URLs, supported claims/scopes/algs |
| `GET`  | `/api/v1/idp/jwks` | JSON Web Key Set — public keys (one per non-expired entry in `idp_signing_keys`) |
| `GET`  | `/api/v1/idp/authorize` | authorization endpoint — validates the request, redirects to `/sso/login` or issues a code |
| `POST` | `/api/v1/idp/login` | credential verification — invoked by the login portal form; on success sets `xpmile_idp_session` and redirects back to `/api/v1/idp/authorize` |
| `POST` | `/api/v1/idp/token` | token endpoint — exchanges code (+ PKCE verifier) for id_token + access_token. Calls `sign_jwt` over wsdbagent. |
| `GET`  | `/api/v1/idp/userinfo` | (optional) returns user claims for a valid access_token |
| `POST` | `/api/v1/idp/end_session` | RP-initiated logout — revokes IdP session, deletes `xpmile_idp_session`, redirects to `post_logout_redirect_uri` |
| `POST` | `/api/v1/idp/password-reset/start` | initiate password reset — invoked by `/sso/password-reset`; sends email |
| `POST` | `/api/v1/idp/password-reset/confirm` | confirm password reset — invoked by `/sso/password-reset/confirm`; updates the hashed password |

### Issuer URL

Proposed: `https://<cloud-host>/api/v1/idp` — keeps the IdP scoped under `/api/v1/` so the rest of the URL space (notably `/` for the SPA) stays the SPA's. The discovery document then lives at `https://<cloud-host>/api/v1/idp/.well-known/openid-configuration`, which a standard OIDC client will check by appending `.well-known/openid-configuration` to the issuer URL. **Open question in §11.**

### Discovery document shape

```json
{
  "issuer":                                  "https://marvel-3a78bd953f5f.herokuapp.com/api/v1/idp",
  "authorization_endpoint":                  ".../api/v1/idp/authorize",
  "token_endpoint":                          ".../api/v1/idp/token",
  "jwks_uri":                                ".../api/v1/idp/jwks",
  "userinfo_endpoint":                       ".../api/v1/idp/userinfo",
  "end_session_endpoint":                    ".../api/v1/idp/end_session",
  "response_types_supported":                ["code"],
  "subject_types_supported":                 ["public"],
  "id_token_signing_alg_values_supported":   ["RS256"],
  "token_endpoint_auth_methods_supported":   ["client_secret_post"],
  "scopes_supported":                        ["openid", "email", "profile"],
  "claims_supported":                        ["sub", "iss", "aud", "exp", "iat", "nonce", "email", "name", "role"],
  "code_challenge_methods_supported":        ["S256"]
}
```

Static (or near-static) — built from `sso_config`'s in-house IdP entry + `publicBaseUrl`. Served with a short Cache-Control so config changes propagate.

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

Cloud-side: cache the JWKS (built from `idp_signing_keys` public-key fields) for ~60 s with hot-reload — same pattern as `sso_config`'s hot-reload thread.

### `/authorize` (the main flow)

Inputs (query string per OIDC):

`response_type=code` (only value supported) ·
`client_id=<registered-id>` ·
`redirect_uri=<exact-registered>` ·
`scope=openid [email] [profile]` ·
`state=<rp-csrf-token>` ·
`nonce=<rp-replay-defense>` ·
`code_challenge=<base64url-S256-of-verifier>` ·
`code_challenge_method=S256`

Behavior:

1. Look up `client_id` in `idp_clients`. Reject if unknown.
2. Validate `redirect_uri` is **exactly** in the client's registered list. (No prefix matching.)
3. Validate `response_type == "code"`, `code_challenge_method == "S256"`, `scope` contains `"openid"`.
4. Check for `xpmile_idp_session` cookie.
   - **No / invalid session** → persist the authorization request in an `idp_pending_auth` document keyed by a random `req_token`; set a short-lived `xpmile_idp_pending` cookie with that `req_token`; redirect (302) to `/sso/login`.
   - **Valid session** → resolve to the user; go to step 5.
5. Generate an authorization `code` (32 bytes CSPRNG, base64url). Persist in `idp_codes` collection: `{ _id: code, client_id, user_sub, redirect_uri, nonce, code_challenge, scope, expiresAt: now+30s }`.
6. Redirect (302) to `<redirect_uri>?code=<code>&state=<state>`. Done.

### `/login` (the credential check)

Invoked by the branded portal form. Validates the `xpmile_idp_pending` cookie (so we know which authorization request to resume), authenticates the credentials against the on-prem `account` collection (reusing the existing password verification path), creates an `xpmile_idp_session` cookie (HttpOnly, Secure, SameSite=Lax, `Path=/api/v1/idp/`), and redirects (302) back to `/api/v1/idp/authorize?...` with the original parameters from the pending-auth document. The user finishes the flow exactly as if they'd had a session to start with.

### `/token`

Inputs (form-urlencoded body per OIDC):

`grant_type=authorization_code` ·
`code=<from /authorize>` ·
`redirect_uri=<must match what was registered with the code>` ·
`client_id` + (optional) `client_secret` if the client is confidential ·
`code_verifier=<PKCE verifier matching the stored challenge>`

Behavior:

1. **Atomically claim** the code from `idp_codes` (a guarded `update_collection` with filter `consumed: {$exists: false}` and `$set: { consumed: true }` — same atomic primitive `next_awbno` uses). Reject on miss / replay / expiry.
2. Validate PKCE: `S256(code_verifier) == stored.code_challenge`.
3. Validate `client_id` (+ `client_secret` for confidential clients).
4. Validate `redirect_uri` matches the value stored with the code.
5. Build the id_token header + payload:
   - header: `{ alg: "RS256", typ: "JWT", kid: <current-kid> }`
   - payload: `{ iss, sub: <accountCode>, aud: <client_id>, exp: <now+1h>, iat: <now>, nonce: <stored>, email, name, role }`
6. base64url(header) + "." + base64url(payload) → `to_sign`.
7. `WsMongodbProxy::sign_jwt(kid="current", alg="RS256", to_sign)` → signature.
8. id_token = `to_sign + "." + signature`. access_token = random opaque 32 bytes (stored in `idp_access_tokens` collection with TTL).
9. Respond JSON: `{ access_token, token_type: "Bearer", expires_in: 3600, id_token, scope: <granted> }`.

### `/jwks` reuses the JWKS-cache thread

The same background thread that hot-reloads `sso_config` for the *consumer* side (v1.0) also hot-reloads `idp_signing_keys` for the *producer* side. One thread, two responsibilities, both ~60 s polling.

### `/userinfo` (optional but standard)

Validates the `Authorization: Bearer <access_token>` header against the `idp_access_tokens` collection; returns the user's claims as JSON. Useful for v2; v1 includes it for protocol completeness but the xpmile SPA doesn't need it (claims live in the id_token).

### `/end_session`

Validates the optional `id_token_hint` (just to identify the user; not strictly required), deletes the `xpmile_idp_session` cookie + the row in `sessions`, and redirects to `post_logout_redirect_uri` (must be in the client's registered list).

---

## 4. The branded Angular login portal — `/sso/login`

A new Angular route, served as a static asset by the existing Angular dist. **Not** an SPA route in the existing routing module — it's a separate, standalone page so it can be the IdP's UI (which conceptually serves *any* RP, not just the xpmile SPA itself).

Components:

- `SsoLoginPortalComponent` (`/sso/login`):
  - Xpmile logo (large, centered).
  - Username + password form.
  - "Forgot password?" link → `/sso/password-reset`.
  - On submit: POST `/api/v1/idp/login` with `{user, pass}` + the `xpmile_idp_pending` cookie.
  - On success: backend returns 302 to `/api/v1/idp/authorize?...`. Browser follows it; ends at the RP's `redirect_uri`.
  - On failure: backend returns 401 with a JSON error; component shows an inline error message.

- `SsoPasswordResetComponent` (`/sso/password-reset` and `/sso/password-reset/confirm?token=`):
  - "Enter your email" → POST `/api/v1/idp/password-reset/start`. Always succeeds (no enumeration). Shows a "check your email" message regardless.
  - The link in the email lands at `/sso/password-reset/confirm?token=...`. Component validates the token (via a `/api/v1/idp/password-reset/check?token=` probe), shows new-password form, POSTs to `/api/v1/idp/password-reset/confirm`. On success, redirects to `/sso/login` with a success flash.

The portal is **deliberately disconnected from the SPA's authenticated UI**. No navbar, no app chrome. Just the xpmile brand and the form. That keeps it usable by any future RP without leaking xpmile-app UX into other apps' login experience.

---

## 5. Password reset flow

Three collections involved:

- `account` — existing. Holds `passwordHash` (already hashed today per the answer in design intake).
- `password_resets` — new. `{ _id: <random-32B-base64url>, accountCode, expiresAt }` with a TTL index on `expiresAt`.
- `sessions` — existing (v1.0). Logging in after a reset mints a fresh session as usual.

Flow:

1. User → `/sso/password-reset` → enters email.
2. `POST /api/v1/idp/password-reset/start` with `{ email }`:
   - Look up account by email.
   - **Same response whether found or not** (no enumeration leak).
   - If found: generate token, insert `password_resets` doc (TTL 30 min), send email via the existing email module.
   - Email body: a short message + a link to `https://<cloud-host>/sso/password-reset/confirm?token=<token>`.
3. User clicks link → `/sso/password-reset/confirm?token=...` → enters new password (twice for confirmation).
4. `POST /api/v1/idp/password-reset/confirm` with `{ token, new_password }`:
   - Look up `password_resets` by token; reject if missing / expired / already consumed.
   - Hash the new password (using the same scheme the rest of the app uses); `update_collection` the account.
   - Delete the `password_resets` doc.
   - Optionally: invalidate all existing `sessions` for this account (force re-login). v1: yes — a password reset should kill stale sessions.
5. Redirect to `/sso/login` with a success flash.

Reuses: the existing email module (SMTP::User FSM), the existing password-hashing function, the existing session-revocation code (`SessionManager::revoke`).

---

## 6. Vaadin admin views (on-prem)

Two new views in the on-prem Vaadin app (`onprem/src/main/java/com/xpmile/onprem/ui/idp/`):

- **`IdpSigningKeysView` (`/idp-keys`)** — list keys (kid, alg, createdAt, notAfter, active flag); "Generate new key" button (creates a fresh RSA-2048 keypair in Java, stores both halves in `idp_signing_keys`, optionally activates it and deactivates the previous one); "Deactivate" / "Delete expired" actions.
- **`IdpClientsView` (`/idp-clients`)** — list registered RP clients; per-client fields: `client_id`, `client_name`, `redirect_uris[]`, `post_logout_redirect_uris[]`, `client_secret` (write-only, hashed in storage), `grant_types[]` (initial: just `authorization_code`), `scopes[]`. The xpmile SPA gets a pre-seeded client.

Both views write directly to MongoDB, no internet-facing config-write endpoint — same trust model as `SsoConfigView`.

---

## 7. Coexistence with v1.0 federated SSO

The in-house IdP appears as **one more entry in the existing `sso_config` collection**, with `protocol: "oidc"`:

```jsonc
{
  "publicBaseUrl": "https://marvel-3a78bd953f5f.herokuapp.com",
  "providers": [
    {
      "id":          "xpmile",                                       // the in-house IdP
      "displayName": "xpmile login",
      "protocol":    "oidc",
      "issuer":      "https://marvel-3a78bd953f5f.herokuapp.com/api/v1/idp",
      "clientId":    "xpmile-spa",                                   // the SPA's client_id
      "clientSecret":"...",                                          // the SPA's client_secret
      "scopes":      ["openid", "email", "profile"],
      "defaultRole": "Customer",
      "allowedEmailDomains": []                                      // unrestricted — these are our own users
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

**The v1.0 `OidcProvider` client code handles the in-house IdP identically to Okta** — discovery fetch + token exchange + JWT verify against JWKS. No client-side changes. The login page lists all configured providers; the in-house "xpmile login" sits alongside any external ones the customer has set up.

If a customer wants the in-house IdP to be the *only* option, they just remove the others from `sso_config`. If they want it default, they put it first in the array. If they want it absent, they don't list it (`sso_config` providers array can be `[]`, in which case only password login works — same as today).

---

## 8. Reuse map (what the existing code gives us)

| Need | Existing code |
|------|---------------|
| HTTP request parsing | `Http` parser (`modules/module/http/`) |
| HTTP response building (200 / 302 / 401) | `MicroService::build_responseOK`, `build_redirect`, `build_responseERROR`, `attach_set_cookie` |
| Routing for the new `/api/v1/idp/*` endpoints | `MicroService::process_request` URI-prefix dispatch |
| JSON serialisation everywhere | `nlohmann::json` (vendored) |
| Cookie issue/parse | `sso_cookie.*` (extend with the IdP cookie name) |
| Session storage + LRU cache + middleware | `sso_session.*` (a second `SessionManager` instance, scoped to the IdP cookie + collection) |
| CSPRNG / base64url codecs | `sso_util.*` |
| JWT **verify** (the v1.0 client side checks this against the JWKS) | `sso_jwt.*` (unchanged) |
| JWT **sign** (the new bit) | extend `sso_jwt.*` with a thin signer that delegates the signature step to `WsMongodbProxy::sign_jwt` |
| OIDC client to consume the in-house IdP | `OidcProvider` + `sso_endpoints.*` (unchanged — the SPA federates against the in-house IdP the same way it federates against Okta) |
| Hot-reload of config / JWKS | reuse the existing `sso_config` hot-reload thread (extend to also poll `idp_signing_keys`) |
| Outbound HTTP client (for the SPA's discovery fetch against the in-house IdP) | `sso_http_client.*` (unchanged — happens to make a request back to the same host, which is fine) |
| MongoDB access from cloud (always via the on-prem agent) | `IMongodbClient` / `WsMongodbProxy` |
| WebSocket DB proxy protocol | `dbproto` + `wsframe` — extended with the new `SIGN_JWT` op |
| Inner-TLS encryption of the wsdbagent tunnel | `security/innertls.*` (unchanged) |
| Password verification | the existing `handle_account_login_POST` path (factor out the credential-check so both the legacy login *and* `/api/v1/idp/login` call the same function) |
| Email delivery for the reset flow | `SMTP::User` (the existing email module) |
| Admin UI for keys + clients | the existing Vaadin app (`onprem/`) — same patterns as `SsoConfigView` |
| Branded login UI | the existing Angular `ui/src/app/login/` patterns (Clarity, FormBuilder, etc.) — add a new component, don't fork |

What's genuinely new — and the scope of the implementation work:

1. The `SIGN_JWT` op end-to-end (cloud-side proxy method, wsdbagent dispatcher, dbproto schema, tests).
2. The OIDC endpoint handlers in `webservice.cpp` (or a new `inhouse_idp.cpp` to keep `webservice.cpp` from growing) — ~8 handler functions.
3. The `idp_clients`, `idp_codes`, `idp_access_tokens`, `password_resets`, `idp_signing_keys`, `idp_pending_auth` collections (TTL indexes on the time-bound ones, seeded by `mongo-init.js`).
4. The two Vaadin admin views (`IdpSigningKeysView`, `IdpClientsView`).
5. The two Angular components (`SsoLoginPortalComponent`, `SsoPasswordResetComponent`) + routing.

---

## 9. Security analysis

| Threat | Mitigation |
|--------|------------|
| **Authorization-code interception** | PKCE (`S256`) required on every `/authorize`; stolen `code` is useless without the verifier, which never leaves the RP. |
| **Code replay** | Codes are one-time use (atomic `update_collection` to mark consumed) and short-lived (TTL 30 s in `idp_codes`). |
| **CSRF on `/authorize` callback** | The RP's `state` round-trips and is validated by the v1.0 client code (unchanged). |
| **id_token replay** | `nonce` is bound to the authorization request and embedded in the id_token; the v1.0 client verifies it (unchanged). |
| **JWT algorithm-confusion** | Server signs only RS256 (no per-request alg negotiation); JWKS only advertises RS256; client (v1.0) already enforces an `alg` allow-list. |
| **Private-key compromise on the cloud** | **The private key is never on the cloud.** Cloud holds only the public key. Compromise of the cloud (read of disk, env, RAM) yields no signing capability. |
| **Forged signing key** | Public keys are served from `idp_signing_keys`, written only by the on-prem Vaadin admin. The RP fetches them through HTTPS to a URL pinned by the issuer config. The RP's TLS verifies the cloud's cert; the in-house IdP's JWKS is no more forgeable than any other cloud endpoint. |
| **Open redirect via `redirect_uri`** | `redirect_uri` is matched **exactly** against the client's registered list (no prefix / wildcard / case-insensitive matching). |
| **Open redirect via `post_logout_redirect_uri`** | Same exact-match rule. |
| **Open redirect via password-reset link** | The link is fixed to `/sso/password-reset/confirm?token=...` on the IdP host; no externally-controlled redirect target. |
| **Password-reset token replay / leak via logs** | Tokens are CSPRNG, single-use (deleted on consumption), short TTL (30 min); stored hashed (not in plaintext) in `password_resets` to limit log-exposure damage. |
| **Email enumeration on password reset** | Same response (and same timing, as far as feasible) whether the email exists or not. |
| **Session cookie theft via XSS** | `HttpOnly` + `Secure` + `SameSite=Lax` on `xpmile_idp_session` and `xpmile_idp_pending`. |
| **`Host`-header poisoning of issuer URL / redirect URL** | Both pinned to `publicBaseUrl` from `sso_config` (same rule as v1.0); never derived from request headers. |
| **Brute-force on `/api/v1/idp/login`** | Initial: rely on Heroku's app-level rate limiting + a per-account exponential backoff stored in the account doc (open question §11; minimal v1 ships with backoff only). |
| **Brute-force on `/api/v1/idp/password-reset/start`** | Stateless rate-limit per requester IP (in-memory ring buffer) + same-response policy (an attacker can't tell which addresses exist). |
| **Tampered authorization request between `/authorize` and `/login`** | The pending request is stored server-side in `idp_pending_auth`, keyed by an opaque `req_token` cookie. The browser only carries the cookie; the request parameters are not in the URL after the redirect. |

---

## 10. Phased delivery

All seven phases ship in v1; the phasing is a build order.

| Phase | Scope |
|-------|-------|
| **A. On-prem signing service** | New `DbOp::SIGN_JWT`, `WsMongodbProxy::sign_jwt`, wsdbagent dispatcher, `idp_signing_keys` collection seed. Unit-testable end-to-end through `MockWsMongodbProxy` on the cloud side and a `MockMongodbClient`-backed dispatcher on the wsdbagent side. |
| **B. Signing-key admin** | `IdpSigningKeysView` Vaadin view — generate / activate / list / deactivate / delete-expired. Key generation in Java via `KeyPairGenerator`; PEM-encode both halves; insert. |
| **C. JWKS + discovery endpoints** | Static-ish responses driven by the hot-reloaded `idp_signing_keys` + `sso_config` `publicBaseUrl`. Unit tests against a `MockMongodbClient`. |
| **D. `/authorize` + `/login` + IdP session** | Full happy path: pending-auth storage, login form POST, credential check, session cookie issuance, code generation, redirect. Negative paths: bad client_id, bad redirect_uri, bad credentials, replayed pending request. |
| **E. `/token` (with `SIGN_JWT` round-trip)** | Code consumption, PKCE validation, id_token assembly, JWT signing via wsdbagent, response. End-to-end test that the resulting token verifies under the JWKS endpoint's keys. |
| **F. Angular login portal** | `SsoLoginPortalComponent`, routing, error handling. Manual verification (the project has no Karma setup, per the v1.0 SSO design's Phase D). |
| **G. Password reset** | `password_resets` collection, the two backend endpoints, the two email templates, `SsoPasswordResetComponent`. Manual + unit tests on the backend pieces. |

`/userinfo` and `/end_session` are part of Phase E (small).

The on-prem Vaadin `IdpClientsView` ships in Phase D (we need at least one registered client to exercise `/authorize`).

---

## 11. Decisions and open questions

### Resolved (from the design intake)

- **In-house IdP is a full OIDC provider** (Authorization Code + PKCE), single registered RP at v1, multi-RP-architected.
- **Coexists with v1.0 federated SSO** — appears as one more entry in `sso_config`.
- **v1 auth features:** username + password + self-service password reset. No MFA, no self-registration.
- **JWT signing happens on-prem** via a new `wsdbagent` SIGN_JWT op; the private key never leaves the on-prem MongoDB.
- **Passwords are already hashed** in `account.passwordHash` (per design intake); no migration needed.

### Still open

1. **Issuer URL placement.** Proposed `https://<cloud-host>/api/v1/idp` — keeps the IdP under the existing `/api/v1` namespace. Alternative: at the root (`https://<cloud-host>`), shorter but takes over the root namespace. Or on a subdomain (`https://idp.<cloud-host>`) — needs DNS work. Default: scoped under `/api/v1/idp`.
2. **`idp_clients` storage vs `sso_config`.** Two options: (a) separate `idp_clients` collection with one document per registered client — cleaner separation, room for many clients; (b) inline into the in-house provider entry in `sso_config` — fewer moving parts, fine while there's exactly one client. Default: separate collection.
3. **Brute-force protection on `/login`.** v1 lean: per-account exponential backoff stored in the account doc (`lastFailedAt`, `failedCount`). v1 thorough: separate `auth_attempts` collection + IP-level rate limit. Default: minimal (exponential backoff only).
4. **Refresh tokens.** Not in v1 (sessions handle the "stay logged in" UX via the RP-side `xpmile_session` cookie). Worth confirming.
5. **Key rotation cadence.** Manual via Vaadin only for v1; no scheduler. Confirm.
6. **`/userinfo` scope.** Include in v1 for protocol completeness (returns claims for an opaque access_token) — but the xpmile SPA doesn't need it (claims live in the id_token). Confirm "build it but don't use it" is fine.
7. **Account lockout policy.** v1: no hard lockout; just exponential backoff (so the legitimate user can still try again after the wait). Vaadin Accounts view gets a "reset failed attempts" button.
8. **Logging out at the IdP vs the RP.** When the xpmile SPA's `POST /api/v1/sso/logout` fires (existing v1.0 code), should we also call the IdP's `/end_session`? For the in-house IdP this would clear the `xpmile_idp_session`, meaning the user has to re-enter credentials next login (rather than silently re-authing via the existing IdP session). v1 default: yes — call `/end_session` for the in-house provider on SPA logout (matches user expectation that "log out" means "log out").

---

## 12. Files — new and changed

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
| `inc/idp_session.hpp` / `src/idp_session.cpp` | Second `SessionManager` instance scoped to the IdP cookie |
| `test/inhouseidp_test.cc` | Unit tests (see TDD plan) |

**Extended existing modules:**

| File | Change |
|------|--------|
| `modules/module/sso/inc/sso_jwt.hpp` / `src/sso_jwt.cpp` | Add `make_jwt_unsigned(header, payload) → to_sign` and `assemble_jwt(to_sign, signature) → string` helpers |
| `modules/module/wsdbproxy/inc/wsdbproxy.hpp` / `src/wsdbproxy.cpp` | Add `WsMongodbProxy::sign_jwt()` (cloud side of the new op) |
| `modules/module/wsdbproxy/inc/dbproto.hpp` / `src/dbproto.cpp` | Add `DbOp::SIGN_JWT` + request/response BSON schema |
| `modules/module/wsdbagent/src/wsdbagent.cpp` | Add `SIGN_JWT` case in the dispatcher; new `sign_jwt_on_prem()` helper |
| `modules/module/webservice/inc/webservice.hpp` / `src/webservice.cpp` | Route `/api/v1/idp/*` to a new `handle_idp` adapter (mirrors `handle_sso`) |
| `modules/module/webservice/src/webservice.cpp` | Extract credential-check into a reusable helper; have both `handle_account_login_POST` and `handle_idp_login_POST` call it |
| `docker/mongo-init.js` | Create the six new collections (`idp_signing_keys`, `idp_clients`, `idp_codes`, `idp_access_tokens`, `idp_pending_auth`, `password_resets`) with the appropriate TTL indexes |
| `CMakeLists.txt` | `add_executable(uniservice ...)` globs the new `inhouseidp` module |
| `test/CMakeLists.txt` | Add the new `inhouseidp` test sources |

**Angular:**

| File | Change |
|------|--------|
| `ui/src/app/sso-login-portal/sso-login-portal.component.{ts,html,css}` (new) | Branded login portal at `/sso/login` |
| `ui/src/app/sso-password-reset/sso-password-reset.component.{ts,html,css}` (new) | Two-step password reset at `/sso/password-reset` and `/sso/password-reset/confirm` |
| `ui/src/app/app-routing.module.ts` | Add the two new routes (outside the auth-guarded set — they're public IdP UI) |
| `ui/src/common/httpsvc.service.ts` | Add `idpLogin()`, `idpPasswordResetStart()`, `idpPasswordResetConfirm()` |
| `ui/src/assets/images/` | The xpmile logo asset for the portal (likely already there) |

**On-prem Vaadin:**

| File | Change |
|------|--------|
| `onprem/.../ui/idp/IdpSigningKeysView.java` (new) | Admin view: generate / list / activate / delete signing keys |
| `onprem/.../ui/idp/IdpClientsView.java` (new) | Admin view: register / list / edit RP clients |
| `onprem/.../service/IdpSigningKeyService.java` (new) | MongoDB CRUD for `idp_signing_keys`; RSA-2048 keypair generation in Java |
| `onprem/.../service/IdpClientService.java` (new) | MongoDB CRUD for `idp_clients` |
| `onprem/.../ui/MainLayout.java` | Add the two new side-nav items |

**Docs:**

| File | Change |
|------|--------|
| `codebase.md` | New section for the `inhouseidp` module and the new `DbOp::SIGN_JWT`; new collections table rows |
| `CLAUDE.md` | New section "In-house IdP" — working conventions for the new module |
| `docs/design/sso/sso-design.md` | One sentence linking forward: "An in-house IdP is also available; see `inhouse-idp-design.md`." |
| `docs/operator-guide.md` (on release/v1.0) | New section on configuring the in-house IdP, signing-key rotation, registering an RP |

**CI workflow:**

No new path-filter entries needed — `modules/**`, `CMakeLists.txt`, `test/**`, `ui/**`, `docker/mongo-init.js`, and `onprem/**` are all already covered (or implicitly covered via the modules they sit under).

---

## 13. Risks and known limitations

- **The on-prem signing round-trip adds latency to token issuance.** Tokens are issued at login + (with refresh) at refresh — not per-request. Empirically the wsdbagent round-trip is a few ms on a healthy connection. Acceptable, but worth measuring once Phase E is up.
- **wsdbagent reconnect during a `SIGN_JWT` round-trip** would surface as a transient `/token` failure. The RP retries cleanly via the standard OIDC error path (`temporarily_unavailable` → user gets a "try again" page). Frequency should be very low (only during deploys).
- **Single registered client at v1 is a soft limit.** The schema is multi-client-ready (`idp_clients`); the Vaadin view is multi-client-ready; only the documentation calls out "v1 ships with one". Adding a second client is purely configuration.
- **Brute-force resistance is minimal in v1** (exponential backoff only). A determined attacker with a botnet could still test passwords faster than a single source. Phase 2 should add an `auth_attempts` collection with per-IP limits and a real lockout policy.
- **No MFA.** Federated customers can require MFA at their IdP; in-house customers can't until we ship MFA. Tracked as the largest follow-up.
- **The IdP cookie is scoped to `/api/v1/idp/`**, so the SSO experience across multiple RPs requires all RPs to redirect through the same IdP host. For different-domain RPs that's not an issue; for same-domain different-path RPs it's actually the right scope. Worth re-checking when the second RP shows up.
