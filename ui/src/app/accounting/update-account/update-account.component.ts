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

  /**
   * Profile photo upload handler. Bound to the file input on the
   * update-account form; on file pick:
   *   1. Validates it's an image and under MAX_BYTES_RAW (5 MB raw file
   *      size — generous; we resize before storing so the on-wire size is
   *      always ~30-50 KB regardless of input).
   *   2. Draws the image onto a canvas scaled to fit in MAX_DIM×MAX_DIM
   *      (preserves aspect ratio) and encodes JPEG at QUALITY.
   *   3. Stores the resulting data URL in personalInfo.photoBase64
   *      (patches the form so the existing updateAccount() flow ships it
   *      to mongo on Save). Also updates the thumbnail preview so the
   *      user sees what's about to be saved.
   *
   * Why client-side resize: avoids shipping multi-MB raw photos to mongo
   * via the wsdbagent, blowing through the dbproto message budget on
   * marvel's WebSocket. 256×256 is plenty for a 32×32 navbar avatar.
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
        // Fit-into MAX_DIM×MAX_DIM preserving aspect ratio.
        const scale = Math.min(1, MAX_DIM / Math.max(img.width, img.height));
        const w = Math.round(img.width  * scale);
        const h = Math.round(img.height * scale);
        const canvas = document.createElement('canvas');
        canvas.width  = w;
        canvas.height = h;
        const ctx = canvas.getContext('2d');
        if (!ctx) { alert('Browser does not support canvas resize — cannot encode photo.'); return; }
        ctx.drawImage(img, 0, 0, w, h);
        const dataUrl = canvas.toDataURL('image/jpeg', QUALITY);
        // Patch the form (existing updateAccount() will ship it on Save).
        this.accountForm.get('personalInfo.photoBase64')?.setValue(dataUrl);
      };
      img.onerror = () => alert('Could not decode that image file.');
      img.src = reader.result as string;
    };
    reader.onerror = () => alert('Could not read the file.');
    reader.readAsDataURL(file);
  }

  /** Clear the photo (sets the form field to empty so save removes it). */
  onPhotoClear(): void {
    this.accountForm.get('personalInfo.photoBase64')?.setValue('');
  }

  /** Used by the template to show the thumbnail preview. */
  get currentPhotoBase64(): string {
    return this.accountForm.get('personalInfo.photoBase64')?.value || '';
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
        postalCode:    '',
        // base64 data URL — see onPhotoSelected() for the resize pipeline.
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
