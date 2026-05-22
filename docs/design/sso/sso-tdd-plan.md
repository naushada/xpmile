# TDD Plan: Single Sign-On (SSO)

Test-first plan for the design in `sso-design.md`. Each phase is a RED → GREEN
→ REFACTOR cycle. Phases map 1:1 to the design's delivery phases (§12).

## Ground rules

- All C++ tests run as the `offtarget` GTest binary. The CI `Dockerfile.test`
  CMD **excludes Mongo-dependent tests** — so every test here uses a **mock DB**
  (`MockMongodbClient`), never a live MongoDB.
- No test makes a real network call. The IdP is always a mock or a static
  fixture.
- Existing tests stay green throughout (current baseline: 46 tests).
- The Angular phase (D) has **no automated tests** — the project has no
  Jasmine/Karma setup. See Phase D.

## Test file map

| Test file | Tests what | New/Existing |
|-----------|-----------|--------------|
| `modules/module/sso/test/sso_test.cc` | Cookie, config, session manager + cache, PKCE, JWT/JWKS, OIDC provider, SAML provider, HTTP-client interface | **New** |
| `modules/module/webservice/test/webservice_test.cc` | SSO endpoint handlers, session middleware, CORS headers, session-on-password-login | Existing — add tests |

## Test doubles

| Double | Implements | Purpose |
|--------|-----------|---------|
| `MockMongodbClient` | `IMongodbClient` | Canned `get_document` / `create_document` / `update_collection` / `find_one_and_delete`; spy fields capture the last call. Reused from the password-hashing work; extended with `find_one_and_delete` (a new `IMongodbClient` method this design adds for atomic transaction/session consumption). |
| `MockHttpClient` | `sso::IHttpClient` | Canned IdP responses keyed by URL — discovery document, token endpoint, JWKS. Spy fields capture request URL/body. |
| `MockIdentityProvider` | `sso::IIdentityProvider` | Canned `AuthnRequest` / `IdentityClaims`. Lets the endpoint-handler tests run without real OIDC/SAML. |
| `FakeClock` | `sso::IClock` | Injectable "now" so token-`exp` and session-`expiresAt` tests are deterministic. |

## Build-time test fixtures

Generated during the Docker build (the same pattern `innertls` uses for its
test certs), written under `modules/module/sso/test/fixtures/`:

- A test **RSA keypair** + a JWKS document exposing its public key.
- A known-good **signed `SAMLResponse`** XML and the matching IdP signing cert.
- Tampered / unsigned / wrong-cert variants derived from the good one.

Tests skip with a logged notice if fixtures are absent (mirrors the `innertls`
"skip if certs not found" behavior).

---

## Phase A — Session foundation

### Why first

Everything else issues or consumes a session. Cookies and the session store
are the smallest, most-depended-on pieces and need no network.

### Step A.1 — RED: cookie helpers

**File:** `modules/module/sso/test/sso_test.cc`

```
TEST(SsoCookieTest, Build_SetsAllSecurityAttributes)
```
- `build_session_cookie("abc")` → contains `HttpOnly`, `Secure`,
  `SameSite=Lax`, `Path=/`, `Max-Age=`.

```
TEST(SsoCookieTest, Build_EmbedsSidValue)
```
- Result contains `xpmile_session=abc`.

```
TEST(SsoCookieTest, BuildExpired_HasMaxAgeZero)
```
- `build_expired_cookie()` (for logout) → `Max-Age=0`.

```
TEST(SsoCookieTest, Parse_ExtractsSidFromCookieHeader)
```
- `parse_session_cookie("foo=1; xpmile_session=abc; bar=2")` → `"abc"`.

```
TEST(SsoCookieTest, Parse_MissingCookie_ReturnsEmpty)
```
- `parse_session_cookie("foo=1; bar=2")` → `""`.

```
TEST(SsoCookieTest, Parse_EmptyHeader_ReturnsEmpty)
TEST(SsoCookieTest, Parse_MalformedHeader_DoesNotCrash)
```
- `""` and `"garbage;;==;"` → `""`, no crash.

**7 tests. RED — helpers do not exist.**

### Step A.2 — RED: config parser

