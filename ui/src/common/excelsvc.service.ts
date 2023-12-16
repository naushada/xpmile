import { Injectable } from '@angular/core';

//import { Workbook } from 'exceljs';
import * as fs from 'file-saver';
import * as Excel from 'exceljs';
import * as XLSX from 'xlsx';

import { Account, AppGlobals, AppGlobalsDefault, SenderInformation, ShipmentExcelRow } from './app-globals';
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

  exportToExcel() {
    const workbook = new Excel.Workbook();
    const worksheet = workbook.addWorksheet("data");
    workbook.xlsx.writeBuffer().then((data) => {
      let blob = new Blob([data], { type: 'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet' });
      fs.saveAs(blob, 'ExportedData.xlsx');
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
