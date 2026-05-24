# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

---

## Build and run

All C++ compilation happens inside containers. There is no native build path — ACE/TAO, mongo-cxx-driver, and googletest are pre-built into a shared toolchain image `localhost/xpmile-cpp-builder:bootstrap` (built from `docker/Dockerfile.bootstrap`). Both `docker/Dockerfile` (uniservice) and `docker/Dockerfile.wsdbagent` start with `FROM ${BUILDER_IMAGE}` so the ~30 min toolchain compile is done once and reused across services. CI publishes the same image to `docker.io/naushada/xpmile-cpp-builder:bootstrap` so the workflow doesn't re-build it on every push.

### Container stack

Use `run.sh` for common operations (wraps `podman-compose`):

```sh
./run.sh build              # full build — C++ + Angular (auto-builds bootstrap on first run)
./run.sh build-bootstrap    # build the shared C++ toolchain image (one-time, ~30 min cold)
./run.sh build-ui           # rebuild Angular only, reuses C++ cache (~3 min)
./run.sh start              # start MongoDB + app
./run.sh start remote       # start in --remote-db mode (wsdbagent on another machine)
./run.sh stop               # stop containers (data preserved)
./run.sh restart            # stop then start
./run.sh logs               # follow logs from both containers
./run.sh logs app           # app (uniservice) logs only
./run.sh logs db            # MongoDB logs only
./run.sh status             # show container status
./run.sh clean              # stop + delete MongoDB data volume
```

`build` and `build-ui` auto-invoke `build-bootstrap` when `localhost/xpmile-cpp-builder:bootstrap` is missing — no manual two-step required after a `podman prune`.

Raw `podman-compose` equivalents (if needed):

```sh
podman-compose up --build                          # build + start
UI_BUST=$(date +%s) podman-compose build app       # Angular-only rebuild
REMOTE_DB=1 podman-compose up -d                   # remote-db mode
podman-compose down                                # stop
```

The `app` service is the only service name relevant for rebuilds. `mongodb` has its own image baked with `mongo-init.js`.

### Heroku deployment

**Pushes to `main` auto-deploy** via `.github/workflows/publish-images.yml` — see the *Continuous integration* section below. `deploy-heroku.sh` is the manual escape hatch (hotfixes, branch builds, offline use). Use `deploy-heroku.sh` (wraps podman + heroku CLI):

```sh
./deploy-heroku.sh login                  # authenticate with registry.heroku.com (once per session)
./deploy-heroku.sh build-bootstrap        # build the shared C++ toolchain image (amd64, ~30 min cold)
./deploy-heroku.sh deploy                 # rebuild UI + extract agent certs + push + release  (typical redeploy)
./deploy-heroku.sh deploy full            # full C++ + Angular build + extract + push + release
./deploy-heroku.sh build                  # full build only (no push)
./deploy-heroku.sh build-ui               # Angular-only rebuild (no push)
./deploy-heroku.sh push                   # push previously built image
./deploy-heroku.sh release                # release (activate) the pushed image
./deploy-heroku.sh extract-agent-certs    # extract wsdbagent client cert family from the local image
./deploy-heroku.sh logs                   # tail live Heroku logs
./deploy-heroku.sh open                   # open the app in the browser
```

- `build` / `build-ui` / `deploy` auto-invoke `build-bootstrap` when the toolchain image is missing or wrong arch.
- `deploy` auto-invokes `extract-agent-certs` after the build, writing `./certs/cloud-issued/innertls/` so the operator can `scp` the rotated client cert family to the on-prem MongoDB machine. Skip the scp if the operator is going to `./run-agent.sh refresh-certs` from Docker Hub instead.

Default app is `marvel`. Override with `HEROKU_APP=<name> ./deploy-heroku.sh deploy`.

Raw equivalents (if needed):

