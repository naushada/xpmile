import { Component, OnDestroy, OnInit } from '@angular/core';
import { FormBuilder, FormGroup } from '@angular/forms';
import { Account, Shipment } from 'src/common/app-globals';
import { HttpsvcService } from 'src/common/httpsvc.service';
import { PubsubsvcService } from 'src/common/pubsubsvc.service';
import { SubSink } from 'subsink';

import * as JsBarcode from "jsbarcode";
import pdfMake from 'pdfmake/build/pdfmake';
import pdfFonts from 'pdfmake/build/vfs_fonts';
import { ExcelsvcService } from 'src/common/excelsvc.service';

pdfMake.vfs = pdfFonts.pdfMake.vfs;

@Component({
  selector: 'app-multiple-shipment',
  templateUrl: './multiple-shipment.component.html',
  styleUrls: ['./multiple-shipment.component.scss']
})
export class MultipleShipmentComponent implements OnInit, OnDestroy {

  multipleShipmentTrackingForm: FormGroup;
  whichVendor: string = "";
  shipments: Shipment[] = [];
  loggedInUser?: Account;
  subsink:SubSink = new SubSink();
  rowsSelected?:Array<Shipment> = [];
  isSingleShipmentView: boolean = false;
  activityOffset:number = 0;
  isButtonDisabled:boolean = true;

  constructor(private http: HttpsvcService, private fb: FormBuilder, private subject: PubsubsvcService, private excel: ExcelsvcService) {
    this.subsink.add(this.subject.onAccount.subscribe(rsp => { this.loggedInUser = rsp;}, (error) => {}, () => {}));

    this.multipleShipmentTrackingForm = this.fb.group({
      shipmentNo:'',
      altRefNo: '',
      vendor: 'self'
    })
  }

  ngOnInit(): void {
  }

  ngOnDestroy(): void {
      this.subsink.unsubscribe();
      let sh:any;
      this.subject.emit_shipment(sh);
  }

  onSelectionChanged(event:Shipment[]) {
    
    //event.forEach(elm => {alert(JSON.stringify(elm))});
    //this.rowsSelected?.forEach(elm => {alert(JSON.stringify(elm))});

  }

  onVendorSelect(what: string) {
    this.whichVendor = what;
  }

  viewSingleShipment(sh: Shipment) {
    this.subject.emit_shipment(sh);
    this.isSingleShipmentView = true;
  }

  onSubmit() {
    this.shipments = [];
    let awbNo = this.multipleShipmentTrackingForm.get('shipmentNo')?.value;
    let altRefNo = this.multipleShipmentTrackingForm.get('altRefNo')?.value;
    let accCode = this.loggedInUser?.loginCredentials.accountCode;
    
    let awbList = new Array<string>();

    let senderRefList = new Array<string>();

    if(awbNo.length > 0) {
      awbNo = awbNo.trim();
      awbList = awbNo.split("\n");
      
    } else if(altRefNo.length > 0) {
      altRefNo = altRefNo.trim();
      senderRefList = altRefNo.split("\n");
    }

    if((awbNo != undefined && awbNo.length) && ((this.loggedInUser?.personalInfo.role != "Employee") && (this.loggedInUser?.personalInfo.role != "Admin"))) {
      this.http.getShipmentsByAwbNo(awbList, accCode).subscribe(
        (rsp: Shipment[]) => {
          rsp.forEach((elm: Shipment) => {this.shipments.push(elm)});},
        (error) => {}, 
        () => {this.activityOffset = this.shipments.length; this.isButtonDisabled = false;});

    } else if(awbNo != undefined && awbNo.length) {

      this.http.getShipmentsByAwbNo(awbList).subscribe((rsp:Shipment[]) => {
        rsp.forEach((elm: Shipment) => {this.shipments.push(elm)});
      },

      (error) => {}, 
      () => {this.activityOffset = this.shipments.length; this.isButtonDisabled = false;});

    } else if(altRefNo != undefined && altRefNo.length && this.loggedInUser?.personalInfo.role != "Employee" && this.loggedInUser?.personalInfo.role != "Admin") {
      this.http.getShipmentsByAltRefNo(senderRefList, accCode).subscribe((rsp: Shipment[]) => {
        rsp.forEach((elm: Shipment) => {this.shipments.push(elm)});
      }, 
      (error) => {}, 
      () => {this.activityOffset = this.shipments.length; this.isButtonDisabled = false;});

    } else {

      this.http.getShipmentsByAltRefNo(senderRefList).subscribe(
        (rsp: Shipment[]) => {rsp.forEach((elm: Shipment) => {this.shipments.push(elm)});}, 
        (error) => {}, 
        () => {this.activityOffset = this.shipments.length; this.isButtonDisabled = false;});
    }
  }
    
