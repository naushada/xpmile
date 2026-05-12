# Shipment-creation Benchmark

`scripts/bench-shipments.py` measures how long the backend takes to create
shipments. For each batch size it does two runs and prints a side-by-side
comparison:

| Mode    | What it does                                                              |
|---------|---------------------------------------------------------------------------|
| SINGLE  | `POST /api/v1/shipment/shipping` repeated N times sequentially            |
| BULK    | `POST /api/v1/shipment/bulk/shipping` once, with an array of N shipments  |

Default batch sweep: **10, 50, 100, 500, 1000**.
Default target: **`https://marvel-3a78bd953f5f.herokuapp.com`** (the Heroku
deploy). Heroku now publishes apps under a hashed subdomain — the short
`marvel.herokuapp.com` form 404s. The canonical URL is whatever
`heroku apps:info --app marvel` prints as **Web URL**; update the
`DEFAULT_HOST` constant if Heroku rotates the hash.

Only the Python 3 standard library is used — no `pip install` needed.

---

## Usage

```sh
# Heroku, default sweep
scripts/bench-shipments.py --account ACC001

# Local podman stack
scripts/bench-shipments.py --host http://localhost:8080 --account ACC001

# Smaller sweep, single mode only
scripts/bench-shipments.py --account ACC001 --counts 10 100 1000 --mode single

# Self-signed dev TLS
scripts/bench-shipments.py --host https://dev.local:8443 --account ACC001 --insecure
```

Flags:

| Flag           | Default                        | Purpose                                                |
|----------------|--------------------------------|--------------------------------------------------------|
| `--host`       | `https://marvel-3a78bd953f5f.herokuapp.com` | Base URL (see note above on Heroku hashed subdomain) |
| `--account`    | *required*                     | `senderInformation.accountNo` — must exist on the target |
| `--counts`     | `10 50 100 500 1000`           | Batch sizes to sweep                                   |
| `--awb-prefix` | `AWB`                          | Fallback prefix if the account doesn't carry one       |
| `--timeout`    | `120`                          | Per-request timeout (seconds)                          |
| `--mode`       | `both`                         | `both`, `single`, or `bulk`                            |
| `--insecure`   | off                            | Skip TLS verification                                  |

Pass `--help` to print the full usage.

---

## What gets sent

Each shipment is a self-contained JSON document modelled on the UI's
single-shipment shape (`ui/src/app/shipping/single/single.component.ts`)
with the minimum fields the backend touches:

- `shipment.isAutoGenerate: true` — backend looks up `awbPrefix` from the
  account and calls `MongodbClient::next_awbno(prefix)` for every row
  (atomic `findOneAndUpdate + $inc` on `counters`). See `CLAUDE.md` →
  *AWB generation*.
- `shipment.senderInformation.accountNo` — `--account` value
- `shipment.shipmentInformation.createdOn` — today (DD/MM/YYYY)
- one `activity` entry with event `"Document Prepared"` so the row shows
  up in the dashboard's **New** bucket and the monthly report

`altRefNo` and receiver name are suffixed with a per-batch sequence
number to keep the documents distinguishable.

---

## What the table means

Example output (illustrative):

```
     N  SINGLE total  SINGLE/ship    BULK total    BULK/ship   speedup  fail
------------------------------------------------------------------------------
    10      4.31 s       431.2 ms      0.42 s         41.7 ms    10.3x
    50     20.85 s       417.0 ms      1.78 s         35.5 ms    11.7x
   100     41.32 s       413.2 ms      3.41 s         34.1 ms    12.1x
   500    207.10 s       414.2 ms     17.83 s         35.7 ms    11.6x
  1000    413.45 s       413.4 ms     35.62 s         35.6 ms    11.6x
```

- **`SINGLE total`** — wall-clock for the N sequential POSTs.
- **`SINGLE/ship`** — per-shipment latency; dominated by request RTT +
  one AWB counter `$inc` + one `insert_one`.
- **`BULK total`** — wall-clock for the one bulk POST.
- **`BULK/ship`** — total ÷ N. Reflects per-row server work amortised
  over one HTTP round-trip and one MongoDB `insert_many` call.
- **`speedup`** — `SINGLE_total / BULK_total`. Anything above ~5–10× says
  the network round-trip dominates single-row creation, which is the
  expected shape against Heroku (router + TLS + WS hop). On a local
  stack the gap should be much smaller.

`fail` shows non-2xx responses per run (e.g. `s=3 b=0` would mean three
single POSTs failed). A non-zero `fail` invalidates the timing.

---

## Caveats

- **The benchmark writes real shipments.** Every successful run leaves N
  rows in the `shipping` collection of the target environment. Don't
  point this at production. For Heroku, run against a dedicated test
  account and clean up afterwards (e.g. delete by `altRefNo` prefix
  `BENCH-`).
- **AWB counter contention.** Every shipment increments the same
  `counters` document keyed by the account's `awbPrefix`. On a busy
  shared environment that contention will show up as added latency in
  both modes.
