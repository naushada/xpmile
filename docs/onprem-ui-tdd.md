# xpmile On-Prem UI — TDD Plan (Phase 1)

Tests are written **before** implementation. Each section lists the test class, what to mock/stub, and the assertions that define "done".

---

## Test layers

| Layer | Tool | What it covers |
|-------|------|----------------|
| Unit | JUnit 5 + Mockito | Service classes in isolation (no HTTP, no Vaadin) |
| Integration | Spring Boot Test + MockMvc / WireMock | Full HTTP round-trip against a stubbed backend |
| UI (flow) | Vaadin TestBench (headless Chromium) | View rendering, form submission, navigation |

Run order: unit → integration → UI. Unit and integration tests must all pass before any UI test is written.

---

## 1. AuthService

**File:** `src/test/java/com/xpmile/onprem/service/AuthServiceTest.java`

### 1.1 — successful login returns account
```
given: backend returns 200 with a valid Account JSON for userId="admin", password="pass"
when:  authService.login("admin", "pass")
then:  returns non-null Account with loginCredentials.accountCode == "admin"
```

### 1.2 — wrong password throws AuthException
```
given: backend returns 200 with empty array []
when:  authService.login("admin", "wrong")
then:  throws AuthException with message "Invalid credentials"
```

### 1.3 — backend unreachable throws AuthException
```
given: backend refuses connection (WireMock not started / stub returns 503)
when:  authService.login("admin", "pass")
then:  throws AuthException wrapping the underlying RestClientException
```

---

## 2. ShipmentService

**File:** `src/test/java/com/xpmile/onprem/service/ShipmentServiceTest.java`

### 2.1 — getShipments with date range returns list
```
given: GET /api/v1/shipment/shipping?fromDate=X&toDate=Y returns JSON array of 3 shipments
when:  shipmentService.getShipments(fromDate, toDate, null)
then:  returns List<Shipment> with size 3
       each Shipment has non-null awbno
```

### 2.2 — getShipments with accountCode passes param
```
given: stub captures query params
when:  shipmentService.getShipments(from, to, "ACC001")
then:  outbound request contains param accountCode=ACC001
```

### 2.3 — createShipment posts correct body and returns AWB
```
given: POST /api/v1/shipment/shipping returns { "awbno": "XP0001" }
when:  shipmentService.createShipment(payload)
then:  returns "XP0001"
       request body contains isAutoGenerate field
```

### 2.4 — createBulkShipment posts array and returns summary
```
given: POST /api/v1/shipment/bulk/shipping returns { "created": 5, "failed": 0 }
when:  shipmentService.createBulkShipment(listOf5Shipments)
then:  returns BulkResult with created=5, failed=0
```

### 2.5 — updateShipment sends PUT and succeeds
```
given: PUT /api/v1/shipment/shipping returns 200
when:  shipmentService.updateShipment(awbno, payload)
then:  no exception; request method is PUT; URL contains awbno param
```

### 2.6 — getShipments with empty response returns empty list
```
given: backend returns []
when:  shipmentService.getShipments(from, to, null)
then:  returns empty List (not null)
```

---

## 3. AccountService

**File:** `src/test/java/com/xpmile/onprem/service/AccountServiceTest.java`

### 3.1 — createAccount posts correct body
```
given: POST /api/v1/account/account returns 200 with created account JSON
when:  accountService.createAccount(accountPayload)
then:  no exception; outbound body contains loginCredentials.accountCode
```

### 3.2 — createAccount with auto-gen sets flag
```
given: payload has isAccountCodeAutoGen=true
when:  accountService.createAccount(payload)
then:  outbound JSON has "isAccountCodeAutoGen": true
```

### 3.3 — getAccount returns account for userId
```
given: GET /api/v1/account/account?userId=ACC001 returns account JSON
when:  accountService.getAccount("ACC001")
then:  returns Account with loginCredentials.accountCode == "ACC001"
```

---

## 4. Excel parsing (BulkShipmentParser)

**File:** `src/test/java/com/xpmile/onprem/service/BulkShipmentParserTest.java`

### 4.1 — valid .xlsx with 3 rows returns 3 shipments
```
given: test-fixture.xlsx in src/test/resources with 3 data rows + header
when:  parser.parse(inputStream)
then:  returns List<ShipmentPayload> with size 3
       row 1: awbno matches cell A2, receiverInformation.name matches cell N2
```

### 4.2 — empty sheet returns empty list
```
given: xlsx with header row only
when:  parser.parse(inputStream)
then:  returns empty list, no exception
```

### 4.3 — row with missing required field throws ParseException
```
given: xlsx row where awbno cell is blank and isAutoGenerate is false
when:  parser.parse(inputStream)
then:  throws ParseException identifying the row number and missing field
```

