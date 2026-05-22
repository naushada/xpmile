import { Injectable } from '@angular/core';
import { HttpClient, HttpParams, HttpHeaders, HttpErrorResponse} from '@angular/common/http';
import { catchError, forkJoin, Observable, switchMap } from 'rxjs';
import { Shipment, Account, ShipmentStatus, Inventory, UriMap, Email, SenderInformation, activityOnShipment, SsoProvider, SsoSession } from './app-globals';
import { environment } from 'src/environments/environment';


@Injectable({
  providedIn: 'root'
})
export class HttpsvcService {

  private apiURL: string = environment.apiUrl;
  constructor(private http: HttpClient) {}
  

  httpOptions = {
    headers: new HttpHeaders({
      'Content-Type': 'application/json'
    })
  } 
  
  getUri(key:string) : string {

    let uri:string;

    if(this.apiURL.length > 0) {
      uri = this.apiURL + UriMap.get(key);
      return(uri);
    }
      uri = UriMap.get(key) as string;
      return(uri);
  }

  /** CREATE Section */
  /**
   * @brief This function retrieves the waybill details based on waybill number.
   * @param awb 
   * @param accountCode 
   * @returns 
   */
  getShipmentByAwbNo(awb: string, accountCode?: string): Observable<Shipment> {
    let param = `awbNo=${awb}`;

    if(accountCode != undefined) {
      param += `&accountCode=${accountCode}`
    }

    const options = {params: new HttpParams({fromString: param})};
    return this.http.get<Shipment>(this.getUri("from_web_shipment"), options);
  }

  /**
   * 
   * @param altRefNo 
   * @param accountCode 
   * @returns 
   */
  getShipmentByAltRefNo(altRefNo: string, accountCode?: string): Observable<Shipment> {
    let param = `altRefNo=${altRefNo}`;

    if(accountCode != undefined) {
      param += `&accountCode=${accountCode}`
    }
    
    const options = {params: new HttpParams({fromString: param})};

    return this.http.get<Shipment>(this.getUri("from_web_shipment"), options);
    
  }

  /**
   * 
   * @param senderRefNo 
   * @param accountCode 
   * @returns 
   */
  getShipmentBySenderRefNo(senderRefNo: string, accountCode?: string): Observable<Shipment> {
    let param = `senderRefNo=${senderRefNo}`;

    if(accountCode != undefined) {
      param += `&accountCode=${accountCode}`
    }
    
    const options = {params: new HttpParams({fromString: param})};
    return this.http.get<Shipment>(this.getUri("from_web_shipment"), options)
  }

  /**
   * 
   * @param fromDate 
   * @param toDate 
   * @param country 
   * @param accountCode 
   * @returns 
   */
  getShipments(fromDate:Date, toDate:Date, country?:string, accountCode?: Array<string>): Observable<Shipment[]> {
    let param = `fromDate=${fromDate}&toDate=${toDate}`;

    if(accountCode != undefined) {
      param += `&accountCode=${accountCode}`
    }

    if(country != undefined) {
      param += `&country=${country}`
    }

    const options = {params: new HttpParams({fromString: param})};
    return this.http.get<Shipment[]>(this.getUri("from_web_shipment"), options);
  }


  /**
   * 
   * @param fromDate 
   * @param toDate 
   * @param country 
   * @param accountCode 
   * @returns 
   */
  getShipmentsList(fromDate:string, toDate:string, accountCode?: string): Observable<Shipment[]> {
    let param = `fromDate=${fromDate}&toDate=${toDate}`;

    if(accountCode != undefined) {
      param += `&accountCode=${accountCode}`
    }

    const options = {params: new HttpParams({fromString: param})};
    return this.http.get<Shipment[]>(this.getUri("from_web_shipment"), options)
  }

  getShipmentsForAccount(accountCode: string): Observable<Shipment[]> {
    const today = new Date().toISOString().split('T')[0];
    const param = `fromDate=2020-01-01&toDate=${today}&accountCode=${accountCode}`;
    const options = {params: new HttpParams({fromString: param})};
    return this.http.get<Shipment[]>(this.getUri("from_web_shipment"), options);
  }

  /**
   * 
   * @param awb 
   * @param accountCode 
   * @returns 
   */
  getShipmentsByAwbNo(awb: Array<string>, accountCode?: string): Observable<Shipment[]> {
    let param = `awbNo=${awb}`;

    if(accountCode != undefined) {
      param += `&accountCode=${accountCode}`
    }

    const options = {params: new HttpParams({fromString: param})};
    return this.http.get<Shipment[]>(this.getUri("from_web_shipment"), options)
  }

