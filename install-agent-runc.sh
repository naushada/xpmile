#!/usr/bin/env bash
# install-agent-runc.sh — operator one-curl for the runc install path.
#
# Detects host arch, downloads the matching xpmile-runc-bundle release
# tarball from GitHub, untars it under ~/xpmile-runc, and runs the bundle's
# install.sh. Sibling to install-agent.sh (the docker/podman compose path).
#
# Usage:
#   # Recommended — single curl into bash:
#   SERVER_HOST=marvel-XXXX.herokuapp.com \
#     curl -sSf https://raw.githubusercontent.com/naushada/xpmile/main/install-agent-runc.sh | bash
#
#   # Pin a specific release:
#   XPMILE_RELEASE=v1.3.0 SERVER_HOST=… \
#     curl -sSf https://raw.githubusercontent.com/naushada/xpmile/v1.3.0/install-agent-runc.sh | bash

set -euo pipefail

XPMILE_RELEASE="${XPMILE_RELEASE:-latest}"
INSTALL_ROOT="${INSTALL_ROOT:-$HOME/xpmile-runc}"
REPO="${REPO:-naushada/xpmile}"

# ── Detect architecture ────────────────────────────────────────────────────
case "$(uname -m)" in
  x86_64|amd64)        ARCH=amd64 ;;
  aarch64|arm64)       ARCH=arm64 ;;
  *)
    echo "install-agent-runc: unsupported uname -m: $(uname -m)" >&2
    exit 1
    ;;
esac

# ── Required env ───────────────────────────────────────────────────────────
if [[ -z "${SERVER_HOST:-}" ]]; then
  echo "install-agent-runc: SERVER_HOST is required (e.g. marvel-XXXXXXXX.herokuapp.com)" >&2
  exit 1
fi

# ── Preflight deps ─────────────────────────────────────────────────────────
need() { command -v "$1" >/dev/null 2>&1 || { echo "install-agent-runc: missing dep: $1" >&2; exit 1; }; }
need curl
need tar
need runc

# ── Resolve release tag ────────────────────────────────────────────────────
if [[ "$XPMILE_RELEASE" == "latest" ]]; then
  TAG="$(curl -sSf "https://api.github.com/repos/$REPO/releases/latest" | sed -n 's/.*"tag_name": *"\([^"]*\)".*/\1/p' | head -1)"
  if [[ -z "$TAG" ]]; then
    echo "install-agent-runc: could not resolve latest release tag from GitHub" >&2
    exit 2
  fi
else
  TAG="$XPMILE_RELEASE"
fi

URL="https://github.com/$REPO/releases/download/$TAG/xpmile-runc-bundle-$TAG-$ARCH.tar.gz"
echo ">>> Downloading $URL"

# ── Download + extract ─────────────────────────────────────────────────────
mkdir -p "$INSTALL_ROOT"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
curl -sSfL "$URL" -o "$TMP/bundle.tar.gz"
tar -xzf "$TMP/bundle.tar.gz" -C "$INSTALL_ROOT" --strip-components=1

# ── Run the bundle's install.sh ────────────────────────────────────────────
cd "$INSTALL_ROOT"
bash install.sh
