import { Component } from '@angular/core';
import { FormBuilder, FormGroup } from '@angular/forms';
import { LabelService, LabelSize } from 'src/common/label.service';

@Component({
  selector: 'app-create-manifest',
  templateUrl: './create-manifest.component.html',
  styleUrls: ['./create-manifest.component.scss']
})
export class CreateManifestComponent {

  manifestForm: FormGroup;
  readonly sizes: LabelSize[] = ['A2', 'A4', 'A6', 'A10'];

  constructor(private fb: FormBuilder, private labels: LabelService) {
    this.manifestForm = this.fb.group({
      sku:  '',
      qty:  1,
      size: 'A10' as LabelSize
    });
  }

  onCreateLabel(): void {
    const sku: string     = this.manifestForm.get('sku')?.value ?? '';
    const qty: number     = Number(this.manifestForm.get('qty')?.value ?? 0);
    const size: LabelSize = (this.manifestForm.get('size')?.value ?? 'A10') as LabelSize;

    if (!sku.trim()) {
      alert('Please enter a SKU before generating labels.');
      return;
    }
    if (!qty || qty < 1) {
      alert('Quantity must be at least 1.');
      return;
    }

    this.labels.createLabelPdf(sku, qty, size);
  }
}
