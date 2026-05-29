#!/usr/bin/env bash
# tests/smoke/xpmile-pull-smoke.sh — end-to-end Hub-pull smoke for the
# xpmile-pull binary. Pulls docker.io/library/alpine:3.19 (the smallest
# public test image), asserts the bundle has the expected file shape,
# and confirms --force / no-force behaviour.
#
# Invoked by the `runc-pull-smoke` job in .github/workflows/publish-images.yml.
# Locally, run from a container that has the binary built at /src/build:
#
#   podman run --rm --network=host xpmile-test:latest \
#       /src/tests/smoke/xpmile-pull-smoke.sh
#
# Exits 0 on success, non-zero on any assertion failure. No external
# tooling required besides `jq` — bash + coreutils otherwise.

set -euo pipefail

PULL_BIN="${PULL_BIN:-/src/build/xpmile-pull}"
BUNDLE="${BUNDLE:-/tmp/xpmile-pull-smoke}"

if [[ ! -x "$PULL_BIN" ]]; then
  echo "smoke: binary not found at $PULL_BIN" >&2
  exit 1
fi

# ── Clean slate ─────────────────────────────────────────────────────────────
rm -rf "$BUNDLE"

# ── 1) Plain pull of alpine:3.19 ───────────────────────────────────────────
echo "smoke: pulling docker.io/library/alpine:3.19 → $BUNDLE"
"$PULL_BIN" docker.io/library/alpine:3.19 --to "$BUNDLE"

# ── 2) Bundle layout ───────────────────────────────────────────────────────
if [[ ! -d "$BUNDLE/rootfs" ]]; then
  echo "smoke: expected $BUNDLE/rootfs/ — not found" >&2
  exit 2
fi
if [[ ! -f "$BUNDLE/config.json" ]]; then
  echo "smoke: expected $BUNDLE/config.json — not found" >&2
  exit 2
fi

# ── 3) config.json is valid JSON + has ociVersion ──────────────────────────
if ! command -v jq >/dev/null 2>&1; then
  # No jq — fall back to python or python3.
  if command -v python3 >/dev/null 2>&1; then
    py=python3
  elif command -v python >/dev/null 2>&1; then
    py=python
  else
    echo "smoke: need jq or python to validate config.json" >&2
    exit 3
  fi
  "$py" -c 'import json,sys; cfg=json.load(open("'"$BUNDLE"'/config.json")); assert cfg.get("ociVersion"), "missing ociVersion"; print("ok")'
else
  if ! jq -e '.ociVersion' "$BUNDLE/config.json" >/dev/null; then
    echo "smoke: config.json missing or invalid ociVersion" >&2
    exit 3
  fi
fi

# ── 4) Alpine sentinel file: /etc/alpine-release contains "3.19" ───────────
if [[ ! -f "$BUNDLE/rootfs/etc/alpine-release" ]]; then
  echo "smoke: rootfs/etc/alpine-release missing — bundle is incomplete" >&2
  exit 4
fi
if ! grep -q '3\.19' "$BUNDLE/rootfs/etc/alpine-release"; then
  echo "smoke: rootfs/etc/alpine-release does not contain '3.19':" >&2
  cat "$BUNDLE/rootfs/etc/alpine-release" >&2
  exit 4
fi

# ── 5) Re-pulling without --force exits 5 (BUNDLE_WRITE_FAILED) ────────────
set +e
"$PULL_BIN" docker.io/library/alpine:3.19 --to "$BUNDLE" >/dev/null 2>&1
rc=$?
set -e
if [[ $rc -eq 0 ]]; then
  echo "smoke: re-pull without --force unexpectedly succeeded (should fail)" >&2
  exit 5
fi

# ── 6) Re-pull with --force succeeds and replaces atomically ───────────────
"$PULL_BIN" docker.io/library/alpine:3.19 --to "$BUNDLE" --force
if ! grep -q '3\.19' "$BUNDLE/rootfs/etc/alpine-release"; then
  echo "smoke: alpine-release missing after --force re-pull" >&2
  exit 6
fi

echo "smoke: all assertions OK"
rm -rf "$BUNDLE"
