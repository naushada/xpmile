import { ComponentFixture, TestBed } from '@angular/core/testing';

import { AltrefBulkComponent } from './altref-bulk.component';

describe('AltrefBulkComponent', () => {
  let component: AltrefBulkComponent;
  let fixture: ComponentFixture<AltrefBulkComponent>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      declarations: [ AltrefBulkComponent ]
    })
    .compileComponents();

    fixture = TestBed.createComponent(AltrefBulkComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
