import { Component, OnDestroy, OnInit } from '@angular/core';

@Component({
  selector: 'app-root',
  templateUrl: './app.component.html',
  styleUrls: ['./app.component.scss']
})
export class AppComponent implements OnInit, OnDestroy {
  title = 'webui';

  constructor() {

  }
  ngOnInit(): void {
    // Routing is handled by the router: the '' route redirects to /main and
    // SsoAuthGuard bounces an unauthenticated visitor to /login.
  }

  ngOnDestroy(): void {
      
  }
}
