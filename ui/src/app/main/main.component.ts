import { Component, Input, OnDestroy, OnInit } from '@angular/core';
import { formatDate } from '@angular/common';
import { Account } from 'src/common/app-globals';
import { PubsubsvcService } from 'src/common/pubsubsvc.service';
import { ShipmentStatsService } from 'src/common/shipment-stats.service';
import { SubSink } from 'subsink';

@Component({
  selector: 'app-main',
  templateUrl: './main.component.html',
  styleUrls: ['./main.component.scss']
})
export class MainComponent implements OnInit, OnDestroy {

  @Input() selectedNavItem: string = "";
  private selectedItem: string = "";

  loggedInUser?: Account;
  today = this.formatToday();
  flashOn = false;

  subsink = new SubSink();

  constructor(private pubsub: PubsubsvcService, public stats: ShipmentStatsService) {
    this.subsink.sink = this.pubsub.onAccount.subscribe(
      rsp => { this.loggedInUser = { ...(rsp as Account) }; }
    );

    this.subsink.sink = this.stats.stats$.subscribe(() => {
      this.today = this.formatToday();
      this.flashOn = true;
      setTimeout(() => { this.flashOn = false; }, 700);
    });
  }

  private formatToday(): string {
    return formatDate(new Date(), 'EEE, d MMM', 'en-US');
  }

  ngOnInit(): void {
    this.onMenuSelect('shipping');
    this.onReceiveEvt('singleShipment');
  }

  ngOnDestroy(): void {
    this.subsink.unsubscribe();
  }

  public onMenuSelect(opt: string) {
    this.selectedMenuItem = opt;
  }

  public get selectedMenuItem(): string {
    return this.selectedItem;
  }

  public set selectedMenuItem(item: string) {
    this.selectedItem = item;
  }

  public onReceiveEvt(navItem: any): void {
    this.selectedNavItem = navItem;
  }
}
