# xpmile UI

Angular 14 single-page application for the xpmile last-mile logistics platform. Served from the C++ backend at `/webui/` after production build.

---

## Tech stack

| Tool | Version | Purpose |
|---|---|---|
| Angular | 14 | Framework |
| Clarity Design System (`@clr/ui`, `@cds/core`) | — | Component library & icons |
| pdfMake | — | Client-side PDF generation (labels, reports, DRS) |
| SheetJS (`xlsx`) | — | Excel file parsing for bulk upload |
| SubSink | — | Subscription cleanup on `ngOnDestroy` |
| TypeScript | — | Language |

---

## Project structure

```
ui/
├── src/
│   ├── app/
│   │   ├── app.module.ts           Root module — imports all feature modules
│   │   ├── app-routing.module.ts   Top-level routes: /login, /main, /dashboard
│   │   ├── login/                  Two-panel login page + password reset
│   │   ├── main/                   Shell layout — header, sidebar nav, router-outlet
│   │   ├── dashboard/              Summary cards (shipments, deliveries, COD)
│   │   ├── shipping/               All shipment operations (see below)
│   │   ├── tracking/               Shipment tracking & update flows
│   │   ├── inventory/              Manifest creation, in/out inventory, find
│   │   ├── accounting/             Account CRUD (create, list, update)
│   │   ├── reporting/              Detailed reports & invoice generation
│   │   └── common/                 Shared layout components
│   ├── common/
│   │   ├── app-globals.ts          Shared interfaces, UriMap (API endpoints), ShipmentExcelRow
│   │   ├── httpsvc.service.ts      All HTTP calls — thin wrappers over Angular HttpClient
│   │   ├── pubsubsvc.service.ts    BehaviorSubject bus (logged-in account, selected shipment)
│   │   ├── excelsvc.service.ts     Excel template generation (SheetJS)
│   │   └── types.d.ts              Module declarations for pdfMake, xlsx
│   ├── assets/
│   │   └── images/
│   │       ├── xpmile-logo.svg     Custom SVG logo (crossing routes + wordmark)
│   │       └── cross3.jpg          Login hero background
│   ├── environments/               environment.ts / environment.prod.ts (apiUrl)
│   └── styles.scss                 Global styles — header, sidebar nav, Clarity overrides
├── angular.json
├── tsconfig.json
└── package.json
```

---

## Shipping module components

| Component | Nav label | Purpose |
|---|---|---|
| `single` | Create Single Shipment | Reactive form — single shipment creation, auto-generates AWB |
| `bulk` | Create Bulk Shipment | Excel upload → parse → POST bulk; results table + auto PDF download |
| `altref-bulk` | Update Bulk ALT REF. Number | Excel upload → bulk ALT REF update |
| `third-party` | Create Third Party Shipment | Vendor selection + Excel upload |
| `list` | Shipment List | Search/filter table, A2/A4/A6 label PDF, copy AWB |
| `collect-shipment` | Collect Shipment | Assign driver, finalise, cancel; stacked actions in one table column |
| `create-drs` | Create DRS | Multi-AWB filter, barcode per row, Export PDF (A4 landscape) |
| `modify` | Update Shipment | Update shipment fields |
| `api-integration` | API Integration | Documentation / key display |

---

## Common services

### `HttpsvcService`

Single service for all backend calls. Resolves endpoint URLs through `UriMap` (defined in `app-globals.ts`) and prefixes them with `environment.apiUrl` when non-empty.

Key methods:

| Method | API endpoint |
|---|---|
| `getShipmentByAwbNo(awb)` | GET `/api/v1/shipment/shipping` |
| `getShipments(from, to, ...)` | GET `/api/v1/shipment/shipping` |
| `createShipment(payload)` | POST `/api/v1/shipment/single/shipping` |
| `createBulkShipment(payload)` | POST `/api/v1/shipment/bulk/shipping` |
| `getAccountInfo(id, pwd?)` | GET `/api/v1/account/account` |
| `getCustomerInfo(accountCode)` | GET `/api/v1/account/account` |
| `updateShipmentStatus(awbs, data)` | PUT `/api/v1/shipment/shipping` |

