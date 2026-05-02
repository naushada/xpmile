import { Component } from '@angular/core';
import { FormBuilder, FormGroup } from '@angular/forms';
import { Email } from 'src/common/app-globals';
import { HttpsvcService } from 'src/common/httpsvc.service';

@Component({
  selector: 'app-email',
  templateUrl: './email.component.html',
  styleUrls: ['./email.component.scss']
})
export class EmailComponent {

  emailForm: FormGroup;
  attachedFiles: File[] = [];

  constructor(private fb: FormBuilder, private http: HttpsvcService) {
    this.emailForm = this.fb.group({
      to:        '',
      cc:        '',
      bcc:       '',
      subject:   '',
      emailbody: ''
    });
  }

  onFilesSelected(event: Event): void {
    const input = event.target as HTMLInputElement;
    this.attachedFiles = input.files ? Array.from(input.files) : [];
  }

  sendEmail(): void {
    const readAll = this.attachedFiles.map(file =>
      new Promise<{ 'file-name': string; 'file-content': string }>((resolve, reject) => {
        const reader = new FileReader();
        reader.onload  = () => resolve({ 'file-name': file.name, 'file-content': (reader.result as string).split(',')[1] });
        reader.onerror = () => reject(reader.error);
        reader.readAsDataURL(file);
      })
    );

    Promise.all(readAll).then(files => {
      const email: Email = {
        from:      'balaagh.technologies@gmail.com',
        passwd:    'htxeootugssowvzl',
        name:      'Balaagh Technologies',
        to:        this.emailForm.get('to')?.value.split(','),
        cc:        this.emailForm.get('cc')?.value,
        bcc:       this.emailForm.get('bcc')?.value,
        emailbody: this.emailForm.get('emailbody')?.value,
        subject:   this.emailForm.get('subject')?.value,
        ...(files.length > 0 && { files })
      };

      this.http.initiateEmail(email).subscribe({
        next:  () => alert('Email sent successfully.'),
        error: () => alert('Failed to send email.')
      });
    }).catch(() => alert('Failed to read attachment(s).'));
  }
}
