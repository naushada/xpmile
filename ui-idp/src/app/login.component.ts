import { Component, OnDestroy, OnInit } from '@angular/core';
import { Subscription } from 'rxjs';
import { PubsubsvcService } from '../common/pubsubsvc.service';

/**
 * The IdP login portal — single page (Phase F login-only slice).
 *
 * Lifecycle:
 *   1. RP redirects browser to /api/v1/idp/authorize?...
 *   2. Backend sees no xpmile_idp_session cookie, persists an
 *      idp_pending_auth doc keyed by req_token, sets the
 *      xpmile_idp_pending cookie, returns 302 to /idp/login.
 *   3. Browser fetches /idp/login → this SPA renders the form.
 *   4. User submits credentials. We fetch() POST application/x-www-
 *      form-urlencoded to /api/v1/idp/login. Backend reads
 *      username + password from the body, the xpmile_idp_pending
 *      cookie ties it back to the original /authorize.
 *       - on success: backend returns 200 + {ok, redirect_to}
 *         + Set-Cookie. We assign window.location to redirect_to
 *         so the browser walks /authorize → RP redirect naturally.
 *       - on failure: backend returns 401 {error, error_description}.
 *         We publish error_description via
 *         PubsubsvcService.emit_loginError, the template subscribes.
 *
 * The pub/sub split (component holds form state; service holds
 * cross-cutting auth events) mirrors ui/src/app/.../*.ts which
 * inject PubsubsvcService for AccountInfo + Shipment events.
 */
@Component({
  selector: 'idp-login',
  template: `
    <main class="shell">
      <section class="card" role="main" aria-labelledby="lbl">
        <img src="assets/logo.svg" alt="xpmile" class="logo" />
        <h1 id="lbl" class="title">Sign in to continue</h1>
        <p class="sub">Use your xpmile credentials.</p>

        <form (ngSubmit)="onSubmit()" novalidate>
          <label for="user">Username</label>
          <input id="user" name="user" type="text"
                 [(ngModel)]="user" autocomplete="username"
                 [disabled]="pending" autofocus required />

          <label for="password">Password</label>
          <input id="password" name="password" type="password"
                 [(ngModel)]="password" autocomplete="current-password"
                 [disabled]="pending" required />

          <div class="error" *ngIf="error">{{ error }}</div>

          <button type="submit" class="primary" [disabled]="pending">
            {{ pending ? 'Signing in…' : 'Sign in' }}
          </button>
        </form>
      </section>
      <footer class="foot">© xpmile</footer>
    </main>
  `,
  styles: [`
    .shell {
      min-height: 100vh;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      padding: 16px;
    }
    .card {
      width: 100%;
      max-width: 380px;
      background: var(--color-card);
      border: 1px solid var(--color-border);
      border-radius: var(--radius);
      padding: 32px 28px;
      box-shadow: 0 2px 12px rgba(33, 52, 63, 0.06);
    }
    .logo { display: block; margin: 0 auto 12px; }
    .title {
      margin: 0 0 4px;
      font-size: 18px;
      font-weight: 600;
      text-align: center;
    }
    .sub {
      margin: 0 0 20px;
      color: var(--color-muted);
      text-align: center;
    }
    label {
      display: block;
      margin-top: 12px;
      margin-bottom: 4px;
      font-size: 12px;
      font-weight: 500;
      color: var(--color-muted);
    }
    input {
      width: 100%;
      padding: 8px 10px;
      border: 1px solid var(--color-border);
      border-radius: var(--radius);
      font-size: 14px;
      font-family: inherit;
      background: #fff;
      color: var(--color-text);
    }
    input:focus {
      outline: none;
      border-color: var(--color-primary);
      box-shadow: 0 0 0 3px rgba(0, 114, 163, 0.15);
    }
    input:disabled { background: #fafbfc; color: var(--color-muted); }
    .error {
      margin-top: 12px;
      padding: 8px 10px;
      border: 1px solid var(--color-danger);
      border-radius: var(--radius);
      color: var(--color-danger);
      font-size: 13px;
    }
    .primary {
      margin-top: 20px;
      width: 100%;
      padding: 10px;
      font-size: 14px;
      font-weight: 600;
      color: #fff;
      background: var(--color-primary);
      border: 0;
      border-radius: var(--radius);
      cursor: pointer;
    }
    .primary:hover:not(:disabled) { background: var(--color-primary-h); }
    .primary:disabled { background: var(--color-muted); cursor: not-allowed; }
    .foot {
      margin-top: 16px;
      color: var(--color-muted);
      font-size: 12px;
    }
  `],
})
export class LoginComponent implements OnInit, OnDestroy {
  user     = '';
  password = '';
  /** Mirror of PubsubsvcService.onLoginError — bound in template. */
  error    = '';
  /** Mirror of PubsubsvcService.onLoginPending — disables form. */
  pending  = false;

  private subs: Subscription[] = [];

  constructor(private pubsub: PubsubsvcService) {}

  ngOnInit(): void {
    // Subscribe to the auth event bus, mirroring the ui/ pattern
    // (`this.subsink.sink = this.pubsub.onAccount.subscribe(...)`).
    this.subs.push(
      this.pubsub.onLoginError.subscribe(msg => { this.error = msg; }));
    this.subs.push(
      this.pubsub.onLoginPending.subscribe(p => { this.pending = p; }));
  }

  ngOnDestroy(): void {
    for (const s of this.subs) s.unsubscribe();
  }

  async onSubmit(): Promise<void> {
    if (!this.user.trim() || !this.password) {
      this.pubsub.emit_loginError('Please enter both your username and password.');
      return;
    }
    this.pubsub.emit_loginError('');
    this.pubsub.emit_loginPending(true);

    try {
      const body = new URLSearchParams();
      body.set('user',     this.user.trim());
      body.set('password', this.password);

      const rsp = await fetch('/api/v1/idp/login', {
        method:      'POST',
        credentials: 'include',          // send + accept xpmile_idp_pending cookie
        headers:     { 'Content-Type': 'application/x-www-form-urlencoded' },
        body:        body.toString(),
      });

      if (rsp.ok) {
        const j: { ok?: boolean; redirect_to?: string } = await rsp.json();
        if (j && j.redirect_to) {
          // Browser walks /authorize → RP redirect_uri from here on.
          window.location.href = j.redirect_to;
          return;
        }
        this.pubsub.emit_loginError('Login succeeded but no redirect was returned.');
      } else {
        // Backend's error shape: {"error":"...", "error_description":"..."}
        let msg = `Sign in failed (HTTP ${rsp.status})`;
        try {
          const j: { error_description?: string; error?: string } = await rsp.json();
          msg = j.error_description || j.error || msg;
        } catch { /* body wasn't JSON; keep generic msg */ }
        this.pubsub.emit_loginError(msg);
      }
    } catch (e) {
      this.pubsub.emit_loginError('Network error — please try again.');
    } finally {
      this.pubsub.emit_loginPending(false);
    }
  }
}
