#!/usr/bin/env bash
# seed-default-idp-sso.sh — register the in-house IdP as a default
# OIDC provider on the marvel app, and the marvel SPA as a registered
# RP on the in-house IdP. One mongosh round trip; idempotent.
#
# Use this after Phase H/I land but before you want federated login to
# actually work. It does the two things the operator would otherwise
# do manually in the on-prem Vaadin admin:
#
#   1) marvel side — adds an OIDC entry to `xpmile.sso_config`'s
#      providers array (id="inhouse"), pointing at the in-house IdP's
#      discovery URL. Marvel's login screen then renders a "Sign in
#      with xpmile IdP" button alongside the username/password form.
#
#   2) idp side — upserts the "xpmile-spa" client into
#      `idp.idp_clients`, with the marvel callback URL registered for
#      exact-byte redirect_uri match.
#
# What this script DOES NOT do:
#   - Generate an RSA signing key in idp.idp_signing_keys (use the
#     Vaadin "IdP Signing Keys → Generate" view; needs a CSPRNG that's
#     awkward from mongosh).
#   - Touch the marvel app's existing accounts or any business data.
#   - Set the Heroku-side IDP_ISSUER / REMOTE_DB / SMTP_* vars on the
#     idp app — that's `./run-idp.sh init`'s job.
#
# Usage:
#   MARVEL_BASE_URL='https://marvel-3a78bd953f5f.herokuapp.com' \
#   IDP_ISSUER_URL='https://idp-63c97365e6ef.herokuapp.com/api/v1/idp' \
#     ./scripts/seed-default-idp-sso.sh
#
# Optional:
#   MONGO_URI         (default: mongodb://localhost:27017)
#   MONGO_USER        (default: xpmile)
#   MONGO_PASS        (default: xpmile_pass)
#   MONGO_AUTH_SOURCE (default: admin)
#   IDP_DISPLAY_NAME  (default: "xpmile IdP")
#   IDP_PROVIDER_ID   (default: "inhouse"; URL path segment for the
#                      sso callback — marvel will accept
#                      /api/v1/sso/callback/$IDP_PROVIDER_ID)
#   IDP_CLIENT_ID     (default: "xpmile-spa"; the RP identifier
#                      registered in idp.idp_clients)
#   IDP_SCOPES        (default: "openid,email,profile" — comma-sep)
#   IDP_DEFAULT_ROLE  (default: "Customer" — role given to
#                      JIT-created accounts)
#
# Connect via the container if running on the on-prem machine:
#   podman exec -i agent-mongo bash -c '...'
# or just point MONGO_URI at the appropriate host:port.

set -euo pipefail

# ── colours ───────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'
info()    { echo -e "${CYAN}[info]${RESET}  $*"; }
ok()      { echo -e "${GREEN}[ok]${RESET}    $*"; }
warn()    { echo -e "${YELLOW}[warn]${RESET}  $*"; }
die()     { echo -e "${RED}[error]${RESET} $*" >&2; exit 1; }
section() { echo -e "\n${BOLD}── $* ──${RESET}"; }

# ── sanity ────────────────────────────────────────────────────────────────────
: "${MARVEL_BASE_URL:?MARVEL_BASE_URL is required, e.g. https://marvel-3a78bd953f5f.herokuapp.com}"
: "${IDP_ISSUER_URL:?IDP_ISSUER_URL  is required, e.g. https://idp-63c97365e6ef.herokuapp.com/api/v1/idp}"

# Strip trailing slashes — both will have /api/... or /sso/callback/...
# appended to them; double slashes confuse the callback-URL exact match.
MARVEL_BASE_URL="${MARVEL_BASE_URL%/}"
IDP_ISSUER_URL="${IDP_ISSUER_URL%/}"

MONGO_URI="${MONGO_URI:-mongodb://localhost:27017}"
MONGO_USER="${MONGO_USER:-xpmile}"
MONGO_PASS="${MONGO_PASS:-xpmile_pass}"
MONGO_AUTH_SOURCE="${MONGO_AUTH_SOURCE:-admin}"

IDP_DISPLAY_NAME="${IDP_DISPLAY_NAME:-xpmile IdP}"
IDP_PROVIDER_ID="${IDP_PROVIDER_ID:-inhouse}"
IDP_CLIENT_ID="${IDP_CLIENT_ID:-xpmile-spa}"
IDP_SCOPES_CSV="${IDP_SCOPES:-openid,email,profile}"
IDP_DEFAULT_ROLE="${IDP_DEFAULT_ROLE:-Customer}"

command -v mongosh &>/dev/null || die "'mongosh' not found. Install it (https://www.mongodb.com/docs/mongodb-shell/install/) or run this inside the agent-mongo container."

# ── compose the connection URI ────────────────────────────────────────────────
# Build the full URI with creds + auth source if MONGO_URI doesn't already
# carry them. The simple guard: only inject if URI has no '@'.
if [[ "$MONGO_URI" != *@* ]]; then
  # strip mongodb:// prefix, inject creds, prepend back.
  base="${MONGO_URI#mongodb://}"
  CONN="mongodb://${MONGO_USER}:${MONGO_PASS}@${base}/?authSource=${MONGO_AUTH_SOURCE}"