```sh
heroku auth:token | podman login --username=_ --password-stdin registry.heroku.com
HEROKU_APP=marvel UI_BUST=$(date +%s) podman-compose -f docker-compose.heroku.yml build
podman push --format=v2s2 registry.heroku.com/marvel/web
heroku container:release web --app marvel
```

`docker-compose.heroku.yml` handles `--platform linux/amd64` and the image tag automatically. See `docs/app.md` for config vars, UI_BUST cache-busting, and wsdbagent setup.

### Continuous integration

`.github/workflows/publish-images.yml` runs on every push to `main` that touches `docker/Dockerfile*`, `modules/**`, `ui/**`, `test/**`, `certs/**`, `CMakeLists.txt`, or the workflow file itself. Four jobs:

1. **bootstrap** — builds `docker.io/naushada/xpmile-cpp-builder:{bootstrap,<sha>}` (multi-arch: amd64 + arm64).
2. **test** — builds `docker/Dockerfile.test` against the just-published bootstrap and runs the `offtarget` GTest binary. Three Mongo-dependent tests are excluded by the Dockerfile.test CMD filter. A failed test (or compile error) blocks jobs 3 and 4.
3. **wsdbagent** — publishes `docker.io/naushada/xpmile-wsdbagent:{latest,<sha>}` (multi-arch).
4. **uniservice + release** — builds amd64-only. One buildx push tags the image at four places: `docker.io/naushada/xpmile-uniservice:{latest,<sha>}`, `registry.heroku.com/marvel/web`, and `registry.heroku.com/idp/web`. Then `heroku container:release web` runs for both apps — the same image deploys to both. What turns the `idp` dyno into the IdP is the `IDP_ISSUER` Heroku config var on that app (`heroku config:set IDP_ISSUER=https://idp-63c97365e6ef.herokuapp.com --app idp`, set once after `heroku create idp`). When unset (as on marvel) every `/api/v1/idp/*` and `/.well-known/openid-configuration` route returns 503.

Required repo secrets: `DOCKERHUB_USERNAME`, `DOCKERHUB_TOKEN` (write-scope token), `HEROKU_API_KEY`. A `concurrency` block auto-cancels older in-flight runs for the same ref.

**PR gating**: pull requests against `main` trigger the same workflow but only the `bootstrap` and `test` jobs run (publish + Heroku release are guarded by `if: github.event_name != 'pull_request'`). Branch protection on `main` requires the `Run offtarget GTest suite` check to pass before merge, with `strict: true` (PR branch must be up-to-date with main) and admins exempt (`enforce_admins: false`).

### Clean up dangling images

```sh
podman rmi $(podman images -f "dangling=true" -q)
```

**Warning:** this also evicts `localhost/xpmile-cpp-builder:bootstrap` and the per-service `cpp-builder` intermediate images. The next `./deploy-heroku.sh deploy` (even the UI-only variant) will then auto-rebuild bootstrap (~30 min) before the per-service compile. Only prune when `podman system df` shows >80 % reclaimable, or when you can afford the rebuild. Full timing table in `codebase.md` → Build & deploy timing.

### wsdbagent stack (MongoDB machine behind NAT)

```sh
# First-time setup — one-shot
cp .env.agent .env                  # set SERVER_HOST=<your-heroku-app>.herokuapp.com
./run-agent.sh start                # auto-refresh certs (if missing) + bring up all 3 services

# After each CI/Heroku deploy (or every 15 min on a cron)
./run-agent.sh refresh-certs        # cert-watcher restarts wsdbagent automatically (~15 s)
```

