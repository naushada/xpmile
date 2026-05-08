# Heroku Deployment Guide

App name used in these examples: **`marvel`**

---

## Prerequisites

- [Heroku CLI](https://devcenter.heroku.com/articles/heroku-cli) installed and logged in (`heroku login`)
- [Podman](https://podman.io/) installed (Docker works identically — substitute `docker` for `podman` throughout)
- App already created on Heroku: `heroku create marvel` (or via the dashboard)

---

## Container stack (Heroku)

Heroku's container runtime uses the `web` process type. The image is pushed to
Heroku's private registry and released with the CLI. No `docker-compose.yml` is
used on Heroku — MongoDB must be provided as an add-on or external service.

```
Mac (ARM64) ──podman build --platform linux/amd64──► Heroku registry
                                                          │
                                                   heroku container:release
                                                          │
                                                   Heroku dyno (linux/amd64)
                                                   runs uniservice + Angular
```

---

## Step-by-step

### 1. Log in to the Heroku container registry

Heroku CLI's `container:login` requires the Docker socket. If only Podman is
available, authenticate directly:

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

### 3. Build the image for linux/amd64

Cross-compile on Apple Silicon (or any ARM host) with `--platform`:

```sh
podman build \
  --platform linux/amd64 \
  -f docker/Dockerfile \
  -t registry.heroku.com/marvel/web \
  .
```

This is a multi-stage build (cpp-builder → ui-builder → runtime) and takes
30–60 minutes the first time. Subsequent builds reuse cached layers.

To force an Angular rebuild without invalidating the C++ layer:

```sh
podman build \
  --platform linux/amd64 \
  -f docker/Dockerfile \
  --build-arg UI_BUST=$(date +%s) \
  -t registry.heroku.com/marvel/web \
  .
```

### 4. Push the image

```sh
podman push registry.heroku.com/marvel/web
```

### 5. Set config vars

Heroku injects `$PORT` automatically. The app reads all other flags from `ARGS`:

```sh
heroku config:set \
  ARGS="--remote-db --mongo-db-name xpmile --server-worker 5" \
  --app marvel
```

**Heroku mode** (no dedicated mTLS port — Heroku terminates TLS at the edge):
```
--remote-db --mongo-db-name xpmile --server-worker 5
```

Email credentials (optional):
```sh
heroku config:set \
  ARGS="--remote-db --mongo-db-name xpmile --server-worker 5 \
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
podman build --platform linux/amd64 -f docker/Dockerfile \
  -t registry.heroku.com/marvel/web .

# 3. Push
podman push registry.heroku.com/marvel/web

# 4. Release
heroku container:release web --app marvel

# 5. Tail logs
heroku logs --tail --app marvel
```

---

## Environment variables injected by Heroku

| Variable | Set by | Purpose |
|---|---|---|
| `PORT` | Heroku (automatic) | TCP port the dyno must listen on |
| `ARGS` | `heroku config:set` | All CLI flags passed to `uniservice` |

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
| **mLab / other add-ons** | Same as Atlas — URI via `ARGS`. |

### wsdbagent on Heroku (no mTLS)

Heroku terminates TLS at its edge router. The agent connects with standard
WSS; no client certificate is needed.

On the MongoDB machine:

```sh
podman run -d --name wsdbagent \
  --network host \
  -e ARGS="--server-host marvel.herokuapp.com \
           --mongo-db-uri mongodb://root:changeme@localhost:27017 \
           --mongo-db-name xpmile" \
  wsdbagent
```

`--network host` allows the agent to reach `mongod` on `localhost:27017`.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `podman login` fails with 401 | Token expired | Re-run `heroku auth:token \| podman login ...` |
| Push fails with "manifest unknown" | Wrong tag format | Tag must be exactly `registry.heroku.com/<app>/web` |
| Dyno crashes immediately | Wrong `PORT` | Ensure `CMD` uses `$PORT`; check `heroku logs` |
| `--platform` build very slow | QEMU emulation on ARM | Normal for cross-compile; cached on repeat builds |
| `uniservice: not found` in logs | Binary not copied | Confirm `Dockerfile` `COPY --from=cpp-builder` step succeeded |
