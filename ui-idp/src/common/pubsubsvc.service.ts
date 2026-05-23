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
}
