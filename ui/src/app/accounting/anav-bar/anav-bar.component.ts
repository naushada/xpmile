import { Component, OnInit, Output, EventEmitter } from '@angular/core';

@Component({
  selector: 'app-anav-bar',
  templateUrl: './anav-bar.component.html',
  styleUrls: ['./anav-bar.component.scss']
})
export class AnavBarComponent implements OnInit {

  navItemSelected = '';
  @Output() evt = new EventEmitter<string>();

  ngOnInit(): void {
    this.navItemSelected = 'createAccount';
    this.evt.emit(this.navItemSelected);
  }

  onItemSelect(opt: string): void {
    this.navItemSelected = opt;
    this.evt.emit(this.navItemSelected);
  }
}
