import { Component } from '@angular/core';
import { FormBuilder, FormGroup } from '@angular/forms';
import { AppGlobals, AppGlobalsDefault } from 'src/common/app-globals';
import { HttpsvcService } from 'src/common/httpsvc.service';

@Component({
  selector: 'app-create-account',
  templateUrl: './create-account.component.html',
  styleUrls: ['./create-account.component.scss']
})
export class CreateAccountComponent {

  accountForm: FormGroup;
  defVal: AppGlobals = { ...AppGlobalsDefault };

  constructor(private fb: FormBuilder, private http: HttpsvcService) {
    this.accountForm = this.buildForm();
  }

  createAccount(): void {
    this.http.createAccount(this.accountForm.value).subscribe({
      next:  () => alert('Account created successfully.'),
      error: () => alert('Account creation failed.')
    });
  }

  private buildForm(): FormGroup {
    return this.fb.group({
      isAccountCodeAutoGen: false,
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
