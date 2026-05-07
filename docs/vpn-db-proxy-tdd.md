# TDD Plan: WebSocket DB Proxy

Tests are grouped by layer, bottom-up. Each group can be implemented and green-lit
before moving to the next. All tests go into `modules/module/wsdbproxy/test/` and
are wired into the existing `offtarget` test binary via `test/CMakeLists.txt`.

---

## Layer 1 — `dbproto`: BSON request/response encoding

**File:** `wsdbproxy/test/dbproto_test.cc`

These are pure-function tests. No sockets, no threads.

### Request builder

```
TEST(DbProto, BuildRequest_CreateDocument_HasRequiredFields)
  build_request(DbOp::CREATE_DOCUMENT, "xpmile", "shipping", bson_doc, "", "")
  → BSON contains: reqid(int32), op==0, db=="xpmile", coll=="shipping", doc==bson_doc
  → no doc2 field, no sval field

TEST(DbProto, BuildRequest_GetDocument_HasDocAndDoc2)
  build_request(DbOp::GET_DOCUMENT, "", "shipping", query_bson, proj_bson, "")
  → contains doc and doc2 fields

TEST(DbProto, BuildRequest_NextAwbno_HasSval)
  build_request(DbOp::NEXT_AWBNO, "", "", "", "", "AWB")
  → contains sval=="AWB", no doc, no doc2

TEST(DbProto, BuildRequest_StoreFile_HasSvalAndData)
  build_request(DbOp::STORE_FILE, "", "", file_bytes, "", "report.pdf|application/pdf")
  → contains sval=="report.pdf|application/pdf", doc==file_bytes

TEST(DbProto, BuildRequest_ReqIdIncrements)
  two consecutive build_request() calls
  → second reqid == first reqid + 1
```

### Response parser

```
TEST(DbProto, ParseResponse_OkWithSval)
  BSON: {reqid:1, ok:true, sval:"507f1f77bcf86cd799439011"}
  → parse_response() → ok==true, sval=="507f1f77..."

TEST(DbProto, ParseResponse_OkWithIval)
  BSON: {reqid:2, ok:true, ival:5}
  → ok==true, ival==5

TEST(DbProto, ParseResponse_OkWithBval)
  BSON: {reqid:3, ok:true, bval:true}
  → ok==true, bval==true

TEST(DbProto, ParseResponse_OkWithData)
  BSON: {reqid:4, ok:true, data:<binary>}
  → ok==true, data==<binary bytes>

TEST(DbProto, ParseResponse_NotOk_HasErrMsg)
  BSON: {reqid:5, ok:false, errmsg:"collection not found"}
  → ok==false, errmsg=="collection not found"

TEST(DbProto, ParseResponse_EmptyBson_ReturnsNotOk)
  empty input vector
  → parse_response() → ok==false
```

---

## Layer 2 — WebSocket framing

**File:** `wsdbproxy/test/wsframe_test.cc`

Pure byte-manipulation tests. No sockets.

### Frame encoder (`ws_encode`)

```
TEST(WsFrame, Encode_SmallPayload_Len7Bit)
  payload of 10 bytes
  → frame[0] == 0x82 (FIN + binary opcode)
  → frame[1] == 10   (MASK=0, len=10)
  → frame[2..11] == payload bytes

TEST(WsFrame, Encode_126ByteBoundary_Uses16BitLen)
  payload of 126 bytes
  → frame[1] == 126
  → frame[2..3] == uint16 BE 126
  → total header == 4 bytes

TEST(WsFrame, Encode_65536BytePayload_Uses64BitLen)
  payload of 65536 bytes
  → frame[1] == 127
  → frame[2..9] == uint64 BE 65536

TEST(WsFrame, Encode_ClientMasked_MaskBitSet)
  ws_encode(payload, mask=true)
  → frame[1] & 0x80 == 0x80  (MASK bit set)
  → frame contains 4-byte masking key
  → applying masking key to payload bytes recovers original

TEST(WsFrame, Encode_PingFrame_CorrectOpcode)
  ws_encode_control(0x9, {})   // ping
  → frame[0] == 0x89
```

### Frame decoder (`ws_decode`)

```
TEST(WsFrame, Decode_UnmaskedSmallFrame_ExtractsPayload)
  encode a 20-byte payload (unmasked), decode it back
  → decoded payload == original

TEST(WsFrame, Decode_MaskedClientFrame_UnmasksCorrectly)
  encode with mask=true, decode
  → decoded payload == original pre-masking bytes

TEST(WsFrame, Decode_TwoByteLen_DecodesCorrectly)
  200-byte payload → encode → decode
  → payload matches

TEST(WsFrame, Decode_IncompleteHeader_ReturnsFalse)
  only 1 byte in buffer
  → ws_decode() returns false, buffer unchanged

TEST(WsFrame, Decode_IncompletePayload_ReturnsFalse)
  header says 100 bytes payload, only 50 bytes in buffer
  → returns false

TEST(WsFrame, Decode_MultipleFramesInBuffer_ConsumesFirstOnly)
  buffer contains frame-A then frame-B concatenated
  → ws_decode() extracts frame-A payload, leaves frame-B in buffer
```

