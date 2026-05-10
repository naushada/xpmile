# TDD Plan: Password Hashing & Secure Agent Communication

## Test file map

| Test file | Tests what | New/Existing |
|-----------|-----------|--------------|
| `modules/module/mongodb/test/mongodbc_test.cc` | `hash_password`, `verify_password` | **New** |
| `modules/module/webservice/test/webservice_test.cc` | `handle_account_login_POST`, hashing on create/update | Existing, add tests |
| `modules/module/security/test/innertls_test.cc` | TLS-over-transport handshake, inner-tls frame I/O | **New** |

Test doubles needed:
- `MockMongodbClient` — implements `IMongodbClient`, returns canned responses for `get_document`, `create_document`, `update_collection`

---

## Phase 1: Password hashing helpers (`mongodbc`)

### Why this first

Everything else depends on `hash_password()` and `verify_password()`. These are pure functions (no DB, no network) — fastest to TDD.

### Step 1.1 — RED: Write failing tests

**File:** `modules/module/mongodb/test/mongodbc_test.cc` (new)

```
TEST(PasswordHashTest, Hash_ProducesModularCryptFormat)
```
- Call `MongodbClient::hash_password("mypassword")`
- Assert result starts with `"$pbkdf2-sha256$i="`
- Assert result contains two `$` separators after the prefix (salt and hash sections)

```
TEST(PasswordHashTest, Hash_ProducesDifferentSaltsPerCall)
```
- Call `hash_password("same_password")` twice
- Assert the two results are not equal (different random salt each time)

```
TEST(PasswordHashTest, Verify_CorrectPassword_ReturnsTrue)
```
- `hash = hash_password("secret123")`
- `verify_password("secret123", hash)` → true

```
TEST(PasswordHashTest, Verify_WrongPassword_ReturnsFalse)
```
- `hash = hash_password("secret123")`
- `verify_password("wrongpassword", hash)` → false

```
TEST(PasswordHashTest, Verify_WrongCase_ReturnsFalse)
```
- `hash = hash_password("Secret123")`
- `verify_password("secret123", hash)` → false

```
TEST(PasswordHashTest, Hash_EmptyPassword_ProducesValidHash)
```
- `hash_password("")` — does not crash, produces valid format

```
TEST(PasswordHashTest, Verify_EmptyPassword_Works)
```
- `hash = hash_password("")`
- `verify_password("", hash)` → true
- `verify_password("x", hash)` → false

```
TEST(PasswordHashTest, Hash_LongPassword_ProducesValidHash)
```
- 10 KB password — does not crash, produces valid format, verify works

```
TEST(PasswordHashTest, Verify_EmptyHash_ReturnsFalse)
```
- `verify_password("anything", "")` → false (does not crash)

```
TEST(PasswordHashTest, Verify_MalformedHash_ReturnsFalse)
```
- `verify_password("anything", "not-a-valid-hash")` → false (does not crash)

```
TEST(PasswordHashTest, Verify_WrongAlgorithmInHash_ReturnsFalse)
```
- Construct hash with `$unknown-algo$...` prefix
- `verify_password("pwd", malformed_hash)` → false

**11 tests. All RED (compilation fails — no implementation yet).**

### Step 1.2 — GREEN: Minimal implementation

Add to `MongodbClient` (static methods, no DB):
```cpp
static std::string hash_password(const std::string &password);
static bool verify_password(const std::string &password, const std::string &stored_hash);
```

Implement using OpenSSL `PKCS5_PBKDF2_HMAC`:
- Salt: 16 random bytes via `RAND_bytes`
- Iterations: 600,000
- Key length: 32 bytes (SHA-256 output)
- Format: `$pbkdf2-sha256$i=600000$<base64-salt>$<base64-hash>`

### Step 1.3 — REFACTOR

- Extract format parsing into a private helper `parse_hash_format()`
- Extract the constant `600000` to a named constant
- No public API changes

---

## Phase 2: Login endpoint

### Why this second

