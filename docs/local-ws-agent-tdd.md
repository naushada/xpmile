# TDD Plan: Local WebSocket Agent Proxy

Each phase: write the test first, watch it fail, then implement, then refactor.

---

## Phase 1 — `LocalWsListener` WebSocket session

**New class:** `LocalWsListener` — self-contained acceptor + session loop. Holds an
`ACE_SOCK_Acceptor`, accepts plain WS connections, upgrades, and runs a receive-dispatch-
respond loop. The `dispatch()` logic is the existing `WsDbAgent::dispatch()` at
wsdbagent.cpp:276, extracted as a private method.

**Test file:** `modules/module/wsdbagent/test/onprem_agent_test.cc`

Reuse `send_ws_binary()` and `recv_ws_binary()` from `wsdbserver_test.hpp`.

A minimal inline stub for `MongodbClient` lives in the test `.cc` file (not a reusable
class) — hardcoded returns for the few methods the session loop exercises.

### Tests — session I/O

| # | Test | What it verifies |
|---|------|-----------------|
| 1 | `AcceptsConnection` | ACE_SOCK_Acceptor on localhost:0 binds, client TCP connects successfully |
| 2 | `WebSocketUpgrade` | Client sends WS upgrade request, listener responds with 101 + correct accept key |
| 3 | `SingleRequestResponse` | Client sends WS binary frame with BSON request, receives WS binary frame with BSON response, reqid matches |
| 4 | `MultipleRequestsInSequence` | Three sequential request/response pairs on the same connection — all correct |
| 5 | `ClientDisconnect_CleansUp` | Client closes, listener returns to accept state, next client can connect |
| 6 | `MalformedRequest_NoCrash` | Client sends non-BSON binary frame, listener handles gracefully (no crash) |
| 7 | `PingPong` | Client sends WS ping (0x09), listener responds with pong (0x0A) |

---

## Phase 2 — `WsDbAgent` local WS wiring

Add `--local-ws-port` to `WsDbAgent`. When > 0, `run()` creates a `LocalWsListener`
thread alongside the existing cloud WSS session.

### Tests

| # | Test | What it verifies |
|---|------|-----------------|
| 8 | `LocalWsPort_Zero_NoListener` | WsDbAgent with local_ws_port=0 does not start a listener |
| 9 | `LocalWsPort_StartsListener` | WsDbAgent with local_ws_port>0 binds and accepts WS connections |
| 10 | `BothPaths_Concurrent` | Cloud WSS session (socketpair fake server) + local WS session both handle requests concurrently |

Test 10 setup: fake cloud uniservice on one socketpair (sends BSON requests, receives
responses). Agent connects its WSS client to the socketpair. Simultaneously, a local
client connects to the agent's WS port and sends a request. Both receive correct
responses.

---

## Phase 3 — CLI parsing

Add `LOCAL_WS_PORT` to wsdbagent's arg enum and parse `--local-ws-port`.

### Tests

| # | Test | What it verifies |
|---|------|-----------------|
| 11 | `Arg_LocalWsPort_DefaultZero` | Without --local-ws-port, value is 0 (disabled) |
| 12 | `Arg_LocalWsPort_ParsesInteger` | --local-ws-port 8085 sets value to 8085 |

---

## Phase 4 — Build integration

Add the test file to `test/CMakeLists.txt`:

```cmake
# SOURCES glob:
"../modules/module/wsdbagent/test/onprem_agent_test.cc"

# Include:
include_directories(../modules/module/wsdbagent/test)
include_directories(../modules/module/wsdbagent/inc)
```

---

## Summary

| Phase | Tests | What |
|-------|-------|------|
| 1. WebSocket session | 7 | Accept, upgrade, request/response, cleanup, ping/pong |
| 2. WsDbAgent wiring | 3 | Port flag, listener lifecycle, concurrency |
| 3. CLI parsing | 2 | Default and parse |
| 4. Build | — | ctest discovers all tests |
| **Total** | **12** | |

---

## Implementation order

1. Write Phase 1 tests → FAIL (no `LocalWsListener` class)
2. Implement `LocalWsListener` → Phase 1 PASS
3. Write Phase 2 tests → FAIL
4. Integrate `LocalWsListener` into `WsDbAgent` → Phase 2 PASS
5. Write Phase 3 tests → FAIL
6. Add CLI flag to `wsdbagent_main.cpp` → Phase 3 PASS
7. Wire into `test/CMakeLists.txt` → all 12 tests green in `ctest`
