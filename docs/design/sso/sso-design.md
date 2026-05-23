# Design: Single Sign-On (SSO)

> Status: **Implemented** — phases A–F shipped via PR #18. The code lives in
> `modules/module/sso/` (the `sso::` namespace); `sso-tdd-plan.md` has the
> test plan. This document is retained as the SSO architecture reference.

## Problem summary

Today the xpmile cloud app authenticates with a username + password against the
`account` collection. There are three gaps that SSO must address, and one
structural gap SSO forces us to fix:

| # | Gap | Impact |
|---|-----|--------|
| 1 | No federated login | Customers/operators cannot use their corporate identity (Google, Entra ID, Okta). Every user is a separate password in our DB. |
| 2 | No server-side session | `POST /api/v1/account/login` returns the account document; the SPA holds it in an in-memory RxJS subject. A page reload logs the user out. There is no token, cookie, or session record. |
| 3 | No route protection | Angular has no route guard and no HTTP interceptor — every route is public. The C++ backend has no auth middleware — every `/api/v1/*` endpoint is callable unauthenticated. |
| 4 | No outbound HTTP client | The C++ backend cannot call an external IdP. An empty `modules/module/sso/` stub exists but is unimplemented. |

SSO cannot be bolted on without a real session mechanism, so this design
introduces server-side sessions as its foundation and makes both SSO **and** the
existing password login mint the same session.

## Goals

- Federated login via **OIDC** (generic, discovery-driven) and **SAML 2.0**.
- A **backend-for-frontend (BFF)** model: the C++ backend runs the entire
  IdP handshake; IdP tokens never reach the browser.
- Server-side sessions backed by an opaque `HttpOnly` cookie.
- **Hybrid provisioning**: an SSO identity is matched to an existing `account`
  by email; if none exists, an account is just-in-time (JIT) created.
- Password login keeps working — SSO is additive, not a replacement.
- One provider abstraction so OIDC and SAML plug in behind the same interface,
  and a third protocol could be added later without touching callers.

## Non-goals

- SSO for the on-prem Vaadin operator console. Per the project's design that
  console is intentionally unauthenticated behind physical access controls; it
  is out of scope here.
- SCIM / automated user de-provisioning. Account lifecycle stays manual.
- Multi-factor authentication beyond whatever the IdP itself enforces.
- IdP-initiated SSO (SAML unsolicited `Response`). Phase 1 is SP-initiated only.

---

## 1. Architecture — Backend-for-Frontend

The C++ backend is the OAuth/SAML client. The browser only ever talks to
xpmile; it never sees an IdP token.

```
 Browser (Angular)            xpmile C++ backend                IdP
   │                                │                            │
   │  GET /api/v1/sso/login         │                            │
   │  ?provider=corp&return_to=/main│                            │
   │ ──────────────────────────────►│                            │
   │                                │  create sso_transaction    │
   │                                │  (state, nonce, PKCE)      │
   │  302 Location: <IdP authz URL> │                            │
   │ ◄──────────────────────────────│                            │
   │                                                             │
   │  ───────────────── user authenticates at IdP ──────────────►│
   │                                                             │
   │  GET /api/v1/sso/callback/corp │                            │
   │  ?code=…&state=…               │                            │
   │ ──────────────────────────────►│                            │
   │                                │  exchange code for tokens  │
   │                                │ ──────────────────────────►│
   │                                │ ◄──────────────────────────│
   │                                │  verify id_token / assertion│
   │                                │  resolve account (hybrid)   │
   │                                │  create session record     │
   │  302 Location: /main           │                            │
   │  Set-Cookie: xpmile_session=…  │                            │
   │ ◄──────────────────────────────│                            │
   │                                │                            │
   │  GET /api/v1/sso/session       │  (cookie sent automatically)│
   │ ──────────────────────────────►│  look up session → account │
   │  200 { account… }              │                            │
   │ ◄──────────────────────────────│                            │
```

Two backend round trips to the IdP — the discovery/JWKS fetch and the token
exchange — require an **outbound HTTPS client** the codebase does not have
today (see §6).

### Why BFF over SPA-side

- IdP `access_token` / `refresh_token` / `client_secret` stay server-side. A
  compromised browser cannot leak them.
- Session revocation is a single DB write — no waiting for a JWT to expire.
- The SPA change is minimal: redirect links plus one guard. No OAuth library
  in the bundle.

The cost is real C++ work: an outbound HTTPS client, a session store, cookie
handling, and SAML's XML signature verification. That cost is accepted.

---

## 2. Server-side sessions

### Session record

A new MongoDB collection `sessions`:

```json
{
  "_id": "<sid>",                       // 256-bit random, base64url — the cookie value
  "accountCode": "acme-ops",            // FK into the account collection
  "authMethod": "oidc",                 // "oidc" | "saml" | "password"
  "provider": "corp",                   // configured provider id; "" for password
  "subject": "auth0|abc123",            // IdP-stable user id (sub / NameID)
  "createdAt": { "$date": "…" },
  "lastSeenAt": { "$date": "…" },
  "expiresAt": { "$date": "…" },        // TTL index drops the doc at this time
  "idpRefreshToken": "…"                // optional, encrypted at rest; SSO only
}
```

