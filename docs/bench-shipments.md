# Shipment-creation Benchmark

`scripts/bench-shipments.py` measures how long the backend takes to create
shipments. For each batch size it does two runs and prints a side-by-side
comparison:

| Mode    | What it does                                                              |
|---------|---------------------------------------------------------------------------|
| SINGLE  | `POST /api/v1/shipment/shipping` repeated N times sequentially            |
| BULK    | `POST /api/v1/shipment/bulk/shipping` once, with an array of N shipments  |

Default batch sweep: **10, 50, 100, 500, 1000**.
Default target: **`https://marvel.herokuapp.com`** (the Heroku deploy).

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
| `--host`       | `https://marvel.herokuapp.com` | Base URL                                               |
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