Up to four core services: `mongodb` (built locally from `docker/Dockerfile.mongo`, ~30 s), `wsdbagent` (pulled from `docker.io/naushada/xpmile-wsdbagent:latest` — CI-published per deploy; pin with `WSDBAGENT_IMAGE=...:<sha>`), the optional `wsdbagent-idp` (same image, dedicated to the IdP Heroku app — included only when `IDP_SERVER_HOST` is set in `.env`), and `xpmile-cert-watcher` (alpine sidecar; `docker.io/library/alpine:3.19`). The watcher md5sums `./certs/cloud-issued/innertls/` every 5 s and POSTs a restart to *both* agents via the host podman socket on any change — the idp agent's restart endpoint returning 404 is treated as benign (marvel-only stack). Same image to both Heroku apps means one InnerTLS CA on-prem and one shared client cert family — `refresh-certs` rotates the single set; the cert-watcher fans the restart out. Full rotation story in `docs/ws-db-agent.md`.

`./run-agent.sh start` auto-invokes `refresh-certs` when `./certs/cloud-issued/innertls/` is missing or empty, and auto-detects `IDP_SERVER_HOST` to decide whether to bring up the second agent. The first-time flow is one-shot whether you run a marvel-only stack or marvel + idp.

### Running tests

Tests run inside the built image as the `offtarget` binary:

```sh
# From inside the cpp-builder stage (or in a running container with access to /src/build)
cd /src/build && ctest --output-on-failure

# Run the test binary directly (more useful for filtering)
./offtarget --gtest_filter='Http*'          # http parser tests only
./offtarget --gtest_filter='MicroService*'  # response builder tests only
./offtarget --gtest_filter='EmailService*'  # SMTP FSM tests only
```

The suite spans every module with tests (http, webservice, email, mongodb, wsdbproxy, security, sso). The SSO phases alone added 127 tests — see `docs/design/sso/sso-tdd-plan.md`. Zero failures is the baseline.

### Angular UI (local development)

```sh
cd ui
npm install
ng serve          # dev server at http://localhost:4200, proxies API to backend
ng build --configuration production   # production build (slow; use Docker for final output)
```

The production build requires `NODE_OPTIONS=--max_old_space_size=1536` — pdfMake's vfs_fonts is large. The Dockerfile already sets this. For local dev builds, prefix accordingly if you hit OOM.

---

## Architecture

See `codebase.md` for the full reference. The key mental model:

**Runtime path for a single HTTP request:**

```
TCP connect → WebServer::handle_input()        accepts, wraps in WebConnection
            → WebConnection::handle_input()    buffers bytes until Http::message_length() > 0
            → enqueue WorkCtx* in ACE_Message_Block on next MicroService (round-robin)
            → MicroService::svc()              dequeues WorkCtx, calls process_request()
            → handle_GET / POST / PUT / DELETE / OPTIONS
            → MongodbClient (shared pool, thread-safe)
            → http_send() writes response
```

**Three classes in `webservice/`:**
- `WebServer` — owns the reactor loop, `MongodbClient` pool, and `MicroService` worker vector
- `WebConnection` — per-socket handler; accumulates partial reads
- `MicroService` — ACE_Task worker thread; all routing and business logic lives here

**`WorkCtx`** (anonymous struct in `webservice.cpp`) is the work item passed through the message queue:
```cpp
struct WorkCtx { ACE_HANDLE handle; MongodbClient *db; std::string request; };
```

**MongoDB:** One `MongodbClient` per process (enforced by `mongocxx::instance` singleton). Workers share it; `pool::acquire()` is thread-safe. The `ACE_Semaphore` in `WebServer` is a startup barrier only (not a DB mutex).

**Remote-DB mode (`--remote-db`):** `WsMongodbProxy` replaces `MongodbClient`. All DB calls are forwarded as BSON messages over a WebSocket to `wsdbagent` running on the MongoDB machine. `WsDbServer` owns the agent connection. On Heroku, `WebConnection` hands off the upgraded socket (see hand-off mechanics below); on self-hosted, `WsDbServer` binds its own mTLS port. See `docs/ws-db-agent.md`.

