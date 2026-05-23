# TDD Plan: In-house OIDC identity provider

Test-first plan for the design in `inhouse-idp-design.md`. Each phase is a RED → GREEN → REFACTOR cycle. Phases map 1:1 to the design's delivery phases (§11).

## Ground rules

- C++ tests live in:
  - `modules/module/inhouseidp/test/` (new — the IdP server module)
  - `modules/module/wsdbproxy/test/` (extended for the SIGN_JWT op)
  - `modules/module/webservice/test/` (extended for the legacy-login repoint)
- All C++ tests run inside the existing `offtarget` GTest binary — no separate runner.
- The CI gating check (`Run offtarget GTest suite`) blocks merges until they pass.
- Phase F (Angular `ui-idp/`) has **no automated tests** — the project has no Karma setup; manual verification per the v1.0 SSO design's Phase D.
- Phase B and Phase J (Vaadin admin views) have minimal automated tests — Spring `@Service` unit tests for `IdpSigningKeyService` / `IdpClientService` against an embedded mongo (or a `MockMongoCollection`); the Vaadin UI layer is verified manually.
- Phase pre-A migration script (Python) has its own pytest-style tests using `mongomock`.

## Test file map

| Phase | New test files | GTest suite prefix |
|-------|----------------|---------------------|
| pre-A | `scripts/test_migrate_account_split.py` | (pytest) |
| A | `modules/module/wsdbproxy/test/sign_jwt_test.cc` | `SignJwt*` |
| A | `modules/module/wsdbagent/test/sign_jwt_dispatch_test.cc` | `SignJwtDispatch*` |
| C | `modules/module/inhouseidp/test/jwks_test.cc` | `Jwks*`, `IdpJwksEndpoint*` |
| C | `modules/module/inhouseidp/test/discovery_test.cc` | `IdpDiscovery*` |
| D | `modules/module/inhouseidp/test/authorize_test.cc` | `IdpAuthorize*` |
| D | `modules/module/inhouseidp/test/login_test.cc` | `IdpLogin*` |
| D | `modules/module/inhouseidp/test/idp_session_test.cc` | `IdpSession*` |
| D | `modules/module/inhouseidp/test/client_registry_test.cc` | `IdpClientRegistry*` |
| E | `modules/module/inhouseidp/test/token_test.cc` | `IdpToken*` |
| E | `modules/module/inhouseidp/test/jwt_signer_test.cc` | `JwtSigner*` |
| E | `modules/module/inhouseidp/test/userinfo_test.cc` | `IdpUserinfo*` |
| E | `modules/module/inhouseidp/test/end_session_test.cc` | `IdpEndSession*` |
| G | `modules/module/inhouseidp/test/password_reset_test.cc` | `PasswordReset*` |
| K | `modules/module/webservice/test/legacy_login_xdb_test.cc` | `LegacyLoginXdb*` |
| B / J | `onprem/src/test/java/com/xpmile/onprem/service/IdpSigningKeyServiceTest.java` + `IdpClientServiceTest.java` | (JUnit) |

## Test doubles

