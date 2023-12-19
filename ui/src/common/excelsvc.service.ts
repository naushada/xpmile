import { Injectable } from '@angular/core';

//import { Workbook } from 'exceljs';
import * as fs from 'file-saver';
import * as Excel from 'exceljs';
import * as XLSX from 'xlsx';

import { Account, AppGlobals, AppGlobalsDefault, SenderInformation, Shipment, ShipmentExcelRow } from './app-globals';
import { HttpsvcService } from './httpsvc.service';

@Injectable({
  providedIn: 'root'
})
export class ExcelsvcService {

  public defValue?: AppGlobals;

  constructor(private http: HttpsvcService) { 
    this.defValue = {...AppGlobalsDefault};

  }

  createAndSaveShipmentTemplate(sheetName: string) {
    const workbook = new Excel.Workbook();
    const worksheet = workbook.addWorksheet(sheetName);

    worksheet.properties.defaultRowHeight = 20;
    worksheet.properties.defaultColWidth = 20;
    worksheet.pageSetup.paperSize = 9;
    worksheet.pageSetup.orientation = 'landscape';

    worksheet.columns = [
      {header: this.defValue?.ExcelHeading?.at(0), key: this.defValue?.ExcelHeading?.at(0), width: 15},
      {header: this.defValue?.ExcelHeading?.at(1), key: this.defValue?.ExcelHeading?.at(1), width: 15},
      {header: this.defValue?.ExcelHeading?.at(2), key: this.defValue?.ExcelHeading?.at(2), width: 15},
      {header: this.defValue?.ExcelHeading?.at(3), key: this.defValue?.ExcelHeading?.at(3), width: 15},
      {header: this.defValue?.ExcelHeading?.at(4), key: this.defValue?.ExcelHeading?.at(4), width: 15},
      {header: this.defValue?.ExcelHeading?.at(5), key: this.defValue?.ExcelHeading?.at(5), width: 15},
      {header: this.defValue?.ExcelHeading?.at(6), key: this.defValue?.ExcelHeading?.at(6), width: 30},
      {header: this.defValue?.ExcelHeading?.at(7), key: this.defValue?.ExcelHeading?.at(7), width: 20},
      {header: this.defValue?.ExcelHeading?.at(8), key: this.defValue?.ExcelHeading?.at(8), width: 30},
      {header: this.defValue?.ExcelHeading?.at(9), key: this.defValue?.ExcelHeading?.at(9), width: 30},
      {header: this.defValue?.ExcelHeading?.at(10), key: this.defValue?.ExcelHeading?.at(10), width: 15},
      {header: this.defValue?.ExcelHeading?.at(11), key: this.defValue?.ExcelHeading?.at(11), width: 20},
      {header: this.defValue?.ExcelHeading?.at(12), key: this.defValue?.ExcelHeading?.at(12), width: 15},
      {header: this.defValue?.ExcelHeading?.at(13), key: this.defValue?.ExcelHeading?.at(13), width: 25},
      {header: this.defValue?.ExcelHeading?.at(14), key: this.defValue?.ExcelHeading?.at(14), width: 15},
    ];
    
    worksheet.views = [
      {state: 'frozen', xSplit: 0, ySplit: 1}
    ];

    workbook.xlsx.writeBuffer().then((data) => {
      let blob = new Blob([data], { type: 'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet' });
      fs.saveAs(blob, 'ShipmentTemplate.xlsx');
    });

  }

