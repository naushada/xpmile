#!/usr/bin/env bash
# onprem/run-onprem.sh — manage the on-prem Vaadin admin UI
#
# The Vaadin admin (SSO provider configuration view) is defined as the
# `onprem-ui` service in docker-compose.agent.yml — it has to live there
# so it can join `agent-net` and reach `agent-mongo` by service hostname.
# This wrapper just owns the lifecycle commands so the on-prem operator
# isn't reaching into agent-stack tooling for a Vaadin app.
#
# Usage (run from anywhere in the repo):
#   ./onprem/run-onprem.sh start    Build (first time) + start agent-onprem-ui
#   ./onprem/run-onprem.sh stop     Stop and remove the container
#   ./onprem/run-onprem.sh logs     Follow live logs
#   ./onprem/run-onprem.sh status   Show container status
#
# Environment variables (set in .env at the repo root):
#   XPMILE_BACKEND_BASE_URL  Cloud uniservice URL the Vaadin app talks to
#                            (default: https://${SERVER_HOST})
#   ONPREM_PORT              Host port for the admin UI (default: 8090)
#   MONGO_APP_USER/PASS      Same credentials wsdbagent uses for agent-mongo

set -euo pipefail

# Resolve repo root regardless of where the script is invoked from.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

COMPOSE_FILE="docker-compose.agent.yml"
COMPOSE_CMD="podman-compose"
ONPREM_PORT="${ONPREM_PORT:-8090}"

# ── colours ───────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'

info()    { echo -e "${CYAN}[info]${RESET}  $*"; }
ok()      { echo -e "${GREEN}[ok]${RESET}    $*"; }
warn()    { echo -e "${YELLOW}[warn]${RESET}  $*"; }
die()     { echo -e "${RED}[error]${RESET} $*" >&2; exit 1; }
section() { echo -e "\n${BOLD}── $* ──${RESET}"; }

# ── sanity checks ─────────────────────────────────────────────────────────────
[[ -f "$COMPOSE_FILE" ]] || die "$COMPOSE_FILE not found at repo root (${REPO_ROOT})."
command -v "$COMPOSE_CMD" &>/dev/null || die "'$COMPOSE_CMD' not found. Install podman-compose first."

# ── subcommands ───────────────────────────────────────────────────────────────
cmd_start() {
  section "Starting on-prem Vaadin admin UI (port ${ONPREM_PORT})"
  info  "Builds xpmile-onprem:latest from ./onprem on first run (~3–5 min)."
  info  "Reachable at http://localhost:${ONPREM_PORT}/sso-config once up."
  $COMPOSE_CMD -f "$COMPOSE_FILE" up -d onprem-ui
  ok    "agent-onprem-ui started."
  warn  "Unauthenticated by design — do not expose port ${ONPREM_PORT} to the internet."
}

cmd_stop() {
  section "Stopping on-prem Vaadin admin UI"
  $COMPOSE_CMD -f "$COMPOSE_FILE" stop onprem-ui >/dev/null
  $COMPOSE_CMD -f "$COMPOSE_FILE" rm -f onprem-ui >/dev/null
  ok "agent-onprem-ui stopped."
}

cmd_logs() {
  section "Live logs — agent-onprem-ui (Ctrl-C to exit)"
  podman logs -f agent-onprem-ui
}

cmd_status() {
  section "agent-onprem-ui status"
  podman ps -a --filter name=agent-onprem-ui \
    --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}"
}

usage() {
  echo -e "${BOLD}Usage:${RESET} $0 {start|stop|logs|status}"
  echo ""
  echo "  start   Build (first run) and start the Vaadin admin UI on port ${ONPREM_PORT}"
  echo "  stop    Stop and remove the container"
  echo "  logs    Follow live logs"
  echo "  status  Show container status"
}

# ── dispatch ──────────────────────────────────────────────────────────────────
case "${1:-}" in
  start)  cmd_start  ;;
  stop)   cmd_stop   ;;
  logs)   cmd_logs   ;;
  status) cmd_status ;;
  *)      usage; exit 1 ;;
esac
