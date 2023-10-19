import { Component, OnInit, Output, EventEmitter } from '@angular/core';

@Component({
  selector: 'app-submenu',
  templateUrl: './submenu.component.html',
  styleUrls: ['./submenu.component.scss']
})
export class SubmenuComponent implements OnInit {

  private selectedItem: string = "";
  @Output() evt = new EventEmitter<string>();

  constructor() { }

  ngOnInit(): void {
    this.navItemSelected = 'singleShipment';
    this.evt.emit(this.navItemSelected);
  }

  
  onItemSelect(opt:string) : void {
    //alert(opt);
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
