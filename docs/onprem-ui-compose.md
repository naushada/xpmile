# Running the On-Prem UI locally with Podman Compose

`docker-compose.onprem.yml` defines three services:

| Service | What it is | Port |
|---------|-----------|------|
| `mongodb` | MongoDB | internal only |
| `app` | C++ backend + Angular UI | 8080 |
| `onprem-ui` | Java/Vaadin on-prem UI | 8090 |

`onprem-ui` has **no `depends_on`** — it can be started alone against any backend (local or remote).

> **All commands must be run from the `xpmile/` repo root.**

---

## Scenario A — On-prem UI only, backend on Heroku

Builds and starts only the Vaadin UI container. The C++ backend is not touched.

`XPMILE_BACKEND_BASE_URL` is set in `.env` — podman-compose picks it up automatically.

```sh
cd xpmile
podman-compose -f docker-compose.onprem.yml up --build onprem-ui
```

Open **http://localhost:8090** — logs in against your live Heroku data.

To override the URL without editing `.env`:
```sh
XPMILE_BACKEND_BASE_URL=http://localhost:8080 podman-compose -f docker-compose.onprem.yml up --build onprem-ui
```

---

## Scenario B — Full local stack (MongoDB + backend + on-prem UI)

Builds all three services. First run takes 30–40 min (C++ + Maven). Subsequent runs use the cache.

```sh
cd xpmile
podman-compose -f docker-compose.onprem.yml up --build
```

- Backend + Angular UI → **http://localhost:8080**
- On-prem Vaadin UI   → **http://localhost:8090**

---

## Common operations

```sh
# Start in background
podman-compose -f docker-compose.onprem.yml up --build -d

# Rebuild on-prem UI only (fast after first build)
podman-compose -f docker-compose.onprem.yml build onprem-ui
podman-compose -f docker-compose.onprem.yml up onprem-ui

# Follow logs (all services)
podman-compose -f docker-compose.onprem.yml logs -f

# Follow on-prem UI logs only
podman-compose -f docker-compose.onprem.yml logs -f onprem-ui

# Stop (data preserved)
podman-compose -f docker-compose.onprem.yml down

# Stop and delete MongoDB data
podman-compose -f docker-compose.onprem.yml down -v
```

---

## Changing ports

| Variable | Default | What it changes |
|----------|---------|----------------|
| `HOST_PORT` | `8080` | Host port for the backend |
| `ONPREM_PORT` | `8090` | Host port for the on-prem UI |

```sh
ONPREM_PORT=9090 podman-compose -f docker-compose.onprem.yml up onprem-ui
```

---

## Note

All commands use `podman-compose`. If you have `docker-compose` installed instead, the commands are identical — just substitute `docker-compose` for `podman-compose`.
