import { Component, OnInit } from '@angular/core';
import { FormBuilder, FormGroup } from '@angular/forms';
import { ClrLoadingState } from '@clr/angular';
import { Account, AppGlobals, AppGlobalsDefault } from 'src/common/app-globals';
import { HttpsvcService } from 'src/common/httpsvc.service';

@Component({
  selector: 'app-create-account',
  templateUrl: './create-account.component.html',
  styleUrls: ['./create-account.component.scss']
})
export class CreateAccountComponent implements OnInit {

  accountForm: FormGroup;
  defVal?: AppGlobals = {...AppGlobalsDefault};;

  constructor(private fb: FormBuilder, private http: HttpsvcService) { 
    this.accountForm = this.fb.group({
      isAccountCodeAutoGen: false,
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
  }

  createAccount(): void {
    let newAcc: Account = this.accountForm.value;
    ClrLoadingState.LOADING
    this.http.createAccount(newAcc).subscribe(
      (rsp: any) => {}, 
      error => {alert("Account Creation Failed");}, 
      () => {ClrLoadingState.SUCCESS; alert('Account is created successfully');});
  }

}