The login handler depends on `verify_password()` (Phase 1) and needs a mock DB client. It's the highest-value change — fixes plain-text password in transit and at rest simultaneously.

### Step 2.0 — Create MockMongodbClient test double

**File:** `modules/module/webservice/test/webservice_test.cc` (append)

```cpp
namespace {

class MockMongodbClient : public IMongodbClient {
public:
  // ── canned responses ──
  std::string getDocumentResult;
  std::string createDocumentResult;
  bool        updateCollectionResult = false;

  // ── spy fields ──
  std::string lastCreateColl;
  std::string lastCreateDoc;
  std::string lastGetColl;
  std::string lastGetQuery;
  std::string lastUpdateColl;
  std::string lastUpdateFilter;
  std::string lastUpdateDoc;

  const std::string& get_database() const override { return m_db; }
  void set_database(const std::string& db) { m_db = db; }

  std::string create_document(const std::string&, const std::string& coll,
                              const std::string& doc) override {
    lastCreateColl = coll;
    lastCreateDoc  = doc;
    return createDocumentResult;
  }

  // ... (boilerplate overrides for all 14 pure-virtual methods, returning
  //      default/empty values — only the spied ones carry state)

  bool update_collection(const std::string& coll, const std::string& filter,
                         const std::string& document) override {
    lastUpdateColl = coll;
    lastUpdateFilter = filter;
    lastUpdateDoc  = document;
    return updateCollectionResult;
  }

  std::string get_document(const std::string& coll, const std::string& query,
                           const std::string& projection) override {
    lastGetColl = coll;
    lastGetQuery = query;
    return getDocumentResult;
  }

  // ... remaining overrides return defaults

private:
  std::string m_db = "testdb";
};

} // namespace
```

### Step 2.1 — RED: Login handler tests

**File:** `modules/module/webservice/test/webservice_test.cc` (append to existing)

```
TEST(AccountLoginTest, ValidCredentials_Returns200WithAccountData)
```
- Mock DB returns: `{"loginCredentials":{"accountCode":"admin"}, "passwordHash":"<hash_of_secret>", "personalInfo":{"role":"Admin"}}`
- Build POST request: `POST /api/v1/account/login` with body `{"userId":"admin","password":"secret"}`
- Call `handle_account_login_POST(request, mockDb)`
- Assert response contains `HTTP/1.1 200 OK`
- Assert response body contains `"personalInfo"`
- Assert response body does **NOT** contain `"passwordHash"`

```
TEST(AccountLoginTest, WrongPassword_Returns401)
```
- Same mock DB result
- POST with `{"userId":"admin","password":"wrongpassword"}`
- Assert response contains `HTTP/1.1 401 Unauthorized`
- Assert response body contains `"Invalid Credentials"`

```
TEST(AccountLoginTest, UnknownUser_Returns401)
```
- Mock DB returns empty string (no account found)
- POST with `{"userId":"nobody","password":"anything"}`
- Assert response contains `HTTP/1.1 401 Unauthorized`

```
TEST(AccountLoginTest, MissingUserId_Returns400)
```
- POST with body `{"password":"secret"}` (no userId)
- Assert response contains `HTTP/1.1 400 Bad Request`

```
TEST(AccountLoginTest, MissingPassword_Returns400)
```
- POST with body `{"userId":"admin"}` (no password)
- Assert response contains `HTTP/1.1 400 Bad Request`

```
TEST(AccountLoginTest, EmptyBody_Returns400)
```
- POST with empty body
- Assert response contains `HTTP/1.1 400 Bad Request`

```
TEST(AccountLoginTest, LegacyPlainTextPassword_StillWorks_DuringMigration)
```
- Mock DB returns account doc with `accountPassword:"secret"` and NO `passwordHash`
- POST with `{"userId":"admin","password":"secret"}`
- Assert response is 200 (plain-text fallback active during migration)

