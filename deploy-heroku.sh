#!/usr/bin/env bash
# deploy-heroku.sh — build, push, and release xpmile to Heroku
#
# Usage:
#   ./deploy-heroku.sh login              Authenticate with the Heroku container registry
#   ./deploy-heroku.sh build-bootstrap    Build the shared C++ toolchain image (amd64)
#   ./deploy-heroku.sh build              Full build — C++ + Angular (~30-40 min first run)
#                                         (auto-invokes build-bootstrap if needed)
#   ./deploy-heroku.sh build-ui           Rebuild Angular only, reuse C++ cache (~3 min)
#   ./deploy-heroku.sh push               Push the built image to registry.heroku.com
#   ./deploy-heroku.sh release            Release the pushed image (activate on dyno)
#   ./deploy-heroku.sh deploy             build-ui + push + release  (typical redeploy)
#   ./deploy-heroku.sh deploy full        Full build + push + release
#   ./deploy-heroku.sh logs               Tail live Heroku logs
#   ./deploy-heroku.sh open               Open the app in the browser
#
# Environment variables (export or prefix on the command line):
#   HEROKU_APP=marvel   Heroku app name   (default: marvel)
#   UI_BUST=<value>     Angular cache-bust seed (auto-set by build-ui)

set -euo pipefail

COMPOSE_FILE="docker-compose.heroku.yml"
COMPOSE_CMD="podman-compose"
APP="${HEROKU_APP:-marvel}"
IMAGE="registry.heroku.com/${APP}/web"
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
command -v heroku         &>/dev/null || die "'heroku' CLI not found. Install the Heroku CLI first."

# ── subcommands ───────────────────────────────────────────────────────────────
cmd_login() {
  section "Authenticating with Heroku container registry"
  heroku auth:token | podman login --username=_ --password-stdin registry.heroku.com
  ok "Logged in to registry.heroku.com"
}

# Ensure `localhost/xpmile-cpp-builder:bootstrap` exists locally as amd64.
# docker/Dockerfile's first stage starts with
#   FROM ${BUILDER_IMAGE}   (default: localhost/xpmile-cpp-builder:bootstrap)
# — if that image is missing OR the wrong arch, podman tries to pull from
# a "localhost" registry and fails with "connection refused". Catching it
# here turns the silent-30-min-into-a-cryptic-error into a one-shot
# bootstrap on the first deploy after a prune.
cmd_build_bootstrap() {
  section "Ensuring C++ toolchain bootstrap image (${BOOTSTRAP_TAG})"
  local need_build=0 reason=""
  local existing_arch
  existing_arch=$(podman image inspect "$BOOTSTRAP_TAG" \
                    --format '{{.Architecture}}' 2>/dev/null || echo "")
  if [ -z "$existing_arch" ]; then
    need_build=1; reason="not present on host"
  elif [ "$existing_arch" != "amd64" ]; then
    need_build=1; reason="host bootstrap is $existing_arch but Heroku needs amd64"
  fi

  if [ "$need_build" = "0" ]; then
    ok "$BOOTSTRAP_TAG already on host (amd64) — skipping bootstrap build"
    return 0
  fi

  warn "$BOOTSTRAP_TAG missing or wrong arch ($reason) — building (~30 min cold, via QEMU)"
  [ -f "$BOOTSTRAP_DOCKERFILE" ] \
    || die "$BOOTSTRAP_DOCKERFILE not found — are you in the repo root?"
  podman build \
    --platform linux/amd64 \
    -f "$BOOTSTRAP_DOCKERFILE" \
    -t "$BOOTSTRAP_TAG" \
    .
  ok "$BOOTSTRAP_TAG built — cached for every subsequent cmd_build / cmd_build_ui"
}

cmd_build() {
  # Bootstrap first — Dockerfile's FROM line depends on it.
  # No-op when already present at the right arch.
  cmd_build_bootstrap

  section "Building image for Heroku (full — C++ + Angular, linux/amd64)"
  info "App: ${APP}"
  info "Toolchain is pre-built (bootstrap image); compile + UI takes ~5–8 min cached."
  echo ""
  HEROKU_APP="$APP" $COMPOSE_CMD -f "$COMPOSE_FILE" build
  ok "Build complete. Image: ${IMAGE}"
}

cmd_build_ui() {
  cmd_build_bootstrap

  section "Rebuilding Angular UI for Heroku (C++ cache reused, ~3 min)"
  info "App: ${APP}"
  HEROKU_APP="$APP" UI_BUST=$(date +%s) $COMPOSE_CMD -f "$COMPOSE_FILE" build
  ok "UI rebuild complete. Image: ${IMAGE}"
}

cmd_push() {
  section "Pushing image to Heroku registry"
  info "Image: ${IMAGE}"
  # podman push with v2s2 manifest — required by Heroku's registry
  podman push --format=v2s2 "${IMAGE}"
  ok "Push complete."
}

cmd_release() {
  section "Releasing image on Heroku"
  info "App: ${APP}"
  heroku container:release web --app "${APP}"
  ok "Release triggered. Dyno is restarting."
  echo ""
  info "Monitor startup with:  ./deploy-heroku.sh logs"
}

cmd_deploy() {
  local mode="${1:-ui}"   # "ui" (default) or "full"
  if [[ "$mode" == "full" ]]; then
    cmd_build
  else
    cmd_build_ui
  fi
  cmd_push
  cmd_release
}

cmd_logs() {
  section "Heroku live logs (Ctrl-C to exit)"
  heroku logs --tail --app "${APP}"
}

cmd_open() {
  heroku open --app "${APP}"
}

usage() {
  echo -e "${BOLD}Usage:${RESET} $0 <command> [option]"
  echo ""
  echo "  login              Authenticate with the Heroku container registry"
  echo "  build-bootstrap    Build the shared C++ toolchain image (amd64)"
  echo "  build              Full build — C++ + Angular (linux/amd64)"
  echo "  build-ui           Rebuild Angular only, reuse C++ layer cache"
  echo "  push               Push the built image to registry.heroku.com"
  echo "  release            Release (activate) the pushed image on the dyno"
  echo "  deploy             build-ui + push + release  (default redeploy)"
  echo "  deploy full        Full build + push + release"
  echo "  logs               Tail live Heroku logs"
  echo "  open               Open the app in the browser"
  echo ""
  echo -e "${BOLD}Environment variables:${RESET}"
  echo "  HEROKU_APP=marvel   Heroku app name (default: marvel)"
  echo ""
  echo -e "${BOLD}Examples:${RESET}"
  echo "  ./deploy-heroku.sh login"
  echo "  ./deploy-heroku.sh deploy              # UI-only rebuild then deploy"
  echo "  ./deploy-heroku.sh deploy full         # full rebuild then deploy"
  echo "  HEROKU_APP=xpmile ./deploy-heroku.sh deploy"
}

# ── dispatch ──────────────────────────────────────────────────────────────────
CMD="${1:-}"
ARG="${2:-}"

case "$CMD" in
  login)             cmd_login ;;
  build-bootstrap)   cmd_build_bootstrap ;;
  build)             cmd_build ;;
  build-ui)          cmd_build_ui ;;
  push)              cmd_push ;;
  release)           cmd_release ;;
  deploy)            cmd_deploy "$ARG" ;;
  logs)              cmd_logs ;;
  open)              cmd_open ;;
  *)                 usage; exit 1 ;;
esac
