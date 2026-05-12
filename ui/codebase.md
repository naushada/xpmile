# UI codebase guide — xpmile Angular frontend

Deep technical reference for the Angular SPA. Covers architecture, every significant component, shared services, styling system, PDF generation, and known pitfalls. Pair with the root `codebase.md` for the backend.

---

## Architecture overview

The app is a standard Angular 14 SPA with no lazy-loaded feature modules — all components are eagerly loaded through `AppModule`. The shell (`MainComponent`) holds the Clarity layout (`clr-main-container`, `clr-header`, `clr-vertical-nav`) and a `<router-outlet>` for page content.

```
Browser
  └─ AppModule (eager)
       ├─ AppRoutingModule   /login → LoginComponent
       │                     /main  → MainComponent (shell)
       │                     /dashboard → DashboardComponent
       ├─ LoginComponent
       ├─ MainComponent      ← shell; hosts <router-outlet>
       ├─ DashboardComponent
       ├─ Shipping components (single, bulk, altref-bulk, third-party,
       │                       list, collect-shipment, create-drs, modify,
       │                       api-integration, submenu)
       ├─ Tracking components (single-shipment, multiple-shipment,
       │                        update-shipment, email, snav-bar)
       ├─ Inventory components (create-manifest, find-inventory,
       │                         in-inventory, out-inventory,
       │                         update-inventory, inav-bar)
       ├─ Accounting components (create-account, list-account,
       │                          update-account, anav-bar)
       ├─ Reporting components (detailed-report, invoice, rnav-bar)
       └─ Common components    (status-badge)
```

Navigation between sections is wired through the sidebar nav in the shell, not through Angular router child routes — each nav item calls a method that sets a flag/input to show the right sub-component.

**Two homes for shared code:**

- `ui/src/common/` — singleton services (`HttpsvcService`, `PubsubsvcService`, `ExcelsvcService`, `LabelService`, `ShipmentStatsService`, `StatusService`) plus `app-globals.ts` (types + `UriMap`). Imported as `from 'src/common/...'`.
- `ui/src/app/common/` — shared Angular components (`status-badge/`). Imported as `from 'src/app/common/...'`.

Keep services in `src/common/` and components in `src/app/common/`; don't mix them.

---

## Shared services (`src/common/`)

### `app-globals.ts`

Central type and constant file imported by almost every component and service.

**Key interfaces:**

```typescript
interface Account {
  loginCredentials: { accountCode, accountPassword }
  personalInfo:     { name, role, city, state, address, postalCode,
                       contact, email, eventLocation }
  customerInfo:     { companyName, vat, awbPrefix }
}

interface Shipment {
  shipment: {
    awbno, altRefNo, isAutoGenerate,
    senderInformation:   { accountNo, name, companyName, country, city,
                            state, address, postalCode, contact, phoneNumber,
                            email, receivingTaxId, referenceNo }
    shipmentInformation: { service, numberOfItems, goodsDescription,
                            goodsValue, customsValue, codAmount, vat,
                            currency, weight, weightUnits, cubicWeight,
                            createdOn, createdBy, hsCode, skuNo,
                            activity: ActivityEntry[] }
    receiverInformation: { name, country, city, state, postalCode,
                            contact, address, phone, email }
  }
}

class ShipmentExcelRow {
  AccountCode, ReferenceNo, AlternateReferenceNo, SenderName,
  ReceiverName, ReceiverCountry, ReceiverCity, ReceiverAddress,
  ReceiverPhoneNo, ReceiverAlternatePhoneNo,
  GoodsDescription, CustomsValue, CustomsCurrency,
  CodAmount, Weight, HSCode
}
```

**`UriMap`** — `Map<string, string>` mapping logical keys to API paths:

| Key | Path |
|---|---|
| `from_web_shipment` | `/api/v1/shipment/shipping` |
| `from_web_single_shipment` | `/api/v1/shipment/single/shipping` |
| `from_web_bulk_shipment` | `/api/v1/shipment/bulk/shipping` |
| `from_web_bulk_altrefshipment` | `/api/v1/shipment/bulk/altref` |
| `from_web_account` | `/api/v1/account/account` |
| `from_web_manifest` | `/api/v1/inventory/manifest` |
| `from_web_inventory` | `/api/v1/inventory` |
| `from_web_document` | `/api/v1/document` |
| `from_web_email` | `/api/v1/email` |
| `from_web_config` | `/api/v1/config` |
| `from_web_job` | `/api/v1/job` |

