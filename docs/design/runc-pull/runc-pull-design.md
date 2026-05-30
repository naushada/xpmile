# Design — in-house Docker Hub puller for the runc operator path

> Status: **design / pre-implementation**. Follow-up doc: `runc-pull-tdd-plan.md` (to be written once this is signed off).

## 1. Context

`docs/operator-runc.md` (PR #40) ships a runc + systemd install path
for operators who don't want a full container engine on the MongoDB
host — the headline saving is ~100 MB of engine-layer RAM on a Pi 3B.
The path still requires **`skopeo`** (image fetch) and **`umoci`**
(layer unpack), so the prerequisite list in §2 is
`runc + skopeo + umoci + jq` (~25 MB of deps). Skopeo + umoci pull in
their own Go runtime and a sizeable transitive surface; in spirit
they're "almost a container engine" — exactly the thing the operator
chose runc to avoid.

This design replaces skopeo + umoci with a single in-house binary
**`xpmile-pull`** that talks Docker Hub's Registry v2 HTTP API
directly, verifies SHA-256 of every blob, and unpacks the layers into
an OCI bundle. After this change the operator's only container
dependency is **`runc` itself** (~5 MB on Bookworm) plus the
`xpmile-runc-bundle` tarball.

The "uniservice, Http" framing in the original request points at the
existing xpmile codebase: the SSO module already has an outbound
HTTPS client (`sso::HttpClient`, ACE_SOCK + OpenSSL, no libcurl) and
the project already links OpenSSL (SHA-256 / EVP) and zlib (gzip).
The new code is the registry protocol, the OCI tar/whiteout extractor,
and CLI plumbing — all genuinely new — riding on the HTTP + crypto
primitives we already trust.

## 2. Goal

A static `linux/amd64` + `linux/arm64` binary `xpmile-pull`, shipped
as part of a versioned
**`xpmile-runc-bundle-<version>-<arch>.tar.gz`** GitHub Release
asset, that gives the operator one command equivalent to
"skopeo copy + umoci unpack":

```sh
xpmile-pull docker.io/naushada/xpmile-wsdbagent:latest \
            --to /var/lib/xpmile/bundles/wsdbagent
```

When this is done, `docs/operator-runc.md` §2's apt-get list drops
to `runc jq` and §4 becomes "run xpmile-pull three times" instead of
six paragraphs of skopeo/umoci recipes.

## 3. Out of scope (v1)

- **Non-Docker-Hub registries** (quay.io, ghcr.io, gcr.io). Same v2
  API shape but each has its own auth realm; add later, not now.
- **Private repos** / authenticated pulls. Public Hub only. The token
  flow we implement is anonymous — the auth.docker.io endpoint will
  hand out a pull-scoped bearer with no credentials.
- **Image push.** Operators don't push.
- **Running containers.** That's runc's job; we hand it the bundle.
- **A complete `config.json` spec generation.** We emit a stub
  containing only the image's `Env` / `Cmd` / `Entrypoint`; the
  operator still overwrites it with the per-service templates from
  `docs/operator-runc.md` §5 (which are shipped in the tarball — see
  §8 below).
- **Layer cache reuse across pulls.** Each call re-downloads.
  Content-addressable cache under `--cache-dir` is a v2 follow-up.

## 4. Component map

New module: `modules/module/runcpull/` (siblings of `sso/`,
`inhouseidp/`). All code in namespace `runcpull::`.

```
modules/module/runcpull/
  inc/
    registry_client.hpp     // Docker Hub v2 API client
    manifest.hpp            // image index / manifest data types + parsers
    image_config.hpp        // OCI image config (Env, Cmd, Entrypoint, ...)
    layer_unpack.hpp        // gzip + tar + whiteout
    bundle_writer.hpp       // bundle/rootfs + stub config.json
    pull_orchestrator.hpp   // wires the above together
  src/
    registry_client.cpp
    manifest.cpp
    image_config.cpp
    layer_unpack.cpp
    bundle_writer.cpp
    pull_orchestrator.cpp
    main.cpp                // CLI: arg parse, exit codes
  test/
    fixtures/               // canned manifests + tiny tarballs
```

### 4a. Reused, not rewritten

| Capability | Source | Notes |
|---|---|---|
| HTTPS GET (TLS verify) | `sso::HttpClient` in `modules/module/sso/inc/sso_http_client.hpp` | Extend, not fork — see §4b |
| JSON | `modules/module/thirdparty/json.hpp` (`nlohmann::json`) | Manifests are 10–50 KB; in-memory parse is fine |
| SHA-256 | `EVP_sha256` / `EVP_DigestInit/Update/Final` (already used in `mongodbc.cpp`) | OpenSSL is already linked |
| gzip | zlib (`inflate` streaming API) — already linked into `uniservice`/`wsdbagent` | Use directly; no wrapper needed |
| Tar | New code in `layer_unpack.cpp`. POSIX ustar + pax extension headers (for long paths). | Don't pull in libarchive — single-purpose ~400 LOC of C++ is smaller and statically linkable |
| URL parse / form encode | `sso::encode_form` + a small parser in `registry_client.cpp` | We need scheme/host/path/query/port from `docker.io/library/mongo:4.4` and from CDN redirect Location headers |

### 4b. Extension to `sso::HttpClient`

The existing client (`modules/module/sso/inc/sso_http_client.hpp`)
is ~200 LOC and does GET + form-encoded POST against a fixed
`Accept: application/json`. We extend it (no fork — SSO benefits
immediately) with three additive features:

1. **Custom request headers** — new optional `headers` map argument
   on `get`. Used for `Authorization: Bearer …` and the multi-value
   `Accept:` header the registry needs (`oci.image.index.v1+json,
   docker.distribution.manifest.list.v2+json,
   oci.image.manifest.v1+json, docker.distribution.manifest.v2+json`).
2. **Redirect following** — up to N (default 5) hops on
   301/302/307/308. Docker Hub serves manifests itself but redirects
   blob GETs to a CDN (CloudFront); the CDN does **not** want the Hub
   bearer token, so the redirect-follower drops `Authorization` on
   cross-origin hops (same policy curl / browsers use).
3. **Streaming body callback** — overload `get` that takes a
   `std::function<void(const char*, size_t)>` instead of returning
   `std::string`. Layer blobs can be 100+ MB; we pipe straight into
   the gzip inflater + SHA-256 digester without buffering. Existing
   string-returning overload kept for manifests / token JSON.

These are purely additive — the SSO module's two existing callers
(`OidcProvider::discover` and the token POST) keep their signatures.

## 5. Wire-level: Docker Hub v2 flow

Per pull (sequence, all over HTTPS):

1. **Probe**: `GET https://registry-1.docker.io/v2/`
   → 401 with `Www-Authenticate: Bearer realm="https://auth.docker.io/token",service="registry.docker.io"`.
2. **Anonymous token**:
   `GET https://auth.docker.io/token?service=registry.docker.io&scope=repository:<name>:pull`
   → 200 `{ "token": "…", "expires_in": 300, … }`. Used for *all*
   subsequent requests to `registry-1.docker.io` for this image.
3. **Manifest**:
   `GET https://registry-1.docker.io/v2/<name>/manifests/<reference>`
   with the multi-Accept header. Two response shapes:
   - **Image index / manifest list** — `mediaType` is
     `…image.index.v1+json` or `…manifest.list.v2+json`. Parse, pick
     the entry whose `platform.os == "linux"` and
     `platform.architecture` matches the host (`uname -m` →
     `amd64` / `arm64` / `arm/v7`). Refuse if no match (exit 3, "no
     manifest for this platform"). Then go back and do step 3 again
     with the chosen digest as `<reference>`.
   - **Image manifest** — `mediaType` is `…image.manifest.v1+json` or
     `…manifest.v2+json`. Has `config.digest` + `layers[*].digest`.
4. **Config blob**:
   `GET https://registry-1.docker.io/v2/<name>/blobs/<config-digest>`
   → JSON, the OCI image config (`{ Env, Cmd, Entrypoint, …rootfs… }`).
   Hash the bytes; refuse if SHA-256 ≠ `<config-digest>`.
5. **Layer blobs** (in order):
   `GET https://registry-1.docker.io/v2/<name>/blobs/<layer-digest>`
   → 307 redirect to a CDN URL (drop bearer on the follow). Stream
   the body through (a) gzip inflate (b) SHA-256 digester (c) the tar
   parser. Refuse if SHA-256 ≠ `<layer-digest>`.

Reference normalisation:
- `mongo:4.4` → `docker.io/library/mongo:4.4` (Hub's "library"
  namespace for single-name refs)
- `naushada/xpmile-wsdbagent` → `docker.io/naushada/xpmile-wsdbagent:latest`
- Digest refs (`naushada/foo@sha256:…`) — parsed but pinned;
  skip step 3's manifest-list fan-out.

## 6. Layer extraction & whiteouts

### 6a. Merge model — flat `rootfs/`, no overlay at runtime

xpmile-pull does **not** mount layers; it merges them into one flat
directory at pull time. An OCI image is a list of compressed tar
blobs (the "layers") whose order matters — each adds, overwrites, or
deletes paths on top of the previous. The pull pipeline applies them
sequentially into a single `bundle/rootfs/` tree:

```
fetch token              fetch manifest (or list → platform-pick)
        │                          │
        ▼                          ▼
fetch image config (SHA-256 verify, parse env/cmd/entrypoint)
        │
        ▼
for each layer in declared order:
    GET blob   →   GzipInflater   →   TarReader
                                            │
                                            ▼
                                   LayerUnpacker:
                                     regular file  → write at <rootfs>/<path>
                                     whiteout      → delete earlier-layer path
                                     opaque whiteout → clear earlier-layer dir
        │
        ▼
write bundle/config.json from image config (operator overwrites in §5
of operator-runc.md with the real runtime spec).
```

After the last layer is applied, `bundle/rootfs/` is the merged
filesystem. `runc run --bundle <dir>` pivots into that tree and
execs the configured args. There is **no overlayfs**, **no FUSE**,
**no layer cache at runtime** — the layers existed only as a
transient construct during the pull.

That's a deliberate trade-off vs docker/podman:

| Concern | Docker / podman | xpmile-pull + runc |
|---|---|---|
| Disk per container | layers shared via the overlay graph driver | one merged `rootfs/` per bundle (hardlink-cloned with `cp -al` for multi-instance) |
| Kernel feature | needs `overlayfs` | no overlay, no FUSE, no graph driver |
| Pull → ready | pull → graph driver registers layers | pull → write flat rootfs |
| Multi-instance same image | overlay clones cost zero extra disk | hardlink clone via `cp -al` — same inodes, separate dentries (see operator-runc.md §6) |
| Layer-level update | swap a single layer in the overlay | re-pull + re-merge (`xpmile-pull --force`) |

We picked flat-merge because the target host is a 1 GB Pi 3B with
2 bundles (mongo + 2× wsdbagent). Overlay graph drivers are tuned for
"100 containers off one base image" — they cost overlay-mount metadata
and a kernel feature we'd rather not depend on. With three running
containers, `cp -al` hardlink-clone hits the same disk-sharing target
with no graph driver in the loop. The cost is that a layer update
re-pulls the whole image (~50 MB for wsdbagent), not just the changed
layer — acceptable when image updates are weekly, not per-deploy.

### 6b. Whiteout mechanics

The OCI whiteout spec ([`image-spec/layer.md`](https://github.com/opencontainers/image-spec/blob/main/layer.md))
is the only non-obvious part of tar extraction. Three cases:

1. **Regular entry** `path/foo` → write to `bundle/rootfs/path/foo`,
   honouring tar header mode (mask off setuid/setgid — we don't ship
   suid for untrusted images), uid/gid (clamped — see rootless
   below), mtime, symlinks (`symlinkat`), hardlinks (`linkat`).
2. **Whiteout** `path/.wh.foo` → delete `bundle/rootfs/path/foo` from
   earlier layers; do **not** write the `.wh.foo` file itself.
3. **Opaque whiteout** `path/.wh..wh..opq` → `rm -rf` everything
   under `bundle/rootfs/path/` from earlier layers, then continue
   applying this layer's entries normally.

Hardening invariants:
- Reject any entry name containing `..` segments after normalisation.
- Reject absolute paths (`/etc/passwd`); strip leading `/`.
- Reject symlinks whose target escapes the rootfs (resolve and check).
- Don't open through symlinks during extract; use `O_NOFOLLOW` on the
  parent dir descriptor and `mkdirat` / `openat` from a held fd.
- Cap max entry size and total bundle size from CLI (default 4 GB) to
  prevent disk-fill from a malicious manifest.

Rootless uid/gid policy (matches `umoci --rootless`):
- All `uid`/`gid` from tar headers → mapped to the **caller's**
  uid/gid on disk. Inside the runc container the user namespace
  mapping in `config.json`'s `linux.uidMappings` puts those back to
  the image's intended values. This is the same policy umoci uses and
  the same one the existing `operator-runc.md` §12 troubleshooting
  (`chown -R 100998`) is referring to.

## 7. Build & artifact

Add to root `CMakeLists.txt`, after the `wsdbagent` block (lines
68–79 of the current file):

```cmake
add_executable(xpmile-pull
    modules/module/runcpull/src/main.cpp
    modules/module/runcpull/src/registry_client.cpp
    modules/module/runcpull/src/manifest.cpp
    modules/module/runcpull/src/image_config.cpp
    modules/module/runcpull/src/layer_unpack.cpp
    modules/module/runcpull/src/bundle_writer.cpp
    modules/module/runcpull/src/pull_orchestrator.cpp
    modules/module/sso/src/sso_http_client.cpp)         # reused
target_include_directories(xpmile-pull PRIVATE
    modules/module/runcpull/inc
    modules/module/sso/inc
    modules/module/thirdparty)
target_link_libraries(xpmile-pull pthread ssl crypto z)
# NO ACE, NO mongodbcxx, NO xmlsec — that's the whole point.
```

Estimated binary size: ~3–5 MB stripped, dynamically linked against
`libssl`/`libz`. Those ship on every Debian/Ubuntu by default. We
deliberately do **not** static-link OpenSSL: it's a CVE liability we
want the OS package manager to own.

CI: extend `.github/workflows/publish-images.yml` with a new
`runc-bundle` job that runs after `bootstrap` + `test`:

1. Cross-builds `xpmile-pull` for `linux/amd64` and `linux/arm64`
   inside `docker.io/naushada/xpmile-cpp-builder:bootstrap` (the
   bootstrap image already has cmake + g++ + openssl-dev + zlib-dev).
2. Assembles the tarball layout (see §8).
3. On a tag push (matches the existing release pattern — see commit
   `daa666e`, the `VERSION` file + `install-agent.sh` auto-pin),
   uploads each tarball as a GitHub Release asset.
4. Pinned tag derivation reuses the same `VERSION` reading as
   `install-agent.sh` so a `v1.2.0` tag produces
   `xpmile-runc-bundle-v1.2.0-{amd64,arm64}.tar.gz`.

## 8. Distribution: tarball layout

```
xpmile-runc-bundle-v1.2.0-arm64/
  VERSION                       # "v1.2.0"
  bin/xpmile-pull               # the binary
  templates/
    config.mongo.json           # copy of operator-runc.md §5a
    config.wsdbagent.json       # copy of §5b
    config.wsdbagent-idp.json   # copy of §5c
    systemd/
      xpmile-mongo.service              # copy of §7a
      xpmile-wsdbagent.service          # copy of §7b
      xpmile-wsdbagent-idp.service      # copy of §7b (idp variant)
      xpmile-certs.path                 # copy of §8
      xpmile-certs-rotate.service       # copy of §8
    mongo-init.js               # copy of docker/mongo-init.js
  install.sh                    # idempotent installer (see below)
  README.md                     # quickstart + link to docs/operator-runc.md
```

`install.sh` (~150 LOC) is what the operator actually runs after
untar:

1. Preflight: check runc ≥ 1.1, jq present,
   `kernel.unprivileged_userns_clone=1`.
2. Read interactive answers (or env vars: `SERVER_HOST`,
   `IDP_SERVER_HOST`, `WSDBAGENT_TAG`, `MONGO_TAG`).
3. `mkdir -p /var/lib/xpmile/{bundles,oci-cache,mongo-data}`.
4. `bin/xpmile-pull docker.io/library/mongo:${MONGO_TAG} --to /var/lib/xpmile/bundles/mongo`
5. `bin/xpmile-pull docker.io/naushada/xpmile-wsdbagent:${WSDBAGENT_TAG} --to /var/lib/xpmile/bundles/wsdbagent-marvel`
6. `cp -al /var/lib/xpmile/bundles/wsdbagent-marvel /var/lib/xpmile/bundles/wsdbagent-idp`
7. Render the four `config.json` templates with `${SERVER_HOST}` etc.
   substituted (envsubst) into the bundle dirs.
8. Install the systemd units to `/etc/systemd/system/`, with `pi` /
   `$HOME` substituted.
9. `systemctl daemon-reload && systemctl enable --now xpmile-mongo xpmile-wsdbagent xpmile-wsdbagent-idp xpmile-certs.path`.
10. Print "Done. Verify with `systemctl status xpmile-*`."

Top-level repo gets a sibling to `install-agent.sh`:

```
install-agent-runc.sh           # one-curl: detects arch, downloads
                                # the matching tarball from the
                                # latest GitHub release, untars,
                                # runs install.sh
```

Operator's full install becomes one line:

```sh
curl -sSf https://raw.githubusercontent.com/naushada/xpmile/main/install-agent-runc.sh | bash
```

## 9. `docs/operator-runc.md` changes (after impl)

| Section | Change |
|---|---|
| §2 Prerequisites | Drop `skopeo umoci` from apt-get list. Add a "Download the bundle" step pointing at `install-agent-runc.sh` (or manual: download + untar). |
| §4 Image fetch | Replace skopeo + umoci recipes with three `xpmile-pull` invocations. Drop §4c's separate "pinning" subsection — it's now just a different tag arg. |
| §5 OCI bundle config | Keep all three template `config.json` files but add a note: "These are also shipped in the tarball at `templates/config.*.json`; `install.sh` substitutes `${SERVER_HOST}` etc." |
| §6 Per-instance bundle clone | Keep — `cp -al` is still how we share rootfs between marvel + idp. |
| §7–§9 systemd | Unchanged (templates moved to tarball; doc remains the canonical reference). |
| §10 Update procedure | Replace umoci re-unpack with `xpmile-pull --to /tmp/wsdbagent-fresh` + `cp -al`. |
| §12 Troubleshooting | Add three entries: HTTP 429 from registry (back off), digest mismatch (network corruption — retry), arch mismatch (no manifest for this platform). |

## 10. Security invariants

Pin these in code review — don't weaken in v2:

- **TLS verification on**. Same CA bundle path / hostname verify as
  `sso::HttpClient` does for OIDC. No `--insecure` flag in v1.
- **SHA-256 digest verification on every blob**. Mismatch → tear down
  bundle tempdir, exit 4. Never write a verified file outside the
  tempdir until the full pull succeeds (atomic rename at end).
- **Path traversal rejection** in tar names (`..` segments, absolute
  paths, escaping symlink targets).
- **No setuid/setgid bits** on extracted files. Mask
  `0177777 & ~07000` before chmod.
- **`Authorization` header dropped on cross-origin redirects.** Hub
  bearer is for `registry-1.docker.io` only; the CDN doesn't want it.
- **Anonymous-only auth in v1.** No credentials read from env, no
  `~/.docker/config.json` parse. Adding private-repo support later
  means adding a dedicated `--auth-token-file` flag, not silently
  picking up shell env.

## 11. Failure modes & UX

| Exit code | Class | Examples |
|---|---|---|
| 0 | OK | Pull succeeded; bundle written. |
| 1 | Transport / network | DNS, connect refused, TLS handshake, body read EOF. |
| 2 | Auth | 401 / 403 from token endpoint; refused scope. |
| 3 | Manifest | Unsupported `mediaType`, no platform match, malformed JSON. |
| 4 | Digest mismatch | Config or any layer hash ≠ digest. Highest-trust failure — clearer message than "transport error". |
| 5 | Unpack | Disk full, path traversal rejection, tar parse error. |
| 6 | Argv / usage | Bad `--to`, missing required arg. |

The CLI writes a one-line summary on success
(`"Pulled mongo:4.4 (linux/arm64), 7 layers, 198 MB, into /var/lib/xpmile/bundles/mongo"`)
and a progress indicator during layer downloads (bytes / total, no
TTY-only ANSI tricks — operators ssh).

## 12. Critical files

To implement (new):
- `modules/module/runcpull/` — all new code (~1500 LOC C++ + tests)
- `install-agent-runc.sh` — new top-level installer
- `.github/workflows/publish-images.yml` — new `runc-bundle` job

To modify (additive only):
- `CMakeLists.txt` — new `xpmile-pull` target (~10 lines)
- `modules/module/sso/inc/sso_http_client.hpp` + `.cpp` — three
  additive features (headers, redirects, streaming callback). SSO's
  existing call sites unchanged.
- `docs/operator-runc.md` — replace §2/§4/§10 contents per the table
  in §9 above. §5–§9 keep the templates as the canonical reference.
- `docs/operator-pi3b.md` — small forward-reference: "if you're on
  runc, see operator-runc.md; the puller-bundle install is now the
  recommended way."

Untouched (deliberate — out of scope):
- `modules/module/wsdbagent/`, `modules/module/inhouseidp/`, anything
  in the cloud-side uniservice path. The puller is a host-side tool
  with zero overlap with the running services.

## 13. Verification (post-implementation)

Manual:
1. On macOS dev box, build `xpmile-pull` via the existing builder
   image; smoke-test against `docker.io/library/alpine:3.19` (smallest
   public image) and confirm `bundle/rootfs/etc/alpine-release`
   contents match what `skopeo copy + umoci unpack` produces.
2. On a Pi 3B (arm64) running Bookworm, run
   `install-agent-runc.sh` end-to-end; confirm all three services come
   up via `systemctl status xpmile-*`, and the cert-rotation
   `PathChanged` flow still restarts both agents within ~5 s of
   `./run-agent.sh refresh-certs`.
3. Confirm the binary's dynamic deps: `ldd ./xpmile-pull` should show
   only libc + libssl + libcrypto + libz (no ACE, no mongo).

Automated (the TDD plan, written separately after this design is
signed off):
- Unit tests under `modules/module/runcpull/test/` using the existing
  GTest harness. `IHttpClient` mock (from `sso::IHttpClient`) feeds
  canned manifests + small tarballs from `fixtures/`. Test list will
  go into `runc-pull-tdd-plan.md` and be the next deliverable.
- Adds `runcpull_test` source files into `test/CMakeLists.txt` next
  to the existing module test groups.

CI:
- The new `runc-bundle` job's success on a PR is the gate. End-to-end
  tarball assembly + cross-arch build runs on every push; release
  upload only on tag push.

## 14. Follow-ups (not v1)

- **Private repos**: `--auth-token-file` flag; or
  `~/.docker/config.json` parser. Needed once we ship private images.
- **Non-Hub registries**: factor `RegistryClient` so `auth_realm` and
  `service` are per-host. Adds quay.io / ghcr.io with ~50 LOC each.
- **Layer cache** (`--cache-dir`): content-addressable by digest;
  shared across multiple `xpmile-pull` invocations. Cuts repeat-pull
  time to near zero (the bundle-update path in `operator-runc.md`
  §10 re-downloads ~60 MB today; with cache it'd be only the new
  layer).
- **`xpmile-pull verify <bundle>`** subcommand — re-walk the rootfs
  and recompute digests, useful for "is this bundle still pristine"
  audits.
