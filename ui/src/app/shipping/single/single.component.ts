import { Component, OnDestroy, OnInit } from '@angular/core';
import { FormBuilder, FormGroup } from '@angular/forms';
import { Account, AppGlobals, AppGlobalsDefault } from 'src/common/app-globals';
import { HttpsvcService } from 'src/common/httpsvc.service';
import { formatDate } from '@angular/common';
import { SubSink } from 'subsink';
import { PubsubsvcService } from 'src/common/pubsubsvc.service';

@Component({
  selector: 'app-single',
  templateUrl: './single.component.html',
  styleUrls: ['./single.component.scss']
})
export class SingleComponent implements OnInit, OnDestroy {

  singleShipmentForm: FormGroup;

  defValue?: AppGlobals;
  isAwbNoDisabled = true;

  accountInfoList: Account[] = [];
  loggedInUser?: Account;

  private subsink = new SubSink();

  constructor(
    private fb: FormBuilder,
    private http: HttpsvcService,
    private subject: PubsubsvcService
  ) {
    this.defValue = { ...AppGlobalsDefault };
    this.singleShipmentForm = this.buildForm();
  }

  ngOnInit(): void {
    this.singleShipmentForm.get('awbno')?.enable();

    this.subsink.add(
      this.subject.onAccount.subscribe((rsp) => { this.loggedInUser = rsp; }),
      this.subject.onAccountList.subscribe((rsp) => { this.accountInfoList = rsp ?? []; })
    );

    if (!this.accountInfoList.length) {
      this.subsink.add(
        this.http.getAccountInfoList().subscribe({
          next: (rsp: Account[]) => {
            this.accountInfoList = rsp;
            this.subject.emit_accountListInfo(rsp);
          }
        })
      );
    }
  }

  ngOnDestroy(): void {
    this.subsink.unsubscribe();
  }

  onAutoGenerate(event: any): void {
    this.isAwbNoDisabled = event.target.checked;
  }

  retrieveAccountInfo(): void {
    if ('Customer' === this.loggedInUser?.personalInfo.role) {
      this.singleShipmentForm.get('referenceNo')?.setValue(this.loggedInUser.personalInfo.name);
      return;
    }

    const selectedCode = this.singleShipmentForm.get('senderInformation.accountNo')?.value;
    const account = this.accountInfoList.find(
      a => a.loginCredentials.accountCode === selectedCode
    );
    if (!account) return;

    this.singleShipmentForm.get('senderInformation')?.patchValue({
      accountNo:      account.loginCredentials.accountCode,
      name:           account.personalInfo.name,
      companyName:    account.customerInfo.companyName,
      city:           account.personalInfo.city,
      state:          account.personalInfo.state,
      address:        account.personalInfo.address,
      postalCode:     account.personalInfo.postalCode,
      contact:        account.personalInfo.contact,
      phoneNumber:    account.personalInfo.contact,
      email:          account.personalInfo.email,
      receivingTaxId: account.customerInfo.vat
    });
  }

  onShipmentCreate(): void {
    const payload = { shipment: { ...this.singleShipmentForm.value } };
    this.http.createShipment(JSON.stringify(payload)).subscribe({
      next:  () => alert('Waybill created successfully.'),
      error: () => alert('Waybill creation failed.')
    });
  }

  private buildForm(): FormGroup {
    const now   = new Date();
    const today = formatDate(now, 'dd/MM/yyyy', 'en-GB');
    const time  = `${now.getHours()}:${now.getMinutes()}`;

    return this.fb.group({
      isAutoGenerate: true,
      awbno:          '',
      altRefNo:       '',

      senderInformation: this.fb.group({
        accountNo:      '',
        referenceNo:    '',
        name:           '',
        companyName:    '',
        country:        this.defValue?.CountryName?.[0] ?? '',
        city:           '',
        state:          '',
        address:        '',
        postalCode:     '',
        contact:        '',
        phoneNumber:    '',
        email:          '',
        receivingTaxId: ''
      }),

      shipmentInformation: this.fb.group({
        activity: this.fb.array([
          this.fb.group({
            date:          today,
            event:         'Document Created',
            time:          time,
            notes:         'Document Created',
            driver:        '',
            updatedBy:     '',
            eventLocation: 'Riyadh'
          })
        ]),
        skuNo:            '',
        service:          this.defValue?.ServiceType?.[0] ?? '',
        numberOfItems:    '',
        goodsDescription: '',
        goodsValue:       '',
        customsValue:     '',
        codAmount:        '',
        vat:              '',
        currency:         this.defValue?.Currency?.[0] ?? '',
        weight:           '',
        weightUnits:      '',
        cubicWeight:      '',
        createdOn:        today,
        createdBy:        ''
      }),

      receiverInformation: this.fb.group({
        name:       '',
        country:    this.defValue?.CountryName?.[0] ?? '',
        city:       '',
        state:      '',
        postalCode: '',
        contact:    '',
        address:    '',
        phone:      '',
        email:      ''
      })
    });
  }
}
