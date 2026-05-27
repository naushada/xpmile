# Operator guide — running xpmile on-prem

This guide is for operators deploying xpmile **v1.0** in production: installing the on-prem agent stack from published Docker Hub images, running the Vaadin admin UI, and keeping the link to the cloud uniservice healthy.

If you're modifying the C++ or Angular code, start with [`README.md`](../README.md) and [`CLAUDE.md`](../CLAUDE.md) instead — this document is task-oriented for operators, not contributors.

---

## 1. Architecture

xpmile is split across two locations:

- **Cloud** — the Heroku app `marvel` (or whatever name your deployment uses) runs the C++ HTTP backend (`uniservice`) plus the Angular SPA. Everything browser-facing lives here, including the SSO handshake.
- **On-prem** (your machine, typically behind NAT) — MongoDB, `wsdbagent` (proxies DB calls from the cloud), the cert-watcher sidecar, and the Vaadin admin UI for operators. **All customer data stays on-prem.**

The two are linked by a single outbound WebSocket from `wsdbagent` to the cloud uniservice. An inner-TLS session runs inside that WebSocket with mutual cert auth — see [`ws-db-agent.md`](ws-db-agent.md) for the full design.

```
                                              browsers
                                                  │ HTTPS
                                                  ▼
                                  ┌────────────────────────────┐
                                  │  marvel.herokuapp.com      │
                                  │  uniservice + Angular SPA  │
                                  └──────────┬─────────────────┘
                                             │  /ws/db  (WebSocket + inner-mTLS)
              ── outbound, behind NAT ───────┘
              │
              ▼
   ┌──────────────────────┐      ┌────────────────────────┐
   │  agent-wsdbagent     │ ◄──► │  agent-mongo (MongoDB) │
   └──────────┬───────────┘      └────────────────────────┘
              │                              ▲
   ┌──────────┴───────────┐                  │  same agent-net
   │  xpmile-cert-watcher │                  │
   │   (restarts wsdbagent│         ┌────────┴─────────┐
   │    on cert change)   │         │  agent-onprem-ui │  ── operator at :8090
   └──────────────────────┘         │  Vaadin admin    │
                                    └──────────────────┘
```

The on-prem operator has two scripted entry points:

| Script | What it manages |
|---|---|
| `./run-agent.sh` | The agent core — `agent-mongo`, `agent-wsdbagent`, `xpmile-cert-watcher` |
| `./onprem/run-onprem.sh` | The Vaadin admin — `agent-onprem-ui` (opt-in; not started by `run-agent.sh start`) |

---

## 2. Prerequisites

- **podman 4.0+ and `podman-compose`** (or Docker — commands are interchangeable). On macOS: `brew install podman podman-compose && podman machine init && podman machine start`.
- **~15 GB free disk** under `~/.local/share/containers` (podman) or `/var/lib/docker` (Docker). The Vaadin image build pulls a Maven JDK + JRE runtime; the uniservice image carries the toolchain runtime libs.
- **Outbound HTTPS** to your cloud uniservice host (e.g. `marvel-3a78bd953f5f.herokuapp.com:443`). No inbound port is required on the on-prem side — the WebSocket is dialed outbound.
- **A clone of this repo** for the scripts and compose files. The binaries are pulled from Docker Hub; you don't need a build toolchain installed.

---

## 3. Quick start — install from Docker Hub

Two installer paths today. Pick by your host class.

### 3a. Shape-A only (mongo + agents): one-file `install-agent.sh` (Recommended for v1.1)

For most operators — the path the v1.1 release optimises for. No repo clone needed; the installer embeds the compose + init scripts. Auto-detects the Pi 3B and pins `mongo:4.4.18`. Writes a systemd-user unit + tries to enable linger for reboot survival in one shot.

```sh
SERVER_HOST=marvel-<hash>.herokuapp.com \
  IDP_SERVER_HOST=idp-<hash>.herokuapp.com \
  SUDO_PASS='your-sudo-password' \
  curl -sSf https://raw.githubusercontent.com/naushada/xpmile/main/install-agent.sh | bash
```

Full walkthrough: [`docs/operator-pi3b.md`](./operator-pi3b.md). For the ultra-light variant skipping the container engine entirely (`runc` + systemd directly), see [`docs/operator-runc.md`](./operator-runc.md).

### 3b. Shape-B (with Vaadin): the original repo-clone installer

