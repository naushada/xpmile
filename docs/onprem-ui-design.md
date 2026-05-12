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
podman build -t xpmile-onprem .
```

### Run against local backend

```sh
podman run -p 8090:8090 \
  -e XPMILE_BACKEND_BASE_URL=http://host.docker.internal:8080 \
  xpmile-onprem
```

> On Linux replace `host.docker.internal` with the host's actual IP (e.g. `172.17.0.1`).

### Run against Heroku backend

```sh
podman run -p 8090:8090 \
  -e XPMILE_BACKEND_BASE_URL=https://marvel-3a78bd953f5f.herokuapp.com \
  xpmile-onprem
```

### Run against on-prem remote machine

```sh
podman run -p 8090:8090 \
  -e XPMILE_BACKEND_BASE_URL=http://<backend-host>:8080 \
  xpmile-onprem
```

Open **http://localhost:8090** after the container starts.

---

## Podman Compose (local stack)

See `docker-compose.onprem.yml` in the repo root for a compose file that brings up both the xpmile backend (MongoDB + uniservice) and the on-prem UI together.

Run from the `xpmile/` repo root:

```sh
# Start everything (backend + on-prem UI)
podman-compose -f docker-compose.onprem.yml up --build

# On-prem UI only against Heroku (single line — no backslash)
XPMILE_BACKEND_BASE_URL=https://marvel-3a78bd953f5f.herokuapp.com podman-compose -f docker-compose.onprem.yml up --build onprem-ui

# Stop
podman-compose -f docker-compose.onprem.yml down
```

See `docs/onprem-ui-compose.md` for the full guide.

---

## Scope

The on-prem Vaadin UI is **deliberately narrower** than the Angular cloud app. It exists primarily as an **admin / recovery tool** for the customer's premises — not as a parallel daily-operations console.

### Why narrower?

The cloud-deployed Angular app has no self-service "forgot password" flow. When a user can't log in, recovery happens here, where the operator is physically on the customer's premises and protected by physical access controls. That's the load-bearing feature.

### Current views

| # | View | Route | Purpose |
|---|------|-------|---------|
| 1 | Dashboard | `""` (landing) | Monthly stats with PDF export for management. Same buckets as the Angular live subnav but scoped to a selected month. |
| 2 | Shipments | `shipments` | Read-only list with **live polling** (60 s). Date-range + account filter. |
| 3 | Accounts | `accounts` | Account list grid. Per-row **Reset Password** dialog. Toolbar **Create Account** dialog. |

### Authentication

**There is none.** The Vaadin app does not have a login screen by design. The `LoginView` source file is retained (un-routed) for reference; `AuthService` is dead code today and may be removed in a later cleanup.

### Out of scope (lives only in the Angular cloud app)

- Create / modify / bulk-upload daily shipment flows
- DRS (Delivery Run Sheet) generation, driver assignment
- Inventory in/out/find/update + A10 label PDFs
- Detailed reports and invoices
- Email notifications

### PDF export (Dashboard)

The Dashboard's Export PDF button generates an A4 portrait PDF via [OpenPDF](https://github.com/LibrePDF/OpenPDF) (Apache 2.0 licensed; AGPL-compatible). Output:

- Title: "xpmile — Monthly Operations Dashboard"
- Subtitle: selected month + generation date
- Two-column stats table: Total / Delivered / In Scan / Out For Delivery / Returned / New
- Footer noting the bucketing definition

The file is delivered via Vaadin's `StreamResource` + `Page.open(..., "_blank")` — no temporary files on disk.

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

**None.** Removed. The Vaadin app is unauthenticated by design — it lives behind the customer's physical access controls and exists primarily as an admin / recovery tool.

The previous design used `AuthService.login(...)` which sent the password as a URL query parameter (`GET /api/v1/account/account?userId=&password=`). That's a multi-way leak: Heroku router logs, browser history, `Referer` headers. Rather than fix it, login was removed entirely — there's no scenario in which an operator with on-prem-machine access also needs to prove who they are to the Vaadin app.

The `LoginView` source file is retained un-routed (no `@Route`) for reference; `AuthService` is dead code today and may be removed in a later cleanup.

---

## Views

### MainLayout (app shell)

- Dark navy navbar with xpmile branding + Agent/DB status badges (driven by `StatusService.check()`).
- Left nav drawer: **Dashboard**, **Shipments**, **Accounts**.
- No logout button, no session-check `BeforeEnterEvent`.
- `UI.setPollInterval(30_000)` drives the status-badge refresh; `ShipmentListView` piggybacks on this poll for its 60 s live update.

### DashboardView (`""` — landing)

- Header strip: title + month picker + Refresh + **Export PDF**.
- Six stat cards: Total, Delivered, In Scan, Out For Delivery, Returned, New — same buckets as the Angular live subnav.
- `compute(shipments, YearMonth)` filters by `createdOn` falling in the selected month and buckets each shipment on its overall latest activity event (current status). Both `DD/MM/YYYY` and ISO `YYYY-MM-DD` createdOn formats are accepted.
- **PDF export**: OpenPDF (Apache 2.0, AGPL-compatible) generates an A4 portrait PDF — title, month subtitle, stats table, footer noting the bucketing definition. Delivered via Vaadin `StreamResource` + `Page.open("_blank")` — no temp files on disk.

### ShipmentListView (`"shipments"`)

- Header: title + LIVE pill + last-refreshed timestamp.
- Filters: `From` date, `To` date, optional account code, Search button.
- `Grid<Shipment>` columns: AWB no, Alt Ref, Sender, Receiver, Service, Weight, Created On. **Read-only** — no row-click navigation, no per-row PDF actions.
- **Live polling**: 60 s refresh while the view is attached. Implemented atop `UI.addPollListener` with a millisecond gate, so it co-exists with MainLayout's 30 s status poll without contention.

### AccountsView (`"accounts"`)

- Toolbar: **Create Account** (opens dialog), Refresh.
- Grid columns: Account Code, Name, Role, Email, Contact, **Actions**.
- Per-row **Reset Password** action opens a dialog (New Password + Confirm). On save: `accountService.resetPassword(code, newPwd)` → `PUT /api/v1/account/account?userId=<>` with the new plaintext in the body — `handle_account_PUT` on the backend hashes via PBKDF2 before storing. Password never appears in any URL.
- **Create Account** dialog: minimal form (account code, password, name, email, contact, role). The full enterprise account form with VAT, IBAN, trading licence, currency, etc. stays in the Angular UI for daily provisioning workflows.

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
