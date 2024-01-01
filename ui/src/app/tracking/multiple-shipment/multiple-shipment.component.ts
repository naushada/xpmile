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

  onCreateA2Label() {
    this.rowsSelected?.forEach(elm => {alert(JSON.stringify(elm))});
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
            [{text: 'Comercial Invoice', colSpan:2,  border:[false,false,false,true], alignment:'center', bold:true}, ''],
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