  /** Label A6 Generation  */
  Info = {
    title: 'A6 Label',
    author: 'Mohd Naushad Ahmed',
    subject: 'A6 Label for Shipment',
    keywords: 'A6 Label',
  };

  A6LabelContentsBody:Array<object> = new Array<object>();

  buildA6ContentsBody() {
    this.A6LabelContentsBody.length = 0;
    this.rowsSelected?.forEach((elm:Shipment) => {
      let altRefNo:string = "default";
      if((elm.shipment.altRefNo != undefined))  {
        altRefNo = elm.shipment.altRefNo.toString();
      }

      if(this.loggedInUser?.personalInfo.eventLocation == elm.shipment.receiverInformation.country) {
        let ent = [
          {
            table: {
              headerRows: 0,
              widths: [ 100, '*'],
              heights: ['auto', 'auto', 'auto', 20, 'auto'],
              body: [
                [ {text: 'Date: ' + elm.shipment.shipmentInformation.activity[0].date + ' '+ elm.shipment.shipmentInformation.activity[0].time, fontSize:10}, {text: 'Destination: ' + elm.shipment.receiverInformation.country +'\n' + 'Product Type: ' + elm.shipment.shipmentInformation.service, bold: true}],
                [ {text: 'Account Number: '+ elm.shipment.senderInformation.accountNo, fontSize:10}, {image: this.textToBase64Barcode(elm.shipment.awbno, 70), bold: false, alignment: 'center',rowSpan:2, width: 170}],
                [ { text: 'No. of Items: ' + elm.shipment.shipmentInformation.numberOfItems + '\n' + 'Weight: '+ elm.shipment.shipmentInformation.weight + elm.shipment.shipmentInformation.weightUnits + '\n' + 'Goods Value: '+ elm.shipment.shipmentInformation.customsValue, bold: false, fontSize: 10 }, ''],
                [ { text: 'From:\n' + elm.shipment.senderInformation.name +'\n'+ 'Mobile: '+ elm.shipment.senderInformation.contact + '\n' + 'Altername Mobile: '+ elm.shipment.senderInformation.phoneNumber + '\n' + 'Country: '+ elm.shipment.senderInformation.country, bold: false, fontSize:10 }, {text: 'To:\n'+ elm.shipment.receiverInformation.name + '\n'+ 'Address: '+elm.shipment.receiverInformation.address +'\n'+'City: '+ elm.shipment.receiverInformation.city+ '\n'+'Mobile: '+elm.shipment.receiverInformation.contact +'\n' + 'Alternate Mobile: '+elm.shipment.receiverInformation.phone +'\n'+'Country:'+elm.shipment.receiverInformation.country, fontSize: 10}],
                [ {text: 'Description: ' + elm.shipment.shipmentInformation.goodsDescription, fontSize:10}, {image: this.textToBase64Barcode(altRefNo, 70), bold:false, alignment:'center',rowSpan:2, width:170} ],
                [ {text: 'COD: '+ elm.shipment.shipmentInformation.currency + ' ' + elm.shipment.shipmentInformation.codAmount, bold: true}, ''],
              ]
            },
            pageBreak: 'after'
          }
        ];
        this.A6LabelContentsBody.push(ent);
      } else {
        let ent = [
          {
            table: {
              headerRows: 0,
              widths: [ 100, '*'],
              heights: ['auto', 'auto', 'auto', 20, 'auto'],
              body: [
                [ {text: 'Date: ' + elm.shipment.shipmentInformation.activity[0].date + ' '+ elm.shipment.shipmentInformation.activity[0].time, fontSize:10}, {text: 'Destination: ' + elm.shipment.receiverInformation.country +'\n' + 'Product Type: ' + elm.shipment.shipmentInformation.service, bold: true}],
                [ {text: 'Account Number: '+ elm.shipment.senderInformation.accountNo, fontSize:10}, {image: this.textToBase64Barcode(elm.shipment.awbno, 70), bold: false, alignment: 'center',rowSpan:2, width: 170}],
                [ { text: 'No. of Items: ' + elm.shipment.shipmentInformation.numberOfItems + '\n' + 'Weight: '+ elm.shipment.shipmentInformation.weight + elm.shipment.shipmentInformation.weightUnits + '\n' + 'Customs Value: '+ elm.shipment.shipmentInformation.currency + ' ' + elm.shipment.shipmentInformation.customsValue, bold: false, fontSize: 10 }, ''],
                [ { text: 'From:\n' + elm.shipment.senderInformation.name +'\n'+ 'Mobile: '+ elm.shipment.senderInformation.contact + '\n' + 'Altername Mobile: '+ elm.shipment.senderInformation.phoneNumber + '\n' + 'Country: '+ elm.shipment.senderInformation.country, bold: false, fontSize:10 }, {text: 'To:\n'+ elm.shipment.receiverInformation.name + '\n'+ 'Address: '+elm.shipment.receiverInformation.address +'\n'+'City: '+ elm.shipment.receiverInformation.city+ '\n'+'Mobile: '+elm.shipment.receiverInformation.contact +'\n' + 'Alternate Mobile: '+elm.shipment.receiverInformation.phone +'\n'+'Country:'+elm.shipment.receiverInformation.country, fontSize: 10}],
                [ {text: 'Description: ' + elm.shipment.shipmentInformation.goodsDescription, fontSize:10}, {image: this.textToBase64Barcode(altRefNo, 70), bold:false, alignment:'center',rowSpan:2, width:170} ],
                [ {text: 'COD: '+ elm.shipment.shipmentInformation.currency + ' ' + elm.shipment.shipmentInformation.codAmount, bold: true}, ''],
              ]
            },
            pageBreak: 'after'
          }
        ];
        this.A6LabelContentsBody.push(ent);
      }
    });
  }

