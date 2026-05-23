import { NgModule } from '@angular/core';
import { RouterModule, Routes } from '@angular/router';
import { LoginComponent } from './login.component';
import { PasswordResetComponent } from './password-reset.component';
import { PasswordResetConfirmComponent } from './password-reset-confirm.component';

// SPA routes are relative to <base href="/idp/">, so the user-visible
// URLs are /idp/login, /idp/password/reset, /idp/password/reset/confirm.
// The `**` fallback sends every unknown path to /login — this is what
// `/api/v1/idp/authorize` redirects to when no IdP session is present.
const routes: Routes = [
  { path: 'login',                  component: LoginComponent },
  { path: 'password/reset',         component: PasswordResetComponent },
  { path: 'password/reset/confirm', component: PasswordResetConfirmComponent },
  { path: '',                       pathMatch: 'full', redirectTo: 'login' },
  { path: '**',                     redirectTo: 'login' },
];

@NgModule({
  imports: [RouterModule.forRoot(routes)],
  exports: [RouterModule],
})
export class AppRoutingModule {}
