import { Component, OnInit } from '@angular/core';

@Component({
  selector: 'app-single-shipment-diaglog',
  templateUrl: './single-shipment-diaglog.component.html',
  styleUrls: ['./single-shipment-diaglog.component.scss']
})
export class SingleShipmentDiaglogComponent implements OnInit {

  isOpened: boolean = true;
  constructor() { }

  ngOnInit(): void {
  }

  onClick() {
    if(this.isOpened == false) {
      this.isOpened = true;
      return(this.isOpened);
    }
    this.isOpened = false;
    return(this.isOpened);
  }
}
