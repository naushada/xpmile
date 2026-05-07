# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

---

## Build and run

All C++ compilation happens inside Docker. There is no native build path — ACE/TAO, mongo-cxx-driver, and googletest are built from source in the `cpp-builder` stage and are not installed on the host.

### Container stack

```sh
# Build and start both services (MongoDB + app)
podman-compose up --build

# Build only the app container (C++ + Angular)
podman-compose build app

# Force Angular rebuild without invalidating the C++ layer
UI_BUST=$(date +%s) podman-compose build app

# Start without rebuilding
podman-compose up

# Stop and remove containers
podman-compose down
```

The `app` service is the only service name relevant for rebuilds. `mongodb` has its own image baked with `mongo-init.js`.

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

**Collection name:** Shipments are stored in the `shipping` collection — not `shipment`. The API path `/api/v1/shipment/...` is unrelated to the collection name.

**AWB generation:** If `shipment.isAutoGenerate == true`, the backend looks up `awbPrefix` from the sender's account, then calls `MongodbClient::next_awbno(prefix)` — an atomic `findOneAndUpdate + $inc` on the `counters` collection. This runs before any insert, in both single and bulk handlers.

---

## Key conventions

**Adding a new API endpoint:** Add a `handle_<resource>_<METHOD>` method to `MicroService` in `webservice.hpp` / `webservice.cpp`, then wire it into `process_request()` with a URI prefix check matching the existing pattern.

**Unit tests:** `MicroService` has a default constructor (`m_parent = nullptr`) for test use. Methods under test (`build_responseOK`, `get_contentType`, `handle_OPTIONS`, etc.) never call `webServer()`, so the null parent is safe. Test file: `modules/module/webservice/test/webservice_test.cc`.

**JSON body handling:** `nlohmann::json` (`json.hpp`) is used everywhere. The `mongodbc` module parses JSON arrays with `nlohmann::json::parse()` and converts each element via `bsoncxx::from_json(elem.dump())` before calling `insert_many`.

**pdfMake (Angular):** Always build the full `docDef` object fresh inside the handler method — never store it or its sub-arrays as class properties. Import and set `pdfMake.vfs` at module level.

**`$any()` cast (Angular templates):** `FormGroup.controls[key]` returns `AbstractControl`. Use `$any()` when binding to `[formControl]` on Clarity inputs.

**C++ standard:** C++20 (`-std=c++2a`) for `uniservice` and `offtarget`. The `mongodbcxx` static library uses C++17 (`-std=c++17`).