```
TEST(SsoConfigTest, Parse_ValidOidcProvider_PopulatesFields)
```
- Parse an `sso_config` JSON document with one OIDC provider → issuer, clientId,
  clientSecret, scopes, defaultRole, allowedEmailDomains all read.

```
TEST(SsoConfigTest, Parse_ValidSamlProvider_PopulatesFields)
```
- SAML provider → idpEntityId, idpSsoUrl, idpSigningCert, spEntityId read.

```
TEST(SsoConfigTest, Parse_MultipleProviders_AllLoaded)
TEST(SsoConfigTest, Parse_EmptyConfig_NoProviders_NoCrash)
TEST(SsoConfigTest, Parse_MalformedJson_ReturnsError)
TEST(SsoConfigTest, Parse_MissingPublicBaseUrl_ReturnsError)
TEST(SsoConfigTest, Parse_GroupRoleMap_DefaultsToDisabled_WhenAbsent)
```
- A provider with no `groupRoleMap` → group→role mapping disabled.

**7 tests. RED.**

### Step A.3 — RED: `SessionManager` (with `MockMongodbClient`)

```
TEST(SessionManagerTest, Create_WritesSessionDoc_ReturnsSid)
```
- `create_session(db, accountCode, "password", ...)` → `db.lastCreateColl ==
  "sessions"`; returned sid is non-empty and ≥ 32 bytes of entropy.

```
TEST(SessionManagerTest, Create_SidsAreUnique)
```
- Two calls → two different sids.

```
TEST(SessionManagerTest, Create_SetsExpiresAt_FromClock)
```
- With `FakeClock` at T, the written doc's `expiresAt` == T + lifetime.

```
TEST(SessionManagerTest, Lookup_ValidSid_ReturnsAuthContext)
```
- Mock returns a non-expired session doc → `lookup()` yields
  `AuthContext{accountCode, role, authMethod}`.

```
TEST(SessionManagerTest, Lookup_ExpiredSession_ReturnsInvalid)
```
- Mock returns a doc whose `expiresAt` < `FakeClock` now → invalid.

```
TEST(SessionManagerTest, Lookup_UnknownSid_ReturnsInvalid)
TEST(SessionManagerTest, Revoke_CallsFindOneAndDelete_OnSessions)
```
- `revoke(sid)` → `db.lastDeleteColl == "sessions"`, filter on `_id == sid`.

```
TEST(SessionManagerTest, Lookup_RefreshesLastSeen_AtMostOncePerMinute)
```
- Two lookups 10 s apart → only one `update_collection` call.

**8 tests. RED.**

### Step A.4 — RED: in-process session cache

```
TEST(SessionCacheTest, Get_AfterPut_ReturnsCached_NoDbCall)
```
- After `cache.put(sid, ctx)`, `lookup()` does not touch the mock DB.

```
TEST(SessionCacheTest, Get_AfterTtlExpiry_FallsThroughToDb)
```
- Advance `FakeClock` past the cache TTL → next `lookup()` hits the DB.

```
TEST(SessionCacheTest, Revoke_PurgesCacheEntrySynchronously)
```
- After `revoke(sid)`, an immediate `lookup(sid)` does **not** return the
  stale cached context — the entry is gone at once (not after TTL).

```
TEST(SessionCacheTest, Eviction_RespectsMaxSize)
```
- Insert > capacity → oldest entry evicted (LRU).

```
TEST(SessionCacheTest, ConcurrentAccess_NoDataRace)
```
- N threads `put`/`get` concurrently → no crash, no race (run under TSan in CI
  if available).

**5 tests. RED.**

### Step A.5 — RED: CORS response builder rework

**File:** `modules/module/webservice/test/webservice_test.cc`

```
TEST(CorsTest, AllowedOrigin_IsEchoed_NotWildcard)
```
- Build a response for a request with `Origin:
  https://marvel-3a78bd953f5f.herokuapp.com` → response has `Access-Control-Allow-Origin:
  https://marvel-3a78bd953f5f.herokuapp.com`, **not** `*`.

```
TEST(CorsTest, AllowCredentials_HeaderPresent)
```
- Response carries `Access-Control-Allow-Credentials: true`.

```
TEST(CorsTest, DisallowedOrigin_NotEchoed)
```
- `Origin: https://evil.example` → that origin is **not** echoed back.

