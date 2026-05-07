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
       └─ Reporting components (detailed-report, invoice, rnav-bar)
```

Navigation between sections is wired through the sidebar nav in the shell, not through Angular router child routes — each nav item calls a method that sets a flag/input to show the right sub-component.

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

---

### `ExcelsvcService`

Wraps SheetJS to generate the bulk-upload template:
- `createAndSaveShipmentTemplate(filename)` — creates a workbook with the correct column headers (`AccountCode`, `ReferenceNo`, etc.) and triggers browser download.

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
- `clr-header` with the xpmile logo and top nav links.
- `clr-vertical-nav` sidebar — the active section is controlled by a string flag.
- `<router-outlet>` or direct `*ngIf`-switched sub-components depending on the section.

The sidebar nav HTML files (`submenu`, `snav-bar`, `rnav-bar`, `anav-bar`, `inav-bar`) contain `<a class="nav-link nav-text">` items — no `<b>` tags (stripped for cleaner font rendering).

---

## Dashboard (`app/dashboard/`)

Five stat cards in a single `clr-row.cards-row`:
- Created Today, Total Active, Delivered, Returned, COD Pending.
- All in `clr-col-lg-4` columns (`align-items: stretch` on the row, `min-height: 160px` on cards) so they stay the same height regardless of content.
- Card icons use gradient backgrounds (`__blue`, `__teal`, `__red`, `__yellow`, `__green` modifiers in `dashboard.component.scss`).

Data is fetched from the shipment API and counted client-side by status.

---

## Shipping components

### `single/` — Create Single Shipment

Reactive form (`FormBuilder`) with three groups:
- `senderInformation` — account-linked fields (auto-filled from account lookup)
- `shipmentInformation` — service type, weight, goods, COD, customs
- `receiverInformation` — name, country, city, address, phone

`isAutoGenerate: true` → backend generates the AWB via `next_awbno(prefix)`.

Compact spacing: `clr-input-container`, `clr-select-container`, `clr-checkbox-container` get `margin-bottom: 6px !important` and `.clr-subtext-wrapper { display: none }` in the component SCSS.

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

Builds a manifest document (A10 label PDF) for a set of shipment AWBs. Uses pdfMake.

### `in-inventory` / `out-inventory` / `find-inventory` / `update-inventory`

Standard CRUD components against `/api/v1/inventory`. Each uses `clr-datagrid` for display.

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

pdfMake is imported in: `bulk`, `create-drs`, `list`, `create-manifest`, `multiple-shipment`.

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
