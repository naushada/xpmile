#!/usr/bin/env python3
"""
bench-shipments.py — time shipment creation against an xpmile backend.

For each requested batch size N (default: 10, 50, 100, 500, 1000) this
script does two runs:

  SINGLE  — POST /api/v1/shipment/shipping N times, sequentially
  BULK    — POST /api/v1/shipment/bulk/shipping once with an array of N items

and prints a comparison table (wall-clock total + per-shipment latency).

Examples:

  # Default — hit the Heroku app, use account ACC001
  scripts/bench-shipments.py --account ACC001

  # Local podman stack
  scripts/bench-shipments.py --host http://localhost:8080 --account ACC001

  # Smaller sweep
  scripts/bench-shipments.py --account ACC001 --counts 10 100 1000

No third-party dependencies — only the Python 3 standard library.
"""

from __future__ import annotations

import argparse
import json
import ssl
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime
from typing import Any


# Heroku now serves apps under a hashed subdomain. The short
# https://marvel.herokuapp.com form 404s for this app — use the
# hashed URL reported by `heroku apps:info --app marvel`.
DEFAULT_HOST = "https://marvel-3a78bd953f5f.herokuapp.com"
DEFAULT_COUNTS = [10, 50, 100, 500, 1000]
SINGLE_PATH = "/api/v1/shipment/shipping"
BULK_PATH = "/api/v1/shipment/bulk/shipping"


def make_shipment(account: str, awb_prefix: str, idx: int) -> dict[str, Any]:
    """Build one shipment payload. `idx` is a per-batch sequence number so
    `referenceNo` / receiver name vary across rows."""
    now = datetime.now()
    date_ddmmyyyy = now.strftime("%d/%m/%Y")
    time_hhmm = now.strftime("%H:%M")
    return {
        "shipment": {
            "isAutoGenerate": True,
            "awbno": "",
            "altRefNo": f"BENCH-{idx:06d}",
            "senderInformation": {
                "accountNo": account,
                "referenceNo": f"REF-{idx:06d}",
                "name": "Bench Sender",
                "country": "AE",
                "city": "Dubai",
                "state": "Dubai",
                "postalCode": "00000",
                "contact": "Bench",
                "address": "Bench Sender Address",
                "phoneNumber": "+971500000000",
                "email": "bench@example.invalid",
                "awbPrefix": awb_prefix,
            },
            "shipmentInformation": {
                "activity": [{
                    "date": date_ddmmyyyy,
                    "event": "Document Prepared",
                    "time": time_hhmm,
                    "notes": "Created by benchmark",
                    "driver": "",
                    "updatedBy": "bench",
                    "eventLocation": "Bench",
                }],
                "skuNo": "BENCH-SKU",
                "service": "Standard",
                "numberOfItems": "1",
                "goodsDescription": "Benchmark parcel",
                "goodsValue": "10",
                "customsValue": "10",
                "codAmount": "0",
                "vat": "0",
                "currency": "AED",
                "weight": "1",
                "weightUnits": "kg",
                "cubicWeight": "1",
                "createdOn": date_ddmmyyyy,
                "createdBy": "bench",
                "hsCode": "0000",
            },
            "receiverInformation": {
                "name": f"Bench Receiver {idx:06d}",
                "country": "AE",
                "city": "Dubai",
                "state": "Dubai",
                "postalCode": "00000",
                "contact": "Bench",
                "address": "Bench Receiver Address",
                "phone": "+971500000001",
                "email": "bench-rx@example.invalid",
            },
        }
    }


def post_json(url: str, payload: Any, timeout: float, ctx: ssl.SSLContext) -> tuple[int, bytes]:
    body = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(
        url,
        data=body,
        method="POST",
        headers={
            "Content-Type": "application/json",
            "Accept": "application/json",
            "User-Agent": "xpmile-bench/1.0",
        },
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout, context=ctx) as resp:
            return resp.status, resp.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read() if hasattr(e, "read") else b""


