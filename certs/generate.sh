#!/usr/bin/env bash
# Generate CA, server, and client certificates for wsdbagent ↔ uniservice mTLS.
# Run once; commit the .crt files but keep .key files out of version control.

set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

# ── CA ──────────────────────────────────────────────────────────────────────────
openssl genrsa -out ca.key 4096
openssl req -new -x509 -days 3650 -key ca.key -out ca.crt \
  -subj "/O=xpmile/CN=xpmile-CA"

# ── Server (uniservice) ─────────────────────────────────────────────────────────
openssl genrsa -out server.key 4096
openssl req -new -key server.key -out server.csr \
  -subj "/O=xpmile/CN=uniservice"
openssl x509 -req -days 3650 -in server.csr \
  -CA ca.crt -CAkey ca.key -CAcreateserial \
  -out server.crt
rm -f server.csr

# ── Client (wsdbagent) ──────────────────────────────────────────────────────────
openssl genrsa -out client.key 4096
openssl req -new -key client.key -out client.csr \
  -subj "/O=xpmile/CN=wsdbagent"
openssl x509 -req -days 3650 -in client.csr \
  -CA ca.crt -CAkey ca.key -CAcreateserial \
  -out client.crt
rm -f client.csr ca.srl

echo "Generated:"
echo "  certs/ca.crt       — CA certificate (distribute to both sides)"
echo "  certs/server.crt   — uniservice certificate"
echo "  certs/server.key   — uniservice private key   (keep secret)"
echo "  certs/client.crt   — wsdbagent certificate"
echo "  certs/client.key   — wsdbagent private key    (keep secret)"
