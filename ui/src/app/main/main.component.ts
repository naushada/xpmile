import { Component, Input, OnDestroy, OnInit } from '@angular/core';
import { formatDate } from '@angular/common';
import { Router } from '@angular/router';
import { Account } from 'src/common/app-globals';
import { HttpsvcService } from 'src/common/httpsvc.service';
import { PubsubsvcService } from 'src/common/pubsubsvc.service';
import { SessionService } from 'src/common/session.service';
import { ShipmentStatsService } from 'src/common/shipment-stats.service';
import { SubSink } from 'subsink';

@Component({
  selector: 'app-main',
  templateUrl: './main.component.html',
  styleUrls: ['./main.component.scss']
})
export class MainComponent implements OnInit, OnDestroy {

  @Input() selectedNavItem: string = "";
  private selectedItem: string = "";

  loggedInUser?: Account;
  today = this.formatToday();
  flashOn = false;

  /** Bound to the About modal's `clrModalOpen` two-way binding. */
  aboutOpen = false;
  /** Read-only host string (e.g. "marvel-xxxx.herokuapp.com") shown in About. */
  currentHost = window?.location?.host || '';

  /** ── My Profile modal state ─────────────────────────────────────
   *  Self-service edit of the logged-in user's name + photo without
   *  having to navigate to Accounting → Update Account → look up self.
   *  profilePhotoBase64 / profileName are working copies that don't
   *  touch loggedInUser until the operator clicks Save.                  */
  profileOpen      = false;
  profilePhotoBase64 = '';
  profileName        = '';
  profileSaving      = false;
  profileError       = '';

  subsink = new SubSink();

  constructor(private pubsub: PubsubsvcService, public stats: ShipmentStatsService,
              private http: HttpsvcService, private session: SessionService,
              private router: Router) {
    this.subsink.sink = this.pubsub.onAccount.subscribe(
      rsp => { this.loggedInUser = { ...(rsp as Account) }; }
    );

    this.subsink.sink = this.stats.stats$.subscribe(() => {
      this.today = this.formatToday();
      this.flashOn = true;
      setTimeout(() => { this.flashOn = false; }, 700);
    });
  }

  private formatToday(): string {
    return formatDate(new Date(), 'EEE, d MMM', 'en-US');
  }

  ngOnInit(): void {
    this.onMenuSelect('shipping');
    this.onReceiveEvt('singleShipment');

    // ── Session restore on page refresh ─────────────────────────────
    // loggedInUser is normally set via pubsub.onAccount, which fires
    // only at login time (LoginComponent emits after a successful
    // POST /api/v1/account/login). On a browser refresh Angular
    // bootstraps fresh, pubsub has no buffered emission, and
    // loggedInUser stays undefined → the navbar renders an empty
    // <span> next to the user icon.
    //
    // The session cookie itself is still valid (HttpOnly, browser
    // kept it across refresh), so we re-hydrate by calling
    // /sso/session → accountCode → getCustomerInfo(accountCode).
    // If the session has expired or never existed, /sso/session returns
    // 401 and we kick back to /login.
    //
    // IMPORTANT: use getCustomerInfo, NOT getAccountInfo. Despite the
    // name, getAccountInfo() is actually the LOGIN endpoint
    // (`POST /api/v1/account/login` with `{userId, password}`) — calling
    // it without a password returns 400 "Missing userId or password",
    // and we'd never re-hydrate loggedInUser. getCustomerInfo is the
    // session-authenticated GET that returns the same Account shape.
    if (!this.loggedInUser?.loginCredentials?.accountCode) {
      this.http.getSession().subscribe({
        next: (sess) => {
          if (sess?.accountCode) {
            this.http.getCustomerInfo(sess.accountCode).subscribe({
              next: (acct) => {
                this.loggedInUser = acct;
                // Re-publish so any other subscribers (sidebar widgets, etc.)
                // see the restored user the same way they would on a fresh login.
                this.pubsub.emit_accountInfo(acct);
              },
              error: (err) => {
                // Make the failure VISIBLE in DevTools — the previous silent
                // handler is exactly what masked the getAccountInfo bug for so
                // long. Operator-facing UX is unchanged (still non-fatal — just
                // an empty navbar — but at least we leave a breadcrumb).
                console.warn('[session-restore] getCustomerInfo failed; navbar will render fallback', err);
              }
            });
          } else {
            this.router.navigateByUrl('/login');
          }
        },
        error: () => { this.router.navigateByUrl('/login'); }
      });
    }
  }

