# xpmile

Logistics management platform — C++ backend (ACE + MongoDB) with an Angular UI, packaged as a multi-container Docker deployment.

---

## Quick start

```sh
git clone https://github.com/naushada/xpmile.git
cd xpmile
docker compose up --build
```

The app is served on `http://localhost:8080` by default.  
MongoDB listens on `localhost:27017` (remove that port mapping in `docker-compose.yml` if direct access is not needed).

---

## Running with Podman

[Podman](https://podman.io) is a daemonless, rootless container engine that is a drop-in replacement for Docker on this project. All `docker compose` commands have direct `podman compose` equivalents.

### Prerequisites

**macOS**

```sh
brew install podman podman-compose
podman machine init
podman machine start
```

**Linux (Fedora / RHEL / CentOS)**

```sh
sudo dnf install -y podman podman-compose
```

**Linux (Debian / Ubuntu)**

```sh
sudo apt-get install -y podman
pip3 install --user podman-compose
```

> Podman 4.0+ ships a built-in `podman compose` sub-command (backed by `podman-compose`). Either form works.

### Quick start

```sh
git clone https://github.com/naushada/xpmile.git
cd xpmile
podman compose up --build
```

The app is served on `http://localhost:8080` — identical to the Docker workflow.

### Command equivalents

| Docker | Podman |
|---|---|
| `docker compose up --build` | `podman compose up --build` |
| `docker compose up -d --build` | `podman compose up -d --build` |
| `docker compose down` | `podman compose down` |
| `docker compose down -v` | `podman compose down -v` |
| `docker compose logs -f app` | `podman compose logs -f app` |
| `docker compose ps` | `podman compose ps` |

### Force-recompiling the UI (cache busting)

```sh
UI_BUST=$(date +%s) podman compose up -d --build app
```

### Resetting passwords / volumes

```sh
podman compose down -v
podman compose up --build
```

### Key differences from Docker

- **No daemon required** — Podman runs containers directly as your user process. No background service to start or stop.
- **Rootless by default** — containers run without root privileges on the host. Ports ≥ 1024 work without any extra configuration.
- **macOS requires a VM** — `podman machine` manages a lightweight Linux VM (similar to Docker Desktop's VM). Run `podman machine start` before any `podman compose` command.
- **`podman-compose` vs Docker Compose** — `podman-compose` is a Python reimplementation and covers all features used by this project. If you hit a parsing edge-case, upgrade to the latest `podman-compose` with `pip3 install -U podman-compose`.

---

## Services

| Service | Image | Description |
|---|---|---|
| `mongodb` | `xpmile-mongo:latest` | MongoDB 7, auth enabled, seeded with a bootstrap admin account |
| `app` | `xpmile:latest` | C++ HTTP server + pre-built Angular UI |

---

## Authentication

MongoDB runs with authentication enabled. On first startup the init
script (`docker/mongo-init.js`) creates:

- A root admin user (used by the healthcheck only)
- An `xpmile` app user with `readWrite` on the `xpmile` database

### Default credentials

| Variable | Default | Purpose |
|---|---|---|
| `MONGO_ROOT_USER` | `root` | MongoDB root (internal only) |
| `MONGO_ROOT_PASS` | `changeme` | MongoDB root password |
| `MONGO_APP_USER` | `xpmile` | App database user |
| `MONGO_APP_PASS` | `xpmile_pass` | App database password |

### Overriding passwords

Create a `.env` file next to `docker-compose.yml` before the first `up`:

```sh
MONGO_ROOT_PASS=a-strong-root-password
MONGO_APP_PASS=a-strong-app-password
```

> **Important:** the init script only runs when the `mongo-data` volume
> is empty. If you change passwords after the first start, drop the
> volume first:
> ```sh
> docker compose down -v
> docker compose up --build
> ```

---

## Configuration reference

All variables can be set in `.env` or passed via `docker compose up -e`.

| Variable | Default | Description |
|---|---|---|
| `PORT` | `8080` | Port the app listens on inside the container |
| `HOST_PORT` | `8080` | Host port mapped to the container |
| `SERVER_WORKERS` | `5` | Number of MicroService worker threads |
| `MONGO_DB` | `xpmile` | MongoDB database name |
| `MONGO_POOL` | `20` | MongoDB connection pool size |
| `MONGO_ROOT_USER` | `root` | MongoDB root username |
| `MONGO_ROOT_PASS` | `changeme` | MongoDB root password |
| `MONGO_APP_USER` | `xpmile` | App database username |
| `MONGO_APP_PASS` | `xpmile_pass` | App database password |

---

## Project layout

The backend follows a **deep module** design — each concern lives in its
own self-contained directory under `modules/module/`, with `inc/` for the
public interface and `src/` for the implementation.

```
.
├── CMakeLists.txt                  Build entry point (uniservice + test suite)
├── test/                           googletest runner (main.cc + CMakeLists.txt)
├── modules/
│   └── module/
│       ├── email/                  SMTP email client (TLS, FSM-driven)
│       │   ├── inc/emailservice.hpp
│       │   ├── src/emailservice.cpp, emailservice_fsm.cpp
│       │   └── test/emailservice_test.hpp, emailservice_test.cc
│       ├── http/                   HTTP/1.1 request parser
│       │   ├── inc/http_parser.hpp
│       │   ├── src/http_parser.cpp
│       │   └── test/httpparser_test.hpp, httpparser_test.cc
│       ├── mongodb/                MongoDB client library (see README.md)
│       │   ├── inc/mongodbc.hpp
│       │   ├── src/mongodbc.cpp
│       │   ├── CMakeLists.txt
│       │   └── README.md
│       ├── oauth2/                 OAuth2 service (stub)
│       │   ├── inc/oauth2service.hpp
│       │   └── src/oauth2service.cpp
│       ├── thirdparty/             Vendored single-file libraries
│       │   └── json.hpp            nlohmann/json (header-only)
│       ├── webservice/             ACE reactor, HTTP server, request routing
│       │   ├── inc/webservice.hpp
│       │   ├── src/webservice.cpp, webservice_main.cpp
│       │   └── test/webservice_test.hpp, webservice_test.cc
│       └── whatsapp/               WhatsApp service (stub)
│           ├── inc/whatsapp_service.hpp
│           └── src/whatsapp_service.cpp
├── ui/                             Angular frontend
├── docker/
│   ├── Dockerfile                  Multi-stage build (C++ → UI → runtime)
│   ├── Dockerfile.mongo            Custom mongo:7 image with init script baked in
│   └── mongo-init.js               DB user creation + bootstrap admin document
└── docker-compose.yml
```

---

## Bootstrap admin account

The first `up` seeds the `xpmile.account` collection with an admin user:

```
accountCode:     admin
accountPassword: admin@123
```

Change this password through the application UI after first login.

---

## Troubleshooting

### `chown: changing ownership of '/proc/1/fd/*': Permission denied`

Seen on Amazon Linux 2 and other hosts where the Docker daemon does not
grant container processes access to `/proc/1`.  The mongodb service in
`docker-compose.yml` sets `user: "999:999"` (the UID/GID of the
`mongodb` system account inside the `mongo:7` image), which causes the
entrypoint to skip the `/proc/1/fd/*` chown entirely.  No action is
needed — this is already the default configuration.

### Build OOM kill (`cc1plus: Killed`) on memory-constrained hosts

`modules/module/webservice/src/webservice.cpp` is a large translation unit.
Building it with full parallelism (`-j$(nproc)`) can exhaust available
memory on machines with less than ~4 GB RAM per core.  The Dockerfile caps
the uniservice build at `-j2` and passes `-DCMAKE_CXX_FLAGS="-fconcepts"`
to suppress a GCC 9 concepts warning from
`modules/module/email/inc/emailservice.hpp`.  If builds still OOM,
reduce Docker's memory limit or set `make -j1` in the Dockerfile.

### `docker compose up` uses a stale image

Always pass `--build` after pulling or changing source files:

```sh
docker compose up --build
```

Running `docker compose up` without `--build` reuses the last built
image and will not pick up code or Dockerfile changes.

### `xpmile-mongo is unhealthy` / `UserNotFound: Could not find user "root"`

The MongoDB init scripts (`MONGO_INITDB_ROOT_USERNAME/PASSWORD` and
`mongo-init.js`) **only run when the data volume is empty**.  If the
`mongo-data` volume was created by an earlier run that predates
authentication being enabled, MongoDB reuses the existing data directory
and skips initialisation entirely — leaving the `root` user absent and
every healthcheck ping failing with `Authentication failed`.

Drop the stale volume and let MongoDB re-initialise from scratch:

```sh
docker compose down -v
docker compose up --build
```

> `down -v` removes the named volume.  All stored shipment data is lost,
> so only do this on a development instance or when you are prepared to
> re-seed.

### Obsolete `version` field warning

Docker Compose v2 ignores the top-level `version` field and prints a
warning if it is present.  The field has been removed from
`docker-compose.yml`; if you see the warning in a fork or older copy,
delete the `version: "..."` line.
