import { Component, OnDestroy, OnInit } from '@angular/core';
import { FormBuilder, FormGroup } from '@angular/forms';
import { Account, Inventory } from 'src/common/app-globals';
import { HttpsvcService } from 'src/common/httpsvc.service';
import { PubsubsvcService } from 'src/common/pubsubsvc.service';
import { SubSink } from 'subsink';
import {formatDate} from '@angular/common';

@Component({
  selector: 'app-in-inventory',
  templateUrl: './in-inventory.component.html',
  styleUrls: ['./in-inventory.component.scss']
})
export class InInventoryComponent implements OnInit,OnDestroy {

  subsink = new SubSink();
  acctList: Account[] = [];
  loggedInUser?: Account; 

  createInventoryForm: FormGroup;
  constructor(private fb: FormBuilder, private http: HttpsvcService, private pubsub:PubsubsvcService) {

    this.subsink.sink = this.pubsub.onAccountList.subscribe(
      rsp  => {this.acctList?.forEach((elm: Account) => {this.acctList?.push(elm)})},
      error => {},
      () => {});

    this.createInventoryForm = this.fb.group({
      sku: '',
      productDescription:'',
      qty:0,
      currentDate: [formatDate(new Date(Date.now()), 'dd/MM/yyyy', 'en-GB')],
      currentTime: [new Date().getHours() + ':' + new Date().getMinutes()],
      shelf:'',
      rowNumber:'',
      createdBy: '',
      accountCode:''
    });
  }

  ngOnInit(): void {

    // @@ In constructor, accList is subscribed for but it's not available then we will fetch accList via http
    //    and when we process http response,we publish acctList and because in ctor, we have subscribed so we are getting 
    //    accountCode twice, Hence unsubscribing it first.
    this.subsink.unsubscribe();

    if(!this.acctList.length) {
      this.http.getAccountInfoList().subscribe((rsp: Account[]) => {
        for(let idx = 0; idx < rsp.length; ++idx) {
          this.acctList[idx] = rsp[idx];
        }
        // Now we are publishing acctList for all observers
        this.pubsub.emit_accountListInfo(this.acctList);
      });
    }

    this.subsink.sink = this.pubsub.onAccount.subscribe(
      rsp => { 
        //alert(JSON.stringify(rsp));
        this.loggedInUser = rsp as Account;
        this.createInventoryForm.get('createdBy')?.setValue(this.loggedInUser?.personalInfo.name);
      },
      erro =>{}, 
      () => {});

  }

  createInventory() : void {
    let inventory: Inventory;
    inventory = this.createInventoryForm.value;
    //alert(JSON.stringify(inventory));

    this.http.createInventory(inventory).subscribe(
      (rsp:any) => {}, 
      error => {}, 
      () => {alert("Inventory is created successfully");});
  }

  ngOnDestroy(): void {
      this.subsink.unsubscribe();
  }
}
