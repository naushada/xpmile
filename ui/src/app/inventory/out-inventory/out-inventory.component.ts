import { Component, OnDestroy, OnInit } from '@angular/core';
import { FormBuilder, FormGroup } from '@angular/forms';
import { Account } from 'src/common/app-globals';
import { HttpsvcService } from 'src/common/httpsvc.service';
import { PubsubsvcService } from 'src/common/pubsubsvc.service';
import { SubSink } from 'subsink';

@Component({
  selector: 'app-out-inventory',
  templateUrl: './out-inventory.component.html',
  styleUrls: ['./out-inventory.component.scss']
})
export class OutInventoryComponent implements OnInit, OnDestroy {

  subsink = new SubSink();
  acctList: Account[] = [];
  loggedInUser?: Account;

  outInventoryForm: FormGroup;
  constructor(private pubsub: PubsubsvcService, private http: HttpsvcService, private fb: FormBuilder) { 

    this.subsink.sink = this.pubsub.onAccountList.subscribe(
      rsp  => {this.acctList?.forEach((elm: Account) => {this.acctList?.push(elm)})},
      error => {},
      () => {});

      this.outInventoryForm = this.fb.group({
        sku:'',
        qty:'',
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
      },
      erro =>{}, 
      () => {});

  }


  outInventory() : void {
    let sku = this.outInventoryForm.get('sku')?.value;
    let qty = this.outInventoryForm.get('qty')?.value;
    let accCode = this.outInventoryForm.get('accountCode')?.value;
    
    //alert("sku " + sku + " qty " + qty + " accCode " + accCode);
    this.http.updateInventory(sku, qty, accCode).subscribe((rsp:any) => {}, error => {}, () => {alert("Item is Removed from Inventory");});
  }
  ngOnDestroy(): void {
    this.subsink.unsubscribe();
  }
}