  A4LabelContentsBody:Array<object> = new Array<object>();

  buildA4ContentsBody() {
    this.A4LabelContentsBody.length = 0;
    this.rowsSelected?.forEach((elm: Shipment) => {

      let altRefNo:string = "default";
      if((elm.shipment.altRefNo != undefined))  {
        altRefNo = elm.shipment.altRefNo.toString();
      }
      if(this.loggedInUser?.personalInfo.eventLocation == elm.shipment.receiverInformation.country) {

        let ent = [
          {
            table: {
              headerRows: 0,
              widths: [ 200, '*'],
              body: [
                
                [ {text: 'Date:' + elm.shipment.shipmentInformation.activity[0].date + ' '+ elm.shipment.shipmentInformation.activity[0].time}, {text: 'Destination:' + elm.shipment.receiverInformation.country +'\n' + 'Product Type:' + elm.shipment.shipmentInformation.service, bold: true}],
                [ {text: 'Account Number:'+ elm.shipment.senderInformation.accountNo}, {image: this.textToBase64Barcode(elm.shipment.awbno, 70), bold: false, alignment: 'center',rowSpan:2, width: 170}],
                [ { text: 'No. of Items: ' + elm.shipment.shipmentInformation.numberOfItems + '\n' + 'Weight: '+ elm.shipment.shipmentInformation.weight + elm.shipment.shipmentInformation.weightUnits + '\n' + 'Goods Value: '+ elm.shipment.shipmentInformation.customsValue, bold: false }, ''],
                [ { text: 'From:\n' + elm.shipment.senderInformation.name +'\n'+ 'Mobile: '+ elm.shipment.senderInformation.contact + '\n' + 'Alternate Mobile: '+ elm.shipment.senderInformation.phoneNumber + '\n' + 'Country: '+ elm.shipment.senderInformation.country, bold: false }, {text: 'To:\n'+ elm.shipment.receiverInformation.name + '\n'+ 'Address: '+elm.shipment.receiverInformation.address+'\n'+'City: '+ elm.shipment.receiverInformation.city+ '\n'+'Mobile: '+elm.shipment.receiverInformation.contact +'\n' + 'Altername Mobile: '+elm.shipment.receiverInformation.phone +'\n'+'Country: '+elm.shipment.receiverInformation.country}],
                [ {text: 'Description:' + elm.shipment.shipmentInformation.goodsDescription}, {image: this.textToBase64Barcode(altRefNo , 70), bold:false, alignment:'center',rowSpan:2, width:170} ],
                [ {text: 'COD: '+ elm.shipment.shipmentInformation.currency +' '+elm.shipment.shipmentInformation.codAmount, bold: true}, ''],
              ]
            },
            pageBreak: 'after'
          }
        ];

        this.A4LabelContentsBody.push(ent);
      } else {
        let ent = [
          {
            table: {
              headerRows: 0,
              widths: [ 200, '*'],
              body: [
                
                [ {text: 'Date:' + elm.shipment.shipmentInformation.activity[0].date + ' '+ elm.shipment.shipmentInformation.activity[0].time}, {text: 'Destination:' + elm.shipment.receiverInformation.country +'\n' + 'Product Type:' + elm.shipment.shipmentInformation.service, bold: true}],
                [ {text: 'Account Number:'+ elm.shipment.senderInformation.accountNo}, {image: this.textToBase64Barcode(elm.shipment.awbno, 70), bold: false, alignment: 'center',rowSpan:2, width: 170}],
                [ { text: 'No. of Items: ' + elm.shipment.shipmentInformation.numberOfItems + '\n' + 'Weight: '+ elm.shipment.shipmentInformation.weight + elm.shipment.shipmentInformation.weightUnits + '\n' + 'Customs Value: '+ elm.shipment.shipmentInformation.currency + ' ' + elm.shipment.shipmentInformation.customsValue, bold: false }, ''],
                [ { text: 'From:\n' + elm.shipment.senderInformation.name +'\n'+ 'Mobile: '+ elm.shipment.senderInformation.contact + '\n' + 'Alternate Mobile: '+ elm.shipment.senderInformation.phoneNumber + '\n' + 'Country: '+ elm.shipment.senderInformation.country, bold: false }, {text: 'To:\n'+ elm.shipment.receiverInformation.name + '\n'+ 'Address: '+elm.shipment.receiverInformation.address+'\n'+'City: '+ elm.shipment.receiverInformation.city+ '\n'+'Mobile: '+elm.shipment.receiverInformation.contact +'\n' + 'Altername Mobile: '+elm.shipment.receiverInformation.phone +'\n'+'Country: '+elm.shipment.receiverInformation.country}],
                [ {text: 'Description:' + elm.shipment.shipmentInformation.goodsDescription}, {image: this.textToBase64Barcode(altRefNo , 70), bold:false, alignment:'center',rowSpan:2, width:170} ],
                [ {text: 'COD: '+ elm.shipment.shipmentInformation.currency +' '+elm.shipment.shipmentInformation.codAmount, bold: true}, ''],
              ]
            },
            pageBreak: 'after'
          }
        ];

        this.A4LabelContentsBody.push(ent);
      }

    });
  }

