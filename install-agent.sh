#!/usr/bin/env bash
# install-agent.sh — one-file installer for the xpmile on-prem agent stack
#
# Usage:
#   # Recommended (single curl into bash):
#   curl -sSf https://raw.githubusercontent.com/naushada/xpmile/main/install-agent.sh | bash
#
#   # If you'd rather inspect first:
#   curl -sSO https://raw.githubusercontent.com/naushada/xpmile/main/install-agent.sh
#   less install-agent.sh
#   bash install-agent.sh
#
#   # Non-interactive (CI / re-install):
#   SERVER_HOST=marvel-xxxx.herokuapp.com \
#     IDP_SERVER_HOST=idp-xxxx.herokuapp.com \
#     bash install-agent.sh
#
# What it does (in order):
#   1. Preflight: podman/docker + compose, RAM, swap, arch
#   2. Auto-detects Pi 3B (Cortex-A53 / armv8.0-A) and pins MONGO_TAG=4.4.18
#      (last release that boots on armv8.0-A — mongo:5+ and any 4.4.x ≥ 4.4.19
#      both require armv8.2-A)
#   3. Writes ~/xpmile-agent/{docker-compose.agent.yml, .env}
#   4. Pulls images from Docker Hub:
#        - docker.io/naushada/xpmile-mongo:<MONGO_TAG>   (mongod + mongo-init.js
#          baked in, multi-arch — no local build on the operator's host)
#        - docker.io/naushada/xpmile-wsdbagent:latest    (multi-arch)
#        - docker.io/library/alpine:3.19                 (cert-watcher sidecar)
#   5. Extracts the rotated InnerTLS cert family (ca + client cert/key)
#      from the wsdbagent image's /opt/wsdbagent/baked-certs/ directory.
#      CI bakes the matching client cert family at build time (PR #44),
#      so this is just a ~10 MB extract from an image we already pull for
#      the agent itself — uniservice no longer touches the operator host.
#   6. Brings up: agent-mongo, agent-wsdbagent, optional agent-wsdbagent-idp,
#      xpmile-cert-watcher.
#   7. Verifies: containers up + wsdbagent connected to marvel.
#
# Bootstrap credentials after install:
#   accountCode: admin
#   password:    admin@123
# CHANGE THIS via the marvel UI after first login.

set -euo pipefail
IFS=$'\n\t'

# ──────────────────────────────────────────────────────────────────────────────
# Defaults — overridable via env var
# ──────────────────────────────────────────────────────────────────────────────
INSTALL_DIR="${INSTALL_DIR:-$HOME/xpmile-agent}"
SERVER_HOST="${SERVER_HOST:-}"
SERVER_PORT="${SERVER_PORT:-443}"
IDP_SERVER_HOST="${IDP_SERVER_HOST:-}"
IDP_SERVER_PORT="${IDP_SERVER_PORT:-443}"

MONGO_ROOT_USER="${MONGO_ROOT_USER:-root}"
MONGO_ROOT_PASS="${MONGO_ROOT_PASS:-changeme}"
MONGO_APP_USER="${MONGO_APP_USER:-xpmile}"
MONGO_APP_PASS="${MONGO_APP_PASS:-xpmile_pass}"

MONGO_IMAGE="${MONGO_IMAGE:-docker.io/naushada/xpmile-mongo}"
WSDBAGENT_IMAGE="${WSDBAGENT_IMAGE:-docker.io/naushada/xpmile-wsdbagent:latest}"

# ──────────────────────────────────────────────────────────────────────────────
# Pretty output
# ──────────────────────────────────────────────────────────────────────────────
if [[ -t 1 ]]; then
  C_BOLD=$'\e[1m'; C_DIM=$'\e[2m'; C_RED=$'\e[31m'; C_GRN=$'\e[32m'
  C_YEL=$'\e[33m'; C_CYN=$'\e[36m'; C_OFF=$'\e[0m'