def run_single(host: str, account: str, awb_prefix: str, n: int,
               timeout: float, ctx: ssl.SSLContext) -> tuple[float, int]:
    url = host.rstrip("/") + SINGLE_PATH
    failed = 0
    start = time.perf_counter()
    for i in range(n):
        status, _ = post_json(url, make_shipment(account, awb_prefix, i), timeout, ctx)
        if status >= 400:
            failed += 1
    return time.perf_counter() - start, failed


def run_bulk(host: str, account: str, awb_prefix: str, n: int,
             timeout: float, ctx: ssl.SSLContext) -> tuple[float, int]:
    url = host.rstrip("/") + BULK_PATH
    payload = [make_shipment(account, awb_prefix, i) for i in range(n)]
    start = time.perf_counter()
    status, _ = post_json(url, payload, timeout, ctx)
    return time.perf_counter() - start, 0 if status < 400 else n


def fmt_ms(seconds: float) -> str:
    return f"{seconds * 1000:8.1f} ms"


def fmt_total(seconds: float) -> str:
    if seconds >= 1.0:
        return f"{seconds:7.2f} s "
    return f"{seconds * 1000:7.1f} ms"


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--host", default=DEFAULT_HOST,
                   help=f"Base URL of the xpmile backend (default: {DEFAULT_HOST})")
    p.add_argument("--account", required=True,
                   help="senderInformation.accountNo to use — must exist on the target")
    p.add_argument("--awb-prefix", default="AWB",
                   help="Fallback AWB prefix if the account has none (default: AWB)")
    p.add_argument("--counts", type=int, nargs="+", default=DEFAULT_COUNTS,
                   metavar="N",
                   help=f"Batch sizes to sweep (default: {' '.join(map(str, DEFAULT_COUNTS))})")
    p.add_argument("--timeout", type=float, default=120.0,
                   help="Per-request timeout in seconds (default: 120)")
    p.add_argument("--insecure", action="store_true",
                   help="Skip TLS verification (useful for self-signed dev certs)")
    p.add_argument("--mode", choices=("both", "single", "bulk"), default="both",
                   help="Which run mode(s) to execute (default: both)")
    args = p.parse_args()

    ctx = ssl.create_default_context()
    if args.insecure:
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE

    # Force line-buffered stdout so progress is visible when piped to
    # `tee` / a log file (Python defaults to full-buffered when stdout
    # is not a TTY, which hides per-row output until the run finishes).
    sys.stdout.reconfigure(line_buffering=True)  # type: ignore[union-attr]

    print(f"Host:    {args.host}", flush=True)
    print(f"Account: {args.account}", flush=True)
    print(f"Sizes:   {args.counts}", flush=True)
    print(f"Modes:   {args.mode}", flush=True)
    print(flush=True)

    header = (
        f"{'N':>6}  {'SINGLE total':>12}  {'SINGLE/ship':>12}  "
        f"{'BULK total':>12}  {'BULK/ship':>12}  {'speedup':>8}  fail"
    )
    print(header, flush=True)
    print("-" * len(header), flush=True)

    for n in args.counts:
        s_total = s_per = float("nan")
        s_fail = b_fail = 0
        b_total = b_per = float("nan")

        if args.mode in ("both", "single"):
            s_total, s_fail = run_single(args.host, args.account, args.awb_prefix,
                                         n, args.timeout, ctx)
            s_per = s_total / n if n else float("nan")

        if args.mode in ("both", "bulk"):
            b_total, b_fail = run_bulk(args.host, args.account, args.awb_prefix,
                                       n, args.timeout, ctx)
            b_per = b_total / n if n else float("nan")

        speedup = "—"
        if args.mode == "both" and b_total > 0:
            speedup = f"{s_total / b_total:6.1f}x"

        fail_note = ""
        if s_fail or b_fail:
            fail_note = f"  s={s_fail} b={b_fail}"

        print(
            f"{n:>6}  "
            f"{fmt_total(s_total):>12}  {fmt_ms(s_per):>12}  "
            f"{fmt_total(b_total):>12}  {fmt_ms(b_per):>12}  "
            f"{speedup:>8}  {fail_note}",
            flush=True,
        )

    return 0


if __name__ == "__main__":
    sys.exit(main())
