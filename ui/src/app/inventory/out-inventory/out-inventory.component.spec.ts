import { ComponentFixture, TestBed } from '@angular/core/testing';

import { OutInventoryComponent } from './out-inventory.component';

describe('OutInventoryComponent', () => {
  let component: OutInventoryComponent;
  let fixture: ComponentFixture<OutInventoryComponent>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      declarations: [ OutInventoryComponent ]
    })
    .compileComponents();

    fixture = TestBed.createComponent(OutInventoryComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
