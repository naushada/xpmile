# Running the On-Prem UI locally with Podman Compose

`docker-compose.onprem.yml` starts the full local stack:

| Container | What it is | Port |
|-----------|-----------|------|
| `xpmile-mongo` | MongoDB | internal only |
| `xpmile-app` | C++ backend + Angular UI | 8080 |
| `xpmile-onprem-ui` | Java/Vaadin on-prem UI | 8090 |

All three share the `xpmile-net` bridge network so the on-prem UI reaches the backend by service name (`http://app:8080`).

> **All commands must be run from the repo root (`xpmile/`), not from inside `onprem/`.**

---

## Prerequisites

- `podman` and `podman-compose` installed
- Repo cloned at `xpmile/`

---

## Quick start

```sh
cd xpmile
podman-compose -f docker-compose.onprem.yml up --build
```

- Backend + Angular UI → **http://localhost:8080**
- On-prem Vaadin UI   → **http://localhost:8090**

First run takes 30–40 min (C++ + Maven build). Subsequent runs reuse the cache.

---

## Common operations

All commands below assume you are in the `xpmile/` repo root.

```sh
# Start everything (build if needed)
podman-compose -f docker-compose.onprem.yml up --build

# Start in background
podman-compose -f docker-compose.onprem.yml up --build -d

# On-prem UI only (backend already running via ./run.sh start)
podman-compose -f docker-compose.onprem.yml up --build onprem-ui

# Rebuild on-prem UI only (fast — Maven cache warm after first build)
podman-compose -f docker-compose.onprem.yml build onprem-ui
podman-compose -f docker-compose.onprem.yml up onprem-ui

# Follow logs
podman-compose -f docker-compose.onprem.yml logs -f
podman-compose -f docker-compose.onprem.yml logs -f onprem-ui

# Stop (data preserved)
podman-compose -f docker-compose.onprem.yml down

# Stop and delete MongoDB data
podman-compose -f docker-compose.onprem.yml down -v
```

---

## Pointing the on-prem UI at Heroku instead of local backend

Run this from the `xpmile/` repo root as a single command:

```sh
XPMILE_BACKEND_BASE_URL=https://marvel-3a78bd953f5f.herokuapp.com podman-compose -f docker-compose.onprem.yml up --build onprem-ui
```

Only the `onprem-ui` service starts; MongoDB and `app` are skipped.

---

## Changing ports

| Variable | Default | What it changes |
|----------|---------|----------------|
| `HOST_PORT` | `8080` | Host port for the backend |
| `ONPREM_PORT` | `8090` | Host port for the on-prem UI |

```sh
ONPREM_PORT=9090 podman-compose -f docker-compose.onprem.yml up -d
```

---

## Note

All commands use `podman-compose`. If you have `docker-compose` installed instead, the commands are identical — just substitute `docker-compose` for `podman-compose`.
