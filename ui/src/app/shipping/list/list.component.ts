import { Component, OnDestroy, OnInit } from '@angular/core';
import { FormBuilder, FormGroup } from '@angular/forms';
import { Account, Shipment } from 'src/common/app-globals';
import { HttpsvcService } from 'src/common/httpsvc.service';
import { formatDate } from '@angular/common';
import { SubSink } from 'subsink';
import { PubsubsvcService } from 'src/common/pubsubsvc.service';

import * as JsBarcode from 'jsbarcode';
import pdfMake from 'pdfmake/build/pdfmake';
import pdfFonts from 'pdfmake/build/vfs_fonts';

pdfMake.vfs = pdfFonts.pdfMake.vfs;

@Component({
  selector: 'app-list',
  templateUrl: './list.component.html',
  styleUrls: ['./list.component.scss']
})
export class ListComponent implements OnInit, OnDestroy {

  shipmentListForm: FormGroup;
  loggedInUser?: Account;
  shipments: Shipment[] = [];
  rowsSelected: Shipment[] = [];

  private subsink = new SubSink();

  // Mutated in place so the docDefinition content reference stays valid across builds
  private readonly a2Body:       object[] = [];
  private readonly a4Body:       object[] = [];
  private readonly a6Body:       object[] = [];
  private readonly invoiceBody:  object[] = [];

  private readonly pdfStyles = {
    header:       { fontSize: 18, bold: true,  margin: [0, 0, 0, 10] as any },
    subheader:    { fontSize: 16, bold: true,  margin: [0, 10, 0, 5] as any },
    tableExample: { margin: [0, 5, 0, 15] as any },
    tableHeader:  { bold: true, fontSize: 13, color: 'black' },
    rH:           { height: 100, fontSize: 10 }
  };

  private readonly docInfo = {
    title:    'xpmile Label',
    author:   'Mohd Naushad Ahmed',
    subject:  'Shipment Label',
    keywords: 'label shipment'
  };

  constructor(
    private fb: FormBuilder,
    private http: HttpsvcService,
    private subject: PubsubsvcService
  ) {
    this.shipmentListForm = this.fb.group({
      startDate: [formatDate(new Date(), 'MM/dd/yyyy', 'en')],
      endDate:   [formatDate(new Date(), 'MM/dd/yyyy', 'en')]
    });
  }

  ngOnInit(): void {
    this.subsink.add(
      this.subject.onAccount.subscribe((rsp) => { this.loggedInUser = rsp; })
    );
  }

  ngOnDestroy(): void {
    this.subsink.unsubscribe();
  }

  retrieveShipmentList(): void {
    this.shipments = [];
    const start     = formatDate(this.shipmentListForm.get('startDate')?.value, 'dd/MM/yyyy', 'en-GB');
    const end       = formatDate(this.shipmentListForm.get('endDate')?.value,   'dd/MM/yyyy', 'en-GB');
    const isCustomer = this.loggedInUser?.personalInfo.role === 'Customer';
    const accCode    = isCustomer ? this.loggedInUser!.loginCredentials.accountCode : undefined;

    this.subsink.add(
      this.http.getShipmentsList(start, end, accCode).subscribe({
        next:  (rsp: Shipment[]) => { this.shipments = rsp; },
        error: ()                 => { this.shipments = []; alert('No shipments found in this date range.'); }
      })
    );
  }

  onClear(): void {
    this.shipmentListForm.reset();
  }

  onCreateA2Label(): void {
    this.buildA2Body();
    pdfMake.createPdf({ info: this.docInfo, pageMargins: 10, content: this.a2Body, styles: this.pdfStyles as any }).download('A2-label');
  }

  onCreateA4Label(): void {
    this.buildA4Body();
    pdfMake.createPdf({ info: this.docInfo, pageMargins: 10, content: this.a4Body, styles: this.pdfStyles as any }).download('A4-label');
  }

  onCreateA6Label(): void {
    this.buildA6Body();
    pdfMake.createPdf({
      info:        this.docInfo,
      pageSize:    'A6',
      pageMargins: 5,
      content:     this.a6Body,
      styles:      { ...this.pdfStyles, defaultStyle: { fontSize: 8 } } as any
    }).download('A6-label');
  }