```
TEST(AccountLoginTest, ResponseBody_ExcludesSensitiveFields)
```
- Mock DB returns full account doc including `passwordHash` and `loginCredentials`
- POST with valid credentials
- Parse the response body JSON
- Assert `passwordHash` key is absent
- Assert `loginCredentials.accountPassword` key is absent

**8 tests. All RED (handler not registered, routing not wired).**

### Step 2.2 — GREEN: Implement `handle_account_login_POST`

1. Add `handle_account_login_POST(std::string &in, IMongodbClient &dbInst)` to `MicroService`
2. Declare in `webservice.hpp`
3. Wire into `process_request()`: match `POST` + `/api/v1/account/login`
4. Implementation logic:
   - Parse JSON body
   - Extract `userId`, `password`
   - Validate both present → else 400
   - `dbInst.get_document("account", query, projection)` — query on `accountCode` only
   - If empty result → 401
   - If `passwordHash` exists → verify against it
   - Else fall back to comparing `loginCredentials.accountPassword` (migration path)
   - On match → strip sensitive fields, return 200
   - On mismatch → 401

### Step 2.3 — REFACTOR

- Extract `strip_sensitive_fields(json)` helper
- Consider extracting `find_account_by_code(db, code)` helper
- No change to public API

---

## Phase 3: Hash on account create/update

### Step 3.1 — RED: Create tests

```
TEST(AccountCreateTest, HashesPasswordBeforeInsert)
```
- POST `/api/v1/account/account` with body containing `loginCredentials.accountPassword: "secret"`
- Mock DB `create_document` returns a fake OID
- Call `handle_account_POST(request, mockDb)`
- Assert `mockDb.lastCreateDoc` (the document passed to create_document) does NOT contain `"secret"` as plain text
- Assert `mockDb.lastCreateDoc` contains a `"passwordHash"` field starting with `$pbkdf2-sha256$`

```
TEST(AccountCreateTest, NoPassword_StillWorks)
```
- POST body without `loginCredentials.accountPassword`
- Mock returns valid OID
- Call handler
- Assert `mockDb.lastCreateDoc` does NOT contain `"passwordHash"` (no password to hash)
- Assert response is 200

```
TEST(AccountCreateTest, EmptyPassword_StillWorks)
```
- POST body with `loginCredentials.accountPassword: ""`
- Call handler
- Assert does not crash, still creates document
```

```
TEST(AccountUpdateTest, PasswordChange_Rehashes)
```
- PUT `/api/v1/account/account?userId=admin` with body `{"loginCredentials":{"accountPassword":"newpass"}}`
- Mock `update_collection` returns true
- Call `handle_account_PUT(request, mockDb)`
- Assert `mockDb.lastUpdateDoc` does NOT contain `"newpass"` as plain text
- Assert `mockDb.lastUpdateDoc` contains a `"passwordHash"` field in the `$set` value

```
TEST(AccountUpdateTest, NoPasswordChange_DoesNotTouchHash)
```
- PUT with body `{"personalInfo":{"name":"New Name"}}` (no password)
- Mock returns true
- Call handler
- Assert `mockDb.lastUpdateDoc` does NOT contain `"passwordHash"`
```

**5 tests. All RED.**

### Step 3.2 — GREEN: Modify create/update handlers

- `handle_account_POST`: After parsing body JSON, if `loginCredentials.accountPassword` is present and non-empty, call `MongodbClient::hash_password()`, store result as `passwordHash`, remove `accountPassword` from the stored document
- `handle_account_PUT`: Before building `$set`, check if the body contains `loginCredentials.accountPassword`. If so, hash it, add `passwordHash` to the `$set`, remove `accountPassword` from the `$set`

### Step 3.3 — REFACTOR

- Extract `hash_password_in_doc(json)` helper that mutates the JSON in place

---

## Phase 4: Seed data update

No TDD cycle — this is a data change. But we can add a validation test:

```
TEST(SeedDataTest, BootstrapAdminHasHashedPassword)
```
- Parse `mongo-init.js` account document
- Assert it does NOT contain `accountPassword` as a plain string
- Assert it contains `passwordHash` in valid modular-crypt format