  onLogout(): void {
    // Revoke the session server-side, then clear local state and return to
    // the login screen — whether or not the revoke call itself succeeds.
    this.http.ssoLogout().subscribe({
      next: () => this.afterLogout(),
      error: () => this.afterLogout()
    });
  }

  private afterLogout(): void {
    this.session.clear();
    this.router.navigateByUrl('/login');
  }

  ngOnDestroy(): void {
    this.subsink.unsubscribe();
  }

  public onMenuSelect(opt: string) {
    this.selectedMenuItem = opt;
  }

  public get selectedMenuItem(): string {
    return this.selectedItem;
  }

  public set selectedMenuItem(item: string) {
    this.selectedItem = item;
  }

  public onReceiveEvt(navItem: any): void {
    this.selectedNavItem = navItem;
  }

  // ── My Profile modal — self-service photo + display-name edit ───────
  openProfile(): void {
    // Seed working copies from the current loggedInUser so the modal opens
    // pre-filled. Editing in the modal is local until Save.
    this.profilePhotoBase64 = this.loggedInUser?.personalInfo?.photoBase64 || '';
    this.profileName        = this.loggedInUser?.personalInfo?.name        || '';
    this.profileError       = '';
    this.profileOpen        = true;
  }

  /**
   * File input handler. Same resize pipeline as
   * update-account.component.ts → canvas 256×256 → JPEG @ 0.85.
   * Keeps the upload payload at ~30-50 KB regardless of input.
   */
  onProfilePhotoSelected(event: Event): void {
    const input = event.target as HTMLInputElement;
    const file = input.files?.[0];
    if (!file) return;
    if (!file.type.startsWith('image/')) {
      this.profileError = 'Please pick an image file (JPEG, PNG, etc.).';
      input.value = '';
      return;
    }
    const MAX_BYTES_RAW = 5 * 1024 * 1024;
    if (file.size > MAX_BYTES_RAW) {
      this.profileError = `Image too big (${Math.round(file.size / 1024 / 1024)} MB). Max ${MAX_BYTES_RAW / 1024 / 1024} MB before client-side resize.`;
      input.value = '';
      return;
    }
    const reader = new FileReader();
    reader.onload = () => {
      const img = new Image();
      img.onload = () => {
        const MAX_DIM = 256;
        const scale = Math.min(1, MAX_DIM / Math.max(img.width, img.height));
        const w = Math.round(img.width  * scale);
        const h = Math.round(img.height * scale);
        const canvas = document.createElement('canvas');
        canvas.width  = w;
        canvas.height = h;
        const ctx = canvas.getContext('2d');
        if (!ctx) { this.profileError = 'Browser canvas resize unavailable.'; return; }
        ctx.drawImage(img, 0, 0, w, h);
        this.profilePhotoBase64 = canvas.toDataURL('image/jpeg', 0.85);
        this.profileError = '';
      };
      img.onerror = () => { this.profileError = 'Could not decode that image file.'; };
      img.src = reader.result as string;
    };
    reader.onerror = () => { this.profileError = 'Could not read the file.'; };
    reader.readAsDataURL(file);
  }

  /**
   * Save the working copies back to the logged-in user's account doc
   * via the existing update-account endpoint. On success, patch
   * loggedInUser locally + re-publish via pubsub so the navbar
   * picks up the new photo/name immediately (no page reload).
   */
  saveProfile(): void {
    const accCode = this.loggedInUser?.loginCredentials?.accountCode;
    if (!accCode) {
      this.profileError = 'No logged-in account; please sign in again.';
      return;
    }
    if (!this.profileName?.trim()) {
      this.profileError = 'Display name cannot be empty.';
      return;
    }
    // Build a patched account doc — preserve everything else.
    const patched: Account = {
      ...(this.loggedInUser as Account),
      personalInfo: {
        ...this.loggedInUser!.personalInfo,
        name:         this.profileName.trim(),
        photoBase64:  this.profilePhotoBase64
      }
    };
    this.profileSaving = true;
    this.profileError  = '';
    this.http.updateAccountInfo(accCode, patched).subscribe({
      next: () => {
        this.loggedInUser = patched;
        this.pubsub.emit_accountInfo(patched);
        this.profileSaving = false;
        this.profileOpen   = false;
      },
      error: () => {
        this.profileSaving = false;
        this.profileError  = 'Save failed — please try again.';
      }
    });
  }
}
