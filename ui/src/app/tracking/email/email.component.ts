import { Component, OnInit } from '@angular/core';
import { FormBuilder, FormGroup } from '@angular/forms';
import { Email } from 'src/common/app-globals';
import { HttpsvcService } from 'src/common/httpsvc.service';

@Component({
  selector: 'app-email',
  templateUrl: './email.component.html',
  styleUrls: ['./email.component.scss']
})
export class EmailComponent implements OnInit {

  emailForm: FormGroup
  constructor(private fb: FormBuilder, private http:HttpsvcService) {
    this.emailForm = this.fb.group({
      to: '',
      cc:'',
      bcc:'',
      subject:'',
      emailbody:''
    });
   }

  ngOnInit(): void {
  }

  sendEmail() : void {

    let email: Email = {
      //TODO puth these into DB Table 
      from: "balaagh.technologies@gmail.com", 
      passwd: "htxeootugssowvzl",
      name: "Balaagh Technologies",
      to: this.emailForm.get('to')?.value.split(','),
      cc: this.emailForm.get('cc')?.value,
      bcc: this.emailForm.get('bcc')?.value,
      emailbody: this.emailForm.get('emailbody')?.value,
      subject: this.emailForm.get('subject')?.value
    };

    this.http.initiateEmail(email).subscribe((rsp: any) => {alert("Email is sent successfully");}, error => {}, () => {alert("Email is sent successfully");});
  }
}
