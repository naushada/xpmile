import { Component, OnDestroy, OnInit } from '@angular/core';
import { FormBuilder, FormGroup } from '@angular/forms';
import { Account, Shipment, activityOnShipment } from 'src/common/app-globals';
import { HttpsvcService } from 'src/common/httpsvc.service';
import { PubsubsvcService } from 'src/common/pubsubsvc.service';
import { SubSink } from 'subsink';

@Component({
  selector: 'app-dashboard',
  templateUrl: './dashboard.component.html',
  styleUrls: ['./dashboard.component.scss']
})
export class DashboardComponent implements OnInit, OnDestroy {

  dashboardForm: FormGroup;
  isLoading = false;

  total           = 0;
  createdToday    = 0;
  delivered       = 0;
  returnedToSender = 0;
  cancelled       = 0;

  private accountCode = '';
  private subsink = new SubSink();

  constructor(
    private fb: FormBuilder,
    private http: HttpsvcService,
    private pubsub: PubsubsvcService
  ) {
    this.dashboardForm = fb.group({ isRefreshTriggered: false });
  }

  ngOnInit(): void {
    this.subsink.add(
      this.pubsub.onAccount.subscribe((account: Account | undefined) => {
        if (account) {
          this.accountCode = account.loginCredentials.accountCode;
          this.loadStats();
        }
      })
    );
  }

  ngOnDestroy(): void {
    this.subsink.unsubscribe();
  }

  onRefresh(evt: any): void {
    if (evt.currentTarget.checked && this.accountCode) {
      this.loadStats();
    }
    this.dashboardForm.patchValue({ isRefreshTriggered: false });
  }

  private loadStats(): void {
    this.isLoading = true;
    const today = new Date().toISOString().split('T')[0];

    this.subsink.add(
      this.http.getShipmentsForAccount(this.accountCode).subscribe(
        (shipments: Shipment[]) => { this.computeStats(shipments, today); },
        ()  => { this.isLoading = false; },
        ()  => { this.isLoading = false; }
      )
    );
  }

  private computeStats(shipments: Shipment[], today: string): void {
    let total = 0, createdToday = 0, delivered = 0, returnedToSender = 0, cancelled = 0;

    for (const s of shipments) {
      total++;
      const si   = s.shipment?.shipmentInformation;
      const acts: activityOnShipment[] = si?.activity ?? [];
      const createdOn: string = (si?.createdOn as any) ?? '';

      if (createdOn.startsWith(today)) createdToday++;

      const has = (evt: string) => acts.some(a => a.event === evt);
      if (has('Proof of Delivery'))                   delivered++;
      if (has('Shipment Returned to Sender') ||
          has('Shiment Returned to Sending Station')) returnedToSender++;
      if (has('User Initiated Shipment Cancellation')) cancelled++;
    }

    this.total            = total;
    this.createdToday     = createdToday;
    this.delivered        = delivered;
    this.returnedToSender = returnedToSender;
    this.cancelled        = cancelled;
  }
}