```
TEST(CorsTest, LocalhostDevOrigin_Allowed)
```
- `Origin: http://localhost:4200` → echoed (dev allow-list entry).

```
TEST(CorsTest, OptionsPreflight_CarriesCorsHeaders)
```
- An `OPTIONS` response carries the same CORS headers.

**5 tests. RED.**

### Step A.6 — RED: session middleware + 302/Set-Cookie builders

```
TEST(ResponseBuilderTest, Redirect_Emits302WithLocation)
```
- `build_redirect("/main")` → status line `302`, `Location: /main`.

```
TEST(ResponseBuilderTest, Response_CanAttachSetCookie)
```
- A response with an attached cookie emits a `Set-Cookie:` header.

```
TEST(SessionMiddlewareTest, ValidCookie_AttachesAuthContext)
```
- Request with a valid `xpmile_session` cookie + mock session → `WorkCtx`
  carries the resolved `AuthContext`.

```
TEST(SessionMiddlewareTest, NoCookie_LeavesAuthContextEmpty)
TEST(SessionMiddlewareTest, InvalidCookie_LeavesAuthContextEmpty)
```

```
TEST(SessionMiddlewareTest, RunsAfterRemoteDb503FastPath)
```
- In `--remote-db` mode with the agent disconnected, a request still gets the
  503 fast-path response — the session step does not run first / does not mask
  the 503.

**6 tests. RED.**

### Step A.7 — RED: password login mints a session

```
TEST(AccountLoginSessionTest, ValidLogin_CreatesSession_SetsCookie)
```
- `POST /api/v1/account/login` with correct credentials → response has
  `Set-Cookie: xpmile_session=`; `db.lastCreateColl == "sessions"` with
  `authMethod == "password"`.

```
TEST(AccountLoginSessionTest, FailedLogin_NoSession_NoCookie)
```
- Wrong password → 401, no `Set-Cookie`, no session write.

**2 tests. RED.**

### A — GREEN / REFACTOR

GREEN: cookie helpers; `SsoConfig` parser; `SessionManager` + `IClock`;
`SessionCache` (mutex-guarded LRU); CORS rework on the shared builders;
`build_redirect` + `Set-Cookie` support; the middleware step in
`process_request()` after the 503 fast-path; the session-mint addition to
`handle_account_login_POST`. Add `find_one_and_delete` to `IMongodbClient`.
REFACTOR: extract an `origin_allowed()` helper; fold cookie attrs into one
constant.

**Phase A: 40 tests.**

---

## Phase B — Outbound HTTP client

### Why second

OIDC (Phase C) cannot be built without it. SAML (Phase E) does not need it.

### Step B.1 — RED: `IHttpClient` contract + form encoding

The real `HttpClient` (ACE_SOCK + OpenSSL) is **integration-verified**, not
unit-tested (it needs a network peer). Unit tests target the pure pieces and
the interface that everything else mocks.

```
TEST(HttpClientTest, FormEncode_EscapesReservedChars)
```
- `encode_form({{"a","x y"},{"b","p&q"}})` → `a=x%20y&b=p%26q` (or `+`).

```
TEST(HttpClientTest, FormEncode_EmptyMap_ReturnsEmpty)
```

```
TEST(MockHttpClientTest, Get_ReturnsCannedResponse_ForUrl)
TEST(MockHttpClientTest, PostForm_RecordsRequestBody)
```
- Sanity tests for the mock itself so later phases can trust it.

**4 tests. RED.**

### B — GREEN / REFACTOR

GREEN: `IHttpClient` interface (`get`, `post_form`); the ACE/OpenSSL-backed
`HttpClient` — `ACE_SOCK_Connector` for DNS+connect, a per-connection OpenSSL
`SSL_CTX` with `SSL_VERIFY_PEER` + `SSL_CTX_set_default_verify_paths()` +
`X509_VERIFY_PARAM_set1_host()`, with **mandatory connect + read timeouts**;
`MockHttpClient`. No new dependency — `ACE`/`OpenSSL` are already linked.
REFACTOR: none expected.

> **Manual/integration check (not in `offtarget`):** one scripted run that
> calls a known HTTPS endpoint and asserts a fast failure when the timeout is
> tripped. Documented in the phase notes, not the GTest suite.

**Phase B: 4 tests.**

---

## Phase C — OIDC