- **`MockMongodbClient`** — the existing one used by the v1.0 SSO tests, extended with a couple of additional setters so the new collections can be primed.
- **`MockSignJwt`** — new. Implements an `IJwtSigner` interface (`sign(kid, alg, to_sign) → SignJwtResult`) that returns a deterministic fake signature. Used by Phase E token-endpoint tests so they don't need a real wsdbagent or real RSA. Real signing is exercised end-to-end in Phase E.3 with a test RSA keypair.
- **`MockEmailSender`** — new. Implements an `IEmailSender` interface (an extraction from the existing `SMTP::User`-driven path) that records sent messages. Used by Phase G password-reset tests.
- **`TestClock`** — existing `IClock` impl from v1.0 (`sso::TestClock` in `sso_session.hpp`'s test fixture).

## Build-time test fixtures

- **A test RSA-2048 keypair** generated at `test/CMakeLists.txt` configure time, written to `test/idp_test_keys/` (`private.pem` + `public.pem`). Used by `JwtSigner*` and `SignJwtDispatch*` tests for end-to-end sign-then-verify round trips.
- **A test SAML-style certificate** is *not* needed (no SAML in this design — purely OIDC).
- **A test mongo seeded with the post-migration schema** — provided by a small helper in `inhouseidp_test_fixtures.hpp` that primes a `MockMongodbClient` with one account, one client, one signing key.

---

## Implementation status

Snapshot of the `feat/inhouse-idp` branch. Each shipped phase has a commit hash on the branch; the count column is the actual landed test count, which over-delivered on the plan's call-outs.

| Phase | Description | Status | Tests landed | Commit |
|---|---|---|---:|---|
| pre-A | Account split migration (Python + pytest in podman) | ✅ shipped | 12 pytest | `0fa7b1b` |
| A pt 1 | `SIGN_JWT` wire op + cloud-side `WsdbJwtSigner` | ✅ shipped | 6 | `3616d6c` |
| A pt 2 | `wsdbagent` `SIGN_JWT` dispatcher + on-prem RSA | ✅ shipped | 6 (end-to-end roundtrip) | `3f22993` |
| B | Vaadin signing-key admin (Java) | ✅ shipped (no automated tests — matches SsoConfigService posture) | — | `d47b5e4` |
| C | JWKS endpoint + OIDC discovery doc | ✅ shipped | 18 | `eeca90d` |
| D pt 1 | `IdpSessionManager` + `IdpClientRegistry` | ✅ shipped | 26 | `42af168` |
| D pt 2 | IdP cookies + `/authorize` + `/login` | ✅ shipped | 27 | `829ad91` |
| E | `/token` + `/userinfo` + `/end_session` (real-RSA end-to-end) | ✅ shipped | 24 | `f6069bc` |
| F | `ui-idp/` Angular SPA | ⏳ pending | — (no Karma) | — |
| G | Password reset (`reset_request` + `reset_confirm`) | ✅ shipped | 11 | `c0d0e26` |
| H | CI publish to `registry.heroku.com/idp/web` + release the `idp` Heroku app | ✅ shipped (no Dockerfile.idp needed — design Q1 same-image resolution) | regression-only | (this commit) |
| I | Two-agent on-prem compose (marvel + idp wsdbagents, shared cert family) | ✅ shipped | smoke-only (operator runs) | (this commit) |
| J | Vaadin `IdpClientsView` (the sso_config integration deferred) | ✅ shipped — view only; sso_config wiring still pending | — | `d47b5e4` |
| K | Legacy `/api/v1/account/login` repoint to `idp.account` | ⏳ pending | — | — |
| (wire 1) | `MicroService::handle_idp` skeleton + `/well-known` + `/jwks` wired | ✅ shipped | regression-only | `97490d4` |
| (wire 2) | `/authorize` + `/userinfo` + `/end_session` wired, `IdpClientRegistry` hot-reload on `WebServer` | ✅ shipped | regression-only | `4749b84` |
| (wire 3a) | `/login` + `/token` wired (PbkdfPasswordVerifier + WsdbJwtSigner production impls) | ✅ shipped | regression-only | `260620e` |
| (wire 3b) | `/password/*` wiring (PbkdfPasswordHasher + SmtpEmailSender impls) | ⏳ pending | — | — |
| (Phase F slice 1) | `ui-idp/` Angular login portal | ✅ shipped — login page only, PubsubsvcService event bus | manual | `7164108` |

**Cumulative on the branch:** 118 GTest + 12 pytest, 379 / 379 total `offtarget` passes (zero regressions).

**Deviations from the original plan worth noting:**

- The `idp_test_keys` CMake fixture also drives the Phase E end-to-end roundtrip test (`IdpToken.EndToEnd_RealRsaSign_VerifyWithJwks`), not just Phase A's `SignJwtDispatch*` — same keys reused via the `IDP_TEST_PRIV_KEY_PATH` / `IDP_TEST_PUB_KEY_PATH` compile defs.
- Phase D over-delivered (53 tests vs the plan's 28) — the client registry got 17 tests on its own once the exact-byte URI match started covering both `redirect_uri` and `post_logout_redirect_uri`.
- The `/token` endpoint signs the JWT *twice* in production — first with kid `"current"` to let the signer resolve the real kid, then again with that kid baked into the header so verifiers find the matching JWKS entry cleanly. One extra wsdbagent round trip per token issuance; acceptable for v1, cache the active kid cloud-side later (open question Q-cache below).
- `IDP_ISSUER` is read from the env on every request rather than threaded through `WebServer` — keeps the marvel ↔ idp posture a deploy-time decision with zero state to plumb.
- The wire adapter ships in slices (1, 2, 3) so each slice is a small, deployable, test-green PR. The handler functions are fully unit-tested in `modules/module/inhouseidp/test/`; the wire layer itself has no direct unit tests (matches the `handle_sso` posture — the dispatch is a thin parse-and-render layer).

---

## Phase pre-A — Account split migration

### Why first

Every later phase that touches `idp.account` or `xpmile.account` assumes the split has happened. Phase pre-A is a small standalone Python script + its tests; runs against a real on-prem MongoDB at deploy time, against `mongomock` in CI.

### Step pre-A.1 — RED: `schema_version` idempotence

```python
def test_migration_exits_when_schema_version_is_already_2(client):
    client["xpmile"]["schema_version"].insert_one({"_id": "current", "version": 2})
    result = migrate(client)
    assert result.skipped is True
    assert result.docs_migrated == 0
```

### Step pre-A.2 — RED: copy + unset

```python
def test_migration_copies_auth_fields_and_unsets_them_in_xpmile(client):
    client["xpmile"]["account"].insert_one({
        "accountCode": "alice", "passwordHash": "h", "email": "a@x.com",
        "name": "Alice", "role": "Admin",
        "awbPrefix": "AWB", "eventLocation": "UAE", "personalInfo": {...},
    })
    migrate(client)
    idp_doc = client["idp"]["account"].find_one({"accountCode": "alice"})
    assert idp_doc["passwordHash"] == "h"
    assert idp_doc["email"] == "a@x.com"
    xpmile_doc = client["xpmile"]["account"].find_one({"accountCode": "alice"})
    assert "passwordHash" not in xpmile_doc
    assert "email" not in xpmile_doc
    assert xpmile_doc["awbPrefix"] == "AWB"   # business field still there
```

### Step pre-A.3 — RED: idempotence on partial state

```python
def test_migration_is_idempotent_on_partial_state(client):
    # Pre-state: idp.account already has alice; xpmile.account still has auth fields
    client["idp"]["account"].insert_one({"accountCode": "alice", "passwordHash": "h", ...})
    client["xpmile"]["account"].insert_one({
        "accountCode": "alice", "passwordHash": "h", "awbPrefix": "AWB",
    })
    migrate(client)
    # idp.account unchanged; xpmile.account passwordHash unset
    xpmile_doc = client["xpmile"]["account"].find_one({"accountCode": "alice"})
    assert "passwordHash" not in xpmile_doc
```

### Step pre-A.4 — RED: schema_version is bumped on success

```python
def test_migration_sets_schema_version_to_2(client):
    migrate(client)
    sv = client["xpmile"]["schema_version"].find_one({"_id": "current"})
    assert sv["version"] == 2
```

### Step pre-A.5 — RED: re-run is no-op

```python
def test_second_run_is_no_op(client):
    migrate(client)        # first run
    result = migrate(client)  # second run
    assert result.skipped is True
```

### Phase pre-A: GREEN / REFACTOR

- Write `scripts/migrate_account_split.py` (uses `pymongo`); main entry takes `--mongo-uri` and optional `--dry-run`.
- Add `scripts/test_migrate_account_split.py` and the pytest fixtures using `mongomock`.
- Add a small README at `scripts/README.md` documenting the script.

**Phase pre-A: 5 tests.**

---

## Phase A — `SIGN_JWT` op (wire protocol + dispatcher + cloud-side proxy)

### Why second

Phase E (`/token`) can't sign id_tokens without it. Other phases (C, D) don't depend on it, but landing A early gives Phase E a working dependency to plug into.

### Step A.1 — RED: `DbOp::SIGN_JWT` enum + dbproto round-trip

```cpp
TEST(SignJwtProto, EncodeRequest_RoundTrips) {
  SignJwtRequest in{42, "current", "RS256", "hdr.payload"};
  auto bson = encode_sign_jwt_request(in);
  auto out = decode_sign_jwt_request(bson);
  EXPECT_EQ(out.reqid,   42);
  EXPECT_EQ(out.kid,     "current");
  EXPECT_EQ(out.alg,     "RS256");
  EXPECT_EQ(out.to_sign, "hdr.payload");
}

TEST(SignJwtProto, EncodeResponse_OK_RoundTrips) { /* ... */ }
TEST(SignJwtProto, EncodeResponse_Error_RoundTrips) { /* ... */ }
TEST(SignJwtProto, DispatchByOp_RoutesSignJwt) { /* ensures the existing dispatcher recognises DbOp::SIGN_JWT */ }
```

### Step A.2 — RED: `WsMongodbProxy::sign_jwt` (cloud-side proxy method)

Tests use a `MockWsDbServer` that captures sent frames and replies with a predetermined `SignJwtResponse`.

```cpp
TEST(SignJwt, CloudProxy_SendsCorrectRequest) {
  MockWsDbServer server;
  WsMongodbProxy proxy(server, "idp");
  server.reply_to_next(SignJwtResponse{ok: true, signature: "FAKE-SIG", kid: "k1"});
  auto r = proxy.sign_jwt("current", "RS256", "hdr.payload");
  ASSERT_TRUE(r.ok);
  EXPECT_EQ(r.signature, "FAKE-SIG");
  EXPECT_EQ(r.kid, "k1");
  // verify what the proxy SENT
  auto sent = server.last_sent_request();
  EXPECT_EQ(sent.op, DbOp::SIGN_JWT);
  EXPECT_EQ(sent.kid, "current");
  EXPECT_EQ(sent.to_sign, "hdr.payload");
}

TEST(SignJwt, CloudProxy_PropagatesError) { /* error response → r.ok == false, r.error populated */ }
TEST(SignJwt, CloudProxy_TimeoutReturnsError) { /* WsDbServer disconnects mid-request */ }
```

### Step A.3 — RED: wsdbagent dispatcher signs against a real key

End-to-end test using the build-time RSA fixture: a `SignJwtRequest` carrying real `to_sign` bytes is fed into `sign_jwt_on_prem(*db, req)`; the returned signature is verified with `verify_jwt`'s existing primitive against the matching public key.

```cpp
TEST(SignJwtDispatch, Roundtrip_VerifyWithPublicKey) {
  MockMongodbClient db;
  db.seed("idp", "idp_signing_keys", load_fixture_signing_key("k1"));
  auto req = SignJwtRequest{1, "k1", "RS256", "hdr.payload"};
  auto resp = sign_jwt_on_prem(db, req);
  ASSERT_TRUE(resp.ok);
  ASSERT_EQ(resp.kid, "k1");
  // verify with the corresponding public key
  EXPECT_TRUE(rs256_verify(load_fixture_public_pem("k1"),
                           "hdr.payload",
                           resp.signature));
}

TEST(SignJwtDispatch, UnknownKid_Errors) { /* missing kid → ok=false */ }
TEST(SignJwtDispatch, KidCurrent_PicksActiveKey) { /* multiple keys; "current" returns the active=true one */ }
TEST(SignJwtDispatch, OnlyRS256Supported) { /* alg="ES256" → ok=false with clear error */ }
```

### Phase A: GREEN / REFACTOR

- Add `DbOp::SIGN_JWT` to `dbproto.hpp`; encode/decode functions in `dbproto.cpp`.
- Add `WsMongodbProxy::sign_jwt` to `wsdbproxy.cpp`.
- Add `sign_jwt_on_prem()` to `wsdbagent.cpp` (loads key by kid, signs with OpenSSL RSA-PKCS#1 v1.5).
- Hook the dispatcher to call `sign_jwt_on_prem` on `DbOp::SIGN_JWT`.

**Phase A: 11 tests.**

---

## Phase B — Signing-key admin (Vaadin)

### Why before C

Phase C's JWKS endpoint needs at least one document in `idp.idp_signing_keys` to return non-empty JWKS. Phase B provides the generator/admin for that.

### Step B.1 — RED: keypair generation

```java
@Test
void generateKeyPair_ProducesRSA2048WithValidPemEncoding() {
    IdpSigningKey key = IdpSigningKeyService.generate("kid-test");
    assertEquals("RS256", key.getAlg());
    assertEquals(2048, parsePrivateKey(key.getPrivateKeyPem()).getModulus().bitLength());
    assertTrue(key.getPublicKeyPem().startsWith("-----BEGIN PUBLIC KEY-----"));
}

@Test
void insertKey_DeactivatesPreviousActiveKey() { /* old key.active becomes false */ }

@Test
void list_ReturnsKeysOrderedByCreatedAtDesc() { /* ... */ }

@Test
void deleteExpired_OnlyRemovesKeysPastNotAfter() { /* ... */ }
```

### Step B.2 — RED: Vaadin view (lightweight; mostly manual verification)

Just one smoke-test:

```java
@Test
void view_RendersWithoutThrowing_WhenNoKeysExist() {
    IdpSigningKeysView view = new IdpSigningKeysView(stubService);
    // smoke: the constructor doesn't blow up; the grid has 0 rows; the "Generate" button exists
}
```

### Phase B: GREEN / REFACTOR

- `IdpSigningKey` POJO.
- `IdpSigningKeyService` (CRUD on `idp.idp_signing_keys`; `generate(kid)` uses `KeyPairGenerator.getInstance("RSA")` with 2048).
- `IdpSigningKeysView` (Vaadin Grid + "Generate" / "Deactivate" / "Delete" actions).
- Update `MainLayout.java` to add the side-nav entry.

**Phase B: 5 tests** (1 manual-verification scope on the view).

---

## Phase C — JWKS + discovery endpoints

### Why before D

D (`/authorize`) doesn't depend on JWKS, but C is a tiny set of pure-function endpoints — fast win, gets the foundation laid.

### Step C.1 — RED: `jwks_from_keys` (pure function)

```cpp
TEST(Jwks, BuildsJwkPerActiveKey) {
  std::vector<SigningKeyView> keys = { /* two non-expired keys */ };
  auto jwks = jwks_from_keys(keys, /*now=*/123);
  auto j = nlohmann::json::parse(jwks);
  ASSERT_EQ(j["keys"].size(), 2);
  EXPECT_EQ(j["keys"][0]["kty"], "RSA");
  EXPECT_EQ(j["keys"][0]["use"], "sig");
  EXPECT_EQ(j["keys"][0]["alg"], "RS256");
  EXPECT_TRUE(j["keys"][0].contains("n"));
  EXPECT_TRUE(j["keys"][0].contains("e"));
  EXPECT_TRUE(j["keys"][0].contains("kid"));
}

TEST(Jwks, ExcludesExpiredKeys) { /* notAfter < now → omitted */ }
TEST(Jwks, IncludesEvenInactiveKeysWhileTokensStillValid) { /* still in JWKS until notAfter+max_token_ttl */ }
TEST(Jwks, EmptyKeysReturnsEmptyArray) { /* JSON `{"keys":[]}` */ }
TEST(Jwks, ParsesRSAModulusCorrectly) { /* n is base64url, no padding */ }
```

### Step C.2 — RED: JWKS HTTP endpoint adapter

```cpp
TEST(IdpJwksEndpoint, ReturnsJsonContentType) { /* GET /api/v1/idp/jwks → 200, Content-Type: application/json */ }
TEST(IdpJwksEndpoint, SetsCacheControl) { /* short Cache-Control header */ }
```

### Step C.3 — RED: discovery document builder

```cpp
TEST(IdpDiscovery, IssuerMatchesConfig) {
  auto doc = build_discovery("https://idp.example/api/v1/idp");
  EXPECT_EQ(doc["issuer"], "https://idp.example/api/v1/idp");
  EXPECT_EQ(doc["authorization_endpoint"], "https://idp.example/api/v1/idp/authorize");
  EXPECT_EQ(doc["token_endpoint"],         "https://idp.example/api/v1/idp/token");
  EXPECT_EQ(doc["jwks_uri"],               "https://idp.example/api/v1/idp/jwks");
}

TEST(IdpDiscovery, AdvertisesPKCEMethodS256Only) {
  auto doc = build_discovery("https://idp.example/api/v1/idp");
  ASSERT_EQ(doc["code_challenge_methods_supported"].size(), 1);
  EXPECT_EQ(doc["code_challenge_methods_supported"][0], "S256");
}

TEST(IdpDiscovery, AdvertisesRS256Only) { /* id_token_signing_alg_values_supported == ["RS256"] */ }
TEST(IdpDiscovery, AdvertisesResponseTypeCodeOnly) { /* response_types_supported == ["code"] */ }
TEST(IdpDiscovery, IncludesScopesAndClaims) { /* openid, email, profile in scopes_supported */ }
```

### Step C.4 — RED: discovery HTTP endpoint adapter

```cpp
TEST(IdpDiscoveryEndpoint, ReturnsJsonContentType) { /* ... */ }
TEST(IdpDiscoveryEndpoint, AllUrlsAreAbsolute) { /* no relative URLs in discovery */ }
```

### Phase C: GREEN / REFACTOR

- `inc/idp_jwks.hpp` / `src/idp_jwks.cpp` — `jwks_from_keys(keys, now)`; `handle_idp_jwks_GET()`.
- `inc/idp_discovery.hpp` / `src/idp_discovery.cpp` — `build_discovery(issuer)`; `handle_idp_discovery_GET()`.
- Wire both into `MicroService::process_request` URI dispatch.

**Phase C: 12 tests.**

---

## Phase D — `/authorize` + `/login` + IdP session

The biggest phase. Many handlers; many negative paths.

### Step D.1 — RED: client registry

```cpp
TEST(IdpClientRegistry, LoadsClientsFromCollection) { /* ... */ }
TEST(IdpClientRegistry, FindByClientId_Returns_Client) { /* ... */ }
TEST(IdpClientRegistry, FindByClientId_Returns_Null_OnUnknownId) { /* ... */ }
TEST(IdpClientRegistry, ValidateRedirectUri_ExactMatch_Only) {
  // Registered: "https://marvel.app/cb"
  // Test: "https://marvel.app/cb"       → true
  //       "https://marvel.app/cb/"      → false  (trailing slash)
  //       "https://marvel.app/cb?x=1"   → false  (query string)
  //       "https://marvel.app/cbx"      → false  (prefix)
  //       "HTTPS://MARVEL.app/cb"       → false  (case mismatch on scheme/host is fine? we follow RFC: scheme+host case-insensitive but exact-match below the host)
}
TEST(IdpClientRegistry, HotReload_PicksUpNewClients) { /* reload_if_changed pattern */ }
```

### Step D.2 — RED: `/authorize` validation (transport-agnostic)

```cpp
TEST(IdpAuthorize, RejectsUnknownClientId) {
  auto r = authorize(query{"response_type":"code","client_id":"unknown",...}, db, registry, clock);
  EXPECT_EQ(r.status, 400);
  EXPECT_TRUE(r.body.contains("invalid_client"));
}

TEST(IdpAuthorize, RejectsUnregisteredRedirectUri) { /* known client, unknown redirect_uri → 400 */ }
TEST(IdpAuthorize, RejectsResponseTypeOtherThanCode) { /* response_type=token → 400 */ }
TEST(IdpAuthorize, RejectsMissingPKCE) { /* no code_challenge → 400 */ }
TEST(IdpAuthorize, RejectsCodeChallengeMethodOtherThanS256) { /* plain → 400 */ }
TEST(IdpAuthorize, RejectsMissingOpenidScope) { /* scope="email" only → 400 */ }
TEST(IdpAuthorize, RequiresStateForCSRF) { /* missing state → 400 (we require it even if OIDC says optional) */ }
```

### Step D.3 — RED: `/authorize` with no IdP session → redirect to /login + pending_auth

```cpp
TEST(IdpAuthorize, NoSession_PersistsPendingAuth_AndRedirects) {
  MockMongodbClient db;
  TestClock clock(/*now=*/1000);
  auto r = authorize(valid_query, /*no cookie*/, db, registry, clock);
  EXPECT_EQ(r.status, 302);
  EXPECT_TRUE(r.location.starts_with("/login"));
  EXPECT_TRUE(r.set_cookie.contains("xpmile_idp_pending="));
  // verify the pending_auth doc was persisted
  auto doc = db.find_one("idp", "idp_pending_auth", {});
  EXPECT_TRUE(doc);
  EXPECT_EQ(doc->at("client_id"), "xpmile-spa");
}

TEST(IdpAuthorize, PendingAuthHas30sExpiry) { /* TTL field set to now+30s */ }
TEST(IdpAuthorize, ReqTokenIs32BytesBase64url) { /* 43 chars, no padding, base64url alphabet */ }
```

### Step D.4 — RED: `/authorize` with valid IdP session → mint code

```cpp
TEST(IdpAuthorize, ValidSession_MintsCode_AndRedirectsToRP) {
  MockMongodbClient db;
  TestClock clock(1000);
  db.seed("idp", "sessions", {sid: "S1", accountCode: "alice", expiresAt: 99999});
  auto r = authorize(valid_query, cookie_header{"xpmile_idp_session=S1"}, db, registry, clock);
  EXPECT_EQ(r.status, 302);
  EXPECT_TRUE(r.location.starts_with("https://marvel.app/cb?code="));
  EXPECT_TRUE(r.location.contains("&state="));
  // verify the code was persisted
  auto codes = db.find_all("idp", "idp_codes");
  ASSERT_EQ(codes.size(), 1);
  EXPECT_EQ(codes[0].at("user_sub"), "alice");
}

TEST(IdpAuthorize, CodeHas30sExpiry) { /* ... */ }
TEST(IdpAuthorize, CodeBindsToPKCEChallenge) { /* stored doc has code_challenge */ }
TEST(IdpAuthorize, CodeBindsToNonce) { /* nonce from request stored with code */ }
```

### Step D.5 — RED: `/login` happy path

```cpp
TEST(IdpLogin, ValidCredentials_CreatesSession_RedirectsToAuthorize) {
  MockMongodbClient db;
  TestClock clock(1000);
  db.seed("idp", "account", {accountCode: "alice", passwordHash: hash("p")});
  db.seed("idp", "idp_pending_auth", {_id: "RT1", original_request: {...}});
  auto r = login("alice", "p", cookie_header{"xpmile_idp_pending=RT1"}, db, clock);
  EXPECT_EQ(r.status, 302);
  EXPECT_TRUE(r.location.starts_with("/api/v1/idp/authorize?"));
  EXPECT_TRUE(r.set_cookie.contains("xpmile_idp_session="));
  // verify session created in idp.sessions
  auto sessions = db.find_all("idp", "sessions");
  ASSERT_EQ(sessions.size(), 1);
  EXPECT_EQ(sessions[0].at("accountCode"), "alice");
}
```

### Step D.6 — RED: `/login` negative paths

```cpp
TEST(IdpLogin, MissingPendingAuth_Returns_400) { /* no xpmile_idp_pending cookie */ }
TEST(IdpLogin, ExpiredPendingAuth_Returns_400) { /* pending_auth doc expired */ }
TEST(IdpLogin, WrongPassword_Returns_401) { /* hash mismatch */ }
TEST(IdpLogin, UnknownAccount_Returns_401) { /* no idp.account doc */ }
TEST(IdpLogin, EmptyCredentials_Returns_400) { /* empty user or pass */ }
TEST(IdpLogin, AfterFailures_ApplyBackoff) { /* 3 failures within 60s → 429 + Retry-After */ }
```

### Step D.7 — RED: IdP session lifecycle

```cpp
TEST(IdpSession, CreateSession_PersistsAndReturnsSid) { /* ... */ }
TEST(IdpSession, Lookup_ValidSid_Returns_AuthContext) { /* ... */ }
TEST(IdpSession, Lookup_ExpiredSid_Returns_Invalid) { /* expiresAt past */ }
TEST(IdpSession, Lookup_UnknownSid_Returns_Invalid) { /* ... */ }
TEST(IdpSession, Revoke_DeletesRow_AndPurgesCache) { /* ... */ }
```

### Phase D: GREEN / REFACTOR

- `idp_client_registry.{hpp,cpp}` — load + lookup + exact-match validation.
- `idp_authorize.{hpp,cpp}` — request validation, pending-auth persistence, code minting.
- `idp_login.{hpp,cpp}` — credential check (calls into the shared password-verification helper), session creation, redirect.
- `idp_session.{hpp,cpp}` — `IdpSessionManager` (a second instance of the SSO `SessionManager`, scoped to `idp.sessions` and the IdP cookie name).
- `idp_endpoints.{hpp,cpp}` — adapter `handle_idp_authorize_GET` / `handle_idp_login_POST` that wires the above into `webservice.cpp`'s routing.

**Phase D: 28 tests.**

---

## Phase E — `/token` (with `SIGN_JWT` round-trip) + `/userinfo` + `/end_session`

### Step E.1 — RED: JWT signer

```cpp
TEST(JwtSigner, MakeUnsignedJwt_Format) {
  auto unsigned_jwt = make_jwt_unsigned(
      {{"alg","RS256"},{"typ","JWT"},{"kid","k1"}},
      {{"sub","alice"},{"iss","https://idp/api/v1/idp"}});
  // unsigned_jwt == b64u(hdr) + "." + b64u(payload), no trailing "."
  auto parts = split_by_dot(unsigned_jwt);
  ASSERT_EQ(parts.size(), 2);
  EXPECT_EQ(b64u_decode_to_json(parts[0])["alg"], "RS256");
  EXPECT_EQ(b64u_decode_to_json(parts[1])["sub"], "alice");
}

TEST(JwtSigner, AssembleJwt_AppendsSignature) {
  auto jwt = assemble_jwt("hdr.payload", "SIGNATURE");
  EXPECT_EQ(jwt, "hdr.payload.SIGNATURE");
}

TEST(JwtSigner, Roundtrip_SignThenVerify) {
  // sign with the test private key fixture; verify with the public key
  auto unsigned_jwt = make_jwt_unsigned(...);
  auto sig = rs256_sign_with_fixture_private_key(unsigned_jwt);
  auto jwt = assemble_jwt(unsigned_jwt, sig);
  Jwks jwks = load_fixture_jwks();
  auto result = verify_jwt(jwt, jwks, expect, /*now=*/123);
  EXPECT_TRUE(result.ok);
}
```

### Step E.2 — RED: `/token` happy path with `MockSignJwt`

```cpp
TEST(IdpToken, ValidCode_AndVerifier_ReturnsTokenPair) {
  MockMongodbClient db;
  MockSignJwt signer; signer.returns("FAKE-SIG", "k1");
  TestClock clock(1000);
  db.seed("idp", "idp_codes", {_id:"C1", client_id:"xpmile-spa", code_challenge:S256("verifier1"), nonce:"N", ...});
  db.seed("idp", "account", {accountCode:"alice", email:"a@x.com"});
  auto r = token_endpoint(form{"grant_type":"authorization_code","code":"C1","code_verifier":"verifier1","client_id":"xpmile-spa",...}, db, signer, clock);
  EXPECT_EQ(r.status, 200);
  auto j = nlohmann::json::parse(r.body);
  EXPECT_TRUE(j.contains("id_token"));
  EXPECT_TRUE(j.contains("access_token"));
  EXPECT_EQ(j["token_type"], "Bearer");
  EXPECT_EQ(j["expires_in"], 3600);
  // verify the id_token includes the right claims
  auto parts = split_by_dot(j["id_token"]);
  auto payload = b64u_decode_to_json(parts[1]);
  EXPECT_EQ(payload["sub"], "alice");
  EXPECT_EQ(payload["nonce"], "N");
}
```

### Step E.3 — RED: `/token` end-to-end with real RSA signing

```cpp
TEST(IdpToken, EndToEnd_SignThenVerify) {
  // Use a real RSA signer backed by the test fixture private key.
  RealRs256Signer real_signer(fixture_private_key_pem());
  // ... drive the full token flow ...
  // Assemble the id_token, then verify it with the matching public key.
  Jwks jwks = load_fixture_jwks();
  auto verify = verify_jwt(j["id_token"], jwks, expect, clock.now());
  EXPECT_TRUE(verify.ok);
}
```

### Step E.4 — RED: `/token` negative paths

```cpp
TEST(IdpToken, ReplayedCode_Returns_400) { /* atomic claim fails on 2nd request */ }
TEST(IdpToken, WrongPKCEVerifier_Returns_400) { /* S256 mismatch */ }
TEST(IdpToken, ExpiredCode_Returns_400) { /* code expiresAt past */ }
TEST(IdpToken, WrongClientId_Returns_400) { /* stored != provided */ }
TEST(IdpToken, MismatchedRedirectUri_Returns_400) { /* same: stored != provided */ }
TEST(IdpToken, MissingClientSecret_Returns_401_OnConfidentialClient) { /* ... */ }
TEST(IdpToken, GrantTypeOtherThanAuthCode_Returns_400) { /* refresh_token → unsupported_grant_type */ }
TEST(IdpToken, SignerError_Returns_500) { /* MockSignJwt returns ok=false */ }
TEST(IdpToken, AccessTokenIsRandom_AndStored) { /* idp_access_tokens row created */ }
```

### Step E.5 — RED: `/userinfo`

```cpp
TEST(IdpUserinfo, ValidBearer_Returns_Claims) { /* claims as JSON */ }
TEST(IdpUserinfo, InvalidBearer_Returns_401) { /* unknown access_token */ }
TEST(IdpUserinfo, ExpiredBearer_Returns_401) { /* TTL expired */ }
TEST(IdpUserinfo, MissingAuthorizationHeader_Returns_401) { /* WWW-Authenticate header set */ }
```

### Step E.6 — RED: `/end_session`

```cpp
TEST(IdpEndSession, DeletesSession_AndRedirects) { /* ... */ }
TEST(IdpEndSession, InvalidPostLogoutRedirectUri_Returns_400) { /* must be in client's list */ }
TEST(IdpEndSession, NoIdTokenHint_StillDeletesSession_OnCookie) { /* hint is optional */ }
```

### Phase E: GREEN / REFACTOR

- Extend `sso_jwt.{hpp,cpp}` with `make_jwt_unsigned` + `assemble_jwt` helpers.
- `idp_token.{hpp,cpp}` — token endpoint logic, calls `IJwtSigner::sign` (the abstraction).
- `idp_userinfo.{hpp,cpp}` — bearer validation + claims response.
- `idp_end_session.{hpp,cpp}` — RP-initiated logout.
- `idp_jwt_signer.{hpp,cpp}` — `IJwtSigner` interface + `WsdbJwtSigner` impl that calls `WsMongodbProxy::sign_jwt`.

**Phase E: 22 tests.**

---

## Phase F — `ui-idp/` Angular project

### Manual verification only

The project has no Karma/Jasmine setup (same situation as the v1.0 SSO design's Phase D). Phase F is verified by:

1. `ng build --configuration production` succeeds without errors.
2. The branded portal renders at `http://localhost:8090/login` (when the IdP uniservice is running locally with the new Angular dist).
3. A wrong-credential POST renders the inline error.
4. A correct-credential POST follows the IdP's 302 chain back to a registered RP.
5. "Forgot password" navigates to `/password-reset`.

**Phase F: 0 automated tests.**

---

## Phase G — Password reset (backend)

### Step G.1 — RED: reset-start

```cpp
TEST(PasswordReset, Start_KnownEmail_GeneratesToken_AndSendsEmail) {
  MockMongodbClient db;
  MockEmailSender mailer;
  TestClock clock(1000);
  db.seed("idp", "account", {accountCode:"alice", email:"a@x.com"});
  auto r = reset_start("a@x.com", db, mailer, clock);
  EXPECT_EQ(r.status, 200);
  // verify token persisted with TTL=30min
  auto resets = db.find_all("idp", "password_resets");
  ASSERT_EQ(resets.size(), 1);
  EXPECT_EQ(resets[0].at("accountCode"), "alice");
  EXPECT_EQ(resets[0].at("expiresAt"), 1000 + 1800);
  // verify email was sent
  EXPECT_EQ(mailer.sent_count(), 1);
  EXPECT_EQ(mailer.last_to(), "a@x.com");
  EXPECT_TRUE(mailer.last_body().contains("/password-reset/confirm?token="));
}

TEST(PasswordReset, Start_UnknownEmail_Returns_200_WithoutEmail) {
  // Same status code as known email; no email sent (enumeration defense)
  EXPECT_EQ(r.status, 200);
  EXPECT_EQ(mailer.sent_count(), 0);
}

TEST(PasswordReset, Start_TimingIsConsistent_Across_Found_And_NotFound) {
  // Both paths take approximately the same wall-clock time
  // (sleep-pad the "not found" path)
}
```

### Step G.2 — RED: reset-confirm

```cpp
TEST(PasswordReset, Confirm_ValidToken_UpdatesHash_DeletesToken_RevokesSessions) {
  MockMongodbClient db;
  TestClock clock(1000);
  db.seed("idp", "password_resets", {_id:"T1", accountCode:"alice", expiresAt:5000});
  db.seed("idp", "account", {accountCode:"alice", passwordHash:hash("old")});
  db.seed("idp", "sessions", {sid:"S1", accountCode:"alice", expiresAt:99999});
  auto r = reset_confirm("T1", "newpass", db, clock);
  EXPECT_EQ(r.status, 302);
  EXPECT_EQ(r.location, "/login?reset=ok");
  EXPECT_TRUE(verify_password_hash("newpass", db.find_one("idp", "account", {})->at("passwordHash")));
  EXPECT_EQ(db.find_all("idp", "password_resets").size(), 0);
  EXPECT_EQ(db.find_all("idp", "sessions").size(), 0);
}

TEST(PasswordReset, Confirm_UnknownToken_Returns_400) { /* ... */ }
TEST(PasswordReset, Confirm_ExpiredToken_Returns_400) { /* expiresAt past */ }
TEST(PasswordReset, Confirm_TokenConsumedTwice_SecondReturns_400) { /* atomic delete-on-use */ }
TEST(PasswordReset, Confirm_WeakPassword_Returns_400) { /* minimum length check */ }
```

### Phase G: GREEN / REFACTOR

- `idp_password_reset.{hpp,cpp}` — reset_start + reset_confirm.
- Extract `IEmailSender` interface from the existing `SMTP::User` machinery; the real impl is the existing path.
- Add `password_resets` collection seed to `mongo-init.js`.

**Phase G: 8 tests.**

---

## Phase H — `docker/Dockerfile.idp` + CI publish

### Verification (no GTest, mostly build/deploy)

1. `docker build -f docker/Dockerfile.idp -t xpmile-idp:local .` succeeds.
2. CI publishes the same image to both `registry.heroku.com/marvel/web` and `registry.heroku.com/idp/web` after a merge to main.
3. The idp Heroku app serves the branded login portal at `/` after the release.

No new GTest. CI workflow extension goes through the existing `Run offtarget GTest suite` gate (the source code changes that ride with H are already covered by Phases A–G tests).

**Phase H: 0 automated tests.**

---

## Phase I — On-prem two-agent stack

### Step I.1 — RED: `docker-compose.agent.yml` validates with two wsdbagents

A one-off CI smoke test (added to the existing test workflow):

```bash
podman-compose -f docker-compose.agent.yml config > /dev/null
# Verify two wsdbagent services with different SERVER_HOST defaults
```

### Step I.2 — RED: cert-watcher handles two cert dirs

```bash
# Spin up the compose stack with both agents pointed at a mock cloud
# Trigger a cert change in one dir; assert only the corresponding agent restarted
```

Manual verification primary. No GTest.

### Phase I: GREEN / REFACTOR

- Add `wsdbagent-idp` service to `docker-compose.agent.yml`.
- Extend `xpmile-cert-watcher` to watch two cert dirs (`/watch/marvel` + `/watch/idp`) and restart the corresponding agent.
- Extend `run-agent.sh` to handle both stacks (`./run-agent.sh start` brings up both; `refresh-certs` refreshes both).
- Default per the Q1 resolution: same image to both Heroku apps → one CA → one cert family → `refresh-certs` extension is minimal.

**Phase I: 0 automated tests** (script-level smoke tests only).

---

## Phase J — Coexistence wiring + `IdpClientsView`

### Step J.1 — RED: `IdpClientService` (Vaadin side)

```java
@Test
void list_ReturnsClientsFromCollection() { /* ... */ }
@Test
void register_HashesClientSecret_BeforeStoring() { /* ... */ }
@Test
void register_ValidatesRedirectUriIsAbsoluteHttps() { /* ... */ }
@Test
void update_PreservesSecret_WhenBlank() { /* "leave blank to keep" pattern */ }
```

### Step J.2 — RED: `IdpClientsView` smoke test

```java
@Test
void view_RendersWithoutThrowing_WhenNoClientsExist() { /* ... */ }
```

### Step J.3 — RED: `sso_config` includes the in-house IdP

A manual verification step in CI: the deployed marvel app's `GET /api/v1/sso/providers` returns the new "xpmile" provider in its list once the operator adds it via the Vaadin SsoConfigView.

### Phase J: GREEN / REFACTOR

- `IdpClient` POJO + `IdpClientService` + `IdpClientsView` (Vaadin).
- Update `MainLayout.java` for the side-nav entry.
- Update the operator guide with "registering an RP" walkthrough.

**Phase J: 5 tests** (Java).

---

## Phase K — Repoint legacy `/api/v1/account/login`

### Step K.1 — RED: cross-DB read against `idp.account`

```cpp
TEST(LegacyLoginXdb, ValidCredentials_ReadsFromIdpAccount_NotXpmileAccount) {
  MockMongodbClient db;
  // idp.account has the hash; xpmile.account has only business fields (post-migration)
  db.seed("idp", "account", {accountCode:"alice", passwordHash:hash("p")});
  db.seed("xpmile", "account", {accountCode:"alice", awbPrefix:"AWB"});
  // legacy endpoint
  auto r = handle_account_login(form{"userId":"alice","password":"p"}, db);
  EXPECT_EQ(r.status, 200);
}

TEST(LegacyLoginXdb, OnlyReadsIdpAccount_NoOtherIdpReads) {
  // verify the handler reads ONLY idp.account; nothing else from the idp DB
  EXPECT_EQ(db.read_log().filter(db="idp").size(), 1);
  EXPECT_EQ(db.read_log().filter(db="idp")[0].coll, "account");
}

TEST(LegacyLoginXdb, WrongPassword_Returns_401) { /* ... */ }
TEST(LegacyLoginXdb, UnknownAccount_Returns_401) { /* ... */ }
TEST(LegacyLoginXdb, SharedPasswordHelper_With_IdpLogin) {
  // The same verification function is called from both endpoints — assert symmetry
  EXPECT_EQ(handle_account_login(form{"userId":"alice","password":"p"}, db).status, 200);
  EXPECT_EQ(handle_idp_login(/*...*/).status, 302);  // both succeed for the same credentials
}
```

### Phase K: GREEN / REFACTOR

- Extract `verify_account_password(IMongodbClient&, db_name, accountCode, password) → bool` helper.
- Update `handle_account_login_POST` to call the helper with `db_name="idp"`.
- Update the marvel SPA login form to remain visible (no UI change needed beyond keeping it).

**Phase K: 5 tests.**

---

## Execution order (dependency graph)

```
pre-A (migration)
 │
 ├─ Phase A (SIGN_JWT) ─────┐
 │                          │
 ├─ Phase B (Vaadin keys)   │
 │                          │
 ├─ Phase C (JWKS + disco) ─┤
 │                          │
 ├─ Phase D (authz+login) ──┤
 │                          ├─ Phase E (token, needs A+D)
 │                          │
 ├─ Phase G (password reset)│
 │                          │
 └─ Phase K (legacy repoint, needs pre-A)
                            │
                            ├─ Phase J (coexistence wiring, needs E ready on idp host)
                            │
                            ├─ Phase F (Angular ui-idp/)  — independent of C++; can start any time
                            │
                            ├─ Phase H (Dockerfile.idp + CI) — needs F dist; needs A-G code
                            │
                            └─ Phase I (two-agent compose) — independent of C++ code; can start any time
```

Recommended build order (one phase at a time): pre-A → A → C → D → E → B → G → K → J → F → H → I.

---

## Test count summary

| Phase | Tests | Module |
|-------|------:|--------|
| pre-A. Migration | 5 | pytest |
| A. SIGN_JWT | 11 | wsdbproxy + wsdbagent |
| B. Vaadin keys | 5 | onprem (Java) |
| C. JWKS + discovery | 12 | inhouseidp |
| D. /authorize + /login | 28 | inhouseidp |
| E. /token + /userinfo + /end_session | 22 | inhouseidp |
| F. Angular ui-idp/ | 0 | manual |
| G. Password reset | 8 | inhouseidp |
| H. Dockerfile.idp + CI | 0 | manual |
| I. Two-agent compose | 0 | shell smoke only |
| J. IdpClientsView | 5 | onprem (Java) |
| K. Legacy repoint | 5 | webservice |
| **Total** | **101** | (78 GTest + 5 pytest + 10 JUnit + manual) |

The existing tests stay green throughout. None of the 78 new GTest cases touch a live MongoDB, real wsdbagent, or the network — all run inside the `offtarget` binary against mocks (`MockMongodbClient`, `MockSignJwt`, `MockEmailSender`).