A **TTL index on `expiresAt`** lets MongoDB expire stale sessions for free. The
session works in `--remote-db` mode unchanged — it is an ordinary collection
reached through `WsMongodbProxy` → `wsdbagent` like any other.

### Cookie

```
Set-Cookie: xpmile_session=<sid>; HttpOnly; Secure; SameSite=Lax; Path=/; Max-Age=43200
```

- **Opaque** random id, **not** a JWT — the server looks it up, so the session
  is revocable and carries no client-readable claims.
- `HttpOnly` — unreadable from JavaScript (XSS cannot exfiltrate it).
- `Secure` — HTTPS only. Heroku terminates TLS and sets `X-Forwarded-Proto:
  https`; the flag is always set in deployed environments.
- `SameSite=Lax` — sent on the top-level GET redirect back from the IdP, but
  withheld from cross-site POSTs (baseline CSRF defense — see §11). Note this
  means the SAML ACS **POST** callback does *not* carry any pre-existing
  `xpmile_session` cookie; that is fine, because a callback establishes a
  session rather than relying on one.
- **Lifetime is absolute**: `Max-Age` and the session record's `expiresAt` are
  the same 12 h value. If a *sliding* window is adopted (open question §14 Q3),
  the cookie must be **re-emitted** with a fresh `Max-Age` on refresh — a
  server-side `expiresAt` slide alone does not extend the browser cookie.

### Session validation middleware

`process_request()` gains a session step: read the `Cookie` header, extract
`xpmile_session`, look up the `sessions` collection, and if valid attach an
`AuthContext { accountCode, role, authMethod }` to the `WorkCtx`. `lastSeenAt`
is refreshed at most once per minute to avoid a write on every request. **This
step runs *after* the existing `--remote-db` 503 fast-path** (which short-
circuits when `wsdbagent` is disconnected) — session lookup needs the DB, so it
cannot precede that guard.

Because session lookup would otherwise add a `wsdbagent` round trip to every
request, an **in-process session cache** (small LRU, ~60 s TTL) sits in front
of the DB lookup. It is shared across the `MicroService` worker threads and so
needs a mutex. Two consequences the implementer must handle:

- **Logout must purge the local cache entry synchronously**, not wait for the
  TTL — otherwise a just-revoked cookie keeps working for up to 60 s.
- The cache is **per-process**. `marvel` runs a **single web dyno**
  (confirmed — §14), so the cache is coherent. If the app is ever scaled out,
  the cache must be dropped (every request hits Mongo) or moved to a shared
  store such as Redis. `sso_transactions` (PKCE/state) is deliberately
  DB-backed, not cached, so it is already multi-dyno safe regardless.

> **Scope decision needed.** *Creating* sessions is in scope. *Enforcing* a
> valid session on every existing `/api/v1/*` endpoint is a larger, riskier
> change (it can break the on-prem Vaadin console and the legacy account GET
> path). This design builds the middleware and applies it to the SSO endpoints
> and a small protected set; blanket enforcement is listed as an open question
> (§14).

### Password login becomes session-aware

`handle_account_login_POST` is extended: on a correct password it now also
creates a `sessions` record (`authMethod: "password"`) and sets the
`xpmile_session` cookie. The whole app moves to one uniform session model
regardless of how the user authenticated.

---

## 3. The `IdentityProvider` abstraction

OIDC and SAML differ in wire format but not in shape: *begin a login*, then
*consume a callback and yield identity claims*. One interface captures both;
`MicroService` only ever sees the interface.

```cpp
// modules/module/sso/inc/identity_provider.hpp
namespace sso {

struct AuthnRequest {
    std::string redirect_url;     // value for the 302 Location header
    std::string transaction_id;   // correlates the callback (state / AuthnRequest ID)
};

struct IdentityClaims {
    bool        ok = false;
    std::string subject;          // IdP-stable user id  (OIDC sub / SAML NameID)
    std::string email;
    std::string display_name;
    std::vector<std::string> groups;   // for optional group→role mapping
    std::string error;           // populated when ok == false
};

struct CallbackInput {
    std::map<std::string,std::string> query;   // OIDC: code, state
    std::map<std::string,std::string> form;    // SAML: SAMLResponse, RelayState
};

class IIdentityProvider {
public:
    virtual ~IIdentityProvider() = default;
    virtual const std::string& id() const = 0;          // configured provider id
    virtual AuthnRequest   begin_login(const std::string& return_to) = 0;
    virtual IdentityClaims handle_callback(const CallbackInput&)      = 0;
    virtual std::string    logout_url(const std::string& subject) const = 0;
};

} // namespace sso
```