---

## Phase 5: Migration tool

### Step 5.1 — RED: Migration tests

```
TEST(MigrationTest, Backfill_HashesPlainTextPassword)
```
- Mock DB `get_documents` returns: `[{"loginCredentials":{"accountCode":"u1","accountPassword":"p1"}}]`
- Call migration function
- Assert `update_collection` was called with filter `{"loginCredentials.accountCode":"u1"}`
- Assert the update document contains `$set` with a `passwordHash` field

```
TEST(MigrationTest, Backfill_SkipsAlreadyHashed)
```
- Mock DB returns doc that already has `passwordHash`
- Call migration
- Assert `update_collection` was NOT called for this document

```
TEST(MigrationTest, Backfill_SkipsDocWithNoPassword)
```
- Mock DB returns doc with no `accountPassword` and no `passwordHash`
- Call migration
- Assert `update_collection` was NOT called

```
TEST(MigrationTest, MigrationFlag_ParsesCorrectly)
```
- Verify `--migrate-passwords` flag is recognized by CLI parser
```

**4 tests. All RED.**

### Step 5.2 — GREEN: Implement migration

- Add `--migrate-passwords` flag to webservice_main.cpp
- On startup, if set: iterate `account` collection, hash all plain-text passwords, update, exit 0
- Uses existing `MongodbClient` — no new infrastructure

---

## Phase 6: Remove plain-text fallback (final cleanup)

This phase runs AFTER migration is complete in production.

### Step 6.1 — RED: Tests

```
TEST(AccountLoginTest, PlainTextFallback_Removed_Returns401)
```
- Mock DB returns account doc with `accountPassword:"secret"` and NO `passwordHash`
- POST with `{"userId":"admin","password":"secret"}`
- Assert response is 401 (not 200 — plain-text fallback is removed)

```
TEST(AccountLoginTest, PlainTextFallback_Removed_LogsWarning)
```
- Same setup
- Assert a warning-level log message about missing passwordHash is emitted
```

### Step 6.2 — GREEN: Remove fallback

- In `handle_account_login_POST`, remove the `else if (accountPassword == ...)` branch
- Return 401 if `passwordHash` is absent

---

## Phase 7: Enforce SSL on wsdbagent

### Step 7.1 — RED: CLI validation test

```
TEST(WsDbAgentConfigTest, NoSslFlag_Rejected)
```
- Parse `--no-ssl` flag
- Assert it is rejected (error message or non-zero exit)

```
TEST(WsDbAgentConfigTest, MissingTlsArgs_Rejected)
```
- Parse args without `--tls-ca`
- Assert error and non-zero exit (mTLS is now mandatory)

```
TEST(WsDbAgentConfigTest, AllTlsArgs_Accepts)
```
- Parse with `--tls-ca`, `--tls-cert`, `--tls-key`
- Assert configuration is accepted

### Step 7.2 — GREEN: Remove `--no-ssl`

- Remove `--no-ssl` flag handling from `wsdbagent_main.cpp`
- Make mTLS args required
- Remove `m_ssl` branching from `WsDbAgent` — always use SSL stream

---

## Phase 8: TLS-over-Transport (inner encryption) ✅ IMPLEMENTED

The inner TLS layer wraps a generic `ITransport` (not socket-specific), making it testable without real WebSocket connections.

### Architecture for testability

Extract a `ITransport` interface so the inner TLS can be tested without real sockets:

```cpp
class ITransport {
public:
  virtual ~ITransport() = default;
  virtual bool send(const std::vector<std::uint8_t>& data) = 0;
  virtual bool recv(std::vector<std::uint8_t>& data) = 0;
};
```

### Implemented files

| File | Purpose |
|------|---------|
| `modules/module/security/inc/innertls.hpp` | ITransport, InnerTlsClient, InnerTlsServer (smart pointers) |
| `modules/module/security/src/innertls.cpp` | OpenSSL memory BIO implementation (~210 lines) |
| `modules/module/security/test/innertls_test.cc` | 7 tests with MockTransport test double |

