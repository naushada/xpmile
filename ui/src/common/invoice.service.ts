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
      pageMargins: [24, 18, 24, 18],
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

    const date = this.formatShipmentDate(
      si.activity?.at(0)?.date ?? si.createdOn,
      'dd/MM/yyyy'
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
            body: [[ { text: 'Commercial Invoice', fontSize: 16, bold: true, margin: [4, 4, 4, 4] } ]]
          },
          margin: [0, 0, 0, 8]
        },

        // 2. Date (left) + barcode (right)
        {
          columns: [
            { width: '*', stack: [ this.labelValue('Date :', date) ] },
            {
              width: 260,
              alignment: 'right',
              stack: [
                { image: this.code39(awbno), width: 200, alignment: 'right' },
                { text: `*${awbno}*`, alignment: 'right', bold: true, fontSize: 10, margin: [0, 1, 0, 0] }
              ]
            }
          ],
          margin: [0, 0, 0, 8]
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
          columnGap: 16,
          margin: [0, 0, 0, 10]
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
          columnGap: 16,
          margin: [0, 0, 0, 8]
        },

        // 5. Items table
        {
          table: {
            widths: ['*', 110, 110],
            heights: [18, 40],
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
          margin: [0, 0, 0, 8]
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
          margin: [0, 0, 0, 8]
        },

        // 7. Reason For Export
        this.labelValue('Reason For Export', 'N/A'),

        // 8. Declaration paragraphs
        {
          text: [
            'I declare that the information is true and correct to the best of my knowledge and the goods are of ',
            { text: sender.country || '__________', decoration: 'underline' },
            ' origin.'
          ],
          margin: [0, 8, 0, 4]
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
        { text: ' ', margin: [0, 22, 0, 0] },
        { canvas: [{ type: 'line', x1: 0, y1: 0, x2: 200, y2: 0, lineWidth: 0.7 }] },
        { text: 'Designation of Authorised Signatory', margin: [0, 2, 0, 16] },
        { canvas: [{ type: 'line', x1: 0, y1: 0, x2: 200, y2: 0, lineWidth: 0.7 }] },
        { text: 'Signature / Stamp', margin: [0, 2, 0, 0] }
      ],
      // gets stamped on by buildCommercialInvoiceContent() between shipments
      pageBreak: undefined as 'after' | undefined
    };
  }

  // Accept the dd/MM/yyyy strings shipments are stored as (created by the
  // bulk + single forms via formatDate(... 'dd/MM/yyyy', 'en-GB')) — feeding
  // those back into Angular's formatDate throws "Unable to convert ... into
  // a date" and tanks the whole pdfMake build silently. Also handle the
  // Date-object and ISO-string cases so this helper is forgiving.
  private formatShipmentDate(raw: unknown, pattern: string): string {
    if (raw instanceof Date) return formatDate(raw, pattern, 'en-GB');
    if (typeof raw === 'string') {
      const m = raw.match(/^(\d{1,2})\/(\d{1,2})\/(\d{4})$/);
      if (m) {
        const [, d, mo, y] = m;
        return formatDate(new Date(+y, +mo - 1, +d), pattern, 'en-GB');
      }
      try { return formatDate(raw as any, pattern, 'en-GB'); } catch { return raw; }
    }
    if (typeof raw === 'number') return formatDate(raw, pattern, 'en-GB');
    return formatDate(new Date(), pattern, 'en-GB');
  }

  // Label + underlined value row. Mirrors the form-field look in the
  // operator template — fixed-width bold label, then the value typed onto
  // a horizontal rule that runs the rest of the row.
  //
  // Rendered as a pdfMake table because the table's '*' column width
  // auto-fits the parent container; the value cell's bottom border
  // then sizes itself to that column. The earlier canvas-line
  // implementation drew with absolute coordinates and overflowed the
  // right CONSIGNEE column into / past the page margin.
  private labelValue(label: string, value: string | undefined): any {
    return {
      table: {
        widths: [120, '*'],
        body: [[
          { text: label, bold: true, fontSize: 9, border: [false, false, false, false] },
          { text: value || ' ', fontSize: 9, border: [false, false, false, true] }
        ]]
      },
      layout: {
        defaultBorder: false,
        paddingLeft:   () => 0,
        paddingRight:  () => 0,
        paddingTop:    () => 1,
        paddingBottom: () => 2
      },
      margin: [0, 1, 0, 2]
    };
  }

  // Barcode used at the top-right of the invoice. CODE128 (not CODE39)
  // because CODE39 throws on lowercase or unsupported characters and
  // would tank the pdfMake build for any non-canonical awbno. The
  // operator template's *<awbno>* visual comes from the caption text
  // rendered separately above; switching the internal encoder doesn't
  // affect that.
  private code39(text: string): string {
    const canvas = document.createElement('canvas');
    JsBarcode(canvas, text || 'N/A', {
      format:       'CODE128',
      height:       46,
      displayValue: false,
      margin:       0
    });
    return canvas.toDataURL('image/png');
  }
}