A `ProviderRegistry` builds one `IIdentityProvider` per configured provider at
startup and resolves it by the `<provider>` path segment / `?provider=` query.
`OidcProvider` and `SamlProvider` are the two implementations.

> Implementation lives in `modules/module/sso/`. (Historical note: the
> initial scaffolding sat under a `modules/module/oauth2/` stub — kept
> in place during phases A–F and renamed to `sso/` afterward, per §14 Q4.)

---

## 4. OIDC provider

Generic and **discovery-driven** — no IdP-specific code. Works with any
spec-compliant provider via configuration alone.

> **Launch target: Okta.** The first real OIDC provider is an Okta org. Okta
> exposes discovery at `https://<org>.okta.com/oauth2/<authServerId>/.well-known/openid-configuration`
> (or the org server without the `oauth2/<id>` segment). To populate the
> `groups` claim Okta needs the `groups` scope plus a groups claim configured
> on the authorization server — note this when registering the app. No
> Okta-specific code is added; this only informs the config values and the
> integration test fixtures.

**Discovery.** At startup (and on a periodic refresh) fetch
`<issuer>/.well-known/openid-configuration` → `authorization_endpoint`,
`token_endpoint`, `jwks_uri`, `end_session_endpoint`. JWKS keys are fetched
from `jwks_uri` and cached; an unknown `kid` triggers a re-fetch.

**`begin_login()`** — Authorization Code flow with PKCE:

1. Generate `state` (CSRF), `nonce` (replay defense), and a PKCE
   `code_verifier`; derive `code_challenge = S256(code_verifier)`.
2. Persist an `sso_transaction` keyed by `state` (see §3 transaction store).
3. Return a redirect to `authorization_endpoint` with `response_type=code`,
   `client_id`, `redirect_uri`, `scope=openid email profile`, `state`,
   `nonce`, `code_challenge`, `code_challenge_method=S256`.

**`handle_callback()`**:

1. **Atomically claim** the `sso_transaction` by `state`: read it, then run a
   guarded `update_collection` — `$set: {consumed: true}` with a filter that
   requires `consumed` to be absent. MongoDB applies that update atomically
   per document, so a replayed or concurrent callback finds the transaction
   already consumed and is rejected. (Same atomic-document-update primitive
   `next_awbno` already uses — no new wire op for the `--remote-db` path,
   unlike a `findOneAndDelete` would need.) Reject if missing/expired.
2. `POST` to `token_endpoint` with `grant_type=authorization_code`, `code`,
   `redirect_uri`, `client_id`, `client_secret`, `code_verifier`. The provider
   must be registered at the IdP as a **confidential / web-application client**
   (it presents a `client_secret`).
3. Verify the returned `id_token` (see mandatory checks below).
4. Map claims → `IdentityClaims` (`sub`, `email`, `email_verified`, `name`,
   `groups`/`roles`).

**`id_token` verification — mandatory checks.** Every one of these is required;
skipping any is a known SSO vulnerability class:

- **`alg` allow-list.** Accept only the specific asymmetric algorithms the
  provider is configured for (e.g. `RS256`). Reject `none`, and reject `HS256`
  when an RS/ES algorithm is expected — the HMAC-vs-RSA *algorithm-confusion*
  attack lets an attacker sign a token with the public key as an HMAC secret.
- **`kid`** present and resolvable in the cached JWKS; an unknown `kid`
  triggers one JWKS re-fetch, then hard-fail.
- **Signature** verified against that JWKS key.
- **`iss`** exactly matches the configured issuer.
- **`aud`** contains `client_id`; if multiple audiences, **`azp`** equals
  `client_id`.
- **`exp`** in the future and **`iat`** not future-dated, within a small
  (~60 s) clock skew.
- **`nonce`** equals the value stored in the `sso_transaction`.

**JWT library, not hand-rolled.** Unlike the rest of the C++ work, JWT
verification should **not** be hand-rolled — algorithm confusion and skipped
claim checks are exactly where JWT CVEs live (the same caution the design
applies to SAML XML-DSig in §5). Recommendation: **`jwt-cpp`**, a header-only
C++ library that verifies against OpenSSL — no binary dependency, just a header
added to the toolchain. JWKS and the JWT payload are still `nlohmann::json`.

---

## 5. SAML 2.0 provider

SP-initiated, with the standard browser bindings:

- **AuthnRequest** → IdP via **HTTP-Redirect** binding (deflate + base64 in the
  URL), `RelayState` carries the transaction id.
- **Response** ← IdP via **HTTP-POST** binding: the browser POSTs a base64
  `SAMLResponse` form field to our Assertion Consumer Service (ACS) URL.

`handle_callback()` must:

1. Base64-decode and parse the `SAMLResponse` XML.
2. **Verify the XML-DSig signature** on the `Assertion` (or `Response`) against
   the IdP's configured signing certificate — canonicalize (C14N), check the
   digest, verify the signature.