  docDefinitionA6 = {
    info: this.Info,
    pageSize: "A6",
    pageMargins: 5,
    content: this.A6LabelContentsBody,
    styles: {
      header: {
        fontSize: 18,
        bold: true,
        margin: [0, 0, 0, 10]
      },
      subheader: {
        fontSize: 16,
        bold: true,
        margin: [0, 10, 0, 5]
      },
      tableExample: {
        margin: [0, 5, 0, 15]
      },
      tableHeader: {
        bold: true,
        fontSize: 13,
        color: 'black'
      },
      defaultStyle: {
        fontSize: 8,
      }
    }
  };

  docDefinitionA4 = {
    info: this.Info,
    pageMargins: 10,
    content: this.A4LabelContentsBody,
    styles: {
      header: {
        fontSize: 18,
        bold: true,
        margin: [0, 0, 0, 10]
      },
      subheader: {
        fontSize: 16,
        bold: true,
        margin: [0, 10, 0, 5]
      },
      tableExample: {
        margin: [0, 5, 0, 15]
      },
      tableHeader: {
        bold: true,
        fontSize: 13,
        color: 'black'
      },
      rH: {
        height: 100,
        fontSize: 10
      }
    }
  };

  textToBase64Barcode(text: string, ht:number, fSize: number = 15) {
    if(!text.length) {
      text = "default";
    }

    var canvas = document.createElement("canvas");
    JsBarcode(canvas, text, {format: "CODE128", height: ht, fontOptions: 'bold', fontSize: fSize});
    return canvas.toDataURL("image/png");
  }