---

### `HttpsvcService`

All HTTP calls live here. `environment.apiUrl` is prepended to every URI when non-empty (empty in the default dev setup so paths are relative to the same origin).

**Shipment methods:**

| Method | Verb | Notes |
|---|---|---|
| `getShipmentByAwbNo(awb, accountCode?)` | GET | Single lookup |
| `getShipmentByAltRefNo(altRef, accountCode?)` | GET | — |
| `getShipments(from, to, country?, accountCode?)` | GET | Date-range list |
| `getShipmentsList(from, to, accountCode?)` | GET | String dates |
| `getShipmentsForAccount(accountCode)` | GET | All for one account |
| `getShipmentsByAwbNo(awbs[], accountCode?)` | GET | Batch lookup |
| `createShipment(payload)` | POST | Single shipment |
| `createBulkShipment(json)` | POST | Bulk array |
| `updateShipmentStatus(awbs[], data)` | PUT | Activity push |
| `updateShipmentParallel(awbs[], data)` | PUT | Parallel update |

**Account methods:**

| Method | Notes |
|---|---|
| `getAccountInfo(id, pwd?)` | Login or info lookup |
| `getAccountInfoList()` | Admin — all accounts |
| `getCustomerInfo(accountCode)` | Used by bulk to resolve sender details |

**Inventory / other:** `getFromInventory`, `createManifest`, `sendEmail`, etc.

---

### `PubsubsvcService`

Three `BehaviorSubject` channels for cross-component state:

```typescript
onAccount:     Observable<Account>    // emitted after login
onAccountList: Observable<Account[]>  // emitted for admin views
onShipment:    Observable<Shipment>   // emitted when a row is selected in the list
```

Emit via `emit_accountInfo(acct)`, `emit_accountListInfo(list)`, `emit_shipment(ship)`.

Every component that needs the logged-in user subscribes to `onAccount` in `ngOnInit` and unsubscribes via `SubSink` in `ngOnDestroy`.

**SubSink pattern** — used throughout the app to avoid manual subscription tracking:

```typescript
import { SubSink } from 'subsink';

private subsink = new SubSink();

ngOnInit(): void {
  this.subsink.add(
    this.pubsub.onAccount.subscribe(acct => { this.loggedInUser = acct; })
  );
}

ngOnDestroy(): void {
  this.subsink.unsubscribe();
}
```

`SubSink.add()` accepts any number of `Subscription` arguments; `.unsubscribe()` disposes all at once. Always use SubSink instead of manually storing subscriptions in arrays.

---

### `ExcelsvcService`

Wraps SheetJS to generate the bulk-upload template:
- `createAndSaveShipmentTemplate(filename)` — creates a workbook with the correct column headers (`AccountCode`, `ReferenceNo`, etc.) and triggers browser download.

---

### `LabelService`

Generates A10-size barcode label PDFs. Lives at `src/common/label.service.ts`.

```typescript
labels.createA10LabelPdf(sku: string, qty: number, fileName = 'A10-label'): void
```

- Renders `qty` pages, one CODE128 barcode per page, and triggers a browser download named `<fileName>-<sku>.pdf`.
- A10 landscape printable area is `~103pt × 72pt` (margins of 1pt each side on a 105×74pt page). The barcode is emitted as a base64 PNG and rendered with `fit: [fitW, fitH]` so it fills the page edge-to-edge.
- `pdfMake.vfs` is set once at module load — do not reset it from callers.
- The `docDef` object is built fresh inside the method, per the project rule against caching `docDef`/content arrays on long-lived objects.

Used by `inventory/create-manifest` (manual generate from SKU + qty) and `inventory/in-inventory` (auto-download on successful Create Inventory).

**A10 fit-to-page note:** an earlier version wrapped the barcode in a one-cell table at `width: 90pt`, which left the bottom half of every label blank. The current implementation uses a top-level `{ image, fit: [fitW, fitH] }` node and fills the page.

---

### `ShipmentStatsService`

Drives the live stat chips in the shell's subnav (`MainComponent`). Lives at `src/common/shipment-stats.service.ts`.

```typescript
readonly stats$:   Observable<{
  total, new, inScan, outForDelivery, delivered, returned: number
}>;
readonly loading$: Observable<boolean>;
refresh(): void;          // manual one-shot fetch
```