3. Validate conditions: `Audience` = our SP entity id, `NotBefore`/
   `NotOnOrAfter` window, `Recipient` = our ACS URL, and `InResponseTo`
   **atomically consumed** from `sso_transactions` (single `findOneAndDelete`)
   so an assertion cannot be replayed.
4. Map `NameID` → `subject`, attributes → `email` / `display_name` / `groups`.

**This is the heavy part of the project.** XML-DSig is unforgiving and
hand-rolling it is a security anti-pattern. Recommended dependency:
**`libxml2` + `xmlsec1`** (the de-facto C library for XML signature
verification), added to the bootstrap toolchain image. There is no XML library
in the stack today.

Because of this cost, delivery is **phased** (§12): OIDC ships first and
end-to-end; SAML lands second behind the same `IIdentityProvider` interface,
so adding it touches no callers.

---

## 6. Outbound HTTPS client

The backend currently has no way to make an outbound HTTP request. SSO needs
one for: OIDC discovery, the OIDC token endpoint, and JWKS fetches. (SAML can
avoid it — the IdP signing cert and endpoints can be static config rather than
a fetched metadata document.)

**Built on ACE + OpenSSL — no new dependency.** `ACE_SOCK_Connector` does the
DNS + TCP connect; OpenSSL — already linked, already used by `innertls` — does
the client-side TLS. libcurl was considered and rejected: the codebase already
hand-rolls its TLS and HTTP, `ACE`/`OpenSSL` are already linked, and a libcurl
dependency would only save ~150 lines while cutting against that grain.

`sso::HttpClient` implements `IHttpClient` (`get`/`post_form`):

- **Connect.** `ACE_INET_Addr` resolves the host; `ACE_SOCK_Connector::connect`
  with an `ACE_Time_Value` gives a TCP socket with a connect timeout.
- **TLS.** A *per-connection* OpenSSL `SSL_CTX` (client) with
  `SSL_CTX_set_verify(SSL_VERIFY_PEER)` + `SSL_CTX_set_default_verify_paths()`
  — the latter verifies the chain against the **system CA bundle**
  automatically. `X509_VERIFY_PARAM_set1_host()`, set before `SSL_connect()`,
  makes OpenSSL verify the hostname *during* the handshake; SNI is set via
  `SSL_set_tlsext_host_name()`. A per-connection `SSL_CTX` deliberately avoids
  touching the process-wide `ACE_SSL_Context` singleton.
- **HTTP.** An HTTP/1.1 request with `Connection: close`; the response is read
  to EOF and handed to the project's existing `Http` parser — extended with a
  `status()` accessor for response status lines — which already decodes
  headers, chunked transfer-encoding, and gzip/deflate. No HTTP parsing is
  reimplemented. IdP well-known endpoints are served directly, so
  redirect-following is not implemented.

**Worker-thread blocking — the key concurrency concern.** The OIDC token
exchange runs *inside* `handle_callback()`, on a fixed-size `MicroService`
worker thread. A slow or hanging IdP token endpoint pins that worker for the
full request timeout; enough stalled callbacks starve the whole pool. Two
mandatory mitigations:

- **Aggressive timeouts** — single-digit-second connect *and* read timeouts
  (`ACE_Time_Value` on connect; `SO_RCVTIMEO` / a deadline on the read loop).
  A callback that can't reach the IdP fast fails fast.
- **Discovery and JWKS refresh run on a dedicated background thread**, not
  lazily on a worker. A worker only ever reads the *cached* discovery/JWKS
  data; it never makes the discovery/JWKS network call itself. (The token
  exchange is unavoidably on the worker — it is per-login — but it is bounded
  by the timeout above.)

The worker-pool size vs. expected concurrent-login rate is a tuning input the
implementer must size deliberately.

**Not unit-tested.** The networking path is verified by integration — an
actual call to the configured IdP during Phase C bring-up. Unit tests use
`MockHttpClient` behind the `IHttpClient` interface.

---

## 7. Account provisioning — hybrid

On a successful callback, `IdentityClaims` is resolved to an `account`:

1. **Match by subject.** If a `sessions`/account history already links this
   `provider` + `subject`, use that account. (Stored on the account as
   `ssoIdentities: [{ provider, subject }]` so a renamed email still resolves.)
2. **Match by email.** Otherwise look up `account` by
   `personalInfo.email == claims.email` (case-insensitive). On a hit, link the
   SSO identity to that account (append to `ssoIdentities`) and use it.
3. **JIT-create.** If still nothing, create a new `account`:
   - `loginCredentials.accountCode` — generated (reuse the existing
     `isAccountCodeAutoGen` path).
   - `personalInfo.email` / `name` from claims.
   - `personalInfo.role` — a **configurable default**, defaulting to the
     least-privileged role (`Customer`).
   - **No `passwordHash`** — the account is SSO-only until a password is set.

**Role mapping.** A matched existing account keeps its MongoDB `role` — the IdP
does not override it. For JIT accounts the role comes from the default, or, if
the provider has `groupRoleMap` configured, from the user's IdP `groups`. This
keeps authorization decisions in our DB, not scattered across IdP config.