  exportToExcel(shipment: Array<Shipment>) {
    const workbook = new Excel.Workbook();
    const worksheet = workbook.addWorksheet("Report");
    worksheet.properties.defaultRowHeight = 20;
    worksheet.properties.defaultColWidth = 20;
    worksheet.pageSetup.paperSize = 9;
    worksheet.pageSetup.orientation = 'landscape';

    let totalLength = this.defValue?.ExcelReportHeading?.length || 0;

    let cols:any = [];
    this.defValue?.ExcelReportHeading?.forEach((ent) => {
      cols.push({header: ent, 
        key: ent.replace(/\s/g, "").toLowerCase(), 
        width: 15});
      
    });
    worksheet.columns = cols;
    

    worksheet.views = [
      {state: 'frozen', xSplit: 0, ySplit: 1}
    ];

    shipment.forEach((ent:Shipment) => {
      let len:number = ent.shipment.shipmentInformation.activity.length;
      if(len > 0) {
        len = len -1;
      }
          worksheet.addRow({awb: ent.shipment.awbno, 
                                  alternaterefrencenumber: ent.shipment.altRefNo, 
                                  accountcode: ent.shipment.senderInformation.accountNo , createdon: ent.shipment.shipmentInformation.createdOn,
                                  status: ent.shipment.shipmentInformation.activity.at(len).event, notes: ent.shipment.shipmentInformation.activity.at(len).notes, 
                                  updatedon: ent.shipment.shipmentInformation.activity.at(len).date + ':' + ent.shipment.shipmentInformation.activity.at(len).time,
                                  createdby: ent.shipment.shipmentInformation.createdBy, 
                                  senderreferencenumber: ent.shipment.senderInformation.referenceNo, 
                                  sendername: ent.shipment.senderInformation.name,
                                  sendercountry: ent.shipment.senderInformation.country, senderaddress: ent.shipment.senderInformation.address, sendercity: ent.shipment.senderInformation.city, 
                                  senderstate: ent.shipment.senderInformation.state, senderpostalcode: ent.shipment.senderInformation.postalCode,
                                  sendercontact: ent.shipment.senderInformation.contact, senderphone: ent.shipment.senderInformation.phoneNumber, 
                                  senderemail:ent.shipment.senderInformation.email, servicetype: ent.shipment.shipmentInformation.service, 
                                  numberofitems: ent.shipment.shipmentInformation.numberOfItems, goodsdescription: ent.shipment.shipmentInformation.goodsDescription, 
                                  goodsvalue: ent.shipment.shipmentInformation.goodsValue, weight: ent.shipment.shipmentInformation.weight, 
                                  weightunit: ent.shipment.shipmentInformation.weightUnits,
                                  codamount: ent.shipment.shipmentInformation.codAmount, currency: ent.shipment.shipmentInformation.currency, 
                                  sku: ent.shipment.shipmentInformation.skuNo, 
                                  receivername: ent.shipment.receiverInformation.name, receiveraddress: ent.shipment.receiverInformation.address, receivercity: ent.shipment.receiverInformation.city, 
                                  receiverstate: ent.shipment.receiverInformation.state, receiverpostalcode: ent.shipment.receiverInformation.postalCode, receivercontact: ent.shipment.receiverInformation.contact, 
                                  receiverphone: ent.shipment.receiverInformation.phone, 
                                  receiveremail: ent.shipment.receiverInformation.email 
                                },'n');
    });

    workbook.xlsx.writeBuffer().then((data) => {
      let blob = new Blob([data], { type: 'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet' });
      fs.saveAs(blob, 'ShipmentReport.xlsx');
    });
  }

  getFromExcel(fileName: string) {
    let rows: Array<string> = [];
    let workbook = new Excel.Workbook();

    workbook.xlsx.readFile(fileName).then((data) => {
      console.log("data " + data);
      workbook.eachSheet((sheetName:any, id:any) => {
        let sheet = workbook.getWorksheet(sheetName);
        for(var i = 1; i <= sheet.actualRowCount; i++) {
          rows.push(JSON.stringify(sheet.getRow(i)));
          //console.log(rows.at(i));
        }
      });
    });
    return(rows);
  }

  createAndSaveAltRefUpdateTemplate(sheetName: string) {
    const workbook = new Excel.Workbook();
    const worksheet = workbook.addWorksheet(sheetName);

    worksheet.properties.defaultRowHeight = 20;
    worksheet.properties.defaultColWidth = 20;
    worksheet.pageSetup.paperSize = 9;
    worksheet.pageSetup.orientation = 'landscape';

    worksheet.columns = [
      {header: "awbno", key: "awbno", width: 20},
      {header: "altRefNo",  key: "altRefNo",  width: 20}
    ];
    
    worksheet.views = [
      {state: 'frozen', xSplit: 0, ySplit: 1}
    ];

    workbook.xlsx.writeBuffer().then((data) => {
      let blob = new Blob([data], { type: 'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet' });
      fs.saveAs(blob, 'UpdateAltRefTemplate.xlsx');
    });

  }
}