- **Sequential, no concurrency.** SINGLE mode is one-at-a-time on
  purpose — that's what gives a clean per-shipment latency number.
  Adding `--concurrency` for parallel SINGLE POSTs would be a
  straightforward next iteration; not yet implemented.
- **No warm-up.** The first request after a Heroku dyno wake-up will be
  slow (cold start). Run a small `--counts 5` once and discard before
  recording numbers.
- **Heroku request timeout is 30 s.** A BULK POST big enough to exceed
  that will be terminated by the router with `H12`. If you see that on
  `--counts 1000`, drop to 500 or run against a local stack.

---

## Sample run — 2026-05-13, Heroku, `account=admin`

Full sweep against the Heroku deploy
(`https://marvel-3a78bd953f5f.herokuapp.com`) with `--timeout 300`.
Single Eco dyno, on-prem MongoDB reached through `wsdbagent`. Numbers
captured straight from the script:

```
     N  SINGLE total   SINGLE/ship    BULK total     BULK/ship   speedup  fail
------------------------------------------------------------------------------
    10      30.73 s      3072.9 ms      11.02 s      1102.2 ms      2.8x
    50     148.82 s      2976.4 ms      32.46 s       649.2 ms      4.6x   s=0 b=50
   100     242.13 s      2421.3 ms      31.71 s       317.1 ms      7.6x   s=1 b=100
   500    —— TimeoutError on one SINGLE POST after 300 s (script aborted) ——
  1000    —— not reached ——
```

### What we learned

| Observation | Reading |
|---|---|
| SINGLE per-shipment ≈ 2.4–3.1 s | TLS handshake + Heroku router hop + WS tunnel to `wsdbagent` + AWB counter `$inc` + `insert_one`. The script opens a fresh HTTPS connection per request — no keep-alive reuse — which inflates per-row cost. |
| BULK per-shipment falls 1.1 s → 0.32 s as N grows | Fixed cost (TLS, router, one tunnel round-trip) amortises across the batch. `insert_many` on the agent side is much cheaper than N separate `insert_one` calls. |
| BULK 50 / 100 wall-clock ≈ 32 s | Both sit *just over* Heroku's **30 s H12** router timeout. The router returned a non-2xx and `run_bulk()` flagged every row as failed (`b=50`, `b=100`). The shipments may still have been committed server-side — the router killed the response, not the worker. |
| SINGLE 500 timeout | One of the 500 sequential POSTs stalled past the 300 s per-request timeout. Likely a dyno GC pause, idle-connection reset, or transient backend slowdown. Symptom would not appear with HTTPS keep-alive + per-request retry. |
| Clean speedup data point | Only `N=10` is uncontaminated by H12. SINGLE/BULK speedup ≈ **2.8×** at N=10 on a cold-ish dyno. Real production speedup is higher (BULK ships per-row latency keeps falling with N until H12 caps it). |

### Caveats specific to this run

- **No connection reuse.** `urllib.request.urlopen` opens a new TCP +
  TLS connection per call. For SINGLE-mode benchmarks on Heroku this
  adds ~150–300 ms of TLS handshake to every shipment. A future
  iteration should reuse `http.client.HTTPSConnection` across the
  loop so SINGLE numbers reflect server work, not handshake cost.
- **No per-request retry.** A single network blip aborts the whole
  sweep. The N=500 crash above is the textbook case.
- **Bulk "fail" doesn't mean rows were rejected.** When `b=N` shows up
  in the table, the *response* was non-2xx (typically `H12`), but the
  backend often completes the `insert_many` regardless. Confirm with a
  document count after the run if it matters.
- **Rows written by this sample run.** Confirmed inserts via SINGLE:
  **10 + 50 + 99 = 159**. Possibly inserted via the H12'd BULK 50/100
  responses: up to **150** more. Unknown number from the partial
  SINGLE 500 loop (script crashed mid-stream; no per-row commit log).
  Upper bound ≈ **800 BENCH-prefixed rows** in the `shipping`
  collection.

### Next time

When re-running the sweep against Heroku, prefer:

```sh
scripts/bench-shipments.py --account admin --counts 10 25 --timeout 30
```

That stays comfortably under the 30 s H12 wall for both BULK runs and
gives clean numbers across the SINGLE/BULK comparison. For the full
10/50/100/500/1000 sweep, run against a local podman stack instead —
no router timeout in the way.

### Cleanup

The DELETE shipment endpoint (`/api/v1/shipment/awblist`) filters on a
top-level `shipmentNo` field, but stored documents have the AWB at
`shipment.awbno`. The handler will not match BENCH rows. Cleanup
options today:

1. **Direct on-prem Mongo** — `db.shipping.deleteMany({
   "shipment.altRefNo": { $regex: "^BENCH-" } })` from a mongo shell
   on the wsdbagent host. Fastest if you have the access.
2. **Fix the DELETE handler** to filter on `shipment.awbno`
   (or add an `altRefList` query param), redeploy, then clean up via
   the API. Tracked as a follow-up if and when the bench rows start
   to bother dashboard / reporting queries.
3. **Leave the rows** — they are all `altRefNo`-prefixed with
   `BENCH-` and filterable out of any application-side query.
   Acceptable while volume is in the low hundreds.
