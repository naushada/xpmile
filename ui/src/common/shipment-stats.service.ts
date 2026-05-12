import { Injectable, OnDestroy } from '@angular/core';
import { BehaviorSubject, Subscription, timer, of } from 'rxjs';
import { switchMap, catchError } from 'rxjs/operators';
import { HttpsvcService } from './httpsvc.service';
import { PubsubsvcService } from './pubsubsvc.service';
import { Account, Shipment, activityOnShipment } from './app-globals';
import { formatDate } from '@angular/common';

export interface ShipmentStats {
  total: number;
  new: number;
  inScan: number;
  outForDelivery: number;
  delivered: number;
  returned: number;
}

const EMPTY_STATS: ShipmentStats = {
  total: 0, new: 0, inScan: 0, outForDelivery: 0, delivered: 0, returned: 0
};

@Injectable({ providedIn: 'root' })
export class ShipmentStatsService implements OnDestroy {

  // Today-only — drives the live subnav strip
  readonly stats$ = new BehaviorSubject<ShipmentStats>(EMPTY_STATS);
  readonly loading$ = new BehaviorSubject<boolean>(false);

  // Monthly — drives the Dashboard. Date is normalised to the 1st of the
  // selected month so consumers can use it as both "active month" key and
  // as the value bound to a month-picker.
  readonly monthly$ = new BehaviorSubject<ShipmentStats>(EMPTY_STATS);
  readonly monthlyLoading$ = new BehaviorSubject<boolean>(false);
  readonly month$ = new BehaviorSubject<Date>(this.firstOfMonth(new Date()));

  private readonly intervalMs = 60_000;
  private account?: Account;
  private accountSub?: Subscription;
  private pollSub?: Subscription;
  // Cache the most recent fetch so setMonth() can recompute monthly stats
  // without making another network call. Today's stats stay live via the
  // 60 s poll regardless.
  private lastShipments: Shipment[] = [];

  constructor(
    private http: HttpsvcService,
    private pubsub: PubsubsvcService
  ) {
    this.accountSub = this.pubsub.onAccount.subscribe((acc: Account | undefined) => {
      if (acc && acc.loginCredentials?.accountCode) {
        const isNewAccount = this.account?.loginCredentials?.accountCode
                          !== acc.loginCredentials.accountCode;
        this.account = acc;
        if (isNewAccount) this.startPolling();
      }
    });
  }

  refresh(): void {
    if (this.account) this.fetchOnce();
  }

  /** Change the active dashboard month and recompute monthly stats from
   *  the cached shipment list. `d` may be any day in the target month;
   *  it's normalised to the 1st internally. */
  setMonth(d: Date): void {
    const norm = this.firstOfMonth(d);
    this.month$.next(norm);
    this.monthly$.next(this.computeMonthly(this.lastShipments, norm));
  }

  /** Shipments whose createdOn falls in the currently-selected dashboard
   *  month. Drawn from the cached fetch so callers (e.g. report download)
   *  don't trigger another network round-trip. */
  getMonthlyShipments(): Shipment[] {
    const month = this.month$.value;
    const m = month.getMonth();
    const y = month.getFullYear();
    return this.lastShipments.filter(s => {
      const createdOn = String(s.shipment?.shipmentInformation?.createdOn ?? '');
      return this.dateInMonth(createdOn, m, y);
    });
  }

  private startPolling(): void {
    this.pollSub?.unsubscribe();
    this.pollSub = timer(0, this.intervalMs).pipe(
      switchMap(() => this.fetch())
    ).subscribe((shipments: Shipment[]) => this.publish(shipments));
  }

  private fetchOnce(): void {
    this.fetch().subscribe(s => this.publish(s));
  }

  private publish(shipments: Shipment[]): void {
    this.lastShipments = shipments;
    this.stats$.next(this.computeToday(shipments));
    this.monthly$.next(this.computeMonthly(shipments, this.month$.value));
    this.loading$.next(false);
    this.monthlyLoading$.next(false);
  }

