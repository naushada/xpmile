import { ComponentFixture, TestBed } from '@angular/core/testing';

import { InavBarComponent } from './inav-bar.component';

describe('InavBarComponent', () => {
  let component: InavBarComponent;
  let fixture: ComponentFixture<InavBarComponent>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      declarations: [ InavBarComponent ]
    })
    .compileComponents();

    fixture = TestBed.createComponent(InavBarComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
