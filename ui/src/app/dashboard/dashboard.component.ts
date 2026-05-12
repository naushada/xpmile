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
  today = formatDate(new Date(), 'EEE, d MMM y', 'en-US');
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

  onRefresh(): void {
    this.stats.refresh();
  }
}
