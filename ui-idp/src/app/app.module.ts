import { NgModule } from '@angular/core';
import { BrowserModule } from '@angular/platform-browser';
import { FormsModule } from '@angular/forms';

import { AppRoutingModule } from './app-routing.module';
import { AppComponent } from './app.component';
import { LoginComponent } from './login.component';
import { PasswordResetComponent } from './password-reset.component';
import { PasswordResetConfirmComponent } from './password-reset-confirm.component';

@NgModule({
  declarations: [AppComponent, LoginComponent,
                 PasswordResetComponent, PasswordResetConfirmComponent],
  imports:      [BrowserModule, FormsModule, AppRoutingModule],
  bootstrap:    [AppComponent],
})
export class AppModule {}