### `PubsubsvcService`

Global state bus using RxJS `BehaviorSubject`. Components subscribe via:

- `onAccount` — emits the logged-in `Account` object after login; consumed by most pages to get `role`, `accountCode`, event location, etc.
- `onShipment` — emits a selected `Shipment` for cross-component navigation (e.g. list → update).
- `onAccountList` — emits all accounts (used by admin-only views).

### `ExcelsvcService`

Generates the ShipmentTemplate Excel file for bulk upload using SheetJS (`XLSX`). Call `createAndSaveShipmentTemplate(filename)` to trigger browser download.

---

## Bulk shipment flow

1. User uploads `.xls/.xlsx` via the custom upload card (hidden `<input type="file">` triggered by a `<label>`).
2. `processShipmentExcelFile()` reads the file with `FileReader` + SheetJS, builds `ShipmentExcelRow[]` and collects unique `AccountCode` values.
3. For Employee/Admin roles, fetches each account's info from `/api/v1/account/account` to resolve sender details.
4. On "Create Bulk Shipment": builds a JSON array of shipment objects (with `isAutoGenerate: true` and `awbno: ''`), POSTs to `/api/v1/shipment/bulk/shipping`.
5. Backend generates real AWB numbers (prefix looked up from account's `awbPrefix` field) and returns `{ createdShipments: N, awbNumbers: [...] }`.
6. UI populates the results table and **auto-downloads a PDF report** (`BulkShipment-DD-MM-YYYY.pdf`) with the same columns: S. No., AWB Number, Alt. Ref. No., Receiver, City, Status.

---

## PDF generation

pdfMake is used across multiple components. Each component imports and initialises it locally:

```typescript
import pdfMake from 'pdfmake/build/pdfmake';
import pdfFonts from 'pdfmake/build/vfs_fonts';
pdfMake.vfs = (pdfFonts as any).pdfMake.vfs;
```

The document definition is always built **fresh inside the handler method** — never stored as a class property — to avoid stale reference bugs when the method is called more than once.

---

## Global styling conventions

`src/styles.scss` sets the baseline for the Clarity shell:

- **Header** (`clr-header`, `.header`): deep navy `#0f2d52`; nav links use `display: inline-flex; align-items: center; height: 100%` to stay vertically centred.
- **Sidebar** (`clr-vertical-nav`): white background, `border-right: 1px solid #e2e8f0`, left-border accent on hover (`#93c5fd`) and active (`#1b4d8e`).
- **Form spacing**: `clr-input-container`, `clr-select-container`, `clr-checkbox-container` use `margin-bottom: 6px !important` and hidden `.clr-subtext-wrapper` for compact layouts.
- **Upload cards**: custom dashed-border card (`border: 2px dashed #c2ccd6`) with a hidden `<input type="file">` triggered by a `<label>`. Used in bulk, altref-bulk, and third-party components.

---

## Development

```sh
cd ui
npm install
npx ng serve
```

For production builds (same flags as Docker):

```sh
npx ng build --configuration production --aot --base-href /webui/
```

To force a UI cache-bust in the Docker build without rebuilding the C++ layer:

```sh
UI_BUST=$(date +%s) podman-compose up -d --build app
```

> The Angular build sets `NODE_OPTIONS=--max_old_space_size=1536` in the Dockerfile because pdfMake's `vfs_fonts` bundle is large and can OOM the Node process on constrained hosts.

---

## Role-based behaviour

The `Account.personalInfo.role` field gates several features:

| Role | Bulk upload | View all accounts |
|---|---|---|
| `Admin` | Yes | Yes |
| `Employee` | Yes | Yes |
| Customer | No (alert shown) | No |
