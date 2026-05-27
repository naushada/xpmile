import { Injectable } from '@angular/core';
import { formatDate } from '@angular/common';
import * as JsBarcode from 'jsbarcode';
import pdfMake from 'pdfmake/build/pdfmake';
import pdfFonts from 'pdfmake/build/vfs_fonts';

import { Shipment } from './app-globals';
import { lookupIata } from './iata-codes';

// Module-level — pdfMake.vfs must be set once before any PDF generation.
pdfMake.vfs = pdfFonts.pdfMake.vfs;

export type LabelSize = 'A2' | 'A4' | 'A6' | 'A10';

// Landscape dimensions in points (1 mm ≈ 2.835 pt). barWidth/barHeight are
// JsBarcode pixel sizes; together they shape the raster canvas to roughly
// match the landscape page aspect for a short (~26-module) SKU so the
// barcode visibly fills bigger pages instead of becoming a thin strip.
// Longer SKUs grow the canvas wider than the page; fit: scales to width
// and leaves vertical whitespace, which is the better failure mode.
// fontSize is the pdfMake text size for the SKU below the bars (rendered
// separately, not baked into the barcode PNG).
const SIZE_SPEC: Record<LabelSize, { w: number; h: number; barWidth: number; barHeight: number; fontSize: number }> = {
  A2:  { w: 1684, h: 1191, barWidth: 60, barHeight: 1100, fontSize: 48 },
  A4:  { w:  842, h:  595, barWidth: 30, barHeight:  540, fontSize: 24 },
  A6:  { w:  421, h:  297, barWidth: 15, barHeight:  260, fontSize: 14 },
  A10: { w:  105, h:   74, barWidth:  8, barHeight:  120, fontSize:  7 },
};

@Injectable({ providedIn: 'root' })
export class LabelService {

  /**
   * Render N copies of a barcode label PDF in the given ISO A-series size for
   * the given SKU and trigger a download. Each label is one PDF page with the
   * CODE128 barcode scaled to fill the page (landscape).
   *
   * The pdfMake document definition is built fresh inside this method per
   * the project rule against caching docDef/content arrays on long-lived
   * objects — see CLAUDE.md.
   */
  createLabelPdf(sku: string, qty: number, size: LabelSize = 'A10', fileName?: string): void {
    const safeSku    = (sku ?? '').toString().trim() || 'default';
    const count      = Math.max(1, Math.floor(Number(qty) || 0));
    const spec       = SIZE_SPEC[size];
    const margin     = 1;
    const fitW       = spec.w - 2 * margin;
    const fitH       = spec.h - 2 * margin;
    // Reserve room under the bars for the pdfMake-rendered SKU line: one
    // line of text at spec.fontSize plus a small gap above it. Whatever
    // remains is the box the barcode is fitted into.
    const textGap    = 4;
    const textBlockH = spec.fontSize + textGap;
    const barFitH    = fitH - textBlockH;
    const barcode    = this.barcodeDataUrl(safeSku, spec.barWidth, spec.barHeight);

    const content: any[] = [];
    for (let i = 0; i < count; i++) {
      content.push({
        stack: [
          { image: barcode, fit: [fitW, barFitH], alignment: 'center' },
          { text: safeSku, alignment: 'center', fontSize: spec.fontSize, bold: true, margin: [0, textGap, 0, 0] }
        ],
        pageBreak: i < count - 1 ? 'after' : undefined
      });
    }

    const docDef: any = {
      info: {
        title:    `${size} Label`,
        author:   'xpmile',
        subject:  `${size} Label for ${safeSku}`,
        keywords: `${size} Label`
      },
      pageSize:        size,
      pageMargins:     [margin, margin, margin, margin],
      pageOrientation: 'landscape',
      content
    };

    pdfMake.createPdf(docDef).download(`${fileName ?? size + '-label'}-${safeSku}`);
  }

