import { Component, OnInit } from '@angular/core';
import { FormBuilder, FormGroup } from '@angular/forms';
import { Account, Shipment } from 'src/common/app-globals';
import { HttpsvcService } from 'src/common/httpsvc.service';
import { PubsubsvcService } from 'src/common/pubsubsvc.service';

import * as JsBarcode from "jsbarcode";
import pdfMake from 'pdfmake/build/pdfmake';
import pdfFonts from 'pdfmake/build/vfs_fonts';


pdfMake.vfs = pdfFonts.pdfMake.vfs;

@Component({
  selector: 'app-create-drs',
  templateUrl: './create-drs.component.html',
  styleUrls: ['./create-drs.component.scss']
})
export class CreateDRSComponent implements OnInit {

  createDRSForm: FormGroup;
  shipments: Shipment[] = [];
  whichVendor: string = "";
  loggedInUser?: Account;

  constructor(private fb: FormBuilder, private http: HttpsvcService, private subject:PubsubsvcService) {
    this.createDRSForm = this.fb.group({
      shipmentNo: '',
      altRefNo: '',
      driverName:'',
      vendor:'self'
    })
   }

  ngOnInit(): void {
  }

  onVendorSelect(what: string) {
    this.whichVendor = what;
  }

  onSubmit() {
    this.shipments = [];
    let awbNo = this.createDRSForm.get('shipmentNo')?.value;
    let altRefNo = this.createDRSForm.get('altRefNo')?.value;
    let accCode = this.loggedInUser?.loginCredentials.accountCode;
    let driver = this.createDRSForm.get('driverName')?.value;
    
    let awbList = new Array<string>();

    let senderRefList = new Array<string>();

    if(awbNo.length > 0) {
      awbNo = awbNo.trim();
      awbList = awbNo.split("\n");
      
    } else if(altRefNo.length > 0) {
      altRefNo = altRefNo.trim();
      senderRefList = altRefNo.split("\n");
    }

    if(awbNo != undefined && awbNo.length && this.loggedInUser?.personalInfo.role != "Employee" && this.loggedInUser?.personalInfo.role != "Admin") {
      this.http.getShipmentsByAwbNo(awbList, accCode).subscribe(
        (rsp: Shipment[]) => {
          rsp.forEach((elm: Shipment) => {this.shipments.push(elm)});},
        (error) => {}, 
        () => {this.buildDRS();});

    } else if(awbNo != undefined && awbNo.length) {

      this.http.getShipmentsByAwbNo(awbList).subscribe((rsp:Shipment[]) => {
        rsp.forEach((elm: Shipment) => {this.shipments.push(elm)});
      },

      (error) => {}, 
      () => {this.buildDRS();});

    } else if(altRefNo != undefined && altRefNo.length && this.loggedInUser?.personalInfo.role != "Employee" && this.loggedInUser?.personalInfo.role != "Admin") {
      this.http.getShipmentsByAltRefNo(senderRefList, accCode).subscribe((rsp: Shipment[]) => {
        rsp.forEach((elm: Shipment) => {this.shipments.push(elm)});
      }, 
      (error) => {}, 
      () => {this.buildDRS();});

    } else {

      this.http.getShipmentsByAltRefNo(senderRefList).subscribe(
        (rsp: Shipment[]) => {rsp.forEach((elm: Shipment) => {this.shipments.push(elm)});}, 
        (error) => {}, 
        () => {this.buildDRS();});
    }
  }

  buildDRS() {
    // PDF export is now triggered manually via the Export PDF button
  }

  Info = {
    title: 'A4 Label',
    author: 'Mohd Naushad Ahmed',
    subject: 'A4 DRS for Shipment',
    keywords: 'A4 DRS',
  };

  textToBase64Barcode(text: string, ht: number, fSize: number = 15): string {
    if (!text.length) text = 'default';
    const canvas = document.createElement('canvas');
    JsBarcode(canvas, text, { format: 'CODE128', height: ht, width: 1, fontOptions: 'bold', fontSize: fSize });
    return canvas.toDataURL('image/png');
  }

  onCreateDRS() {
    const body: any[] = [
      [
        { text: 'S.No.',    bold: true },
        { text: 'Sender',   bold: true },
        { text: 'Receiver', bold: true },
        { text: 'Phone No.', bold: true },
        { text: 'COD',      bold: true },
        { text: 'AWB No.',  bold: true },
        { text: 'Received By', bold: true }
      ]
    ];

    this.shipments.forEach((elm: Shipment, idx: number) => {
      body.push([
        { text: idx + 1 },
        { text: elm.shipment.senderInformation.name },
        { text: elm.shipment.receiverInformation.address },
        { text: elm.shipment.receiverInformation.contact },
        { text: elm.shipment.shipmentInformation.codAmount },
        { image: this.textToBase64Barcode(elm.shipment.awbno, 50), width: 140, alignment: 'center' },
        {}
      ]);
    });

    const docDef: any = {
      info: this.Info,
      pageSize: 'A4',
      pageOrientation: 'landscape',
      pageMargins: [10, 10, 10, 10],
      content: [
        {
          table: {
            headerRows: 1,
            widths: ['auto', '*', '*', 'auto', 70, 160, '*'],
            body
          }
        }
      ],
      styles: {
        tableHeader: { bold: true, fontSize: 10, color: 'black' }
      },
      defaultStyle: { fontSize: 9 }
    };

    pdfMake.createPdf(docDef).download('DRS-A4.pdf');
  }


}