### Step C.1 — RED: PKCE

```
TEST(PkceTest, Verifier_IsHighEntropy_UrlSafe)
```
- `make_code_verifier()` → 43–128 chars, only `[A-Za-z0-9-._~]`.

```
TEST(PkceTest, Challenge_IsS256OfVerifier_Base64Url)
```
- `code_challenge(v)` == base64url(SHA256(v)), no padding.

```
TEST(PkceTest, Verifier_DiffersEachCall)
```

**3 tests. RED.**

### Step C.2 — RED: JWT verification (the critical security tests)

Uses the build-time RSA keypair + JWKS fixture.

```
TEST(JwtVerifyTest, ValidRs256Token_Verifies)
```
- Token signed with the fixture key, correct `iss/aud/exp/nonce` → accepted.

```
TEST(JwtVerifyTest, TamperedPayload_Rejected)
TEST(JwtVerifyTest, WrongSigningKey_Rejected)
```

```
TEST(JwtVerifyTest, AlgNone_Rejected)
```
- A token with header `{"alg":"none"}` and no signature → rejected.

```
TEST(JwtVerifyTest, AlgConfusion_Hs256SignedWithRsaPublicKey_Rejected)
```
- Token signed `HS256` using the RSA **public** key bytes as the HMAC secret →
  rejected (the algorithm-confusion attack).

```
TEST(JwtVerifyTest, MissingKid_Rejected)
TEST(JwtVerifyTest, UnknownKid_Rejected)
```

```
TEST(JwtVerifyTest, ExpiredToken_Rejected)
TEST(JwtVerifyTest, FutureIatBeyondSkew_Rejected)
TEST(JwtVerifyTest, WrongIssuer_Rejected)
TEST(JwtVerifyTest, AudienceMissingClientId_Rejected)
TEST(JwtVerifyTest, MultiAudience_WrongAzp_Rejected)
TEST(JwtVerifyTest, NonceMismatch_Rejected)
```

```
TEST(JwtVerifyTest, ValidToken_ClaimsExtracted)
```
- `sub`, `email`, `email_verified`, `name`, `groups` all read into
  `IdentityClaims`.

**14 tests. RED.**

### Step C.3 — RED: JWKS parsing & cache

```
TEST(JwksTest, Parse_RsaKey_ExposesByKid)
TEST(JwksTest, Parse_MultipleKeys_AllIndexed)
TEST(JwksTest, Lookup_UnknownKid_TriggersOneRefetch)
```
- An unknown `kid` → exactly one `MockHttpClient` re-fetch, then hard-fail if
  still absent.

```
TEST(JwksTest, Parse_MalformedJwks_ReturnsError)
```

**4 tests. RED.**

### Step C.4 — RED: OIDC discovery (`MockHttpClient`)

```
TEST(OidcDiscoveryTest, Fetch_PopulatesEndpoints)
```
- Mock returns an Okta-style discovery doc → `authorization_endpoint`,
  `token_endpoint`, `jwks_uri`, `end_session_endpoint` all read.

```
TEST(OidcDiscoveryTest, Fetch_MalformedDoc_ReturnsError)
TEST(OidcDiscoveryTest, Fetch_MissingRequiredEndpoint_ReturnsError)
```

**3 tests. RED.**

### Step C.4b — RED: `sso_config` hot-reload

The `ProviderRegistry` is rebuilt from the `sso_config` document when it
changes (design §10). Tested by feeding canned config strings to a
`reload_if_changed()` entry point — no DB needed.

```
TEST(ConfigReloadTest, ChangedConfig_RebuildsRegistry)
```
- Feed config A, then a different config B → the registry now reflects B's
  providers.

```
TEST(ConfigReloadTest, UnchangedConfig_DoesNotRebuild)
```
- Feed the same config twice → no rebuild (document hash unchanged).

```
TEST(ConfigReloadTest, InvalidConfig_KeepsLastGood)
```
- Feed a valid config, then a malformed one → the registry still serves the
  last-good providers; the bad config is rejected, login is not taken down.

**3 tests. RED.**

### Step C.5 — RED: `OidcProvider::begin_login`

