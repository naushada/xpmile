import { ComponentFixture, TestBed } from '@angular/core/testing';

import { DetailedReportComponent } from './detailed-report.component';

describe('DetailedReportComponent', () => {
  let component: DetailedReportComponent;
  let fixture: ComponentFixture<DetailedReportComponent>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      declarations: [ DetailedReportComponent ]
    })
    .compileComponents();

    fixture = TestBed.createComponent(DetailedReportComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
