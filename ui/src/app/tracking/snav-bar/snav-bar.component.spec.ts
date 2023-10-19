import { ComponentFixture, TestBed } from '@angular/core/testing';

import { SnavBarComponent } from './snav-bar.component';

describe('SnavBarComponent', () => {
  let component: SnavBarComponent;
  let fixture: ComponentFixture<SnavBarComponent>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      declarations: [ SnavBarComponent ]
    })
    .compileComponents();

    fixture = TestBed.createComponent(SnavBarComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