> **`groupRoleMap` is a privilege-escalation surface.** A provider's IdP
> controls the `groups` claim. For a provider whose IdP we do **not** fully
> operate (e.g. the partner SAML case), that IdP could assert
> `groups: ["xpmile-admins"]` and self-provision an Admin account. Therefore
> `groupRoleMap` is **opt-in per provider** — it is disabled unless the
> operator explicitly configures the map, and must only be enabled for IdPs we
> fully trust. The operator-authored map *is* the policy: the IdP only asserts
> group membership; what each group grants is the operator's choice. (A
> separate ranked `roleCeiling` was considered and dropped — `personalInfo.role`
> is a free-form string with no defined hierarchy to rank against, and the
> opt-in map already bounds what an IdP can grant.)

### Account-takeover guards on the email-match path

Email-based matching trusts the IdP's `email_verified` claim, and a malicious
or misconfigured IdP can lie. Two layers defend the email-match path:

1. **`email_verified == true` required.** If the claim is absent or false, the
   user falls through to JIT-create instead of matching an existing account.
2. **Per-provider `allowedEmailDomains`.** A provider may only match (or
   JIT-create) accounts whose email is in its configured domain allow-list.
   This stops a configured-but-untrusted IdP from seizing an account in a
   domain it has no authority over. An email outside the allow-list is
   rejected outright.

---

## 8. New API endpoints

All under `/api/v1/sso/`, wired into `process_request()` with the existing
URI-prefix dispatch pattern.

| Method | Path | Purpose |
|--------|------|---------|
| `GET` | `/api/v1/sso/providers` | List configured providers `[{id, displayName, protocol}]` so the UI can render a button per provider. Public. |
| `GET` | `/api/v1/sso/login?provider=<id>&return_to=<path>` | Begin login. `302` to the IdP. |
| `GET` | `/api/v1/sso/callback/<id>` | OIDC redirect callback (`code`, `state`). On success: create session, `Set-Cookie`, `302` to `return_to`. |
| `POST` | `/api/v1/sso/callback/<id>` | SAML ACS (HTTP-POST `SAMLResponse`). Same success path. |
| `GET` | `/api/v1/sso/session` | Return the current account for the session cookie, or `401`. The SPA calls this on load to hydrate state. |
| `POST` | `/api/v1/sso/logout` | Delete the session record, purge the in-process cache entry, clear the cookie, **revoke the stored IdP refresh token** at the IdP's revocation endpoint, and optionally return the IdP `end_session_endpoint` URL for RP-initiated logout. |

`return_to` is validated as a **local path** (must start with `/`, no
scheme/host) to prevent open-redirect abuse.

The response builder needs additions: emit a `302` with a `Location` header,
and attach `Set-Cookie`. `process_request()` needs to parse the `Cookie`
request header — the `Http` class already lowercases header keys, so `cookie`
lookup is consistent.

SSO **configuration** is intentionally *not* an API endpoint here — it is
managed out-of-band by the on-prem Vaadin admin UI, which writes the
`sso_config` collection directly (§10). No public config-write surface exists.

### CORS rework — a cross-cutting prerequisite

The current response builders emit `Access-Control-Allow-Origin: *` on every
endpoint. **`*` is incompatible with credentialed requests.** Once the SPA
sends the session cookie (`withCredentials: true`, §9), the browser **rejects**
any response that combines `Access-Control-Allow-Origin: *` with credentials —
every API call would fail CORS.

This is a blocking, cross-cutting change to the shared response builders, not
an SSO-local detail:

- Replace `Access-Control-Allow-Origin: *` with the **specific allowed
  origin** echoed back (validated against an allow-list — the SPA origin, e.g.
  `https://marvel-3a78bd953f5f.herokuapp.com`, and `http://localhost:4200` for dev).
- Add `Access-Control-Allow-Credentials: true`.
- Ensure `OPTIONS` preflight responses carry the same headers.

Because it touches every endpoint, this is sequenced into Phase A (§12).

---

## 9. Angular changes

Minimal, because the backend does the handshake.

- **Login component** — call `GET /api/v1/sso/providers`; render one button per
  provider. A button navigates the browser (`window.location.href`) to
  `/api/v1/sso/login?provider=<id>&return_to=/main`. Keep the existing
  username/password form for password login.
- **Session bootstrap** — an `APP_INITIALIZER` (or the root component) calls
  `GET /api/v1/sso/session`. `200` → populate the account state; `401` →
  redirect to `/login`.
- **HTTP** — set `withCredentials: true` so the `xpmile_session` cookie rides
  on API calls. No interceptor needs to attach a token (the cookie is
  automatic), but an interceptor that redirects to `/login` on `401` is worth
  adding.
- **Route guard** — a `CanActivate` guard on protected routes (`/main`,
  `/dashboard`) that checks the hydrated session state.
- **Logout** — call `POST /api/v1/sso/logout`, clear local state, follow the
  returned IdP logout URL if present.

