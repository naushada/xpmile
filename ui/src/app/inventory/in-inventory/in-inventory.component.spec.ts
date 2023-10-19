import { ComponentFixture, TestBed } from '@angular/core/testing';

import { InInventoryComponent } from './in-inventory.component';

describe('InInventoryComponent', () => {
  let component: InInventoryComponent;
  let fixture: ComponentFixture<InInventoryComponent>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      declarations: [ InInventoryComponent ]
    })
    .compileComponents();

    fixture = TestBed.createComponent(InInventoryComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
