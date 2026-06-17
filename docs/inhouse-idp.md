# In-house OIDC identity provider — operator guide

xpmile runs its **own** OIDC identity provider on a second Heroku app sharing the existing on-prem MongoDB. The v1.0 SSO module remains the OIDC/SAML *client*; this guide is about the *issuer* side.

For the *why* and the protocol-level design, see [`docs/design/inhouse-idp/inhouse-idp-design.md`](design/inhouse-idp/inhouse-idp-design.md). For the test-first delivery + commit-by-commit status, see [`docs/design/inhouse-idp/inhouse-idp-tdd-plan.md`](design/inhouse-idp/inhouse-idp-tdd-plan.md). For the file-by-file module reference, see [`modules/module/inhouseidp/README.md`](../modules/module/inhouseidp/README.md) + the `inhouseidp module` section of [`codebase.md`](../codebase.md).

---

## Architecture in one paragraph

The same `uniservice` binary deploys to two Heroku apps: **marvel** (the xpmile UI + API) and **idp** (the OIDC issuer). The posture is chosen at deploy time by `IDP_ISSUER` — when unset every `/api/v1/idp/*` and `/.well-known/openid-configuration` route returns 503 (marvel posture); when set, those routes activate. On-prem, one MongoDB instance holds two databases: `xpmile.*` (business state — shipments, accounts, etc.) and `idp.*` (auth state — sessions, codes, signing keys, clients, password reset tokens, account auth fields). Two `wsdbagent` containers bridge the two Heroku apps to the same MongoDB, sharing one InnerTLS CA + one client cert family. JWT signing happens **on-prem** via a new `SIGN_JWT` dbproto op — the RSA private key never leaves the on-prem NAT.

---

## Operator pre-conditions (one-time, post-merge)

The CI workflow auto-publishes the uniservice image to both Heroku registries (`registry.heroku.com/marvel/web` and `registry.heroku.com/idp/web`) on every push to `main` and runs `heroku container:release web` against both apps. The marvel app needs no operator action — the `IDP_ISSUER` env var stays unset and the IdP routes correctly return 503.

The **idp** app needs a one-time setup. Use the wrapper script — it sets the `container` stack, prompts for each config var (showing which ones are already set), scales `web=1`, and idemoptently re-running it is safe:

```sh
./run-idp.sh init        # one-time: stack:set container + config:set + ps:scale
./run-idp.sh release     # release the image already in registry.heroku.com/idp/web
./run-idp.sh verify      # curl /.well-known + /jwks to confirm
./run-idp.sh status      # show stack + dyno scale + config vars (passwords masked)
./run-idp.sh logs        # tail live Heroku logs
```

If you'd rather run the raw Heroku CLI commands yourself, here's what `init` would do:

```sh
heroku stack:set container --app idp           # one-time, hard-to-reverse
heroku config:set IDP_ISSUER=https://idp-63c97365e6ef.herokuapp.com --app idp
heroku config:set REMOTE_DB=1                  --app idp
heroku config:set SMTP_FROM_EMAIL=no-reply@xpmile.app --app idp
heroku config:set SMTP_FROM_PASSWORD='…'       --app idp
heroku config:set SMTP_FROM_NAME='xpmile'      --app idp     # optional
heroku ps:scale web=1                          --app idp
heroku container:release web                   --app idp
```