The post-callback `302` lands the browser back in the SPA with the cookie
already set; `GET /api/v1/sso/session` then hydrates the account.

---

## 10. Configuration & the on-prem admin UI

SSO provider config is **secret-bearing** (OIDC `clientSecret`) and changes
over a deployment's life — new providers, rotated secrets. It lives in a
MongoDB **`sso_config` collection** (a single document) — not in git, and not
in a Heroku config var that would need a redeploy to change.

### Who writes it, who reads it

```
 on-prem Vaadin admin UI       MongoDB (on-prem)        C++ backend (Heroku)
         │                       sso_config                    │
         │── edit form: write ──────▶│                          │
         │                           │◀── read + hot-reload ────│
         │                           │      (via wsdbagent)     │
```

- **Writer — the on-prem Vaadin admin UI.** The Vaadin app already runs on the
  MongoDB machine, so it writes the `sso_config` document **directly to its
  co-located MongoDB**. The secret-bearing config is authored and stored
  entirely on-prem; no internet-facing config-*write* endpoint is created.
- **Reader — the C++ backend.** It reads `sso_config` over the existing
  wsdbagent channel (inner-TLS encrypted) like any other collection, builds the
  `ProviderRegistry`, and **hot-reloads** on change.

> *Option A considered and rejected:* having the Vaadin app POST config to a
> C++ admin REST endpoint would put a security-critical, secret-bearing
> config-**write** endpoint on the public internet — which would then need its
> own service-to-service auth (a shared admin key) to be safe. The direct-write
> model above has no such endpoint to defend. (§14 records this decision.)

### The `sso_config` document

A single document holding the same JSON the `parse_sso_config` parser already
accepts — so that parser is **unchanged**; only the *source* of the JSON moves
from an env var to this collection:

```jsonc
{
  "publicBaseUrl": "https://marvel-3a78bd953f5f.herokuapp.com",
  "providers": [
    {
      "id": "corp",
      "displayName": "Acme Corp SSO",
      "protocol": "oidc",
      "issuer": "https://acme.okta.com/oauth2/default",   // Okta org auth server
      "clientId": "xpmile-prod",
      "clientSecret": "…",                 // secret — write-only from the UI
      "scopes": ["openid", "email", "profile", "groups"],
      "defaultRole": "Customer",
      "allowedEmailDomains": ["acme.com"], // email-match guard, §7
      "groupRoleMap": { "xpmile-admins": "Admin" }    // opt-in; trusted IdP only
    },
    {
      "id": "partner",
      "displayName": "Partner SAML",
      "protocol": "saml",
      "idpEntityId": "https://idp.partner.com/saml",
      "idpSsoUrl": "https://idp.partner.com/saml/sso",
      "idpSigningCert": "-----BEGIN CERTIFICATE----- …",
      "spEntityId": "xpmile-marvel",        // an identifier, NOT a URL
      "defaultRole": "Customer",
      "allowedEmailDomains": ["partner.com"]
      // no groupRoleMap — partner IdP is not fully trusted
    }
  ]
}
```

### Hot-reload

The C++ backend re-reads `sso_config` on the same background thread that
refreshes OIDC discovery/JWKS (§6) — roughly every 60 s. It hashes the
document; on a change it runs `parse_sso_config` and swaps in a freshly-built
`ProviderRegistry` **only if it parses**. Invalid config (malformed JSON,
missing `publicBaseUrl`, …) is rejected and the last-good registry is kept —
a bad edit in the Vaadin form can never take down login. An operator edit
takes effect within ~60 s, no redeploy.

### Vaadin admin view

A new admin view in the on-prem Vaadin app:

- Lists the configured providers; supports add / edit / remove.
- Per-provider fields mirror the JSON above.
- **Secrets are write-only.** `clientSecret` is never rendered back into the
  form on load — it shows a "leave blank to keep current" placeholder, and an
  empty secret field on save means "keep the stored value." `idpSigningCert`
  is a public certificate and is fine to display.
- The view does client-side validation for UX; the backend's
  parse-or-keep-last-good reload is the real safety net.

This is **not** SSO *login* for Vaadin — the on-prem console stays
unauthenticated by design, behind the customer's physical access controls. It
is an SSO *configuration* surface; see the §11 reconfiguration-risk row.

### Derived URLs are pinned to `publicBaseUrl`, not request headers

The OIDC `redirect_uri` and the SAML ACS URL are built as
`<publicBaseUrl>/api/v1/sso/callback/<id>`. They are deliberately **not**
derived from the request's `Host` / `X-Forwarded-Host` header — an
attacker-controlled `Host` header poisoning `redirect_uri` is a known SSO bug
class. `publicBaseUrl` is config; these URLs must be registered with each IdP.
Note `spEntityId` is just a stable identifier string — it does not have to be a
URL and is unrelated to the ACS URL.

---

## 11. Security analysis

