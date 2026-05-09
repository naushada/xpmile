# xpmile On-Prem UI — Design Document

## Overview

A standalone Java/Vaadin web application that provides the on-premises UI for xpmile. It consumes the existing C++ REST backend unchanged. No backend modifications are required.

**Stack:** Java 17, Vaadin 24 (Flow), Maven  
**Location:** `xpmile/onprem/`  
**Backend:** existing REST API at configurable `BASE_URL` (default `http://localhost:8080`)

---

## Running against your backend

### Prerequisites

| Tool | Version |
|------|---------|
| Java | 17+ |
| Maven | 3.9+ |

### 1. Start your backend

```sh
cd xpmile
./run.sh start
# or start it on a remote machine and note its IP
```

### 2. Configure the backend URL

The default is `http://localhost:8080`. `RestTemplate` supports both `http://` and `https://` — no code changes needed for either.

| Target | URL format |
|--------|-----------|
| Local stack | `http://localhost:8080` |
| On-prem remote machine | `http://<ip-or-hostname>:8080` |
| Heroku (deployed app) | `https://marvel-3a78bd953f5f.herokuapp.com` |

Override with one of:

**Option A — environment variable (recommended):**
```sh
# local
export XPMILE_BACKEND_BASE_URL=http://localhost:8080

# Heroku
export XPMILE_BACKEND_BASE_URL=https://marvel-3a78bd953f5f.herokuapp.com

mvn spring-boot:run
```

**Option B — command-line property:**
```sh
# Heroku
mvn spring-boot:run -Dspring-boot.run.arguments="--xpmile.backend.base-url=https://marvel-3a78bd953f5f.herokuapp.com"
```

**Option C — edit `application.properties`:**
```properties
# Heroku
xpmile.backend.base-url=https://marvel-3a78bd953f5f.herokuapp.com
```

> **Note:** Heroku uses a valid CA-signed TLS certificate so HTTPS works with the default JVM trust store. No extra SSL configuration is needed.

### 3. Start the UI

```sh
cd onprem
mvn spring-boot:run
```

Open **http://localhost:8090** and log in with any account from your backend.

### Running tests (no backend needed)

```sh
cd onprem
mvn test
```

WireMock stubs replace the backend. Excel fixtures are generated automatically before the test phase.

---

## Docker

### Build the image

```sh
cd onprem
docker build -t xpmile-onprem .
# or with podman:
podman build -t xpmile-onprem .
```

### Run against local backend

```sh
docker run -p 8090:8090 \
  -e XPMILE_BACKEND_BASE_URL=http://host.docker.internal:8080 \
  xpmile-onprem
```

> On Linux replace `host.docker.internal` with the host's actual IP (e.g. `172.17.0.1`).

### Run against Heroku backend

```sh
docker run -p 8090:8090 \
  -e XPMILE_BACKEND_BASE_URL=https://marvel-3a78bd953f5f.herokuapp.com \
  xpmile-onprem
```

### Run against on-prem remote machine

```sh
docker run -p 8090:8090 \
  -e XPMILE_BACKEND_BASE_URL=http://<backend-host>:8080 \
  xpmile-onprem
```

Open **http://localhost:8090** after the container starts.

---

## Docker Compose (local stack)

See `docker-compose.onprem.yml` in the repo root for a compose file that brings up both the xpmile backend (MongoDB + uniservice) and the on-prem UI together.

```sh
# Start everything (backend + on-prem UI)
docker-compose -f docker-compose.onprem.yml up --build

# On-prem UI only (backend already running separately)
docker-compose -f docker-compose.onprem.yml up --build onprem-ui

# Stop
docker-compose -f docker-compose.onprem.yml down
```

See `docs/onprem-ui-compose.md` for the full guide.

---

## Scope (Phase 1)

