#!/usr/bin/env bash
# install.sh — one-shot installer for the xpmile on-prem stack.
#
# Targets a fresh on-prem host (Raspberry Pi 4/5 on aarch64, a small
# linux/amd64 server, or anything multi-arch) and brings up everything
# the deployment needs:
#
#   agent-mongo          MongoDB 7 (built locally from docker/Dockerfile.mongo
#                        on top of the pulled mongo:7 — ~5 s build)
#   agent-wsdbagent      docker.io/naushada/xpmile-wsdbagent:latest (multi-arch)
#   xpmile-cert-watcher  docker.io/library/alpine:3.19
#   agent-onprem-ui      Vaadin admin UI on port 8090 — pulled from Docker Hub
#                        if available, otherwise built locally (slow on a Pi)
#
# What this script does:
#   1. Sanity-checks the host (container runtime, disk, network, port 8090)
#      and resolves `uname -m` to a docker platform string — `linux/amd64`
#      or `linux/arm64` — that every `pull` is then explicitly pinned to.
#      Works on any host where multi-arch images exist for the detected
#      arch: Raspberry Pi 4/5 (64-bit), small amd64 servers, AWS Graviton,
#      Apple Silicon, etc.
#   2. Pulls the images from Docker Hub up-front, with `--platform`, so
#      subsequent bring-up commands don't wait on slow Pi-side pulls
#   3. Sets up .env interactively if missing — prompts for SERVER_HOST and
#      derives XPMILE_BACKEND_BASE_URL
#   4. Runs ./run-agent.sh start  (agent core + cert-watcher)
#   5. Runs ./onprem/run-onprem.sh start  (Vaadin admin)
#   6. Probes each container's health and the Vaadin landing page
#   7. Prints next-step pointers
#
# This is a first-time bootstrap. Day-to-day management lives in
# ./run-agent.sh and ./onprem/run-onprem.sh; you don't need to re-run
# install.sh on every reboot — restart is automatic via
# `restart: unless-stopped` on every container.

set -euo pipefail

# ── images we expect from Docker Hub ──────────────────────────────────────────
MONGO_BASE_IMAGE="docker.io/library/mongo:7"
ALPINE_IMAGE="docker.io/library/alpine:3.19"
WSDBAGENT_IMAGE="${WSDBAGENT_IMAGE:-docker.io/naushada/xpmile-wsdbagent:latest}"
ONPREM_IMAGE="${ONPREM_IMAGE:-docker.io/naushada/xpmile-onprem:latest}"
ONPREM_PORT="${ONPREM_PORT:-8090}"

# ── colours ───────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'

info()    { echo -e "${CYAN}[info]${RESET}  $*"; }
ok()      { echo -e "${GREEN}[ok]${RESET}    $*"; }
warn()    { echo -e "${YELLOW}[warn]${RESET}  $*"; }
die()     { echo -e "${RED}[error]${RESET} $*" >&2; exit 1; }
section() { echo -e "\n${BOLD}── $* ──${RESET}"; }

# ── resolve repo root regardless of cwd ───────────────────────────────────────
# `${BASH_SOURCE[0]:-.}` keeps this safe under `curl | bash`, where
# BASH_SOURCE[0] is not a real file path; SCRIPT_DIR then just resolves to
# the operator's cwd, which is what the bootstrap check below is for.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-.}")" 2>/dev/null && pwd || pwd)"