  // ── A2 Label (4" × 6" shipping label) ─────────────────────────────────────

  A2LabelContentsBody: Array<object> = new Array<object>();

  docDefinitionA2: any = {
    info: { title: 'Shipping Label', author: 'Xpmile Logistics', subject: 'AWB Label', keywords: 'shipping label' },
    pageSize: { width: 288, height: 432 },   // 4" × 6" in points (1 pt = 1/72")
    pageMargins: [8, 8, 8, 8],
    content: this.A2LabelContentsBody,
    defaultStyle: { fontSize: 8 },
    styles: {
      brand:        { fontSize: 11, bold: true, color: '#ffffff' },
      awbTitle:     { fontSize: 7,  bold: true, color: '#b8d4e8', alignment: 'right' as const },
      sectionLabel: { fontSize: 6.5, bold: true, color: '#1b5a7d', margin: [0, 0, 0, 1] },
      fromName:     { fontSize: 8.5, bold: true },
      toName:       { fontSize: 12,  bold: true },
      fieldLabel:   { fontSize: 6.5, bold: true, color: '#555' },
      fieldValue:   { fontSize: 8.5 },
    }
  };

  buildA2ContentsBody() {
    this.A2LabelContentsBody.length = 0;

    this.rowsSelected?.forEach((elm: Shipment) => {
      const altRefNo  = elm.shipment.altRefNo?.toString() || 'default';
      const activity0 = elm.shipment.shipmentInformation.activity[0];
      const si        = elm.shipment.shipmentInformation;
      const sender    = elm.shipment.senderInformation;
      const receiver  = elm.shipment.receiverInformation;

      const altRefRow = elm.shipment.altRefNo ? [{
        stack: [
          { text: 'REF: ' + altRefNo, fontSize: 7, alignment: 'center' as const },
          { image: this.textToBase64Barcode(altRefNo, 38), width: 240, alignment: 'center' as const }
        ],
        margin: [0, 2, 0, 0]
      }] : [];

      const ent = [{
        table: {
          widths: ['*'],
          body: [
            // ── Header ──────────────────────────────────────────────────────
            [{
              columns: [
                { text: 'XPMILE LOGISTICS', style: 'brand',    width: '*'    },
                { text: 'AIR WAYBILL',      style: 'awbTitle', width: 'auto' }
              ],
              fillColor: '#1b5a7d',
              margin: [6, 5, 6, 5]
            }],
            // ── AWB number ──────────────────────────────────────────────────
            [{
              text: elm.shipment.awbno,
              fontSize: 11, bold: true, alignment: 'center' as const,
              margin: [0, 4, 0, 2]
            }],
            // ── Barcode ─────────────────────────────────────────────────────
            [{
              image: this.textToBase64Barcode(elm.shipment.awbno, 55),
              width: 255, alignment: 'center' as const, margin: [0, 0, 0, 4]
            }],
            // ── Service / Date / Account ─────────────────────────────────────
            [{
              columns: [
                { stack: [{ text: 'SERVICE', style: 'fieldLabel' }, { text: si.service, style: 'fieldValue' }], width: '*' },
                { stack: [{ text: 'DATE', style: 'fieldLabel' }, { text: activity0?.date || '', style: 'fieldValue' }], width: '*', alignment: 'center' as const },
                { stack: [{ text: 'ACCT', style: 'fieldLabel' }, { text: sender.accountNo || '', style: 'fieldValue' }], width: '*', alignment: 'right' as const }
              ],
              fillColor: '#eef2f6', margin: [6, 4, 6, 4]
            }],
            // ── FROM ────────────────────────────────────────────────────────
            [{
              stack: [
                { text: 'FROM', style: 'sectionLabel' },
                { text: sender.name, style: 'fromName' },
                { text: (sender.address ? sender.address + '\n' : '') +
                        (sender.city    ? sender.city + ', '   : '') + sender.country,
                  fontSize: 8 },
                { text: 'Tel: ' + sender.contact, fontSize: 7, color: '#666' }
              ],
              fillColor: '#f6f8fb', margin: [6, 4, 6, 4]
            }],
            // ── TO ──────────────────────────────────────────────────────────
            [{
              stack: [
                { text: 'SHIP TO', style: 'sectionLabel' },
                { text: receiver.name, style: 'toName' },
                { text: receiver.address || '', fontSize: 9 },
                { text: (receiver.city    ? receiver.city    + ', ' : '') +
                        (receiver.state   ? receiver.state   + ', ' : '') +
                        receiver.country,
                  fontSize: 10, bold: true },
                { text: 'Tel: ' + receiver.contact +
                        (receiver.phone ? '   Alt: ' + receiver.phone : ''),
                  fontSize: 8 }
              ],
              margin: [6, 5, 6, 5]
            }],
            // ── Weight / Items / COD ─────────────────────────────────────────
            [{
              columns: [
                { stack: [{ text: 'WEIGHT', style: 'fieldLabel' }, { text: si.weight + ' ' + si.weightUnits, style: 'fieldValue' }], width: '*' },
                { stack: [{ text: 'ITEMS',  style: 'fieldLabel' }, { text: String(si.numberOfItems), style: 'fieldValue' }], width: '*', alignment: 'center' as const },
                { stack: [{ text: 'COD',    style: 'fieldLabel' }, { text: si.currency + ' ' + si.codAmount, style: 'fieldValue', bold: true }], width: '*', alignment: 'right' as const }
              ],
              fillColor: '#eef2f6', margin: [6, 4, 6, 4]
            }],
            // ── Description ──────────────────────────────────────────────────
            [{
              text: [{ text: 'DESC: ', style: 'fieldLabel' }, { text: si.goodsDescription || '', fontSize: 8 }],
              margin: [6, 3, 6, 4]
            }],
            // ── Alt-ref barcode (conditional) ────────────────────────────────
            ...altRefRow.map(r => [r])
          ]
        },
        layout: {
          hLineWidth: (i: number, node: any) => (i === 0 || i === node.table.body.length) ? 1 : 0.5,
          vLineWidth: () => 1,
          hLineColor: () => '#c8d6e0',
          vLineColor: () => '#c8d6e0'
        },
        pageBreak: 'after'
      }];

      this.A2LabelContentsBody.push(ent);
    });
  }

