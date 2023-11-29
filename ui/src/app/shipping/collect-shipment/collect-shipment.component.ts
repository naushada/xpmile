import { Component, OnDestroy, OnInit } from '@angular/core';
import {formatDate} from '@angular/common';
import { FormBuilder, FormGroup } from '@angular/forms';
import { Router } from '@angular/router';
import { Account, AppGlobals, AppGlobalsDefault } from 'src/common/app-globals';
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
          station:'',
          customer:'',
          accCode:'',
          company:'',
          collectionAddress:'',
          city:'',
          state:'',
          postcode:'',
          service:'',
          weight:'',
          noOfShipments:'',
          noOfItems:'',
          dest:'',
          area:'',
          type:'',
          when:'',
          readyTime:'',
          contact:'',
          telephone:'',
          email:'',
          close:'',
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

  ngOnDestroy(): void {
    this.subsink.unsubscribe();
  }

}