  /**
   * 
   * @param altRefNo 
   * @param accountCode 
   * @returns 
   */
  getShipmentsByAltRefNo(altRefNo: Array<string>, accountCode?: string): Observable<Shipment[]> {
    let param = `awbNo=${altRefNo}`;

    if(accountCode != undefined) {
      param += `&accountCode=${accountCode}`
    }
    
    const options = {params: new HttpParams({fromString: param})};
    return this.http.get<Shipment[]>(this.getUri("from_web_shipment"), options)
  }

  /**
   * 
   * @param senderRefNo 
   * @param accountCode 
   * @returns 
   */
  getShipmentsBySenderRefNo(senderRefNo: Array<string>, accountCode?: string): Observable<Shipment[]> {
    let param = `awbNo=${senderRefNo}`;

    if(accountCode != undefined) {
      param += `&accountCode=${accountCode}`
    }
    
    const options = {params: new HttpParams({fromString: param})};
    return this.http.get<Shipment[]>(this.getUri("from_web_shipment"), options)
  }

  /**
   * 
   * @param id 
   * @param pwd 
   * @returns 
   */
  getAccountInfo(id:string, pwd?: string): Observable<Account> {
    const body: any = { userId: id };
    if (pwd && pwd.length > 0) {
      body.password = pwd;
    }
    return this.http.post<Account>(this.getUri("from_web_login"), body);
  }

  /**
   * 
   * @param id 
   * @param pwd 
   * @returns 
   */
    // Two-step reset: verify current password via POST /login (body, not URL),
    // then PUT the new password — backend hashes via PBKDF2 before storing.
    // Neither call places a password into the request URL, so passwords no
    // longer leak into Heroku router logs, browser history, or Referer headers.
    resetAccountPassword(id: string, currentPwd: string, newPassword: string): Observable<Account> {
      return this.http.post<Account>(
        this.getUri("from_web_login"),
        { userId: id, password: currentPwd }
      ).pipe(
        switchMap(() => {
          const params = new HttpParams().set('userId', id);
          const body = { loginCredentials: { accountPassword: newPassword } };
          return this.http.put<Account>(this.getUri("from_web_account"), body, { params });
        })
      );
    }
  
  /**
   * 
   * @returns 
   */
  getAccountInfoList(): Observable<Account[]> {
    
     const options = {
      headers: new HttpHeaders({
        'Content-Type': 'application/json',
        'Content-Length': '0'
      })
     } 

    return this.http.get<Account[]>(this.getUri("from_web_account"), options);
  }

  /**
   * 
   * @param accountCode 
   * @returns 
   */
  getCustomerInfo(accountCode: string): Observable<Account> {
    let param = `accountCode=${accountCode}`;

    const options = {params: new HttpParams({fromString: param})};
    return this.http.get<Account>(this.getUri("from_web_account"), options);
  }


  getFromInventory(sku: string, acc?: string): Observable<Inventory[]> {
    let param = ``;

    if(sku.length > 0 && acc && acc.length > 0) {
      param = `sku=${sku}&accountCode=${acc}`;
    } else if(acc && acc.length > 0) {
      param = `accountCode=${acc}`;
    } else {
      param = `sku=${sku}`;
    }

    const options = {params: new HttpParams({fromString: param})};

    return this.http.get<Inventory[]>(this.getUri("from_web_inventory"), options);
  }

  /** UPDATE Section */

  updateInventory(sku:string, qty:number, acc?: string, isUpdate?: string): Observable<any> {
    let param = `sku=${sku}&qty=${qty}`;
    
    if(acc && acc.length) {
      param += `&accountCode=${acc}`;
    }
    if(isUpdate && isUpdate.length) {
      param += `&isUpdate=${isUpdate}`;
    }

    const options = {
                     params: new HttpParams({fromString: param}),
                     headers: new HttpHeaders({
                              'Content-Type': 'application/json'
                      })
                    };
    return this.http.put<any>(this.getUri("from_web_inventory"), JSON.stringify({}), options);
  }


  updateAccountInfo(id:string, accInfo:Account): Observable<any> {
    let param = `userId=${id}`;
    
    const options = {
                     params: new HttpParams({fromString: param}),
                     headers: new HttpHeaders({
                              'Content-Type': 'application/json'
                      })
                    };
    return this.http.put<Account>(this.getUri("from_web_account"), JSON.stringify(accInfo), options);
  }

  /**
   * 
   * @param awbNo 
   * @param data 
   * @returns 
   */
  updateShipmentStatus(awbNo: Array<string>, data: ShipmentStatus) : Observable<any> {
    let param = `shipmentNo=${awbNo}`;

    const options = {
                     params: new HttpParams({fromString: param}),
                     headers: new HttpHeaders({
                              'Content-Type': 'application/json'
                      })
                    };
    return this.http.put<any>(this.getUri("from_web_shipment"), JSON.stringify(data), options);
  }

