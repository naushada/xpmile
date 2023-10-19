import { Component, OnInit } from '@angular/core';
import { FormBuilder, FormControl, FormControlName, FormGroup, Validators } from '@angular/forms';
import { Router } from '@angular/router';
import { ClrLoadingState } from '@clr/angular';
import { Account } from 'src/common/app-globals';
import { HttpsvcService } from 'src/common/httpsvc.service';
import { PubsubsvcService } from 'src/common/pubsubsvc.service';
import { __values } from 'tslib';

@Component({
  selector: 'app-login',
  templateUrl: './login.component.html',
  styleUrls: ['./login.component.scss']
})
export class LoginComponent implements OnInit {

  isPasswordReset:boolean = false;
  loginForm: FormGroup;
  get username() {
    return this.loginForm.value.username;
  }

  get password() {
    return this.loginForm.value.password;
  }

  constructor(private fb: FormBuilder, private rt: Router, private http: HttpsvcService, private on: PubsubsvcService) { 

    this.loginForm = this.fb.group({
      corporatename: ['', Validators.required],
      username: ['', Validators.required],
      password: ['', Validators.required]
    });

  }
  
  onChange(event:any) {
    
  }

  ngOnInit(): void {
  }

  getHash32 = (str: string) => {
    let hash = 0
    for (let i = 0; i < str.length; ++i)
      hash = Math.imul(31, hash) + str.charCodeAt(i)
  
    return hash | 0
  }

  onLogin() {
    let cName = this.loginForm.get('corporatename')?.value;
    let passwd = this.loginForm.get('password')?.value;
    let id = this.loginForm.get('username')?.value;
    //this.loginForm.get('password')?.setValue(this.getHash32(passwd));

    if(id.length && passwd.length) {
      this.http.getAccountInfo(id, passwd).subscribe(
        (rsp:Account) => {
          // Publish the Account Info of logged in user to subscribed widget
          this.on.emit_accountInfo(rsp);
        }, 
        error => {alert("Login Failed");}, 
        () => {this.rt.navigateByUrl('/main');});
    } else{
      alert("Please provide Username And Password");
    }
  }

  validateBtnState: ClrLoadingState = ClrLoadingState.DEFAULT;
  submitBtnState: ClrLoadingState = ClrLoadingState.DEFAULT;

  validateDemo() {
    this.validateBtnState = ClrLoadingState.LOADING;
    //Validating Logic
    //this.validateBtnState = ClrLoadingState.SUCCESS;
  }

  submitDemo() {
    this.submitBtnState = ClrLoadingState.LOADING;
    //Submit Logic
    this.submitBtnState = ClrLoadingState.DEFAULT;
  }

  onPasswordReset() {
    this.isPasswordReset = true;
  }
}
