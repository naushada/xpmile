import { Component, OnDestroy } from '@angular/core';
import { formatDate } from '@angular/common';
import { Account } from 'src/common/app-globals';
import { PubsubsvcService } from 'src/common/pubsubsvc.service';
import { ShipmentStatsService } from 'src/common/shipment-stats.service';
import { SubSink } from 'subsink';

@Component({
  selector: 'app-dashboard',
  templateUrl: './dashboard.component.html',
  styleUrls: ['./dashboard.component.scss']
})
export class DashboardComponent implements OnDestroy {

  accountCode = '';
  // Bound to a <input type="month"> — value is "YYYY-MM".
  monthValue = formatDate(new Date(), 'yyyy-MM', 'en-US');
  monthLabel = formatDate(new Date(), 'MMMM y', 'en-US');

  private subsink = new SubSink();

  constructor(
    private pubsub: PubsubsvcService,
    public  stats: ShipmentStatsService
  ) {
    this.subsink.sink = this.pubsub.onAccount.subscribe(
      (account: Account | undefined) => {
        if (account) this.accountCode = account.loginCredentials.accountCode;
      }
    );
  }

  ngOnDestroy(): void {
    this.subsink.unsubscribe();
  }

  onMonthChange(value: string): void {
    // <input type="month"> emits "YYYY-MM"; build a Date on the 1st so the
    // service's setMonth() can normalise + filter consistently.
    const [y, m] = value.split('-').map(Number);
    if (!y || !m) return;
    const d = new Date(y, m - 1, 1);
    this.monthValue = value;
    this.monthLabel = formatDate(d, 'MMMM y', 'en-US');
    this.stats.setMonth(d);
  }

  onRefresh(): void {
    this.stats.refresh();
  }
}
