import { Injectable } from '@angular/core';
import { CanActivate, Router, UrlTree } from '@angular/router';
import { map, Observable, of } from 'rxjs';
import { SessionService } from './session.service';

/**
 * Route guard for authenticated areas of the app.
 *
 * Allows the route when a session is already cached; otherwise it loads the
 * session once (covering a password login that happened without a page
 * reload) and either allows the route or redirects to /login.
 */
@Injectable({ providedIn: 'root' })
export class SsoAuthGuard implements CanActivate {

  constructor(private session: SessionService, private router: Router) {}

  canActivate(): Observable<boolean | UrlTree> {
    if (this.session.isAuthenticated) {
      return of(true);
    }
    return this.session.loadSession().pipe(
      map(s => s ? true : this.router.createUrlTree(['/login']))
    );
  }
}
