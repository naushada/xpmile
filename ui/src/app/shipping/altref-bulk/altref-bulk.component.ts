import { Component, OnInit } from '@angular/core';
import * as XLSX from 'xlsx';
import { ExcelsvcService } from 'src/common/excelsvc.service';
import { Account, UpdateAltRefForShipment } from 'src/common/app-globals';
import { SubSink } from 'subsink';
import { FormBuilder, FormGroup } from '@angular/forms';
import { PubsubsvcService } from 'src/common/pubsubsvc.service';
import { HttpsvcService } from 'src/common/httpsvc.service';

@Component({
  selector: 'app-altref-bulk',
  templateUrl: './altref-bulk.component.html',
  styleUrls: ['./altref-bulk.component.scss']
})
export class AltrefBulkComponent implements OnInit {

  public loggedInUser?: Account;
  public subsink: SubSink = new SubSink();
  public bulkAltRefUpdateForm: FormGroup;
  
  public accountInfoList: Map<string, Account > = new Map<string, Account>();
  public altRefUpdateExcelRows?: Array<UpdateAltRefForShipment> = new Array<UpdateAltRefForShipment>();


  constructor(private xls: ExcelsvcService, private fb: FormBuilder,  private subject: PubsubsvcService, private http: HttpsvcService) {

    this.subsink.sink = this.subject.onAccount.subscribe(rsp => {this.loggedInUser = rsp;},
      error => {},
      () => {});

    this.bulkAltRefUpdateForm = this.fb.group({
      excelFileName: ''
    });

   }

  ngOnInit(): void {
  }


  onDownloadTemplate() {
    this.xls.createAndSaveAltRefUpdateTemplate("AltRefUUpdate");
  }

  /////
  onUpdateBulkShipment() {
    let awbno: Array<string> = [];
    let altrefno: Array<string> = [];

    this.altRefUpdateExcelRows?.forEach((ent: UpdateAltRefForShipment) => {
      awbno.push(ent.awbno.toString());
      altrefno.push(ent.altRefNo.toString());
    });
  
    let jObject:any = {
      "awbno": awbno,
      "altrefno": altrefno
    };
    //console.log(jObject);

    this.http.updateBulkAltRefForShipments(jObject).subscribe((rsp: any) =>{
      let record: any; 
      let jObj = JSON.stringify(rsp);
      record = JSON.parse(jObj); 
      alert("Alt Ref for Shipments Updated are: " + record["updatedShipments"]);
    },
    error => {alert("ALT REF Update Failed");},
    () => {});
  }

  public processAltRefUpdateShipmentExcelFile(evt: any) {
    if(evt.target.files[0] == undefined) return;

    let rows: any[] = [];
    ///this.alttRefUpdateExcelRows?. = 0;
    const fileReader = new FileReader();
    fileReader.readAsBinaryString(evt.target.files[0]);

    /** This is lamda Funtion = anonymous function */
    fileReader.onload = (event) => {
      let binaryData = event.target?.result;
      /** wb -- workBook of excel sheet */
      let wb = XLSX.read(binaryData, {type:'binary'});
      
      wb.SheetNames.forEach(sheet => {
        let data = XLSX.utils.sheet_to_json(wb.Sheets[sheet]);
        rows = <any[]>data;
        for(let idx:number = 0; idx < rows.length; ++idx) {
          
          this.altRefUpdateExcelRows?.push(rows.at(idx));
        }
      });
    }

    /** This lamda Fn is invoked once excel file is loaded */
    fileReader.onloadend = (event) => {
      console.log(this.altRefUpdateExcelRows);
    }

    fileReader.onerror = (event) => {
      alert("Excel File is invalid: ");
    }
  }

  onFileSelect(event: any) {
    console.log(event.target.files[0]);
    this.processAltRefUpdateShipmentExcelFile(event);
  }
  
  ngOnDestroy(): void {
      this.subsink.unsubscribe();
  }
}
