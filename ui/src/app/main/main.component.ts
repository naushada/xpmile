import { Component, Input, OnDestroy, OnInit } from '@angular/core';
import { Account } from 'src/common/app-globals';
import { PubsubsvcService } from 'src/common/pubsubsvc.service';
import { SubSink } from 'subsink';

@Component({
  selector: 'app-main',
  templateUrl: './main.component.html',
  styleUrls: ['./main.component.scss']
})
export class MainComponent implements OnInit, OnDestroy {

  @Input() selectedNavItem: string = "";
  private selectedItem: string = "";

  loggedInUser?: Account;
  subsink = new SubSink();

  constructor(private pubsub: PubsubsvcService) {
    this.subsink.sink = this.pubsub.onAccount.subscribe(
      rsp => {
       // let acc: Account = rsp as Account;
        this.loggedInUser = {...rsp as Account};
      },
      (error: any) => {},
      () => {});
    
   }

  ngOnInit(): void {
    this.onMenuSelect('shipping');
    this.onReceiveEvt('singleShipment');
  }

  ngOnDestroy(): void {
      this.subsink.unsubscribe();
  }

  public onMenuSelect(opt: string) {
    this.selectedMenuItem = opt;
  }

  public get selectedMenuItem(): string {
    return(this.selectedItem);
  }

  public set selectedMenuItem(item:string) {
    this.selectedItem = item;
  }

  public onReceiveEvt(navItem:any):void {
    this.selectedNavItem = navItem;
  }
}
