import { Component, OnDestroy, OnInit } from '@angular/core';
import { FormBuilder, FormGroup } from '@angular/forms';
import { Account, AppGlobals, AppGlobalsDefault, Shipment } from 'src/common/app-globals';
import { HttpsvcService } from 'src/common/httpsvc.service';
import { PubsubsvcService } from 'src/common/pubsubsvc.service';
import { SubSink } from 'subsink';

@Component({
  selector: 'app-modify',
  templateUrl: './modify.component.html',
  styleUrls: ['./modify.component.scss']
})
export class ModifyComponent implements OnInit, OnDestroy {

  defValue?: AppGlobals
  modifyShipmentForm: FormGroup;
  subsink = new SubSink();
  loggedInUser?: Account;
  isAutoGenerateState: boolean = true;
  
  constructor(private fb:FormBuilder, private http: HttpsvcService, private subject: PubsubsvcService) {

    this.defValue = {...AppGlobalsDefault };

    this.subsink.sink = this.subject.onAccount.subscribe(
      rsp => {this.loggedInUser = rsp;},
      error => {},
      () => {});

    this.modifyShipmentForm = this.fb.group({
      isAutoGenerate: this.isAutoGenerateState,
      awbno: '',
      altRefNo: '',

      senderInformation : this.fb.group({
        accountNo: '',
        referenceNo: '',
        name:'',
        companyName:'',
        country: '',
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
        activity: this.fb.array([{date: '', 
                                  event: '', 
                                  time: '', 
                                  notes: '', 
                                  driver:'', 
                                  updatedBy: '', 
                                  eventLocation: ''}]),
        skuNo:'',
        service: '',
        numberOfItems:'',
        goodsDescription:'',
        goodsValue:'',
        customsValue:'',
        codAmount:'',
        vat:'',
        currency: '',
        weight:'',
        weightUnits:'',
        cubicWeight:'',
        createdOn: '',
        createdBy: ''
      }),

      receiverInformation: this.fb.group({
        name:'',
        country:'',
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

  ngOnInit(): void {
  }

  onShipmentUpdate(){
    let ship: Shipment = {shipment: this.modifyShipmentForm.value};
    let awbNo = this.modifyShipmentForm.get('awbno')?.value;
    this.http.updateSingleShipment(awbNo, ship as any).subscribe(
      rsp => {alert("Shipment is updated successfully");},
      error => {alert("Shipment updation is failed");},
      () => {});
  }
  
  retrieveShipment() {

    let awbNo = this.modifyShipmentForm.get('awbno')?.value;
    let altrefno = this.modifyShipmentForm.get('altRefNo')?.value;

    if(awbNo && awbNo.length > 0) {

      this.http.getShipmentByAwbNo(awbNo).subscribe((rsp: any) => {
        // Got the Response 
        this.modifyShipmentForm.patchValue(rsp[0].shipment);
      },

      error => {},

      () => {});

    } else if(altrefno && altrefno.length > 0) {

      this.http.getShipmentsByAltRefNo(awbNo).subscribe((rsp: any) => {
        // Got the Response 
        this.modifyShipmentForm.patchValue(rsp[0].shipment);
      },

      error => {},

      () => {});
    }
  }

  onFetchByAwbNo() {
    this.retrieveShipment();
  }

  onFetchByAltRefNo() {
    this.retrieveShipment();
  }

  ngOnDestroy(): void {
      this.subsink.unsubscribe();
  }
}
