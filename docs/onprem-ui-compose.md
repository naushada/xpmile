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

## Build prerequisites (already in place)

The following issues were encountered and fixed during initial bring-up. Documented here so the build is understood end-to-end:

| Issue | Root cause | Fix |
|-------|-----------|-----|
| `Alignment.BOTTOM` compile error | `FlexComponent.Alignment` has no `BOTTOM` in Vaadin 24 | Changed to `BASELINE` |
| WireMock test failure | `wireMock.port()` called lazily inside `getBaseUrl()`, hitting a stopped server | Port captured eagerly in `setUp()` |
| Excel fixtures not found | Files written to `src/test/resources/` after Maven resource processing; never on classpath | Write directly to `target/test-classes/fixtures/` via system property |
| `isAutoGenerate` serialised as `autoGenerate` | Jackson strips `is` prefix from boolean getters | `@JsonProperty("isAutoGenerate")` added to model fields |
| Tomcat context failed to start | `vaadin.allowed-packages` is not a valid Vaadin 24 property | Removed from `application.properties` |
| Vaadin frontend not built | `production` profile was missing `build-frontend` goal | Profile added with both `prepare-frontend` and `build-frontend` |
| Theme directory missing | `@Theme("onprem")` requires `frontend/themes/onprem/` to exist | Created with `theme.json` (parent: lumo) and `styles.css` |
| Theme not found in Docker | Dockerfile only copied `src/`; `frontend/` was never in the image | Added `COPY frontend ./frontend` to Dockerfile builder stage |
| Login — "backend unavailable" | Backend returns `{"cause":"Invalid Credentials","error":404}` on bad credentials, not `[]`; `RestTemplate` failed to deserialize it as `Account[]` throwing `HttpMessageNotReadableException` caught as "unavailable" | `AuthService` now parses raw JSON as `JsonNode` first — array = success, object = reads `cause` field |
| Login — no request reaching Heroku | Backend requires client identification headers on every request | `RestTemplate` interceptor added — injects `onprem-ui: onprem-ui` and `client: onprem-xpmil-v.0.1` on all outgoing calls |

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