If you need the Vaadin admin UI bundled with the agent install (registering OIDC clients, generating IdP signing keys), use the original `install.sh` from the release branch. Pulls multi-arch images, prompts for env, brings up the full stack including `agent-onprem-ui`.

**One-liner over `curl` (the script clones the rest of the repo itself):**

```sh
curl -fsSL https://raw.githubusercontent.com/naushada/xpmile/release/v1.1/install.sh | bash
```

It clones `xpmile` (branch `release/v1.1`) into `./xpmile/` and re-execs itself from inside. Override the target dir with `XPMILE_DIR=/opt/xpmile curl ... | bash`, or override the ref to pin to a specific patch tag with `XPMILE_REF=v1.1.0`. The installer is idempotent.

**Explicit clone (preferred if you want to inspect or pin to a tag):**

```sh
git clone --branch release/v1.1 --depth 1 https://github.com/naushada/xpmile.git
cd xpmile
./install.sh                # ~5 min on amd64; ~20–25 min first run on a Pi
```

For the previous release line (`v1.0.x` — pre-Pi-3B install path, pre-profile-photo, etc.), substitute `release/v1.0` and `XPMILE_REF=v1.0.x`.

> **Trust note for the curl pipe.** `curl | bash` runs whatever bytes the URL serves. Read the script once if you're not sure, and pin to an immutable tag (`XPMILE_REF=v1.1.0`) rather than the rolling branch once a patch tag is available. If your organisation forbids the pattern entirely, use the explicit-clone form — the install behaviour is identical.

If you'd rather do it by hand (e.g. inside an Ansible role), the underlying lifecycle scripts work directly:

```sh
cp .env.agent .env
$EDITOR .env                       # set SERVER_HOST + XPMILE_BACKEND_BASE_URL + DB passwords

./run-agent.sh start               # mongo + wsdbagent + cert-watcher
./onprem/run-onprem.sh start       # Vaadin admin on http://localhost:8090
```

Either way, open `http://localhost:8090/sso-config` in a browser after it's up and add your first SSO provider. The cloud login page picks it up within ~60 s (the C++ backend hot-reloads `sso_config`).

### Required `.env` entries

The template `.env.agent` carries the full list; the ones that *must* be set or reviewed:

```sh
# Cloud uniservice — both of these point at the same Heroku app
SERVER_HOST=marvel-3a78bd953f5f.herokuapp.com
XPMILE_BACKEND_BASE_URL=https://marvel-3a78bd953f5f.herokuapp.com

# MongoDB credentials — defaults are fine for a single-tenant install,
# but change them before exposing the deployment to anything beyond your
# trusted network
MONGO_APP_USER=xpmile
MONGO_APP_PASS=xpmile_pass
MONGO_ROOT_PASS=changeme
MONGO_DB=xpmile
```

> **Why two URL variables?** `SERVER_HOST` is what `wsdbagent` dials for the WebSocket. `XPMILE_BACKEND_BASE_URL` is what the Vaadin admin's Spring Boot app uses for its REST calls to the cloud backend. They almost always reference the same hostname, but the wsdbagent path is hostname-only (no scheme) while Spring needs the full URL.

---

## 4. What gets pulled from Docker Hub

| Image | When | Source |
|---|---|---|
| `docker.io/naushada/xpmile-wsdbagent:latest` | `run-agent.sh start` | Published by CI on every push to `main` — multi-arch (amd64 + arm64). Pin with `WSDBAGENT_IMAGE=...:<sha>`. |
| `docker.io/library/alpine:3.19` | `run-agent.sh start` | The cert-watcher sidecar. |
| `docker.io/library/mongo:7` | `run-agent.sh start` (first run only) | Pulled, then layered with `docker/Dockerfile.mongo` (~5 s build on top) to bake in the seed script. |
| `docker.io/naushada/xpmile-onprem:latest` | `onprem/run-onprem.sh start` (first run only) | **Pulled when published; otherwise built locally** from `onprem/Dockerfile` (Maven + JRE, ~3–5 min on amd64, ~20 min on a Pi). `install.sh` does the pull/build automatically. |

The uniservice itself isn't installed on-prem — it runs only in the cloud. `run-agent.sh refresh-certs` pulls it temporarily to extract certs, then discards it.

To pin a specific build of `wsdbagent`:
```sh
WSDBAGENT_IMAGE=docker.io/naushada/xpmile-wsdbagent:abc1234 ./run-agent.sh start
```

---

## 5. The agent stack — `./run-agent.sh`

