import { Component, OnInit } from '@angular/core';
import { FormBuilder, FormGroup } from '@angular/forms';
import { Account, AppGlobals, AppGlobalsDefault, Inventory } from 'src/common/app-globals';
import { HttpsvcService } from 'src/common/httpsvc.service';
import { PubsubsvcService } from 'src/common/pubsubsvc.service';
import { SubSink } from 'subsink';

@Component({
  selector: 'app-inventory-report',
  templateUrl: './inventory-report.component.html',
  styleUrls: ['./inventory-report.component.scss']
})
export class InventoryReportComponent implements OnInit {

  accountInfoList: Account[] = [];
  inventoryReportForm: FormGroup;
  defVal: AppGlobals;
  loggedInUser?: Account;
  subsink = new SubSink();

  constructor(private fb: FormBuilder, private http:HttpsvcService, private subj: PubsubsvcService) {
    this.subsink.sink = this.subj.onAccountList.subscribe(rsp => {
      rsp?.forEach((elm: Account) => {this.accountInfoList.push(elm);});
    },
    error => {},
    () => {});

    this.defVal = {...AppGlobalsDefault };
    this.inventoryReportForm = this.fb.group({
      sku:'',
      accountCode:''
    });
   }

  ngOnInit(): void {
    // @@ In constructor, accList is subscribed for but it's not available then we will fetch accList via http
    //    and when we process http response,we publish acctList and because in ctor, we have subscribed so we are getting 
    //    accountCode twice, Hence unsubscribing it first.
    this.subsink.unsubscribe();

    if(!this.accountInfoList.length) {
      this.http.getAccountInfoList().subscribe((rsp: Account[]) => {
        for(let idx = 0; idx < rsp.length; ++idx) {
          this.accountInfoList[idx] = rsp[idx];
        }
        // Now we are publishing acctList for all observers
        this.subj.emit_accountListInfo(this.accountInfoList);
      });
    }

    this.subsink.sink = this.subj.onAccount.subscribe(
      rsp => { 
        //alert(JSON.stringify(rsp));
        this.loggedInUser = rsp as Account;
        //this.createInventoryForm.get('createdBy')?.setValue(this.loggedInUser?.loginCredentials.accountCode);
      },
      erro =>{}, 
      () => {});
  }
  createInventoryReport() : void {
    let sku = this.inventoryReportForm.get('sku')?.value;
    let accCode = this.inventoryReportForm.get('accountCode')?.value;

    //alert(JSON.stringify(inventory));

    this.http.getFromInventory(sku, accCode).subscribe(
      (rsp: Inventory[]) => {}, 
      error => {}, 
      () => {alert("Inventory is created successfully");});
  }

  ngOnDestroy(): void {
      this.subsink.unsubscribe();
  }
}
