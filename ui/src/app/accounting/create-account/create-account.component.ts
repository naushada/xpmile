import { Component } from '@angular/core';
import { FormBuilder, FormGroup, Validators } from '@angular/forms';
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

  /**
   * Profile photo upload at account-creation time. Mirror of
   * update-account.component.ts onPhotoSelected — same client-side
   * resize pipeline (canvas → 256×256 → JPEG @ 0.85). Patches
   * personalInfo.photoBase64 in the form; existing createAccount()
   * flow ships the field with the rest of the doc.
   */
  onPhotoSelected(event: Event): void {
    const input = event.target as HTMLInputElement;
    const file = input.files?.[0];
    if (!file) return;

    const MAX_BYTES_RAW = 5 * 1024 * 1024;
    const MAX_DIM = 256;
    const QUALITY = 0.85;

    if (!file.type.startsWith('image/')) {
      alert('Please pick an image file (JPEG, PNG, etc.).');
      input.value = '';
      return;
    }
    if (file.size > MAX_BYTES_RAW) {
      alert(`Image too big (${Math.round(file.size / 1024 / 1024)} MB). Max ${MAX_BYTES_RAW / 1024 / 1024} MB before client-side resize.`);
      input.value = '';
      return;
    }

    const reader = new FileReader();
    reader.onload = () => {
      const img = new Image();
      img.onload = () => {
        const scale = Math.min(1, MAX_DIM / Math.max(img.width, img.height));
        const w = Math.round(img.width  * scale);
        const h = Math.round(img.height * scale);
        const canvas = document.createElement('canvas');
        canvas.width  = w;
        canvas.height = h;
        const ctx = canvas.getContext('2d');
        if (!ctx) { alert('Browser does not support canvas resize — cannot encode photo.'); return; }
        ctx.drawImage(img, 0, 0, w, h);
        this.accountForm.get('personalInfo.photoBase64')?.setValue(canvas.toDataURL('image/jpeg', QUALITY));
      };
      img.onerror = () => alert('Could not decode that image file.');
      img.src = reader.result as string;
    };
    reader.onerror = () => alert('Could not read the file.');
    reader.readAsDataURL(file);
  }

  onPhotoClear(): void {
    this.accountForm.get('personalInfo.photoBase64')?.setValue('');
  }

  get currentPhotoBase64(): string {
    return this.accountForm.get('personalInfo.photoBase64')?.value || '';
  }

  private buildForm(): FormGroup {
    return this.fb.group({
      isAccountCodeAutoGen: false,
      awbPrefix: '',
      loginCredentials: this.fb.group({
        accountCode:     '',
        accountPassword: ''
      }),
      personalInfo: this.fb.group({
        eventLocation: '',
        role:          '',
        // Name is required so the navbar (main.component.html) has something
        // to render next to the user icon. Before this fix, accounts created
        // with name blank rendered an empty <span> and the navbar looked
        // broken until the user clicked the icon dropdown.
        name:          ['', Validators.required],
        contact:       '',
        email:         '',
        address:       '',
        city:          '',
        state:         '',
        postalCode:    '',
        // Optional base64 profile photo — same shape as update-account.
        // See onPhotoSelected() for the resize pipeline.
        photoBase64:   ''
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