**WebSocket hand-off mechanics (Heroku mode):** When `WebConnection::handle_input()` detects a WS upgrade to `/ws/db`, it must remove itself from the reactor *before* clearing `m_handle`. Order matters: `reactor()->remove_handler(this, READ_MASK | DONT_CALL)` → `m_handle = ACE_INVALID_HANDLE` → `wsDbServer()->on_agent_connected(raw)` → `connectionPool().erase(raw)`. If `m_handle` is cleared first, `remove_handler` calls `get_handle()` internally, gets `-1`, and the fd is never removed from epoll — the reactor then dispatches to the deleted `WebConnection` the next time the socket is readable.

**HTTP header case:** `Http::add_element()` and `get_element()` lowercase all keys. Heroku normalises `Sec-WebSocket-Key` → `Sec-Websocket-Key`; the lowercase lookup handles this transparently.

**Collection name:** Shipments are stored in the `shipping` collection — not `shipment`. The API path `/api/v1/shipment/...` is unrelated to the collection name.

**AWB generation:** If `shipment.isAutoGenerate == true`, the backend looks up `awbPrefix` from the sender's account, then calls `MongodbClient::next_awbno(prefix)` — an atomic `findOneAndUpdate + $inc` on the `counters` collection. This runs before any insert, in both single and bulk handlers.

---

## Single sign-on (SSO)

The `modules/module/sso/` directory implements SSO — server-side sessions, OIDC, and SAML 2.0. All code is in the `sso::` namespace. Design: `docs/design/sso/sso-design.md`; test plan: `sso-tdd-plan.md`. `codebase.md` → *sso module* has the file-by-file reference.

**Backend-for-frontend (BFF):** the C++ backend runs the entire IdP handshake; IdP tokens never reach the browser. Every login — federated *and* password — mints a `sessions` record and sets an opaque `HttpOnly` cookie (`xpmile_session`). The cookie is a random id, not a JWT, so a session is revocable by deleting the record.

**SSO routing:** `/api/v1/sso/*` is dispatched in `process_request()` to `MicroService::handle_sso()`. Keep endpoint *logic* in the transport-agnostic functions in `sso_endpoints.hpp` — they return a `SsoHttpResult` and know nothing about ACE or the `Http` parser; `handle_sso()` only parses the request and renders the result onto the wire. That split is what keeps the SSO endpoints unit-testable with mocks.

**Adding a provider protocol:** implement `sso::IIdentityProvider` (`begin_login` / `handle_callback`). `OidcProvider` and `SamlProvider` are the existing two; the endpoint handlers depend only on the interface, so callers do not change.

**SSO config lives in MongoDB, not git:** the `sso_config` collection holds one secret-bearing document, authored by the on-prem Vaadin admin UI (`onprem/.../SsoConfigView.java`). The backend hot-reloads it every ~60 s via `WebServer::init_sso()` / `reload_sso()`; a document that fails `parse_sso_config()` is rejected and the last-good `ProviderRegistry` kept. Never put SSO secrets in a Heroku config var or commit them.

**Security invariants — do not weaken:** `verify_jwt()` accepts RS256 only (reject `none` and every HMAC variant — algorithm-confusion attack). SAML signatures are verified by xmlsec1 against the configured IdP cert only; an embedded `<KeyInfo>` is ignored. Callback URLs are pinned to `publicBaseUrl`, never derived from request headers. The response builders echo a *specific* allowed origin plus `Access-Control-Allow-Credentials: true` — never reintroduce `Access-Control-Allow-Origin: *` on a cookie-bearing endpoint; browsers reject that pair.

**xmlsec1 dependency:** SAML XML-DSig needs `libxml2` + `xmlsec1`. The root `CMakeLists.txt` does `pkg_check_modules(XMLSEC REQUIRED xmlsec1-openssl)`, which cmake resolves at configure time for *every* target — so all three builder Dockerfiles (`Dockerfile`, `Dockerfile.test`, `Dockerfile.wsdbagent`) `apt-get install libxmlsec1-dev`, including `wsdbagent` even though its binary never links xmlsec1. Only the `uniservice` runtime stage ships the `libxmlsec1`/`libxmlsec1-openssl` runtime libs.

