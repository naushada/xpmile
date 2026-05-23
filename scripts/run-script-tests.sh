#!/usr/bin/env bash
# scripts/run-script-tests.sh — build (if missing) + run the Python script
# tests inside a container.
#
# Usage:
#   ./scripts/run-script-tests.sh                # full run
#   ./scripts/run-script-tests.sh -k migration   # pytest filter
#   ./scripts/run-script-tests.sh --rebuild      # force rebuild the test image
#
# All deps live in the xpmile-scripts-test:local image (built from
# scripts/Dockerfile.test). Nothing is installed on the host.

set -euo pipefail

# ── colours ───────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'
info()    { echo -e "${CYAN}[info]${RESET}  $*"; }
ok()      { echo -e "${GREEN}[ok]${RESET}    $*"; }
warn()    { echo -e "${YELLOW}[warn]${RESET}  $*"; }
die()     { echo -e "${RED}[error]${RESET} $*" >&2; exit 1; }
section() { echo -e "\n${BOLD}── $* ──${RESET}"; }

# Resolve repo root regardless of where the script is invoked from.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

IMAGE_TAG="${SCRIPTS_TEST_IMAGE:-xpmile-scripts-test:local}"

# ── parse args ────────────────────────────────────────────────────────────────
rebuild=0
pytest_args=()
for arg in "$@"; do
  case "$arg" in
    --rebuild) rebuild=1 ;;
    *)         pytest_args+=("$arg") ;;
  esac
done

command -v podman &>/dev/null || die "podman not found"

# ── build (if missing or --rebuild) ───────────────────────────────────────────
if [[ $rebuild -eq 1 ]] || ! podman image exists "$IMAGE_TAG" 2>/dev/null; then
  section "Building ${IMAGE_TAG}"
  # The image only bakes the Python runtime + an entrypoint; deps are
  # installed at run time and cached in a podman volume (see Dockerfile
  # header for why build-time install is unreliable on macOS podman).
  podman build -q -f scripts/Dockerfile.test -t "$IMAGE_TAG" scripts/
  ok "Built ${IMAGE_TAG}"
fi

# ── run ───────────────────────────────────────────────────────────────────────
section "Running script tests"

# Bind-mount scripts/ read-only so iterating on test files doesn't require a
# rebuild. Mount a named volume for the pip cache so the deps install is fast
# after the first run.
if [[ ${#pytest_args[@]} -eq 0 ]]; then
  exec podman run --rm \
    -v "${REPO_ROOT}/scripts:/work:ro" \
    -v xpmile-pip-cache:/root/.cache/pip \
    "$IMAGE_TAG"
else
  exec podman run --rm \
    -v "${REPO_ROOT}/scripts:/work:ro" \
    -v xpmile-pip-cache:/root/.cache/pip \
    "$IMAGE_TAG" \
    -v --color=yes test_migrate_account_split.py "${pytest_args[@]}"
fi
