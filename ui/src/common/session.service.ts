import { Injectable } from '@angular/core';
import { catchError, firstValueFrom, Observable, of, tap } from 'rxjs';
import { SsoSession } from './app-globals';
import { HttpsvcService } from './httpsvc.service';

/**
 * Holds the current session for the SPA.
 *
 * The session itself lives server-side (an opaque cookie, see
 * docs/design/sso/sso-design.md §2); this service caches only what
 * GET /api/v1/sso/session returns, so the route guard can answer without a
 * round trip once the session has been loaded.
 */
@Injectable({ providedIn: 'root' })
export class SessionService {

  private session: SsoSession | null = null;

  constructor(private http: HttpsvcService) {}

  /** True once a session has been loaded for this browser session. */
  get isAuthenticated(): boolean {
    return this.session !== null;
  }

  /** The current session, or null when unauthenticated. */
  get current(): SsoSession | null {
    return this.session;
  }

  /** Forget the cached session — used by logout and the 401 interceptor. */
  clear(): void {
    this.session = null;
  }

  /**
   * Fetch the session from the backend and cache it. A 401 (no session) is
   * not an error here — it resolves to null so callers never have to catch.
   */
  loadSession(): Observable<SsoSession | null> {
    return this.http.getSession().pipe(
      tap(s => { this.session = s; }),
      catchError(() => { this.session = null; return of(null); })
    );
  }
}

/**
 * APP_INITIALIZER factory — loads the session once, before the app bootstraps,
 * so a reload of an authenticated page is not bounced to /login.
 */
export function initSession(session: SessionService): () => Promise<unknown> {
  return () => firstValueFrom(session.loadSession());
}
