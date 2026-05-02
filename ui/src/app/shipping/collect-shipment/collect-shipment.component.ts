import { Component, OnDestroy, OnInit } from '@angular/core';
import { formatDate } from '@angular/common';
import { FormBuilder, FormGroup } from '@angular/forms';
import { Account, AppGlobals, AppGlobalsDefault, JobDetails } from 'src/common/app-globals';
import { HttpsvcService } from 'src/common/httpsvc.service';
import { PubsubsvcService } from 'src/common/pubsubsvc.service';
import { SubSink } from 'subsink';

@Component({
  selector: 'app-collect-shipment',
  templateUrl: './collect-shipment.component.html',
  styleUrls: ['./collect-shipment.component.scss']
})
export class CollectShipmentComponent implements OnInit, OnDestroy {

  collectShipmentForm: FormGroup;
  defValue: AppGlobals = { ...AppGlobalsDefault };
  accountInfoList: Account[] = [];
  loggedInUser?: Account;
  jobDetails: JobDetails[] = [];
  selected: JobDetails[] = [];

  private accInfo?: Account;
  private subsink = new SubSink();

  constructor(
    private fb: FormBuilder,
    private http: HttpsvcService,
    private pubsub: PubsubsvcService
  ) {
    const now = new Date();
    const hhmm = `${now.getHours()}:${String(now.getMinutes()).padStart(2, '0')}`;
    this.collectShipmentForm = this.fb.group({
      from: this.fb.group({
        jobId:              '',
        station:            '',
        customer:           '',
        accCode:            '',
        company:            '',
        collectionAddress:  '',
        city:               '',
        state:              '',
        country:            '',
        postcode:           '',
        service:            '',
        weight:             '',
        noOfShipments:      '',
        noOfItems:          '',
        dest:               '',
        area:               '',
        type:               '',
        when:               formatDate(now, 'dd/MM/yyyy', 'en-GB'),
        readyTime:          hhmm,
        contact:            '',
        telephone:          '',
        email:              '',
        close:              hhmm,
        cash:               '',
        order:              '',
        pickupLocation:     '',
        transport:          '',
        additionalInfo:     '',
        specialInstructions: ''
      })
    });
  }

  ngOnInit(): void {
    this.subsink.add(
      this.pubsub.onAccount.subscribe((rsp: Account | undefined) => { this.loggedInUser = rsp; })
    );
    this.subsink.add(
      this.pubsub.onAccountList.subscribe((rsp: Account[] | undefined) => {
        rsp?.forEach(elm => this.accountInfoList.push(elm));
      })
    );

    this.jobDetails = [{
      jobId:              '12',
      station:            'Test',
      customer:           'ABC',
      accCode:            'Test',
      company:            'AABC Coo.',
      collectionAddress:  'From Delhi',
      city:               'New Delhi',
      state:              'New Delhi',
      country:            'India',
      postcode:           411048,
      service:            'Test 1',
      weight:             '1.3',
      noOfShipments:      2,
      noOfItems:          3,
      dest:               'Pune',
      area:               'Pune India, Maharastra',
      type:               'Test',
      when:               new Date(),
      readyTime:          { hours: 1, minutes: 20 },
      contact:            'Contact 1',
      telephone:          '997013613611',
      email:              'abc@example.com',
      close:              { hours: 2, minutes: 2 },
      cash:               '12USD',
      order:              '1Pack',
      pickupLocation:     'NA',
      transport:          'NA',
      additionalInfo:     'NA',
      specialInstructions: 'NA'
    }];
  }

  onEnter(_evt: any): void {
    const code = this.collectShipmentForm.get('from.accCode')?.value;
    if (!code) return;

    this.http.getAccountInfo(code).subscribe(
      (rsp: Account) => {
        this.accInfo = rsp;
        this.collectShipmentForm.get('from')?.patchValue({
          company:           rsp.customerInfo.companyName,
          collectionAddress: rsp.personalInfo.address,
          city:              rsp.personalInfo.city,
          state:             rsp.personalInfo.state,
          country:           rsp.personalInfo.eventLocation,
          postcode:          rsp.personalInfo.postalCode
        });
      },
      () => {
        this.collectShipmentForm.get('from')?.patchValue({
          company: '', collectionAddress: '', city: '', state: '', country: '', postcode: ''
        });
      }
    );
  }

  onJobCreate(): void {
    this.http.createJob(JSON.stringify(this.collectShipmentForm.value)).subscribe((rsp: any) => {
      this.collectShipmentForm.get('from.jobId')?.setValue(rsp.jobId);
    });
  }

  onAssign(): void {}
  onFinalise(): void {}
  onCancel(): void {}
  onSelectionChanged(_evt: any): void {}

  ngOnDestroy(): void {
    this.subsink.unsubscribe();
  }
}