**Polling:** subscribes to `pubsub.onAccount`; when a new account logs in, kicks off `timer(0, 60_000)` and re-fetches every 60s. Each tick calls `HttpsvcService.getShipmentsList(from, to, accCode)`.

**Date format:** the backend stores `createdOn` and `activity[].date` as `DD/MM/YYYY` and compares lexicographically. The service formats `today` and `fromDate` with `formatDate(d, 'dd/MM/yyyy', 'en-GB')`. Anything else (ISO, US) will silently match zero rows.

**Account scoping:** for `role === 'Customer'`, the request is scoped to `loginCredentials.accountCode`. For Admin/Employee the `accountCode` param is omitted so the badges aggregate across all customers.

**Empty-result handling:** the backend returns HTTP 400 with `{cause:..., error:400}` when zero docs match the date range. The service `catchError(() => of([]))` so the chips show `0`, not stale numbers.

**Bucketing:** for each shipment with any activity dated today, the *latest* event from today decides the bucket:

| Event string | Bucket |
|---|---|
| `Document Prepared` / `Document Created` | `new` |
| `In Scan at HUB` / `Arrived in HUB` | `inScan` |
| `Out For Delivery` | `outForDelivery` |
| `Proof of Delivery` | `delivered` |
| `Shipment Returned to Sender` / `Shiment Returned to Sending Station` | `returned` |

The `Shiment` typo is intentional — it matches a real value that exists in production data.

---

### `StatusService`

Drives the Agent + DB status pills (`status-badge`). Lives at `src/common/status.service.ts`.

```typescript
readonly agent$:       Observable<'up' | 'down' | 'unknown'>;
readonly db$:          Observable<'up' | 'down' | 'unknown'>;
readonly lastChecked$: Observable<Date | null>;
```

**Probe:** `GET /api/v1/config` every 30s using `responseType: 'text'` + `observe: 'response'`. Any 2xx/3xx/4xx status means the request reached the backend and got a response, so the path through `nginx/uniservice → (proxy) → DB` is alive — pills go green. Only network errors and 5xx (status === 0 in some browsers) mark "down".

**Single-state limitation:** without a dedicated health endpoint we can't tell "agent up, DB down" from "agent down" — one probe drives both `agent$` and `db$`. If the backend later exposes `/api/v1/health` returning per-component state, update the probe to set the two streams independently.

The probe URL is built from `environment.apiUrl + UriMap.get('from_web_config')`, so it works in both same-origin dev and absolute-URL deployments.

---

## Role-based behaviour

The `personalInfo.role` field on the logged-in `Account` controls feature availability:

| Role | Bulk shipment upload | Account code in Excel | Account list visible |
|---|---|---|---|
| `Admin` | Yes — fetches all AccountCodes from Excel | Yes | Yes |
| `Employee` | Yes — fetches AccountCodes from Excel | Yes | Yes |
| Customer / other | No — alert "Bulk Upload is not supported for your Account" | — | No |

Checked in `processShipmentExcelFile()` at the `onloadend` callback — the role is read from `this.loggedInUser?.personalInfo.role`.

---

## Login (`app/login/`)

Two-panel layout:
- **Left panel** (`.login-panel`, 420 px, white): xpmile SVG logo + Clarity `clr-form` with account code and password fields + login button.
- **Right panel** (`.hero-panel`, `flex: 1`): `cross3.jpg` as `background-image` with a `linear-gradient` dark overlay + "Last Mile, First Class" hero copy.

`LoginComponent.onLogin()`:
1. Calls `HttpsvcService.getAccountInfo(code, password)`.
2. On success: emits via `PubsubsvcService.emit_accountInfo(account)`, navigates to `/main`.
3. On error: shows an alert.

`password-reset/` — sub-component for the reset flow (OTP or email-based).

---

## Shell (`app/main/`)

`MainComponent` wraps the Clarity `clr-main-container`:
- `clr-header` with the xpmile logo, top section links (Shipping / Tracking / Reporting / Accounting / Inventory), and the user dropdown.
- A **live subnav strip** (`<nav class="live-subnav">`) directly below the header — see below.
- A per-section sidebar (`submenu`, `snav-bar`, `rnav-bar`, `anav-bar`, `inav-bar`) — the active section is controlled by `selectedMenuItem`, and the active sub-page by `selectedNavItem` (both strings).
- A `content-area` with `*ngIf`-switched sub-components for each `selectedNavItem` value. No Angular router child routes are used here.

