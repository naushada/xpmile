import { Component } from '@angular/core';
import { StatusService } from 'src/common/status.service';

@Component({
  selector: 'app-status-badge',
  templateUrl: './status-badge.component.html',
  styleUrls: ['./status-badge.component.scss']
})
export class StatusBadgeComponent {
  constructor(public status: StatusService) {}
}
