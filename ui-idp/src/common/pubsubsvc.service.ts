import { Injectable } from '@angular/core';
import { BehaviorSubject } from 'rxjs';

/**
 * Auth-event bus for the IdP SPA. Mirrors the pattern used in
 * ui/src/common/pubsubsvc.service.ts — `providedIn: 'root'` singleton,
 * BehaviorSubjects exposed as observables, `emit_*` helpers that
 * publish + remember the current value.
 *
 * v1 surface (login-only Phase F):
 *   - onLoginPending — boolean, true while a /login POST is in flight
 *   - onLoginError   — string, last error message (empty = no error)
 *
 * Add new Subjects here as the SPA grows (e.g. onResetRequested for
 * Phase F password-reset, onLogout for /end_session-from-SPA, etc).
 */
@Injectable({ providedIn: 'root' })
export class PubsubsvcService {

  private loginPendingBs$ = new BehaviorSubject<boolean>(false);
  private loginErrorBs$   = new BehaviorSubject<string>('');

  /** Subscribe to the in-flight indicator (toggle spinners + disable submit). */
  public onLoginPending = this.loginPendingBs$.asObservable();
  /** Subscribe to the last login error message — empty string means none. */
  public onLoginError   = this.loginErrorBs$.asObservable();

  /** Publish a login-in-progress state change. */
  public emit_loginPending(v: boolean): void { this.loginPendingBs$.next(v); }
  /** Publish a login error (empty string to clear). */
  public emit_loginError(msg: string): void { this.loginErrorBs$.next(msg); }

  // ── Password reset (Phase F slice 2) ────────────────────────────────────

  private resetPendingBs$ = new BehaviorSubject<boolean>(false);
  private resetErrorBs$   = new BehaviorSubject<string>('');
  private resetNoticeBs$  = new BehaviorSubject<string>('');

  /** Subscribe to the in-flight indicator for the request OR confirm POST. */
  public onResetPending = this.resetPendingBs$.asObservable();
  /** Subscribe to the last reset-flow error — empty string means none. */
  public onResetError   = this.resetErrorBs$.asObservable();
  /** Subscribe to non-error notice messages (e.g. "if the address
   *  exists we just sent a link") that the form should render in the
   *  success spot rather than the error spot. */
  public onResetNotice  = this.resetNoticeBs$.asObservable();

  public emit_resetPending(v: boolean): void { this.resetPendingBs$.next(v); }
  public emit_resetError(msg: string): void  { this.resetErrorBs$.next(msg); }
  public emit_resetNotice(msg: string): void { this.resetNoticeBs$.next(msg); }
}
