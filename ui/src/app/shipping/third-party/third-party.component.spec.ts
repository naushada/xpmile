import { ComponentFixture, TestBed } from '@angular/core/testing';

import { ThirdPartyComponent } from './third-party.component';

describe('ThirdPartyComponent', () => {
  let component: ThirdPartyComponent;
  let fixture: ComponentFixture<ThirdPartyComponent>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      declarations: [ ThirdPartyComponent ]
    })
    .compileComponents();

    fixture = TestBed.createComponent(ThirdPartyComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