  onCreateInvoice(): void {
    this.buildInvoiceBody();
    pdfMake.createPdf({
      info:        { title: 'A4 Invoice', author: 'Mohd Naushad Ahmed', subject: 'A4 Invoice for Shipment', keywords: 'A4 Invoice' },
      pageMargins: 10,
      content:     this.invoiceBody,
      styles:      this.pdfStyles as any
    }).download('A4-invoice');
  }

  textToBase64Barcode(text: string, ht: number, fSize = 15): string {
    const canvas = document.createElement('canvas');
    JsBarcode(canvas, text || 'N/A', { format: 'CODE128', height: ht, fontOptions: 'bold', fontSize: fSize });
    return canvas.toDataURL('image/png');
  }

  private isDomestic(elm: Shipment): boolean {
    return this.loggedInUser?.personalInfo.eventLocation === elm.shipment.receiverInformation.country;
  }

  private valueText(elm: Shipment, domestic: boolean): string {
    const si = elm.shipment.shipmentInformation;
    return domestic
      ? `Goods Value: ${si.customsValue}`
      : `Customs Value: ${si.currency} ${si.customsValue}`;
  }

  private buildLabelTable(
    elm: Shipment,
    leftWidth: number,
    barcodeWidth: number,
    barcodeHeight: number,
    fontSize: number,
    destinationField: 'country' | 'city' = 'country'
  ): object {
    const awbno    = elm.shipment.awbno || 'N/A';
    const altRefNo = elm.shipment.altRefNo?.toString() || awbno;
    const domestic = this.isDomestic(elm);
    const si       = elm.shipment.shipmentInformation;
    const sender   = elm.shipment.senderInformation;
    const receiver = elm.shipment.receiverInformation;
    const date     = si.activity?.[0]?.date ?? '';
    const time     = si.activity?.[0]?.time ?? '';
    const dest     = receiver[destinationField];

    const c = (text: string, extra: object = {}): object => ({ text, fontSize, ...extra });
    const awbBarcode    = this.textToBase64Barcode(awbno, barcodeHeight, fontSize + 2);
    const altRefBarcode = this.textToBase64Barcode(altRefNo, Math.round(barcodeHeight * 0.9), fontSize);

    return {
      table: {
        headerRows: 0,
        widths: [leftWidth, '*'],
        body: [
          [
            c(`Date: ${date} ${time}`),
            c(`Destination: ${dest}\nProduct Type: ${si.service}`, { bold: true, fontSize: fontSize + 1 })
          ],
          [
            c(`Account Number: ${sender.accountNo}`),
            { image: awbBarcode, alignment: 'center', rowSpan: 2, width: barcodeWidth, margin: [0, 2, 0, 2] }
          ],
          [
            c(`No. of Items: ${si.numberOfItems}\nWeight: ${si.weight} ${si.weightUnits}\n${this.valueText(elm, domestic)}`),
            ''
          ],
          [
            c(`From:\n${sender.name}\nMobile: ${sender.phoneNumber}\nAlternate Mobile: ${sender.contact}\nCountry: ${sender.country}`,
              { maxLines: 6, ellipsis: true }),
            c(`To:\n${receiver.name}\nAddress: ${receiver.address}\nCity: ${receiver.city}\nMobile: ${receiver.phone}\nAlternate Mobile: ${receiver.contact}\nCountry: ${receiver.country}`,
              { maxLines: 9, ellipsis: true })
          ],
          [
            c(`Description: ${si.goodsDescription}`),
            { image: altRefBarcode, alignment: 'center', rowSpan: 2, width: barcodeWidth, margin: [0, 2, 0, 2] }
          ],
          [
            c(`COD: ${si.codAmount} ${si.currency}`, { bold: true, fontSize: fontSize + 1 }),
            ''
          ],
        ]
      }
    };
  }

  private buildA6Body(): void {
    this.a6Body.length = 0;
    this.rowsSelected.forEach((elm, idx, arr) => {
      const table = this.buildLabelTable(elm, 110, 148, 65, 8) as any;
      if (idx < arr.length - 1) { table.pageBreak = 'after'; }
      this.a6Body.push([table]);
    });
  }