### Accept key computation

```
TEST(WsFrame, AcceptKey_RFC6455Example)
  // RFC 6455 §1.3 uses this known pair:
  input  = "dGhlIHNhbXBsZSBub25jZQ=="
  expected = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="
  → ws_accept_key(input) == expected
```

---

## Layer 3 — `WsDbServer`: connection and dispatch logic

**File:** `wsdbproxy/test/wsdbserver_test.cc`

These tests use `socketpair()` (POSIX) to create in-process fake "sockets" so no
network is needed. `WsDbServer` is constructed in test mode with a configurable
timeout (1 s for fast tests instead of 30 s).

```
TEST(WsDbServer, DispatchWithNoAgent_ReturnsError)
  WsDbServer server;
  auto rsp = server.dispatch(some_bson);
  → rsp is empty (no agent connected → error response immediately)

TEST(WsDbServer, OnAgentConnected_SetsConnectedFlag)
  int sv[2]; socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
  server.on_agent_connected(sv[0]);
  → server.is_connected() == true

TEST(WsDbServer, Dispatch_SendsFrameToAgent)
  connect fake agent socket pair
  thread: server.dispatch(request_bson)    // blocks
  main:   read raw bytes from sv[1]
  → bytes decode as a valid WebSocket binary frame
  → BSON payload matches request_bson

TEST(WsDbServer, Dispatch_MatchesResponseByReqId)
  connect fake agent socket pair
  thread: server.dispatch(request_bson)    // blocks, captures reqid
  main:   build response BSON with matching reqid, write as WS frame to sv[1]
  → dispatch() returns that response BSON

TEST(WsDbServer, Dispatch_TimesOutAfter1s)
  connect fake agent (doesn't send any response)
  server.dispatch(bson, timeout=1s)
  → returns error response within ~1s

TEST(WsDbServer, Disconnect_WakesAllPendingDispatchers)
  two threads each call server.dispatch() and block
  main: close sv[1]  (simulate agent disconnect)
  → both threads unblock and receive error responses
  → server.is_connected() == false

TEST(WsDbServer, SecondAgentRejected_WhenFirstAlive)
  connect first agent sv1[0]
  server.on_agent_connected(sv1[0])
  try: server.on_agent_connected(sv2[0])
  → returns false (second connection rejected)
  → sv2[1] readable: HTTP/1.1 409 Conflict

TEST(WsDbServer, ConcurrentDispatches_UniqueReqIds)
  N threads each call build_request() without dispatching
  → all reqids are distinct (atomic increment)
```

---

## Layer 4 — `WsMongodbProxy`: IMongodbClient implementation

**File:** `wsdbproxy/test/wsproxy_test.cc`

Uses a `FakeWsDbServer` stub that captures the last dispatched BSON and returns a
pre-configured response, so no real socket is involved.

```
TEST(WsMongodbProxy, CreateDocument_SendsCorrectOp)
  proxy.create_document("xpmile", "shipping", json_string)
  → captured request: op == DbOp::CREATE_DOCUMENT, coll == "shipping"

TEST(WsMongodbProxy, CreateDocument_ReturnsSval)
  fake server responds: {ok:true, sval:"<oid>"}
  → proxy.create_document(...) == "<oid>"

TEST(WsMongodbProxy, CreateDocument_ReturnsEmptyOnError)
  fake server responds: {ok:false, errmsg:"write failed"}
  → proxy.create_document(...) == ""

TEST(WsMongodbProxy, GetDocument_SendsDocAndDoc2)
  proxy.get_document("coll", query_json, proj_json)
  → request contains doc (query as BSON) and doc2 (projection as BSON)

TEST(WsMongodbProxy, GetDocuments_WithQuery_UsesQueriedOp)
  proxy.get_documents("coll", query_json, proj_json)
  → op == DbOp::GET_DOCUMENTS_QUERIED

TEST(WsMongodbProxy, GetDocuments_WithoutQuery_UsesAllOp)
  proxy.get_documents("coll", proj_json)
  → op == DbOp::GET_DOCUMENTS_ALL

TEST(WsMongodbProxy, UpdateCollection_SendsBothDocs)
  proxy.update_collection("coll", filter_json, update_json)
  → op == DbOp::UPDATE_COLLECTION, doc == filter_bson, doc2 == update_bson

TEST(WsMongodbProxy, DeleteDocument_ReturnsBval)
  fake server responds: {ok:true, bval:true}
  → proxy.delete_document("coll", filter) == true

TEST(WsMongodbProxy, NextAwbno_SendsPrefixInSval)
  proxy.next_awbno("AWB")
  → request sval == "AWB"
  → fake response {ok:true, sval:"AWB000000042"} → returns "AWB000000042"

TEST(WsMongodbProxy, StoreFile_ReturnsOid)
  proxy.store_file("file.pdf", "application/pdf", bytes)
  → request sval == "file.pdf|application/pdf"
  → fake response {ok:true, sval:"<oid>"} → returns "<oid>"

TEST(WsMongodbProxy, FetchFileById_ReturnsData)
  fake response: {ok:true, data:<bytes>}
  proxy.fetch_file_by_id("<oid>") == <bytes>

TEST(WsMongodbProxy, GetDatabase_ReturnsConfiguredName)
  WsMongodbProxy proxy(server, "xpmile");
  proxy.get_database() == "xpmile"

TEST(WsMongodbProxy, ImplementsIMongodbClient)
  IMongodbClient *p = new WsMongodbProxy(server, "xpmile");
  → compiles and does not crash on virtual dispatch
```