  /**
   * Render an A6 shipment label per the operator-supplied template — top
   * brand + horizontal AWB barcode, origin/destination 3-letter codes,
   * service codes row, weight/customs row, sender + receiver address blocks,
   * a vertical side barcode + COD box, and a description / shipper-ref
   * footer. One label per shipment, one shipment per page.
   *
   * Fields not yet on the Shipment model (Foreign Ref, the second + third
   * service-code slots, Short Address Code, Route, Remarks, Consignee Ref
   * / Ref2) print as blank slots for the operator to fill in.
   */
  downloadA6ShipmentLabel(shipments: Shipment[], opts: { brand?: string; filename?: string } = {}): void {
    if (!shipments?.length) return;
    const brand    = opts.brand ?? 'xpmile';
    const filename = opts.filename ?? 'A6-label';

    const content = shipments.map((sh, idx) => {
      const block = this.buildA6ShipmentLabel(sh, brand);
      if (idx < shipments.length - 1) block.pageBreak = 'after';
      return block;
    });

    pdfMake.createPdf({
      info: {
        title:    'A6 Shipment Label',
        author:   'xpmile',
        subject:  'Shipment Label',
        keywords: 'shipment label A6'
      },
      pageSize:        'A6',
      pageMargins:     [6, 6, 6, 6],
      pageOrientation: 'portrait',
      content,
      defaultStyle:    { fontSize: 7 }
    }).download(filename);
  }

