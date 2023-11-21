import { ComponentFixture, TestBed } from '@angular/core/testing';

import { CollectShipmentComponent } from './collect-shipment.component';

describe('CollectShipmentComponent', () => {
  let component: CollectShipmentComponent;
  let fixture: ComponentFixture<CollectShipmentComponent>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      declarations: [ CollectShipmentComponent ]
    })
    .compileComponents();

    fixture = TestBed.createComponent(CollectShipmentComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
