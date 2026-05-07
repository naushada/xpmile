# mongodbc — MongoDB Client Library

`MongodbClient` is a thin, thread-safe wrapper around `mongocxx::pool`.
Every public method acquires a connection from the pool, performs its
operation, and releases the connection on return.  Multiple
`MicroService` worker threads share a single `MongodbClient` instance
safely without any additional locking.

---

## Construction

```cpp
// No auth (development only)
MongodbClient db("mongodb://localhost:27017/xpmile?maxPoolSize=20");

// With authentication (production)
MongodbClient db("mongodb://xpmile:xpmile_pass@localhost:27017/xpmile"
                 "?authSource=admin&maxPoolSize=20");
```

The URI must include the database name.  Pool size is controlled via the
`maxPoolSize` query parameter in the URI.  The constructor calls
`mongocxx::instance` (one per process) and initialises the pool; the
destructor is a no-op (`= default`) because `unique_ptr` cleans up the
pool and instance automatically.

---

## Collection Schemas

### `shipping` — shipment records

Each document represents one Air Waybill.

```json
{
  "_id":      ObjectId("..."),
  "shipment": {
    "awbno":      "AWB000000042",
    "shipmentNo": "AWB000000042",
    "sender":   { "name": "...", "address": "...", "phone": "..." },
    "receiver": { "name": "...", "address": "...", "phone": "..." },
    "weight":   2.5,
    "product_type": "104",
    "payment_mode": "COD",
    "status":   "created"
  }
}
```

Key query paths used by the API:

| Field path         | Purpose                              |
|--------------------|--------------------------------------|
| `shipment.awbno`   | Exact or `$in` match for GET/PUT     |
| `shipment.status`  | Status-based filtering               |
| `shipmentNo`       | Top-level alias used in DELETE lists |

---

### `counters` — atomic sequence counters

One document per entity type.  **Do not write to this collection
directly** — use `next_awbno()` which performs an atomic
`find_one_and_update`.

```json
{ "_id": "awbno", "seq": 42 }
```

| Field  | Type    | Description                         |
|--------|---------|-------------------------------------|
| `_id`  | string  | Counter name (`"awbno"`, etc.)      |
| `seq`  | int64   | Last issued sequence number         |

---

### `fs.files` — GridFS file metadata

Created automatically by `store_file()`.  One document per uploaded file.

```json
{
  "_id":        ObjectId("6650a1b2c3d4e5f600000001"),
  "filename":   "invoice_AWB000000042.pdf",
  "length":     204800,
  "chunkSize":  261120,
  "uploadDate": ISODate("2026-05-01T10:30:00Z"),
  "metadata": {
    "contentType": "application/pdf"
  }
}
```

| Field              | Description                                    |
|--------------------|------------------------------------------------|
| `_id`              | OID string returned by `store_file()`          |
| `filename`         | The `filename` argument passed to `store_file` |
| `length`           | Total file size in bytes                       |
| `chunkSize`        | Bytes per chunk (default 261,120 ≈ 255 KB)     |
| `metadata.contentType` | MIME type stored for retrieval             |

---

### `fs.chunks` — GridFS binary data

One document per 255 KB chunk.  Managed entirely by GridFS; never write
to this collection directly.

```json
{
  "_id":      ObjectId("..."),
  "files_id": ObjectId("6650a1b2c3d4e5f600000001"),
  "n":        0,
  "data":     BinData(0, "<binary>")
}
```

| Field      | Description                              |
|------------|------------------------------------------|
| `files_id` | Foreign key → `fs.files._id`            |
| `n`        | Zero-based chunk index within the file  |
| `data`     | Up to `chunkSize` bytes of file content |

---

## AWB Number Generation

### Why a counter collection?

MongoDB has no built-in auto-increment equivalent to SQL `SERIAL`.
Common alternatives and their trade-offs:

| Approach               | Collision-safe | Sequential | Notes                          |
|------------------------|:--------------:|:----------:|--------------------------------|
| `ObjectId` (_id)       | ✓              | ✗          | Not human-readable             |
| UUID v4                | ✓              | ✗          | Not human-readable             |
| Client timestamp+rand  | ✗              | ✗          | Races under concurrent inserts |
| **Counter collection** | **✓**          | **✓**      | MongoDB-recommended pattern    |

