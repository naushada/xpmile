import { Component, OnInit, Output, EventEmitter } from '@angular/core';

@Component({
  selector: 'app-snav-bar',
  templateUrl: './snav-bar.component.html',
  styleUrls: ['./snav-bar.component.scss']
})
export class SnavBarComponent implements OnInit {


  private selectedItem: string = "";
  @Output() evt = new EventEmitter<string>();

  constructor() { }

  ngOnInit(): void {
    this.navItemSelected = 'singleShipmentStatus';
    this.evt.emit(this.navItemSelected);
  }

  
  onItemSelect(opt:string) : void {
    this.navItemSelected = opt;
    this.evt.emit(this.navItemSelected);
  }

  get navItemSelected(): string {
    return(this.selectedItem);
  }

  set navItemSelected(opt:string) {
    this.selectedItem = opt;
  }

}
