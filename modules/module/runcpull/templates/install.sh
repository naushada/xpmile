#!/usr/bin/env bash
# install.sh — runs inside the untarred xpmile-runc-bundle-<v>-<arch>/ dir.
#
# Pulls mongo:4.4 + xpmile-wsdbagent:latest (and optionally :idp) via the
# bundled `xpmile-pull` binary; writes per-container `config.json` from
# the templates; installs the systemd units; enables the stack.
#
# Idempotent. Re-run after editing .env or rotating certs.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="$HERE/bin/xpmile-pull"

# ── Configuration (.env / env vars) ─────────────────────────────────────────
INSTALL_DIR="${INSTALL_DIR:-/var/lib/xpmile}"
CERTS_DIR="${CERTS_DIR:-$HOME/xpmile/certs/cloud-issued/innertls}"
TZ="${TZ:-Asia/Calcutta}"

SERVER_HOST="${SERVER_HOST:-}"
SERVER_PORT="${SERVER_PORT:-443}"
IDP_SERVER_HOST="${IDP_SERVER_HOST:-}"
IDP_SERVER_PORT="${IDP_SERVER_PORT:-443}"

MONGO_TAG="${MONGO_TAG:-4.4}"
WSDBAGENT_TAG="${WSDBAGENT_TAG:-latest}"
MONGO_ROOT_USER="${MONGO_ROOT_USER:-root}"
MONGO_ROOT_PASS="${MONGO_ROOT_PASS:-changeme}"
MONGO_APP_USER="${MONGO_APP_USER:-xpmile}"
MONGO_APP_PASS="${MONGO_APP_PASS:-xpmile_pass}"

if [[ -z "$SERVER_HOST" ]]; then
  echo "install.sh: SERVER_HOST is required (e.g. marvel-XXXXXXXX.herokuapp.com)" >&2
  echo "            export it or put it in .env and re-run." >&2
  exit 1
fi

# ── Preflight ──────────────────────────────────────────────────────────────
need() { command -v "$1" >/dev/null 2>&1 || { echo "install.sh: missing dep: $1" >&2; exit 1; }; }
need runc
need sudo
need install
[[ -x "$BIN" ]] || { echo "install.sh: $BIN missing" >&2; exit 1; }

# ── Layout ──────────────────────────────────────────────────────────────────
sudo install -d -m 755 "$INSTALL_DIR/bundles" "$INSTALL_DIR/mongo-data" "$INSTALL_DIR/templates"

# Copy mongo-init.js into the install dir so the mongo bundle can bind-mount it.
sudo install -m 644 "$HERE/templates/mongo-init.js" "$INSTALL_DIR/templates/mongo-init.js"

# Substitution helper.
subst() {
  sed -e "s|__INSTALL_DIR__|$INSTALL_DIR|g" \
      -e "s|__CERTS_DIR__|$CERTS_DIR|g" \
      -e "s|__TZ__|$TZ|g" \
      -e "s|__SERVER_HOST__|$SERVER_HOST|g" \
      -e "s|__SERVER_PORT__|$SERVER_PORT|g" \
      -e "s|__IDP_SERVER_HOST__|$IDP_SERVER_HOST|g" \
      -e "s|__IDP_SERVER_PORT__|$IDP_SERVER_PORT|g" \
      -e "s|__MONGO_ROOT_USER__|$MONGO_ROOT_USER|g" \
      -e "s|__MONGO_ROOT_PASS__|$MONGO_ROOT_PASS|g" \
      -e "s|__MONGO_APP_USER__|$MONGO_APP_USER|g" \
      -e "s|__MONGO_APP_PASS__|$MONGO_APP_PASS|g" \
      "$1"
}

# ── Pull images ─────────────────────────────────────────────────────────────
echo ">>> Pulling mongo:$MONGO_TAG into $INSTALL_DIR/bundles/mongo"
sudo -E "$BIN" "docker.io/library/mongo:$MONGO_TAG" --to "$INSTALL_DIR/bundles/mongo" --force

echo ">>> Pulling xpmile-wsdbagent:$WSDBAGENT_TAG into $INSTALL_DIR/bundles/wsdbagent-marvel"
sudo -E "$BIN" "docker.io/naushada/xpmile-wsdbagent:$WSDBAGENT_TAG" --to "$INSTALL_DIR/bundles/wsdbagent-marvel" --force

if [[ -n "$IDP_SERVER_HOST" ]]; then
  echo ">>> Hard-link clone wsdbagent-marvel → wsdbagent-idp"
  sudo rm -rf "$INSTALL_DIR/bundles/wsdbagent-idp"
  sudo cp -al "$INSTALL_DIR/bundles/wsdbagent-marvel" "$INSTALL_DIR/bundles/wsdbagent-idp"
fi

# ── Render and place per-bundle config.json ────────────────────────────────
echo ">>> Writing per-bundle config.json from templates"
subst "$HERE/templates/config.mongo.json"        | sudo tee "$INSTALL_DIR/bundles/mongo/config.json" >/dev/null
subst "$HERE/templates/config.wsdbagent.json"    | sudo tee "$INSTALL_DIR/bundles/wsdbagent-marvel/config.json" >/dev/null
if [[ -n "$IDP_SERVER_HOST" ]]; then
  subst "$HERE/templates/config.wsdbagent-idp.json" | sudo tee "$INSTALL_DIR/bundles/wsdbagent-idp/config.json" >/dev/null
fi

# ── Install systemd units ──────────────────────────────────────────────────
echo ">>> Installing systemd units"
for u in xpmile-mongo.service xpmile-wsdbagent.service xpmile-certs.path xpmile-certs-rotate.service; do
  subst "$HERE/templates/systemd/$u" | sudo tee "/etc/systemd/system/$u" >/dev/null
done
if [[ -n "$IDP_SERVER_HOST" ]]; then
  subst "$HERE/templates/systemd/xpmile-wsdbagent-idp.service" \
    | sudo tee "/etc/systemd/system/xpmile-wsdbagent-idp.service" >/dev/null
fi

# ── Enable & start ─────────────────────────────────────────────────────────
sudo systemctl daemon-reload
sudo systemctl enable --now xpmile-mongo.service
sudo systemctl enable --now xpmile-wsdbagent.service
[[ -n "$IDP_SERVER_HOST" ]] && sudo systemctl enable --now xpmile-wsdbagent-idp.service
sudo systemctl enable --now xpmile-certs.path

cat <<EOF

✓ xpmile runc stack installed under $INSTALL_DIR.

Verify:
  sudo systemctl status xpmile-mongo xpmile-wsdbagent$([[ -n "$IDP_SERVER_HOST" ]] && echo " xpmile-wsdbagent-idp")
  sudo journalctl -u xpmile-wsdbagent -f

Cert rotation:  drop fresh ca.crt / client.crt / client.key into $CERTS_DIR;
                xpmile-certs.path will restart both wsdbagents within ~5s.
EOF
