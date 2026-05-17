#!/usr/bin/env bash
# run.sh — build and manage the xpmile app stack (MongoDB + uniservice + Angular UI)
#
# Usage:
#   ./run.sh build          Build both images (C++ + Angular, 30–40 min on first run)
#   ./run.sh build-ui       Rebuild Angular UI only (reuses cached C++ layer, ~3 min)
#   ./run.sh start          Start MongoDB + app (builds if images are missing)
#   ./run.sh start remote   Start in --remote-db mode (wsdbagent provides MongoDB)
#   ./run.sh stop           Stop and remove containers (data volume preserved)
#   ./run.sh restart        Stop then start
#   ./run.sh logs           Follow live logs from both containers
#   ./run.sh logs app       Follow app logs only
#   ./run.sh logs db        Follow MongoDB logs only
#   ./run.sh status         Show container status
#   ./run.sh clean          Stop containers AND delete the MongoDB data volume

set -euo pipefail

COMPOSE_FILE="docker-compose.yml"
COMPOSE_CMD="podman-compose"
APP_URL="http://localhost:${HOST_PORT:-8080}/webui/"
BOOTSTRAP_TAG="localhost/xpmile-cpp-builder:bootstrap"
BOOTSTRAP_DOCKERFILE="docker/Dockerfile.bootstrap"

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

# ── subcommands ───────────────────────────────────────────────────────────────

# Ensure `localhost/xpmile-cpp-builder:bootstrap` exists locally.
# docker/Dockerfile and docker/Dockerfile.wsdbagent both start with
#   FROM ${BUILDER_IMAGE}   (default: localhost/xpmile-cpp-builder:bootstrap)
# — without this image present, the build fails with a "pull from
# localhost" error. Auto-invoked by cmd_build / cmd_build_ui so the
# common flows keep working after a podman prune.
cmd_build_bootstrap() {
  if podman image exists "$BOOTSTRAP_TAG" 2>/dev/null; then
    return 0
  fi
  section "Building C++ toolchain bootstrap image (one-time, ~30 min cold)"
  warn "$BOOTSTRAP_TAG not present — building."
  [[ -f "$BOOTSTRAP_DOCKERFILE" ]] \
    || die "$BOOTSTRAP_DOCKERFILE not found — run this script from the repo root."
  podman build -f "$BOOTSTRAP_DOCKERFILE" -t "$BOOTSTRAP_TAG" .
  ok "$BOOTSTRAP_TAG built — cached for every subsequent build."
}

cmd_build() {
  cmd_build_bootstrap
  section "Building images (uniservice + Angular UI)"
  info "Toolchain is pre-built (bootstrap image); compile + UI takes ~5–8 min."
  echo ""
  $COMPOSE_CMD -f "$COMPOSE_FILE" build
  ok "Build complete."
}

cmd_build_ui() {
  cmd_build_bootstrap
  section "Rebuilding Angular UI only"
  info "Reuses the cached C++ layer — takes ~3 minutes."
  UI_BUST=$(date +%s) $COMPOSE_CMD -f "$COMPOSE_FILE" build app
  ok "UI rebuild complete."
}

cmd_start() {
  local remote_db="${1:-}"
  section "Starting containers${remote_db:+ (remote-db mode)}"

  # Auto-build if images are missing
  if ! podman image exists xpmile-mongo:latest &>/dev/null || \
     ! podman image exists xpmile:latest &>/dev/null; then
    warn "One or more images not found — building first (this will take a while)."
    $COMPOSE_CMD -f "$COMPOSE_FILE" build
  fi

  if [[ -n "$remote_db" ]]; then
    info "Starting in --remote-db mode: uniservice will wait for wsdbagent to connect."
    REMOTE_DB=1 $COMPOSE_CMD -f "$COMPOSE_FILE" up -d
  else
    $COMPOSE_CMD -f "$COMPOSE_FILE" up -d
  fi

  echo ""
  info "Waiting for MongoDB healthcheck…"
  local attempts=0
  until podman inspect xpmile-mongo --format '{{.State.Health.Status}}' 2>/dev/null | grep -q healthy; do
    ((attempts++))
    if ((attempts > 30)); then
      warn "MongoDB did not become healthy within 60s — check logs with: ./run.sh logs db"
      break
    fi
    sleep 2
    echo -n "."
  done
  echo ""

  cmd_status
  echo ""
  ok "App is up at ${APP_URL}"
  info "Follow logs with:  ./run.sh logs"
}

cmd_stop() {
  section "Stopping containers"
  $COMPOSE_CMD -f "$COMPOSE_FILE" down
  ok "Containers stopped. Data volume preserved."
}

cmd_restart() {
  local remote_db="${1:-}"
  cmd_stop
  cmd_start "$remote_db"
}

cmd_logs() {
  local target="${1:-}"
  section "Live logs (Ctrl-C to exit)"
  case "$target" in
    app) podman logs -f xpmile-app ;;
    db)  podman logs -f xpmile-mongo ;;
    *)   $COMPOSE_CMD -f "$COMPOSE_FILE" logs -f ;;
  esac
}

cmd_status() {
  section "Container status"
  podman ps -a \
    --filter name=xpmile-mongo \
    --filter name=xpmile-app \
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
  echo -e "${BOLD}Usage:${RESET} $0 <command> [option]"
  echo ""
  echo "  build          Build both images (C++ + Angular)"
  echo "  build-ui       Rebuild Angular UI only (reuses C++ cache)"
  echo "  start          Start MongoDB + app"
  echo "  start remote   Start in --remote-db mode (wsdbagent supplies MongoDB)"
  echo "  stop           Stop containers (data preserved)"
  echo "  restart        Stop then start"
  echo "  restart remote Stop then start in --remote-db mode"
  echo "  logs           Follow logs from both containers"
  echo "  logs app       Follow app (uniservice) logs only"
  echo "  logs db        Follow MongoDB logs only"
  echo "  status         Show container status"
  echo "  clean          Stop containers and delete the MongoDB data volume"
  echo ""
  echo -e "${BOLD}Environment variables (export or prefix on the command):${RESET}"
  echo "  HOST_PORT=8080    Host port mapped to the app  (default: 8080)"
  echo "  PORT=8080         Container port the app binds (default: 8080)"
  echo "  SERVER_WORKERS=5  uniservice worker threads    (default: 5)"
  echo "  MONGO_POOL=20     MongoDB connection pool size  (default: 20)"
  echo "  MONGO_DB=xpmile   Database name               (default: xpmile)"
  echo ""
  echo -e "${BOLD}Examples:${RESET}"
  echo "  ./run.sh build"
  echo "  ./run.sh start"
  echo "  ./run.sh start remote          # with wsdbagent on a separate machine"
  echo "  HOST_PORT=9090 ./run.sh start  # serve on port 9090"
  echo "  ./run.sh build-ui && ./run.sh restart"
}

# ── dispatch ──────────────────────────────────────────────────────────────────
CMD="${1:-}"
ARG="${2:-}"

case "$CMD" in
  build)           cmd_build ;;
  build-bootstrap) cmd_build_bootstrap ;;
  build-ui)        cmd_build_ui ;;
  start)           cmd_start   "$ARG" ;;
  stop)            cmd_stop ;;
  restart)         cmd_restart "$ARG" ;;
  logs)            cmd_logs    "$ARG" ;;
  status)          cmd_status ;;
  clean)           cmd_clean ;;
  *)               usage; exit 1 ;;
esac