The sidebar nav HTML files contain `<a class="nav-link nav-text">` items — no `<b>` tags (stripped for cleaner font rendering).

### Live subnav strip

Always-visible "today at a glance" bar bound to `ShipmentStatsService` and `StatusService`:

```
[ ● LIVE ]  [📅 Tue, 12 May]  [✓ Delivered N]  [≡ In Scan N]  [🚚 Out N]
            [↶ Returned N]    [+ New N]        [⟳ refresh]   [Agent ●][DB ●]
```

- **LIVE pulse** — animated dot (`@keyframes live-pulse`) to signal real-time data.
- **Date chip** — today's date formatted for display as `EEE, d MMM` (e.g. "Tue, 12 May"). Note: this is *display only* — `ShipmentStatsService` sends `DD/MM/YYYY` to the backend.
- **Five gradient stat chips** — `chip-delivered`, `chip-inscan`, `chip-out`, `chip-returned`, `chip-new`. Each binds to `(stats.stats$ | async)?.<bucket> ?? 0` via the async pipe, so the UI re-renders automatically on every poll without `ChangeDetectorRef` calls.
- **Refresh button** (`.live-refresh-btn`) — calls `stats.refresh()`; the icon spins while `stats.loading$` is true via `[class.spinning]`.
- **Agent/DB pills** — rendered by `<app-status-badge>` at the right edge.
- **Auto-flash on poll** — `MainComponent` subscribes to `stats.stats$` in its constructor and sets `flashOn = true` for 700ms every time a poll completes (auto or manual). The template binds `[class.flashing]="flashOn"` on `.live-subnav`, which animates a brief highlight across `.live-strip`. Gives a visible "just refreshed" cue without the user clicking anything.

**End-to-end live-update flow:**

```
ShipmentStatsService.timer(0, 60_000)
  → GET /api/v1/shipment/shipping?fromDate=...&toDate=... (DD/MM/YYYY)
  → compute() buckets shipments by today's latest activity event
  → stats$.next(newStats)
       ├─ async pipe in template updates the five chip counts
       └─ MainComponent subscriber toggles flashOn for 700ms

StatusService.timer(0, 30_000)
  → GET /api/v1/config
  → agent$.next(state), db$.next(state), lastChecked$.next(now)
       └─ async pipe in status-badge updates the two pills
```

The strip's chip styling is nested under `.live-subnav` in `main.component.scss` so the rules don't leak into reused chips elsewhere. `@media (prefers-reduced-motion: reduce)` disables the pulse, the flash, and the refresh-icon spin — animations are opt-in.

### `common/status-badge/`

Shared component that renders two pills (Agent, DB) backed by `StatusService`. Both pills get `up`/`down` modifier classes and a `title` attribute showing the current state plus the last-checked timestamp:

```html
<app-status-badge></app-status-badge>
```

Use this anywhere the user needs at-a-glance backend connectivity — currently only the live subnav, but it has no other dependencies, so it can be dropped into headers, footers, or status panels.

---

## Dashboard (`app/dashboard/`)

Five stat cards in a single `clr-row.cards-row`:
- Created Today, Total Active, Delivered, Returned, COD Pending.
- All in `clr-col-lg-4` columns (`align-items: stretch` on the row, `min-height: 160px` on cards) so they stay the same height regardless of content.
- Card icons use gradient backgrounds (`__blue`, `__teal`, `__red`, `__yellow`, `__green` modifiers in `dashboard.component.scss`).

Data is fetched from the shipment API and counted client-side by status.

---

## Form-page layout pattern

Nine form pages share a consistent card-based layout, applied in one wave of redesign:

- Shipping: `single/`, `modify/`, `collect-shipment/`
- Tracking: `update-shipment/`
- Accounting: `create-account/`, `update-account/`
- Inventory: `create-manifest/`, `in-inventory/`, `out-inventory/`, `update-inventory/`

Each page has four parts:

1. **Page header strip** (`.<prefix>-header`) — icon tile (`<clr-icon>` in a coloured square), title (`h2`), one-line subtitle, and optional step indicators on the right (used by `single/` for Sender / Shipment / Receiver).
2. **Optional summary card** (e.g. `.ss-awb-card`) — for fields that govern the rest of the form (auto-generate toggle, AWB no., alt ref).
3. **Section cards grid** (`.<prefix>-cards-grid`) — two or three `.<prefix>-card` blocks each containing `.card-head` (icon + h3 + hint) and `.card-body` (form fields). On narrow viewports the grid collapses to `1fr`.
4. **Sticky action bar** (`.<prefix>-action-bar`) — `position: sticky; bottom: 0;` row holding the primary submit button and an optional hint. Stays visible while the user scrolls long forms.

