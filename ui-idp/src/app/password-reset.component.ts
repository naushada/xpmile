import { Component, OnDestroy, OnInit } from '@angular/core';
import { Subscription } from 'rxjs';
import { PubsubsvcService } from '../common/pubsubsvc.service';

/**
 * Phase F slice 2 — reset_request page.
 *
 * Two-state UI: enter your email → submit → ALWAYS show the same
 * "if the address is registered, you'll get a link" notice (no
 * enumeration). The backend's /api/v1/idp/password/reset_request
 * returns 200 + {} whether the email exists or not; we render the
 * notice on any non-error response.
 */
@Component({
  selector: 'idp-password-reset',
  template: `
    <main class="shell">
      <section class="card" role="main" aria-labelledby="lbl">
        <img src="assets/logo.svg" alt="xpmile" class="logo" />
        <h1 id="lbl" class="title">Reset your password</h1>
        <p class="sub">Enter the email on your account. If it exists we'll send a link.</p>

        <form (ngSubmit)="onSubmit()" novalidate *ngIf="!notice">
          <label for="email">Email</label>
          <input id="email" name="email" type="email"
                 [(ngModel)]="email" autocomplete="email"
                 [disabled]="pending" autofocus required />

          <div class="error" *ngIf="error">{{ error }}</div>

          <button type="submit" class="primary" [disabled]="pending">
            {{ pending ? 'Sending…' : 'Send reset link' }}
          </button>
        </form>

        <div class="notice" *ngIf="notice">{{ notice }}</div>

        <p class="back"><a routerLink="/login">← Back to sign in</a></p>
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
    .error  { margin-top: 12px; padding: 8px 10px;
              border: 1px solid var(--color-danger);
              border-radius: var(--radius); color: var(--color-danger);
              font-size: 13px; }
    .notice { padding: 12px;
              border: 1px solid var(--color-border);
              border-radius: var(--radius);
              background: #f3f8fb;
              color: var(--color-text);
              font-size: 13px; line-height: 1.5; }
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
export class PasswordResetComponent implements OnInit, OnDestroy {
  email   = '';
  /** Mirror of onResetError. */
  error   = '';
  /** Mirror of onResetPending — disables form. */
  pending = false;
  /** Mirror of onResetNotice — when set, the form is replaced by this. */
  notice  = '';

  private subs: Subscription[] = [];

  constructor(private pubsub: PubsubsvcService) {}

  ngOnInit(): void {
    // Clear any stale state from a previous visit.
    this.pubsub.emit_resetError('');
    this.pubsub.emit_resetNotice('');
    this.pubsub.emit_resetPending(false);

    this.subs.push(this.pubsub.onResetError.subscribe(m => { this.error  = m; }));
    this.subs.push(this.pubsub.onResetPending.subscribe(p => { this.pending = p; }));
    this.subs.push(this.pubsub.onResetNotice.subscribe(n => { this.notice = n; }));
  }

  ngOnDestroy(): void { for (const s of this.subs) s.unsubscribe(); }

  async onSubmit(): Promise<void> {
    if (!this.email.trim()) {
      this.pubsub.emit_resetError('Please enter your email address.');
      return;
    }
    this.pubsub.emit_resetError('');
    this.pubsub.emit_resetPending(true);
    try {
      const body = new URLSearchParams();
      body.set('email', this.email.trim());

      const rsp = await fetch('/api/v1/idp/password/reset_request', {
        method:      'POST',
        credentials: 'include',
        headers:     { 'Content-Type': 'application/x-www-form-urlencoded' },
        body:        body.toString(),
      });
      // Backend ALWAYS returns 200 on a well-formed request (no enumeration).
      // We surface the same notice regardless — even on a 5xx — so an
      // attacker can't tell mis-typed addresses from real ones.
      if (rsp.status >= 200 && rsp.status < 500) {
        this.pubsub.emit_resetNotice(
          'If that email is registered, we just sent a reset link. ' +
          'The link expires in 1 hour.');
      } else {
        this.pubsub.emit_resetError(
          `Something went wrong on our side (HTTP ${rsp.status}). Try again in a minute.`);
      }
    } catch {
      this.pubsub.emit_resetError('Network error — please try again.');
    } finally {
      this.pubsub.emit_resetPending(false);
    }
  }
}