```
TEST(OidcBeginLoginTest, BuildsAuthorizeUrl_WithAllParams)
```
- Redirect URL has `response_type=code`, `client_id`, `redirect_uri`,
  `scope`, `state`, `nonce`, `code_challenge`, `code_challenge_method=S256`.

```
TEST(OidcBeginLoginTest, PersistsTransaction_WithStateNonceVerifier)
```
- `db.lastCreateColl == "sso_transactions"`; doc has state, nonce,
  code_verifier, return_to.

```
TEST(OidcBeginLoginTest, RedirectUri_PinnedToPublicBaseUrl)
```
- `redirect_uri` is built from config `publicBaseUrl`, not any request header.

**3 tests. RED.**

### Step C.6 — RED: `OidcProvider::handle_callback`

```
TEST(OidcCallbackTest, ValidCode_ExchangesAndReturnsClaims)
```
- Mock token endpoint returns a fixture `id_token` → `IdentityClaims.ok`,
  claims populated.

```
TEST(OidcCallbackTest, Transaction_ConsumedAtomically)
```
- Callback uses `find_one_and_delete` on `sso_transactions` (not find+delete).

```
TEST(OidcCallbackTest, UnknownState_Rejected)
TEST(OidcCallbackTest, ReplayedState_SecondCallRejected)
```
- After the first callback consumes the transaction, a second callback with
  the same `state` fails.

```
TEST(OidcCallbackTest, TokenEndpointError_ReturnsFailure)
TEST(OidcCallbackTest, IdTokenVerificationFails_ReturnsFailure)
TEST(OidcCallbackTest, NonceMismatchVsTransaction_Rejected)
```

**7 tests. RED.**

### Step C.7 — RED: hybrid provisioning

```
TEST(ProvisioningTest, SubjectMatch_UsesLinkedAccount)
```
- Account with `ssoIdentities:[{provider,subject}]` → resolves to it.

```
TEST(ProvisioningTest, EmailMatch_VerifiedEmail_LinksAndUsesAccount)
```
- No subject link, `email_verified==true`, an account with that
  `personalInfo.email` exists → matched; `ssoIdentities` appended.

```
TEST(ProvisioningTest, EmailMatch_UnverifiedEmail_FallsThroughToJit)
```
- `email_verified==false` → no match; a JIT account is created instead.

```
TEST(ProvisioningTest, EmailOutsideAllowedDomains_Rejected)
```
- Claims email domain not in the provider's `allowedEmailDomains` → rejected
  outright (no match, no JIT).

```
TEST(ProvisioningTest, NoMatch_JitCreatesAccount_WithDefaultRole)
```
- New `account` created; `personalInfo.role` == provider `defaultRole`; no
  `passwordHash`.

```
TEST(ProvisioningTest, Jit_GroupRoleMap_MapsGroupToRole)
```
- Provider with `groupRoleMap` + a matching IdP group → that role.

```
TEST(ProvisioningTest, MatchedAccount_KeepsExistingDbRole)
```
- An existing matched account's role is **not** overwritten by IdP claims.

**8 tests. RED.**

### Step C.8 — RED: SSO endpoint handlers (`MockIdentityProvider`)

**File:** `modules/module/webservice/test/webservice_test.cc`

```
TEST(SsoEndpointTest, GetProviders_ReturnsConfiguredList)
TEST(SsoEndpointTest, Login_RedirectsToProvider_302)
```
- `GET /api/v1/sso/login?provider=corp&return_to=/main` → 302 with the mock
  provider's redirect URL.

```
TEST(SsoEndpointTest, Login_UnknownProvider_400)
TEST(SsoEndpointTest, Login_NonLocalReturnTo_Rejected)
```
- `return_to=https://evil.example` → rejected (open-redirect guard).

```
TEST(SsoEndpointTest, Callback_SuccessfulClaims_CreatesSession_SetsCookie_302)
```
- Mock provider returns `ok` claims → session created, `Set-Cookie`, 302 to
  `return_to`.

```
TEST(SsoEndpointTest, Callback_FailedClaims_NoSession_RedirectsToLoginError)
TEST(SsoEndpointTest, Session_ValidCookie_ReturnsAccount)
TEST(SsoEndpointTest, Session_NoCookie_Returns401)
TEST(SsoEndpointTest, Logout_DeletesSession_ClearsCookie)
```
- `POST /api/v1/sso/logout` → session `find_one_and_delete`, response sets the
  expired cookie.

