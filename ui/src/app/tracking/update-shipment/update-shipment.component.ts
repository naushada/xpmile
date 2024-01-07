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
      currentDate: new Date(),
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
    let activity = {
      date: formatDate(this.updateShipmentStatus.get('currentDate')?.value, 'dd/MM/yyyy', 'en-GB'),
      event: this.updateShipmentStatus.get('events')?.value,
      time: this.updateShipmentStatus.get('currentTime')?.value,
      notes: this.updateShipmentStatus.get('notes')?.value,
      driver: this.updateShipmentStatus.get('driverName')?.value,
      updatedBy: this.updateShipmentStatus.get('updatedBy')?.value,
      eventLocation: this.updateShipmentStatus.get('eventLocation')?.value
    };
    

    if(this.updateShipmentStatus.get('manualEventLocation')?.value.length) {
      activity.eventLocation = this.updateShipmentStatus.get('manualEventLocation')?.value;
    }

    let awbNoList = new Array<string>();
    awbNo = awbNo.trim();
    awbNoList = awbNo.split("\n");
    

    this.http.updateShipmentParallel(awbNoList, JSON.stringify(activity)).subscribe((data) => {
                          alert("Sipment Status is Updated Successfully");
                          this.updateShipmentStatus.get('shipmentNo')?.setValue('');
                          this.updateShipmentStatus.get('notes')?.setValue('');
                          
                        },
              (error) => {alert("Shipment Status Update is Failed");},
              () => {});
  }
}