  // Build one A6 page for one shipment. Returns a pdfMake `stack` block;
  // the caller stamps `pageBreak: 'after'` between consecutive shipments.
  private buildA6ShipmentLabel(elm: Shipment, brand: string): any {
    const si       = elm.shipment.shipmentInformation;
    const sender   = elm.shipment.senderInformation;
    const receiver = elm.shipment.receiverInformation;
    const awbno    = elm.shipment.awbno ?? '';

    const date = this.formatShipmentDate(
      si.activity?.at(0)?.date ?? si.createdOn,
      'MMM d, y'
    );
    const ref1 = sender.referenceNo ?? '';

    // 3-letter origin/destination codes from the IATA lookup in
    // iata-codes.ts. Falls back to the first 3 chars of the city
    // (upper-cased) when the city isn't in the curated table — the
    // operator can extend iata-codes.ts as new routes appear.
    const origin = lookupIata(sender.city)   ?? (sender.city   || '').slice(0, 3).toUpperCase();
    const dest   = lookupIata(receiver.city) ?? (receiver.city || '').slice(0, 3).toUpperCase();

    // Service codes. First slot derives from si.service ("Document" →
    // DOC, "Non Document" → NDC, anything else → first 3 chars uppercase).
    // The remaining two slots aren't in the model — blank for now.
    const svc       = (si.service || '').toLowerCase();
    const svcCode   = svc.includes('non') ? 'NDC' : svc.includes('doc') ? 'DOC' : (si.service || '').slice(0, 3).toUpperCase();
    const svcCode2  = '';
    const svcCode3  = '';
    const itemCount = si.numberOfItems ?? '';

    const weight     = `${si.weight ?? ''} ${si.weightUnits ?? 'KG'}`.trim();
    const customsLine = `${si.customsValue ?? ''} ${si.currency ?? ''}`.trim();
    const codLine    = `${si.codAmount ?? '0'} ${si.currency ?? ''}`.trim();

    const topBarcode  = this.code128(awbno, 36);
    const sideBarcode = this.code128Rotated(awbno, 28);

    // Each thin info row uses small font; the address blocks use slightly
    // larger so the receiver/sender names are legible on a 4×6 print.
    return {
      stack: [
        // Row 1: brand | top barcode + AWB text
        {
          table: {
            widths: [78, '*'],
            heights: [44, 44],
            body: [
              [
                // Brand cell + Origin block
                {
                  stack: [
                    { text: brand, fontSize: 14, bold: true, color: '#c00', margin: [0, 0, 0, 2] },
                    { text: 'Origin:', fontSize: 7 },
                    { text: origin || '—', fontSize: 18, bold: true }
                  ],
                  rowSpan: 2,
                  margin: [4, 4, 4, 4]
                },
                {
                  // Top barcode + AWB text below
                  stack: [
                    { image: topBarcode, fit: [180, 32], alignment: 'center' },
                    { text: awbno, alignment: 'center', fontSize: 10, margin: [0, 1, 0, 0] }
                  ],
                  margin: [2, 2, 2, 2]
                }
              ],
              [
                '',
                {
                  // Destination block + Date / Refs
                  table: {
                    widths: [44, '*'],
                    body: [[
                      {
                        stack: [
                          { text: 'Destination:', fontSize: 7 },
                          { text: dest || '—', fontSize: 18, bold: true }
                        ],
                        border: [false, false, true, false]
                      },
                      {
                        stack: [
                          { text: `Date: ${date}`, fontSize: 7 },
                          { text: 'Foreign Ref:',  fontSize: 7 },
                          { text: `Ref1: ${ref1}`, fontSize: 7 }
                        ],
                        border: [false, false, false, false]
                      }
                    ]]
                  },
                  layout: { defaultBorder: false }
                }
              ]
            ]
          }
        },

        // Row 2: service codes EXP | PPX | P | 1
        {
          table: {
            widths: ['*', '*', '*', '*'],
            body: [[
              { text: svcCode  || ' ', fontSize: 13, bold: true, alignment: 'center', margin: [0, 4, 0, 4] },
              { text: svcCode2 || ' ', fontSize: 13, bold: true, alignment: 'center', margin: [0, 4, 0, 4] },
              { text: svcCode3 || ' ', fontSize: 13, bold: true, alignment: 'center', margin: [0, 4, 0, 4] },
              { text: itemCount.toString() || ' ', fontSize: 13, alignment: 'center', margin: [0, 4, 0, 4] }
            ]]
          }
        },

        // Row 3: Weight + Chargeable + Customs row
        {
          table: {
            widths: ['*', '*'],
            body: [
              [
                { text: `Weight: ${weight}`,       fontSize: 7, margin: [2, 2, 0, 0] },
                { text: `Chargeable: ${weight}`,   fontSize: 7, margin: [2, 2, 0, 0] }
              ],
              [
                { text: 'Services:',                fontSize: 7, margin: [2, 0, 0, 2] },
                { text: `Customs: ${customsLine}`,  fontSize: 7, margin: [2, 0, 0, 2] }
              ]
            ]
          }
        },

        // Row 4 — body table: sender + receiver on the left, side barcode + COD on the right
        {
          table: {
            widths: ['*', 58],
            body: [[
              // LEFT column: sender block, then receiver block
              {
                stack: [
                  // Sender
                  { text: `Account: ${sender.accountNo ?? ''}`, fontSize: 7 },
                  { text: sender.name ?? '',          fontSize: 8, bold: true },
                  { text: sender.companyName ?? '',   fontSize: 8 },
                  { text: sender.address ?? '',       fontSize: 8 },
                  { text: ' ', margin: [0, 1, 0, 0] },
                  { text: sender.city ?? '',          fontSize: 8 },
                  {
                    columns: [
                      { text: this.countryAlpha2(sender.country), fontSize: 8, width: 'auto' },
                      { text: sender.contact ?? '', fontSize: 8, alignment: 'right' }
                    ]
                  },
                  { text: ' ', margin: [0, 4, 0, 0] },

                  // Receiver
                  { text: receiver.name ?? '',    fontSize: 8, bold: true },
                  { text: receiver.name ?? '',    fontSize: 8 },
                  { text: ' ', margin: [0, 1, 0, 0] },
                  { text: receiver.address ?? '', fontSize: 8 },
                  { text: ' ', margin: [0, 1, 0, 0] },
                  { text: receiver.city ?? '',    fontSize: 8 },
                  {
                    columns: [
                      { text: this.countryAlpha2(receiver.country), fontSize: 8, width: 'auto' },
                      { text: receiver.state ?? '', fontSize: 8, alignment: 'right' }
                    ]
                  },
                  {
                    columns: [
                      { text: receiver.contact ?? '', fontSize: 8 },
                      { text: receiver.phone   ?? '', fontSize: 8, alignment: 'right' }
                    ]
                  },
                  {
                    columns: [
                      { text: 'Short Address Code:', fontSize: 7 },
                      { text: 'Route: N/A',          fontSize: 7, alignment: 'right' }
                    ]
                  },
                  { text: 'Remarks:', fontSize: 7 }
                ],
                margin: [2, 2, 2, 2]
              },

              // RIGHT column: COD box + vertical side barcode
              {
                stack: [
                  { text: 'COD', fontSize: 9, bold: true, alignment: 'center', margin: [0, 2, 0, 0] },
                  {
                    table: {
                      widths: ['*'],
                      body: [[ { text: codLine || '0', alignment: 'center', fontSize: 9, fillColor: '#e6e6e6', margin: [0, 4, 0, 4] } ]]
                    },
                    margin: [2, 2, 2, 6]
                  },
                  { image: sideBarcode, fit: [54, 260], alignment: 'center' }
                ],
                margin: [0, 0, 0, 0]
              }
            ]]
          }
        },

        // Row 5: Description + Shipper Ref | Consignee Ref + Ref2
        {
          table: {
            widths: ['*', '*'],
            body: [
              [
                { text: `Description: ${si.goodsDescription ?? ''}`, fontSize: 7, margin: [2, 2, 0, 0] },
                { text: 'Consignee Ref:',                              fontSize: 7, margin: [2, 2, 0, 0] }
              ],
              [
                { text: `Shipper Ref: ${ref1}`, fontSize: 7, margin: [2, 0, 0, 2] },
                { text: 'Consignee Ref2:',     fontSize: 7, margin: [2, 0, 0, 2] }
              ]
            ]
          }
        }
      ],
      pageBreak: undefined as 'after' | undefined
    };
  }