else
  CONN="$MONGO_USER"
  CONN="$MONGO_URI"
fi

MARVEL_CALLBACK_URL="${MARVEL_BASE_URL}/api/v1/sso/callback/${IDP_PROVIDER_ID}"
MARVEL_POSTLOGOUT_URL="${MARVEL_BASE_URL}/login"

# Pretty-print the plan before doing anything.
section "Plan"
echo "  MongoDB:                $MONGO_URI  (user=$MONGO_USER)"
echo "  Marvel base URL:        $MARVEL_BASE_URL"
echo "  IdP issuer URL:         $IDP_ISSUER_URL"
echo "  IdP provider id:        $IDP_PROVIDER_ID    (display: \"$IDP_DISPLAY_NAME\")"
echo "  IdP client id:          $IDP_CLIENT_ID"
echo "  Marvel callback URL:    $MARVEL_CALLBACK_URL"
echo "  Marvel postLogout URL:  $MARVEL_POSTLOGOUT_URL"
echo "  Scopes:                 $IDP_SCOPES_CSV"
echo "  Default role on JIT:    $IDP_DEFAULT_ROLE"
echo
echo -n "Type YES to proceed (anything else aborts): "
read -r confirm
[[ "$confirm" == "YES" ]] || die "aborted."

# Convert comma-separated scopes to a JS array literal: ["openid","email",…]
IFS=',' read -ra SCOPE_ARR <<<"$IDP_SCOPES_CSV"
SCOPES_JS="["
for s in "${SCOPE_ARR[@]}"; do
  s="$(echo "$s" | tr -d '[:space:]')"
  SCOPES_JS+="\"$s\","
done
SCOPES_JS="${SCOPES_JS%,}]"

# ── mongosh script ────────────────────────────────────────────────────────────
section "Writing config to MongoDB"

mongosh --quiet "$CONN" <<MONGOSH
// ─── 1) xpmile.sso_config — add/update the "inhouse" OIDC provider ────────
const xpDb         = db.getSiblingDB("xpmile");
const ssoConfigCol = xpDb.getCollection("sso_config");

// Provider entry shape matches what parse_sso_config + the Vaadin
// SsoConfigView write. Keys are camelCase per the on-disk JSON contract.
const inhouseProvider = {
  id:                  "${IDP_PROVIDER_ID}",
  displayName:         "${IDP_DISPLAY_NAME}",
  protocol:            "oidc",
  issuer:              "${IDP_ISSUER_URL}",
  clientId:            "${IDP_CLIENT_ID}",
  clientSecret:        "",                            // not validated in v1 (PKCE covers auth)
  scopes:              ${SCOPES_JS},
  defaultRole:         "${IDP_DEFAULT_ROLE}",
  allowedEmailDomains: [],                            // empty = no domain gate
  groupRoleMapEnabled: false,
  groupRoleMap:        {}
};

let existing = ssoConfigCol.findOne();
if (!existing) {
  ssoConfigCol.insertOne({
    publicBaseUrl: "${MARVEL_BASE_URL}",
    providers:     [inhouseProvider]
  });
  print("[ok] sso_config created with publicBaseUrl + the inhouse provider.");
} else {
  // Preserve existing providers + replace the inhouse entry by id.
  const others = (existing.providers || []).filter(p => p.id !== inhouseProvider.id);
  others.push(inhouseProvider);
  ssoConfigCol.updateOne(
    { _id: existing._id },
    { \$set: { publicBaseUrl: "${MARVEL_BASE_URL}", providers: others } }
  );
  print("[ok] sso_config updated — inhouse provider upserted (kept " + (others.length - 1) + " other provider(s)).");
}

// ─── 2) idp.idp_clients — register "xpmile-spa" with marvel's callback ───
const idpDb        = db.getSiblingDB("idp");
const clientsCol   = idpDb.getCollection("idp_clients");

const xpmileClient = {
  _id:                   "${IDP_CLIENT_ID}",
  clientName:            "${IDP_DISPLAY_NAME} (marvel SPA)",
  clientSecretHash:      "",                                    // not validated in v1
  redirectUris:          ["${MARVEL_CALLBACK_URL}"],            // EXACT byte match
  postLogoutRedirectUris:["${MARVEL_POSTLOGOUT_URL}"],
  scopes:                ${SCOPES_JS},
  grantTypes:            ["authorization_code"]
};

clientsCol.replaceOne(
  { _id: xpmileClient._id },
  xpmileClient,
  { upsert: true }
);
print("[ok] idp.idp_clients upserted — clientId=" + xpmileClient._id +
      " redirectUri=" + xpmileClient.redirectUris[0]);
MONGOSH

section "Done"
ok "marvel will pick up the new provider within ~60 s (sso_config hot-reload poll)."
ok "idp will pick up the new client within ~60 s (idp_clients hot-reload poll)."
echo
warn "Don't forget — for /token to actually mint id_tokens you still need:"
warn "  * Vaadin → IdP Signing Keys → Generate (mints the active RSA-2048 keypair)"
warn "  * On-prem two-agent stack running (IDP_SERVER_HOST set, ./run-agent.sh start)"
warn "  * Heroku 'idp' app initialised (./run-idp.sh init + release)"
