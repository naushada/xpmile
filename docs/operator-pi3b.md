# Operator guide — fresh install on a Raspberry Pi 3B

Pi-3B-specific install of the on-prem agent stack. The Pi 3B is **tight on memory** (1 GB) and **constrained on CPU** (Cortex-A53, armv8.0-A); the rest of this doc walks the choices each constraint forces.

For Pi 4 / Pi 5 / amd64 boxes, use the generic instructions in [`docs/inhouse-idp.md`](inhouse-idp.md) — none of the Pi-3B-only knobs below are needed there.

---

## TL;DR — single-command install (shape A)

If you just want a working agent install on a Pi 3B (or any Linux host) with podman or docker already present, run:

```sh
curl -sSf https://raw.githubusercontent.com/naushada/xpmile/main/install-agent.sh | bash
```

The script:
1. **Detects** your container engine (`podman-compose` or `docker compose`) + your CPU architecture. On a Pi 3B (Cortex-A53 / armv8.0-A) it auto-pins `MONGO_TAG=4.4.18` — the last mongo build that boots on pre-armv8.2-A arm64.
2. **Prompts** for `SERVER_HOST` (marvel Heroku) and optionally `IDP_SERVER_HOST` (idp Heroku).
3. **Writes** `~/xpmile-agent/{docker-compose.agent.yml, .env}` — both embedded in the script, no repo clone needed.
4. **Pulls** `xpmile-mongo`, `xpmile-wsdbagent`, `alpine` from Docker Hub. The mongo image is pre-built per-arch (no local Dockerfile build on your host).
5. **Extracts** the rotated InnerTLS cert family from `xpmile-wsdbagent`'s `/opt/wsdbagent/baked-certs/` directory — same image we're already pulling for the agent, so this is a ~10 MB no-op marginal cost. (CI bakes the matching client cert family in at build time — see PR #44.)
6. **Brings up** all 4 containers and **verifies** the wsdbagent connects to marvel.

After install: log in to `https://<your-marvel-host>/login` with `admin / admin@123` (change it immediately via the marvel UI).

**Reboot survival** — the install script already writes a systemd-user unit (`~/.config/systemd/user/xpmile-agent.service`) and `systemctl --user enable`s it. The only piece it CAN'T do for you is enable linger (needs `sudo`):

```sh
sudo loginctl enable-linger $(whoami)
```

Without linger, user systemd dies when your ssh session ends, and the boot-time unit never fires. With linger + the auto-installed unit, after `sudo reboot` the stack comes back on its own. See §10 for what the unit does + the full rootless-podman lifecycle story. Disable boot-start any time with `systemctl --user disable xpmile-agent`.

If you'd rather inspect first:
```sh
curl -sSO https://raw.githubusercontent.com/naushada/xpmile/main/install-agent.sh
less install-agent.sh   # ~430 lines — every step is commented
bash install-agent.sh
```

The rest of this guide is the **long-form** install — useful if you want to hack on the agent code, override defaults the script doesn't expose, or pick path B (docker) / path C (runc) instead of A (podman).

---

## What you're installing

Three landings, depending on how much you want the Pi to do:

| Shape | Containers | Approx. RAM at idle | Use when |
|---|---|---|---|
| **A — minimal (mongo + both agents)** | `agent-mongo`, `agent-wsdbagent`, `agent-wsdbagent-idp`, `xpmile-cert-watcher` | ~450 MB | You want the bridge to the marvel + idp Heroku apps but admin-via-mongosh is fine. Best fit for a 1 GB Pi. |
| **B — add Vaadin admin** | shape A + `agent-onprem-ui` | ~950 MB | Day-to-day admin via web UI. **Very tight on a Pi 3B.** Workable if no other apps share the box; falls over if you also run a desktop or another service. |
| **C — agents only (mongo elsewhere)** | `agent-wsdbagent`, `agent-wsdbagent-idp`, `xpmile-cert-watcher` | ~100 MB | The mongo lives on a different host (a NAS, another Pi, etc.); this Pi is just the protocol bridge. Lightest. |

Shape **A** is what most Pi 3B operators want. Section §6 covers when to add Vaadin (shape B); §7 covers the agents-only variant (shape C).

---

## Memory + disk requirements

### Per-container budget (RSS — what shows up in `podman stats`)

| Container | Idle RSS | Under load (steady) | Build / first-pull spike | Notes |
|---|---:|---:|---:|---|
| `agent-mongo` (mongo:4.4.18) | ~280 MB | ~350 MB | n/a (image is pulled, not built) | mongo's WiredTiger cache defaults to ½(RAM − 1 GB) — on a 1 GB Pi the floor (~256 MB) applies. |
| `agent-wsdbagent` | ~30 MB | ~50 MB | ~50 MB during compose-up | Per agent — there are two of these on a marvel+idp install. |
| `agent-wsdbagent-idp` | ~30 MB | ~50 MB | ~50 MB | Same image, second container. |
| `xpmile-cert-watcher` | ~5 MB | ~5 MB | ~5 MB | Alpine + a polling shell loop. |
| `agent-onprem-ui` (Vaadin) | ~480 MB | ~520 MB | **~900 MB peak** during `mvn package` build | The JVM heap is unbounded by default; first-build OOM is the usual Pi-3B trap. |

### Total RAM by install shape

| Shape | At idle | Under load | Build spike (first install) |
|---|---:|---:|---:|
| A — mongo + both agents | ~450 MB | ~570 MB | ~620 MB |
| B — shape A + Vaadin | ~930 MB | ~1.05 GB | **~1.5 GB** ⚠ needs swap |
| C — agents only | ~70 MB | ~110 MB | ~110 MB |

The Pi 3B has **1 GB total RAM** with ~870 MB user-available after the kernel + GPU split. So:

| Shape | Verdict on a 1 GB Pi 3B (headless, 870 MB user RAM) |
|---|---|
| A | ✅ comfortable. ~420 MB free at idle for OS + ssh + tooling. |
| B | ⚠ works but **only headless** (no desktop). 2 GB swap is mandatory during the Vaadin first-build; can drop swap to 512 MB after first build if memory becomes the constraint. Don't run other services. |
| C | ✅ trivial. ~760 MB free at idle. |

### Swap

| Shape | Recommended swap |
|---|---|
| A | 512 MB (the dphys-swapfile default is fine) |
| B | **2 GB during install** (Vaadin first-build hits ~1.5 GB peak); drop to 1 GB after first build |
| C | 512 MB |

Section §1 below has the swap-resize commands.

### Disk

| What | Size | Notes |
|---|---:|---|
| OS (Raspberry Pi OS Lite 64-bit) | ~3 GB | A4 / U3 SD card recommended. |
| Container images (xpmile-mongo + wsdbagent + cert-watcher + uniservice-for-certs) | ~2.5 GB | uniservice pulled once for cert extraction. |
| `agent-mongo` data volume | grows with use | 1 GB seed; budget 2 GB / year of typical use. |
| Vaadin image (shape B) | ~600 MB | onprem-ui jar + JRE + Maven cache. |
| Build cache + log spillover | ~1 GB | `podman system prune -a` reclaims when full. |
| **Recommended minimum SD card** | **16 GB** | 32 GB if you ever expect to build the Vaadin image more than once. |

A **USB SSD instead of an SD card** is strongly recommended for production — mongo's WAL hammers the wear levelling on cheap SDs. The Pi 3B boots from USB out-of-the-box on recent firmware.

### CPU

All four shape-A containers idle at <2% CPU on the Pi 3B's 4 × 1.2 GHz A53 cores. The hot path is mongo writes (during login bursts) and the wsdbagent TLS handshake on reconnect — both are well within budget. Vaadin's first-page render after a cold start spikes one core to ~70% for ~3 s while it lazy-loads the Spring Boot context.

---

## 1. Hardware + OS prep

You need:

- Raspberry Pi 3B (or 3B+) with at least **1 GB RAM** (the 3B has exactly that — no headroom).
- **16 GB+ SD card** (or USB SSD — recommended; the mongo data dir gets WAL-heavy).
- **64-bit Raspberry Pi OS** — Bookworm aarch64. 32-bit Raspbian won't pull arm64 container images. Verify with `uname -m` → expect `aarch64`.
- A wired Ethernet connection ideally (TLS handshakes over flaky Wi-Fi → cert-rotation thrashes).
- Outbound TCP 443 reachable (to talk to both Heroku apps).

### Configure swap

The Pi 3B's 1 GB RAM is right at the line for the full stack. mongo's WAL + the Java/Maven build of Vaadin can both spike past available memory. Add 2 GB of swap:

```sh
sudo dphys-swapfile swapoff
sudo sed -i 's/^CONF_SWAPSIZE=.*/CONF_SWAPSIZE=2048/' /etc/dphys-swapfile
sudo dphys-swapfile setup
sudo dphys-swapfile swapon
swapon --show       # should list /var/swap at 2G
```

Swap is **not** a substitute for RAM at steady state — if you see `kswapd0` constantly at 100% CPU, drop Vaadin (shape A) or move mongo off the Pi (shape C). Swap covers spikes during build + first-image-pull only.

---

## 2. Install the container engine

Three supported runtimes on the Pi. **Pick one** — don't install more than one side-by-side.

### Path A — podman (project default; rootless, no daemon)

```sh
sudo apt-get update
sudo apt-get install -y podman podman-compose git
podman --version            # ≥ 4.0
podman-compose --version    # ≥ 1.0.6
```

Pi 3B's stock Bookworm ships podman 4.x.

### Path B — Docker (rootful, system daemon — what you'll use if you've already got Docker Desktop / `dockerd` muscle memory)

```sh
sudo apt-get update
sudo apt-get install -y docker.io docker-compose-v2 git
sudo usermod -aG docker $(whoami) && newgrp docker     # so you can run docker without sudo
docker --version            # ≥ 24.x
docker compose version      # ≥ 2.x  (note: `docker compose`, no hyphen)
```

**Three differences vs podman** that matter for the rest of this guide:

1. `./run-agent.sh` wraps `podman-compose`. On Docker, either edit the script (`COMPOSE_CMD="docker compose"`) or run the equivalent commands directly:
   - `docker compose -f docker-compose.agent.yml up -d mongodb wsdbagent wsdbagent-idp`
   - `docker compose -f docker-compose.agent.yml down`
   - `docker compose -f docker-compose.agent.yml logs -f`
2. **Reboots are auto-handled.** Docker's `dockerd` is a system service that starts at boot (`systemctl enable --now docker`), and the compose file's `restart: unless-stopped` policy survives the host reboot natively. **You can ignore §10's `@reboot` cron / systemd-user dance entirely.**
3. **`xpmile-cert-watcher` auto-detects the engine.** The sidecar's startup probes the bind-mounted socket — libpod's `/v4.0.0/libpod/_ping` first, docker's `/_ping` second — and picks the matching restart URL template (`/v1.41/containers/.../restart?t=5` for Docker). Override the host-side socket path in `.env` if yours isn't the default:
   ```sh
   PODMAN_SOCKET=/var/run/docker.sock
   ```
   (The env var stays named `PODMAN_SOCKET` for backward compatibility — it's really "whatever-engine socket the watcher should mount". The watcher logs `detected engine: docker` on startup so you can confirm.)

The rest of this guide says `podman` in commands; replace with `docker` (and `podman-compose` with `docker compose`) where applicable. The `Dockerfile.mongo` build + image semantics are identical.

### Path C — runc only (lightest; no daemon, no compose, no sidecar)

For operators who want to skip a full container engine and drive things
directly through the OCI runtime + systemd. Saves ~100–200 MB of RAM
vs path A/B on a Pi 3B and removes the cert-watcher sidecar entirely
(`systemd.path` watches the certs dir instead).

The trade-off is operator complexity: no `docker ps`, no
`./run-agent.sh`, hand-edited `config.json` files per container,
image updates are manual `xpmile-pull --force` calls (the in-house
puller shipped in v1.3.0 — see [`operator-runc.md`](./operator-runc.md)).
Worth it when memory pressure is the binding constraint; not worth it
on a Pi 4 with 4 GB RAM. Most operators should pick A or B.

Full walkthrough — install, image pipeline, per-container `config.json`,
systemd units, cert-rotation `path` unit, update procedure,
troubleshooting — lives in [`operator-runc.md`](./operator-runc.md).
The rest of this guide assumes path A or B; if you go C, jump there
now and ignore §§3–10 here.

---

## 3. Clone the repo

The on-prem stack only needs the agent compose + scripts + the on-prem Vaadin source (if you go with shape B). On a Pi 3B SD card a **shallow clone** is the right default — no git history, ~10 MB on disk vs ~150 MB+ for a full clone, `git pull` still works for updates:

```sh
cd ~
git clone --depth 1 https://github.com/naushada/xpmile.git
cd xpmile
```

You're **not** going to compile any C++ on the Pi — the wsdbagent image is pulled multi-arch from Docker Hub. The repo is mostly here for the compose file + the operator scripts.

### What `run-agent.sh` actually reads

If you're disk-constrained even at 10 MB (Pi Zero 2 W on 4 GB card) and want to know the absolute minimum, only these five files are touched at runtime:

```
run-agent.sh                       # the wrapper
docker-compose.agent.yml           # services + volumes + env wiring
.env.agent                         # template — copied to .env on first run
docker/Dockerfile.mongo            # built into xpmile-mongo:latest at first start
docker/mongo-init.js               # COPY'd into that image; creates xpmile + idp app users
```

The full `modules/`, `ui/`, `test/`, `onprem/`, `docs/` trees are unused at runtime. A sparse-checkout pulls the agent slice in ~50 KB:

```sh
git clone --depth 1 --filter=blob:none --sparse https://github.com/naushada/xpmile.git
cd xpmile
git sparse-checkout set run-agent.sh docker-compose.agent.yml .env.agent docker/Dockerfile.mongo docker/mongo-init.js
```

Future updates need `git pull && git sparse-checkout reapply`. For most operators the plain `--depth 1` above is the better trade.

---

## 4. Configure `.env`

```sh
cp .env.agent .env
```

Edit `.env` with `nano`/`vim`. The two settings you MUST change from the template:

| Variable | Value | Why |
|---|---|---|
| `SERVER_HOST` | `marvel-3a78bd953f5f.herokuapp.com` (or your marvel app's hostname) | The marvel-side wsdbagent connects here. |
| `IDP_SERVER_HOST` | `idp-63c97365e6ef.herokuapp.com` (or your idp app's hostname) | The idp-side wsdbagent connects here. Without this set, `wsdbagent-idp` doesn't start (= shape C without IdP). |

**Pi-3B-only setting:**

```sh
# Append to .env. Pins mongo to 4.4.18 so it actually starts on the Pi 3B's
# armv8.0-A cores. mongo:5+ requires armv8.2-A — AND mongo:4.4.x where
# x ≥ 19 also tightened to armv8.2-A. 4.4.18 is the last release that
# boots on Cortex-A53. Don't use the bare `4.4` floating tag — it now
# resolves to a post-4.4.19 build that SIGILL-crashes on the Pi.
MONGO_VERSION=4.4.18
```

Optional: change `MONGO_ROOT_PASS` + `MONGO_APP_PASS` to something less guessable than the defaults (`changeme` / `xpmile_pass`). The on-prem mongo isn't internet-exposed, but defense in depth.

---

## 5. Bring up shape A — mongo + both agents

```sh
./run-agent.sh start
```

This:
1. **Builds** `xpmile-mongo:latest` from `docker/Dockerfile.mongo` (`mongo:4.4.18` base because of `MONGO_VERSION=4.4.18`). First build ~3–5 min on a Pi 3B; subsequent runs reuse the cache.
2. **Pulls** `docker.io/naushada/xpmile-wsdbagent:latest` (multi-arch; the arm64 manifest fits the Pi). ~2 min on first pull.
3. **Auto-runs** `./run-agent.sh refresh-certs` if `./certs/cloud-issued/innertls/` is missing — that itself pulls `xpmile-uniservice:latest` (~400 MB; ~3 min on a Pi).
4. **Starts** `agent-mongo`, `agent-wsdbagent`, `agent-wsdbagent-idp` (because `IDP_SERVER_HOST` is set), `xpmile-cert-watcher`.

Verify:

```sh
./run-agent.sh status
# Expected:
#   agent-mongo          Up X seconds (healthy)
#   agent-wsdbagent      Up X seconds
#   agent-wsdbagent-idp  Up X seconds
#   xpmile-cert-watcher  Up X seconds

podman logs --tail 5 agent-wsdbagent-idp
# Expected: "inner TLS established" + "session started"
```

### Pi-3B traps (and how to recognise them in the logs)

Three compat issues hit us during the first live install on the Pi 3B (May 2026); the code + docs now ship correct defaults, but if you bring up an older revision you'll see one of:

| Log signature | What it is | Fix |
|---|---|---|
| `podman logs agent-mongo` repeats `Illegal instruction (core dumped)` + `MongoDB requires ARMv8.2-A or higher` | `MONGO_VERSION` is unset or set to the floating `4.4` tag (now resolves to 4.4.19+ which requires armv8.2-A) | `MONGO_VERSION=4.4.18` in `.env`, then `./run-agent.sh clean && ./run-agent.sh start` |
| `STEP 1/2: FROM mongo:4.4.18` → `short-name "mongo" did not resolve to an alias and no unqualified-search registries are defined` | Older `Dockerfile.mongo` has bare `FROM mongo:…`; Debian 13 Trixie ships podman with empty `unqualified-search-registries` | Pull latest `main` (`Dockerfile.mongo` is fully-qualified `docker.io/library/mongo:…`), OR add `unqualified-search-registries = ["docker.io"]` to `/etc/containers/registries.conf.d/00-xpmile.conf` |
| `agent-mongo` shows `Up X (unhealthy)` despite mongo serving requests fine, AND `./run-agent.sh start` hangs forever in its healthcheck wait loop | Healthcheck uses `mongosh` (only in mongo:5+), absent in 4.4 | Pull latest `main` (compose healthcheck now portable: `command -v mongosh \|\| mongo`). For interim, `kill` the stuck script and `podman-compose up -d` directly |
| `podman logs agent-mongo` shows `@/docker-entrypoint-initdb.d/mongo-init.js:4:7 failed to load` | `mongo-init.js` used ES6 (`const`, template literals); mongo:4.4's legacy shell only does ES5 | Pull latest `main` (rewritten to ES5; works on both 4.4 and 5+ shells) |

The "image platform (linux/amd64) does not match the expected platform (linux/arm64)" warning logged twice during `./run-agent.sh refresh-certs` on arm64 hosts is **benign** — the uniservice image is amd64-only by design (Heroku is amd64), but `refresh-certs` only `podman create` + `podman cp` cert files out, never executes the binary, so the arch mismatch doesn't matter.

**At this point the Pi can serve as the bridge for federated login.** A user clicking *Sign in with xpmile IdP* on marvel walks → idp Heroku app → wsdbagent-idp → mongo on the Pi → back through.

You still need (one-time, via mongosh — see §5b):
- one RSA signing key in `idp.idp_signing_keys`
- the marvel-side `sso_config` entry + the idp-side `idp_clients` entry

If you have shape A and you're happy with mongosh for admin, **skip to §5b**. If you want a web UI, add §6 (Vaadin).

### 5b. Seed the RSA key + marvel↔IdP wiring (mongosh)

Without Vaadin, you do these from a mongosh shell. Connect into the container:

```sh
podman exec -it agent-mongo mongosh \
    'mongodb://root:changeme@localhost:27017/?authSource=admin'
```

In the shell, the JavaScript below mints a signing key + registers the marvel SPA as a client. **Paste it as one block.**

```js
// ── 1) Generate an RSA-2048 keypair using mongo's built-in OpenSSL.
//     mongosh doesn't expose RSA gen directly; if openssl is on the
//     Pi host you can mint outside the shell instead (see fallback).
//     This block uses the Pi-side openssl + writes the result back.
//     Easier path: install openssl on the Pi + use the shell script
//     scripts/mint-idp-signing-key.sh (added in a follow-up PR).
//
// FALLBACK (paste in a different terminal on the Pi):
//   KID="k-$(openssl rand -hex 4)"
//   openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out /tmp/key.pem
//   PRIV=$(cat /tmp/key.pem)
//   PUB=$(openssl rsa -in /tmp/key.pem -pubout)
//   then in mongosh:
//     db.getSiblingDB("idp").idp_signing_keys.insertOne({
//       kid: "<KID>", alg: "RS256",
//       publicKeyPem:  "<PUB>",
//       privateKeyPem: "<PRIV>",
//       active: true, createdAt: Math.floor(Date.now()/1000), notAfter: 0
//     });

// ── 2) Wire marvel ↔ idp in mongosh.
const MARVEL_BASE_URL = "https://marvel-3a78bd953f5f.herokuapp.com";
const IDP_ISSUER_URL  = "https://idp-63c97365e6ef.herokuapp.com/api/v1/idp";

db.getSiblingDB("xpmile").sso_config.replaceOne(
  {},
  { publicBaseUrl: MARVEL_BASE_URL,
    providers: [{
      id: "inhouse", displayName: "xpmile IdP", protocol: "oidc",
      issuer: IDP_ISSUER_URL, clientId: "xpmile-spa", clientSecret: "",
      scopes: ["openid","email","profile"],
      defaultRole: "Customer",
      allowedEmailDomains: [],
      groupRoleMapEnabled: false, groupRoleMap: {}
    }]
  },
  { upsert: true }
);

db.getSiblingDB("idp").idp_clients.replaceOne(
  { _id: "xpmile-spa" },
  { _id: "xpmile-spa",
    clientName: "xpmile IdP (marvel SPA)",
    clientSecretHash: "",
    redirectUris:          [MARVEL_BASE_URL + "/api/v1/sso/callback/inhouse"],
    postLogoutRedirectUris:[MARVEL_BASE_URL + "/login"],
    scopes:                ["openid","email","profile"],
    grantTypes:            ["authorization_code"]
  },
  { upsert: true }
);
```

Or use the one-shot script (which does the same thing): see [`docs/inhouse-idp.md`](inhouse-idp.md) → *Shortcut: seed the marvel ↔ IdP wiring in one script*.

---

## 6. Optional: bring up the Vaadin admin (shape B)

### First: what do you actually need Vaadin for?

The on-prem Vaadin admin is **not** how you create regular user accounts. That's confusing for first-time operators, so:

| Surface | What it manages | When you reach for it |
|---|---|---|
| **marvel UI** (logged in as `admin`) | Business accounts in `xpmile.account` (shippers, branches, regular users with `role: User/Admin`), shipments, etc. | Day-to-day user management — *no Vaadin install needed* |
| **Vaadin admin** | Auth-plane state in the `idp` database: SSO providers (`sso_config`), IdP signing keys (`idp.idp_signing_keys`), IdP client registrations (`idp.idp_clients`) | Wiring up SSO/OIDC, generating IdP RSA keypairs, registering OIDC RPs |

If all you want is more users on the marvel app, just log in as `admin / admin@123` and use the marvel UI's **Accounts** page. Skip this entire section.

### Option A — Vaadin on the Pi (shape B, in-place)

**Memory check first.** `free -h` should show at least ~600 MB free *before* you start Vaadin. If you've also got X11 or the desktop running, kill that with `sudo systemctl set-default multi-user.target && sudo reboot` and come back here in headless mode.

```sh
./onprem/run-onprem.sh start
# First call builds the Vaadin image — Maven + spring-boot compile.
# On a Pi 3B that's ~15 min (vs ~3 min on amd64). Subsequent calls
# reuse the cache.
./onprem/run-onprem.sh status   # confirm agent-onprem-ui is "Up"
```

Reach it at `http://<pi-ip>:8090`. From here the steps are identical to the generic guide — see [`docs/inhouse-idp.md`](inhouse-idp.md) → §6/§7/§8 (Vaadin walkthrough).

If the build OOMs (`cc1 killed`, or `java.lang.OutOfMemoryError`), bump swap to 4 GB and retry, OR run **Option B** below instead.

### Option B — Vaadin on your Mac/desktop, mongo on the Pi (Recommended for Pi 3B)

The Pi 3B's 1 GB RAM is genuinely tight for Vaadin (~480 MB idle + ~900 MB build spike). The clean alternative is to **leave Vaadin off the Pi entirely** and run it on your dev machine, pointing at the Pi's MongoDB. The Pi stays in shape A's ~450 MB footprint and Vaadin uses your laptop's resources.

Vaadin is a standard Java Spring Boot app that connects to MongoDB via a regular `mongodb://…` URI — it does NOT go through wsdbagent. So all you need is mongo to be reachable from your Mac. SSH tunnel is the easiest path (no Pi config change, no firewall edits, mongo never on the LAN):

```sh
# Terminal 1 — on your Mac. Leaves an SSH tunnel open so
# Pi:localhost:27017 looks like Mac:localhost:27017.
ssh -fN -L 27017:localhost:27017 naushada@<pi-ip>
# -f → background after auth, -N → don't run a remote command, runs silently.
# Kill later with: pkill -f "ssh -fN -L 27017"
```

Then run Vaadin one of two ways on the Mac. Both clone the repo first (only the `onprem/` subtree is used):

```sh
git clone --depth 1 https://github.com/naushada/xpmile.git ~/xpmile
cd ~/xpmile/onprem
```

**Option B.1 — pure Maven** (fastest dev-iteration; needs JDK 17 + Maven):

```sh
SPRING_DATA_MONGODB_URI='mongodb://xpmile:xpmile_pass@localhost:27017/xpmile?authSource=admin' \
  mvn spring-boot:run
```

**Option B.2 — same `onprem/Dockerfile`, run locally via podman** (no JDK install needed):

```sh
podman build -t xpmile-onprem-ui .
podman run --rm -p 8090:8090 \
  -e SPRING_DATA_MONGODB_URI='mongodb://xpmile:xpmile_pass@host.containers.internal:27017/xpmile?authSource=admin' \
  xpmile-onprem-ui
```

`host.containers.internal` inside the container resolves to your Mac's loopback through the podman-machine VM — which is where the SSH tunnel lands. For Option B.1 the URI is just `localhost:27017` because mvn runs natively, not in a container.

Either way: browse to **`http://localhost:8090`** on the Mac and the Vaadin admin loads. The data it manages lives in the Pi's mongo — same `sso_config`, same `idp.idp_signing_keys`, same `idp.idp_clients`. From the Vaadin UI's point of view, mongo is just "the database"; it doesn't care that the bytes are 4000 miles away over an SSH tunnel.

**Security**: never bypass the SSH tunnel by adding `ports: ["27017:27017"]` to the Pi's compose and exposing mongo on the LAN unauthenticated. The whole on-prem trust model assumes mongo is reachable only from `localhost`/`agent-net`. SSH-tunneled access keeps that invariant intact.

### Remote access to Vaadin from off-LAN

If you went with Option A (Vaadin on the Pi) and want to admin it from anywhere:

| Scenario | How |
|---|---|
| **Same LAN as the Pi** | Just open `http://<pi-ip>:8090`. |
| **Off-LAN, Pi reachable over SSH** | `ssh -L 8090:localhost:8090 naushada@<pi-ip>` from your Mac, then `http://localhost:8090`. |
| **Off-LAN, want a real URL** | Install [Tailscale](https://tailscale.com) on both Pi and Mac (free for personal). `tailscale up` on each. Vaadin lives at `http://<pi-tailscale-ip>:8090` from anywhere you're tailscaled. Zero NAT/cert work. |

**Hard rule**: never put `<pi-public-ip>:8090` on the public internet via router port-forwarding. The on-prem Vaadin admin has **no authentication** by design — it's a recovery/admin tool that lives behind physical or network access controls. A public port-forward would give the internet root-equivalent control over your SSO config + IdP signing keys.

---

## 7. Alternative: agents only (shape C — mongo elsewhere)

If mongo lives on a different machine (a NAS running mongo, another Pi, a desktop), the Pi 3B just needs to bridge the agents. Skip `agent-mongo` and `xpmile-mongo` build.

Edit `docker-compose.agent.yml` and **comment out** the `mongodb:` service block (or add `profiles: [skip]` to it). Then point both wsdbagent's `--mongo-db-uri` at the remote host:

```sh
# In .env:
SERVER_HOST=marvel-3a78bd953f5f.herokuapp.com
IDP_SERVER_HOST=idp-63c97365e6ef.herokuapp.com
# Override the MONGO_APP_USER / MONGO_APP_PASS for the remote mongo
# OR override the full URI per-agent if your topology differs.
MONGO_APP_USER=xpmile
MONGO_APP_PASS=<remote-mongo-app-password>
```

Then:

```sh
./run-agent.sh start
```

The wsdbagent containers reach `mongodb://...@mongodb:27017/...` by default — to point at a remote host, either:
- Set up a host alias inside `docker-compose.agent.yml` (`extra_hosts: - "mongodb:192.168.1.42"`), or
- Edit the `ARGS:` line on each wsdbagent service to use a different `--mongo-db-uri`.

Memory at idle is ~100 MB; the Pi handles this trivially.

---

## 8. Verification (any shape)

The smoke-test cookbook in [`docs/inhouse-idp.md`](inhouse-idp.md#verifying-the-deploy) is host-agnostic — run those same `curl` commands from the Pi (or from your laptop). Look for the 5-step layered sequence.

---

## 9. Routine maintenance on a Pi 3B

- **After a Heroku release** that touches `Dockerfile.wsdbagent`: `podman pull docker.io/naushada/xpmile-wsdbagent:latest && ./run-agent.sh stop && ./run-agent.sh start`. The cert-watcher's restart alone won't re-pull. See [`docs/inhouse-idp.md`](inhouse-idp.md) → troubleshooting → *agent-wsdbagent / agent-wsdbagent-idp running an outdated image*.
- **Cert rotation** (~every CI release): `./run-agent.sh refresh-certs` — pulls the latest uniservice image, extracts the rotated cert family, cert-watcher restarts both agents.
- **Disk pressure**: `podman system prune -a` clears unused images. The mongo data volume is untouched; the next agent start reuses it.
- **Reboots**: see §10 below — the default is NOT auto-start.

---

## 10. Surviving reboots — auto-start the agent stack

### If you're on Docker (Path B in §2) — nothing to do here

`dockerd` is a system service (`systemctl is-enabled docker` → `enabled`), so the compose file's `restart: unless-stopped` policy survives host reboots natively. After a reboot, the kernel comes up → systemd starts dockerd → dockerd brings the unless-stopped containers back automatically. Skip the rest of this section.

Verify once after your first reboot:

```sh
sudo reboot
# (wait, ssh back in)
docker ps        # all four containers should be Up without you doing anything
```

### If you're on podman (Path A in §2) — three options, install-agent.sh handles one for you

By default, the agent stack does **not** restart on a Pi reboot:

- `docker-compose.agent.yml`'s `restart: unless-stopped` policy only catches in-process crashes (e.g. mongo SIGSEGV) — podman re-launches the container. It does NOT survive the host reboot.
- Rootless podman runs containers under your user systemd scope. When you log out (or reboot), that scope tears down → containers stop.

Three options, listed in **order of operator-effort** (lowest first). Pick one.

### Option C — `install-agent.sh`'s auto-generated systemd-user unit (Recommended)

If you installed via the TL;DR (`curl -sSf …install-agent.sh | bash`) on a rootless-podman host, the script **already** wrote `~/.config/systemd/user/xpmile-agent.service` and ran `systemctl --user enable xpmile-agent.service` for you. Verify with:

```sh
systemctl --user status xpmile-agent.service
# Should say `Loaded: loaded (~/.config/systemd/user/xpmile-agent.service; enabled; …)`
```

What the unit does:

```ini
[Unit]
Description=xpmile on-prem agent stack
After=network-online.target
Wants=network-online.target

[Service]
Type=oneshot
RemainAfterExit=yes
WorkingDirectory=%h/xpmile-agent
ExecStartPre=/bin/sleep 15
ExecStart=/usr/bin/podman-compose -f docker-compose.agent.yml [--profile idp] up -d
ExecStop=/usr/bin/podman-compose -f docker-compose.agent.yml [--profile idp] down
TimeoutStartSec=600
Restart=on-failure
RestartSec=30

[Install]
WantedBy=default.target
```

The `--profile idp` part is included automatically when you supplied `IDP_SERVER_HOST` at install time. The 15 s `sleep` lets DNS settle before the first Docker Hub pull (matters on a Pi 3B's first 10 s of uptime).

**Still required** — linger (the one piece `install-agent.sh` can't do, because it needs root):

```sh
sudo loginctl enable-linger $(whoami)
```

Without linger, your user systemd dies when your ssh session ends, and the unit never fires at boot. Run this ONCE; then `sudo reboot` and verify with `systemctl --user status xpmile-agent`.

Routine controls:

```sh
systemctl --user start    xpmile-agent     # bring stack up (also runs at boot)
systemctl --user stop     xpmile-agent     # bring stack down
systemctl --user restart  xpmile-agent     # cycle
systemctl --user disable  xpmile-agent     # opt out of boot start (without deleting)
```

If you'd rather keep boot-start in your own hands, opt out at install time:

```sh
INSTALL_SYSTEMD=0 curl -sSf …/install-agent.sh | bash
```

Then pick **Option A** or **Option B** below.

### Option A — `@reboot` cron (legacy / no systemd preference)

```sh
# As the user that runs the agents:
crontab -e
# Append:
@reboot sleep 30 && cd $HOME/xpmile && ./run-agent.sh start >> /tmp/agent-boot.log 2>&1
```

The `sleep 30` lets the network come up first (TLS handshake needs it). Output goes to `/tmp/agent-boot.log` so you can diagnose if it didn't.

**Caveat:** rootless podman needs lingering for cron's `@reboot` to actually reach a running user systemd. Run once:

```sh
sudo loginctl enable-linger $(whoami)
```

After that, your user systemd stays alive across logout AND comes up before login on boot — `@reboot` cron entries fire with podman able to talk to its socket.

### Option B — per-container systemd-user units (most granular; survives podman upgrades)

If you need each container managed independently by systemd (e.g. so you can `systemctl --user restart container-agent-wsdbagent` without touching mongo), generate per-container units instead of Option C's single wrapper.

Generate a per-container unit from each running container, then enable:

```sh
sudo loginctl enable-linger $(whoami)   # (same prerequisite)

mkdir -p ~/.config/systemd/user
cd ~/.config/systemd/user

podman generate systemd --new --files --name agent-mongo
podman generate systemd --new --files --name agent-wsdbagent
podman generate systemd --new --files --name agent-wsdbagent-idp
podman generate systemd --new --files --name xpmile-cert-watcher
# (Add agent-onprem-ui too if you've gone with shape B.)

systemctl --user daemon-reload
systemctl --user enable --now \
    container-agent-mongo.service \
    container-agent-wsdbagent.service \
    container-agent-wsdbagent-idp.service \
    container-xpmile-cert-watcher.service
```

Verify:

```sh
systemctl --user status container-agent-wsdbagent.service
# Should show "active (running)".
reboot   # try it
# After login:
./run-agent.sh status
# All containers up without you doing anything else.
```

The `--new` flag generates units that re-CREATE the container on each start (rather than just restarting an existing one). That's what you want — survives `podman system prune`, image upgrades, etc.

### Option C — don't auto-start (intentional)

Some operators prefer manual start so a misconfigured `.env` doesn't blow up unattended. Run `./run-agent.sh start` after each reboot. The downside is forgetting → idp dyno can't reach mongo → user-visible 503s.

---

---

## References

- [`docs/inhouse-idp.md`](inhouse-idp.md) — host-agnostic operator guide (covers all the post-install Vaadin walkthroughs + troubleshooting + verification)
- [`docs/ws-db-agent.md`](ws-db-agent.md) — InnerTLS + cert-rotation playbook
- [`docs/app.md`](app.md) — Heroku-side deploy mechanics
- [`CLAUDE.md`](../CLAUDE.md) → *wsdbagent stack* — the architecture summary
