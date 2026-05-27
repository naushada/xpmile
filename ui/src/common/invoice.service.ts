import { Injectable } from '@angular/core';
import { formatDate } from '@angular/common';
import * as JsBarcode from 'jsbarcode';
import pdfMake from 'pdfmake/build/pdfmake';
import pdfFonts from 'pdfmake/build/vfs_fonts';

import { Shipment } from './app-globals';

pdfMake.vfs = pdfFonts.pdfMake.vfs;

/**
 * Builds the A4 Commercial Invoice PDF that the operator hands to customs.
 * Used by `tracking > Multiple Shipment Status > Generate Invoice` and the
 * `shipping > list > Generate Invoice` button — both render the same layout
 * via this service.
 *
 * Layout follows the operator-supplied template: title box, Date + CODE-39
 * barcode, SHIPPER and CONSIGNEE side-by-side, a strip of shipment-meta
 * fields, the items table, total, reason-for-export, declaration text, and
 * signature/stamp lines.
 *
 * Fields the current Shipment model doesn't carry (Dimensions, Shipment
 * Terms, Incoterms, GST, IEC, Reason For Export) render as a blank line
 * for the operator to fill in by hand on the printed copy.
 */
@Injectable({ providedIn: 'root' })
export class InvoiceService {

  downloadCommercialInvoice(shipments: Shipment[], filename = 'commercial-invoice'): void {
    if (!shipments?.length) return;
    const content = this.buildCommercialInvoiceContent(shipments);
    pdfMake.createPdf({
      info: {
        title:    'Commercial Invoice',
        author:   'xpmile',
        subject:  'Commercial Invoice',
        keywords: 'commercial invoice customs'
      },
      pageSize:    'A4',
      pageMargins: [28, 24, 28, 24],
      content,
      defaultStyle: { fontSize: 9 }
    }).download(filename);
  }

  buildCommercialInvoiceContent(shipments: Shipment[]): any[] {
    return shipments.map((elm, idx) => {
      const block = this.buildOne(elm);
      if (idx < shipments.length - 1) block.pageBreak = 'after';
      return block;
    });
  }