---

## Layer 5 — WebSocket upgrade detection in `WebConnection`

**File:** `modules/module/webservice/test/webservice_test.cc` (extend existing file)

Uses a `MicroService` default-constructor approach — construct a `WebConnection`-like
test double or test the pure helper function `ws_accept_key` directly. Full
`WebConnection` path requires a fake `WebServer`; use a minimal mock.

```
TEST(WebConnection, WsUpgradeRequest_Detected)
  raw HTTP string:
    "GET /ws/db HTTP/1.1\r\nUpgrade: websocket\r\nSec-WebSocket-Key: dGhl...\r\n\r\n"
  → ws_is_upgrade_request(raw) == true

TEST(WebConnection, NormalGetRequest_NotUpgrade)
  "GET /api/v1/shipment HTTP/1.1\r\n\r\n"
  → ws_is_upgrade_request(raw) == false

TEST(WebConnection, WsUpgradeResponse_Contains101)
  ws_build_upgrade_response("dGhlIHNhbXBsZSBub25jZQ==")
  → contains "HTTP/1.1 101 Switching Protocols"
  → contains "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo="

TEST(WebConnection, WsUpgradeToWrongPath_Ignored)
  "GET /api/v1/account HTTP/1.1\r\nUpgrade: websocket\r\n..."
  → ws_is_upgrade_request recognises Upgrade header but path != /ws/db
  → treated as a 404, not handed to WsDbServer
```

---

## Layer 6 — `MongodbClient` implements `IMongodbClient`

**File:** Extend existing `modules/module/webservice/test/webservice_test.cc`  
or add `modules/module/mongodb/test/mongodbc_iface_test.cc`

```
TEST(MongodbClient, IsAssignableToIMongodbClientPointer)
  // Compile-time check — if this test builds, the inheritance is correct.
  static_assert(std::is_base_of_v<IMongodbClient, MongodbClient>);

TEST(MongodbClient, WorkCtxAcceptsIMongodbClientPtr)
  IMongodbClient *p = nullptr;
  WorkCtx ctx{ACE_INVALID_HANDLE, p, ""};
  → compiles; ctx.db == nullptr
```

---

## Test file wiring

Add to `test/CMakeLists.txt` SOURCES glob:

```cmake
"../modules/module/wsdbproxy/src/dbproto.cpp"
"../modules/module/wsdbproxy/src/wsdbproxy.cpp"
"../modules/module/wsdbproxy/test/dbproto_test.cc"
"../modules/module/wsdbproxy/test/wsframe_test.cc"
"../modules/module/wsdbproxy/test/wsdbserver_test.cc"
"../modules/module/wsdbproxy/test/wsproxy_test.cc"
```

And add the include path:

```cmake
include_directories(../modules/module/wsdbproxy/inc)
include_directories(../modules/module/wsdbproxy/test)
```

---

## Implementation order (Red → Green)

```
1. dbproto.hpp / dbproto.cpp          → Layer 1 green
2. ws_encode / ws_decode / ws_accept_key  → Layer 2 green
3. WsDbServer (skeleton + dispatch)   → Layer 3 green
4. WsMongodbProxy + FakeWsDbServer    → Layer 4 green
5. MongodbClient : IMongodbClient     → Layer 6 green
6. WebConnection upgrade detection    → Layer 5 green
7. Wire everything in webservice_main.cpp
8. End-to-end manual test with real ws-db-agent (client phase)
```
