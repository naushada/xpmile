import { Component, OnInit, Output, EventEmitter } from '@angular/core';

@Component({
  selector: 'app-inav-bar',
  templateUrl: './inav-bar.component.html',
  styleUrls: ['./inav-bar.component.scss']
})
export class InavBarComponent implements OnInit {

  private selectedItem: string = "";
  @Output() evt = new EventEmitter<string>();

  constructor() { }

  ngOnInit(): void {
    this.navItemSelected = 'createManifest';
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
