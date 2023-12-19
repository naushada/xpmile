import { Component, OnInit } from '@angular/core';
import { FormBuilder, FormGroup } from '@angular/forms';

@Component({
  selector: 'app-dashboard',
  templateUrl: './dashboard.component.html',
  styleUrls: ['./dashboard.component.scss']
})
export class DashboardComponent implements OnInit {

  isRefreshTriggered:boolean = false;
  dashboardForm: FormGroup;
  constructor(private fb:FormBuilder) {
    this.dashboardForm = fb.group({
      isRefreshTriggered: this.isRefreshTriggered
    })
   }

  ngOnInit(): void {
  }

  onRefresh(evt:any) {
    alert(evt.currentTarget.checked);

    console.log(evt.currentTarget.checked);
  }
}