**Class-prefix convention:** each page uses a 2-letter component prefix to keep its rules scoped — `ss-` for single-shipment, `ca-` for create-account, `ii-` for in-inventory, etc. Prefixes are private to the component SCSS; the grid/card/action-bar structure is the only thing standardised.

`clrForm clrLayout="vertical" clrLabelSize="5"` is used throughout — labels sit above inputs.

Global compact spacing rules still apply (see Global styles): `clr-input-container`, `clr-select-container`, `clr-checkbox-container` get `margin-bottom: 6px !important` and `.clr-subtext-wrapper { display: none }`.

---

## Shipping components

### `single/` — Create Single Shipment

Reactive form (`FormBuilder`) with three groups:
- `senderInformation` — account-linked fields (auto-filled from account lookup)
- `shipmentInformation` — service type, weight, goods, COD, customs
- `receiverInformation` — name, country, city, address, phone

`isAutoGenerate: true` → backend generates the AWB via `next_awbno(prefix)`.

Uses the four-part form layout (header strip → AWB summary card → three-card grid → sticky action bar) with the `ss-` prefix.

---

### `bulk/` — Create Bulk Shipment

**Template:** Custom upload card — dashed border box, hidden `<input type="file">` triggered by a `<label class="btn">`. Shows the selected file name below the card. Action row: Download Template button + Create Bulk Shipment button (disabled until file is parsed and accounts are fetched).

**Component logic:**

```
onFileSelect()
  └─ processShipmentExcelFile()
       ├─ FileReader.readAsBinaryString()
       ├─ XLSX.read() → sheet_to_json()
       ├─ builds ShipmentExcelRow[]
       └─ (Employee/Admin) fetches Account per unique AccountCode
            └─ accountInfoList.set(code, account)

onCreateBulkShipment()
  ├─ builds bulkShipment[] — one object per ShipmentExcelRow, sender filled from accountInfoList
  ├─ POST /api/v1/shipment/bulk/shipping
  └─ on success:
       ├─ createdResults[] ← zip awbNumbers[] with rowSnapshot[]
       └─ downloadPdf()   ← auto-download A4 portrait PDF
```

**Results table** (`clr-datagrid`): S. No., AWB Number (monospace + copy button), Alt. Ref. No., Receiver, City, Status badge. Paginated 10/25/50 per page.

**PDF report** (`downloadPdf()`): pdfMake A4 portrait, blue header row, alternating row fill, Courier font for AWB column. File: `BulkShipment-DD-MM-YYYY.pdf`.

**Known gotcha:** `[formControl]` on a Clarity `<input>` requires `$any()` cast because `FormGroup.controls[key]` returns `AbstractControl`, not `FormControl`:
```html
[formControl]="$any(bulkShipmentForm.controls['excelFileName'])"
```

---

### `altref-bulk/` — Update Bulk ALT REF. Number

Same upload card pattern as bulk. Parses the Excel file (different column layout — AWB + new Alt Ref pairs) and POSTs to `/api/v1/shipment/bulk/altref`.

---

### `third-party/` — Create Third Party Shipment

Two sections:
- Vendor selector (radio buttons: Ajoul, etc.)
- Upload card — same pattern as bulk

Submit is disabled until both `selectedVendor` and `selectedFileName` are set.

---

### `list/` — Shipment List

Large feature component:
- Date-range + account code filter.
- `clr-datagrid` with pagination showing shipment rows.
- Row click → emits via `PubsubsvcService.emit_shipment()` → navigates to update/tracking.
- **PDF labels**: A2 (landscape), A4 (portrait), A6 (small label). Each calls `pdfMake.createPdf(def).download(name)`. Barcode embedded as base64 PNG via JsBarcode.
- Copy AWB to clipboard via `navigator.clipboard.writeText()`.

---

### `collect-shipment/` — Collect Shipment

Table of pending shipments. Each row has a single `.action-cell` column containing (stacked vertically, `flex-direction: column`):
- Driver select dropdown
- Assign button
- Finalise button
- Cancel button

