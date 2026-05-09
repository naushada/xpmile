package com.xpmile.onprem.service;

import org.apache.poi.ss.usermodel.*;
import org.apache.poi.xssf.usermodel.XSSFWorkbook;

import java.io.FileOutputStream;
import java.io.IOException;

/**
 * Run once: generates Excel fixtures for BulkShipmentParserTest.
 * Execute with: mvn exec:java -Dexec.mainClass=com.xpmile.onprem.service.GenerateFixtures
 */
public class GenerateFixtures {

    private static final String BASE = "src/test/resources/fixtures/";

    public static void main(String[] args) throws IOException {
        generate3Rows();
        generateEmpty();
        generateMissingAwb();
        System.out.println("Fixtures generated in " + BASE);
    }

    private static String[] headers() {
        return new String[]{
                "isAutoGenerate", "awbno", "altRefNo",
                "sender.accountNo", "sender.name", "sender.companyName", "sender.country",
                "sender.city", "sender.address", "sender.postalCode", "sender.contact", "sender.email",
                "info.service", "info.numberOfItems", "info.weight", "info.goodsDescription",
                "info.currency", "info.codAmount", "info.hsCode",
                "receiver.name", "receiver.country", "receiver.city",
                "receiver.address", "receiver.postalCode", "receiver.phone", "receiver.email"
        };
    }

    private static void writeHeader(Sheet sheet) {
        Row row = sheet.createRow(0);
        String[] h = headers();
        for (int i = 0; i < h.length; i++) row.createCell(i).setCellValue(h[i]);
    }

    private static void writeDataRow(Sheet sheet, int rowIdx, String awbno, boolean autoGen,
                                     String senderName, String receiverName) {
        Row row = sheet.createRow(rowIdx);
        row.createCell(0).setCellValue(autoGen);
        row.createCell(1).setCellValue(awbno);
        row.createCell(2).setCellValue("ALT-" + rowIdx);
        row.createCell(3).setCellValue("ACC001");
        row.createCell(4).setCellValue(senderName);
        row.createCell(5).setCellValue("Company A");
        row.createCell(6).setCellValue("AE");
        row.createCell(7).setCellValue("Dubai");
        row.createCell(8).setCellValue("123 Main St");
        row.createCell(9).setCellValue("00000");
        row.createCell(10).setCellValue("+971500000001");
        row.createCell(11).setCellValue("sender@test.com");
        row.createCell(12).setCellValue("Express");
        row.createCell(13).setCellValue(1);
        row.createCell(14).setCellValue(2.5);
        row.createCell(15).setCellValue("Electronics");
        row.createCell(16).setCellValue("AED");
        row.createCell(17).setCellValue(0);
        row.createCell(18).setCellValue("8471.30");
        row.createCell(19).setCellValue(receiverName);
        row.createCell(20).setCellValue("SA");
        row.createCell(21).setCellValue("Riyadh");
        row.createCell(22).setCellValue("456 King Rd");
        row.createCell(23).setCellValue("11111");
        row.createCell(24).setCellValue("+966500000001");
        row.createCell(25).setCellValue("receiver@test.com");
    }

    private static void generate3Rows() throws IOException {
        try (Workbook wb = new XSSFWorkbook()) {
            Sheet sheet = wb.createSheet("Shipments");
            writeHeader(sheet);
            writeDataRow(sheet, 1, "XP001", false, "Alice", "Bob");
            writeDataRow(sheet, 2, "XP002", false, "Charlie", "Dave");
            writeDataRow(sheet, 3, "XP003", false, "Eve", "Frank");
            try (FileOutputStream out = new FileOutputStream(BASE + "bulk-3rows.xlsx")) {
                wb.write(out);
            }
        }
    }

    private static void generateEmpty() throws IOException {
        try (Workbook wb = new XSSFWorkbook()) {
            Sheet sheet = wb.createSheet("Shipments");
            writeHeader(sheet);
            try (FileOutputStream out = new FileOutputStream(BASE + "bulk-empty.xlsx")) {
                wb.write(out);
            }
        }
    }

    private static void generateMissingAwb() throws IOException {
        try (Workbook wb = new XSSFWorkbook()) {
            Sheet sheet = wb.createSheet("Shipments");
            writeHeader(sheet);
            // autoGenerate=false, awbno="" → should trigger ParseException
            writeDataRow(sheet, 1, "", false, "Alice", "Bob");
            try (FileOutputStream out = new FileOutputStream(BASE + "bulk-missing-awb.xlsx")) {
                wb.write(out);
            }
        }
    }
}