| Threat | Mitigation |
|--------|------------|
| **CSRF on login** (forged authz response) | OIDC `state` is random, single-use, bound to the `sso_transaction`; mismatch → reject. SAML uses `InResponseTo` against the stored AuthnRequest id. |
| **Token replay** | OIDC `nonce` is bound to the transaction and checked inside the `id_token`. SAML `NotOnOrAfter` + one-time `InResponseTo` consumption. |
| **Authorization-code interception** | PKCE (`S256`) — a stolen `code` is useless without the `code_verifier`, which never leaves the backend. |
| **id_token forgery** | Signature verified against the IdP's JWKS; `iss`/`aud`/`exp` all checked. CA-of-the-internet trust on the discovery/JWKS fetch (TLS). |
| **SAML assertion forgery / XML wrapping** | XML-DSig verified with `xmlsec1` against the configured IdP cert; `Audience`, time window, `Recipient`, `InResponseTo` all validated. Reject unsigned assertions outright. |
| **Session cookie theft via XSS** | `HttpOnly` — JS cannot read it. Standard XSS hygiene in the SPA still applies. |
| **Session cookie over plain HTTP** | `Secure` flag; Heroku is HTTPS-only at the router. |
| **CSRF on state-changing API calls** | `SameSite=Lax` blocks cross-site POST — the primary defense, active from Phase A. A double-submit CSRF token is added on mutating endpoints in Phase F (§14 Q3). Interim acceptance: between Phases C/D and F the app ships cookie auth + mutating endpoints with `SameSite=Lax` only — acceptable because Lax already blocks the common cross-site-POST vector. |
| **JWT algorithm-confusion (`none` / HS256-for-RS256)** | Strict `alg` allow-list; vetted JWT library rather than hand-rolled verification (§4). |
| **`redirect_uri` / `Host`-header poisoning** | Callback URLs pinned to the `publicBaseUrl` config value, never derived from request headers (§10). |
| **IdP-asserted role escalation** | `groupRoleMap` is opt-in per provider (disabled by default), enabled only for fully-trusted IdPs; the operator-authored map is the policy — the IdP only asserts group membership (§7). |
| **Lingering IdP access via refresh token** | Logout revokes the stored `idpRefreshToken` at the IdP revocation endpoint (§8). |
| **Open redirect via `return_to`** | `return_to` must be a local path (`^/`); scheme/host rejected. |
| **Account takeover via unverified email** | Email-match provisioning requires `email_verified == true`; otherwise fall through to JIT-create. |
| **`client_secret` / refresh-token leak** | BFF — secrets and refresh tokens live only server-side. `idpRefreshToken` is encrypted at rest in the `sessions` doc. |
| **Malicious SSO reconfiguration** (repoint login to a rogue IdP) | Config is written **only** by the on-prem Vaadin UI directly to its co-located MongoDB — there is no internet-facing config-write endpoint to attack (§10). The Vaadin console stays on the customer's trusted on-prem network, unauthenticated by design behind physical access controls — the same trust boundary that already permits direct `account`-collection edits, so config editing grants the operator no power they lack. The backend rejects invalid config on hot-reload (keep-last-good). **Hard requirement:** the Vaadin admin UI must never be internet-exposed. |
| **Stale session after logout** | Logout deletes the `sessions` record; the in-process cache TTL bounds staleness to ~60 s. |
| **IdP-initiated / unsolicited Response** | Out of scope Phase 1; the SAML provider rejects a `Response` with no matching `InResponseTo`. |

---

## 12. Phased delivery

All six phases (A–F) are **v1 scope** — SAML is included in the first release.
The phases are a build/sequencing order, not a release split: behind the
`IIdentityProvider` interface the protocols are independent, so OIDC can be
working and testable before SAML is finished.

| Phase | Scope |
|-------|-------|
| **A. Session foundation** | `sessions` collection + TTL index, cookie issue/parse, validation middleware + in-process cache, `sso_transactions` collection, `sso_config` collection (seeded empty). Password login mints a session. **CORS rework** — specific-origin echo + `Allow-Credentials` on the shared response builders (§8). |
| **B. Outbound HTTP** | `sso::HttpClient` (ACE_SOCK + OpenSSL) behind the `IHttpClient` interface — no new dependency. |
| **C. OIDC end-to-end** | `OidcProvider`, discovery, JWKS + JWT verify, the six `/api/v1/sso/*` endpoints, hybrid provisioning, `sso_config` hot-reload on the discovery background thread (§10). Ship with one OIDC provider. |
| **D. Angular** | Provider buttons, session bootstrap, route guard, `401` interceptor, logout. |
| **E. SAML** | `libxml2`/`xmlsec1` in the bootstrap image, `SamlProvider`, XML-DSig verification. Additive — no caller changes. |
| **F. Hardening** | CSRF token on mutating endpoints; decide blanket auth enforcement (§14). |

Phases A and B are prerequisites for everything. C+D deliver usable OIDC SSO;
E adds SAML without disturbing C/D. All ship in v1.

