import { Component, OnInit } from '@angular/core';

@Component({
  selector: 'app-third-party',
  templateUrl: './third-party.component.html',
  styleUrls: ['./third-party.component.scss']
})
export class ThirdPartyComponent implements OnInit {

  selectedVendor = '';
  selectedFileName = '';

  constructor() { }

  ngOnInit(): void { }

  onFileSelect(event: any) {
    this.selectedFileName = event.target.files[0]?.name ?? '';
  }
}
