# Operator guide — Pi 3B (or any small host) with `runc` only

This is an alternative install path for operators who don't want a full
container engine (docker or podman) on the MongoDB host. It uses
**`runc`** — the OCI low-level runtime that both docker and podman wrap
underneath — directly, plus **systemd** for lifecycle management and
**`xpmile-pull`** for image fetch.

> **Audience.** This is the **advanced** path. If you don't already
> understand the OCI runtime spec (`config.json`, mounts, namespaces),
> the Pi-3B [path A or B](./operator-pi3b.md#2-install-podman-or-docker)
> will save you several hours and is the recommended default. The win
> here is **memory + attack surface**, not ergonomics.

Why it exists:
- **No daemon.** docker's `dockerd` idles at ~80–120 MB RSS on a Pi 3B;
  rootless podman has no daemon but `podman-compose` + the pause
  container still costs ~30 MB. `runc` is invoked once per container
  start and then exits — the running mongod / wsdbagent is the **only**
  process you pay for.
- **No compose-layer.** Eliminates `podman-compose` (a Python wrapper
  with its own quirks on arm) and the cert-watcher Alpine sidecar
  (replaced with a 10-line systemd `path` unit).
- **Smaller install footprint.** `runc` + `jq` + `xpmile-pull` is ~13 MB
  on disk vs ~250 MB for the full docker stack. On a 16 GB SD card this
  matters less than the RAM saving.

What you give up:
- **No `docker ps` / `podman ps`.** Status comes from `systemctl status`
  + `runc list`. You'll need to be comfortable with both.
- **No `./run-agent.sh`.** That script wraps `podman-compose`. With runc
  you drive lifecycle through `systemctl`.
- **Image updates are manual** — no `pull_policy: always`. Re-running
  `xpmile-pull --force` is the moral equivalent.

> **What is `xpmile-pull`?** A small in-house OCI image puller (~3 MB
> binary, no daemon, no Go runtime, no Python). It does what
> `skopeo copy` + `umoci unpack` used to do in one call — fetch a
> Docker Hub image and write a ready-to-`runc` OCI bundle to disk.
> Shipped with the operator tarball (see §0). Design + behaviour:
> [`docs/design/runc-pull/`](./design/runc-pull/USAGE.md).

---

## 0. TL;DR — one-curl install (skip to §1 if you want the manual recipe)

Since v1.3.0 there is a self-contained operator tarball that bundles
`xpmile-pull` + every OCI `config.json` template + every systemd unit
+ a renderer script that wires it all together. If you just want a
working runc stack with the same defaults this guide documents, run:

```sh
export SERVER_HOST="marvel-XXXXXXXX.herokuapp.com"
export IDP_SERVER_HOST="idp-XXXXXXXX.herokuapp.com"   # optional second Heroku app
export CERTS_DIR="$HOME/xpmile/certs/cloud-issued/innertls"
curl -sSf https://raw.githubusercontent.com/naushada/xpmile/v1.3.0/install-agent-runc.sh | bash
```

That detects host arch, downloads
`xpmile-runc-bundle-v1.3.0-{amd64,arm64}.tar.gz` from the
[v1.3.0 GitHub Release](https://github.com/naushada/xpmile/releases/tag/v1.3.0),
runs the bundled `install.sh`, and brings up the three systemd units
(mongo + 2× wsdbagent + a cert-rotation path watcher). Update procedure
becomes re-running the same one-liner with a fresh `XPMILE_RELEASE` env
var.

The rest of this guide is for operators who want to **own each step
manually** — useful when you're customising the OCI config, debugging
an install failure, or porting the recipe to a non-Bookworm host where
the tarball's bundled libs don't fit. Everything below produces the same
result as the one-liner.

---

## 1. Memory budget vs path A/B (Pi 3B reference)

| Stack | RSS at idle | Notes |
|---|---|---|
| Path A (podman + cert-watcher + 3 containers) | ~450 MB | `podman-compose` itself adds ~25 MB on top. |
| Path B (docker + cert-watcher + 3 containers) | ~530 MB | `dockerd` daemon is the difference. |
| **Path C (runc + 3 containers via systemd)** | **~340 MB** | mongo + 2× wsdbagent only; no engine daemon, no sidecar. |

The mongo RSS (~280 MB on Pi 3B with the default cache size) dominates
all three rows — savings on the engine layer matter most on the
**agents-only** variant of this stack (see §10), where mongo lives
elsewhere and the Pi only runs the two wsdbagents.

---

## 2. Prerequisites

Raspberry Pi OS Bookworm 64-bit (or any Debian-12-based arm64 distro).
Bookworm gives you cgroups v2 by default — that's what `runc` wants on
modern kernels. Ubuntu 22.04+ and Fedora 38+ also work; older distros on
cgroups v1 require `runc --systemd-cgroup=false` and aren't covered
here.

```sh
sudo apt-get update
sudo apt-get install -y runc jq                      # ~10 MB total
runc --version                                       # ≥ 1.1
```

Plus `xpmile-pull` — download the matching tarball from the latest
GitHub Release and extract the binary + its bundled `.so` deps:

```sh
ARCH=$(uname -m); case "$ARCH" in x86_64) ARCH=amd64;; aarch64) ARCH=arm64;; esac
TAG=v1.3.0   # or "latest"; pin for reproducible deploys
mkdir -p ~/xpmile-runc
curl -sSfL "https://github.com/naushada/xpmile/releases/download/$TAG/xpmile-runc-bundle-$TAG-$ARCH.tar.gz" \
    | tar -xz -C ~/xpmile-runc --strip-components=1
~/xpmile-runc/bin/xpmile-pull --version             # smoke-test the wrapper
```

`bin/xpmile-pull` is a 3-line `sh` wrapper that prepends `lib/` to
`LD_LIBRARY_PATH` and execs `bin/xpmile-pull.real`. The real binary
needs glibc ≥ 2.31 (Bullseye or newer); `libssl1.1 + libcrypto1.1 +
libACE + libstdc++ + libgcc_s + libz` travel with the tarball under
`~/xpmile-runc/lib/`, so you do **not** need to apt-install them. Debug
by running `bin/xpmile-pull.real --version` directly + reading `ldd`
output if something fails.

User namespaces — required for rootless runc. Bookworm enables them by
default; verify:

```sh
sysctl kernel.unprivileged_userns_clone     # → 1
```

If it returns 0:
```sh
echo 'kernel.unprivileged_userns_clone=1' | sudo tee /etc/sysctl.d/00-userns.conf
sudo sysctl --system
```

Subordinate UID/GID ranges for the user that will run the containers
(default Pi user is `pi`; substitute as needed):

```sh
grep -E "^${USER}:" /etc/subuid /etc/subgid
# Expected output (Bookworm sets this automatically on user create):
#   /etc/subuid:pi:100000:65536
#   /etc/subgid:pi:100000:65536
# If empty:
sudo usermod --add-subuids 100000-165535 --add-subgids 100000-165535 "${USER}"
```

---

## 3. Repo clone — same as paths A/B

Shallow clone is the right default — saves ~140 MB on the SD card:

```sh
git clone --depth 1 https://github.com/naushada/xpmile.git ~/xpmile
cd ~/xpmile
cp .env.agent .env
$EDITOR .env                                         # set SERVER_HOST, IDP_SERVER_HOST, MONGO_VERSION=4.4
```

You're **not** going to use `docker-compose.agent.yml` itself — but the
repo carries the cert helper (`./run-agent.sh refresh-certs`),
`docker/mongo-init.js` (bind-mounted into the mongo bundle in §5a),
and the SSO/IdP seed scripts that path A/B use. The operator scripts
are still useful with runc; they don't depend on the container engine.

The minimum file set for the runc path is even smaller than path A/B's
(no `Dockerfile.mongo` — we use upstream mongo:4.4 directly):

```
run-agent.sh                       # only for refresh-certs
docker/mongo-init.js               # bind-mounted at runtime into the mongo bundle
scripts/seed-default-idp-sso.sh    # only if you wire the in-house IdP
```

Sparse checkout works the same way as in
[`operator-pi3b.md`](./operator-pi3b.md#what-run-agentsh-actually-reads).

```sh
./run-agent.sh refresh-certs                         # writes ./certs/cloud-issued/innertls/{ca,client}.{crt,key}
ls ./certs/cloud-issued/innertls/
# ca.crt  client.crt  client.key
```

---

## 4. Image fetch — `xpmile-pull` into OCI bundles

We need three images on disk as runc-ready bundles. The layout we'll
use lives under `/var/lib/xpmile/bundles/` (rootful runc) or
`~/.local/share/xpmile/bundles/` (rootless). Pick one and stay
consistent; the rest of this guide uses the rootful path.

```sh
sudo mkdir -p /var/lib/xpmile/{bundles,mongo-data}
sudo chown -R "${USER}:${USER}" /var/lib/xpmile
PULL=~/xpmile-runc/bin/xpmile-pull                  # the wrapper from §2
```

`xpmile-pull <ref> --to <dir>` fetches the image from Docker Hub,
verifies every SHA-256 digest, unpacks the layers honouring OCI
whiteouts, and writes a runc-ready bundle at `<dir>/` (`rootfs/` +
`config.json`). One call does what `skopeo copy` + `umoci unpack` used
to take two. `--force` overwrites an existing bundle; without it the
tool refuses and exits 5. See
[`docs/design/runc-pull/USAGE.md`](./design/runc-pull/USAGE.md) for
flags + exit codes.

### 4a. mongo:4.4 (upstream, no rebuild needed)

```sh
sudo -E "$PULL" docker.io/library/mongo:4.4 \
    --to /var/lib/xpmile/bundles/mongo \
    --force
```

We deliberately **skip** `docker/Dockerfile.mongo` (the custom
`xpmile-mongo` image that bakes `mongo-init.js` into the image at
`/docker-entrypoint-initdb.d/`). Building that image needs docker or
podman — exactly what this path avoids. Instead, we **bind-mount**
`mongo-init.js` into the container in §5a. The upstream image's
`docker-entrypoint.sh` scans `/docker-entrypoint-initdb.d/` on first
startup either way, so behaviour is identical.

### 4b. xpmile-wsdbagent (Docker Hub, multi-arch — arm64 picked
automatically on Pi)

```sh
sudo -E "$PULL" docker.io/naushada/xpmile-wsdbagent:latest \
    --to /var/lib/xpmile/bundles/wsdbagent-marvel \
    --force
```

The tool picks the matching arch entry from the multi-arch manifest
automatically. Override with `--arch arm64` (or amd64) if needed; see
USAGE.

We use the same bundle for both `wsdbagent` and `wsdbagent-idp` —
they're the same binary with different env vars. Two pulls means two
copies of the 60 MB rootfs on disk and is a waste; we pull once into
`wsdbagent-marvel`, hard-link clone to `wsdbagent-idp` (§6), and give
each instance its own `config.json`.

### 4c. Pinning to a specific build

Same idiom as docker-compose's `WSDBAGENT_IMAGE` env var — substitute
the tag:

```sh
WSDBAGENT_TAG=sha-e1353c1
sudo -E "$PULL" docker.io/naushada/xpmile-wsdbagent:${WSDBAGENT_TAG} \
    --to /var/lib/xpmile/bundles/wsdbagent-marvel \
    --force
```

Rolling back is just re-pulling the previous tag into the same bundle
dir. There is no separate `oci-cache/` step — `xpmile-pull` writes the
bundle directly, so disk only carries the currently-deployed version
per bundle. If you want to keep multiple versions side-by-side, pull
into `bundles/wsdbagent-<tag>/` and update the systemd unit's
`--bundle` path instead.

---

## 5. OCI bundle config — `config.json`

Each running container needs its own `config.json`. `xpmile-pull`
writes a minimal stub (env from the image config, args = the image's
CMD/Entrypoint, host networking, no bind mounts) — enough for a
sanity-check `runc run`, but not what we actually want. Overwrite each
one with the specs below.

### 5a. `mongo` config.json

Bind-mounts:
- `/var/lib/xpmile/mongo-data` → `/data/db` (persistent data; survives
  bundle re-unpack)
- `~/xpmile/docker/mongo-init.js` → `/docker-entrypoint-initdb.d/mongo-init.js:ro`
  (read by docker-entrypoint.sh on first start to create the `xpmile`
  and `idp` app users)

Env:
- `MONGO_INITDB_ROOT_USERNAME`, `MONGO_INITDB_ROOT_PASSWORD` — read by
  the upstream entrypoint to create the root user
- `MONGO_APP_USER`, `MONGO_APP_PASS` — read by `mongo-init.js` (it
  templates these into the `createUser` calls — see the file)

Save as `/var/lib/xpmile/bundles/mongo/config.json`:

```jsonc
{
  "ociVersion": "1.0.2",
  "process": {
    "terminal": false,
    "user": { "uid": 999, "gid": 999 },
    "args": ["docker-entrypoint.sh", "mongod", "--bind_ip_all"],
    "env": [
      "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
      "HOME=/data/db",
      "TERM=xterm",
      "MONGO_INITDB_ROOT_USERNAME=root",
      "MONGO_INITDB_ROOT_PASSWORD=changeme",
      "MONGO_APP_USER=xpmile",
      "MONGO_APP_PASS=xpmile_pass"
    ],
    "cwd": "/"
  },
  "root": { "path": "rootfs", "readonly": false },
  "hostname": "agent-mongo",
  "mounts": [
    { "destination": "/proc", "type": "proc", "source": "proc" },
    { "destination": "/dev", "type": "tmpfs", "source": "tmpfs",
      "options": ["nosuid", "strictatime", "mode=755", "size=65536k"] },
    { "destination": "/dev/pts", "type": "devpts", "source": "devpts",
      "options": ["nosuid", "noexec", "newinstance", "ptmxmode=0666", "mode=0620"] },
    { "destination": "/sys", "type": "sysfs", "source": "sysfs",
      "options": ["nosuid", "noexec", "nodev", "ro"] },
    { "destination": "/data/db", "type": "bind",
      "source": "/var/lib/xpmile/mongo-data",
      "options": ["rbind", "rw"] },
    { "destination": "/docker-entrypoint-initdb.d/mongo-init.js", "type": "bind",
      "source": "/home/pi/xpmile/docker/mongo-init.js",
      "options": ["rbind", "ro"] }
  ],
  "linux": {
    "namespaces": [
      { "type": "pid" },
      { "type": "ipc" },
      { "type": "uts" },
      { "type": "mount" }
    ]
  }
}
```

Three things to note vs the docker-compose:

1. **No `network` namespace** — host networking. mongo binds 27017 on
   localhost; the two wsdbagents connect to `127.0.0.1:27017`. Bridge
   networking is a per-container ~3 MB overhead we don't need on a Pi.
2. **No `--auth`** in args — `mongo-init.js` enables it on the user it
   creates; the root-user bootstrap happens before auth is enforced
   (same as docker-compose's behaviour, which also doesn't pass
   `--auth`).
3. **uid/gid 999** — the mongo user inside the upstream image.

### 5b. `wsdbagent` config.json

Save as `/var/lib/xpmile/bundles/wsdbagent-marvel/config.json`. Note we
clone the unpacked bundle dir per instance — see §6.

Bind-mounts:
- `./certs/cloud-issued/innertls` → `/certs:ro`

Env: the `--server-host`, `--mongo-db-uri`, etc. that compose passes
via `ARGS`. The image's CMD is `/opt/wsdbagent/wsdbagent ${ARGS}`, so
we set `ARGS` here.

```jsonc
{
  "ociVersion": "1.0.2",
  "process": {
    "terminal": false,
    "user": { "uid": 0, "gid": 0 },
    "args": ["/bin/sh", "-c", "/opt/wsdbagent/wsdbagent ${ARGS}"],
    "env": [
      "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
      "TZ=Asia/Calcutta",
      "ARGS=--server-host marvel-3a78bd953f5f.herokuapp.com --server-port 443 --mongo-db-uri mongodb://xpmile:xpmile_pass@127.0.0.1:27017/xpmile?authSource=admin --mongo-db-name xpmile --mongo-db-connection-pool 10 --backoff 5 --tls-ca /certs/ca.crt --tls-cert /certs/client.crt --tls-key /certs/client.key"
    ],
    "cwd": "/opt/wsdbagent"
  },
  "root": { "path": "rootfs", "readonly": false },
  "hostname": "agent-wsdbagent",
  "mounts": [
    { "destination": "/proc", "type": "proc", "source": "proc" },
    { "destination": "/dev", "type": "tmpfs", "source": "tmpfs",
      "options": ["nosuid", "strictatime", "mode=755", "size=65536k"] },
    { "destination": "/dev/pts", "type": "devpts", "source": "devpts",
      "options": ["nosuid", "noexec", "newinstance", "ptmxmode=0666", "mode=0620"] },
    { "destination": "/sys", "type": "sysfs", "source": "sysfs",
      "options": ["nosuid", "noexec", "nodev", "ro"] },
    { "destination": "/etc/resolv.conf", "type": "bind",
      "source": "/etc/resolv.conf", "options": ["rbind", "ro"] },
    { "destination": "/certs", "type": "bind",
      "source": "/home/pi/xpmile/certs/cloud-issued/innertls",
      "options": ["rbind", "ro"] }
  ],
  "linux": {
    "namespaces": [
      { "type": "pid" },
      { "type": "ipc" },
      { "type": "uts" },
      { "type": "mount" }
    ]
  }
}
```

Key things:
- **`/bin/sh -c ...`** wrapper — same idiom as the Dockerfile's `CMD`
  shell-form. Needed for `${ARGS}` substitution; runc's `exec` form
  doesn't expand variables.
- **`/etc/resolv.conf` bind-mount** — host networking shares the host's
  network namespace but NOT its `/etc`. Without this, DNS lookup for
  `marvel-3a78bd953f5f.herokuapp.com` fails inside the container.
- **No network namespace** — same reason as mongo: host networking, no
  isolation, no overhead.

### 5c. `wsdbagent-idp` config.json

Save as `/var/lib/xpmile/bundles/wsdbagent-idp/config.json`. Identical
to 5b except:

```diff
-   "hostname": "agent-wsdbagent",
+   "hostname": "agent-wsdbagent-idp",

-      "ARGS=--server-host marvel-3a78bd953f5f.herokuapp.com ... /xpmile?authSource=admin --mongo-db-name xpmile ..."
+      "ARGS=--server-host idp-63c97365e6ef.herokuapp.com ... /idp?authSource=admin --mongo-db-name idp ..."
```

Same `/certs` bind-mount — both agents share the cert family by design
(see [`docs/ws-db-agent.md`](./ws-db-agent.md) → cert rotation).

---

## 6. Per-instance bundle clone

§4b pulled into `wsdbagent-marvel`. We run two wsdbagent **instances**
off the same rootfs. The clean way is a hard-link clone of the rootfs:

```sh
cd /var/lib/xpmile/bundles
rm -rf wsdbagent-idp                                 # ensure target is clean
cp -al wsdbagent-marvel wsdbagent-idp                # hard-link clone — same inodes, separate config.json
# Now overwrite config.json in each with the spec from §5b and §5c.
```

`cp -al` (archive + hard-links) means the two bundle dirs share rootfs
inodes — disk cost is just one bundle plus two `config.json` files.
Image updates via re-`xpmile-pull --force` overwrite
`wsdbagent-marvel/`; you then re-clone to `wsdbagent-idp/` and re-write
its `config.json` — same recipe as initial install.

> **Why hardlink-clone, not overlayfs?** `xpmile-pull` merges the
> image's layers into a single flat `rootfs/` at *pull time* — there is
> no overlay graph driver at runtime, no FUSE, no kernel `overlayfs`
> dependency. That's deliberate for 1 GB Pi-class hosts where the
> overlay-mount metadata isn't worth carrying for two long-running
> containers. The cost is that re-pull rewrites the whole rootfs
> (`xpmile-pull --force` ~50 MB for wsdbagent) instead of swapping a
> single layer. Acceptable when image rotations are weekly. Full
> rationale + comparison table: [`docs/design/runc-pull/runc-pull-design.md`](./design/runc-pull/runc-pull-design.md#6a-merge-model--flat-rootfs-no-overlay-at-runtime).

---

## 7. systemd units

Three units — one per container. Save under `/etc/systemd/system/`.
Replace `/home/pi` with your actual `$HOME` if you cloned elsewhere.

### 7a. `xpmile-mongo.service`

```ini
[Unit]
Description=xpmile MongoDB (runc)
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStartPre=-/usr/bin/runc delete --force xpmile-mongo
ExecStart=/usr/bin/runc run --bundle /var/lib/xpmile/bundles/mongo xpmile-mongo
ExecStop=/usr/bin/runc kill xpmile-mongo SIGTERM
Restart=always
RestartSec=5
TimeoutStopSec=30

[Install]
WantedBy=multi-user.target
```

`ExecStartPre` with the leading `-` (ignore-failure) is the runc
equivalent of `--rm` — without it, a unit restart hits
`container with id exists` because the previous run's metadata is still
in `/run/runc/`. The dash tolerates the first-ever start where there's
nothing to delete.

### 7b. `xpmile-wsdbagent.service` and `xpmile-wsdbagent-idp.service`

```ini
[Unit]
Description=xpmile wsdbagent → marvel (runc)
After=xpmile-mongo.service
Requires=xpmile-mongo.service

[Service]
Type=simple
ExecStartPre=-/usr/bin/runc delete --force xpmile-wsdbagent
ExecStart=/usr/bin/runc run --bundle /var/lib/xpmile/bundles/wsdbagent-marvel xpmile-wsdbagent
ExecStop=/usr/bin/runc kill xpmile-wsdbagent SIGTERM
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

The idp variant flips `xpmile-wsdbagent` → `xpmile-wsdbagent-idp` in
all four references (`Description`, both `runc` calls, the
`--bundle` path).

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now xpmile-mongo.service
# wait ~20s for mongo to bootstrap users on first run
sudo journalctl -u xpmile-mongo -f                   # watch for "MongoDB starting" + "createUser"
sudo systemctl enable --now xpmile-wsdbagent.service xpmile-wsdbagent-idp.service
```

`Requires=xpmile-mongo.service` is **start-ordering**, not a health
check — wsdbagent will retry connect every `--backoff 5` seconds until
mongo is ready. The 20-second wait above is just for the operator's
peace of mind; systemd doesn't need it.

---

## 8. Cert rotation — replaces the cert-watcher sidecar

Path A/B use an Alpine sidecar that polls the certs dir and POSTs to
the engine's REST API. With runc + systemd, the equivalent is two
units: a `.path` unit watching the dir, and a `.service` unit that runs
`systemctl restart` on the two agents.

`/etc/systemd/system/xpmile-certs.path`:

```ini
[Unit]
Description=xpmile certs change watcher
After=xpmile-wsdbagent.service

[Path]
PathChanged=/home/pi/xpmile/certs/cloud-issued/innertls
Unit=xpmile-certs-rotate.service

[Install]
WantedBy=multi-user.target
```

`/etc/systemd/system/xpmile-certs-rotate.service`:

```ini
[Unit]
Description=Restart both wsdbagents after a cert rotation

[Service]
Type=oneshot
ExecStart=/bin/systemctl restart xpmile-wsdbagent.service xpmile-wsdbagent-idp.service
```

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now xpmile-certs.path
```

Now `./run-agent.sh refresh-certs` (which writes new `ca.crt` /
`client.crt` / `client.key` into the watched dir) triggers a restart of
both agents within ~5 seconds — same end-to-end latency as the
cert-watcher sidecar, but with zero per-second polling cost.

`PathChanged` fires once per write; if `refresh-certs` writes three
files, you get one restart cycle (systemd debounces). The idp agent
restart is fired unconditionally — exactly like the cert-watcher's
"restart both" semantics; if you're running a marvel-only stack, simply
omit `xpmile-wsdbagent-idp.service` from the `ExecStart` line.

---

## 9. Reboots are auto-handled

`systemctl enable` on all four units adds them to
`multi-user.target.wants`. On boot:
1. Kernel + systemd come up
2. `network-online.target` reaches ready
3. `xpmile-mongo.service` starts → `runc run` → mongo
4. `xpmile-wsdbagent.service` + `xpmile-wsdbagent-idp.service` start in
   parallel after mongo
5. `xpmile-certs.path` arms and starts watching

No cron, no user-systemd-linger, no `@reboot` hooks — `multi-user.target`
is the default boot target and the `[Install] WantedBy=multi-user.target`
stanza is what wires you in.

Compare with path A (podman §10 of [`operator-pi3b.md`](./operator-pi3b.md)),
which needs `loginctl enable-linger` + `@reboot` cron because rootless
podman runs in the user systemd scope. runc-via-system-systemd doesn't
have that problem.

---

## 10. Update procedure — when CI publishes a new wsdbagent image

This replaces `./run-agent.sh refresh-certs && ./run-agent.sh stop &&
./run-agent.sh start` from path A/B.

```sh
WSDBAGENT_TAG=latest                    # or sha-<commit> to pin
PULL=~/xpmile-runc/bin/xpmile-pull      # the wrapper from §2

# 10a. Stop the agents (mongo keeps running — clients reconnect on resume)
sudo systemctl stop xpmile-wsdbagent.service xpmile-wsdbagent-idp.service

# 10b. Re-pull into the marvel bundle (config.json outside rootfs survives;
#       xpmile-pull preserves an existing config.json by design)
sudo -E "$PULL" docker.io/naushada/xpmile-wsdbagent:${WSDBAGENT_TAG} \
    --to /var/lib/xpmile/bundles/wsdbagent-marvel \
    --force

# 10c. Re-clone for the idp instance (rootfs is hard-linked; idp config.json
#       is the one you wrote in §5c — re-write it from your saved copy)
sudo rm -rf /var/lib/xpmile/bundles/wsdbagent-idp/rootfs
sudo cp -al /var/lib/xpmile/bundles/wsdbagent-marvel/rootfs \
            /var/lib/xpmile/bundles/wsdbagent-idp/rootfs

# 10d. Bring both agents back
sudo systemctl start xpmile-wsdbagent.service xpmile-wsdbagent-idp.service

# 10e. Cert refresh (if a CI deploy rotated InnerTLS) — same script as path A/B
./run-agent.sh refresh-certs
# (xpmile-certs.path fires the restart automatically — no manual action needed)
```

If you'd rather not maintain this by hand, the tarball's `install.sh`
re-runs idempotently — `bash ~/xpmile-runc/install.sh` does the
equivalent in one call (re-pulls both images with `--force`, re-clones,
re-renders the configs, restarts the units). Most operators on the
runc path use that route.

---

## 11. Agents-only variant (mongo on a different host)

If mongo lives on another machine, skip §5a / §7a entirely. Edit each
wsdbagent `config.json` to point at the remote mongo:

```diff
-      "ARGS=... --mongo-db-uri mongodb://xpmile:xpmile_pass@127.0.0.1:27017/xpmile?... ..."
+      "ARGS=... --mongo-db-uri mongodb://xpmile:xpmile_pass@192.168.1.42:27017/xpmile?... ..."
```

The Pi 3B's job is then only the two agents — RSS drops to ~70 MB and
the install fits trivially in a 512 MB Pi Zero 2 W if you're targeting
even smaller hardware.

---

## 12. Troubleshooting

**`runc create failed: rootfs ... is not an absolute path`**
Bundle paths in `--bundle` must be absolute. `runc` resolves `rootfs`
relative to the bundle dir; you can't pass `./bundles/mongo`.

**`unable to mount /sys: permission denied`**
You're running rootless without `unprivileged_userns_clone`. See §2.

**Mongo logs `permission denied` on `/data/db`**
The host's `/var/lib/xpmile/mongo-data` is owned by the host's `pi`
user; inside the user namespace mongo runs as uid 999, which maps to
host uid `100000 + 999 = 100998`. Fix:
```sh
sudo chown -R 100998:100998 /var/lib/xpmile/mongo-data
```
(Or run mongo with `--user-mapping` in `config.json`'s `linux.uidMappings` if you prefer.)

**Agent logs `Could not resolve host: marvel-...herokuapp.com`**
You forgot the `/etc/resolv.conf` bind-mount in §5b. Without a network
namespace, the host shares its `lo` and `eth0`, but the container's
`/etc/resolv.conf` is still whatever the image baked (often empty).

**`xpmile-certs.path` never fires**
`PathChanged` watches a directory's contents; it requires the dir to
exist at boot. If `./certs/cloud-issued/innertls/` is empty until you
first run `./run-agent.sh refresh-certs`, that first refresh might
race. Run `./run-agent.sh refresh-certs` **once before**
`systemctl enable --now xpmile-certs.path`, then you're set.

**`runc list` shows the container is gone but `systemctl status` says
active**
Race between `ExecStop` and the container's natural exit. `runc kill`
sends SIGTERM, mongo / wsdbagent exit, runc auto-deletes the container,
then systemd's `ExecStop` runs and finds nothing. The `-` prefix
pattern in `ExecStartPre` (§7a) and the `Restart=always` policy handle
this; it's logged as a `Result: exit-code` and otherwise harmless.

---

## 13. When to NOT use this path

- You're on a Pi 4 with 4+ GB RAM — the 100 MB you save isn't worth the
  operator complexity. Use path A.
- You're going to need to debug shipment/account data interactively
  (`docker exec -it agent-mongo mongosh`). `runc exec` works, but the
  ergonomics are worse — you'll be `runc exec -t xpmile-mongo mongosh`
  with no tab completion of container IDs.
- The deployment is going to grow to include additional cloud-side
  Heroku apps. Each new app = another wsdbagent = another bundle clone
  + systemd unit pair. The per-add cost grows linearly; compose's
  `services:` block grows by a single yaml stanza. The break-even is
  around 4–5 cloud apps, after which path A becomes simpler.

For the canonical Pi 3B deployment with one or two cloud apps, runc is
the right choice when memory pressure matters. For everything else,
[`operator-pi3b.md`](./operator-pi3b.md) is the easier path.
