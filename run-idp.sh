#!/usr/bin/env bash
# run-idp.sh — manage the IdP Heroku app (idp.herokuapp.com)
#
# The IdP runs the SAME uniservice image as marvel (design Q1, resolved
# during implementation) — what makes a dyno "the IdP" is the IDP_ISSUER
# Heroku config var. CI publishes to registry.heroku.com/idp/web on
# every push to main; this script wraps the one-time init + the routine
# operator concerns that aren't in the CI loop.
#
# Usage:
#   ./run-idp.sh init                Set the container stack + prompt for any missing config vars
#   ./run-idp.sh release             Release the image already in registry.heroku.com/idp/web
#                                    (use after CI publishes when stack:set was the only blocker)
#   ./run-idp.sh logs                Tail live Heroku logs from the idp dyno
#   ./run-idp.sh status              Show stack, dyno scale, and the IdP-relevant config vars
#   ./run-idp.sh restart             Restart the idp dyno (no redeploy)
#   ./run-idp.sh open                Open https://idp-…herokuapp.com in your browser
#   ./run-idp.sh verify              curl /.well-known/openid-configuration + /api/v1/idp/jwks
#                                    to confirm the IdP is actually serving traffic
#   ./run-idp.sh help                Print this usage

set -euo pipefail

HEROKU_APP="${HEROKU_APP_IDP:-idp}"
# Default IdP issuer URL — matches docs/inhouse-idp.md + .env.agent.
DEFAULT_IDP_ISSUER="${IDP_ISSUER:-https://idp-63c97365e6ef.herokuapp.com}"

# ── colours ───────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'

info()    { echo -e "${CYAN}[info]${RESET}  $*"; }
ok()      { echo -e "${GREEN}[ok]${RESET}    $*"; }
warn()    { echo -e "${YELLOW}[warn]${RESET}  $*"; }
die()     { echo -e "${RED}[error]${RESET} $*" >&2; exit 1; }
section() { echo -e "\n${BOLD}── $* ──${RESET}"; }

# ── sanity ────────────────────────────────────────────────────────────────────
command -v heroku &>/dev/null || die "'heroku' CLI not found. Install with: brew install heroku/brew/heroku"

heroku_get_stack() {
  heroku stack --app "$HEROKU_APP" 2>/dev/null | grep -E '^\*' | awk '{print $2}'
}

heroku_get_config() {
  heroku config:get "$1" --app "$HEROKU_APP" 2>/dev/null
}

# Prompt the operator for a config var if it isn't set on the app yet.
# $1=var name, $2=description, $3=default (optional; "" = no default).
prompt_config() {
  local name="$1" desc="$2" default="${3:-}"
  local current
  current=$(heroku_get_config "$name")
  if [[ -n "$current" ]]; then
    info "$name already set (${#current} chars)."
    return 0
  fi
  echo
  warn "$name is unset on app=$HEROKU_APP."
  echo "  $desc"
  if [[ -n "$default" ]]; then
    echo -n "  value (Enter for default '$default'): "
  else
    echo -n "  value (Enter to skip): "
  fi
  read -r value
  if [[ -z "$value" && -n "$default" ]]; then
    value="$default"
  fi
  if [[ -n "$value" ]]; then
    heroku config:set "$name=$value" --app "$HEROKU_APP" >/dev/null
    ok "$name set."
  else
    warn "$name left unset — IdP will not be fully functional until you set it."
  fi
}

# ── commands ──────────────────────────────────────────────────────────────────