Previously these were spread across 4 separate columns; consolidated into one for readability.

---

### `create-drs/` — Create DRS

**Filter card** (`.filter-card`):
- Multi-AWB textarea (one AWB per line, split by `\n`)
- Driver name input
- Vendor radio buttons
- Submit button

**Grid toolbar**: shipment count + "Export PDF (A4)" button (only visible when `shipments.length > 0`).

**Table**: each row has a barcode image generated by `textToBase64Barcode(awbno, height)` using JsBarcode with `width: 1` (narrow bars for compact fit).

**PDF export** (`onCreateDRS()`):
```typescript
// Document definition built fresh each call — never stored as class property
const docDef: any = {
  pageSize: 'A4', pageOrientation: 'landscape',
  pageMargins: [10, 10, 10, 10],
  content: [{ table: { headerRows: 1,
    widths: ['auto', '*', '*', 'auto', 70, 160, '*'],
    body  // header row + one row per shipment with barcode image
  }}],
  defaultStyle: { fontSize: 9 }
};
pdfMake.createPdf(docDef).download('DRS-A4.pdf');
```

**Pitfall fixed:** Original code had `cols: any[][7] = [][7]` — `[][7]` is `undefined` in JS, so `cols` was never an array. Also, the doc definition referenced `this.cols` at class-init time, so reassigning `this.cols` later had no effect on the PDF body. Fixed by building the entire doc definition inside the method.

---

## Tracking components

### `multiple-shipment/` — Multiple Shipment Tracking

Tracks a batch of AWBs. Fetches activity history per AWB and renders a timeline. Supports A4 and A6 label PDF export and A4 invoice PDF export.

### `update-shipment/` — Update Shipment

Updates activity log entries (status events) on existing shipments. Uses `updateShipmentStatus()` / `updateShipmentParallel()`.

### `email/` — Send Email

Composes and sends a shipment notification email via `/api/v1/email`.

---

## Inventory components

### `create-manifest/` — Create Manifest

Single-form page (SKU + quantity) that delegates A10 barcode label PDF generation to `LabelService.createA10LabelPdf(sku, qty)`. The component no longer owns any `pdfMake` setup or `docDef` — it's a thin wrapper around the service call.

### `in-inventory` / `out-inventory` / `find-inventory` / `update-inventory`

Standard CRUD components against `/api/v1/inventory`. Each uses `clr-datagrid` for display and follows the four-part form layout (header strip → section cards → sticky action bar).

**`in-inventory` auto-label flow:** on successful `createInventory()` the component:
1. Calls `LabelService.createA10LabelPdf(sku, qty)` so the user gets their labels without re-typing SKU + qty on `create-manifest`.
2. Resets the form.

Previously the create handler had an empty error block (silent failures) and forced the user to retype the same SKU + qty on the manifest page — both fixed in this refactor.

---

## Global styles (`src/styles.scss`)

Key overrides on top of Clarity defaults:

```scss
// Deep navy header
clr-header, .header {
  background-color: #0f2d52 !important;

  .header-nav .nav-link.nav-text {
    display: inline-flex;
    align-items: center;
    height: 100%;           // keeps items vertically centred
    font-size: 0.82rem;
    font-weight: 500;
    color: rgba(255,255,255,0.80);
    padding: 0 14px;
    &:hover, &.active { color: #fff; background: rgba(255,255,255,0.10); }
  }
}

// Sidebar with left-border accent
clr-vertical-nav .nav-link.nav-text {
  font-size: 0.8rem;
  font-weight: 500;
  color: #475569;
  border-left: 3px solid transparent;
  &:hover  { background: #f1f5f9; border-left-color: #93c5fd; }
  &.active { background: #eff6ff; color: #1b4d8e; font-weight: 600; border-left-color: #1b4d8e; }
}
```

Component-level spacing (applied in `single.component.scss`, `collect-shipment.component.scss`, etc.):

```scss
clr-input-container,
clr-select-container,
clr-checkbox-container {
  margin-top: 0 !important;
  margin-bottom: 6px !important;
  .clr-subtext-wrapper { display: none; }
}
```

Upload card pattern (in `bulk.component.scss`, `altref-bulk.component.scss`, `third-party.component.scss`):

