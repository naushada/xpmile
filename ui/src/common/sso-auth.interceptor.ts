import { Injectable, Injector } from '@angular/core';
import {
  HttpErrorResponse, HttpEvent, HttpHandler, HttpInterceptor, HttpRequest
} from '@angular/common/http';
import { Router } from '@angular/router';
import { catchError, Observable, throwError } from 'rxjs';
import { SessionService } from './session.service';

/**
 * Two jobs on every outbound request:
 *  - sends the session cookie (`withCredentials`), required for the
 *    cross-origin dev proxy and for any credentialed CORS call;
 *  - on a 401 from a protected API call, clears the cached session and
 *    bounces to /login.
 *
 * A 401 from the SSO probe endpoints or from a password-login attempt is
 * expected (it just means "not signed in") and must not trigger the bounce.
 *
 * SessionService and Router are resolved lazily through the Injector so the
 * interceptor itself carries no dependency on HttpClient at construction —
 * that would be a circular DI (HttpClient -> interceptor -> SessionService
 * -> HttpsvcService -> HttpClient).
 */
@Injectable()
export class SsoAuthInterceptor implements HttpInterceptor {

  constructor(private injector: Injector) {}

  intercept(req: HttpRequest<unknown>,
            next: HttpHandler): Observable<HttpEvent<unknown>> {
    const authReq = req.clone({ withCredentials: true });

    return next.handle(authReq).pipe(
      catchError((err: HttpErrorResponse) => {
        if (err.status === 401 && this.shouldBounce(authReq.url)) {
          this.injector.get(SessionService).clear();
          this.injector.get(Router).navigateByUrl('/login');
        }
        return throwError(() => err);
      })
    );
  }

  private shouldBounce(url: string): boolean {
    return url.includes('/api/v1/') &&
           !url.includes('/sso/') &&            // session probe / logout
           !url.includes('/account/login');     // password-login attempt
  }
}
