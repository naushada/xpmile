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

  defVal: AppGlobals = { ...AppGlobalsDefault };
  accountForm: FormGroup;

  private subsink = new SubSink();

  constructor(
    private fb: FormBuilder,
    private http: HttpsvcService,
    private subject: PubsubsvcService
  ) {
    this.accountForm = this.buildForm();
  }

  ngOnInit(): void {
    this.subsink.add(
      this.subject.onAccount.subscribe({
        next: (rsp) => { if (rsp) this.accountForm.setValue({ ...rsp }); }
      })
    );
  }

  ngOnDestroy(): void {
    this.subsink.unsubscribe();
  }

  retrieveAccountInfo(): void {
    const accCode = this.accountForm.get('loginCredentials.accountCode')?.value;
    if (!accCode) return;
    this.subsink.add(
      this.http.getCustomerInfo(accCode).subscribe({
        next:  (rsp: Account) => { this.accountForm.setValue({ ...rsp }); },
        error: ()             => { alert('Account not found.'); }
      })
    );
  }

  updateAccount(): void {
    const accCode = this.accountForm.get('loginCredentials.accountCode')?.value;
    this.subsink.add(
      this.http.updateAccountInfo(accCode, this.accountForm.value).subscribe({
        next:  () => alert('Account updated successfully.'),
        error: () => alert('Account update failed.')
      })
    );
  }

  private buildForm(): FormGroup {
    return this.fb.group({
      isAccountCodeAutoGen: true,
      loginCredentials: this.fb.group({
        accountCode:     '',
        accountPassword: ''
      }),
      personalInfo: this.fb.group({
        eventLocation: '',
        role:          '',
        name:          '',
        contact:       '',
        email:         '',
        address:       '',
        city:          '',
        state:         '',
        postalCode:    ''
      }),
      customerInfo: this.fb.group({
        companyName:       '',
        quotedAmount:      '',
        tradingLicense:    '',
        vat:               '',
        currency:          '',
        bankAccountNumber: '',
        iban:              ''
      })
    });
  }
}