**Unit tests:** `modules/module/sso/test/sso_test.cc` (114 tests), plus the session / CORS / middleware tests in `webservice_test.cc`. All run against mocks behind `IMongodbClient` / `IHttpClient` — no live MongoDB, no network — inside the standard `offtarget` binary.

---

## In-house identity provider (IdP)

The `modules/module/inhouseidp/` directory implements an OIDC IdP that xpmile runs itself, instead of delegating to a third party. All code is in the `idp::` namespace. Design: `docs/design/inhouse-idp/inhouse-idp-design.md`; test plan: `inhouse-idp-tdd-plan.md` (the *Implementation status* table tracks which phases have shipped). `codebase.md` → *inhouseidp module* has the file-by-file reference. The IdP coexists with the v1.0 SSO machinery — the SSO module turns xpmile into an OIDC/SAML *client*; the inhouseidp module turns it into an OIDC *issuer*.

**Two-Heroku-app architecture:** the same `uniservice` binary serves both the marvel app (xpmile UI + API) and a second `idp` Heroku app (login portal). Which posture a dyno takes is purely a deploy-time decision, controlled by the `IDP_ISSUER` env var. When unset (marvel), every `/api/v1/idp/*` and `/.well-known/openid-configuration` route returns 503 with `"IdP not enabled on this dyno"`. When set (e.g. `https://idp-63c97365e6ef.herokuapp.com`), the IdP routes activate. Both apps share *one* on-prem MongoDB via *two* `wsdbagent` instances behind the same NAT (Phase I — pending).

**Dual-database layout — no cross-DB reads except legacy login:** the `xpmile` database holds business state (shipments, accounts as business records). A separate `idp` database holds auth state: `idp.account` (passwordHash + email + name + role), `idp.sessions`, `idp.idp_clients`, `idp.idp_signing_keys`, `idp.idp_pending_auth`, `idp.idp_codes`, `idp.idp_access_tokens`, `idp.password_reset_tokens`. The `scripts/migrate-account-split.py` migration (Phase pre-A) splits the auth fields out of `xpmile.account` into `idp.account` idempotently. The only intentional cross-DB read is the legacy `POST /api/v1/account/login` fallback (Phase K — pending), which reads `idp.account` from a marvel-side webservice; Q12 of the design explicitly kept legacy password login as the fallback rather than deprecating it.

**On-prem JWT signing — the private key never leaves the NAT:** when the cloud-side IdP needs to sign an `id_token`, it does *not* hold the RSA private key. Instead, it sends a `SIGN_JWT` request over the existing dbproto WebSocket to `wsdbagent`, which reads the active key from `idp.idp_signing_keys`, signs, and returns the base64url signature plus the resolved kid. `agent::sign_jwt_on_prem` is the only code path that ever sees the private key bytes. `idp::WsdbJwtSigner` is the cloud-side proxy that implements `idp::IJwtSigner` over this channel — and rejects every alg other than RS256 *before* any wire I/O.