# ── self-bootstrap when piped via `curl | bash` outside a checkout ────────────
# install.sh needs the rest of the repo (run-agent.sh, the agent
# compose, .env.agent, Dockerfile.mongo, …). When it's fetched via
# `curl ... | bash` on a fresh host there is no checkout — detect that and
# clone v1.0.0 ourselves, then re-exec from inside it.
if [[ ! -f "${SCRIPT_DIR}/run-agent.sh" || ! -f "${SCRIPT_DIR}/docker-compose.agent.yml" ]]; then
  if [[ "${XPMILE_BOOTSTRAPPED:-0}" == "1" ]]; then
    die "Bootstrap re-exec didn't land in an xpmile checkout. Clone manually:
       git clone --branch v1.0.0 --depth 1 https://github.com/naushada/xpmile.git
       cd xpmile && ./install.sh"
  fi
  section "Bootstrap — no xpmile checkout next to this script"
  command -v git &>/dev/null || die "git not found. Install it first:
       Debian/Raspberry Pi OS:  sudo apt-get install -y git
       Fedora/RHEL:             sudo dnf install -y git
       macOS:                   xcode-select --install"

  # XPMILE_REF — override to pin to a specific tag or branch. Defaults to
  # release/v1.0 (the rolling v1.0 line, which always carries this installer).
  # For an immutable pin use e.g. XPMILE_REF=v1.0.0 (once that tag covers the
  # installer commit) or XPMILE_REF=v1.0.1.
  local REF="${XPMILE_REF:-release/v1.0}"
  TARGET_DIR="${XPMILE_DIR:-${PWD}/xpmile}"
  if [[ -d "${TARGET_DIR}/.git" ]]; then
    info "Reusing existing checkout at ${TARGET_DIR}"
    cd "${TARGET_DIR}"
    git fetch --tags --quiet
    git checkout "${REF}"
  else
    [[ -e "${TARGET_DIR}" ]] && die "${TARGET_DIR} exists but is not a git checkout. Move it aside or set XPMILE_DIR=<other-path>."
    info "Cloning xpmile (${REF}) into ${TARGET_DIR}"
    git clone --branch "${REF}" --depth 1 https://github.com/naushada/xpmile.git "${TARGET_DIR}"
    cd "${TARGET_DIR}"
  fi

  export XPMILE_BOOTSTRAPPED=1
  exec bash ./install.sh "$@"
fi

cd "${SCRIPT_DIR}"

# ── runtime / platform selection ──────────────────────────────────────────────
# PLATFORM is set from `uname -m` in check_prereqs and passed to every `pull`
# so we explicitly select the right manifest from each multi-arch image. The
# default behaviour usually does the right thing, but pinning at pull time is
# the right belt-and-braces move — defends against DOCKER_DEFAULT_PLATFORM
# being set, or a podman machine VM whose default arch differs from the OS
# the operator is on.
CONTAINER_CMD=""
COMPOSE_CMD=""
PLATFORM=""
ONPREM_PUBLISHED=0

