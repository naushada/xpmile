import { Component, OnDestroy } from '@angular/core';
import { formatDate } from '@angular/common';
import * as Excel from 'exceljs';
import * as fs from 'file-saver';
import { Account, activityOnShipment } from 'src/common/app-globals';
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
  downloadingReport = false;

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

  // Download the monthly shipment report as .xlsx. Pulls from the cached
  // shipment list maintained by ShipmentStatsService so this does not issue
  // an additional API call. Columns: AWB No., Shipment Status, Updated Date
  // & Time — taken from the latest activity entry on each shipment.
  onDownloadReport(): void {
    if (this.downloadingReport) return;
    const shipments = this.stats.getMonthlyShipments();
    if (!shipments.length) {
      window.alert(`No shipments found for ${this.monthLabel}.`);
      return;
    }
    this.downloadingReport = true;

    const workbook = new Excel.Workbook();
    const worksheet = workbook.addWorksheet(`Report ${this.monthValue}`);
    worksheet.properties.defaultRowHeight = 20;
    worksheet.properties.defaultColWidth = 20;
    worksheet.pageSetup.paperSize = 9;
    worksheet.pageSetup.orientation = 'landscape';

    worksheet.columns = [
      { header: 'AWB No.',              key: 'awbno',      width: 24 },
      { header: 'Shipment Status',      key: 'status',     width: 32 },
      { header: 'Updated Date & Time',  key: 'updatedOn',  width: 26 }
    ];
    worksheet.getRow(1).font = { bold: true };
    worksheet.views = [{ state: 'frozen', xSplit: 0, ySplit: 1 }];

    for (const s of shipments) {
      const acts: activityOnShipment[] = s.shipment?.shipmentInformation?.activity ?? [];
      const latest = acts.length ? acts[acts.length - 1] : undefined;
      worksheet.addRow({
        awbno:     s.shipment?.awbno ?? '',
        status:    latest?.event ?? '',
        updatedOn: this.formatActivityTimestamp(latest)
      });
    }

    workbook.xlsx.writeBuffer().then((data) => {
      const blob = new Blob([data], {
        type: 'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet'
      });
      fs.saveAs(blob, `MonthlyReport_${this.monthValue}.xlsx`);
      this.downloadingReport = false;
    }, () => {
      this.downloadingReport = false;
    });
  }

  // Activity `date` and `time` are typed as Date/Time but stored on the
  // wire as strings (e.g. "12/05/2026" and "14:23") — see excelsvc.service.
  // String() handles both shapes safely.
  private formatActivityTimestamp(a: activityOnShipment | undefined): string {
    if (!a) return '';
    const date = String(a.date ?? '').trim();
    const time = String(a.time ?? '').trim();
    if (!date && !time) return '';
    if (!time) return date;
    return `${date} ${time}`;
  }
}