  /**
   * @brief This method sends multiple request
   * @param awbNo An array of string
   * @param data 
   * @returns Observable <any>
   */
  updateShipmentParallel(awbNo: Array<string>, data: string) : Observable<any> {
    let start = 0;
    let end = awbNo.length;
    let step = 50;
    let reqArr: Array<any> = [];
    
    while(start < end) {
      
      let subArray = awbNo.slice(start, start +=step);
      let param = `shipmentNo=${subArray}`;

      const options = {
                       params: new HttpParams({fromString: param}),
                       headers: new HttpHeaders({
                                'Content-Type': 'application/json'
                        })
                      };
      let req = this.http.put<any>(this.getUri("from_web_shipment"), data, options);
      reqArr.push(req);
    }
    
    return forkJoin(reqArr);
  }

  /**
   * @brief This method sends multiple request
   * @param awbNo An aero bill of string
   * @param json data 
   * @returns Observable <any>
   */
  updateSingleShipment(awbNo: string, data: string, acc?: string) : Observable<any> {
    let param = "";
    if(acc && acc.length > 0) {
      param = `shipmentNo=${awbNo}&isSingleShipment=true&accountCode=${acc}`;
    } else {
      param = `shipmentNo=${awbNo}&isSingleShipment=true`;
    }
    const options = {
                       params: new HttpParams({fromString: param}),
                       headers: new HttpHeaders({
                                'Content-Type': 'application/json'
                        })
                    };
    return(this.http.put<any>(this.getUri("from_web_shipment"), JSON.stringify(data), options));
  }

  /**
   * @brief This method sends multiple request
   * @param { "awbno": [], "altrefno": []} JSON object
   * @param json data 
   * @returns Observable <any>
   */
  updateBulkAltRefForShipments(data: any) : Observable<any> {
    
    const options = {
                       headers: new HttpHeaders({
                                'Content-Type': 'application/json'
                        })
                    };
    return(this.http.put<any>(this.getUri("from_web_bulk_altrefshipment"), JSON.stringify(data), options));
  }

  /** CREATE Section */
  /**
   * 
   * @param newAccount 
   * @returns 
   */
  createAccount(newAccount:Account) : Observable<Account> {
    return this.http.post<Account>(this.getUri("from_web_account"), JSON.stringify(newAccount), this.httpOptions);
  }

  createInventory(product: Inventory): Observable<any> {

    return this.http.post<Inventory>(this.getUri("from_web_inventory"), JSON.stringify(product), this.httpOptions);
  }

  //3rd Part Shipment Creation
  create3rdPartyShipment(awbList: string, uri: string, acc?: string): Observable<any> {
    const headers = new HttpHeaders({ 'Content-Type': 'application/json' });
    const options = acc && acc.length
      ? { headers, params: new HttpParams({ fromString: `accountCode=${acc}` }) }
      : { headers };
    return this.http.post<any>(uri, JSON.stringify(awbList), options);
  }


  /**
   * 
   * @param newShipment 
   * @returns 
   */
   createBulkShipment(newShipment:string) : Observable<any> {
    return this.http.post<Shipment>(this.getUri("from_web_bulk_shipment"), 
                                    newShipment, 
                                    this.httpOptions);
  }

  /**
   * 
   * @param newShipment 
   * @returns 
   */
   createShipment(newShipment:any) : Observable<any> {
    return this.http.post<any>(this.getUri("from_web_shipment"), 
                                    newShipment, 
                                    this.httpOptions);
  }


  initiateEmail(email: Email): Observable<any> {
    return this.http.post<Email>(this.getUri("from_web_email"), JSON.stringify(email), this.httpOptions);
  }

  createJob(job:any) : Observable<any> {
    return this.http.post<any>(this.getUri("from_web_job"),
                                    job,
                                    this.httpOptions);
  }

  /** SSO Section — see docs/design/sso/sso-design.md §8. */

  /** List the identity providers configured for the login screen. */
  getSsoProviders(): Observable<SsoProvider[]> {
    return this.http.get<SsoProvider[]>(this.getUri("from_web_sso_providers"));
  }

  /** Return the account behind the current session cookie (401 when none). */
  getSession(): Observable<SsoSession> {
    return this.http.get<SsoSession>(this.getUri("from_web_sso_session"));
  }

  /** Revoke the current session server-side and clear the session cookie. */
  ssoLogout(): Observable<any> {
    return this.http.post<any>(this.getUri("from_web_sso_logout"), {});
  }

  /**
   * Absolute URL that begins an SSO login. Used with window.location.href —
   * the round trip leaves the SPA for the IdP and the backend redirects the
   * browser back to returnTo once authenticated.
   */
  ssoLoginUrl(providerId: string, returnTo: string): string {
    return this.getUri("from_web_sso_login") +
           `?provider=${encodeURIComponent(providerId)}` +
           `&return_to=${encodeURIComponent(returnTo)}`;
  }

}
