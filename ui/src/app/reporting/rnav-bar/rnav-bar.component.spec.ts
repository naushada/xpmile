import { ComponentFixture, TestBed } from '@angular/core/testing';

import { RnavBarComponent } from './rnav-bar.component';

describe('RnavBarComponent', () => {
  let component: RnavBarComponent;
  let fixture: ComponentFixture<RnavBarComponent>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      declarations: [ RnavBarComponent ]
    })
    .compileComponents();

    fixture = TestBed.createComponent(RnavBarComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