  private buildA4Body(): void {
    this.a4Body.length = 0;
    this.rowsSelected.forEach((elm: Shipment, idx, arr) => {
      const awbno    = elm.shipment.awbno || 'N/A';
      const altRefNo = elm.shipment.altRefNo?.toString() || awbno;
      const domestic = this.isDomestic(elm);
      const si       = elm.shipment.shipmentInformation;
      const sender   = elm.shipment.senderInformation;
      const receiver = elm.shipment.receiverInformation;
      const entry: any = {
        table: {
          headerRows: 0,
          widths: [200, '*'],
          body: [
            [{text: `Date: ${si.activity[0].date} ${si.activity[0].time}`},
             {text: `Destination: ${receiver.country}\nProduct Type: ${si.service}`, bold: true}],
            [{text: `Account Number: ${sender.accountNo}`},
             {image: this.textToBase64Barcode(awbno, 70), alignment: 'center', rowSpan: 2, width: 170}],
            [{text: `No. of Items: ${si.numberOfItems}\nWeight: ${si.weight}${si.weightUnits}\n${this.valueText(elm, domestic)}`}, ''],
            [{text: `From:\n${sender.name}\nMobile: ${sender.phoneNumber}\nAlternate Mobile: ${sender.contact}\nCountry: ${sender.country}`},
             {text: `To:\n${receiver.name}\nAddress: ${receiver.address}\nCity: ${receiver.city}\nMobile: ${receiver.contact}\nAlternate Mobile: ${receiver.phone}\nCountry: ${receiver.country}`}],
            [{text: `Description: ${si.goodsDescription}`},
             {image: this.textToBase64Barcode(altRefNo, 70), alignment: 'center', rowSpan: 2, width: 170}],
            [{text: `COD: ${si.codAmount} ${si.currency}`, bold: true}, ''],
          ]
        }
      };
      if (idx < arr.length - 1) { entry.pageBreak = 'after'; }
      this.a4Body.push([entry]);
    });
  }

  private buildA2Body(): void {
    this.a2Body.length = 0;
    this.rowsSelected.forEach((elm: Shipment, idx, arr) => {
      const awbno    = elm.shipment.awbno || 'N/A';
      const altRefNo = elm.shipment.altRefNo?.toString() || awbno;
      const si       = elm.shipment.shipmentInformation;
      const sender   = elm.shipment.senderInformation;
      const receiver = elm.shipment.receiverInformation;
      const entry: any = {
        table: {
          headerRows: 0,
          widths: [200, '*'],
          body: [
            [{text: `Date: ${si.activity[0].date} ${si.activity[0].time}`},
             {text: `Destination: ${receiver.city}\nProduct Type: ${si.service}`, bold: true}],
            [{text: `Account Number: ${sender.accountNo}`},
             {image: this.textToBase64Barcode(awbno, 70), alignment: 'center', rowSpan: 2, width: 170}],
            [{text: `No. of Items: ${si.numberOfItems}\nWeight: ${si.weight}${si.weightUnits}\nGoods Value: ${si.customsValue}`}, ''],
            [{text: `From:\n${sender.name}\nMobile: ${sender.contact}\nAlternate Mobile: ${sender.phoneNumber}\nCountry: ${sender.country}`},
             {text: `To:\n${receiver.name}\nAddress: ${receiver.address}\nCity: ${receiver.city}\nMobile: ${receiver.phone}\nAlternate Mobile: ${receiver.contact}\nCountry: ${receiver.country}`}],
            [{text: `Description: ${si.goodsDescription}`},
             {image: this.textToBase64Barcode(altRefNo, 70), alignment: 'center', rowSpan: 2, width: 170}],
            [{text: `COD: ${si.codAmount} ${si.currency}`, bold: true}, ''],
          ]
        }
      };
      if (idx < arr.length - 1) { entry.pageBreak = 'after'; }
      this.a2Body.push([entry]);
    });
  }

