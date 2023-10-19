import { TestBed } from '@angular/core/testing';

import { PubsubsvcService } from './pubsubsvc.service';

describe('PubsubsvcService', () => {
  let service: PubsubsvcService;

  beforeEach(() => {
    TestBed.configureTestingModule({});
    service = TestBed.inject(PubsubsvcService);
  });

  it('should be created', () => {
    expect(service).toBeTruthy();
  });
});