| # | View | API endpoints used |
|---|------|--------------------|
| 1 | Login | GET `/api/v1/account/account` |
| 2 | Shipment List | GET `/api/v1/shipment/shipping` |
| 3 | Create Single Shipment | POST `/api/v1/shipment/shipping` |
| 4 | Create Bulk Shipment | POST `/api/v1/shipment/bulk/shipping` |
| 5 | Modify Shipment | PUT `/api/v1/shipment/shipping` |
| 6 | Create Account | POST `/api/v1/account/account` |

Out of scope for Phase 1: inventory, tracking, reporting, email, third-party (Ajoul).

---

## Project structure

```
onprem/
├── pom.xml
└── src/
    └── main/
        ├── java/com/xpmile/onprem/
        │   ├── Application.java
        │   ├── config/
        │   │   └── BackendConfig.java          # BASE_URL, RestTemplate bean
        │   ├── service/
        │   │   ├── AuthService.java            # login validation
        │   │   ├── ShipmentService.java        # CRUD against /api/v1/shipment/*
        │   │   └── AccountService.java         # CRUD against /api/v1/account/account
        │   ├── model/
        │   │   ├── Account.java
        │   │   ├── Shipment.java
        │   │   ├── SenderInformation.java
        │   │   ├── ReceiverInformation.java
        │   │   └── ShipmentInformation.java
        │   └── ui/
        │       ├── LoginView.java              # route: ""  (public)
        │       ├── MainLayout.java             # app shell with nav drawer
        │       ├── shipment/
        │       │   ├── ShipmentListView.java   # route: "shipments"
        │       │   ├── CreateShipmentView.java # route: "shipments/create"
        │       │   ├── BulkShipmentView.java   # route: "shipments/bulk"
        │       │   └── ModifyShipmentView.java # route: "shipments/modify"
        │       └── account/
        │           └── CreateAccountView.java  # route: "accounts/create"
        └── resources/
            ├── application.properties
            └── META-INF/resources/             # static assets if needed
```

---

## Authentication model

- No server-side session store. On login, `AuthService` calls `GET /api/v1/account/account?userId=<u>&password=<p>`.
- On success the backend returns the full account object. The logged-in account is stored in a Vaadin `VaadinSession` attribute (`"account"`).
- All views except `LoginView` extend `MainLayout` which checks the session on `BeforeEnterEvent`; unauthenticated users are redirected to `""`.
- Logout clears the session and navigates to `""`.

---

## Views

### LoginView

- Route: `""` (no auth check)
- Layout: centred card with xpmile logo, username field, password field, Login button.
- On submit: call `AuthService.login(username, password)`.
  - Success → store account in session, navigate to `"shipments"`.
  - Failure (HTTP 4xx or empty response) → show inline error notification.

### MainLayout (app shell)

- Left nav drawer with menu items:
  - Shipments → `"shipments"`
  - Create Shipment → `"shipments/create"`
  - Bulk Upload → `"shipments/bulk"`
  - Create Account → `"accounts/create"`
- Top bar: app name + logged-in user name + Logout button.
- All child views rendered in the content area.

### ShipmentListView (`"shipments"`)

- Date range pickers (fromDate, toDate) + optional account code filter + Search button.
- Results in a `Grid<Shipment>` showing: AWB no, Alt Ref, Sender name, Receiver name, Service, Weight, Status, Created on.
- Row click opens `ModifyShipmentView` pre-populated with the selected shipment.
- Action buttons per row: Print Label (A4), Download PDF (stub for Phase 1).

### CreateShipmentView (`"shipments/create"`)

Three section cards matching existing Angular layout:

1. **AWB** — auto-generate toggle; AWB number field (disabled when auto-generate).
2. **Sender** — account code, name, company, contact, email, address, city, state, country, postal code, tax ID.
3. **Shipment Info** — SKU, service type (dropdown), items count, goods description, weight, COD amount, currency, customs value, HS code.
4. **Receiver** — name, company, contact, email, address, city, state, country, postal code, phone.

Submit → POST `/api/v1/shipment/shipping` → success notification + reset form.