cmd_init() {
  section "Initialising Heroku app '$HEROKU_APP' for the in-house IdP"

  local current_stack
  current_stack=$(heroku_get_stack)
  if [[ "$current_stack" == "container" ]]; then
    ok "Stack is already 'container'."
  else
    warn "Current stack is '${current_stack:-unknown}', not 'container'."
    echo "  Switching to the container stack lets 'heroku container:release' work."
    echo "  THIS IS HARD TO REVERSE — Heroku won't accept buildpack deploys after."
    echo -n "  Type YES to switch (anything else aborts): "
    read -r confirm
    [[ "$confirm" == "YES" ]] || die "aborted."
    heroku stack:set container --app "$HEROKU_APP"
    ok "Stack set to 'container'."
  fi

  section "Config vars"
  prompt_config IDP_ISSUER \
    "Public issuer URL the IdP advertises in its discovery doc + signs into id_tokens." \
    "$DEFAULT_IDP_ISSUER"
  prompt_config REMOTE_DB \
    "Set to '1' so the dyno talks to MongoDB via the on-prem wsdbagent-idp container." \
    "1"
  prompt_config SMTP_FROM_EMAIL \
    "Sender address for password-reset emails (e.g. no-reply@xpmile.app)."
  prompt_config SMTP_FROM_PASSWORD \
    "SMTP auth password for SMTP_FROM_EMAIL."
  prompt_config SMTP_FROM_NAME \
    "Optional sender display name (e.g. 'xpmile')."

  section "Web dyno scale"
  local scale
  scale=$(heroku ps:scale --app "$HEROKU_APP" 2>/dev/null | grep -E '^web=' || true)
  if [[ -n "$scale" ]]; then
    ok "Already scaled: $scale"
  else
    info "No web dyno scaled yet. Scaling web=1 (default Eco/Basic — adjust later via 'heroku ps:scale')."
    heroku ps:scale web=1 --app "$HEROKU_APP"
  fi

  section "Done"
  info "Next: ./run-idp.sh release  (releases the image already in the Heroku registry)"
  info "Then: ./run-idp.sh verify   (curls /.well-known + /jwks to confirm)"
}

cmd_release() {
  section "Releasing image registry.heroku.com/$HEROKU_APP/web"
  local stack
  stack=$(heroku_get_stack)
  if [[ "$stack" != "container" ]]; then
    die "App is on stack='$stack', not 'container'. Run './run-idp.sh init' first."
  fi
  heroku container:release web --app "$HEROKU_APP"
  ok "Release triggered."
  info "Tail logs:   ./run-idp.sh logs"
  info "Verify:      ./run-idp.sh verify"
}

cmd_logs() {
  section "Live logs (Ctrl-C to exit)"
  heroku logs --tail --app "$HEROKU_APP"
}

cmd_status() {
  section "App status — $HEROKU_APP"
  echo
  heroku ps --app "$HEROKU_APP" || true
  echo
  echo "Stack:     $(heroku_get_stack)"
  echo "Idp issuer: $(heroku_get_config IDP_ISSUER || echo '(unset)')"
  echo "Remote DB: $(heroku_get_config REMOTE_DB  || echo '(unset)')"
  echo "SMTP from: $(heroku_get_config SMTP_FROM_EMAIL || echo '(unset)')"
  echo "SMTP pass: $(heroku_get_config SMTP_FROM_PASSWORD | sed 's/./*/g' || echo '(unset)')"
  echo "SMTP name: $(heroku_get_config SMTP_FROM_NAME || echo '(unset)')"
}

cmd_restart() {
  section "Restarting dyno"
  heroku restart --app "$HEROKU_APP"
  ok "Restart requested."
}

cmd_open() {
  heroku open --app "$HEROKU_APP"
}

cmd_verify() {
  section "Probing the IdP — $HEROKU_APP"
  local app_url
  app_url=$(heroku apps:info --json --app "$HEROKU_APP" 2>/dev/null \
              | python3 -c 'import sys,json; print(json.load(sys.stdin)["app"]["web_url"])' \
              | sed 's:/$::')
  [[ -n "$app_url" ]] || die "couldn't resolve the app's web URL."
  info "App URL: $app_url"

  section "GET /.well-known/openid-configuration"
  curl -fsS -o /tmp/idp-disco.json -w 'HTTP %{http_code}\n' \
       "$app_url/.well-known/openid-configuration" \
       && python3 -m json.tool /tmp/idp-disco.json | head -20 \
       || warn "discovery fetch failed — is IDP_ISSUER set? See: ./run-idp.sh status"

  section "GET /api/v1/idp/jwks"
  curl -fsS -o /tmp/idp-jwks.json -w 'HTTP %{http_code}\n' \
       "$app_url/api/v1/idp/jwks" \
       && python3 -m json.tool /tmp/idp-jwks.json | head -10 \
       || warn "JWKS fetch failed — is there an active signing key in idp.idp_signing_keys (Vaadin IdP Signing Keys)?"
}

cmd_help() {
  sed -n '2,18p' "$0"
}

cmd="${1:-help}"
case "$cmd" in
  init)     cmd_init ;;
  release)  cmd_release ;;
  logs)     cmd_logs ;;
  status)   cmd_status ;;
  restart)  cmd_restart ;;
  open)     cmd_open ;;
  verify)   cmd_verify ;;
  help|-h|--help) cmd_help ;;
  *) die "unknown command '$cmd'. Run './run-idp.sh help' for usage." ;;
esac
