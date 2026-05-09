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

The default is `http://localhost:8080`. Override with one of:

**Option A — environment variable (recommended):**
```sh
export XPMILE_BACKEND_BASE_URL=http://<your-backend-host>:8080
mvn spring-boot:run
```

**Option B — command-line property:**
```sh
mvn spring-boot:run -Dspring-boot.run.arguments="--xpmile.backend.base-url=http://<host>:8080"
```

**Option C — edit `application.properties`:**
```properties
xpmile.backend.base-url=http://<your-backend-host>:8080
```

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
