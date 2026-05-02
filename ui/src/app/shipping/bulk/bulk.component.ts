import { Component, OnDestroy, OnInit } from '@angular/core';
import '@cds/core/file/register.js';

import { HttpsvcService } from 'src/common/httpsvc.service';
import { FormBuilder, FormGroup } from '@angular/forms';
import { ExcelsvcService } from 'src/common/excelsvc.service';
import { Account, ShipmentExcelRow } from 'src/common/app-globals';
import { PubsubsvcService } from 'src/common/pubsubsvc.service';
import { formatDate } from '@angular/common';
import { SubSink } from 'subsink';
import * as XLSX from 'xlsx';

@Component({
  selector: 'app-bulk',
  templateUrl: './bulk.component.html',
  styleUrls: ['./bulk.component.scss']
})
export class BulkComponent implements OnInit, OnDestroy {

  loggedInUser?: Account;
  bulkShipmentForm: FormGroup;
  isButtonEnabled = true;

  private accountInfoList = new Map<string, Account>();
  private shipmentExcelRows: ShipmentExcelRow[] = [];
  private subsink = new SubSink();

  constructor(
    private http: HttpsvcService,
    private fb: FormBuilder,
    private xls: ExcelsvcService,
    private subject: PubsubsvcService
  ) {
    this.bulkShipmentForm = this.fb.group({ excelFileName: '' });
  }

  ngOnInit(): void {
    this.subsink.add(
      this.subject.onAccount.subscribe((rsp) => { this.loggedInUser = rsp; })
    );
  }

  ngOnDestroy(): void {
    this.subsink.unsubscribe();
  }

  onFileSelect(event: any): void {
    this.isButtonEnabled = true;
    this.processShipmentExcelFile(event, this.loggedInUser?.personalInfo.role ?? '');
  }

  onDownloadTemplate(): void {
    this.xls.createAndSaveShipmentTemplate('ShipmentTemplate');
  }

  onCreateBulkShipment(): void {
    const now   = new Date();
    const today = formatDate(now, 'dd/MM/yyyy', 'en-GB');
    const time  = `${now.getHours()}:${now.getMinutes()}`;

    const bulkShipment: any[] = [];

    this.shipmentExcelRows.forEach((ent: ShipmentExcelRow) => {
      const senderInfo = this.accountInfoList.get(ent.AccountCode);
      if (!senderInfo) return;

      const shipment = this.fb.group({
        isAutoGenerate: true,
        awbno:          '',
        altRefNo:       ent.AlternateReferenceNo,

        senderInformation: this.fb.group({
          accountNo:      ent.AccountCode,
          referenceNo:    ent.ReferenceNo,
          name:           ent.SenderName || senderInfo.personalInfo.name,
          companyName:    senderInfo.customerInfo.companyName,
          country:        senderInfo.personalInfo.city,
          city:           senderInfo.personalInfo.city,
          state:          senderInfo.personalInfo.state,
          address:        senderInfo.personalInfo.address,
          postalCode:     senderInfo.personalInfo.postalCode,
          contact:        senderInfo.personalInfo.contact,
          phoneNumber:    senderInfo.personalInfo.contact,
          email:          senderInfo.personalInfo.email,
          receivingTaxId: senderInfo.customerInfo.vat
        }),

        shipmentInformation: this.fb.group({
          activity: this.fb.array([
            this.fb.group({
              date:          today,
              event:         'Document Created',
              time:          time,
              notes:         'Document Created',
              driver:        '',
              updatedBy:     this.loggedInUser?.personalInfo.name ?? '',
              eventLocation: this.loggedInUser?.personalInfo.eventLocation ?? ''
            })
          ]),
          skuNo:            '',
          service:          'Non Document',
          numberOfItems:    1,
          goodsDescription: ent.GoodsDescription,
          goodsValue:       ent.CustomsValue,
          customsValue:     ent.CustomsValue,
          codAmount:        ent.CodAmount,
          vat:              '',
          currency:         ent.CustomsCurrency,
          weight:           ent.Weight,
          weightUnits:      'KG',
          cubicWeight:      '',
          createdOn:        today,
          createdBy:        this.loggedInUser?.personalInfo.name ?? '',
          hsCode:           ent.HSCode
        }),

        receiverInformation: this.fb.group({
          name:       ent.ReceiverName,
          country:    ent.ReceiverCountry,
          city:       ent.ReceiverCity,
          state:      ent.ReceiverCity,
          postalCode: '',
          contact:    ent.ReceiverPhoneNo,
          address:    ent.ReceiverAddress,
          phone:      ent.ReceiverAlternatePhoneNo,
          email:      ''
        })
      });

      bulkShipment.push({ shipment: { ...shipment.value } });
    });

    if (this.accountInfoList.size) {
      this.subsink.add(
        this.http.createBulkShipment(JSON.stringify(bulkShipment)).subscribe({
          next: (rsp: any) => alert(`Shipments created: ${rsp.createdShipments}`)
        })
      );
    }

    this.accountInfoList.clear();
    this.shipmentExcelRows = [];
  }

  private deduplicate(data: string[]): string[] {
    return [...new Set(data)];
  }

  private processShipmentExcelFile(evt: any, accountType: string): void {
    if (!evt.target.files[0]) {
      this.isButtonEnabled = true;
      return;
    }

    const accList: string[] = [];
    const fileReader = new FileReader();
    fileReader.readAsBinaryString(evt.target.files[0]);

    fileReader.onload = (event) => {
      const wb = XLSX.read(event.target?.result, { type: 'binary' });
      wb.SheetNames.forEach(sheet => {
        const rows = XLSX.utils.sheet_to_json(wb.Sheets[sheet]) as any[];
        rows.forEach(row => {
          accList.push(row.AccountCode);
          this.shipmentExcelRows.push(new ShipmentExcelRow(row));
        });
      });
    };

    fileReader.onloadend = () => {
      this.isButtonEnabled = false;
      if (accountType === 'Employee' || accountType === 'Admin') {
        this.deduplicate(accList).forEach(code => {
          this.subsink.add(
            this.http.getCustomerInfo(code).subscribe({
              next:  (data: Account) => { this.accountInfoList.set(data.loginCredentials.accountCode, data); },
              error: () => { alert('Invalid AccountCode'); this.isButtonEnabled = true; }
            })
          );
        });
      } else {
        alert('Bulk Upload is not supported for your Account');
      }
    };

    fileReader.onerror = () => {
      alert('Excel file is invalid');
      this.isButtonEnabled = true;
    };
  }
}
