import { Injectable } from '@angular/core';
import * as JsBarcode from 'jsbarcode';
import pdfMake from 'pdfmake/build/pdfmake';
import pdfFonts from 'pdfmake/build/vfs_fonts';

// Module-level — pdfMake.vfs must be set once before any PDF generation.
pdfMake.vfs = pdfFonts.pdfMake.vfs;

@Injectable({ providedIn: 'root' })
export class LabelService {

  /**
   * Render N copies of an A10-size barcode label PDF for the given SKU and
   * trigger a download. Each label is one PDF page with the CODE128 barcode.
   *
   * The pdfMake document definition is built fresh inside this method per
   * the project rule against caching docDef/content arrays on long-lived
   * objects — see CLAUDE.md.
   */
  createA10LabelPdf(sku: string, qty: number, fileName = 'A10-label'): void {
    const safeSku = (sku ?? '').toString().trim() || 'default';
    const count   = Math.max(1, Math.floor(Number(qty) || 0));

    // A10 landscape page = 37mm × 26mm → ~105pt × 74pt at 1pt/1mm * 72/25.4.
    // Margins are 1pt each side, so the printable area is ~103pt × 72pt.
    // We scale the barcode to fit that box exactly so there's no empty space
    // at the bottom of each label.
    const pageW = 105, pageH = 74, margin = 1;
    const fitW  = pageW - 2 * margin;
    const fitH  = pageH - 2 * margin;

    const content: any[] = [];
    for (let i = 0; i < count; i++) {
      content.push({
        image:     this.barcodeDataUrl(safeSku, 120, 14),
        fit:       [fitW, fitH],
        alignment: 'center',
        pageBreak: i < count - 1 ? 'after' : undefined
      });
    }

    const docDef: any = {
      info: {
        title:    'A10 Label',
        author:   'xpmile',
        subject:  `A10 Label for ${safeSku}`,
        keywords: 'A10 Label'
      },
      pageSize:        'A10',
      pageMargins:     [margin, margin, margin, margin],
      pageOrientation: 'landscape',
      content
    };

    pdfMake.createPdf(docDef).download(`${fileName}-${safeSku}`);
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
