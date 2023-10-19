import { Component, OnInit, Output, EventEmitter } from '@angular/core';

@Component({
  selector: 'app-rnav-bar',
  templateUrl: './rnav-bar.component.html',
  styleUrls: ['./rnav-bar.component.scss']
})
export class RnavBarComponent implements OnInit {

  private selectedItem: string = "";
  @Output() evt = new EventEmitter<string>();

  constructor() { }

  ngOnInit(): void {
    this.navItemSelected = 'detailedReport';
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