```
./run-agent.sh build            # build xpmile-mongo locally (one-time, ~30 s)
./run-agent.sh start            # mongo + wsdbagent + cert-watcher; auto-refresh certs if missing
./run-agent.sh stop             # stop all three; data volume preserved
./run-agent.sh restart          # stop + start
./run-agent.sh refresh-certs    # pull the latest uniservice image; extract rotated client certs
./run-agent.sh logs             # follow all three
./run-agent.sh status           # container state table
./run-agent.sh clean            # stop + delete the mongo-data volume (destructive)
```

The three services:

- **`agent-mongo`** — MongoDB 7 with auth enabled. Persists in volume `mongo-data`. Health-checked via `db.adminCommand('ping')`.
- **`agent-wsdbagent`** — opens an outbound WebSocket to `<SERVER_HOST>/ws/db`, presents the InnerTLS client cert, runs the inner-mTLS handshake, then proxies all DB ops from the cloud uniservice to `agent-mongo`. Pulled fresh from Docker Hub on every `start`.
- **`xpmile-cert-watcher`** — alpine sidecar. md5sums `certs/cloud-issued/innertls/` every 5 s and POSTs `/libpod/containers/agent-wsdbagent/restart` via the host podman socket on any change. Bind-mounts `/run/podman/podman.sock`.

`./run-agent.sh start` enumerates exactly these three services. The agent compose file also defines `onprem-ui` (the Vaadin admin), but that's opt-in — you start it separately with `./onprem/run-onprem.sh`.

### First-time start

`run-agent.sh start` does two things automatically the first time:

1. If `certs/cloud-issued/innertls/` is missing or empty, it invokes `refresh-certs` first — without these certs `wsdbagent` would crash-loop with a TLS load failure.
2. If `xpmile-mongo:latest` doesn't exist, it builds it from `docker/Dockerfile.mongo`.

---

## 6. The Vaadin admin — `./onprem/run-onprem.sh`

```
./onprem/run-onprem.sh start    # build image (first run) + start agent-onprem-ui on port 8090
./onprem/run-onprem.sh stop     # stop and remove the container
./onprem/run-onprem.sh logs     # follow logs
./onprem/run-onprem.sh status   # container state
```

`agent-onprem-ui` is a Spring Boot + Vaadin 24 web app. It joins `agent-net` (same network as `agent-mongo`/`wsdbagent`) and:

- Talks **directly to MongoDB** for the SSO config view — it writes `sso_config` to the co-located DB.
- Talks **to the cloud uniservice** (via `XPMILE_BACKEND_BASE_URL`) for the dashboard, shipments, and accounts views.

Routes:

| Path | Purpose |
|---|---|
| `/` | Dashboard — monthly shipment counts |
| `/shipments` | Shipment list view |
| `/accounts` | Account list view |
| `/sso-config` | **SSO provider configuration** — the one that matters for this guide |

### Security — read this before exposing the port

The Vaadin admin is **unauthenticated by design** (see [`sso-design.md`](design/sso/sso-design.md) §10). It sits behind the customer's physical / network access controls — the same trust boundary that already allows direct edits to the on-prem `account` collection.

**Hard requirement: do not expose port 8090 to the internet.** Anything on the same LAN can edit `sso_config`. If your operator workstation can't reach the on-prem machine over a trusted network directly, use SSH port-forwarding rather than opening the port:

```sh
ssh -L 8090:localhost:8090 operator@on-prem-host
# then browse http://localhost:8090/ on your workstation
```

---

## 7. Configuring SSO providers

1. Open `http://localhost:8090/sso-config`.
2. Click **Add Provider**.
3. Fill in the fields below.
4. **Save.** Spring writes the document to the `sso_config` collection.
5. The cloud uniservice re-reads `sso_config` every ~60 s on a background thread. A document that fails to parse is rejected and the previous good config is kept, so a bad edit can never take down login.
6. Refresh the cloud login page (after up to ~60 s). The **"or continue with"** section appears with one button per provider.

### OIDC fields