  // First two letters of country name, uppercased — a stand-in for ISO
  // alpha-2 until we wire the country-state-city lookup here. "United
  // Arab Emirates" → "UN", "Kuwait" → "KU"; an operator who saved an
  // ISO-style "KW" gets back "KW" unchanged.
  // Accept the dd/MM/yyyy strings shipments are stored as (created by the
  // bulk + single forms via formatDate(... 'dd/MM/yyyy', 'en-GB')) — feeding
  // those back into Angular's formatDate throws "Unable to convert ... into
  // a date" and tanks the whole pdfMake build silently. Also handle Date /
  // ISO-string / numeric-timestamp inputs so this is forgiving.
  private formatShipmentDate(raw: unknown, pattern: string): string {
    if (raw instanceof Date) return formatDate(raw, pattern, 'en');
    if (typeof raw === 'string') {
      const m = raw.match(/^(\d{1,2})\/(\d{1,2})\/(\d{4})$/);
      if (m) {
        const [, d, mo, y] = m;
        return formatDate(new Date(+y, +mo - 1, +d), pattern, 'en');
      }
      try { return formatDate(raw as any, pattern, 'en'); } catch { return raw; }
    }
    if (typeof raw === 'number') return formatDate(raw, pattern, 'en');
    return formatDate(new Date(), pattern, 'en');
  }

  private countryAlpha2(country?: string): string {
    if (!country) return '';
    if (country.length <= 3) return country.toUpperCase();
    return country.slice(0, 2).toUpperCase();
  }

  // Side barcode rendered into a canvas then rotated 90° via a second
  // canvas, since pdfMake doesn't rotate images natively. Returns a PNG
  // data URL the caller drops into an {image:...} cell.
  private code128Rotated(text: string, barWidth: number): string {
    const src = document.createElement('canvas');
    JsBarcode(src, text || 'N/A', {
      format:       'CODE128',
      width:        2,
      height:       barWidth * 9,
      displayValue: true,
      fontSize:     14,
      margin:       0
    });
    // Rotate 90° clockwise onto a destination canvas.
    const dst = document.createElement('canvas');
    dst.width  = src.height;
    dst.height = src.width;
    const ctx = dst.getContext('2d');
    if (ctx) {
      ctx.translate(dst.width, 0);
      ctx.rotate(Math.PI / 2);
      ctx.drawImage(src, 0, 0);
    }
    return dst.toDataURL('image/png');
  }

  private code128(text: string, height: number): string {
    const canvas = document.createElement('canvas');
    JsBarcode(canvas, text || 'N/A', {
      format:       'CODE128',
      height,
      displayValue: false,
      margin:       0
    });
    return canvas.toDataURL('image/png');
  }

  private barcodeDataUrl(text: string, width: number, height: number): string {
    const canvas = document.createElement('canvas');
    JsBarcode(canvas, text, {
      format:       'CODE128',
      width,
      height,
      displayValue: false
    });
    return canvas.toDataURL('image/png');
  }
}