  onCreateA2Label() {
    this.buildA2ContentsBody();
    pdfMake.createPdf(this.docDefinitionA2).download('A2-label');
  }

  onCreateA4Label() {
    this.buildA4ContentsBody();
    pdfMake.createPdf(this.docDefinitionA4).download( "A4" + "-label");
  }

  onCreateA6Label() {
    this.buildA6ContentsBody();
    pdfMake.createPdf(this.docDefinitionA6).download( "A6" + "-label");
  }

  /** A4 Invoice Generation Generation  */
  InfoInvoice = {
    title: 'A4 Invoice',
    author: 'Mohd Naushad Ahmed',
    subject: 'A4 Invoice for Shipment',
    keywords: 'A4 Invoice',
  };
  A4InvoiceContentsBody:Array<object> = new Array<object>();


  docDefinitionA4Invoice = {
    info: this.InfoInvoice,
    pageMargins: 10,
    content: this.A4InvoiceContentsBody,
    styles: {
      header: {
        fontSize: 18,
        bold: true,
        margin: [0, 0, 0, 10]
      },
      subheader: {
        fontSize: 16,
        bold: true,
        margin: [0, 10, 0, 5]
      },
      tableExample: {
        margin: [0, 5, 0, 15]
      },
      tableHeader: {
        bold: true,
        fontSize: 13,
        color: 'black'
      },
      rH: {
        height: 100,
        fontSize: 10
      }
    }
  };