---

## 5. LoginView (UI test)

**File:** `src/test/java/com/xpmile/onprem/ui/LoginViewIT.java`  
Requires: Vaadin TestBench, running Spring Boot test server, WireMock for backend.

### 5.1 — page renders login form
```
given: app started, user navigates to "/"
then:  username field visible
       password field visible
       Login button visible
```

### 5.2 — successful login navigates to shipments
```
given: WireMock stubs GET /api/v1/account/account → valid account
when:  user types username + password, clicks Login
then:  browser URL contains "/shipments"
       nav drawer visible with "Shipments" item
```

### 5.3 — wrong credentials shows error
```
given: WireMock stubs GET /api/v1/account/account → []
when:  user types username + wrong password, clicks Login
then:  error notification visible with text "Invalid credentials"
       URL still "/"
```

### 5.4 — unauthenticated access to /shipments redirects to login
```
given: no session
when:  user navigates directly to "/shipments"
then:  browser URL is "/"
```

---

## 6. ShipmentListView (UI test)

**File:** `src/test/java/com/xpmile/onprem/ui/ShipmentListViewIT.java`

### 6.1 — grid renders after search
```
given: authenticated session; WireMock returns 3 shipments
when:  user sets fromDate, toDate, clicks Search
then:  grid shows 3 rows
       first row AWB column matches first shipment awbno
```

### 6.2 — empty result shows empty grid with no error
```
given: WireMock returns []
when:  user searches
then:  grid has 0 rows; no exception dialog visible
```

### 6.3 — row click navigates to modify view with AWB pre-filled
```
given: grid has 1 row with awbno="XP001"
when:  user clicks the row
then:  URL contains "/shipments/modify"
       AWB field value is "XP001"
```

---

## 7. CreateShipmentView (UI test)

**File:** `src/test/java/com/xpmile/onprem/ui/CreateShipmentViewIT.java`

### 7.1 — auto-generate toggle disables AWB field
```
when:  user checks auto-generate toggle
then:  AWB number field is disabled
```

### 7.2 — form submit posts to backend and shows success
```
given: WireMock stubs POST /api/v1/shipment/shipping → { "awbno": "XP999" }
when:  user fills all required fields, clicks Save
then:  success notification visible containing "XP999"
       form is reset (AWB field cleared)
```

### 7.3 — missing required field shows validation error
```
when:  user clicks Save with sender name empty
then:  inline error shown on sender name field
       no HTTP call made to backend
```

---

## 8. CreateAccountView (UI test)

**File:** `src/test/java/com/xpmile/onprem/ui/CreateAccountViewIT.java`

### 8.1 — auto-gen toggle disables account code field
```
when:  user checks auto-gen toggle
then:  account code field disabled
```

### 8.2 — successful creation shows notification
```
given: WireMock stubs POST /api/v1/account/account → 200
when:  user fills required fields, clicks Create Account
then:  success notification visible
       form fields cleared
```

### 8.3 — password field is masked
```
when:  view loads
then:  password input type is "password"
```

---

## Implementation order (TDD cycle)

1. Write all unit tests for `AuthService` → implement `AuthService` → green.
2. Write all unit tests for `ShipmentService` → implement `ShipmentService` → green.
3. Write all unit tests for `AccountService` → implement `AccountService` → green.
4. Write `BulkShipmentParser` unit tests → implement parser → green.
5. Scaffold `LoginView` → write UI tests 5.1–5.4 → implement view logic → green.
6. Scaffold `MainLayout` + `ShipmentListView` → write UI tests 6.1–6.3 → implement → green.
7. Scaffold `CreateShipmentView` → write UI tests 7.1–7.3 → implement → green.
8. Scaffold `BulkShipmentView` → (UI tests TBD in Phase 1.5) → implement → green.
9. Scaffold `ModifyShipmentView` → (reuses 7.x pattern) → implement → green.
10. Scaffold `CreateAccountView` → write UI tests 8.1–8.3 → implement → green.

Each step: **red → green → refactor**. No step is marked done until its tests pass and the coverage report shows the new service/view code is fully exercised.

---

## Fixture files required

| File | Location | Purpose |
|------|----------|---------|
| `test-fixture.xlsx` | `src/test/resources/` | 3-row Excel for bulk parser tests |
| `account-response.json` | `src/test/resources/wiremock/` | WireMock stub for valid login |
| `shipment-list-response.json` | `src/test/resources/wiremock/` | WireMock stub for shipment list |
| `create-shipment-response.json` | `src/test/resources/wiremock/` | WireMock stub for shipment creation |
