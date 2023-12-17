import { Component, OnDestroy, OnInit } from '@angular/core';
import { FormBuilder, FormGroup } from '@angular/forms';
import { Account, Shipment } from 'src/common/app-globals';
import { HttpsvcService } from 'src/common/httpsvc.service';
import { PubsubsvcService } from 'src/common/pubsubsvc.service';
import { SubSink } from 'subsink';

@Component({
  selector: 'app-single-shipment',
  templateUrl: './single-shipment.component.html',
  styleUrls: ['./single-shipment.component.scss']
})
export class SingleShipmentComponent implements OnInit, OnDestroy {

  singleShipmentTrackingForm: FormGroup;
  whichVendor: string = "";
  loggedInUser?: Account;
  subsink = new SubSink();
  shipment?: Shipment;

  len?:number = 0;
  status:string = "";

  constructor(private fb: FormBuilder, private http: HttpsvcService, private subject: PubsubsvcService) {
    this.subsink.add(this.subject.onAccount.subscribe(rsp => { this.loggedInUser = rsp;}, (error) => {}, () => {}));

    this.singleShipmentTrackingForm = fb.group({
      awbNo: '',
      altRefNo: '',
      vendor: 'self'
    }); 
   }

  ngOnInit(): void {
  }

  ngOnDestroy(): void {
      this.subsink.unsubscribe();
  }

  onVendorSelect(what: string) {
    this.whichVendor = what;
  }

  onSubmit() {
    let awbNo = this.singleShipmentTrackingForm.get('awbNo')?.value;
    let altRefNo = this.singleShipmentTrackingForm.get('altRefNo')?.value;
    let accCode = this.loggedInUser?.loginCredentials.accountCode;

    if((awbNo != undefined && awbNo.length && this.loggedInUser?.personalInfo.role != "Employee") && 
       (awbNo != undefined && awbNo.length && this.loggedInUser?.personalInfo.role != "Admin")) {
      this.http.getShipmentByAwbNo(awbNo, accCode).subscribe((rsp: Shipment) => {this.shipment = {...rsp};}, (error) => {}, () => {});
    } else if(awbNo != undefined && awbNo.length) {

      this.http.getShipmentsByAwbNo(awbNo).subscribe((rsp:Shipment[]) => {
        this.shipment = {...rsp[0]};
        this.len = this.shipment?.shipment?.shipmentInformation?.activity.length;
        this.status = this.shipment.shipment.shipmentInformation.activity[this.len -1].event;
      }, 
      (error) => {}, 
      () => {});

    } else if((altRefNo != undefined && altRefNo.length && this.loggedInUser?.personalInfo.role != "Employee") && 
              (altRefNo != undefined && altRefNo.length && this.loggedInUser?.personalInfo.role != "Admin")) {
      this.http.getShipmentByAltRefNo(altRefNo, accCode).subscribe(rsp => {this.shipment = rsp;}, (error) => {}, () => {});
    } else {
      this.http.getShipmentByAltRefNo(altRefNo).subscribe(rsp => {this.shipment = rsp;}, (error) => {}, () => {});
    }
  }
}
