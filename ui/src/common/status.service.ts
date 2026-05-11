import { Injectable, OnDestroy } from '@angular/core';
import { HttpClient, HttpErrorResponse } from '@angular/common/http';
import { BehaviorSubject, Subscription, timer, of } from 'rxjs';
import { switchMap, catchError } from 'rxjs/operators';
import { environment } from 'src/environments/environment';
import { UriMap } from './app-globals';

export type ConnState = 'up' | 'down' | 'unknown';

@Injectable({ providedIn: 'root' })
export class StatusService implements OnDestroy {

  // Without a dedicated backend health endpoint we cannot distinguish
  // "agent up, DB down" from "agent down" — a single probe drives both.
  // If the backend later exposes /api/v1/health returning per-component
  // state, update the probe to set agent$/db$ independently.
  readonly agent$ = new BehaviorSubject<ConnState>('unknown');
  readonly db$    = new BehaviorSubject<ConnState>('unknown');
  readonly lastChecked$ = new BehaviorSubject<Date | null>(null);

  private sub?: Subscription;
  private readonly intervalMs = 30_000;
  private readonly probeUrl: string;

  constructor(private http: HttpClient) {
    const base = environment.apiUrl ?? '';
    this.probeUrl = base + (UriMap.get('from_web_config') ?? '/api/v1/config');
    this.start();
  }

  private start(): void {
    this.sub = timer(0, this.intervalMs).pipe(
      switchMap(() =>
        this.http
          .get(this.probeUrl, { observe: 'response', responseType: 'text' })
          .pipe(
            // Any HTTP status that reached the backend (2xx/3xx/4xx) means
            // backend → agent → DB are responding. Only network errors and
            // 5xx (status === 0 in some browsers) count as "down".
            catchError((err: HttpErrorResponse) =>
              of({ status: err.status ?? 0 })
            )
          )
      )
    ).subscribe(rsp => {
      const status = (rsp as { status?: number }).status ?? 0;
      const ok = status >= 200 && status < 500;
      const state: ConnState = ok ? 'up' : 'down';
      this.agent$.next(state);
      this.db$.next(state);
      this.lastChecked$.next(new Date());
    });
  }

  ngOnDestroy(): void {
    this.sub?.unsubscribe();
  }
}
