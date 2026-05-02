import { Component, OnInit } from '@angular/core';
import { Account } from 'src/common/app-globals';
import { HttpsvcService } from 'src/common/httpsvc.service';

@Component({
  selector: 'app-list-account',
  templateUrl: './list-account.component.html',
  styleUrls: ['./list-account.component.scss']
})
export class ListAccountComponent implements OnInit {

  accountInfoList: Account[] = [];

  constructor(private http: HttpsvcService) {}

  ngOnInit(): void {
    this.http.getAccountInfoList().subscribe({
      next: (rsp: Account[]) => { this.accountInfoList = rsp; }
    });
  }

  get totalItems(): number {
    return this.accountInfoList.length;
  }
}
