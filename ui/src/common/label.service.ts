import { Injectable } from '@angular/core';
import * as JsBarcode from 'jsbarcode';
import pdfMake from 'pdfmake/build/pdfmake';
import pdfFonts from 'pdfmake/build/vfs_fonts';

// Module-level — pdfMake.vfs must be set once before any PDF generation.
pdfMake.vfs = pdfFonts.pdfMake.vfs;

export type LabelSize = 'A2' | 'A4' | 'A6' | 'A10';

// Landscape dimensions in points (1 mm ≈ 2.835 pt). Bar height + font size
// scale with the page so the barcode stays crisp at the larger sizes —
// JsBarcode rasterises to PNG and pdfMake's `fit:` upscaling would pixelate
// otherwise.
const SIZE_SPEC: Record<LabelSize, { w: number; h: number; barHeight: number; fontSize: number }> = {
  A2:  { w: 1684, h: 1191, barHeight: 1200, fontSize: 80 },
  A4:  { w:  842, h:  595, barHeight:  600, fontSize: 40 },
  A6:  { w:  421, h:  297, barHeight:  300, fontSize: 20 },
  A10: { w:  105, h:   74, barHeight:  120, fontSize: 14 },
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
    const safeSku = (sku ?? '').toString().trim() || 'default';
    const count   = Math.max(1, Math.floor(Number(qty) || 0));
    const spec    = SIZE_SPEC[size];
    const margin  = 1;
    const fitW    = spec.w - 2 * margin;
    const fitH    = spec.h - 2 * margin;
    const barcode = this.barcodeDataUrl(safeSku, spec.barHeight, spec.fontSize);

    const content: any[] = [];
    for (let i = 0; i < count; i++) {
      content.push({
        image:     barcode,
        fit:       [fitW, fitH],
        alignment: 'center',
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

  private barcodeDataUrl(text: string, height: number, fontSize = 15): string {
    const canvas = document.createElement('canvas');
    JsBarcode(canvas, text, {
      format:      'CODE128',
      height,
      fontOptions: 'bold',
      fontSize
    });
    return canvas.toDataURL('image/png');
  }
}
