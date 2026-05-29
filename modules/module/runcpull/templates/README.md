# xpmile-runc-bundle — operator quick-start

This tarball is everything you need to bring up the xpmile on-prem agent
stack on a memory-constrained host (Pi 3B class) without `docker`,
`podman`, `skopeo`, or `umoci`. Just `runc` + `systemd`.

## Prerequisites

- Debian Bookworm / Raspberry Pi OS Bookworm / Ubuntu 22.04+ on arm64 or
  amd64. `cgroups v2` (the default since Bookworm). glibc 2.31+ (Bullseye
  or newer) — the bundled `xpmile-pull` binary expects this glibc level.
- `runc` >= 1.1 (`sudo apt install runc`)
- `sudo` access for the user running the installer.

> The bundled `bin/xpmile-pull` is a tiny shell wrapper. It sets
> `LD_LIBRARY_PATH=<bundle>/lib` and execs `bin/xpmile-pull.real` — so the
> binary's libssl1.1 + libACE deps travel with the tarball and don't
> require apt-installing anything beyond runc + glibc. If you need to
> debug the linkage, run `bin/xpmile-pull.real --version` directly and
> check `ldd` output.

## Install

```sh
export SERVER_HOST="marvel-XXXXXXXX.herokuapp.com"
# Optional second Heroku app:
export IDP_SERVER_HOST="idp-XXXXXXXX.herokuapp.com"

# Cert family — usually generated on the cloud side by
# `./deploy-heroku.sh extract-agent-certs`, then scp'd here:
export CERTS_DIR="$HOME/xpmile/certs/cloud-issued/innertls"

bash install.sh
```

## What it does

1. Uses `bin/xpmile-pull` to fetch `mongo:4.4` and
   `naushada/xpmile-wsdbagent:latest` from Docker Hub straight into runc
   OCI bundles under `/var/lib/xpmile/bundles/`. No docker daemon
   involved.
2. Renders the per-bundle `config.json` files from `templates/` with
   your host's substitutions (`__INSTALL_DIR__`, `__SERVER_HOST__`,
   `__CERTS_DIR__`, …).
3. Installs systemd units (`xpmile-mongo.service`,
   `xpmile-wsdbagent.service`, optional `xpmile-wsdbagent-idp.service`,
   and a `xpmile-certs.path` watcher that restarts the agents on
   cert-family rotation).
4. Enables + starts everything.

## Verify

```sh
sudo systemctl status xpmile-mongo xpmile-wsdbagent
sudo journalctl -u xpmile-wsdbagent -f
```

## Update procedure

Re-running `bash install.sh` is idempotent. It re-pulls (with `--force`)
and re-renders the configs.

To pull a specific build:

```sh
WSDBAGENT_TAG=sha-e1353c1 bash install.sh
```

## See also

- The runc install design that this tarball ships with:
  `docs/operator-runc.md` in the xpmile repo.
- The runc-pull tool itself:
  `docs/design/runc-pull/` (design, TDD plan, USAGE).