### How `next_awbno()` works

```
counters collection
┌─────────────────────────┐        find_one_and_update
│  { _id: "awbno",        │ ◄────  filter:  { _id: "awbno" }
│    seq:  41  }          │        update:  { $inc: { seq: 1 } }
│                         │        upsert:  true
│  ─── atomic write ───►  │        return:  k_after
│  { _id: "awbno",        │
│    seq:  42  }          │ ────►  returns "AWB000000042"
└─────────────────────────┘
```

`find_one_and_update` is a **single atomic operation** at the MongoDB
document level.  Concurrent workers calling `next_awbno()` simultaneously
will each receive a distinct sequence number — no application-level
mutex is required.

### Format

```
AWB  000000042
───  ─────────
 ↑       ↑
prefix  9-digit zero-padded decimal (seq 1 … 999,999,999)
```

The prefix can be overridden per-carrier:

```cpp
std::string awb = db.next_awbno("DHL");   // "DHL000000001"
std::string awb = db.next_awbno("FDX");   // "FDX000000001"
std::string awb = db.next_awbno();        // "AWB000000001"  (default)
```

To support per-carrier independent sequences, store one counter document
per prefix (`{ _id: "DHL" }`, `{ _id: "FDX" }`, etc.).  The
`next_awbno` implementation already uses `prefix` as the `_id` — so
passing different prefixes automatically isolates their counters.

### Using `next_awbno` in a handler

```cpp
// Single shipment creation
std::string awb = dbInst.next_awbno();          // e.g. "AWB000000042"
// Embed awb into the JSON body before inserting:
json body = json::parse(content);
body["shipment"]["awbno"]     = awb;
body["shipment"]["shipmentNo"] = awb;
std::string oid = dbInst.create_document(
    dbInst.get_database(), "shipping", body.dump());

// Bulk creation — generate one AWB per document
json body = json::parse(content);
for (auto &[key, doc] : body.items()) {
    std::string awb = dbInst.next_awbno();
    doc["shipment"]["awbno"]      = awb;
    doc["shipment"]["shipmentNo"] = awb;
}
int32_t cnt = dbInst.create_bulk_document(
    dbInst.get_database(), "shipping", body.dump());
```

---

## GridFS File Storage

```cpp
// Upload (PDF, PNG, XLSX, …)
std::vector<uint8_t> pdf_bytes = /* read from multipart body */;
std::string oid = dbInst.store_file(
    "invoice_AWB000000042.pdf", "application/pdf", pdf_bytes);

// Download by logical name
auto bytes = dbInst.fetch_file("invoice_AWB000000042.pdf");

// Download by OID (faster; use the OID stored in the shipment document)
auto bytes = dbInst.fetch_file_by_id(oid);

// Delete
dbInst.delete_file(oid);
```

Store the `oid` returned by `store_file` in the shipment document so
files can be retrieved by OID without a GridFS filename scan:

```json
{
  "shipment": {
    "awbno": "AWB000000042",
    "attachments": [
      { "name": "invoice.pdf",  "oid": "6650a1b2c3d4e5f600000001" },
      { "name": "label.png",    "oid": "6650a1b2c3d4e5f600000002" }
    ]
  }
}
```

---

## `JsonExtract` Variant

`from_json(json_obj, key)` returns a `JsonExtract` variant that
automatically picks the right C++ type from the BSON element type:

```cpp
using JsonExtract = std::variant<
    std::monostate,   // key absent or type unsupported
    std::string,      // k_utf8  scalar
    JsonStrVec,       // k_array of UTF-8 strings
    JsonDocList       // k_array of {file-name, file-content} sub-documents
>;
```

Typical usage with `std::get_if` — `from_json()` returns by value, so
the result must be bound to a named variable before taking its address.
Use C++17 if-init statements:

```cpp
// Scalar string
if (auto v = dbInst.from_json(body, "awbno"); auto *s = std::get_if<std::string>(&v))
    awbno = *s;

// Array of strings (e.g. recipient list)
if (auto v = dbInst.from_json(body, "to"); auto *p = std::get_if<JsonStrVec>(&v))
    recipients = std::move(*p);

// Array of file attachments
if (auto v = dbInst.from_json(body, "files"); auto *p = std::get_if<JsonDocList>(&v))
    attachments = std::move(*p);
```