  private buildInvoiceBody(): void {
    this.invoiceBody.length = 0;
    this.rowsSelected.forEach((elm: Shipment) => {
      const si       = elm.shipment.shipmentInformation;
      const sender   = elm.shipment.senderInformation;
      const receiver = elm.shipment.receiverInformation;
      const awbno    = elm.shipment.awbno;

      const blankRow  = () => [{text: '', rowSpan: 10, border: [true, false, false, false]},
                                {text: '', border: [true, false, false, false]},
                                {text: '', border: [true, false, false, false]},
                                {text: '', border: [true, false, false, false]},
                                {text: '', border: [true, false, false, false]},
                                {text: '', border: [true, false, false, false]},
                                {text: '', border: [true, false, true,  false]}];
      const lastBlank = () => [{text: '', rowSpan: 10, border: [true, false, true, true]},
                                ...blankRow().slice(1)];

      this.invoiceBody.push([{
        table: {
          headerRows: 1,
          widths: ['*', '*'],
          heights: 20,
          body: [
            [{text: 'Commercial Invoice', colSpan: 2, border: [false, false, false, true], alignment: 'center', bold: true, margin: 10}, ''],
            [{text: `International Air Way Bill NO: ${awbno}`, border: [true, false, true, true]},
             {image: this.textToBase64Barcode(awbno, 70), fit: [150, 150], border: [true, false, true, true]}],
            [{text: `DATE OF EXPORTATION: ${si.activity.at(0).date}`, border: [false, false, true, false]},
             {text: 'EXPORT REFERENCE (i.e. Order no, etc)', border: [false, false, false, false]}],
            [{text: `SHIPPER/EXPORTER (complete name and address)\n${sender.name}\n${sender.city}\n${sender.country}\n${sender.address}\n${sender.contact}\n${sender.email}`},
             {text: `CONSIGNEE (complete name and address)\n${receiver.name}\n${receiver.address}\n${receiver.city}\n${receiver.country}\n${receiver.contact}`, border: [true, true, true, true]}],
            [{text: `COUNTRY OF EXPORT:\n${sender.country}`},
             {text: 'IMPORTER - IF OTHER THAN CONSIGNEE (complete name and address)', rowSpan: 3}],
            [{text: 'COUNTRY OF MANUFACTURE:'}, ''],
            [{text: `COUNTRY OF ULTIMATE DESTINATION:\n${receiver.country}`}, ''],
            [{text: '', colSpan: 2, border: [false, false, false, false]}],
            [{
              colSpan: 2, headerRows: 1, heights: 80, border: [false, false, false, false],
              table: {
                body: [
                  [{text: 'NO. OF PKGS.'}, {text: 'TYPE OF PKGS.'}, {text: 'FULL DESCRIPTION'}, {text: 'QTY.'}, {text: 'HS Code'}, {text: 'UNIT VALUE'}, {text: 'TOTAL VALUE'}],
                  [{text: si.numberOfItems, rowSpan: 3}, {text: si.service, rowSpan: 3}, {text: si.goodsDescription, rowSpan: 3},
                   {text: si.numberOfItems, rowSpan: 3}, {text: si.hsCode, rowSpan: 3},
                   {text: si.customsValue,  rowSpan: 3}, {text: si.customsValue, rowSpan: 3}],
                  [{text: ''}, '', '', '', '', '', ''],
                  [{text: ''}, '', '', '', '', '', ''],
                  ...Array.from({length: 9}, blankRow),
                  lastBlank(),
                  [{text: 'TOTAL PKGS.'}, {text: '', colSpan: 4}, '', '', '', '', {text: 'TOTAL INVOICE VALUE'}],
                  [{text: si.numberOfItems}, {text: '', colSpan: 4}, '', '', '', '', {text: `${si.currency} ${si.customsValue}`}],
                ]
              }
            }],
            ...Array.from({length: 7}, () => [{text: '', colSpan: 2, border: [false, false, false, false]}, {text: ''}]),
            [{text: 'SIGNATURE OF SHIPPER/EXPORTER', height: 200, border: [false, true, false, false]},
             {text: 'DATE', border: [false, true, false, false], alignment: 'center'}],
          ]
        },
        pageBreak: 'after'
      }]);
    });
  }
}
