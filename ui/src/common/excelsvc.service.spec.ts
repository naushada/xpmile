import { TestBed } from '@angular/core/testing';

import { ExcelsvcService } from './excelsvc.service';

describe('ExcelsvcService', () => {
  let service: ExcelsvcService;

  beforeEach(() => {
    TestBed.configureTestingModule({});
    service = TestBed.inject(ExcelsvcService);
  });

  it('should be created', () => {
    expect(service).toBeTruthy();
  });
});
