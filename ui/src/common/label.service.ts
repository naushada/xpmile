import { Injectable } from '@angular/core';
import * as JsBarcode from 'jsbarcode';
import pdfMake from 'pdfmake/build/pdfmake';
import pdfFonts from 'pdfmake/build/vfs_fonts';

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
