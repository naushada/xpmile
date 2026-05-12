# Dashboard — Monthly Shipment Report

The Angular Dashboard exposes a **Download Report** action that exports the
shipments for the currently-selected month as an Excel workbook. The
feature is fully client-side — the workbook is built in the browser from
the shipment list that `ShipmentStatsService` already caches for the
dashboard cards, so clicking the button does **not** issue an additional
backend request.

Source files:

| Concern                  | File                                                    |
|--------------------------|---------------------------------------------------------|
| Button + click handler   | `ui/src/app/dashboard/dashboard.component.{ts,html,scss}` |
| Cached-shipment accessor | `ui/src/common/shipment-stats.service.ts` (`getMonthlyShipments()`) |
| Excel writer             | `exceljs` + `file-saver` (already in `ui/package.json`) |

Introduced in commit
[`07ba8de`](https://github.com/naushada/xpmile/commit/07ba8de3255fb743a776c0d9fa1c3298295aa82e)
(PR #6).

---

## What gets exported

One row per shipment whose `createdOn` falls in the selected month.
The latest entry in `shipment.shipmentInformation.activity` provides the
current status and the most recent update timestamp.

| Column                | Source                                  |
|-----------------------|-----------------------------------------|
| AWB No.               | `shipment.awbno`                        |
| Shipment Status       | latest `activity[].event`               |
| Updated Date & Time   | latest `activity[].date` + `activity[].time` |

Output filename: `MonthlyReport_<YYYY-MM>.xlsx` (e.g.
`MonthlyReport_2026-05.xlsx`). The sheet is named `Report <YYYY-MM>` and
has a frozen header row.

If the active month contains no shipments, the user is shown a `No
shipments found for <Month YYYY>` alert and no file is written.

---

## Where the data comes from

`ShipmentStatsService` polls `getShipmentsList(...)` every 60 s and keeps
the result in a private `lastShipments` array. The dashboard's monthly
cards already consume this cache via `monthly$`. For the report,
`getMonthlyShipments()` returns the same array filtered to the active
month (`month$`):

```ts
// shipment-stats.service.ts
getMonthlyShipments(): Shipment[] {
  const month = this.month$.value;
  const m = month.getMonth();
  const y = month.getFullYear();
  return this.lastShipments.filter(s => {
    const createdOn = String(s.shipment?.shipmentInformation?.createdOn ?? '');
    return this.dateInMonth(createdOn, m, y);
  });
}
```

`dateInMonth()` accepts both `DD/MM/YYYY` and ISO `YYYY-MM-DD[T...]`
forms — see the service for the parsing details.

Account scoping mirrors the rest of the dashboard:

- **Customer** role — only that customer's shipments (server-side filter
  via `accountCode`).
- **Admin / Employee** — all customers.

---

## Progress UX

The handler sets `downloadingReport = true` synchronously on click and
resets it when `workbook.xlsx.writeBuffer()` resolves (or rejects).
While the flag is set:

1. The **Download Report** button switches its icon to `sync` and spins,
   the label changes to **Preparing…**, and the button is disabled.
2. A small progress chip — *Preparing monthly report…* — appears under
   the dashboard header subtitle.

Both indicators clear once `file-saver` has handed the blob to the
browser. The button is also disabled while the monthly stats fetch
itself is in flight (`stats.monthlyLoading$`).

`prefers-reduced-motion: reduce` suppresses the spinner animations.

---

## Implementation notes

- **No extra HTTP call.** The export always works against the most
  recent cached fetch. If the cache is empty (e.g. user just switched
  accounts and the first poll hasn't returned), the user gets the
  empty-month alert; clicking **Refresh** first and then **Download
  Report** is the workaround.
- **Activity field shape.** `activityOnShipment.date` and `.time` are
  typed as `Date` / `Time` in `app-globals.ts`, but the wire payload
  stores them as strings (e.g. `"12/05/2026"` and `"14:23"`). The
  handler wraps both in `String(... ?? '')` to handle either shape
  safely — same pattern used by `ExcelsvcService.exportToExcel()`.
- **Column widths** are picked to fit a typical AWB and event name on
  one row without wrapping; adjust in
  `DashboardComponent.onDownloadReport()` if your AWB or event-name
  conventions are longer.
- **Bundle impact.** `exceljs` and `file-saver` were already imported
  by `ExcelsvcService`, so adding this feature did not change the
  initial-bundle size beyond a few KB of new component code.

---

## Limitations / future work

- Only the **latest** activity event per shipment is exported. A full
  audit-trail variant (one row per activity entry) would be a
  straightforward additional column-set in the same workbook.
- The export is limited to whatever the dashboard's poll already
  fetched — currently `getShipmentsList('01/01/2020', today, ...)`. For
  very large customer accounts a server-side, streamed export endpoint
  would be more appropriate.
- PDF export is **not** wired up here — the existing PDF flow lives in
  the on-prem Vaadin Dashboard (see `docs/onprem-ui-design.md`).
