import { ComponentFixture, TestBed } from '@angular/core/testing';

import { FindInventoryComponent } from './find-inventory.component';

describe('FindInventoryComponent', () => {
  let component: FindInventoryComponent;
  let fixture: ComponentFixture<FindInventoryComponent>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      declarations: [ FindInventoryComponent ]
    })
    .compileComponents();

    fixture = TestBed.createComponent(FindInventoryComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