else
  C_BOLD="" C_DIM="" C_RED="" C_GRN="" C_YEL="" C_CYN="" C_OFF=""
fi
section() { printf '\n%s── %s ──%s\n' "$C_BOLD" "$*" "$C_OFF"; }
info()    { printf '%s[info]%s  %s\n'  "$C_CYN" "$C_OFF" "$*"; }
ok()      { printf '%s[ok]%s    %s\n'  "$C_GRN" "$C_OFF" "$*"; }
warn()    { printf '%s[warn]%s  %s\n'  "$C_YEL" "$C_OFF" "$*"; }
die()     { printf '%s[error]%s %s\n'  "$C_RED" "$C_OFF" "$*" >&2; exit 1; }

# ──────────────────────────────────────────────────────────────────────────────
# Preflight — engine + arch detection
# ──────────────────────────────────────────────────────────────────────────────
section "Preflight"

detect_engine() {
  if command -v podman-compose >/dev/null 2>&1; then
    echo "podman-compose"
  elif command -v docker >/dev/null 2>&1 && docker compose version >/dev/null 2>&1; then
    echo "docker compose"
  else
    echo ""
  fi
}
COMPOSE_CMD="$(detect_engine)"
[[ -n "$COMPOSE_CMD" ]] || die "no compose driver — install podman+podman-compose OR docker+docker-compose-v2 first"
info "compose driver: $COMPOSE_CMD"

# Sniff the underlying engine for cert-watcher socket guess.
if command -v podman >/dev/null 2>&1; then
  ENGINE=podman
  DEFAULT_SOCKET=/run/podman/podman.sock
  STATUS_CMD="podman"
elif command -v docker >/dev/null 2>&1; then
  ENGINE=docker
  DEFAULT_SOCKET=/var/run/docker.sock
  STATUS_CMD="docker"
else
  die "neither podman nor docker found"
fi
info "engine: $ENGINE  (default socket: $DEFAULT_SOCKET)"

# Arch + Pi 3B detection for the mongo tag default.
ARCH="$(uname -m)"
DETECTED_PI=""
if [[ "$ARCH" == "aarch64" || "$ARCH" == "arm64" ]]; then
  if grep -qsE '^Model.*Raspberry Pi 3' /proc/cpuinfo; then
    DETECTED_PI="pi-3b"
  fi
fi

# MONGO_TAG: explicit env wins, else auto-pin for Pi 3B, else default to latest.
if [[ -n "${MONGO_TAG:-}" ]]; then
  info "MONGO_TAG=$MONGO_TAG (operator-supplied)"
elif [[ "$DETECTED_PI" == "pi-3b" ]]; then
  MONGO_TAG="4.4.18"
  warn "Raspberry Pi 3 detected (Cortex-A53 / armv8.0-A) → pinning MONGO_TAG=4.4.18"
  warn "  (mongo:5+ and any 4.4.x ≥ 4.4.19 require armv8.2-A — would SIGILL on this CPU)"
else
  MONGO_TAG="latest"
  info "MONGO_TAG=latest  (override with MONGO_TAG=4.4.18 for pre-armv8.2-A arm64 hosts)"
fi

# RAM hint — Pi 3B + shape A is tight; warn if <850 MB.
RAM_KB="$(awk '/^MemTotal:/ {print $2}' /proc/meminfo 2>/dev/null || echo 0)"
if (( RAM_KB > 0 && RAM_KB < 870000 )); then
  warn "Host has only $((RAM_KB/1024)) MB RAM — agent stack idles at ~450 MB; ensure swap is configured"
fi

# ──────────────────────────────────────────────────────────────────────────────
# Prompts — anything not in env, ask for it interactively
# ──────────────────────────────────────────────────────────────────────────────
section "Configuration"