  // Backend stores createdOn as DD/MM/YYYY and compares lexicographically, so
  // dates must be in that exact format. For Admin/Employee, omit accountCode
  // to see across all customers; for Customer role, scope to their account.
  private fetch() {
    this.loading$.next(true);
    this.monthlyLoading$.next(true);
    const today = formatDate(new Date(), 'dd/MM/yyyy', 'en-GB');
    const fromDate = '01/01/2020';
    const role = this.account?.personalInfo?.role ?? '';
    const accCode = role === 'Customer'
      ? this.account?.loginCredentials?.accountCode
      : undefined;

    return this.http.getShipmentsList(fromDate, today, accCode).pipe(
      // Backend returns 400 + JSON {"cause":..., "error":400} when zero docs
      // match — treat as "empty result" so the badges show 0, not stale.
      catchError(() => of([] as Shipment[]))
    );
  }

  // Activity entries store date as DD/MM/YYYY (see single.component buildForm),
  // so compare against today in the same format. For each shipment that had
  // any activity today, bucket on the latest event from today.
  private computeToday(shipments: Shipment[]): ShipmentStats {
    const today = formatDate(new Date(), 'dd/MM/yyyy', 'en-GB');
    const out: ShipmentStats = { ...EMPTY_STATS };

    for (const s of shipments) {
      const acts: activityOnShipment[] = s.shipment?.shipmentInformation?.activity ?? [];
      if (!acts.length) continue;

      const todays = acts.filter(a => String(a.date) === today);
      if (!todays.length) continue;

      out.total++;
      this.bucketByEvent(todays[todays.length - 1].event ?? '', out);
    }
    return out;
  }

  // Monthly: shipments whose createdOn falls within `month`. Each shipment
  // is bucketed by its overall latest activity event (the current status),
  // not just events that happened in the month — so a December-created
  // shipment delivered in January doesn't count toward December's Delivered.
  private computeMonthly(shipments: Shipment[], month: Date): ShipmentStats {
    const out: ShipmentStats = { ...EMPTY_STATS };
    const m = month.getMonth();
    const y = month.getFullYear();

    for (const s of shipments) {
      const acts: activityOnShipment[] = s.shipment?.shipmentInformation?.activity ?? [];
      if (!acts.length) continue;

      // createdOn is stored in the shipmentInformation as DD/MM/YYYY (string)
      // or sometimes as an ISO date — handle both.
      const createdOn = String(s.shipment?.shipmentInformation?.createdOn ?? '');
      if (!this.dateInMonth(createdOn, m, y)) continue;

      out.total++;
      const latest = acts[acts.length - 1];
      this.bucketByEvent(latest.event ?? '', out);
    }
    return out;
  }

  private bucketByEvent(evt: string, out: ShipmentStats): void {
    if (evt === 'Document Prepared' || evt === 'Document Created')    out.new++;
    else if (evt === 'In Scan at HUB' || evt === 'Arrived in HUB')    out.inScan++;
    else if (evt === 'Out For Delivery')                              out.outForDelivery++;
    else if (evt === 'Proof of Delivery')                             out.delivered++;
    else if (
      evt === 'Shipment Returned to Sender' ||
      evt === 'Shiment Returned to Sending Station'
    )                                                                  out.returned++;
  }

  // Accepts DD/MM/YYYY or YYYY-MM-DDTHH:mm:ss(Z). Returns true if the date
  // falls in the given (zero-indexed month, full year). Anything we can't
  // parse returns false rather than throwing.
  private dateInMonth(s: string, month: number, year: number): boolean {
    if (!s) return false;
    let day: number, mon: number, yr: number;
    const ddmmyyyy = /^(\d{1,2})\/(\d{1,2})\/(\d{4})$/;
    const iso      = /^(\d{4})-(\d{2})-(\d{2})/;
    const m1 = ddmmyyyy.exec(s);
    const m2 = iso.exec(s);
    if (m1)      { day = +m1[1]; mon = +m1[2] - 1; yr = +m1[3]; }
    else if (m2) { yr  = +m2[1]; mon = +m2[2] - 1; day = +m2[3]; }
    else return false;
    return mon === month && yr === year && day >= 1 && day <= 31;
  }

  private firstOfMonth(d: Date): Date {
    return new Date(d.getFullYear(), d.getMonth(), 1);
  }

  ngOnDestroy(): void {
    this.accountSub?.unsubscribe();
    this.pollSub?.unsubscribe();
  }
}
