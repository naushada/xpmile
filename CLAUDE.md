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
4. **uniservice + release** — builds amd64-only, pushes to both `docker.io/naushada/xpmile-uniservice:{latest,<sha>}` and `registry.heroku.com/marvel/web`, then PATCHes the Heroku Platform API to release the new digest on the `web` dyno.

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

Three services: `mongodb` (built locally from `docker/Dockerfile.mongo`, ~30 s), `wsdbagent` (pulled from `docker.io/naushada/xpmile-wsdbagent:latest` — CI-published per deploy; pin with `WSDBAGENT_IMAGE=...:<sha>`), and `xpmile-cert-watcher` (alpine sidecar; `docker.io/library/alpine:3.19`). The watcher md5sums `./certs/cloud-issued/innertls/` every 5 s and POSTs a restart to `agent-wsdbagent` via the host podman socket on any change. It exists because `docker/Dockerfile` mints a fresh CA per uniservice build — wsdbagent's trust anchor + client cert pair must rotate in lockstep or the next reconnect fails with `tls_process_client_certificate verify failed`. Full rotation story in `docs/ws-db-agent.md`.

`./run-agent.sh start` auto-invokes `refresh-certs` when `./certs/cloud-issued/innertls/` is missing or empty, so the first-time flow is genuinely one-shot.

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

There are 46 tests across three modules (http, webservice, email). Zero failures is the baseline.

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

## Key conventions

**Adding a new API endpoint:** Add a `handle_<resource>_<METHOD>` method to `MicroService` in `webservice.hpp` / `webservice.cpp`, then wire it into `process_request()` with a URI prefix check matching the existing pattern.

**Unit tests:** `MicroService` has a default constructor (`m_parent = nullptr`) for test use. Methods under test (`build_responseOK`, `get_contentType`, `handle_OPTIONS`, etc.) never call `webServer()`, so the null parent is safe. Test file: `modules/module/webservice/test/webservice_test.cc`.

**JSON body handling:** `nlohmann::json` (`json.hpp`) is used everywhere. The `mongodbc` module parses JSON arrays with `nlohmann::json::parse()` and converts each element via `bsoncxx::from_json(elem.dump())` before calling `insert_many`.

**pdfMake (Angular):** Always build the full `docDef` object fresh inside the handler method — never store it or its sub-arrays as class properties. Import and set `pdfMake.vfs` at module level.

**`$any()` cast (Angular templates):** `FormGroup.controls[key]` returns `AbstractControl`. Use `$any()` when binding to `[formControl]` on Clarity inputs.

**C++ standard:** C++20 (`-std=c++2a`) for `uniservice`, `wsdbagent`, and `offtarget`. The `mongodbcxx` static library uses C++17 (`-std=c++17`).

**`BUILD_TESTS` CMake option:** `add_subdirectory(test)` is guarded by `option(BUILD_TESTS ... ON)`. Pass `-DBUILD_TESTS=OFF` when building `wsdbagent` without GTest (as `Dockerfile.wsdbagent` does).
