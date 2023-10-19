import { ComponentFixture, TestBed } from '@angular/core/testing';

import { CreateDRSComponent } from './create-drs.component';

describe('CreateDRSComponent', () => {
  let component: CreateDRSComponent;
  let fixture: ComponentFixture<CreateDRSComponent>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      declarations: [ CreateDRSComponent ]
    })
    .compileComponents();

    fixture = TestBed.createComponent(CreateDRSComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
