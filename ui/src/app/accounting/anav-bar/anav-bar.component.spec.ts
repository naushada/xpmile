import { ComponentFixture, TestBed } from '@angular/core/testing';

import { AnavBarComponent } from './anav-bar.component';

describe('AnavBarComponent', () => {
  let component: AnavBarComponent;
  let fixture: ComponentFixture<AnavBarComponent>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      declarations: [ AnavBarComponent ]
    })
    .compileComponents();

    fixture = TestBed.createComponent(AnavBarComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