if [[ -z "$SERVER_HOST" ]]; then
  if [[ -t 0 ]]; then
    read -rp "marvel Heroku host (e.g. marvel-xxxx.herokuapp.com): " SERVER_HOST
  else
    die "SERVER_HOST not set and stdin is not a tty — re-run with SERVER_HOST=... env var"
  fi
fi
[[ -n "$SERVER_HOST" ]] || die "SERVER_HOST is required"

if [[ -z "$IDP_SERVER_HOST" ]]; then
  if [[ -t 0 ]]; then
    read -rp "IdP Heroku host (e.g. idp-xxxx.herokuapp.com — empty to skip the IdP agent): " IDP_SERVER_HOST || true
  fi
fi
[[ -n "$IDP_SERVER_HOST" ]] && info "will bring up wsdbagent-idp → $IDP_SERVER_HOST" || info "skipping wsdbagent-idp (no IDP_SERVER_HOST)"

# ──────────────────────────────────────────────────────────────────────────────
# Write artifacts to $INSTALL_DIR
# ──────────────────────────────────────────────────────────────────────────────
section "Writing artifacts to $INSTALL_DIR"
mkdir -p "$INSTALL_DIR/certs/cloud-issued/innertls"

cat > "$INSTALL_DIR/.env" <<EOF
# Generated by install-agent.sh on $(date -u +%Y-%m-%dT%H:%M:%SZ)
SERVER_HOST=$SERVER_HOST
SERVER_PORT=$SERVER_PORT
IDP_SERVER_HOST=$IDP_SERVER_HOST
IDP_SERVER_PORT=$IDP_SERVER_PORT
MONGO_TAG=$MONGO_TAG
MONGO_ROOT_USER=$MONGO_ROOT_USER
MONGO_ROOT_PASS=$MONGO_ROOT_PASS
MONGO_APP_USER=$MONGO_APP_USER
MONGO_APP_PASS=$MONGO_APP_PASS
PODMAN_SOCKET=$DEFAULT_SOCKET
EOF

cat > "$INSTALL_DIR/docker-compose.agent.yml" <<'COMPOSE_EOF'
# Auto-generated by install-agent.sh — do not edit by hand; rerun the
# installer to regenerate. For local edits, clone the repo + use the
# in-tree docker-compose.agent.yml + run-agent.sh instead.

