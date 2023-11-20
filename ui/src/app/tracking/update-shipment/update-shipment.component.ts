import { Component, OnDestroy, OnInit } from '@angular/core';
import { FormBuilder, FormGroup } from '@angular/forms';
import '@cds/core/time/register.js';
import { Account, activityOnShipment, AppGlobals, AppGlobalsDefault} from 'src/common/app-globals';
import { HttpsvcService } from 'src/common/httpsvc.service';
import { PubsubsvcService } from 'src/common/pubsubsvc.service';
import { SubSink } from 'subsink';
import {formatDate} from '@angular/common';

@Component({
  selector: 'app-update-shipment',
  templateUrl: './update-shipment.component.html',
  styleUrls: ['./update-shipment.component.scss']
})
export class UpdateShipmentComponent implements OnInit, OnDestroy {

  subsink = new SubSink();
  loggedInUser?: Account;
  updateShipmentStatus: FormGroup;
  defValue?: AppGlobals;
  
  constructor(private http: HttpsvcService, private subject: PubsubsvcService, private fb: FormBuilder) {

    this.defValue = {...AppGlobalsDefault};

    this.subsink.add(this.subject.onAccount.subscribe(rsp => { this.loggedInUser = rsp;}, (error) => {}, () => {}));

    this.updateShipmentStatus = this.fb.group({
      shipmentNo: '',
      events: '',
      currentDate: [formatDate(new Date(), 'dd/MM/yyyy', 'en-GB')],
      currentTime: [new Date().getHours() + ':' + new Date().getMinutes()],
      notes:'',
      eventLocation: this.defValue?.CountryName?.at(0),
      manualEventLocation:'',
      driverName:'',
      updatedBy:this.loggedInUser?.personalInfo.name

    });
   }

  ngOnInit(): void {
  }

  ngOnDestroy(): void {
      this.subsink.unsubscribe();
  }
  
  onSubmit() {

    let awbNo: string = this.updateShipmentStatus.get('shipmentNo')?.value;
    let status: activityOnShipment = {...this.updateShipmentStatus.value};
    let cDate:any = formatDate(this.updateShipmentStatus.get('currentDate')?.value, 'dd/MM/yyyy', 'en-GB');
    (status as activityOnShipment).date = cDate;
    (status as activityOnShipment).event = this.updateShipmentStatus.get('events')?.value;
    (status as activityOnShipment).time = this.updateShipmentStatus.get('currentTime')?.value;
    (status as activityOnShipment).notes = this.updateShipmentStatus.get('notes')?.value;
    (status as activityOnShipment).driver = this.updateShipmentStatus.get('driverName')?.value;
    (status as activityOnShipment).updatedBy = this.updateShipmentStatus.get('updatedBy')?.value;
    (status as activityOnShipment).eventLocation = this.updateShipmentStatus.get('eventLocation')?.value;

    if(this.updateShipmentStatus.get('manualEventLocation')?.value.length) {
      (status as activityOnShipment).eventLocation = this.updateShipmentStatus.get('manualEventLocation')?.value;
    }

    let awbNoList = new Array<string>();
    awbNo = awbNo.trim();
    awbNoList = awbNo.split("\n");
    

    this.http.updateShipmentParallel(awbNoList, status).subscribe((data) => {
                          alert("Sipment Status is Updated Successfully");
                          this.updateShipmentStatus.get('shipmentNo')?.setValue('');
                          this.updateShipmentStatus.get('notes')?.setValue('');
                          
                        },
              (error) => {alert("Shipment Status Update is Failed");},
              () => {});
  }
}
