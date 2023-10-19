import { Component, OnInit } from '@angular/core';
import { FormBuilder, FormGroup } from '@angular/forms';
import { HttpsvcService } from 'src/common/httpsvc.service';

import * as JsBarcode from "jsbarcode";
import pdfMake from 'pdfmake/build/pdfmake';
import pdfFonts from 'pdfmake/build/vfs_fonts';

pdfMake.vfs = pdfFonts.pdfMake.vfs;

@Component({
  selector: 'app-create-manifest',
  templateUrl: './create-manifest.component.html',
  styleUrls: ['./create-manifest.component.scss']
})
export class CreateManifestComponent implements OnInit {

  manifestForm:FormGroup;
  constructor(private fb:FormBuilder, private http:HttpsvcService) {
    this.manifestForm = this.fb.group({
      sku: '',
      qty: ''
    });
   }

  ngOnInit(): void {
  }

  createManifest(): void {
    //let sku: string = this.manifestForm.get('sku')?.value;
    //let qty: number = this.manifestForm.get('qty')?.value;

    //alert("sku " + sku + " qty " + qty);
    //this.onCreateA2Label;
  }

      
  /** Label A10 Generation  */
  Info = {
    title: 'A10 Label',
    author: 'Mohd Naushad Ahmed',
    subject: 'A10 Label for Shipment',
    keywords: 'A10 Label',
  };

  A10LabelContentsBody:Array<object> = new Array<object>();

  buildA10ContentsBody() {

    let sku: string = this.manifestForm.get('sku')?.value;
    let qty: number = this.manifestForm.get('qty')?.value;

    this.A10LabelContentsBody.length = 0;
    for(let idx = 0; idx < qty; ++idx) {
      let ent = [
        {
          table: {
            body: [
              [ {image: this.textToBase64Barcode(sku, 70, 10), bold: false, alignment: 'left', valign:'top', width:90, pageOrientation: 'portrait'}]
            ]
          },
          pageBreak: 'after'
        }
      ];
      this.A10LabelContentsBody.push(ent);
    }
  }

  docDefinitionA10 = {
    info: this.Info,
    pageSize:'A10',
    // [left, top, right, bottom] or [horizontal, vertical] or just a number for equal margins
    pageMargins: [ 1, 1, 1, 1],
    pageOrientation: 'landscape',
    content: this.A10LabelContentsBody,
    
  };


  textToBase64Barcode(text: string, ht:number, fSize: number = 15) {
    if(!text.length) {
      text = "default";
    }

    var canvas = document.createElement("canvas");
    JsBarcode(canvas, text, {format: "CODE128", height: ht, fontOptions: 'bold', fontSize: fSize});
    return canvas.toDataURL("image/png");
  }

  onCreateA2Label() {
    this.buildA10ContentsBody();
    pdfMake.createPdf(this.docDefinitionA10).download( "A10" + "-label");
  }
}
