import { ComponentFixture, TestBed } from '@angular/core/testing';

import { SingleShipmentComponent } from './single-shipment.component';

describe('SingleShipmentComponent', () => {
  let component: SingleShipmentComponent;
  let fixture: ComponentFixture<SingleShipmentComponent>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      declarations: [ SingleShipmentComponent ]
    })
    .compileComponents();

    fixture = TestBed.createComponent(SingleShipmentComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