  private buildOne(elm: Shipment): any {
    const si       = elm.shipment.shipmentInformation;
    const sender   = elm.shipment.senderInformation;
    const receiver = elm.shipment.receiverInformation;
    const awbno    = elm.shipment.awbno ?? '';

    const date = formatDate(
      si.activity?.at(0)?.date as any ?? si.createdOn as any ?? new Date(),
      'dd/MM/yyyy', 'en-GB'
    );

    const totalWeight  = `${si.weight ?? ''} ${si.weightUnits ?? 'KG'}`.trim();
    const currency     = si.currency ?? '';
    const customsValue = si.customsValue ?? '';
    const description  = si.goodsDescription || `${si.numberOfItems ?? ''} pcs`.trim();

    return {
      stack: [
        // 1. Title box
        {
          table: {
            widths: ['*'],
            body: [[ { text: 'Commercial Invoice', fontSize: 18, bold: true, margin: [6, 6, 6, 6] } ]]
          },
          margin: [0, 0, 0, 14]
        },

        // 2. Date (left) + barcode (right)
        {
          columns: [
            { width: '*', stack: [ this.labelValue('Date :', date) ] },
            {
              width: 260,
              alignment: 'right',
              stack: [
                { image: this.code39(awbno), width: 240, alignment: 'right' },
                { text: `*${awbno}*`, alignment: 'right', bold: true, fontSize: 11, margin: [0, 2, 0, 0] }
              ]
            }
          ],
          margin: [0, 0, 0, 16]
        },

        // 3. SHIPPER + CONSIGNEE side-by-side
        {
          columns: [
            {
              width: '*',
              stack: [
                { text: 'SHIPPER', bold: true, fontSize: 10, margin: [0, 0, 0, 6] },
                this.labelValue('Company Name :', sender.companyName || sender.name),
                this.labelValue('Address :',      sender.address),
                { text: ' ', margin: [0, 4, 0, 0] },
                this.labelValue('Town/Area Code :', sender.city ? `${sender.city},` : ''),
                this.labelValue('State/Country :',  sender.country),
                this.labelValue('Contact Name :',   sender.name),
                this.labelValue('Phone/Fax No :',   sender.contact || sender.phoneNumber)
              ]
            },
            {
              width: '*',
              stack: [
                { text: 'CONSIGNEE', bold: true, fontSize: 10, margin: [0, 0, 0, 6] },
                this.labelValue('Company Name :', receiver.name),
                this.labelValue('Address :',      receiver.address),
                { text: ' ', margin: [0, 4, 0, 0] },
                this.labelValue('Town/Area Code :', receiver.city ? `${receiver.city},` : ''),
                this.labelValue('State/Country :',  receiver.country),
                this.labelValue('Contact Name :',   receiver.name),
                this.labelValue('Phone/Fax No :',   receiver.contact || receiver.phone),
                this.labelValue('Email :',          receiver.email),
                // GST / IEC are not in the Shipment model — print blank for
                // the operator to fill in by hand if customs requires them.
                this.labelValue('GST :',            ''),
                this.labelValue('IEC :',            '')
              ]
            }
          ],
          columnGap: 28,
          margin: [0, 0, 0, 16]
        },

        // 4. Shipment-meta strip — Consignment Note + dims on the left,
        // Harmonised + Incoterms on the right.
        {
          columns: [
            {
              width: '*',
              stack: [
                this.labelValue('Consignment Note No.:', awbno),
                this.labelValue('No. of Items :',        si.numberOfItems),
                this.labelValue('Total Weight :',        totalWeight),
                // Dimensions / Shipment Terms not yet modelled.
                this.labelValue('Dimensions (LxWxH) :',  ''),
                this.labelValue('Shipment Terms :',      '')
              ]
            },
            {
              width: '*',
              stack: [
                this.labelValue('Harmonised Code:', si.hsCode),
                // Incoterms not yet modelled.
                this.labelValue('Incoterms:',       '')
              ]
            }
          ],
          columnGap: 28,
          margin: [0, 0, 0, 14]
        },

        // 5. Items table
        {
          table: {
            widths: ['*', 110, 110],
            heights: [22, 80],
            body: [
              [
                { text: 'DESCRIPTION',    alignment: 'center', bold: true, fillColor: '#f2f2f2' },
                { text: 'CURRENCY CODE',  alignment: 'center', bold: true, fillColor: '#f2f2f2' },
                { text: 'CUSTOMS VALUE',  alignment: 'center', bold: true, fillColor: '#f2f2f2' }
              ],
              [
                { text: description },
                { text: currency, alignment: 'center' },
                { text: customsValue }
              ]
            ]
          },
          margin: [0, 0, 0, 12]
        },

        // 6. Total Invoice Value box, right-aligned
        {
          columns: [
            { width: '*', text: '' },
            {
              width: 'auto',
              table: {
                widths: [120, 110],
                body: [[
                  { text: 'Total Invoice Value', bold: true, alignment: 'right', border: [false, false, false, false] },
                  { text: `${currency} ${customsValue}`.trim() }
                ]]
              }
            }
          ],
          margin: [0, 0, 0, 16]
        },

        // 7. Reason For Export
        this.labelValue('Reason For Export', 'N/A', 160),

        // 8. Declaration paragraphs
        {
          text: [
            'I declare that the information is true and correct to the best of my knowledge and the goods are of ',
            { text: sender.country || '__________', decoration: 'underline' },
            ' origin.'
          ],
          margin: [0, 14, 0, 8]
        },
        {
          text: [
            'We, ',
            { text: ` ${sender.name || '__________'} `, decoration: 'underline' },
            ' certify the particulars and quantity of the goods specified in this document are the goods which are submitted for clearance for export out of ',
            { text: sender.country || '__________', decoration: 'underline' },
            '.'
          ]
        },

        // 9. Signature blocks (visible underline lines so a printed copy is signable)
        { text: ' ', margin: [0, 48, 0, 0] },
        { canvas: [{ type: 'line', x1: 0, y1: 0, x2: 220, y2: 0, lineWidth: 0.7 }] },
        { text: 'Designation of Authorised Signatory', margin: [0, 4, 0, 36] },
        { canvas: [{ type: 'line', x1: 0, y1: 0, x2: 220, y2: 0, lineWidth: 0.7 }] },
        { text: 'Signature / Stamp', margin: [0, 4, 0, 0] }
      ],
      // gets stamped on by buildCommercialInvoiceContent() between shipments
      pageBreak: undefined as 'after' | undefined
    };
  }

  // Label + underlined value row. Mirrors the form-field look in the
  // operator template — fixed-width bold label, then the value typed onto
  // a horizontal rule that runs the rest of the row.
  private labelValue(label: string, value: string | undefined, lineWidth = 200): any {
    return {
      columns: [
        { width: 130, text: label, bold: true, fontSize: 9 },
        {
          width: '*',
          stack: [
            { text: value || ' ', fontSize: 9, margin: [4, 0, 0, 0] },
            { canvas: [{ type: 'line', x1: 4, y1: 0, x2: lineWidth, y2: 0, lineWidth: 0.5 }] }
          ]
        }
      ],
      margin: [0, 2, 0, 3]
    };
  }

  // CODE-39 with the asterisk sentinels visible below, matching the
  // operator template's barcode style. JsBarcode's own `displayValue`
  // renders the digits without the asterisks, so we render them
  // separately as a centred caption.
  private code39(text: string): string {
    const canvas = document.createElement('canvas');
    JsBarcode(canvas, text || 'N/A', {
      format:       'CODE39',
      height:       46,
      displayValue: false,
      margin:       0
    });
    return canvas.toDataURL('image/png');
  }
}
