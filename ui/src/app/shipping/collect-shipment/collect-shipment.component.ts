import { Component, OnDestroy, OnInit } from '@angular/core';
import {formatDate} from '@angular/common';
import { FormBuilder, FormGroup } from '@angular/forms';
import { Router } from '@angular/router';
import { Account, AppGlobals, AppGlobalsDefault, JobDetails } from 'src/common/app-globals';
import { HttpsvcService } from 'src/common/httpsvc.service';
import { PubsubsvcService } from 'src/common/pubsubsvc.service';
import { SubSink } from 'subsink';

@Component({
  selector: 'app-collect-shipment',
  templateUrl: './collect-shipment.component.html',
  styleUrls: ['./collect-shipment.component.scss']
})
export class CollectShipmentComponent implements OnInit, OnDestroy {

  collectShipmentForm: FormGroup;
  
  defValue?: AppGlobals;
  isAutoGenerateState: boolean = true;
  isAwbNoDisabled: boolean = true;

  accountInfoList: Account[] = [];
  loggedInUser?: Account;
  subsink = new SubSink();
  accInfo?:Account;

  jobDetail?: JobDetails;
  jobDetails:any[] = [] ;
  selected:any;

  constructor(private fb: FormBuilder, private rt: Router, private http: HttpsvcService, private subject: PubsubsvcService) { 
    this.defValue = {...AppGlobalsDefault};

    this.subsink.sink = this.subject.onAccount.subscribe(rsp => {this.loggedInUser = rsp;},
      error => {},
      () => {});

    this.subsink.sink = this.subject.onAccountList.subscribe(rsp => {
      rsp?.forEach((elm: Account) => {this.accountInfoList.push(elm);});
      },
      error => {},
      () => {});
      
      this.collectShipmentForm = this.fb.group({
       from: this.fb.group({
          jobId:'',
          station:'',
          customer:'',
          accCode:'',
          company:'',
          collectionAddress:'',
          city:'',
          state:'',
          country:'',
          postcode:'',
          service:'',
          weight:'',
          noOfShipments:'',
          noOfItems:'',
          dest:'',
          area:'',
          type:'',
          when:formatDate(new Date(), 'dd/MM/yyyy', 'en-GB'),
          readyTime: [new Date().getHours() + ':' + new Date().getMinutes()],
          contact:'',
          telephone:'',
          email:'',
          close:[new Date().getHours() + ':' + new Date().getMinutes()],
          cash:'',
          order:'',
          pickupLocation:'',
          transport:'',
          additionalInfo:'',
          specialInstructions:''
        })
      })
  }

  ngOnInit(): void {
  }

  onSchedulePickup() {

  }

  onEnter(event: any) {
    if(this.collectShipmentForm.get('from.accCode')?.value.length > 0) {
      this.http.getAccountInfo(this.collectShipmentForm.get('from.accCode')?.value).subscribe(
        (rsp:Account) => {
          this.accInfo = rsp;
          this.collectShipmentForm.get('from.company')?.setValue(this.accInfo.customerInfo.companyName);
          this.collectShipmentForm.get('from.collectionAddress')?.setValue(this.accInfo.personalInfo.address);
          this.collectShipmentForm.get('from.city')?.setValue(this.accInfo.personalInfo.city);
          this.collectShipmentForm.get('from.state')?.setValue(this.accInfo.personalInfo.state);
          this.collectShipmentForm.get('from.country')?.setValue(this.accInfo.personalInfo.eventLocation);
          this.collectShipmentForm.get('from.postcode')?.setValue(this.accInfo.personalInfo.postalCode);
        }, 
        error => {
          this.collectShipmentForm.get('from.company')?.setValue('');
          this.collectShipmentForm.get('from.collectionAddress')?.setValue('');
          this.collectShipmentForm.get('from.city')?.setValue('');
          this.collectShipmentForm.get('from.state')?.setValue('');
          this.collectShipmentForm.get('from.country')?.setValue('');
          this.collectShipmentForm.get('from.postcode')?.setValue('');
        }, 
        () => {});
    }
    //alert(this.collectShipmentForm.get('from.accCode')?.value);
  }

  onJobCreate() {
    //Get the Open Job Count
    this.http.createJob(JSON.stringify(this.collectShipmentForm.value)).subscribe((rsp:any) => {
      let result = JSON.parse(JSON.stringify(rsp));
      this.collectShipmentForm.get('from.jobId')?.setValue(result["jobId"]);
      this.jobDetail = {...this.collectShipmentForm.value};
    },
    (error) => {},
    () => {} 
    );
  }

  ngOnDestroy(): void {
    this.subsink.unsubscribe();
  }

}
