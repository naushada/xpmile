import { ComponentFixture, TestBed } from '@angular/core/testing';

import { MultipleShipmentComponent } from './multiple-shipment.component';

describe('MultipleShipmentComponent', () => {
  let component: MultipleShipmentComponent;
  let fixture: ComponentFixture<MultipleShipmentComponent>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      declarations: [ MultipleShipmentComponent ]
    })
    .compileComponents();

    fixture = TestBed.createComponent(MultipleShipmentComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
