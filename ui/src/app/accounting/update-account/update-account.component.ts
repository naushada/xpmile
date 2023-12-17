import { Component, OnDestroy, OnInit } from '@angular/core';
import { FormBuilder, FormGroup } from '@angular/forms';
import { Account, AppGlobals, AppGlobalsDefault } from 'src/common/app-globals';
import { HttpsvcService } from 'src/common/httpsvc.service';
import { PubsubsvcService } from 'src/common/pubsubsvc.service';
import { SubSink } from 'subsink';

@Component({
  selector: 'app-update-account',
  templateUrl: './update-account.component.html',
  styleUrls: ['./update-account.component.scss']
})
export class UpdateAccountComponent implements OnInit, OnDestroy {

  subsink = new SubSink();
  defVal: AppGlobals = {...AppGlobalsDefault};
  accountForm: FormGroup;

  constructor(private fb: FormBuilder, private http: HttpsvcService, private subject: PubsubsvcService) { 
    this.accountForm = this.fb.group({
      isAccountCodeAutoGen: true,
      loginCredentials: this.fb.group({
      accountCode: '',
      accountPassword: ''}),
      personalInfo: this.fb.group({
        eventLocation:'',
	      role:'',
	      name: '',
	      contact: '',
	      email: '',
	      address: '',
		    city:'',
		    state:'',
		    postalCode:'',
      }),
      customerInfo: this.fb.group({
        companyName:'',
        quotedAmount: '',
        tradingLicense:'',
        vat: '',
        currency:'',
        bankAccountNumber: '',
        iban: ''
      })
    });
  }

  

  ngOnInit(): void {
    this.subsink.sink = this.subject.onAccount.subscribe(
      (rsp) => {this.accountForm.setValue({...rsp});}, 
      error => {}, 
      () => {});
  }

  ngOnDestroy(): void {
      this.subsink.unsubscribe();
  }
  
  retrieveAccountInfo(): void {
    let accCode: string = this.accountForm.get("loginCredentials.accountCode")?.value;
    this.http.getCustomerInfo(accCode).subscribe((rsp: Account) => {this.accountForm.setValue({...rsp});}, error => {}, () => {});

  }

  updateAccount(): void {
    //alert("Not implemented yet");
    let accCode: string = this.accountForm.get("loginCredentials.accountCode")?.value;
    this.http.updateAccountInfo(accCode, this.accountForm.value).subscribe((rsp: any) => {}, error => {alert("Account is updated failed");}, () => {alert("Account is updated successfully");});
  }
}