### Design decisions

- **Smart pointers**: `std::unique_ptr<SSL_CTX, detail::SslCtxDeleter>` and `std::unique_ptr<SSL, detail::SslDeleter>` — zero-overhead custom deleters, no raw owning pointers
- **Separate client/server classes**: `InnerTlsClient` and `InnerTlsServer` rather than a single `InnerTlsChannel` — cleaner API, no runtime state flags
- **Memory BIOs**: `BIO_s_mem()` for both rbio and wbio. SSL_set_bio takes ownership — BIO pointers are non-owning after that call
- **Handshake loop critical fix**: wbio must be flushed on EVERY iteration (including before checking if recv() is empty). SSL_connect writes ClientHello to wbio before returning WANT_READ
- **Post-handshake flush**: wbio flushed after SSL_connect/SSL_accept returns 1 — TLS 1.3 Finished is buffered in wbio on completion

### Step 8.1 — RED: Inner TLS tests (7 implemented, 6 deferred)

**Implemented (GREEN):**

```
TEST(InnerTlsTest, Handshake_CompletesSuccessfully)     — two MockTransports wired back-to-back
TEST(InnerTlsTest, Handshake_Fails_ReturnsFalse)        — garbage data fed to client
TEST(InnerTlsTest, EncryptedData_DiffersFromPlaintext)  — ciphertext != plaintext on wire
TEST(InnerTlsTest, Roundtrip_LargePayload)              — 1 MB payload roundtrip
TEST(InnerTlsTest, MultipleMessages_StayInOrder)         — 10 sequential messages
TEST(InnerTlsTest, Mitm_PlaintextInsteadOfTls_DetectedByClient)  — non-TLS frame rejected
TEST(InnerTlsTest, Mitm_TamperedFrame_Detected)          — byte modification detected
```

**Deferred (mTLS-specific — require client certs not yet generated in test):**
- ClientRejectsUntrustedServerCert
- ServerRejectsClientWithoutCert
- Mitm_ReplayOldHandshake_Fails
- Mitm_StrippingInnerTls_DetectedByServer
- NoDbTrafficBeforeInnerTls (design assertion, not runtime test)
- Handshake_EnforcesMutualTls

### Step 8.2 — GREEN: Implementation

Two classes in `modules/module/security/`:

```cpp
class InnerTlsClient {
  ITransport &m_transport;
  SslCtxPtr   m_ctx;    // unique_ptr<SSL_CTX, SslCtxDeleter>
  SslPtr      m_ssl;    // unique_ptr<SSL, SslDeleter>
  BIO        *m_rbio;   // non-owning (owned by m_ssl after SSL_set_bio)
  BIO        *m_wbio;   // non-owning

  bool handshake();     // SSL_connect loop with wbio flush before every read
  bool send(...);       // SSL_write → flush_wbio
  bool recv(...);       // transport recv → BIO_write rbio → SSL_read
  void set_ca(...);     // SSL_CTX_load_verify_locations for mTLS
};

class InnerTlsServer {
  // Same structure but uses SSL_accept + cert/key loading in constructor
  InnerTlsServer(ITransport&, cert_path, key_path, ca_path = "");
  bool accept();        // SSL_accept loop (mirrors client handshake structure)
  bool send(...);
  bool recv(...);
};
```

Key implementation details:
- `SSL_CTX_set_min_proto_version(TLS1_2_VERSION)` — minimum TLS 1.2
- Handshake loop: flush_wbio() → recv from transport → BIO_write to rbio → SSL_connect/accept → repeat
- Critical: flush_wbio() after SSL_connect/accept returns 1 (catches TLS 1.3 Finished)
- `ERR_clear_error()` on all error paths (prevents error queue pollution)
- `~InnerTlsClient() = default` — smart pointers handle cleanup

### Build integration

