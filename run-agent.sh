#!/usr/bin/env bash
# run-agent.sh — build and manage the MongoDB + wsdbagent container stack
#
# Usage:
#   ./run-agent.sh build    Build both images (required on first run or after code changes)
#   ./run-agent.sh start    Start both containers (builds if images are missing)
#   ./run-agent.sh stop     Stop and remove containers (data volume is preserved)
#   ./run-agent.sh restart  Stop then start
#   ./run-agent.sh logs     Follow live logs from both containers
#   ./run-agent.sh status   Show container status
#   ./run-agent.sh clean    Stop containers AND delete the MongoDB data volume

set -euo pipefail

COMPOSE_FILE="docker-compose.agent.yml"
COMPOSE_CMD="podman-compose"

# ── colours ───────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'

info()    { echo -e "${CYAN}[info]${RESET}  $*"; }
ok()      { echo -e "${GREEN}[ok]${RESET}    $*"; }
warn()    { echo -e "${YELLOW}[warn]${RESET}  $*"; }
die()     { echo -e "${RED}[error]${RESET} $*" >&2; exit 1; }
section() { echo -e "\n${BOLD}── $* ──${RESET}"; }

# ── sanity checks ─────────────────────────────────────────────────────────────
[[ -f "$COMPOSE_FILE" ]] || die "$COMPOSE_FILE not found — run this script from the repo root."
command -v "$COMPOSE_CMD" &>/dev/null || die "'$COMPOSE_CMD' not found. Install podman-compose first."

# ── .env setup ────────────────────────────────────────────────────────────────
ensure_env() {
  if [[ ! -f .env ]]; then
    warn ".env not found."
    if [[ -f .env.agent ]]; then
      cp .env.agent .env
      info "Copied .env.agent → .env"
    else
      touch .env
      info "Created empty .env"
    fi
  fi

  # Prompt for SERVER_HOST if missing or still set to placeholder
  local current_host
  current_host=$(grep -E '^SERVER_HOST=' .env | cut -d= -f2- | tr -d '[:space:]' || true)
  if [[ -z "$current_host" || "$current_host" == "marvel.herokuapp.com" ]]; then
    echo -n "  Enter SERVER_HOST (e.g. marvel.herokuapp.com): "
    read -r input_host
    if [[ -n "$input_host" ]]; then
      # update or append
      if grep -q '^SERVER_HOST=' .env; then
        sed -i.bak "s|^SERVER_HOST=.*|SERVER_HOST=${input_host}|" .env && rm -f .env.bak
      else
        echo "SERVER_HOST=${input_host}" >> .env
      fi
      ok "SERVER_HOST set to ${input_host}"
    fi
  else
    info "Using SERVER_HOST=${current_host}"
  fi
}

# ── subcommands ───────────────────────────────────────────────────────────────
cmd_build() {
  section "Building images"
  ensure_env
  info "Building mongodb and wsdbagent images (this takes 20–30 min on first run)…"
  $COMPOSE_CMD -f "$COMPOSE_FILE" build
  ok "Build complete."
}

cmd_start() {
  section "Starting containers"
  ensure_env

  # Auto-build if either image is missing
  if ! podman image exists xpmile-mongo:latest &>/dev/null || \
     ! podman image exists wsdbagent:latest &>/dev/null; then
    warn "One or more images not found — building first."
    $COMPOSE_CMD -f "$COMPOSE_FILE" build
  fi

  $COMPOSE_CMD -f "$COMPOSE_FILE" up -d
  echo ""
  info "Waiting for MongoDB healthcheck…"
  local attempts=0
  until podman inspect agent-mongo --format '{{.State.Health.Status}}' 2>/dev/null | grep -q healthy; do
    ((attempts++))
    if ((attempts > 30)); then
      warn "MongoDB did not become healthy within 60s — check logs with: ./run-agent.sh logs"
      break
    fi
    sleep 2
    echo -n "."
  done
  echo ""
  cmd_status
  echo ""
  info "Follow logs with:  ./run-agent.sh logs"
  info "Check Heroku side: heroku logs --tail --app <app-name>"
}

cmd_stop() {
  section "Stopping containers"
  $COMPOSE_CMD -f "$COMPOSE_FILE" down
  ok "Containers stopped. Data volume preserved."
}

cmd_restart() {
  cmd_stop
  cmd_start
}

cmd_logs() {
  section "Live logs (Ctrl-C to exit)"
  $COMPOSE_CMD -f "$COMPOSE_FILE" logs -f
}

cmd_status() {
  section "Container status"
  podman ps -a \
    --filter name=agent-mongo \
    --filter name=agent-wsdbagent \
    --format "table {{.Names}}\t{{.Status}}\t{{.Image}}"
}

cmd_clean() {
  section "Clean — stop containers and delete data volume"
  warn "This will DELETE all MongoDB data stored in the 'mongo-data' volume."
  echo -n "  Are you sure? (yes/N): "
  read -r confirm
  [[ "$confirm" == "yes" ]] || { info "Aborted."; exit 0; }
  $COMPOSE_CMD -f "$COMPOSE_FILE" down -v
  ok "Containers and data volume removed."
}

usage() {
  echo -e "${BOLD}Usage:${RESET} $0 {build|start|stop|restart|logs|status|clean}"
  echo ""
  echo "  build    Build both Docker images"
  echo "  start    Start MongoDB + wsdbagent (builds if images are missing)"
  echo "  stop     Stop containers (data preserved)"
  echo "  restart  Stop then start"
  echo "  logs     Follow live logs from both containers"
  echo "  status   Show container status"
  echo "  clean    Stop containers and delete the MongoDB data volume"
}

# ── dispatch ──────────────────────────────────────────────────────────────────
case "${1:-}" in
  build)   cmd_build   ;;
  start)   cmd_start   ;;
  stop)    cmd_stop    ;;
  restart) cmd_restart ;;
  logs)    cmd_logs    ;;
  status)  cmd_status  ;;
  clean)   cmd_clean   ;;
  *)       usage; exit 1 ;;
esac
