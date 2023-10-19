import { Component, OnDestroy, OnInit } from '@angular/core';
import { FormArray, FormBuilder, FormGroup } from '@angular/forms';
import { Router } from '@angular/router';
import { Account, AppGlobals, AppGlobalsDefault, Shipment } from 'src/common/app-globals';
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
  isAutoGenerateState: boolean = true;
  isAwbNoDisabled: boolean = true;

  accountInfoList: Account[] = [];
  loggedInUser?: Account;
  subsink = new SubSink();

  constructor(private fb: FormBuilder, private rt:Router, private http:HttpsvcService, private subject: PubsubsvcService) {
    
    this.defValue = {...AppGlobalsDefault};

    this.subsink.sink = this.subject.onAccount.subscribe(rsp => {this.loggedInUser = rsp;},
      error => {},
      () => {});

    this.subsink.sink = this.subject.onAccountList.subscribe(rsp => {
      rsp?.forEach((elm: Account) => {this.accountInfoList.push(elm);});
      },
      error => {},
      () => {});

    this.singleShipmentForm = this.fb.group({
      isAutoGenerate: this.isAutoGenerateState,
      awbno: '',
      altRefNo: '',

      senderInformation : this.fb.group({
        accountNo: '',
        referenceNo: '',
        name:'',
        companyName:'',
        country: this.defValue.CountryName?.at(1),
        city:'',
        state:'',
        address:'',
        postalCode:'',
        contact:'',
        phoneNumber:'',
        email:'',
        receivingTaxId:''
      }),

      shipmentInformation : this.fb.group({
        activity: this.fb.array([{date: formatDate(new Date(), 'dd/MM/yyyy', 'en-GB'), event: "Document Created", 
                                  time:new Date().getHours() + ':' + new Date().getMinutes(), notes:'Document Created', driver:'', 
                                  updatedBy: this.loggedInUser?.personalInfo.name, eventLocation:'Riyadh'}]),
        skuNo:'',
        service:this.defValue.ServiceType?.at(1),
        numberOfItems:'',
        goodsDescription:'',
        goodsValue:'',
        customsValue:'',
        codAmount:'',
        vat:'',
        currency: this.defValue.Currency?.at(1),
        weight:'',
        weightUnits:'',
        cubicWeight:'',
        createdOn: formatDate(new Date(), 'dd/MM/yyyy', 'en-GB'),
        createdBy: this.loggedInUser?.personalInfo.name
      }),

      receiverInformation: this.fb.group({
        name:'',
        country:this.defValue.CountryName?.at(1),
        city:'',
        state:'',
        postalCode:'',
        contact:'',
        address:'',
        phone:'',
        email:''
      })

    });



  }

  onShipmentCreate()
  {
      console.log(this.singleShipmentForm.value);
      let new_shipment:any = {"shipment": { ...this.singleShipmentForm.value}
                    };

      //alert(JSON.stringify(new_shipment));
      this.http.createShipment(JSON.stringify(new_shipment)).subscribe((resp: any) => {alert("Waybill is create successfully for shipment.")},
                                                                       error => {alert("Waybill creation failed");},
                                                                       () => {});
  }

  ngOnInit(): void {
    if(true == this.isAutoGenerateState) {
      //this.singleShipmentForm.get('awbno')?.disable();
    } else {
      this.singleShipmentForm.get('awbno')?.enable();
    }

    if( !this.accountInfoList.length) {
      this.subsink.unsubscribe();
      this.http.getAccountInfoList().subscribe((rsp: Account[]) => {
        for(let idx = 0; idx < rsp.length; ++idx) {
          this.accountInfoList[idx] = rsp[idx];
        }
        // Now we are publishing acctList for all observers
        this.subject.emit_accountListInfo(this.accountInfoList);
      });
    }
  }

  retrieveAccountInfo(): void {
    
    if('Customer' == this.loggedInUser?.personalInfo.role) {
      this.singleShipmentForm.get('referenceNo')?.setValue(this.loggedInUser.personalInfo.name);

    } else {
      for (let idx:number = 0; idx < this.accountInfoList.length ; ++idx) {
        if(this.accountInfoList[idx].loginCredentials.accountCode == this.singleShipmentForm.get('senderInformation.accountNo')?.value) {
          this.singleShipmentForm.get('senderInformation')?.patchValue(
            {
              accountNo:      this.accountInfoList[idx].loginCredentials.accountCode,
              name:           this.accountInfoList[idx].personalInfo.name,
              companyName:    this.accountInfoList[idx].customerInfo.companyName,
              city:           this.accountInfoList[idx].personalInfo.city,
              state:          this.accountInfoList[idx].personalInfo.state,
              address:        this.accountInfoList[idx].personalInfo.address,
              postalCode:     this.accountInfoList[idx].personalInfo.postalCode,
              contact:        this.accountInfoList[idx].personalInfo.contact,
              phoneNumber:    this.accountInfoList[idx].personalInfo.contact,
              email:          this.accountInfoList[idx].personalInfo.email,
              receivingTaxId: this.accountInfoList[idx].customerInfo.vat
            });
        }
      }
    }

  }

  getIsAutoGenerateStatus(): boolean {
    return(false);
  }

  isAutoGenerateClicked(evt: any): void {
    if(true == evt.target.checked) {
      this.singleShipmentForm.get("awbno")?.disable();
    }
  }

  ngOnDestroy(): void {
      this.subsink.unsubscribe();
  }
}
