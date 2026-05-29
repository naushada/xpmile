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

## [v1.3.0] — 2026-05-29

The headline of this release is the **in-house Docker Hub puller**
(`xpmile-pull`) and the self-contained operator tarball it ships in —
a runc-only install path that needs nothing on the operator host
beyond `runc + jq + glibc 2.31+`. Plus a **commercial-invoice + A6
shipment-label** PDF refresh on the marvel UI, and a full
**country/state/city dropdown** rollout across every shipment form.

### Added

- **xpmile-pull** — in-house Docker Hub registry client / OCI image
  puller / runc-bundle assembler. Replaces the
  `skopeo + umoci + jq` prerequisite chain in the runc operator path
  with one ~3 MB binary. 141 GTest cases + 12 pytest, end-to-end TDD
  across eight phases (A → H), real-Hub verified against
  `docker.io/library/alpine:3.19` on amd64 and arm64. Design + TDD
  plan: `docs/design/runc-pull/`. (#64, #77–#83)
- **xpmile-runc-bundle tarballs** — per-arch operator tarballs published
  as GitHub Release assets:
  `xpmile-runc-bundle-v1.3.0-{amd64,arm64}.tar.gz` (~10 MB each).
  Contains `bin/xpmile-pull` (sh wrapper) + `bin/xpmile-pull.real` +
  `lib/*.so*` (libssl1.1, libcrypto1.1, libACE, libstdc++, libgcc_s,
  libz, bundled for portability across libssl1.1 ↔ libssl3 hosts) +
  `install.sh` + `install-agent-runc.sh` + per-bundle OCI `config.json`
  templates + systemd units. (#85)
- **One-curl operator install for the runc path**:
  `SERVER_HOST=marvel-XXXXXXXX.herokuapp.com curl -sSf https://raw.githubusercontent.com/naushada/xpmile/v1.3.0/install-agent-runc.sh | bash`
  — sibling to `install-agent.sh`. Detects host arch, resolves the
  latest release tag, downloads the matching tarball, runs the
  bundle's `install.sh`, which renders the OCI configs + installs the
  systemd units + brings the stack up. (#85)
- **Cascading country / state / city dropdowns** across every shipment
  form (single, bulk, modify, collect-shipment). Sender + receiver
  blocks are driven by a single reusable Angular component
  (`LocationSelectComponent`) backed by the `country-state-city` npm
  package (ISO 3166: 250 countries / ~5k states / ~150k cities, all
  client-side, no backend dependency). Storage convention is the
  human-readable name so legacy free-text data round-trips. Bulk
  shipments now default sender country from
  `account.personalInfo.country` and receiver state from a new
  `ReceiverState` Excel column. (#68, #69, #70)
- **`personalInfo.country` field** on `xpmile.account` — Vaadin
  CreateAccount + UpdateAccount views gain a country `ComboBox`
  (sourced from `Locale.getISOCountries()`); marvel UI reads the field
  to seed bulk-shipment sender country. (#68)
- **IATA airport-code lookup** baked into the A6 shipment label —
  origin + destination airports are looked up from a built-in IATA
  table and rendered next to the city name. Lookup is client-side; no
  backend round trip. (#74)
- **Shared `InvoiceService` + `LabelService`** — extracted the PDF
  layout logic out of the two screens that generate commercial
  invoices and the two that generate A6 labels, so the layout edits
  in this release propagate from one place. (#71, #72)
- **runc-pull CI release pipeline** — new `runc-bundle` matrix job in
  `.github/workflows/publish-images.yml` (amd64 + arm64). amd64 leg
  runs a smoke test against real Docker Hub on `ubuntu:22.04`
  (libssl3-only host) so the bundled-libs portability story is
  validated on every push. On tag pushes the tarballs are uploaded
  to the matching GitHub Release via
  `softprops/action-gh-release@v2`. (#85, #86, #87)

### Changed

- **Commercial invoice layout** — redesigned to operator-supplied
  reference layout: tighter form lines, removed the per-line overflow
  that was pushing the bottom totals onto a second A4 page, accepts
  shipments whose stored date is `dd/MM/yyyy` instead of `dd-MM-yyyy`.
  Single-A4-page guarantee. (#71, #73, #75, #76)
- **A6 shipment label layout** — redesigned to operator-supplied
  reference layout, shared service across both screens that render
  it, accepts `dd/MM/yyyy` stored dates, IATA airport code under each
  city. (#72, #73, #74, #75)
- **City field is a dropdown** in the shipment forms — was previously
  an autocomplete-on-type input whose suggestions appeared mid-edit
  and lost focus on selection. Now a Clarity `clrCombobox` that
  behaves like a real picker. (#70)
- **Angular initial-bundle budget raised to 30 MB** to accommodate the
  `country-state-city` package + the bundled IATA lookup table. The
  prod build's `NODE_OPTIONS=--max_old_space_size=1536` was already
  documented and remains unchanged. (#69)

### Fixed

- **xpmile-pull binary portability** — bootstrap base is Debian
  Bullseye (libssl1.1) but every realistic operator target ships
  libssl3 (Bookworm, Pi OS Bookworm, Ubuntu 22.04+). The runc-bundle
  assembler now `ldd`-snapshots every non-glibc shared library the
  binary needs, copies them into `lib/`, and ships a 3-line `sh`
  wrapper at `bin/xpmile-pull` that prepends `lib/` to
  `LD_LIBRARY_PATH` before exec'ing `xpmile-pull.real`. Excludes
  glibc-family libs (`libc`, `libpthread`, `libdl`, `libm`, etc.) so
  the operator host's NSS resolver isn't disturbed. Validated by the
  CI smoke on `ubuntu:22.04` (libssl3-only). (#86)
- **`bulk shipment` form blocked numeric AccountCodes** — operators
  whose accountCode happens to be a pure-numeric string had Excel
  parse it as a number, then the bulk handler tripped on
  `code.startsWith()`. The Excel-row constructor now coerces
  `AccountCode` to `String(…)` before storage. (#65)
- **`install-agent.sh` did not propagate `WSDBAGENT_IMAGE`** from env
  into the generated `.env`, so pinning a `sha-X` tag survived only
  until the first stack restart. (#58)

### Docs

- New: `docs/design/runc-pull/runc-pull-design.md` (architecture +
  resolved-question log), `docs/design/runc-pull/runc-pull-tdd-plan.md`
  (the eight-phase TDD blueprint that this release executes against),
  `docs/design/runc-pull/USAGE.md` (operator-facing usage guide). (#64,
  #84)
- Updated: `README.md` gains the runc-bundle one-curl install snippet
  + a runc-pull-design pointer; `modules/module/runcpull/templates/README.md`
  documents the wrapper + `lib/` portability pattern so operators can
  debug `ldd` against `xpmile-pull.real` if needed (#84, #86); `CLAUDE.md`
  gains a runc-pull architecture note alongside the existing
  ws-db-agent + sso + inhouseidp notes (#84).
- ws-db-agent docs refreshed: bake-certs-into-wsdbagent pattern, real
  Heroku hostname examples, persistence + re-install safety + host
  paths, re-install downtime budget. (#59, #60, #61, #63)

### Notes for operators

- The runc-bundle tarballs are **GitHub Release assets only** —
  they aren't pushed to Docker Hub. Pin via
  `XPMILE_RELEASE=v1.3.0` (or omit, default is `latest`).
- The runc install path is still positioned as the
  *memory-constrained* option (Pi 3B class, no daemon, no compose).
  Most operators should stick with `install-agent.sh`. See
  `docs/operator-runc.md` for the trade-off.
- Image-tag scheme from v1.1.0 unchanged: pin to `:v1.3.0` for
  reproducible deploys.

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