# ── 1. sanity checks ──────────────────────────────────────────────────────────
check_prereqs() {
  section "Sanity checks"

  local arch
  arch=$(uname -m)
  case "$arch" in
    aarch64|arm64)
      PLATFORM="linux/arm64"
      info "Architecture: ${arch} → pulling ${PLATFORM} (Raspberry Pi 4/5 64-bit, AWS Graviton, Apple Silicon, …)"
      ;;
    x86_64|amd64)
      PLATFORM="linux/amd64"
      info "Architecture: ${arch} → pulling ${PLATFORM}"
      ;;
    armv6l|armv7l)
      die "Architecture ${arch} is not supported. Published images are amd64 + arm64 only.
       A 64-bit OS is required on Raspberry Pi — use Raspberry Pi OS 64-bit on a Pi 4 or 5."
      ;;
    *)
      PLATFORM="linux/${arch}"
      warn "Architecture ${arch} is unusual; pulling --platform ${PLATFORM} but the multi-arch images may not have a matching layer."
      ;;
  esac

  if command -v podman &>/dev/null && command -v podman-compose &>/dev/null; then
    CONTAINER_CMD="podman"
    COMPOSE_CMD="podman-compose"
  elif command -v docker &>/dev/null && command -v docker-compose &>/dev/null; then
    CONTAINER_CMD="docker"
    COMPOSE_CMD="docker-compose"
  elif command -v docker &>/dev/null && docker compose version >/dev/null 2>&1; then
    CONTAINER_CMD="docker"
    COMPOSE_CMD="docker compose"
  else
    die "Neither podman+podman-compose nor docker+docker-compose found.
       Install one:
         Raspberry Pi OS / Debian:  sudo apt-get install -y podman podman-compose
         Ubuntu:                    sudo apt-get install -y podman podman-compose
         macOS:                     brew install podman podman-compose
                                    podman machine init && podman machine start"
  fi
  info "Container runtime: ${CONTAINER_CMD} (compose via ${COMPOSE_CMD})"

  local missing=()
  for f in run-agent.sh onprem/run-onprem.sh docker-compose.agent.yml .env.agent docker/Dockerfile.mongo onprem/Dockerfile; do
    [[ -f "$f" ]] || missing+=("$f")
  done
  if [[ ${#missing[@]} -ne 0 ]]; then
    die "Run this script from a checkout of the release/v1.0 branch (or v1.0.0 tag).
       Missing: ${missing[*]}"
  fi
  info "Repo files present."

  # Rough disk-space check. mongo:7 (~700 MB), wsdbagent (~170 MB),
  # alpine (~5 MB), Maven build cache (~600 MB), mongo-data + GridFS room.
  local avail_kb avail_gb
  avail_kb=$(df -k . 2>/dev/null | awk 'NR==2 {print $4}' || echo 0)
  avail_gb=$(( avail_kb / 1024 / 1024 ))
  if [[ "$avail_gb" -lt 5 ]]; then
    warn "Less than 5 GB free under $(pwd). The Vaadin build alone needs ~2 GB; continuing anyway."
  else
    info "Disk space: ${avail_gb} GB available."
  fi

  # Outbound HTTPS smoke test — Docker Hub registry probe.
  if curl -sI --max-time 5 https://registry-1.docker.io/v2/ >/dev/null 2>&1; then
    info "Outbound HTTPS to Docker Hub: reachable."
  else
    warn "Could not reach registry-1.docker.io. Image pulls will fail unless your network policy allows it."
  fi

  # Port 8090 free?
  if (command -v ss   &>/dev/null && ss   -tln 2>/dev/null | awk '{print $4}' | grep -qE "[:.]${ONPREM_PORT}\$") \
  || (command -v lsof &>/dev/null && lsof -nP -iTCP:${ONPREM_PORT} -sTCP:LISTEN 2>/dev/null | grep -q .); then
    warn "Port ${ONPREM_PORT} is already in use. The Vaadin admin will fail to bind until it is freed."
  else
    info "Port ${ONPREM_PORT} is free."
  fi
}

# ── 2. .env setup ─────────────────────────────────────────────────────────────
setup_env() {
  section ".env setup"

  if [[ -f .env ]]; then
    info ".env already present — using it as-is."
  else
    cp .env.agent .env
    info ".env created from .env.agent."
  fi

  # SERVER_HOST — required, prompt if missing/placeholder.
  local current_host
  current_host=$(grep -E '^SERVER_HOST=' .env | cut -d= -f2- | tr -d '[:space:]' || true)
  if [[ -z "$current_host" || "$current_host" == "marvel.herokuapp.com" ]]; then
    echo -n "  Enter SERVER_HOST (the cloud uniservice hostname, e.g. marvel-3a78bd953f5f.herokuapp.com): "
    read -r input_host
    [[ -n "$input_host" ]] || die "SERVER_HOST is required."
    if grep -q '^SERVER_HOST=' .env; then
      sed -i.bak "s|^SERVER_HOST=.*|SERVER_HOST=${input_host}|" .env && rm -f .env.bak
    else
      echo "SERVER_HOST=${input_host}" >> .env
    fi
    ok "SERVER_HOST set to ${input_host}."
    current_host="$input_host"
  else
    info "SERVER_HOST already set: ${current_host}"
  fi

  # XPMILE_BACKEND_BASE_URL — derive from SERVER_HOST if missing.
  local current_url
  current_url=$(grep -E '^XPMILE_BACKEND_BASE_URL=' .env | cut -d= -f2- | tr -d '[:space:]' || true)
  if [[ -z "$current_url" ]]; then
    local derived="https://${current_host}"
    echo "XPMILE_BACKEND_BASE_URL=${derived}" >> .env
    ok "XPMILE_BACKEND_BASE_URL set to ${derived}."
  else
    info "XPMILE_BACKEND_BASE_URL already set: ${current_url}"
  fi

  # Friendly nudge about default passwords.
  if grep -qE '^MONGO_APP_PASS=xpmile_pass$' .env || grep -qE '^MONGO_ROOT_PASS=changeme$' .env; then
    warn "MongoDB passwords are still defaults. Change them in .env before exposing the deployment."
  fi
}

# ── 3. pull images ────────────────────────────────────────────────────────────
pull_images() {
  section "Pulling images from Docker Hub"

  local img
  for img in "$MONGO_BASE_IMAGE" "$WSDBAGENT_IMAGE" "$ALPINE_IMAGE"; do
    info "Pull ${PLATFORM}: ${img}"
    if ! "$CONTAINER_CMD" pull --platform "$PLATFORM" "$img"; then
      die "Pull failed for ${img} on ${PLATFORM}. Check network / registry visibility / arch availability for this image."
    fi
  done

  info "Pull ${PLATFORM} (best-effort): ${ONPREM_IMAGE}"
  if "$CONTAINER_CMD" pull --platform "$PLATFORM" "$ONPREM_IMAGE" 2>/dev/null; then
    "$CONTAINER_CMD" tag "$ONPREM_IMAGE" xpmile-onprem:latest
    ok "${ONPREM_IMAGE} pulled (${PLATFORM}) — Vaadin admin will use the published image."
    ONPREM_PUBLISHED=1
  else
    warn "${ONPREM_IMAGE} not pullable yet on ${PLATFORM}. Falling back to a local build."
    warn "On a Raspberry Pi this Maven build takes ~15–25 min. Subsequent starts are instant (image is cached)."
    warn "To skip this step in the future, publish xpmile-onprem to Docker Hub from CI (multi-arch)."
    ONPREM_PUBLISHED=0
  fi
}

# ── 4. start the stack ────────────────────────────────────────────────────────
start_stack() {
  section "Starting the agent stack (mongo + wsdbagent + cert-watcher)"
  ./run-agent.sh start

  section "Starting the Vaadin admin (onprem-ui)"
  if [[ "$ONPREM_PUBLISHED" -eq 1 ]]; then
    info "Skipping the local Maven build — pulled image will be used."
  else
    info "Building xpmile-onprem:latest locally now."
  fi
  ./onprem/run-onprem.sh start
}

# ── 5. verify ─────────────────────────────────────────────────────────────────
verify() {
  section "Verifying health"
  sleep 5

  local all_ok=1
  local name
  for name in agent-mongo agent-wsdbagent xpmile-cert-watcher agent-onprem-ui; do
    if "$CONTAINER_CMD" ps --filter "name=${name}" --format '{{.Names}}' 2>/dev/null | grep -qx "$name"; then
      ok "${name} running"
    else
      warn "${name} not running — check '${CONTAINER_CMD} logs ${name}'"
      all_ok=0
    fi
  done

  # Vaadin landing probe — Spring Boot may still be coming up.
  local probe
  probe=$(curl -s -o /dev/null -w "%{http_code}" --max-time 12 "http://localhost:${ONPREM_PORT}/sso-config" 2>/dev/null || echo "000")
  case "$probe" in
    200) ok    "Vaadin admin reachable at http://localhost:${ONPREM_PORT}/sso-config" ;;
    000) warn  "Vaadin admin not yet reachable — Spring Boot may still be starting. Re-probe in ~30 s." ;;
    *)   warn  "Vaadin admin returned HTTP ${probe} — check ./onprem/run-onprem.sh logs" ;;
  esac

  echo ""
  if [[ "$all_ok" -eq 1 ]]; then
    section "Install complete"
    ok    "Open http://localhost:${ONPREM_PORT}/sso-config to add SSO providers."
    info  "Day-to-day management:"
    info  "  ./run-agent.sh         {build|start|stop|restart|refresh-certs|logs|status|clean}"
    info  "  ./onprem/run-onprem.sh {start|stop|logs|status}"
    info  "Full operator guide:    docs/operator-guide.md"
    warn  "Do not expose port ${ONPREM_PORT} to the internet — the Vaadin admin is unauthenticated by design."
  else
    warn "One or more containers failed to come up. Inspect logs and re-run after fixing."
    exit 1
  fi
}

# ── main ──────────────────────────────────────────────────────────────────────
check_prereqs
setup_env
pull_images
start_stack
verify
