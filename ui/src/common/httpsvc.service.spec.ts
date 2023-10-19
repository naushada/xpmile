import { TestBed } from '@angular/core/testing';

import { HttpsvcService } from './httpsvc.service';

describe('HttpsvcService', () => {
  let service: HttpsvcService;

  beforeEach(() => {
    TestBed.configureTestingModule({});
    service = TestBed.inject(HttpsvcService);
  });

  it('should be created', () => {
    expect(service).toBeTruthy();
  });
});