  onGennerateInvoice() {
  //alert("onGenerateInvoice");
  this.A4InvoiceContentsBody.length = 0;
  this.rowsSelected?.forEach((elm: Shipment) => {
    //console.log("awbNo: " + elm.shipment.awbno + " altRefNo: " + elm.shipment.altRefNo);
    if(elm.shipment.altRefNo != undefined) {
      let altRefNo:string = elm.shipment.altRefNo.toString();
    }

    let ent = [
      { 
        table: {
          headerRows: 1,
          widths: [ '*', '*'],
          heights:20,
          body: [
            [{text: 'Comercial Invoice', colSpan:2,  border:[false,false,false,true], alignment:'center', bold:true,  margin:10}, ''],
            [{text: 'International Air Way Bill NO: ' + elm.shipment.awbno , border:[true, false, true,true]}, {image: this.textToBase64Barcode(elm.shipment.awbno, 70), fit: [150, 150],  border:[true,false,true,true]}],
            [{text: 'DATE OF EXPORTATION: ' + elm.shipment.shipmentInformation.activity.at(0).date, border:[false, false, true, false]}, {text: 'EXPORT REFERENCE(i.e. Order no,etc)', border:[false, false, false, false]}],
            [{text: 'SHIPPER/EXPORTER (complete name and address)\n' + 
              elm.shipment.senderInformation.name +"\n" + elm.shipment.senderInformation.city + "\n" +
              elm.shipment.senderInformation.country + "\n" +
              elm.shipment.senderInformation.address +"\n" + elm.shipment.senderInformation.contact + "\n" +
              elm.shipment.senderInformation.email
              }, 
             {text: 'CONSIGNEE (complete name and address)' + "\n" +
              elm.shipment.receiverInformation.name + "\n" +
              elm.shipment.receiverInformation.address + "\n" +
              elm.shipment.receiverInformation.city + "\n" + 
              elm.shipment.receiverInformation.country + "\n" +
              elm.shipment.receiverInformation.contact,
              border: [true,true,true,true]
             },],
            [{text: 'COUNTRY OF EXPORT:' + "\n" + elm.shipment.senderInformation.country}, {text: 'IMPORTER - IF OTHER THAN CONSIGNEE' + '(Complete name and address )', rowSpan:3}],
            [{text: 'COUNTRY OF MANUFACTURE:'}, ''],
            [{text: 'COUNTRY OF ULTIMATE DESTINATION:' + "\n" + elm.shipment.receiverInformation.country}, ''],
            [{text: '', colSpan:2, border:[false, false, false, false]}],
            [
              { colSpan:2,
                headerRows:1,
                heights:80,

                border: [false, false, false, false],
                table: {
                 body: [
                      [{text: 'NO. OF PKGS.'}, {text:'TYPE OF PKGS.'}, {text: 'FULL DESCRIPTION'}, {text:'QTY.'}, {text:'HS Code'}, {text:'UNIT VALUE'}, {text:'TOTAL VALUE'}],
                      [{text: elm.shipment.shipmentInformation.numberOfItems, rowSpan:3}, {text: elm.shipment.shipmentInformation.service, rowSpan:3}, {text: elm.shipment.shipmentInformation.goodsDescription, rowSpan:3},
                       {text: elm.shipment.shipmentInformation.numberOfItems, rowSpan:3}, {text: elm.shipment.shipmentInformation.hsCode, rowSpan:3} ,
                       {text: elm.shipment.shipmentInformation.customsValue, rowSpan:3}, {text: elm.shipment.shipmentInformation.customsValue, rowSpan:3}
                      ],
                      [{text:''}, '','','','','',''],
                      [{text:''}, '','','','','',''],

                      [{text:'', rowSpan:10, border:[true, false, false, false]}, {text:'', border:[true, false, false, false]}, {text:'', border:[true, false, false, false]} ,{text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, true, false]} ],
                      [{text:'', rowSpan:10, border:[true, false, false, false]}, {text:'', border:[true, false, false, false]}, {text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, true, false]}],
                      [{text:'', rowSpan:10, border:[true, false, false, false]}, {text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, true, false]}],
                      [{text:'', rowSpan:10, border:[true, false, false, false]}, {text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, true, false]}],
                      [{text:'', rowSpan:10, border:[true, false, false, false]}, {text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, true, false]}],
                      [{text:'', rowSpan:10, border:[true, false, false, false]}, {text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, true, false]}],
                      [{text:'', rowSpan:10, border:[true, false, false, false]}, {text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, true, false]}],
                      [{text:'', rowSpan:10, border:[true, false, false, false]}, {text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, true, false]}],
                      [{text:'', rowSpan:10, border:[true, false, false, false]}, {text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, true, false]}],
                      [{text:'', rowSpan:10, border:[true, false, true, true]}, {text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, false, false]},{text:'', border:[true, false, true, false]}],

                      [{text: 'TOTAL PKGS.'}, {text:'',  colSpan:4}, '', '','', '', {text: 'TOTAL INVOICE VALUE'}],
                      
                      [{text: elm.shipment.shipmentInformation.numberOfItems}, {text:'',  colSpan:4},'', '', '',  '', 
                       {text: elm.shipment.shipmentInformation.currency  + ' ' + elm.shipment.shipmentInformation.customsValue}],
                      
                 ]
                }
              }
            ],

            [{text: '', colSpan:2, border:[false, false, false, false]}, {text:''}],

            [{text: '', colSpan:2, border:[false, false, false, false]}, {text:''}],
            [{text: '', colSpan:2, border:[false, false, false, false]}, {text:''}],
            [{text: '', colSpan:2, border:[false, false, false, false]}, {text:''}],
            [{text: '', colSpan:2, border:[false, false, false, false]}, {text:''}],
            [{text: '', colSpan:2, border:[false, false, false, false]}, {text:''}],
            [{text: '', colSpan:2, border:[false, false, false, false]}, {text:''}],
            [{text: 'SIGNATURE OF SHIPPER/EXPORTER', height:200, border:[false, true, false, false]}, {text: 'DATE' , border:[false, true, false, false], alignment:'center'}],
          ]
        },
        pageBreak: 'after'
      }
    ];

    this.A4InvoiceContentsBody.push(ent);
  });

}

  onCreateInvoice() {
    this.onGennerateInvoice();
    pdfMake.createPdf(this.docDefinitionA4Invoice).download( "A4" + "-invoice");
  }

  onExcelExport() {
    this.excel.exportToExcel(this.shipments);
    this.isButtonDisabled = true;
  }

}