- `CMakeLists.txt` (root): `include_directories(modules/module/security/inc)` + `${MODULE_SECURITY_SOURCES}` added to uniservice and wsdbagent
- `test/CMakeLists.txt`: security inc path added, test source path updated
- `docker/Dockerfile`: cert generation (`openssl req -x509 ...`) added before cmake build
- **133/134 tests pass** (1 pre-existing flaky: WsDbServer.SecondAgentRejected_When_FirstAlive)

### Step 8.3 — REFACTOR (completed)

- Moved from raw OpenSSL pointers to `std::unique_ptr` with custom deleters
- Extracted to dedicated `modules/module/security/` module (inc/src/test)
- `ITransport` decouples TLS from any specific transport — reusable for non-WebSocket use cases

---

## Phase 9: Angular frontend changes ✅ IMPLEMENTED

### Changes made (3 files)

**`ui/src/common/app-globals.ts`:**
- Added login URI entry: `["from_web_login", "/api/v1/account/login"]`

**`ui/src/common/httpsvc.service.ts`:**
- Changed `getAccountInfo()` from GET with query params to POST with JSON body:
```typescript
getAccountInfo(id:string, pwd?: string): Observable<Account> {
  const body: any = { userId: id };
  if (pwd && pwd.length > 0) {
    body.password = pwd;
  }
  return this.http.post<Account>(this.getUri("from_web_login"), body);
}
```

**`ui/src/app/login/login.component.ts`:**
- Removed dead `getHash32` function (unused legacy code)
- Removed unused `cName` variable in `onLogin()`
- `onLogin()` now extracts passwd and id from form, calls `getAccountInfo(id, passwd)`
- Password is now sent in POST JSON body — no longer appears in URL/query params

### What was NOT done

- Jasmine/Karma unit tests were not written (frontend test infrastructure not set up in this project)
- The Angular UI compiles cleanly (`ng build --configuration production`) with these changes
- Backend `POST /api/v1/account/login` handler implementation (Phase 2) is needed for end-to-end login to work

---

## Execution order (dependency graph)

```
Phase 1 (hash helpers) ─────────────────────────────────────────────┐
     │                                                               │
     ├── Phase 2 (login endpoint) ── depends on Phase 1              │
     │       │                                                       │
     │       ├── Phase 3 (hash on create/update) ── depends on Phase 1
     │       │                                                       │
     │       ├── Phase 4 (seed data) ── depends on Phase 1           │
     │       │                                                       │
     │       └── Phase 9 (Angular) ── depends on Phase 2             │
     │                                                               │
     ├── Phase 5 (migration) ── depends on Phase 1                   │
     │       │                                                       │
     │       └── Phase 6 (remove fallback) ── depends on Phase 5     │
     │                                                               │
     └── Phase 7 (enforce SSL) ──────────────────────────────────────┤
             │                                                       │
             └── Phase 8 (TLS-over-WS) ── depends on Phase 7         │
```

Phases 1, 2, 3, 4, 7, 9 can start in parallel by different developers once Phase 1 is done.

---

## Test count summary

| Phase | Planned tests | Implemented | Status | Module |
|-------|--------------|-------------|--------|--------|
| 1. Hash helpers | 11 | 0 | pending | mongodb |
| 2. Login endpoint | 8 | 0 | pending | webservice |
| 3. Create/update hashing | 5 | 0 | pending | webservice |
| 4. Seed data | 1 | 0 | pending | (validation) |
| 5. Migration tool | 4 | 0 | pending | webservice |
| 6. Remove fallback | 2 | 0 | pending | webservice |
| 7. Enforce SSL | 3 | 0 | pending | wsdbagent |
| 8. TLS-over-Transport | 13 | **7** | partial (mTLS tests deferred) | security |
| 9. Angular | 10 | **3 files changed** | done (no unit tests) | ui |
| **Total** | **57** | **10** | | |

Existing tests stay green throughout (133/134 pass — 1 pre-existing flaky test).
