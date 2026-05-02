import { Component, OnDestroy, OnInit } from '@angular/core';
import { FormBuilder, FormGroup } from '@angular/forms';
import { Account } from 'src/common/app-globals';
import { HttpsvcService } from 'src/common/httpsvc.service';
import { PubsubsvcService } from 'src/common/pubsubsvc.service';
import { SubSink } from 'subsink';

@Component({
  selector: 'app-modify',
  templateUrl: './modify.component.html',
  styleUrls: ['./modify.component.scss']
})
export class ModifyComponent implements OnInit, OnDestroy {

  modifyShipmentForm: FormGroup;
  loggedInUser?: Account;

  private subsink = new SubSink();

  constructor(
    private fb: FormBuilder,
    private http: HttpsvcService,
    private subject: PubsubsvcService
  ) {
    this.modifyShipmentForm = this.buildForm();
  }

  ngOnInit(): void {
    this.subsink.add(
      this.subject.onAccount.subscribe((rsp) => { this.loggedInUser = rsp; })
    );
  }

  ngOnDestroy(): void {
    this.subsink.unsubscribe();
  }

  onFetchByAwbNo(): void {
    const awbNo = this.modifyShipmentForm.get('awbno')?.value;
    if (!awbNo) return;
    this.subsink.add(
      this.http.getShipmentByAwbNo(awbNo).subscribe({
        next: (rsp: any) => { this.modifyShipmentForm.patchValue(rsp[0].shipment); }
      })
    );
  }

  onFetchByAltRefNo(): void {
    const altRefNo = this.modifyShipmentForm.get('altRefNo')?.value;
    if (!altRefNo) return;
    this.subsink.add(
      this.http.getShipmentsByAltRefNo([altRefNo]).subscribe({
        next: (rsp: any) => { this.modifyShipmentForm.patchValue(rsp[0].shipment); }
      })
    );
  }

  onShipmentUpdate(): void {
    const awbNo   = this.modifyShipmentForm.get('awbno')?.value;
    const payload = { shipment: this.modifyShipmentForm.value };
    this.subsink.add(
      this.http.updateSingleShipment(awbNo, payload as any).subscribe({
        next:  () => alert('Shipment updated successfully.'),
        error: () => alert('Shipment update failed.')
      })
    );
  }

  private buildForm(): FormGroup {
    return this.fb.group({
      isAutoGenerate: true,
      awbno:          '',
      altRefNo:       '',

      senderInformation: this.fb.group({
        accountNo:      '',
        referenceNo:    '',
        name:           '',
        companyName:    '',
        country:        '',
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
            date: '', event: '', time: '', notes: '', driver: '', updatedBy: '', eventLocation: ''
          })
        ]),
        skuNo:            '',
        service:          '',
        numberOfItems:    '',
        goodsDescription: '',
        goodsValue:       '',
        customsValue:     '',
        codAmount:        '',
        vat:              '',
        currency:         '',
        weight:           '',
        weightUnits:      '',
        cubicWeight:      '',
        createdOn:        '',
        createdBy:        ''
      }),

      receiverInformation: this.fb.group({
        name:       '',
        country:    '',
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
