import { ComponentFixture, TestBed } from '@angular/core/testing';

import { SingleShipmentDiaglogComponent } from './single-shipment-diaglog.component';

describe('SingleShipmentDiaglogComponent', () => {
  let component: SingleShipmentDiaglogComponent;
  let fixture: ComponentFixture<SingleShipmentDiaglogComponent>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      declarations: [ SingleShipmentDiaglogComponent ]
    })
    .compileComponents();

    fixture = TestBed.createComponent(SingleShipmentDiaglogComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