services:
  mongodb:
    image: docker.io/naushada/xpmile-mongo:${MONGO_TAG:-latest}
    pull_policy: always
    container_name: agent-mongo
    user: "999:999"
    restart: unless-stopped
    environment:
      MONGO_INITDB_ROOT_USERNAME: ${MONGO_ROOT_USER:-root}
      MONGO_INITDB_ROOT_PASSWORD: ${MONGO_ROOT_PASS:-changeme}
      MONGO_APP_USER: ${MONGO_APP_USER:-xpmile}
      MONGO_APP_PASS: ${MONGO_APP_PASS:-xpmile_pass}
    command: ["mongod", "--setParameter", "logComponentVerbosity={accessControl:{verbosity:0}}"]
    volumes:
      - mongo-data:/data/db
    networks: [agent-net]
    healthcheck:
      # Picks mongosh on mongo:5+, falls back to mongo on 4.4 — same compose
      # works on both. See docs/operator-pi3b.md for why.
      test: ["CMD-SHELL", "( command -v mongosh >/dev/null && SHELL_CMD=mongosh || SHELL_CMD=mongo ) && $$SHELL_CMD -u $$MONGO_INITDB_ROOT_USERNAME -p $$MONGO_INITDB_ROOT_PASSWORD --eval \"db.adminCommand('ping')\" --quiet"]
      interval: 10s
      timeout: 5s
      retries: 5
      start_period: 20s

  wsdbagent:
    image: docker.io/naushada/xpmile-wsdbagent:latest
    pull_policy: always
    container_name: agent-wsdbagent
    restart: unless-stopped
    depends_on:
      mongodb:
        condition: service_healthy
    environment:
      ARGS: >-
        --server-host ${SERVER_HOST:?SERVER_HOST must be set}
        --server-port ${SERVER_PORT:-443}
        --mongo-db-uri mongodb://${MONGO_APP_USER:-xpmile}:${MONGO_APP_PASS:-xpmile_pass}@mongodb:27017/${MONGO_DB:-xpmile}?authSource=admin
        --mongo-db-name ${MONGO_DB:-xpmile}
        --mongo-db-connection-pool ${MONGO_POOL:-10}
        --backoff ${BACKOFF:-5}
        --tls-ca /certs/ca.crt
        --tls-cert /certs/client.crt
        --tls-key /certs/client.key
    volumes:
      - ./certs/cloud-issued/innertls:/certs:ro
    networks: [agent-net]

  wsdbagent-idp:
    profiles: [idp]
    image: docker.io/naushada/xpmile-wsdbagent:latest
    pull_policy: always
    container_name: agent-wsdbagent-idp
    restart: unless-stopped
    depends_on:
      mongodb:
        condition: service_healthy
    environment:
      ARGS: >-
        --server-host ${IDP_SERVER_HOST:?IDP_SERVER_HOST must be set when using the IdP profile}
        --server-port ${IDP_SERVER_PORT:-443}
        --mongo-db-uri mongodb://${MONGO_APP_USER:-xpmile}:${MONGO_APP_PASS:-xpmile_pass}@mongodb:27017/${IDP_MONGO_DB:-idp}?authSource=admin
        --mongo-db-name ${IDP_MONGO_DB:-idp}
        --mongo-db-connection-pool ${MONGO_POOL:-10}
        --backoff ${BACKOFF:-5}
        --tls-ca /certs/ca.crt
        --tls-cert /certs/client.crt
        --tls-key /certs/client.key
    volumes:
      - ./certs/cloud-issued/innertls:/certs:ro
    networks: [agent-net]

  xpmile-cert-watcher:
    image: docker.io/library/alpine:3.19
    container_name: xpmile-cert-watcher
    restart: unless-stopped
    security_opt: [label=disable]
    command:
      - sh
      - -c
      - |
        set -eu
        apk add --no-cache curl jq util-linux >/dev/null
        log() { printf '[cert-watcher %s] %s\n' "$$(date -u +%H:%M:%S)" "$$1"; }
        SOCK=/run/podman/podman.sock
        if curl -fsS -o /dev/null --unix-socket $$SOCK 'http://d/v4.0.0/libpod/_ping' 2>/dev/null; then
          ENGINE=podman; API_BASE='http://d/v4.0.0/libpod'
          RESTART_URL_FMT="$$API_BASE/containers/%s/restart?timeout=5"
        elif curl -fsS -o /dev/null --unix-socket $$SOCK 'http://d/_ping' 2>/dev/null; then
          ENGINE=docker; API_BASE='http://d/v1.41'
          RESTART_URL_FMT="$$API_BASE/containers/%s/restart?t=5"
        else
          log "FATAL — no container engine REST API responds on $$SOCK"
          exit 1
        fi
        log "detected engine: $$ENGINE  (API base: $$API_BASE)"

        WSDBAGENT_REF="$${WSDBAGENT_REF:-docker.io/naushada/xpmile-wsdbagent:latest}"
        WSDBAGENT_REF_URLENC=$$(echo "$$WSDBAGENT_REF" | sed 's|/|%2F|g; s|:|%3A|g')

        restart_agent() {
          ctr="$$1"
          url=$$(printf "$$RESTART_URL_FMT" "$$ctr")
          rc=$$(curl -fsS -o /dev/null -w '%%{http_code}' -X POST --unix-socket $$SOCK "$$url" || echo "000")
          case "$$rc" in
            2*) log "  $$ctr restarted (HTTP $$rc)" ;;
            404) log "  $$ctr not present (skipped)" ;;
            *)  log "  $$ctr restart FAILED (HTTP $$rc)" ;;
          esac
        }

        get_local_digest() {
          curl -fsS --unix-socket $$SOCK "$$API_BASE/images/$$WSDBAGENT_REF_URLENC/json" 2>/dev/null \
            | jq -r '.Id // empty' || true
        }

        pull_and_extract_certs() {
          log "auto-pull: pulling $$WSDBAGENT_REF…"
          OLD_DIGEST=$$(get_local_digest)
          if [ "$$ENGINE" = "podman" ]; then
            curl -fsS -o /dev/null -X POST --unix-socket $$SOCK \
                 "$$API_BASE/images/pull?reference=$$WSDBAGENT_REF" \
              || { log "  pull FAILED — will retry next cycle"; return 1; }
          else
            IMG=$${WSDBAGENT_REF%:*}; TAG=$${WSDBAGENT_REF##*:}
            [ "$$IMG" = "$$WSDBAGENT_REF" ] && TAG=latest
            curl -fsS -o /dev/null -X POST --unix-socket $$SOCK \
                 "$$API_BASE/images/create?fromImage=$$IMG&tag=$$TAG" \
              || { log "  pull FAILED — will retry next cycle"; return 1; }
          fi
          NEW_DIGEST=$$(get_local_digest)
          [ -z "$$NEW_DIGEST" ] && { log "  cannot read new digest"; return 0; }
          [ "$$NEW_DIGEST" = "$$OLD_DIGEST" ] && { log "  digest unchanged ($${NEW_DIGEST:7:12}…) — no extract"; return 0; }
          log "  new digest $${NEW_DIGEST:7:12}…; extracting certs"

          CREATE_JSON='{"Image":"'"$$WSDBAGENT_REF"'","Cmd":["/bin/true"]}'
          CID=$$(curl -fsS -X POST --unix-socket $$SOCK -H "Content-Type: application/json" \
                       -d "$$CREATE_JSON" "$$API_BASE/containers/create" | jq -r '.Id' 2>/dev/null) || true
          [ -z "$$CID" ] || [ "$$CID" = "null" ] && { log "  container create FAILED"; return 1; }

          mkdir -p /watch/.staging
          curl -fsS --unix-socket $$SOCK \
               "$$API_BASE/containers/$$CID/archive?path=/opt/wsdbagent/baked-certs/" \
            | tar -xf - -C /watch/.staging --strip-components=1 \
            || { log "  archive extract FAILED"; rm -rf /watch/.staging; \
                 curl -fsS -o /dev/null -X DELETE --unix-socket $$SOCK "$$API_BASE/containers/$$CID" || true; \
                 return 1; }

          flock /watch/.lock -c '
            mv -f /watch/.staging/ca.crt     /watch/ca.crt
            mv -f /watch/.staging/client.crt /watch/client.crt
            mv -f /watch/.staging/client.key /watch/client.key
            chmod 600 /watch/client.key
          ' || log "  flock+mv had a hiccup (md5sum loop will recover)"
          rm -rf /watch/.staging
          curl -fsS -o /dev/null -X DELETE --unix-socket $$SOCK "$$API_BASE/containers/$$CID" || true
          log "  certs republished to /watch"
        }

        log "watching /watch every $${POLL_SECONDS:-5}s for cert changes"
        [ "$${AUTO_PULL:-1}" = "1" ] \
          && log "auto-pulling $$WSDBAGENT_REF every $${IMAGE_POLL_SECONDS:-900}s" \
          || log "AUTO_PULL=0 → image polling disabled"
        LAST=""; LAST_PULL_TIME=0
        while true; do
          NEW=$$(flock -s /watch/.lock find /watch -maxdepth 1 -type f \
                   \( -name '*.crt' -o -name '*.key' -o -name '*.pem' \) \
                   -exec md5sum {} + 2>/dev/null | sort | md5sum | cut -d' ' -f1)
          if [ -n "$$NEW" ] && [ "$$NEW" != "$$LAST" ]; then
            if [ -n "$$LAST" ]; then
              log "cert change detected ($$LAST -> $$NEW); restarting agents"
              restart_agent agent-wsdbagent
              restart_agent agent-wsdbagent-idp
            else
              log "first observation: $$NEW (no restart, baseline)"
            fi
            LAST=$$NEW
          fi
          if [ "$${AUTO_PULL:-1}" = "1" ]; then
            NOW=$$(date +%s)
            if [ $$((NOW - LAST_PULL_TIME)) -ge $${IMAGE_POLL_SECONDS:-900} ]; then
              pull_and_extract_certs || true
              LAST_PULL_TIME=$$NOW
            fi
          fi
          sleep $${POLL_SECONDS:-5}
        done
    environment:
      POLL_SECONDS: ${CERT_WATCH_POLL_SECONDS:-5}
      IMAGE_POLL_SECONDS: ${IMAGE_POLL_SECONDS:-900}
      AUTO_PULL: ${AUTO_PULL:-1}
      WSDBAGENT_REF: ${WSDBAGENT_IMAGE:-docker.io/naushada/xpmile-wsdbagent:latest}
    volumes:
      # :rw because the auto-pull loop writes refreshed certs into /watch.
      - ./certs/cloud-issued/innertls:/watch:rw
      - ${PODMAN_SOCKET:-/run/podman/podman.sock}:/run/podman/podman.sock
    depends_on:
      wsdbagent:
        condition: service_started

networks:
  agent-net: {}

volumes:
  mongo-data: {}
COMPOSE_EOF

ok "wrote $INSTALL_DIR/.env"
ok "wrote $INSTALL_DIR/docker-compose.agent.yml"

# ──────────────────────────────────────────────────────────────────────────────
# Cert extraction — one-time per install, plus on any cert rotation.
# CI bakes the matching client cert family into the wsdbagent image
# itself (Dockerfile.wsdbagent stage 0 → COPY --from=uniservice-source).
# Operator extracts from wsdbagent (~10 MB — the same image compose
# will pull next) instead of uniservice (~500 MB, was the old path).
# ──────────────────────────────────────────────────────────────────────────────
section "Extracting InnerTLS cert family from $WSDBAGENT_IMAGE"
info "~10 MB pull (same image compose will reuse for agent-wsdbagent + agent-wsdbagent-idp)"

$ENGINE pull "$WSDBAGENT_IMAGE" || die "pull failed — check network / Docker Hub auth"
CID=$($ENGINE create "$WSDBAGENT_IMAGE")
trap "$ENGINE rm -f $CID >/dev/null 2>&1 || true" EXIT
if ! $ENGINE cp "$CID:/opt/wsdbagent/baked-certs/." "$INSTALL_DIR/certs/cloud-issued/innertls/" 2>/dev/null; then
  $ENGINE rm -f "$CID" >/dev/null 2>&1 || true
  die "couldn't extract /opt/wsdbagent/baked-certs/ — image predates the cert-rebake (PR #44).
  Pull a post-PR-44 wsdbagent (latest after this PR's CI publishes), OR pin
  WSDBAGENT_IMAGE=docker.io/naushada/xpmile-wsdbagent:<sha> to a build that has the baked-certs/ dir."
fi
$ENGINE rm -f "$CID" >/dev/null 2>&1 || true
trap - EXIT
for f in ca.crt client.crt client.key; do
  [[ -s "$INSTALL_DIR/certs/cloud-issued/innertls/$f" ]] \
    || die "expected $f not present after extraction — wsdbagent baked-certs layout changed?"
done
chmod 600 "$INSTALL_DIR/certs/cloud-issued/innertls/client.key" 2>/dev/null || true
ok "extracted ca.crt + client.crt + client.key"

# ──────────────────────────────────────────────────────────────────────────────
# Bring up the stack
# ──────────────────────────────────────────────────────────────────────────────
section "Starting containers"
cd "$INSTALL_DIR"

# Always: mongodb + wsdbagent + cert-watcher
SERVICES=(mongodb wsdbagent xpmile-cert-watcher)
COMPOSE_ARGS=(-f docker-compose.agent.yml)
# Opt-in via profile when IDP_SERVER_HOST set
if [[ -n "$IDP_SERVER_HOST" ]]; then
  COMPOSE_ARGS+=(--profile idp)
  SERVICES+=(wsdbagent-idp)
fi
$COMPOSE_CMD "${COMPOSE_ARGS[@]}" up -d "${SERVICES[@]}" || die "compose up failed — see output above"
ok "compose up issued for ${SERVICES[*]}"

# ──────────────────────────────────────────────────────────────────────────────
# Verify
# ──────────────────────────────────────────────────────────────────────────────
section "Verifying"
sleep 5
ALL_UP=true
for c in "${SERVICES[@]/#/agent-}"; do
  # Strip "agent-" from "agent-mongodb" → "agent-mongodb", but service "mongodb" → "agent-mongo"
  # Easier: use explicit container names.
  :
done
# Explicit map (service → container) since they differ:
declare -A C2C=(
  [mongodb]=agent-mongo
  [wsdbagent]=agent-wsdbagent
  [wsdbagent-idp]=agent-wsdbagent-idp
  [xpmile-cert-watcher]=xpmile-cert-watcher
)
for svc in "${SERVICES[@]}"; do
  ctr="${C2C[$svc]}"
  if $STATUS_CMD ps --format '{{.Names}}' 2>/dev/null | grep -qx "$ctr"; then
    ok "$ctr up"
  else
    warn "$ctr NOT up"
    ALL_UP=false
  fi
done

# Tail wsdbagent for the inner-TLS handshake outcome (one of:
# "inner TLS established", "inner TLS handshake failed", silence).
sleep 8
info "wsdbagent last log lines:"
$STATUS_CMD logs --tail 6 agent-wsdbagent 2>&1 | sed 's/^/   /'

# ──────────────────────────────────────────────────────────────────────────────
# Done — what next?
# ──────────────────────────────────────────────────────────────────────────────
section "Install complete"

if $ALL_UP; then
  ok "all expected containers are up"
else
  warn "some containers are not running — see $STATUS_CMD logs <name> to debug"
fi

cat <<EOF

${C_BOLD}Bootstrap credentials${C_OFF}
  accountCode:   admin
  password:      admin@123
  ${C_YEL}→ change via the marvel UI after first login.${C_OFF}

${C_BOLD}Files installed${C_OFF}
  $INSTALL_DIR/.env
  $INSTALL_DIR/docker-compose.agent.yml
  $INSTALL_DIR/certs/cloud-issued/innertls/{ca,client}.{crt,key}

${C_BOLD}Routine maintenance${C_OFF}
  Stop:           cd $INSTALL_DIR && $COMPOSE_CMD -f docker-compose.agent.yml down
  Start:          cd $INSTALL_DIR && $COMPOSE_CMD -f docker-compose.agent.yml ${IDP_SERVER_HOST:+--profile idp }up -d
  Status:         $STATUS_CMD ps
  Logs:           $STATUS_CMD logs -f agent-wsdbagent
  Cert rotation:  re-run install-agent.sh (idempotent — overwrites .env from prompts/env, re-extracts certs)

${C_BOLD}Next steps${C_OFF}
  1. Visit https://$SERVER_HOST/login, sign in with admin / admin@123.
  2. ${C_DIM}(optional) wire up the in-house OIDC IdP — see docs/inhouse-idp.md${C_OFF}
  3. ${C_DIM}(optional) deploy on-prem Vaadin admin UI (shape B) — see docs/operator-pi3b.md §6${C_OFF}

EOF