| Field | Notes |
|---|---|
| `id` | Short identifier; becomes the URL path segment (`/api/v1/sso/login?provider=<id>`). |
| `displayName` | Shown on the login button. |
| `protocol` | `oidc` |
| `issuer` | The IdP's discovery issuer URL — e.g. `https://acme.okta.com/oauth2/default`. The backend fetches `<issuer>/.well-known/openid-configuration` from here. |
| `clientId` | Registered confidential web-client id at the IdP. |
| `clientSecret` | Registered client secret. **Write-only** — the form never displays it back. Leave blank on edit to keep the stored value. |
| `scopes` | At minimum `["openid","email","profile"]`. Add `"groups"` if you intend to use `groupRoleMap`. |
| `defaultRole` | Role assigned to JIT-created accounts (defaults to least privilege, e.g. `Customer`). |
| `allowedEmailDomains` | Email-match guard, e.g. `["acme.com"]`. Required to prevent a misconfigured IdP from claiming accounts outside its domain. |
| `groupRoleMap` | Optional `{idp-group: xpmile-role}` map. **Opt-in per provider** — only enable for IdPs you fully trust (see §11 of the SSO design). |

### SAML fields

| Field | Notes |
|---|---|
| `id`, `displayName` | As above. |
| `protocol` | `saml` |
| `idpEntityId` | The IdP's entity id (a stable identifier, often a URL). |
| `idpSsoUrl` | The IdP's HTTP-Redirect SSO endpoint. |
| `idpSigningCert` | The IdP's signing certificate, PEM-encoded. The XML-DSig check trusts only this cert — embedded `<KeyInfo>` is ignored. |
| `spEntityId` | Your SP entity id — a stable string identifier (does **not** have to be a URL). |
| `defaultRole`, `allowedEmailDomains` | As above. |

> SAML doesn't support `groupRoleMap` for v1 because the partner-IdP case typically can't be fully trusted.

### Register the callback URL with each IdP

For each provider, register the matching callback URL at the IdP:

| Protocol | URL to register |
|---|---|
| OIDC | `<cloud-host>/api/v1/sso/callback/<id>` |
| SAML (ACS) | `<cloud-host>/api/v1/sso/callback/<id>` (HTTP-POST binding) |

`<cloud-host>` is `publicBaseUrl` in the SSO config — **always pinned to the configured value**, never derived from request headers (a known SSO security best practice; see §10 of the SSO design).

---

## 8. Cert rotation playbook

Every uniservice build mints a fresh InnerTLS CA (per `docker/Dockerfile`). So **every deploy of `marvel`** — CI auto-deploy on push to `main` or a manual `deploy-heroku.sh` — invalidates the cert pair your `wsdbagent` is presenting.

When this happens you'll see:

- All `/api/v1/*` requests to marvel return `503 {"cause":"wsdbagent not connected"}`.
- The Vaadin admin's **Agent** and **Database** badges turn red within ~30 s.
- `./run-agent.sh logs` shows `wsdbagent` crash-looping with `tls_process_client_certificate verify failed`.

Recovery — pick one of:

**Manual, after each deploy:**
```sh
./run-agent.sh refresh-certs
```
This pulls the latest `xpmile-uniservice:latest` image from Docker Hub, extracts the rotated client cert family into `certs/cloud-issued/innertls/`, and exits. The `xpmile-cert-watcher` sidecar detects the file change within 5 s and POSTs a restart to `agent-wsdbagent`.

End-to-end refresh-to-reconnect: ~15 s (5 s detect + 5–10 s restart + 1–2 s handshake).

**Scheduled (cron / systemd timer)** — if you don't want to manually refresh after each deploy:
```cron
*/15 * * * * cd /opt/xpmile && ./run-agent.sh refresh-certs >> /var/log/xpmile-refresh.log 2>&1
```
15 minutes is a reasonable bound between a CI deploy completing and certs being stale; tune to your deploy cadence.

See [`ws-db-agent.md`](ws-db-agent.md) for the full rotation design — why the CA mints per-build, the watcher's restart mechanism, and the failure modes it guards against.

---

## 9. Troubleshooting

### `503 {"cause":"wsdbagent not connected"}` on every `/api/v1/*`

The cloud uniservice can't reach your `wsdbagent`. Almost always a stale cert pair after a deploy.

1. `./run-agent.sh status` — all three containers should be `Up`.
2. `./run-agent.sh logs` (or `podman logs agent-wsdbagent`) — look for `tls_process_client_certificate verify failed` or repeated reconnect attempts.
3. `./run-agent.sh refresh-certs`.

### Vaadin navbar **Agent** + **Database** badges red

The Vaadin admin probes the cloud uniservice every 30 s. Any 2xx or 4xx → green; timeout or 5xx → both red. Both red almost always means the 503 above. The badges are intentionally *combined* — they don't distinguish "agent disconnected" from "agent connected but DB down" (both surface as 503).

