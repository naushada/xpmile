# xpmile On-Prem UI — TDD Plan

> **Status (2026-05-13):** Phase 1 was about building a full daily-operations UI in Vaadin. The Vaadin app was subsequently re-scoped as an unauthenticated admin / recovery tool. Several sections below (5, 7, 8) reference views that no longer exist; they're kept for historical context. The **Current test scope** section is what applies today. See `docs/onprem-ui-design.md` for the live design.

---

## Current test scope (post-redesign)

| # | Test class | Subject | Status |
|---|---|---|---|
| 1 | `AccountServiceTest` | List/create/update + `resetPassword` + `deleteAccount` | Existing; needs new tests for `resetPassword`, `getAllAccounts`, `deleteAccount` |
| 2 | `ShipmentServiceTest` | Read APIs (list, by AWB) | Existing |
| 3 | `BulkShipmentParserTest` | XLSX → Shipment[] (kept; service is still referenced) | Existing |
| 4 | `AuthServiceTest` | Login flow | **Dead** — `AuthService` is now unreferenced code; remove when `AuthService` itself is deleted |
| 5 | _Future_ `DashboardViewIT` | Month picker → bucketing; PDF export downloads non-empty bytes | Not yet written |
| 6 | _Future_ `AccountsViewIT` | Grid renders, reset-password + delete-confirm dialog round-trips via WireMock | Not yet written |
| 7 | _Future_ `CreateAccountViewIT` | Submit posts to `/api/v1/account/account` and navigates back to `/accounts` | Not yet written |
| 8 | _Future_ `ShipmentListViewIT` | Live-poll fires every 60 s while attached | Not yet written |

**What to add next (highest-value new tests):**

- `AccountServiceTest`:
  - `resetPassword_sendsPutWithBodyOnly_noPasswordInUrl` — assert the captured WireMock request URL has no `password=` query param and the body contains `loginCredentials.accountPassword`.
  - `getAllAccounts_returnsArray` — backend returns 3 docs → list of 3.
  - `getAllAccounts_on400ReturnsEmpty` — backend returns 400 (no docs) → empty list (no exception).
  - `deleteAccount_sendsDeleteWithAccountCodeQueryParam` — assert the captured request is `DELETE /api/v1/account/account?accountCode=<code>` and the body is empty. Locks in the contract with the new backend handler.
  - `deleteAccount_on404PropagatesException` — backend returns 404 (account not found) → service rethrows so the UI dialog can show "Delete failed".

- `DashboardViewTest` (unit-level, no Vaadin):
  - Extract `compute(shipments, YearMonth)` into a static helper or package-private method and unit-test the bucketing rules directly against fixtures.
  - Test that `inMonth("15/05/2026", YearMonth.of(2026, 5))` is `true` and the same string against any other month is `false`.

- Backend C++ side: add a gtest for the new `handle_DELETE` `/api/v1/account/account` branch — assert that calling without `accountCode` returns 400, and calling with one builds the correct `{"loginCredentials.accountCode": "<code>"}` filter against the mock `IMongodbClient`. This is the guardrail against an empty-filter regression.

PDF generation is harder to assert exactly (binary output) — a "PDF starts with `%PDF-` and contains `Total shipments created` text" smoke test is enough.

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

> _Historical (Phase 1)._ AuthService is dead code after the login removal. The tests in `AuthServiceTest.java` still pass against the existing class but no UI consumer reaches `AuthService.login(...)`. Delete this section together with `AuthService.java` when convenient.

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

## 5. LoginView (UI test) — _historical_

> _Historical (Phase 1)._ `LoginView` is no longer routed; the Vaadin app is unauthenticated. The tests below would no longer compile (`@Route("")` is gone, the `LoginView` element selectors no longer have a host page). Skip implementing.



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

> Note: this view is now **read-only with live polling**. There is no "row click → modify" navigation any more (ModifyShipmentView was deleted). Replace 6.3 with a live-polling assertion.

### 6.1 — grid renders after search
```
given: WireMock returns 3 shipments
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

### 6.3 — live polling re-fetches after 60 s
```
given: view is attached; WireMock returns 1 shipment initially, then 2 on subsequent calls
when:  60 s elapses (TestBench: advance the UI poll-listener clock or wait)
then:  grid updates to 2 rows without user interaction
       "last refreshed" label updates to the later timestamp
```

---

## 7. CreateShipmentView (UI test) — _historical_

> _Historical (Phase 1)._ `CreateShipmentView` was deleted as part of the Vaadin re-scope (daily ops live in the Angular UI). Skip implementing.



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

## 8. CreateAccountView (UI test) — _historical_

> _Historical (Phase 1)._ `CreateAccountView` (the standalone form-page) was replaced by a dialog inside `AccountsView`. The "auto-gen toggle" and "password masked" assertions still apply, but against the dialog component now. Use `AccountsViewIT` (item 6 in Current test scope) instead.



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