**IdP routing:** `handle_idp()` in `webservice.cpp` dispatches the IdP routes the same way `handle_sso()` does — parse, call the transport-agnostic handlers in `modules/module/inhouseidp/inc/idp_*.hpp`, render the `SsoHttpResult` (reused from v1.0 — the contract is identical). The IdP-side cookies live at `Path=/api/v1/idp/` so they're never sent to the marvel app: `xpmile_idp_session` (12 h IdP login session) and `xpmile_idp_pending` (10 min, ties `/login` back to the originating `/authorize`). All 8 routes are wired today: `/.well-known/openid-configuration`, `/api/v1/idp/jwks`, `/api/v1/idp/authorize`, `/api/v1/idp/userinfo`, `/api/v1/idp/end_session`, `/api/v1/idp/login`, `/api/v1/idp/token`, and `/api/v1/idp/password/{reset_request,reset_confirm}`. `/login` uses `PbkdfPasswordVerifier` (thin wrapper around the existing `MongodbClient::verify_password` PBKDF2 routine, so the IdP shares the marvel app's password-hash scheme — no schema migration). `/token` uses `WsdbJwtSigner` over `WebServer::wsDbServer()`; returns 503 in local-DB mode (Heroku always uses `--remote-db`, so this is benign there). `/password/*` uses `PbkdfPasswordHasher` + `SmtpEmailSender` (the latter reads `SMTP_FROM_EMAIL` + `SMTP_FROM_PASSWORD` + optional `SMTP_FROM_NAME` from env on every send; missing creds → `send()` returns false silently so SMTP outages can't be turned into an account-enumeration side channel).

**ui-idp Angular SPA:** the IdP host serves a small Angular 14 app at `/idp/*` (Path-scoped cookies, `--base-href /idp/`). Three pages: `/idp/login` (credential form), `/idp/password/reset` (email entry), `/idp/password/reset/confirm?token=…` (new-password form). All three subscribe to `PubsubsvcService`'s `BehaviorSubject`s (`onLoginPending`/`onLoginError` + `onResetPending`/`onResetError`/`onResetNotice`) — same idiom as `ui/src/common/pubsubsvc.service.ts`. The portal assumes JavaScript: `/login` POST returns 200 + `{redirect_to}` (not a 302) so the SPA can render inline errors and then assign `window.location` itself. Password reset returns 200 unconditionally for `reset_request` (no enumeration); `reset_confirm` 4xx surfaces via `error_description`.

**On-prem admin views:** the Vaadin app (`onprem/`) carries two IdP-specific views — `IdpSigningKeysView` (Phase B; generate / activate / set-notAfter / delete RSA-2048 keypairs in `idp.idp_signing_keys`; private key bytes never reach the UI) and `IdpClientsView` (Phase J; register / edit / delete RPs in `idp.idp_clients`). Both reach mongo directly through `IdpSigningKeyService` / `IdpClientService`, scoped to the `idp` database (not `xpmile`) — and the cloud-side `WebServer::reload_idp()` poll picks up changes within ~60 s.

**Operator scripts** — two wrappers cover the deploy steps that aren't in the CI loop:
- `./run-idp.sh` (host-side, talks to the Heroku CLI) — `init` does the one-time `stack:set container` + `config:set` for IDP_ISSUER / REMOTE_DB / SMTP_FROM_*  + `ps:scale web=1`, asking for explicit `YES` before the irreversible stack switch. `release` re-releases the image already in `registry.heroku.com/idp/web` (use after CI publishes but stack:set was the only blocker). `verify` curls `/.well-known` + `/jwks` and pretty-prints — catches the common live-deploy regressions (missing `IDP_ISSUER`, no active signing key, wsdbagent not connected).
- `./scripts/seed-default-idp-sso.sh` (on-prem mongosh) — one-shot marvel ↔ in-house-IdP wiring. Idempotently upserts the `inhouse` OIDC provider into `xpmile.sso_config.providers` (so marvel's login screen renders "Sign in with xpmile IdP") AND the `xpmile-spa` client into `idp.idp_clients` (so `/authorize` accepts it). Required env: `MARVEL_BASE_URL`, `IDP_ISSUER_URL`. Doesn't touch `idp.idp_signing_keys` — strong RSA-2048 keypair generation stays in the Vaadin **IdP Signing Keys → Generate** view (Java's `KeyPairGenerator`). Full operator walkthrough: [`docs/inhouse-idp.md`](docs/inhouse-idp.md).

**Security invariants — do not weaken:** algorithm is RS256 only — the same restriction as `sso::verify_jwt`. The OIDC authorization code is consumed by an *atomic* `update_collection` with filter `{_id, consumed:{$exists:false}}` — a second concurrent `/token` call for the same code gets 400 `invalid_grant "code already used"` because the filter matches zero docs. The PKCE `code_verifier` is checked with `sso::code_challenge` (S256). The RP's `redirect_uri` and `post_logout_redirect_uri` are validated by **exact byte equality** against the `IdpClientRegistry`. `/login` returns the same `401 invalid_credentials` body whether the account is missing or the password is wrong — no enumeration. `/reset_request` always returns 200 (even for unknown emails) for the same reason. The id_token header carries the *resolved* kid from the signer, not the placeholder — a single extra wsdbagent round trip per token issuance avoids client-side kid lookup confusion.

**IdP config lives in MongoDB, not git:** `idp.idp_clients` holds one document per registered RP (clientId, name, redirectUris list, postLogoutRedirectUris list, scopes, grantTypes). The on-prem Vaadin `IdpClientsView` (Phase J — pending) is the authoring surface; the C++ backend hot-reloads the registry every ~60 s via `WebServer::init_idp()` / `reload_idp()`, same pattern as `init_sso/reload_sso`. `idp.idp_signing_keys` holds one document per RSA keypair (kid, public key PEM, private key PEM, alg, active, notAfter); the on-prem Vaadin admin (Phase B — pending) is the authoring surface. **Never** put IdP secrets in a Heroku config var or commit them — same rule as SSO.

**Unit tests:** 118 GTest under the `Idp*`, `Jwks`, `PasswordReset*`, and `SignJwt*` prefixes, spread across nine files in `modules/module/inhouseidp/test/` + one in `modules/module/wsdbagent/test/`. Plus 12 pytest for the migration script (`scripts/test_migrate_account_split.py`). The C++ tests run in the standard `offtarget` binary against the same `IMongodbClient` mock the v1.0 SSO tests use, with a build-time-generated RSA-2048 fixture keypair (`test/CMakeLists.txt` — `idp_test_keys` target) for the end-to-end sign-then-verify roundtrip in `IdpToken.EndToEnd_RealRsaSign_VerifyWithJwks`. The pytest tests run inside an ephemeral podman container (`scripts/run-script-tests.sh`) — nothing installs on the host.

---

## Key conventions

**Adding a new API endpoint:** Add a `handle_<resource>_<METHOD>` method to `MicroService` in `webservice.hpp` / `webservice.cpp`, then wire it into `process_request()` with a URI prefix check matching the existing pattern.

**Unit tests:** `MicroService` has a default constructor (`m_parent = nullptr`) for test use. Methods under test (`build_responseOK`, `get_contentType`, `handle_OPTIONS`, etc.) never call `webServer()`, so the null parent is safe. Test file: `modules/module/webservice/test/webservice_test.cc`.

**JSON body handling:** `nlohmann::json` (`json.hpp`) is used everywhere. The `mongodbc` module parses JSON arrays with `nlohmann::json::parse()` and converts each element via `bsoncxx::from_json(elem.dump())` before calling `insert_many`.

**pdfMake (Angular):** Always build the full `docDef` object fresh inside the handler method — never store it or its sub-arrays as class properties. Import and set `pdfMake.vfs` at module level.

**`$any()` cast (Angular templates):** `FormGroup.controls[key]` returns `AbstractControl`. Use `$any()` when binding to `[formControl]` on Clarity inputs.

**C++ standard:** C++20 (`-std=c++2a`) for `uniservice`, `wsdbagent`, and `offtarget`. The `mongodbcxx` static library uses C++17 (`-std=c++17`).

**`BUILD_TESTS` CMake option:** `add_subdirectory(test)` is guarded by `option(BUILD_TESTS ... ON)`. Pass `-DBUILD_TESTS=OFF` when building `wsdbagent` without GTest (as `Dockerfile.wsdbagent` does).