### BulkShipmentView (`"shipments/bulk"`)

- `Upload` component accepting `.xlsx` files.
- Download template button (static file served from `resources`).
- On upload: parse Excel (Apache POI), convert rows to `Shipment[]`, POST to `/api/v1/shipment/bulk/shipping`.
- Results summary: total submitted, success count, failure list.

### ModifyShipmentView (`"shipments/modify"`)

- AWB number field (read-only after load) + Load button → GET shipment.
- Same section cards as CreateShipmentView, pre-populated.
- Save → PUT `/api/v1/shipment/shipping`.

### CreateAccountView (`"accounts/create"`)

Three section cards:

1. **Login Credentials** — account code (auto-gen toggle), password.
2. **Personal Info** — role (dropdown: admin/user), name, contact, email, address, city, state, postal code, event location.
3. **Customer Info** — company name, quoted amount, trading license, VAT, currency, bank account, IBAN, AWB prefix.

Submit → POST `/api/v1/account/account` → success notification + reset.

---

## Data models (Java)

```java
// Account
record LoginCredentials(String accountCode, String accountPassword) {}
record PersonalInfo(String eventLocation, String role, String name,
                    String contact, String email, String address,
                    String city, String state, String postalCode) {}
record CustomerInfo(String companyName, String quotedAmount,
                    String tradingLicense, String vat, String currency,
                    String bankAccountNumber, String iban) {}
record Account(boolean isAccountCodeAutoGen, String awbPrefix,
               LoginCredentials loginCredentials,
               PersonalInfo personalInfo, CustomerInfo customerInfo) {}

// Shipment (abbreviated — mirrors existing Angular model)
record SenderInformation(String accountNo, String name, String companyName,
                         String country, String city, String state,
                         String address, String postalCode,
                         String contact, String phoneNumber,
                         String email, String receivingTaxId) {}
record ReceiverInformation(String name, String country, String city,
                           String state, String postalCode,
                           String contact, String address,
                           String phone, String email) {}
record ShipmentInformation(String skuNo, String service,
                           int numberOfItems, String goodsDescription,
                           double goodsValue, double customsValue,
                           double codAmount, String currency,
                           double weight, String weightUnits,
                           String hsCode, String createdBy) {}
record ShipmentPayload(boolean isAutoGenerate, String awbno, String altRefNo,
                       SenderInformation senderInformation,
                       ShipmentInformation shipmentInformation,
                       ReceiverInformation receiverInformation) {}
```

---

## Configuration

`application.properties`:
```
xpmile.backend.base-url=http://localhost:8080
server.port=8090
vaadin.launch-browser=false
```

Override `base-url` at startup for different on-prem environments.

---

## Dependencies (pom.xml)

```xml
<dependencies>
  <dependency>
    <groupId>com.vaadin</groupId>
    <artifactId>vaadin-spring-boot-starter</artifactId>
    <version>24.x</version>
  </dependency>
  <dependency>
    <groupId>org.springframework.boot</groupId>
    <artifactId>spring-boot-starter-web</artifactId>
  </dependency>
  <!-- Excel parsing for bulk upload -->
  <dependency>
    <groupId>org.apache.poi</groupId>
    <artifactId>poi-ooxml</artifactId>
    <version>5.x</version>
  </dependency>
  <!-- Testing -->
  <dependency>
    <groupId>org.springframework.boot</groupId>
    <artifactId>spring-boot-starter-test</artifactId>
    <scope>test</scope>
  </dependency>
  <dependency>
    <groupId>com.vaadin</groupId>
    <artifactId>vaadin-testbench</artifactId>
    <scope>test</scope>
  </dependency>
</dependencies>
```

---

## Open questions / decisions deferred to Phase 2

- Label/PDF generation (currently Angular uses pdfMake; Java side could use iText or JasperReports).
- Role-based access control (admin vs user nav items).
- Inventory, tracking, reporting, email views.
- Packaging: fat JAR vs Docker image for on-prem distribution.