Notes:
- `IDP_ISSUER` is the marker that flips the dyno into IdP mode — without it the `/api/v1/idp/*` + `/.well-known/openid-configuration` routes return 503 (which is correct on marvel; wrong on idp).
- `REMOTE_DB=1` activates the `--remote-db` mode so the dyno reaches MongoDB via the on-prem `wsdbagent-idp` container instead of trying a local mongo (which doesn't exist on Heroku).
- `SMTP_FROM_*` are read on every `/password/reset_request` send. When unset, the endpoint still returns 200 (no enumeration) but `ACE_ERROR` lines appear in the logs and no email goes out.

On the **on-prem MongoDB machine**, after the next CI publish:

```sh
# 4) Run the account-split migration. Idempotent + gated by xpmile.schema_version
#    so it's safe to re-run.  Splits passwordHash out of xpmile.account.loginCredentials
#    into a flat idp.account doc; the marvel side keeps personalInfo.{role,name,email}
#    so existing views keep rendering.
./scripts/migrate-account-split.py \
    --mongo-uri 'mongodb://xpmile:xpmile_pass@localhost:27017/?authSource=admin'

# 5) Tell run-agent.sh to bring up the second wsdbagent. Without IDP_SERVER_HOST,
#    only the marvel-side agent starts (marvel-only deployments).
#    If run-agent is ALREADY up, re-running `start` is safe — it picks up the
#    new IDP_SERVER_HOST and adds wsdbagent-idp to the running stack.
echo 'IDP_SERVER_HOST=idp-63c97365e6ef.herokuapp.com' >> .env
./run-agent.sh start
# Confirm both wsdbagents are up + the cert-watcher:
./run-agent.sh status
# Expected names: agent-mongo, agent-wsdbagent, agent-wsdbagent-idp,
#                 xpmile-cert-watcher, [agent-onprem-ui if started].
```

**Launch the on-prem Vaadin admin** (opt-in — not started by `./run-agent.sh start`):

```sh
# From the repo root on the on-prem machine. First call builds the
# image (~3 min); subsequent calls reuse it. Idempotent.
./onprem/run-onprem.sh start
./onprem/run-onprem.sh status   # confirm agent-onprem-ui is "Up"
```

The admin is now reachable at **http://&lt;on-prem-host&gt;:8090** (port overridable via `ONPREM_PORT` in `.env`). No authentication — runs behind the customer's physical access controls. Side-nav has six items; the two that matter for the IdP are **IdP Signing Keys** and **IdP Clients**.

In the Vaadin admin:

#### 6. IdP Signing Keys → Generate the active RSA keypair

The cloud-side IdP can't mint id_tokens until there's at least one row in `idp.idp_signing_keys` with `active: true`. The view manages the lifecycle (generate / activate / set-notAfter / delete).

1. Side-nav → **IdP Signing Keys**.
2. Click **Generate new key** (top toolbar). A small dialog appears.
3. Leave the **Activate immediately** checkbox ✓ (so the new key replaces the previously-active one atomically — `IdpSigningKeyService.activate()` sets the target row to `active:true` first, then clears every other row's `active` flag, so there's never a moment with zero active keys).
4. Click **Generate**. A row appears with `kid=k-<8 hex>`, `alg=RS256`, `active=✓`, `created=now`, `notAfter=never`.

**Verify** the cloud picked it up:
```sh
curl -s https://idp-63c97365e6ef.herokuapp.com/api/v1/idp/jwks | jq
# Within ~60 s the `keys` array should contain one entry with your kid
# (was `{"keys":[]}` before). Empty after >60 s = the cloud reload
# poll hasn't fired yet (it runs every ~60 s on a dedicated thread —
# WebServer::reload_idp in webservice.cpp).
```

**Routine rotation** (later, not needed for this first setup): click **Generate new key** again with auto-activate ✓ — old key stays in JWKS so in-flight RP tokens still verify; new tokens are signed with the new key. Use **Set expiry…** on the old row to retire it (it drops out of JWKS once `now >= notAfter`).

#### 7. IdP Clients → Register the marvel SPA as an RP

Each relying party that wants to authenticate through this IdP needs a row in `idp.idp_clients`. The cloud-side `IdpClientRegistry` reads this collection on startup + every ~60 s and validates every `/authorize` request against it.

1. Side-nav → **IdP Clients**.
2. Click **Register client…** (top toolbar). A modal form opens.
3. Fill in the form:

| Field | Value (for marvel as the RP) | Notes |
|---|---|---|
| **clientId (_id)** | `xpmile-spa` | The natural key — appears in `/authorize?client_id=xpmile-spa`. Stored as the doc's `_id`, so renaming = delete + recreate. |
| **clientName** | `xpmile IdP (marvel SPA)` | Human label shown in the grid. Free-text. |
| **redirectUris** (one per line) | `https://marvel-3a78bd953f5f.herokuapp.com/api/v1/sso/callback/inhouse` | EXACT byte match against what marvel sends. Trailing slashes + case matter. Add one line per allowed URI; `/authorize` rejects any URI not in this list with `invalid_request: redirect_uri not registered for client`. |
| **postLogoutRedirectUris** (one per line) | `https://marvel-3a78bd953f5f.herokuapp.com/login` | Same EXACT-match rule. Used by `/end_session?post_logout_redirect_uri=…`. |
| **scopes** (one per line) | `openid`<br>`email`<br>`profile` | Pre-filled for new clients. `openid` is required by OIDC; the others gate email/name claims in the id_token. |
| **grantTypes** (one per line) | `authorization_code` | Pre-filled. The only grant the IdP supports in v1 — refresh tokens are out of scope. |

4. Click **Save**. The grid refreshes; `xpmile-spa` shows up with `#redirects=1`, `#scopes=3`.

#### 8. SSO Configuration → Add the in-house IdP as a provider on marvel

The other side of the wiring: marvel's `xpmile.sso_config` needs an entry that tells it to render a *"Sign in with xpmile IdP"* button + how to route `/api/v1/sso/login?provider=inhouse` to the right issuer URL.

1. Side-nav → **SSO Configuration**.
2. Set **Public base URL** to `https://marvel-3a78bd953f5f.herokuapp.com` (marvel's own URL — callback URLs are pinned to this origin). Skip if already filled in.
3. Click **Add provider**. A row is added; a form pops out.
4. Fill in:

| Field | Value | Notes |
|---|---|---|
| **id** | `inhouse` | URL path segment — marvel will accept `/api/v1/sso/callback/inhouse`. Must match the `id` used on the marvel-side callback URL you registered above. |
| **Display name** | `xpmile IdP` | Shown on marvel's login screen as the button label. |
| **Protocol** | `oidc` | (Not SAML.) The OIDC fields below become required. |
| **Issuer** | `https://idp-63c97365e6ef.herokuapp.com/api/v1/idp` | The path-bearing URL — must match `IDP_ISSUER` on the idp dyno + the `iss` claim minted into id_tokens. |
| **Client ID** | `xpmile-spa` | Must match the `clientId` you registered in step 7. |
| **Client secret** | *(leave blank)* | Not validated by the IdP's `/token` in v1 (PKCE covers RP auth). Write-only field — leaving blank keeps any stored value. |
| **Scopes** | `openid email profile` | Space-separated. The IdP advertises these in its discovery doc. |
| **Default role** | `Customer` | The role given to JIT-created marvel accounts (`resolve_account` when the IdP claims an email not yet linked). |
| **Allowed email domains** | *(leave blank)* | Empty = JIT-create regardless of email domain. Restrict later if needed. |

5. Click **Save configuration** (bottom-left). The doc is persisted to `xpmile.sso_config.providers`.

**Verify** marvel picked it up (after ~60 s):
```sh
curl -s 'https://marvel-3a78bd953f5f.herokuapp.com/api/v1/sso/providers' | jq
# Should now show:
#   { "providers": [ { "id": "inhouse", "displayName": "xpmile IdP", … } ] }
```

You can SKIP steps 7 and 8 entirely if you'd rather run `./scripts/seed-default-idp-sso.sh` — see the next section.

#### 9. Make sure there's an account in idp.account to log in WITH

The IdP authenticates against `idp.account` (separate from `xpmile.account`, by design). Three ways to get accounts into it:

| Source | How |
|---|---|
| **Migrate existing xpmile.account records** (recommended for the seed `admin` account) | `./scripts/migrate-account-split.py --mongo-uri '<your-uri>'`. Idempotent + gated by `xpmile.schema_version`. Copies `passwordHash` + `email/name/role` to `idp.account` and `$unset`s `loginCredentials.passwordHash` on the xpmile side. After this, marvel's legacy `POST /api/v1/account/login` keeps working via Phase K's cross-DB fallback. |
| **Create new accounts via Vaadin Accounts view, then re-run migration** | The current **Accounts** view writes to `xpmile.account` via marvel's `/api/v1/account` endpoint (which hashes with the same PBKDF2-SHA256). Re-running the migration moves the new auth fields to `idp.account`. The migration's idempotent so this is safe. |
| **Direct mongosh insert** (for testing only) | Use `MongodbClient::hash_password(plain)` from a one-shot — the IdP login pipeline uses `MongodbClient::verify_password()` which expects the `$pbkdf2-sha256$i=<n>$<salt>$<hash>` shape. Awkward by hand; not recommended outside dev. |

For first-time setup, run the migration:
```sh
./scripts/migrate-account-split.py \
    --mongo-uri 'mongodb://xpmile:xpmile_pass@localhost:27017/?authSource=admin'
# Expected: `inserted idp.account: accountCode=admin` (+ any other accounts).
```

After this, you can log in to the IdP at `https://<idp-host>/idp/login` with `admin` / `admin@123` (the seed credentials).

Both apps hot-reload these collections within ~60 s — no redeploy needed for routine config changes.

### Shortcut: seed the marvel ↔ IdP wiring in one script

Step 7 (IdP client registration) AND the marvel-side `sso_config` entry that adds the in-house IdP as a provider can both land via one mongosh invocation:

```sh
MARVEL_BASE_URL='https://marvel-3a78bd953f5f.herokuapp.com' \
IDP_ISSUER_URL='https://idp-63c97365e6ef.herokuapp.com/api/v1/idp' \
  ./scripts/seed-default-idp-sso.sh
```

The script idempotently upserts two things in one go:

- `xpmile.sso_config.providers` — adds a new OIDC entry with `id="inhouse"`, `displayName="xpmile IdP"`, the right issuer URL + scopes (`openid email profile`) + default JIT role (`Customer`). Marvel's login screen renders a *"Sign in with xpmile IdP"* button alongside the password form within ~60 s.
- `idp.idp_clients` — registers `clientId="xpmile-spa"` with `redirectUris=[<MARVEL_BASE_URL>/api/v1/sso/callback/inhouse]` (exact byte match) + the matching post-logout URI.

The signing key (step 6) still has to come from the Vaadin **IdP Signing Keys → Generate** view — `mongosh` can't easily produce a strong RSA-2048 keypair, so we leave that to Java's `KeyPairGenerator`.

Every other piece (provider id, client id, scopes, default role, etc.) is overridable via env vars — see the script's header for the full knob list.

---

## Verifying the deploy

Layered smoke test — fix any failure before moving to the next probe.

```sh
# ── IdP-only checks (no marvel involvement) ─────────────────────────────────

# 1) Discovery at the path-bearing URL (the one marvel actually fetches per
#    OIDC core §4 — the bare-root /.well-known also works for tooling).
curl -s https://idp-63c97365e6ef.herokuapp.com/api/v1/idp/.well-known/openid-configuration | jq '.issuer, .authorization_endpoint, .jwks_uri'
# → issuer + all endpoint URLs, every path under /api/v1/idp/*.
# If 501 "IdP route not yet wired" → release without PR #35.
# If 503 "wsdbagent not connected" → on-prem wsdbagent-idp isn't talking
#   to the idp dyno; check `podman logs agent-wsdbagent-idp`.

# 2) JWKS should list the active signing key (NOT empty).
curl -s https://idp-63c97365e6ef.herokuapp.com/api/v1/idp/jwks | jq '.keys[0] | {kid, alg, kty}'
# → { kid: "k-…", alg: "RS256", kty: "RSA" }
# If `{"keys":[]}` → Vaadin "Generate" never ran, or the cloud-side reload
#   poll hasn't fired yet (~60 s).

# 3) IdP login portal serves the Angular SPA at /idp/login (NOT the
#    marvel SPA — title should be "Sign in — xpmile", base href /idp/).
curl -s https://idp-63c97365e6ef.herokuapp.com/idp/login | grep -E '<title>|<base'

# ── Marvel ↔ IdP wiring ─────────────────────────────────────────────────────

# 4) Marvel knows about the inhouse provider (loaded from xpmile.sso_config).
curl -s https://marvel-3a78bd953f5f.herokuapp.com/api/v1/sso/providers | jq
# → contains { id: "inhouse", displayName: "xpmile IdP", protocol: "oidc" }.

# 5) The login dispatch redirects to the IdP's /authorize with full PKCE.
curl -sS -o /dev/null -w 'HTTP %{http_code}  → %{redirect_url}\n' \
     --max-redirs 0 \
     'https://marvel-3a78bd953f5f.herokuapp.com/api/v1/sso/login?provider=inhouse&return_to=/'
# → 302 with Location:
#     https://idp-…/api/v1/idp/authorize?client_id=xpmile-spa&code_challenge=…
#     &code_challenge_method=S256&nonce=…&redirect_uri=https%3A%2F%2Fmarvel-…%2Fapi%2Fv1%2Fsso%2Fcallback%2Finhouse
#     &response_type=code&scope=openid%20email%20profile&state=…
# If `HTTP 400 unknown provider` → marvel's reload_sso failed to fetch
#   discovery; bump the dyno (heroku restart --app marvel) then re-probe
#   after ~30 s. Look for "SSO config loaded — N OIDC provider(s) ready"
#   in `heroku logs --app marvel`.

# ── Browser end-to-end (only after 1–5 are green) ──────────────────────────
#
# Open https://marvel-3a78bd953f5f.herokuapp.com/ in a fresh window.
# Login screen renders "Sign in with xpmile IdP" alongside the password form.
# Click it → bounced to https://idp-…/idp/login (your branded portal).
# Enter admin / admin@123 → bounced back to marvel, /webui/main loads
# (xpmile_session cookie set).
```

End-to-end: have the marvel app's `sso_config` reference the in-house IdP as one provider (the on-prem `SsoConfigView` already supports adding OIDC providers — just point it at `https://idp-63c97365e6ef.herokuapp.com/api/v1/idp` as the issuer + reuse the `xpmile-spa` clientId + the same redirect_uri). Or use `./scripts/seed-default-idp-sso.sh` to do it in one mongosh call.

---

## What's deliberately deferred

Items the design's [§12 *Still open*](design/inhouse-idp/inhouse-idp-design.md#12-decisions-and-open-questions) flagged as not-needed-for-v1. None are required for the IdP to be functional:

1. **Brute-force backoff on `/login`** — `idp::login` does the credential check + session mint today. Per-account exponential backoff in a follow-up. (Q1)
2. **Refresh tokens** — sessions handle "stay logged in" via the RP-side `xpmile_session` cookie. (Q2)
3. **Key rotation scheduler** — manual via the Vaadin admin in v1, no auto-rotate. (Q3)
4. **`/userinfo` adoption** — the endpoint is wired (E phase) but the marvel SPA pulls claims from the id_token directly; `/userinfo` exists for protocol completeness. (Q4)
5. **Account lockout policy** — "no hard lockout, just backoff" + a Vaadin "reset failed attempts" button. (Q5)
6. **Log-out chaining** — when marvel `POST /api/v1/sso/logout` fires, also call the IdP's `/end_session` to match user expectation that "log out" = "log out". (Q6)
7. **Host-header gating** for IdP routes on the marvel host — accept the spec-rejected-but-not-exploitable mismatch in v1. (Q7)
8. **Cloud-side `kid` cache** — `/token` currently signs twice (placeholder kid → learn real kid → re-sign with real kid). A small in-process TTL cache of the active kid would drop the extra wsdbagent round trip. (Q8)
9. **`client_secret` validation in `/token`** — the field is accepted from the form but not yet verified against `idp.idp_clients.clientSecretHash`. Acceptable in v1 because PKCE authenticates the original `/authorize` requester.

---

## Database name conventions (today)

The two MongoDB databases this project uses are **fixed by the design** at `xpmile` (business) and `idp` (auth). The names appear as string literals across the stack:

- `docker/mongo-init.js` (seed + role grants)
- C++ `idp::*` service classes (`kDb = "idp"`)
- Java `IdpSigningKeyService` / `IdpClientService` (`IDP_DATABASE = "idp"`)
- `scripts/migrate-account-split.py` (`client["xpmile"]["account"]`, `client["idp"]["account"]`)
- The Phase K cross-DB read in `webservice.cpp` (`db.get_document("idp", "account", …)`)

The `MONGO_DB` env var on the wsdbagent containers only controls the agent's *default* db for wire requests whose `req.db` is empty — and after the dbproto cross-DB fix in PR #24 (commit `22a2bad`), almost every IdP-side call sets `req.db` explicitly anyway. So renaming `xpmile` → `xpmile_v2` would require touching the source files above too, not just env vars.

Making the db names env-driven (with the current literals as defaults) is a tracked follow-up — see the design's *Still open* section.

---

## Troubleshooting

### `/authorize` returns `invalid_client`
The `client_id` isn't in `idp.idp_clients`. Add it via the Vaadin **IdP Clients** view, then wait ≤ 60 s for the cloud-side hot-reload.

### `/authorize` returns `redirect_uri not registered for client`
Matching is **exact byte equality**. Trailing slashes and case-differences both cause this. Edit the client's `redirectUris` to add the exact URI the RP is sending.

### `/token` returns 500 `signing failed: no active key`
No row in `idp.idp_signing_keys` has `active:true`. Either run **Generate new key** with the auto-activate checkbox, or click **Activate** on an existing row.

### `/token` returns 503 `/token requires --remote-db mode`
The idp dyno isn't running with `--remote-db`. Set `REMOTE_DB=1` config var (above).

### Password-reset email never arrives
Check `heroku logs --app idp` for `ACE_ERROR …SMTP_FROM_EMAIL or SMTP_FROM_PASSWORD env var unset` or `…SMTP transaction to … failed`. Verify the three SMTP env vars are set + the SMTP server accepts the credentials.

### Legacy `POST /api/v1/account/login` returns 401 after the migration ran
The migration unsets `xpmile.account.loginCredentials.passwordHash` and puts the hash in `idp.account`. The marvel uniservice falls back to a cross-DB read for this case (Phase K, commit `22a2bad`). If you see 401 anyway, confirm:
- `migrate-account-split.py` actually created the `idp.account` doc — `db.idp.account.findOne({accountCode: '…'})` should return a doc with `passwordHash`.
- The marvel-side wsdbagent is on the post-Phase-K image (i.e. CI has run since merge).

### Marvel browser flow: `…/webui/login?error=callback_failed` after a successful IdP login
This catch-all comes from `sso_complete_callback` and hides every specific error from `OidcProvider::handle_callback` (token-exchange failure, malformed token response, missing id_token, JWKS-fetch failure, signature verification failure, iss/aud/exp/nonce mismatch). To find the actual cause:

1. Look at the IdP dyno logs — the response from `/token` is the smoking gun: `heroku logs --app idp --num 200 | grep -E '/token|sign|kid'`. A 500 with `signing failed:` means the wsdbagent's `sign_jwt_on_prem` rejected the request.
2. Look at the on-prem `agent-wsdbagent-idp` logs for the SIGN_JWT op (op=13): `podman logs agent-wsdbagent-idp | grep 'op=13'`. A line ending `ok=0` is a sign failure with the reason in the very next `errmsg` field.
3. If `/token` returned 200 but marvel still failed: the JWKS verification likely tripped. Inspect the id_token header (`echo "<id_token>" | cut -d. -f1 | base64 -d`) — its `kid` MUST match a key in `https://<idp>/api/v1/idp/jwks`.

Historically (PR #36) the render switch in `handle_idp` mapped any non-{302/400/401} into HTTP 200 with the JSON error body, so a 500 from `/token` reached marvel as a fake 200 and marvel logged the generic `callback_failed`. The switch now explicitly branches on 403/404/500/503. **Do not shrink that switch.** Any new 5xx the IdP can return needs an explicit case or the failure becomes invisible again.

### `agent-wsdbagent` / `agent-wsdbagent-idp` is running an outdated image after a Heroku release
`docker-compose.agent.yml` sets `pull_policy: always` on both wsdbagent services, but that only fires at the initial `podman-compose up`. The cert-watcher restart (which fires when `./run-agent.sh refresh-certs` rotates the cert family) uses `podman container restart` under the hood — **that does NOT re-pull the image**. So after a CI release that ships a new `xpmile-wsdbagent:latest`, the on-prem agents keep running the old binary forever.

Symptom: a bug fixed in the latest CI image (e.g. the kid-field fix in PR #36) still reproduces on-prem. `podman inspect agent-wsdbagent-idp --format '{{.Image}}'` shows an older digest than `podman pull docker.io/naushada/xpmile-wsdbagent:latest && podman image inspect …:latest --format '{{.Id}}'`.

Fix (run after every CI release that touches `Dockerfile.wsdbagent`):

```sh
podman pull docker.io/naushada/xpmile-wsdbagent:latest
./run-agent.sh stop && ./run-agent.sh start
```

`./run-agent.sh start` brings the containers up via `podman-compose up -d`, which honours `pull_policy: always` and picks up the freshly-pulled image. Without the `stop && start`, only `refresh-certs` runs and the binary stays stale.

### Signing-key reader sees `{"$oid":"…"}` instead of the kid string
Vaadin's `IdpSigningKeyService.generate()` writes the signing-key doc with `_id` as an auto-generated `new ObjectId()` and stores the domain `kid` in its OWN top-level field — NOT as the `_id`. Canonical-JSON serialisation of an ObjectId is `{"$oid":"…"}` (an object), so any code that does `row.value("_id", string{})` throws `json::type_error.302` ("type must be string, but is object") and either aborts the dyno or returns the default empty string and signs with kid=`""`. Both end with marvel failing to verify the id_token.

The cloud-side (`idp_jwks.cpp`) and on-prem side (`sign_jwt_on_prem.cpp`) both used to read `_id` (PRs #33 and #36 fixed them). Any future code that reads from `idp.idp_signing_keys` must:
- Project `{"_id":0, "kid":1, …}` (drop `_id`, include `kid`).
- Read the kid from the `kid` field, not from `_id`.

Same pattern applies to `IdpClientService` writes — but there `_id` IS the clientId (deliberate; the registry's natural key). Keep the distinction in mind when wiring new collections.

### IdP login for a NEW account (created via Vaadin) returns `invalid_credentials`
**Known limitation today.** The Vaadin **Accounts** view POSTs to marvel's `/api/v1/account` endpoint, which writes the new account to `xpmile.account` (nested `loginCredentials.passwordHash`) but **not** to `idp.account`. Symptoms:

- Legacy `POST /api/v1/account/login` ✅ works for the new account (the hash is in `xpmile.account.loginCredentials.passwordHash`; Phase K's fallback never fires).
- IdP `POST /api/v1/idp/login` ❌ returns `invalid_credentials` (no row in `idp.account`).

Workaround until the dual-write is implemented: after creating accounts via Vaadin, drop the migration's schema-version gate and re-run the migration to sync the new accounts into `idp.account`:

```sh
# Drop the gate doc so the migration re-walks every account
podman exec agent-mongo mongosh --quiet \
    'mongodb://root:changeme@localhost:27017/?authSource=admin' \
    --eval 'db.getSiblingDB("xpmile").schema_version.drop()'

# Re-run the migration — idempotent at the per-doc level, so already-migrated
# accounts are no-ops on the idp side (the find_one + insert-if-absent pattern).
./scripts/migrate-account-split.py \
    --mongo-uri 'mongodb://xpmile:xpmile_pass@localhost:27017/?authSource=admin'
```

Proper fix tracked: `handle_account_POST` (+ `handle_account_PUT` for password updates) should dual-write — business fields to `xpmile.account`, auth fields to `idp.account` — atomically. See the design's *Still open* §10.

---

## References

- **Design:** [`docs/design/inhouse-idp/inhouse-idp-design.md`](design/inhouse-idp/inhouse-idp-design.md)
- **TDD plan + delivery status:** [`docs/design/inhouse-idp/inhouse-idp-tdd-plan.md`](design/inhouse-idp/inhouse-idp-tdd-plan.md)
- **Module reference:** [`modules/module/inhouseidp/README.md`](../modules/module/inhouseidp/README.md), [`codebase.md`](../codebase.md) → *inhouseidp module*
- **Build/CI conventions:** [`CLAUDE.md`](../CLAUDE.md) → *In-house identity provider (IdP)*
- **wsdbagent rotation playbook (applies to both agents):** [`docs/ws-db-agent.md`](ws-db-agent.md)