The **Vaadin SSO-config admin view** (§10) is a parallel Java workstream: it
depends only on the `sso_config` document shape, so it can be built any time
after Phase A creates that collection — independently of the C++ phases. The
Vaadin app has no automated-test harness (same as Angular, §9 / Phase D).

---

## 13. Files — new and changed

**New module `modules/module/sso/`** (rename of the empty `oauth2` stub):

| File | Contents |
|------|----------|
| `inc/identity_provider.hpp` | `IIdentityProvider`, `AuthnRequest`, `IdentityClaims`, `CallbackInput` |
| `inc/provider_registry.hpp` / `src/provider_registry.cpp` | Build + resolve providers from config |
| `inc/oidc_provider.hpp` / `src/oidc_provider.cpp` | OIDC: discovery, JWT/JWKS verify, code exchange |
| `inc/saml_provider.hpp` / `src/saml_provider.cpp` | SAML: AuthnRequest, XML-DSig verify, assertion mapping |
| `inc/sso_http_client.hpp` / `src/sso_http_client.cpp` | `IHttpClient` + `encode_form` + the ACE/OpenSSL `HttpClient` |
| `inc/session_manager.hpp` / `src/session_manager.cpp` | Session create/lookup/revoke, in-process cache |
| `inc/sso_config.hpp` / `src/sso_config.cpp` | Parse the `sso_config` document |
| `test/sso_test.cc` | Unit tests (see TDD plan) |

**On-prem Vaadin app (`onprem/`)** — a parallel Java workstream (§10):

| File | Contents |
|------|----------|
| `onprem/.../SsoConfigView.java` (new) | Admin view: list / add / edit / remove SSO providers; secrets write-only |
| `onprem/.../SsoConfigService.java` (new) | Reads/writes the `sso_config` document directly in the co-located MongoDB |

**Changed:**

| File | Change |
|------|--------|
| `modules/module/webservice/inc/webservice.hpp` | Declare `handle_sso_*` handlers; `AuthContext` on `WorkCtx` |
| `modules/module/webservice/src/webservice.cpp` | `/api/v1/sso/*` routing; cookie parse; session middleware; `302`/`Set-Cookie` response builders; session on password login |
| `modules/module/webservice/src/webservice_main.cpp` | Read SSO config from the `sso_config` collection; build `ProviderRegistry`; start the hot-reload poll |
| `docker/Dockerfile.bootstrap` | Add `libxml2`, `xmlsec1` to the toolchain image (Phase E / SAML only) |
| `docker/mongo-init.js` | Create `sessions` / `sso_transactions` (TTL indexes) and `sso_config` (seeded with an empty `{publicBaseUrl, providers:[]}` document) |
| `CMakeLists.txt` | Link `xmlsec1` (Phase E); the SSO sources build via the `sso/src` glob |
| `ui/src/common/app-globals.ts` | URIs for the `/api/v1/sso/*` endpoints |
| `ui/src/common/httpsvc.service.ts` | `withCredentials: true`; `getSsoProviders()`, `getSession()`, `logout()` |
| `ui/src/app/login/login.component.ts` | Render provider buttons |
| `ui/src/app/app-routing.module.ts` | `CanActivate` guard on protected routes |
| `ui/src/app/...` | New auth guard + `401` interceptor |

---

## 14. Decisions and open questions

### Resolved

- **OIDC and SAML are both in v1.** All phases A–F (§12) ship in the first
  release; the phasing is build order, not a release split.
- **Launch OIDC provider: Okta.** Discovery, claim names, and the `groups`
  shape are validated against an Okta org (§4).
- **Single web dyno.** `marvel` runs one web dyno, so the in-process session
  cache (§2) is coherent as designed. Scale-out would require revisiting it.
- **Config home & admin surface.** SSO config lives in the `sso_config`
  MongoDB collection, edited by the on-prem Vaadin admin UI which writes
  directly to its co-located MongoDB — no internet-facing config-write
  endpoint (§10). The Vaadin console stays unauthenticated; its trust model is
  unchanged.
- **Module directory name.** Implementation proceeded under the original
  `modules/module/oauth2/` stub directory through phases A–F (the C++
  namespace was always `sso::`), then renamed to `modules/module/sso/`
  in a post-v1.0 refactor.

### Still open

1. **Blanket auth enforcement.** Should every `/api/v1/*` endpoint require a
   valid session, or only a defined protected set? Blanket enforcement is the
   secure default but risks breaking the on-prem Vaadin console and the legacy
   `GET /api/v1/account/account` path. Recommendation: protected set in Phase
   C/D, blanket in Phase F after auditing every caller.
2. **Session lifetime & idle timeout.** Proposed: 12 h absolute. Is an idle
   timeout (e.g. 30 min) also wanted, and should the window slide?
3. **CSRF token timing.** Is `SameSite=Lax` alone acceptable until Phase F,
   with the double-submit token added then? (Lax already blocks the common
   cross-site POST — see the §11 interim-acceptance note.)
