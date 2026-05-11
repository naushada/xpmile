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

  readonly stats$ = new BehaviorSubject<ShipmentStats>(EMPTY_STATS);
  readonly loading$ = new BehaviorSubject<boolean>(false);

  private readonly intervalMs = 60_000;
  private account?: Account;
  private accountSub?: Subscription;
  private pollSub?: Subscription;

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

  private startPolling(): void {
    this.pollSub?.unsubscribe();
    this.pollSub = timer(0, this.intervalMs).pipe(
      switchMap(() => this.fetch())
    ).subscribe((shipments: Shipment[]) => {
      this.stats$.next(this.compute(shipments));
      this.loading$.next(false);
    });
  }

  private fetchOnce(): void {
    this.fetch().subscribe(s => {
      this.stats$.next(this.compute(s));
      this.loading$.next(false);
    });
  }

  // Backend stores createdOn as DD/MM/YYYY and compares lexicographically, so
  // dates must be in that exact format. For Admin/Employee, omit accountCode
  // to see across all customers; for Customer role, scope to their account.
  private fetch() {
    this.loading$.next(true);
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
  private compute(shipments: Shipment[]): ShipmentStats {
    const today = formatDate(new Date(), 'dd/MM/yyyy', 'en-GB');
    const out: ShipmentStats = { ...EMPTY_STATS };

    for (const s of shipments) {
      const acts: activityOnShipment[] = s.shipment?.shipmentInformation?.activity ?? [];
      if (!acts.length) continue;

      const todays = acts.filter(a => String(a.date) === today);
      if (!todays.length) continue;

      out.total++;
      const latest = todays[todays.length - 1];
      const evt = latest.event ?? '';

      if (evt === 'Document Prepared' || evt === 'Document Created') out.new++;
      else if (evt === 'In Scan at HUB' || evt === 'Arrived in HUB') out.inScan++;
      else if (evt === 'Out For Delivery')                            out.outForDelivery++;
      else if (evt === 'Proof of Delivery')                           out.delivered++;
      else if (
        evt === 'Shipment Returned to Sender' ||
        evt === 'Shiment Returned to Sending Station'
      )                                                                out.returned++;
    }
    return out;
  }

  ngOnDestroy(): void {
    this.accountSub?.unsubscribe();
    this.pollSub?.unsubscribe();
  }
}
