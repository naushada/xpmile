# Changelog

All notable changes to xpmile are documented here. Format roughly follows
[Keep a Changelog](https://keepachangelog.com/); the project uses
[semver](https://semver.org/) for tag names (`vMAJOR.MINOR.PATCH`).

Tag conventions:
- **`vX.Y.Z`** — immutable point release; CI publishes images with the
  matching `:vX.Y.Z` tag.
- **`release/vX.Y`** branch — rolling within a minor release line; CI
  publishes images with the matching `:vX.Y` tag on every push to the
  branch.
- **`main`** — continuous deploy; CI publishes `:latest` + `:<sha>`.

---

## [v1.1.0] — 2026-05-27

A focused release on the **operator install path** for the on-prem
agent stack (Pi-3B-class hosts in particular) + **profile-level UX
polish** on the marvel app.

### Added

- **One-file installer**: `curl -sSf https://raw.githubusercontent.com/naushada/xpmile/main/install-agent.sh | bash` brings up the full 4-container agent stack (`agent-mongo`, `agent-wsdbagent`, optional `agent-wsdbagent-idp`, `xpmile-cert-watcher`) from pre-built Docker Hub images. Auto-detects Pi 3B (Cortex-A53 / armv8.0-A) and pins `MONGO_TAG=4.4.18`. (#43)
- **Reboot survival**: install-agent.sh writes a systemd-user unit at `~/.config/systemd/user/xpmile-agent.service` and auto-enables linger via a 3-tier fallback chain (`sudo -n` → `SUDO_PASS` env → interactive sudo when stdin is a tty). After `sudo reboot`, the stack auto-comes-back via `start-stack.sh`. (#50)
- **Cert family baked into wsdbagent**: CI now bakes the matching InnerTLS client cert family directly into the `xpmile-wsdbagent` image at build time (multi-stage `FROM uniservice:<sha>`). `refresh-certs` is a ~10 MB extract from an image the operator already needs, instead of a ~500 MB pull of the entire `uniservice` cloud binary just to scrape three cert files. (#44, #45)
- **Auto-pulling cert-watcher**: the `xpmile-cert-watcher` sidecar now polls Docker Hub every `IMAGE_POLL_SECONDS` (default 900 s = 15 min) for a new wsdbagent image. On a digest change it extracts the baked cert family and atomically republishes it into the host certs dir — the md5sum loop then restarts the agents. Hands-off rotation. Opt out with `AUTO_PULL=0`. (#46)
- **CI-published `xpmile-mongo` image**: `mongo:7` (+ `mongo:latest` alias) and `mongo:4.4.18` (Cortex-A53 / armv8.0-A target) — both multi-arch, with `mongo-init.js` baked in. Removes Dockerfile.mongo + mongo-init.js from the operator's filesystem surface entirely. (#43)
- **runc-only operator path**: `docs/operator-runc.md` documents the lightest install option for hosts where memory pressure matters (no engine daemon, no compose, no cert-watcher sidecar). systemd-units + an OCI bundle per container. Most operators should stick with the install-agent.sh path; runc is for the 1 GB Pi crowd. (#40)
- **Pi 3B install guide**: `docs/operator-pi3b.md` with shape A/B/C deployment options, RAM + disk budgets, swap setup, the Pi-3B-traps troubleshooting table, and the Vaadin-on-Mac alternative for offloading Vaadin from the constrained host. (#38, #47)
- **Profile photo support**: optional `personalInfo.photoBase64` field on each `xpmile.account` doc — base64 data URL, client-side resized to ≤256×256 + JPEG @ 0.85 → ~30-50 KB per photo. Three upload entry-points: Create Account, Update Account, and the new My Profile modal in the navbar dropdown. The marvel navbar renders the photo when present, falls back to `<clr-icon shape="user">` otherwise. (#49, #51)
- **About modal**: navbar dropdown's previously-dead `About` link is now a small Clarity modal showing version, marvel host, GitHub source, docs link. (#49, #51)
- **My Profile self-service modal**: navbar dropdown gains a "My Profile" entry that opens an inline editor for display name + profile photo, with save reflected immediately in the navbar without a page reload. (#51)
- **Account-schema documentation**: `codebase.md` gains a table of which collection / field each account attribute lives in (`xpmile.account` business fields vs `idp.account` auth fields post-migration), including the rationale for inline-base64 photo storage vs GridFS. (#55)

### Changed

- **cert-watcher engine auto-detect**: the sidecar's startup now probes `/v4.0.0/libpod/_ping` (libpod) then `/_ping` (docker) and picks the matching REST URL templates for restart / image-pull / archive-extract. The same compose file now works on rootful podman, rootless podman, and dockerd. (#39)
- **install-agent.sh brings up services serially**: switched from a single `podman-compose up -d <4 services>` to a per-service `up -d` loop via the auto-written `start-stack.sh` wrapper, to dodge a podman-compose 1.3 multi-service hang observed on the Pi 3B. (#52, #53, #54)
- **Rootless-aware socket detection**: install-agent.sh now uses `${XDG_RUNTIME_DIR}/podman/podman.sock` when `EUID != 0` (rootless), rather than the rootful `/run/podman/podman.sock`. Removes a silent cert-watcher failure on the standard Pi 3B install. (#54)
- **Shallow-clone recommendation**: `git clone --depth 1` (~10 MB) is now the default in operator docs; sparse-checkout of the 5 runtime files (~50 KB) is documented as the truly-minimum variant for operators on small SD cards. (#41)

### Fixed

- **Four Pi 3B compat bugs** surfaced by the first live install on a Debian 13 Trixie Pi 3B (Cortex-A53):
  - `mongo:4.4` floating tag SIGILL crash → pin to `mongo:4.4.18` (last build before armv8.2-A floor)
  - Trixie's empty `unqualified-search-registries` rejects bare `FROM mongo:…` → fully-qualify every Dockerfile base image as `docker.io/library/...`
  - Healthcheck uses `mongosh` (absent in mongo:4.4) → portable `command -v mongosh || mongo` detection
  - `mongo-init.js` used ES6 (`const`, template literals) → rewrite to ES5 so the legacy mongo:4.4 shell parses it (#42)
- **wsdbagent Dockerfile ARG scoping** after the #44 multi-stage refactor — hoist `BUILDER_IMAGE` + `UNISERVICE_REF` ARGs above any FROM so BuildKit's per-stage scoping doesn't make BUILDER_IMAGE undefined for the second FROM. (#45)
- **Session restore on browser refresh** in the marvel UI — after login, refreshing the page used to leave the navbar's name + photo blank because the cookie-backed session wasn't being re-hydrated client-side. Implemented `MainComponent.ngOnInit` → `getSession() → getCustomerInfo(accountCode) → loggedInUser = acct`. (#48, #56)
- **The bug PR #48 introduced**: the session-restore chain originally called `getAccountInfo(accountCode)` — which despite the name is actually the LOGIN endpoint (`POST /api/v1/account/login`). With only `userId` and no password the cloud returned 400 `"Missing userId or password"`, the silent error handler swallowed it, and the navbar stayed blank. Replaced with `getCustomerInfo()` (the session-authenticated GET) + a `console.warn` in the error handler so the next time something similar regresses it shows up in DevTools instead of disappearing. (#56)

### Docs

- New: `docs/operator-pi3b.md`, `docs/operator-runc.md`, `CHANGELOG.md`.
- Updated: `codebase.md` (account schema + photoBase64 storage rationale; InnerTLS cert family source post-#44), `CLAUDE.md` (same), `README.md` (one-curl install one-liner), `docs/operator-pi3b.md` (Vaadin-on-Mac, reboot survival, Pi-3B traps).

### Image-tag scheme (NEW)

CI now publishes images with version-aware tags in addition to `:latest` + `:<sha>`:

| Trigger | New image tags |
|---|---|
| Push to `main` | `:latest`, `:<sha>`              (unchanged) |
| Push to `release/vX.Y` | `:vX.Y`                        (rolling release-line) |
| Tag push `vX.Y.Z` | `:vX.Y.Z`                          (immutable) |

Operators should pin to a specific tag (e.g. `:v1.1.0`) for reproducible deploys.

---

## [v1.0.0] — initial release

- SSO module (OIDC + SAML 2.0), in-house OIDC IdP, password-reset flow.
- on-prem `wsdbagent` over mutually-authenticated TLS to the cloud uniservice.
- Vaadin admin UI for IdP key/client management.
- Pi-class hosts supported (rough; the v1.1.0 release line tightens the install UX).

See git log on `release/v1.0` for the full PR-by-PR detail.