```scss
.upload-card {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 8px;
  padding: 36px 24px;
  border: 2px dashed #c2ccd6;
  border-radius: 6px;
  background: #f8fafc;
  text-align: center;
  transition: border-color 0.2s;
  &:hover { border-color: #1353a4; }

  input[type="file"] { display: none; }  // triggered by <label>
}
```

---

## Assets

### `xpmile-logo.svg`

Custom SVG logo:
- Two crossing diagonal paths (blue `#1353a4`) representing route intersections.
- Orange `#ff6b35` filled circles at the four outer endpoints (origin/destination nodes).
- A darker blue `#0f2d52` filled circle at the centre crossing (hub).
- Wordmark: `"xp"` in bold blue + `"mile"` in light grey, monospace-style.
- Tagline: `"LAST MILE DELIVERY"` in small caps below the wordmark.

Used in: login left panel and the main header branding.

### `cross3.jpg`

Hero background photo used in the login right panel. Overlaid with `linear-gradient(135deg, rgba(15,45,82,0.75), rgba(19,83,164,0.55))` to maintain text readability.

---

## PDF generation — cross-component notes

pdfMake is imported in: `bulk`, `create-drs`, `list`, `multiple-shipment`, and the shared `LabelService` (which `create-manifest` and `in-inventory` use for A10 labels — they don't import pdfMake directly).

**Always:**
- Import and set `pdfMake.vfs` at module level (top of the `.ts` file).
- Build the full `docDef` object fresh inside the method that triggers the download.
- Never store `docDef` (or sub-objects like `body`) as class properties and mutate them later — the table body reference captured at class init time does not update when the property is reassigned.

**JsBarcode integration (create-drs):**
```typescript
textToBase64Barcode(text: string, height: number): string {
  const canvas = document.createElement('canvas');
  JsBarcode(canvas, text, { format: 'CODE128', width: 1, height, displayValue: false });
  return canvas.toDataURL('image/png');
}
```
`width: 1` narrows the bars so the barcode fits in the DRS table column at `width: 140` pts.

**A10 label fit-to-page pattern (LabelService):**

```typescript
content.push({
  image: this.barcodeDataUrl(sku, 120, 14),
  fit:   [fitW, fitH],         // ~103 × 72 pt for A10 landscape (1pt margins)
  alignment: 'center',
  pageBreak: i < count - 1 ? 'after' : undefined
});
```

Do **not** wrap the image in a table cell with a fixed `width:` — that scales the image to a fraction of the page and leaves the lower half blank. Use a top-level image node with `fit:` so pdfMake sizes the barcode to the page.

---

## Build notes

Production build command (as used in Docker):

```sh
NODE_OPTIONS=--max_old_space_size=1536 \
  npx ng build --configuration production --aot \
    --base-href /webui/ \
    --progress=false \
    --optimization=false \
    --build-optimizer=false \
    --extract-licenses=false
```

- `--optimization=false` — keeps bundle sizes smaller during build to avoid OOM (vfs_fonts is ~5 MB).
- `--base-href /webui/` — required because the C++ server serves the app under `/webui/`.
- `UI_BUST=$(date +%s)` build arg — passed as `ARG UI_BUST` in the Dockerfile; invalidates the Angular build cache layer without touching the C++ layer.

---

## Common pitfalls

| Pitfall | Cause | Fix |
|---|---|---|
| `[formControl]` TypeScript error on `FormGroup.controls[key]` | Returns `AbstractControl`, not `FormControl` | Use `$any()` cast: `[formControl]="$any(form.controls['field'])"` |
| pdfMake downloads empty PDF | `docDef.content.table.body` was set to an `undefined` value at class init; later array fill doesn't propagate | Build the entire `docDef` fresh inside the handler method |
| Bulk shipment AWB numbers are empty strings | Backend was echoing `awbno: ''` from the request body instead of generating | Backend now generates AWBs per shipment before inserting; uses `insert_many` |
| `ng build` killed (SIGKILL / OOM) | pdfMake's vfs_fonts is large; Node.js exhausted memory | `NODE_OPTIONS=--max_old_space_size=1536` in Dockerfile; retry usually succeeds |
| Header nav items top-aligned | Clarity's default flex layout overridden by custom styles without re-applying `align-items: center` | Add `display: inline-flex; align-items: center; height: 100%` to `.header-nav .nav-link.nav-text` |
| Dashboard cards different heights | Cards in separate `clr-row` rows couldn't stretch to each other | Merge all cards into one row; add `align-items: stretch` + `min-height` |
