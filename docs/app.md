# Heroku Deployment Guide

App name used in these examples: **`marvel`**

> **Heads-up:** every push to `main` auto-deploys to Heroku via
> `.github/workflows/publish-images.yml` (bootstrap → test → uniservice
> + Heroku release). The flow documented below is the **manual escape
> hatch** — useful for hotfixes from a feature branch, offline deploys,
> or when CI is down. See `CLAUDE.md` → Continuous integration for the
> CI pipeline overview.

---

## Compose files

| File | Where it runs | What it does |
|---|---|---|
| `docker-compose.yml` | Dev machine | Local stack — MongoDB + app together |
| `docker-compose.heroku.yml` | Dev machine | Builds and pushes to Heroku registry |
| `docker-compose.agent.yml` | MongoDB machine | MongoDB + wsdbagent outbound to Heroku |

---

## Prerequisites

- [Heroku CLI](https://devcenter.heroku.com/articles/heroku-cli) installed and logged in (`heroku login`)
- [Podman](https://podman.io/) and [podman-compose](https://github.com/containers/podman-compose) installed
- App already created on Heroku: `heroku create marvel` (or via the dashboard)

---

## Container stack (Heroku)

`docker-compose.heroku.yml` defines the `web` service with `platform: linux/amd64`
and `image: registry.heroku.com/<app>/web`. `podman-compose` builds it;
`podman push --format=v2s2` pushes it (plain `podman-compose push` fails with
"unsupported manifest" on some Podman versions); `heroku container:release`
activates it on the dyno.

```
Mac (ARM64) ──podman-compose build (linux/amd64)──► local image
                                                          │
                                              podman push --format=v2s2
                                                          │
                                                   Heroku registry
                                                          │
                                                   heroku container:release
                                                          │
                                                   Heroku dyno (linux/amd64)
                                                   runs uniservice + Angular
```

---

## Step-by-step

### 1. Log in to the Heroku container registry

Heroku CLI's `container:login` requires the Docker socket. Authenticate with
Podman directly instead:

```sh
heroku auth:token | podman login \
  --username=_ \
  --password-stdin \
  registry.heroku.com
```

### 2. Set the Heroku stack to `container`

Only needed once per app:

```sh
heroku stack:set container --app marvel
```

### 3. Build the image

`docker-compose.heroku.yml` sets `platform: linux/amd64` automatically:

```sh
podman-compose -f docker-compose.heroku.yml build
```

This is a multi-stage build (cpp-builder → ui-builder → runtime) and takes
30–60 minutes the first time. Subsequent builds reuse cached layers.

To force an Angular rebuild without re-compiling C++:

```sh
UI_BUST=$(date +%s) podman-compose -f docker-compose.heroku.yml build
```

### 4. Push the image

`podman-compose push` fails with an "unsupported manifest" error against the
Heroku registry. Use `podman push` directly with the v2s2 manifest format:

```sh
podman push --format=v2s2 registry.heroku.com/marvel/web
```

### 5. Set config vars

Heroku injects `$PORT` automatically. The app reads all other flags from `ARGS`.

**Remote-DB mode** (MongoDB behind NAT via wsdbagent — recommended for Heroku):

```sh
heroku config:set \
  ARGS="--remote-db --server-worker 5" \
  --app marvel
```

With email credentials:

```sh
heroku config:set \
  ARGS="--remote-db --server-worker 5 \
        --email-from-name 'My App' \
        --email-from-id sender@gmail.com \
        --email-from-password secret" \
  --app marvel
```

### 6. Release

```sh
heroku container:release web --app marvel
```

### 7. Verify

```sh
heroku logs --tail --app marvel
heroku open --app marvel
```

---

## Full redeploy sequence (quick reference)

```sh
# 1. Authenticate
heroku auth:token | podman login --username=_ --password-stdin registry.heroku.com

# 2. Build
podman-compose -f docker-compose.heroku.yml build

# 3. Push  (must use --format=v2s2; podman-compose push doesn't work)
podman push --format=v2s2 registry.heroku.com/marvel/web

# 4. Release
heroku container:release web --app marvel

# 5. Tail logs
heroku logs --tail --app marvel
```

---

## Environment variables

| Variable | Set by | Purpose |
|---|---|---|
| `PORT` | Heroku (automatic) | TCP port the dyno must listen on |
| `ARGS` | `heroku config:set` | All CLI flags passed to `uniservice` |
| `HEROKU_APP` | shell / `.env` | App name used in compose file (default: `marvel`) |
| `UI_BUST` | shell | Cache-bust value to force Angular rebuild (default: `0`) |

The `Dockerfile` `CMD` is:
```
CMD /opt/xAPP/granada/uniservice --server-port $PORT $ARGS
```

---

## MongoDB on Heroku

Heroku does not provide a MongoDB add-on by default. Options:

| Option | Notes |
|---|---|
| **wsdbagent** (recommended) | MongoDB runs on your own machine behind NAT; `wsdbagent` connects outbound to the Heroku app's `/ws/db` WebSocket endpoint. No inbound ports needed. |
| **MongoDB Atlas** | Managed cloud MongoDB; pass the Atlas URI via `--mongo-db-uri` in `ARGS` (use local mode, not `--remote-db`). |

### wsdbagent on the MongoDB machine (Heroku path, no mTLS)

Heroku terminates TLS at its edge router — no client certificate is needed.

```sh
# On the MongoDB machine
cp .env.agent .env          # set SERVER_HOST=marvel-3a78bd953f5f.herokuapp.com
podman-compose -f docker-compose.agent.yml up --build -d
```

The agent connects outbound to `wss://marvel-3a78bd953f5f.herokuapp.com/ws/db` on port 443.
No inbound firewall rules are needed on the MongoDB machine.

See `docs/ws-db-agent.md` for full details including mTLS configuration and
running the agent locally for development.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `podman login` fails with 401 | Token expired | Re-run `heroku auth:token \| podman login ...` |
| Push fails with "unsupported manifest" | Wrong push command | Use `podman push --format=v2s2 registry.heroku.com/marvel/web` |
| Push fails with "manifest unknown" | Wrong tag format | Tag must be exactly `registry.heroku.com/<app>/web` |
| Dyno crashes immediately | Wrong `PORT` | Ensure `CMD` uses `$PORT`; check `heroku logs` |
| Build very slow on Apple Silicon | QEMU emulation for amd64 | Normal for first cross-compile; cached layers speed up repeats |
| `uniservice: not found` in logs | Binary not copied | Confirm `Dockerfile` `COPY --from=cpp-builder` step succeeded |
| Login times out (H12) with agent connected | DB response not received | Check wsdbagent logs for `ws_send failed`; check Heroku logs for `recv_n` errors |