### Vaadin dashboard shows zeros / `Accounts` view throws an error page

The dashboard fetches data from the cloud backend. If `XPMILE_BACKEND_BASE_URL` resolves to a malformed or unreachable host, every backend-dependent view throws `UnknownHostException` at Vaadin bean instantiation and the route fails. Check the live env in the container:

```sh
podman exec agent-onprem-ui printenv XPMILE_BACKEND_BASE_URL
```

Expected: `https://<your-cloud-host>` — **no trailing characters, no stray punctuation.**

### SSO buttons missing on the cloud login page

`curl https://<cloud-host>/api/v1/sso/providers` distinguishes:

- `503 {"cause":"wsdbagent not connected"...}` → see the 503 troubleshooting above.
- `200 []` → providers list is empty. Add one at `http://localhost:8090/sso-config`; the cloud backend hot-reloads within ~60 s.

The Angular login component renders the **"or continue with"** section only when the providers list is non-empty.

### A specific SSO provider was added but its button doesn't appear

- Wait up to ~60 s — the cloud backend's hot-reload polls on that interval.
- Check the saved JSON in MongoDB:
  ```sh
  podman exec agent-mongo mongosh -u "$MONGO_APP_USER" -p "$MONGO_APP_PASS" --authenticationDatabase admin --eval 'db.getSiblingDB("xpmile").sso_config.findOne()'
  ```
  If the field shapes look wrong, the cloud backend logs `parse_sso_config` errors and keeps the previous (working) registry — which is why the button doesn't change.
- Verify the callback URL is registered at the IdP and matches the provider `id`.

### `xpmile-onprem` image build fails with disk space / Maven download errors

The first `onprem/run-onprem.sh start` does a Maven build. It needs ~2 GB of free disk and outbound access to `repo.maven.apache.org`. After the first run the image is cached; subsequent starts skip the build entirely.

---

## 10. Backups

The `mongo-data` volume holds everything — shipments, accounts, sessions, SSO config, GridFS. Standard `mongodump` / `mongorestore` works:

```sh
# Dump
podman exec agent-mongo mongodump \
  --uri "mongodb://${MONGO_APP_USER}:${MONGO_APP_PASS}@localhost:27017/xpmile?authSource=admin" \
  -o /tmp/dump
podman cp agent-mongo:/tmp/dump ./mongo-backup-$(date +%F)

# Restore
podman cp ./mongo-backup-2026-05-23 agent-mongo:/tmp/dump
podman exec agent-mongo mongorestore \
  --uri "mongodb://${MONGO_APP_USER}:${MONGO_APP_PASS}@localhost:27017/xpmile?authSource=admin" \
  /tmp/dump
```

The `sessions` collection has a TTL index, so old sessions self-expire — a stale `mongodump` is safe to restore. SSO config (`sso_config`) is a single document; back it up explicitly if you have an elaborate provider setup before doing risky edits.

---

## 11. Upgrading

CI publishes new images to Docker Hub on every push to `main`. To pull a newer set:

```sh
./run-agent.sh stop
./run-agent.sh start            # pulls fresh wsdbagent (pull_policy: always)
./run-agent.sh refresh-certs    # in case the new uniservice rotated the CA

./onprem/run-onprem.sh stop
podman rmi xpmile-onprem:latest # force rebuild of the Vaadin image from the new repo state
./onprem/run-onprem.sh start
```

For point-in-time pinning, set `WSDBAGENT_IMAGE=docker.io/naushada/xpmile-wsdbagent:<sha>` and check out the matching repo commit before starting.

---

## 12. Pointers

| Doc | Audience |
|---|---|
| [`ws-db-agent.md`](ws-db-agent.md) | Deep design of the WebSocket DB tunnel, inner-TLS, and cert rotation |
| [`design/sso/sso-design.md`](design/sso/sso-design.md) | SSO architecture (BFF, sessions, OIDC, SAML, hybrid provisioning) |
| [`design/sso/sso-tdd-plan.md`](design/sso/sso-tdd-plan.md) | SSO test plan (development reference) |
| [`app.md`](app.md) | Heroku-side configuration of the cloud app |
| [`../README.md`](../README.md) | Project overview and developer setup |
| [`../CLAUDE.md`](../CLAUDE.md) | Project instructions (development / Claude Code sessions) |
| [`../codebase.md`](../codebase.md) | Full codebase reference |