```
TEST(SsoEndpointTest, Logout_RevokesIdpRefreshToken)
```
- The stored `idpRefreshToken` triggers a revocation `post_form` on the mock
  HTTP client.

**10 tests. RED.**

### C — GREEN / REFACTOR

GREEN: `Pkce`, `JwtVerifier` (jwt-cpp), `Jwks`, `OidcDiscovery`,
`OidcProvider`, the provisioning resolver, the six `handle_sso_*` handlers
wired into `process_request()`, and `ProviderRegistry` with a
`reload_if_changed()` entry point (document hash + parse-or-keep-last-good),
polled from the `sso_config` collection on the discovery background thread.
Add `jwt-cpp` header to the toolchain. REFACTOR: extract `resolve_account()`;
extract a shared `consume_transaction()` used by both OIDC and SAML.

**Phase C: 56 tests.**

---

## Phase D — Angular

**No automated tests** — the project has no Jasmine/Karma infrastructure (same
situation as the password-hashing work). Phase D is verified by:

- `ng build --configuration production` compiles clean.
- Manual browser walkthrough: provider buttons render from
  `GET /api/v1/sso/providers`; clicking one completes a full Okta round trip
  and lands authenticated on `/main`; reload keeps the session; the route guard
  bounces an unauthenticated user to `/login`; logout clears the session.

Changes (no test cycle): provider buttons in `login.component.ts`;
`withCredentials: true` + `getSsoProviders/getSession/logout` in
`httpsvc.service.ts`; SSO URIs in `app-globals.ts`; `CanActivate` guard +
`401` interceptor; session bootstrap in an `APP_INITIALIZER`.

If a frontend test harness is set up later, the guard, the interceptor, and
`return_to` handling are the first things worth covering.

**On-prem Vaadin SSO-config admin view** (design §10) — also **no automated
tests** (the Vaadin app has no test harness, same as Angular). Verified
manually: add/edit/remove a provider; confirm `clientSecret` is never rendered
back; confirm a saved change is picked up by the backend hot-reload within
~60 s. The backend-side reload logic itself *is* covered — see Step C.4b.

**Phase D: 0 automated tests.**

---

## Phase E — SAML

### Step E.1 — RED: AuthnRequest

```
TEST(SamlAuthnRequestTest, Build_ProducesDeflatedBase64)
```
- `begin_login()` redirect URL has a `SAMLRequest=` param that base64-decodes
  then inflates to XML.

```
TEST(SamlAuthnRequestTest, Build_ContainsIssuerAndAcsUrl)
```
- The XML carries the SP `Issuer` (= `spEntityId`) and the ACS URL.

```
TEST(SamlAuthnRequestTest, Build_PersistsTransaction_WithRequestId)
```
- An `sso_transaction` is written keyed by the AuthnRequest `ID`; `RelayState`
  carries it.

**3 tests. RED.**

### Step E.2 — RED: response parse

```
TEST(SamlParseTest, DecodeBase64Response_ExtractsAssertion)
TEST(SamlParseTest, MalformedXml_ReturnsError)
TEST(SamlParseTest, MissingAssertion_ReturnsError)
```

**3 tests. RED.**

### Step E.3 — RED: XML-DSig verification (critical — uses fixtures)

```
TEST(SamlSignatureTest, ValidSignature_KnownGoodResponse_Verifies)
```
- The build-time known-good signed `SAMLResponse` against its IdP cert →
  accepted.

```
TEST(SamlSignatureTest, TamperedAssertion_Rejected)
```
- One byte changed in the assertion body → rejected.

```
TEST(SamlSignatureTest, WrongSigningCert_Rejected)
```
- Verified against a different cert → rejected.

```
TEST(SamlSignatureTest, UnsignedResponse_Rejected)
```
- A `SAMLResponse` with the `Signature` element stripped → rejected (never
  trust an unsigned assertion).

```
TEST(SamlSignatureTest, SignatureWrappingAttack_Rejected)
```
- A wrapped-signature variant (valid signature over a decoy element, real
  assertion injected elsewhere) → rejected.

**5 tests. RED.**

### Step E.4 — RED: assertion condition validation

