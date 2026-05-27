import { Component, OnDestroy, OnInit } from '@angular/core';
import { FormBuilder, FormGroup, Validators } from '@angular/forms';
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
        next: (rsp) => { if (rsp) this.accountForm.patchValue({ ...rsp }); }
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
        next:  (rsp: Account) => { this.accountForm.patchValue({ ...rsp }); },
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
      awbPrefix: '',
      loginCredentials: this.fb.group({
        accountCode:     '',
        accountPassword: ''
      }),
      personalInfo: this.fb.group({
        eventLocation: '',
        role:          '',
        // Name is required so the navbar (main.component.html) has something
        // to render next to the user icon. Empty `name` produces a blank
        // <span> that looks like a broken navbar; the fallback in the
        // template is to render `accountCode` but we'd rather not need it.
        name:          ['', Validators.required],
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
