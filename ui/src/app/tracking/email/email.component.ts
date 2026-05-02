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

  constructor(private fb: FormBuilder, private http: HttpsvcService) {
    this.emailForm = this.fb.group({
      to:        '',
      cc:        '',
      bcc:       '',
      subject:   '',
      emailbody: ''
    });
  }

  sendEmail(): void {
    const email: Email = {
      from:      'balaagh.technologies@gmail.com',
      passwd:    'htxeootugssowvzl',
      name:      'Balaagh Technologies',
      to:        this.emailForm.get('to')?.value.split(','),
      cc:        this.emailForm.get('cc')?.value,
      bcc:       this.emailForm.get('bcc')?.value,
      emailbody: this.emailForm.get('emailbody')?.value,
      subject:   this.emailForm.get('subject')?.value
    };

    this.http.initiateEmail(email).subscribe({
      next:  () => alert('Email sent successfully.'),
      error: () => alert('Failed to send email.')
    });
  }
}