```
TEST(SamlConditionsTest, AudienceMismatch_Rejected)
TEST(SamlConditionsTest, ExpiredNotOnOrAfter_Rejected)
TEST(SamlConditionsTest, NotYetValidNotBefore_Rejected)
TEST(SamlConditionsTest, RecipientMismatch_Rejected)
```

```
TEST(SamlConditionsTest, InResponseTo_ConsumedAtomically)
```
- Uses `find_one_and_delete` on `sso_transactions`.

```
TEST(SamlConditionsTest, ReplayedInResponseTo_SecondCallRejected)
TEST(SamlConditionsTest, UnsolicitedResponse_NoInResponseTo_Rejected)
```

**7 tests. RED.**

### Step E.5 — RED: `SamlProvider::handle_callback`

```
TEST(SamlCallbackTest, ValidResponse_ReturnsClaims)
```
- Full path: signature OK + conditions OK → `IdentityClaims` with `subject`
  (NameID), email, groups.

```
TEST(SamlCallbackTest, RegistryRoutesPostCallback_ToSamlProvider)
```
- `POST /api/v1/sso/callback/<saml-id>` reaches `SamlProvider` and reuses the
  Phase C session-creation path unchanged.

**2 tests. RED.**

### E — GREEN / REFACTOR

GREEN: `SamlProvider` — AuthnRequest (deflate+base64), response parse,
xmlsec1-based XML-DSig verification, condition checks. Add `libxml2` +
`xmlsec1` to `Dockerfile.bootstrap`, link in `CMakeLists.txt`. The `POST`
callback route and session creation are already in place from Phase C — SAML
plugs into the registry, no caller changes. REFACTOR: share
`consume_transaction()` and the session-creation tail with OIDC.

**Phase E: 20 tests.**

---

## Phase F — Hardening

### Step F.1 — RED: CSRF double-submit token

```
TEST(CsrfTest, IssuedToken_MatchesCookieAndHeader_Accepted)
TEST(CsrfTest, MissingHeaderToken_OnMutatingRequest_Rejected)
TEST(CsrfTest, MismatchedToken_Rejected)
TEST(CsrfTest, SafeMethods_GetHead_NotChecked)
```

**4 tests. RED.**

### Step F.2 — RED: auth enforcement (gated on §14 Q1 decision)

```
TEST(AuthEnforcementTest, ProtectedEndpoint_NoSession_Returns401)
TEST(AuthEnforcementTest, ProtectedEndpoint_ValidSession_Proceeds)
TEST(AuthEnforcementTest, ExemptEndpoint_NoSession_StillServed)
```
- The exempt set (login, SSO endpoints, and whatever §14 Q1 resolves) stays
  reachable without a session.

**3 tests. RED.**

### F — GREEN / REFACTOR

GREEN: double-submit CSRF token issue + check on mutating endpoints; the
enforcement predicate over the protected/exempt route set. REFACTOR: fold the
exempt list into one table.

**Phase F: 7 tests.**

---

## Execution order (dependency graph)

```
Phase A (session foundation) ──┬─────────────────────────────────────┐
                               │                                     │
                               ├── Phase B (HTTP client) ── Phase C (OIDC)
                               │                                │    │
                               │                                ├── Phase D (Angular)
                               │                                │
                               ├── Phase E (SAML) ──────────────┘
                               │   (needs A; reuses C's endpoints
                               │    + session tail; no HTTP client)
                               │
                               └── Phase F (hardening) ── needs A, C, E
```

- A is the universal prerequisite.
- B blocks C only. E does **not** need B.
- C and E both feed D (the UI lists whatever providers exist) and F.
- Within v1, a sensible build order is A → B → C → D → E → F, but A→E can run
  in parallel with B→C once A lands.

## Test count summary

| Phase | Tests | Module |
|-------|------:|--------|
| A. Session foundation | 40 | sso + webservice |
| B. Outbound HTTP | 4 | sso |
| C. OIDC (incl. `sso_config` hot-reload) | 56 | sso + webservice |
| D. Angular + Vaadin admin view | 0 | ui / onprem (manual verification) |
| E. SAML | 20 | sso |
| F. Hardening | 7 | webservice |
| **Total** | **127** | |

The existing tests stay green throughout. None of the 127 new tests touch a
live MongoDB or the network — all run inside the standard `offtarget` binary.
