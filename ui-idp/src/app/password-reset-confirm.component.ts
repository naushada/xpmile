import { Component, OnDestroy, OnInit } from '@angular/core';
import { ActivatedRoute, Router } from '@angular/router';
import { Subscription } from 'rxjs';
import { PubsubsvcService } from '../common/pubsubsvc.service';

/**
 * Phase F slice 2 — reset_confirm page.
 *
 * Reached by the link in the reset email:
 *   {portal_base_url}/idp/password/reset/confirm?token=<32B base64url>
 *
 * The component reads `token` from the query string, lets the user
 * enter + confirm a new password, and POSTs to
 * /api/v1/idp/password/reset_confirm. The backend rejects weak
 * passwords (< 8 chars), expired tokens, and unknown tokens with
 * 400 + `error_description`; we surface that via PubsubsvcService.
 * Success navigates to /login with a clear notice so the user knows
 * to sign in with the new password.
 */
@Component({
  selector: 'idp-password-reset-confirm',
  template: `
    <main class="shell">
      <section class="card" role="main" aria-labelledby="lbl">
        <img src="assets/logo.svg" alt="xpmile" class="logo" />
        <h1 id="lbl" class="title">Set a new password</h1>
        <p class="sub" *ngIf="token">Enter a new password for your account.</p>
        <p class="sub" *ngIf="!token">
          This link is missing its token. Request a fresh reset link below.
        </p>

        <form (ngSubmit)="onSubmit()" novalidate *ngIf="token">
          <label for="new_password">New password</label>
          <input id="new_password" name="new_password" type="password"
                 [(ngModel)]="newPassword" autocomplete="new-password"
                 [disabled]="pending" autofocus required minlength="8" />

          <label for="confirm">Confirm new password</label>
          <input id="confirm" name="confirm" type="password"
                 [(ngModel)]="confirmPassword" autocomplete="new-password"
                 [disabled]="pending" required />

          <div class="error" *ngIf="error">{{ error }}</div>

          <button type="submit" class="primary" [disabled]="pending">
            {{ pending ? 'Saving…' : 'Save new password' }}
          </button>
        </form>

        <p class="back">
          <a routerLink="/password/reset">Request a new reset link</a>
          ·
          <a routerLink="/login">Back to sign in</a>
        </p>
      </section>
      <footer class="foot">© xpmile</footer>
    </main>
  `,
  styles: [`
    .shell { min-height: 100vh; display: flex; flex-direction: column;
             align-items: center; justify-content: center; padding: 16px; }
    .card  { width: 100%; max-width: 380px; background: var(--color-card);
             border: 1px solid var(--color-border); border-radius: var(--radius);
             padding: 32px 28px; box-shadow: 0 2px 12px rgba(33,52,63,0.06); }
    .logo  { display: block; margin: 0 auto 12px; }
    .title { margin: 0 0 4px; font-size: 18px; font-weight: 600; text-align: center; }
    .sub   { margin: 0 0 20px; color: var(--color-muted); text-align: center; }
    label  { display: block; margin-top: 12px; margin-bottom: 4px;
             font-size: 12px; font-weight: 500; color: var(--color-muted); }
    input  { width: 100%; padding: 8px 10px; border: 1px solid var(--color-border);
             border-radius: var(--radius); font-size: 14px; font-family: inherit;
             background: #fff; color: var(--color-text); }
    input:focus { outline: none; border-color: var(--color-primary);
                  box-shadow: 0 0 0 3px rgba(0,114,163,0.15); }
    input:disabled { background: #fafbfc; color: var(--color-muted); }
    .error { margin-top: 12px; padding: 8px 10px;
             border: 1px solid var(--color-danger);
             border-radius: var(--radius); color: var(--color-danger);
             font-size: 13px; }
    .primary { margin-top: 20px; width: 100%; padding: 10px;
               font-size: 14px; font-weight: 600; color: #fff;
               background: var(--color-primary); border: 0;
               border-radius: var(--radius); cursor: pointer; }
    .primary:hover:not(:disabled) { background: var(--color-primary-h); }
    .primary:disabled { background: var(--color-muted); cursor: not-allowed; }
    .back  { margin-top: 16px; text-align: center; font-size: 13px; }
    .foot  { margin-top: 16px; color: var(--color-muted); font-size: 12px; }
  `],
})
export class PasswordResetConfirmComponent implements OnInit, OnDestroy {
  token           = '';
  newPassword     = '';
  confirmPassword = '';
  error           = '';
  pending         = false;

  private subs: Subscription[] = [];

  constructor(private route: ActivatedRoute,
              private router: Router,
              private pubsub: PubsubsvcService) {}

  ngOnInit(): void {
    this.pubsub.emit_resetError('');
    this.pubsub.emit_resetPending(false);

    this.subs.push(
      this.route.queryParamMap.subscribe(qp => {
        this.token = qp.get('token') ?? '';
      }));
    this.subs.push(
      this.pubsub.onResetError.subscribe(m => { this.error = m; }));
    this.subs.push(
      this.pubsub.onResetPending.subscribe(p => { this.pending = p; }));
  }

  ngOnDestroy(): void { for (const s of this.subs) s.unsubscribe(); }

  async onSubmit(): Promise<void> {
    if (!this.token) {
      this.pubsub.emit_resetError('Missing reset token.');
      return;
    }
    if (this.newPassword.length < 8) {
      this.pubsub.emit_resetError('Password must be at least 8 characters.');
      return;
    }
    if (this.newPassword !== this.confirmPassword) {
      this.pubsub.emit_resetError('The two passwords don\'t match.');
      return;
    }
    this.pubsub.emit_resetError('');
    this.pubsub.emit_resetPending(true);

    try {
      const body = new URLSearchParams();
      body.set('token',        this.token);
      body.set('new_password', this.newPassword);

      const rsp = await fetch('/api/v1/idp/password/reset_confirm', {
        method:      'POST',
        credentials: 'include',
        headers:     { 'Content-Type': 'application/x-www-form-urlencoded' },
        body:        body.toString(),
      });
      if (rsp.ok) {
        // Stash a notice for /login to render and hop over.
        this.pubsub.emit_resetNotice(
          'Password updated. Sign in with your new password.');
        this.router.navigateByUrl('/login');
      } else {
        let msg = `Couldn't update password (HTTP ${rsp.status})`;
        try {
          const j: { error_description?: string; error?: string } = await rsp.json();
          msg = j.error_description || j.error || msg;
        } catch { /* non-JSON body */ }
        this.pubsub.emit_resetError(msg);
      }
    } catch {
      this.pubsub.emit_resetError('Network error — please try again.');
    } finally {
      this.pubsub.emit_resetPending(false);
    }
  }
}
