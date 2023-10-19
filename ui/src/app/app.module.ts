import { LOCALE_ID, NgModule } from '@angular/core';
import { BrowserModule } from '@angular/platform-browser';
import { HttpClientModule } from '@angular/common/http';

import { BrowserAnimationsModule } from "@angular/platform-browser/animations";
import { ClarityModule} from "@clr/angular";

import { CdsModule } from '@cds/angular';

import { AppRoutingModule } from './app-routing.module';
import { AppComponent } from './app.component';
import { MainComponent } from './main/main.component';
import { LoginComponent } from './login/login.component';
import { FormsModule, ReactiveFormsModule } from "@angular/forms";
import { SubmenuComponent } from './shipping/submenu/submenu.component';
import { SingleComponent } from './shipping/single/single.component';
import { BulkComponent } from './shipping/bulk/bulk.component';
import { ThirdPartyComponent } from './shipping/third-party/third-party.component';
import { ModifyComponent } from './shipping/modify/modify.component';
import { ListComponent } from './shipping/list/list.component';
import { ApiIntegrationComponent } from './shipping/api-integration/api-integration.component';
import { SnavBarComponent } from './tracking/snav-bar/snav-bar.component';
import { SingleShipmentComponent } from './tracking/single-shipment/single-shipment.component';
import { MultipleShipmentComponent } from './tracking/multiple-shipment/multiple-shipment.component';
import { UpdateShipmentComponent } from './tracking/update-shipment/update-shipment.component';
import { RnavBarComponent } from './reporting/rnav-bar/rnav-bar.component';
import { DetailedReportComponent } from './reporting/detailed-report/detailed-report.component';
import { InvoiceComponent } from './reporting/invoice/invoice.component';
import { AnavBarComponent } from './accounting/anav-bar/anav-bar.component';
import { CreateAccountComponent } from './accounting/create-account/create-account.component';
import { UpdateAccountComponent } from './accounting/update-account/update-account.component';
import { ListAccountComponent } from './accounting/list-account/list-account.component';
import { InInventoryComponent } from './inventory/in-inventory/in-inventory.component';
import { OutInventoryComponent } from './inventory/out-inventory/out-inventory.component';
import { UpdateInventoryComponent } from './inventory/update-inventory/update-inventory.component';
import { CreateManifestComponent } from './inventory/create-manifest/create-manifest.component';
import { FindInventoryComponent } from './inventory/find-inventory/find-inventory.component';
import { InavBarComponent } from './inventory/inav-bar/inav-bar.component';
import { InventoryReportComponent } from './inventory-report/inventory-report.component';
import { DashboardComponent } from './dashboard/dashboard.component';
import { EmailComponent } from './tracking/email/email.component';
import { SingleShipmentDiaglogComponent } from './tracking/single-shipment-diaglog/single-shipment-diaglog.component';
import { CreateDRSComponent } from './shipping/create-drs/create-drs.component';
import { PasswordResetComponent } from './login/password-reset/password-reset.component';


@NgModule({
  declarations: [
    AppComponent,
    MainComponent,
    LoginComponent,
    SubmenuComponent,
    SingleComponent,
    BulkComponent,
    ThirdPartyComponent,
    ModifyComponent,
    ListComponent,
    ApiIntegrationComponent,
    SnavBarComponent,
    SingleShipmentComponent,
    MultipleShipmentComponent,
    UpdateShipmentComponent,
    RnavBarComponent,
    DetailedReportComponent,
    InvoiceComponent,
    AnavBarComponent,
    CreateAccountComponent,
    UpdateAccountComponent,
    ListAccountComponent,
    InInventoryComponent,
    OutInventoryComponent,
    UpdateInventoryComponent,
    CreateManifestComponent,
    FindInventoryComponent,
    InavBarComponent,
    InventoryReportComponent,
    DashboardComponent,
    EmailComponent,
    SingleShipmentDiaglogComponent,
    CreateDRSComponent,
    PasswordResetComponent,
    
  ],
  imports: [
    BrowserModule,
    AppRoutingModule,
    BrowserAnimationsModule,
    ClarityModule,
    FormsModule,
    ReactiveFormsModule,
    CdsModule,
    HttpClientModule
  ],
  providers: [ { provide: LOCALE_ID, useValue: 'en-GB' }],
  bootstrap: [AppComponent]
})
export class AppModule { }
