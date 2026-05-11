import { Component, OnDestroy, OnInit } from '@angular/core';
import { FormBuilder, FormGroup } from '@angular/forms';
import { formatDate } from '@angular/common';
import { Account, Inventory } from 'src/common/app-globals';
import { HttpsvcService } from 'src/common/httpsvc.service';
import { LabelService } from 'src/common/label.service';
import { PubsubsvcService } from 'src/common/pubsubsvc.service';
import { SubSink } from 'subsink';

@Component({
  selector: 'app-in-inventory',
  templateUrl: './in-inventory.component.html',
  styleUrls: ['./in-inventory.component.scss']
})
export class InInventoryComponent implements OnInit, OnDestroy {

  subsink = new SubSink();
  acctList: Account[] = [];
  loggedInUser?: Account;

  createInventoryForm: FormGroup;

  constructor(
    private fb: FormBuilder,
    private http: HttpsvcService,
    private pubsub: PubsubsvcService,
    private labels: LabelService
  ) {
    this.createInventoryForm = this.fb.group({
      sku:                '',
      productDescription: '',
      qty:                0,
      currentDate:        [formatDate(new Date(), 'dd/MM/yyyy', 'en-GB')],
      currentTime:        [new Date().getHours() + ':' + new Date().getMinutes()],
      shelf:              '',
      rowNumber:          '',
      createdBy:          '',
      accountCode:        ''
    });
  }

  ngOnInit(): void {
    if (!this.acctList.length) {
      this.http.getAccountInfoList().subscribe((rsp: Account[]) => {
        this.acctList = rsp ?? [];
        this.pubsub.emit_accountListInfo(this.acctList);
      });
    }

    this.subsink.sink = this.pubsub.onAccount.subscribe(rsp => {
      this.loggedInUser = rsp as Account;
      this.createInventoryForm.get('createdBy')?.setValue(this.loggedInUser?.personalInfo.name);
    });
  }

  ngOnDestroy(): void {
    this.subsink.unsubscribe();
  }

  createInventory(): void {
    const sku: string = this.createInventoryForm.get('sku')?.value ?? '';
    const qty: number = Number(this.createInventoryForm.get('qty')?.value ?? 0);

    if (!sku.trim()) {
      alert('SKU is required.');
      return;
    }
    if (!qty || qty < 1) {
      alert('Quantity must be at least 1.');
      return;
    }

    const inventory: Inventory = this.createInventoryForm.value;

    this.http.createInventory(inventory).subscribe({
      next:  () => {
        // Generate A10 labels for the newly registered stock so the user
        // can print them immediately — fixes the gap where users had to
        // re-enter SKU+qty on the Create Manifest page after creation.
        this.labels.createA10LabelPdf(sku, qty);
        alert(`Inventory created. ${qty} A10 label(s) downloaded for SKU "${sku}".`);
        this.resetForm();
      },
      error: () => alert('Inventory creation failed. Please try again.')
    });
  }

  private resetForm(): void {
    this.createInventoryForm.patchValue({
      sku:                '',
      productDescription: '',
      qty:                0,
      shelf:              '',
      rowNumber:          ''
      // accountCode, createdBy, currentDate/Time are kept for the next entry
    });
  }
}
