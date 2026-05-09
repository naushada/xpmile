# xpmile On-Prem UI

Java 17 / Vaadin 24 / Spring Boot 3 frontend for xpmile on-premises deployments.
Consumes the existing C++ REST backend — no backend changes required.

---

## Prerequisites

| Tool | Version |
|------|---------|
| Java | 17+ |
| Maven | 3.9+ |

---

## Running against your backend

### 1. Start your backend

Use the existing stack (make sure it is reachable from this machine):

```sh
# local stack
cd ..
./run.sh start

# or if backend is on another machine, note its IP/hostname
```

### 2. Configure the backend URL

The default is `http://localhost:8080`. Override it in one of three ways:

**Option A — environment variable (recommended for dev):**
```sh
export XPMILE_BACKEND_BASE_URL=http://<your-backend-host>:8080
mvn spring-boot:run
```

**Option B — command-line property:**
```sh
mvn spring-boot:run -Dspring-boot.run.arguments="--xpmile.backend.base-url=http://<host>:8080"
```

**Option C — edit `src/main/resources/application.properties`:**
```properties
xpmile.backend.base-url=http://<your-backend-host>:8080
```

### 3. Start the UI

```sh
cd onprem
mvn spring-boot:run
```

Open **http://localhost:8090** in your browser.

Log in with any account that exists in your backend (same credentials you use in the Angular UI).

---

## Running tests

```sh
cd onprem
mvn test
```

Tests run against a WireMock stub — no live backend needed.
The Excel fixtures for the bulk-parser tests are generated automatically before the test phase.

---

## Building a fat JAR (for distribution)

```sh
cd onprem
mvn package -Pproduction
```

Produces `target/onprem-1.0.0-SNAPSHOT.jar`. Deploy to any machine with Java 17:

```sh
java -jar onprem-1.0.0-SNAPSHOT.jar --xpmile.backend.base-url=http://<host>:8080
```

---

## Ports

| Service | Default port |
|---------|-------------|
| On-prem UI | 8090 |
| Backend (C++ uniservice) | 8080 |

Change the UI port: `--server.port=<port>`.

---

## Docs

- [Design doc](docs/design.md)
- [TDD plan](docs/tdd-plan.md)
