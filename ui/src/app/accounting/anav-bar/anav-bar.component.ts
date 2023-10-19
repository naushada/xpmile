import { Component, OnInit, Output, EventEmitter } from '@angular/core';

@Component({
  selector: 'app-anav-bar',
  templateUrl: './anav-bar.component.html',
  styleUrls: ['./anav-bar.component.scss']
})
export class AnavBarComponent implements OnInit {

  private selectedItem: string = "";
  @Output() evt = new EventEmitter<string>();

  constructor() { }

  ngOnInit(): void {
    this.navItemSelected = 'createAccount';
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
