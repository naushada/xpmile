package com.xpmile.onprem.service;

import com.xpmile.onprem.model.*;
import org.apache.poi.ss.usermodel.*;
import org.apache.poi.xssf.usermodel.XSSFWorkbook;
import org.springframework.stereotype.Service;

import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;

@Service
public class BulkShipmentParser {

    public static class ParseException extends RuntimeException {
        public ParseException(String message) { super(message); }
    }

    // Expected column order (0-indexed):
    // 0:isAutoGenerate 1:awbno 2:altRefNo
    // 3:sender.accountNo 4:sender.name 5:sender.companyName 6:sender.country
    // 7:sender.city 8:sender.address 9:sender.postalCode 10:sender.contact 11:sender.email
    // 12:info.service 13:info.numberOfItems 14:info.weight 15:info.goodsDescription
    // 16:info.currency 17:info.codAmount 18:info.hsCode
    // 19:receiver.name 20:receiver.country 21:receiver.city
    // 22:receiver.address 23:receiver.postalCode 24:receiver.phone 25:receiver.email

    public List<Shipment> parse(InputStream inputStream) {
        List<Shipment> shipments = new ArrayList<>();
        try (Workbook workbook = new XSSFWorkbook(inputStream)) {
            Sheet sheet = workbook.getSheetAt(0);
            int lastRow = sheet.getLastRowNum();
            // row 0 is header
            for (int i = 1; i <= lastRow; i++) {
                Row row = sheet.getRow(i);
                if (row == null) continue;
                shipments.add(parseRow(row, i + 1));
            }
        } catch (ParseException e) {
            throw e;
        } catch (Exception e) {
            throw new ParseException("Failed to parse Excel: " + e.getMessage());
        }
        return shipments;
    }

    private Shipment parseRow(Row row, int humanRow) {
        boolean autoGenerate = boolCell(row, 0);
        String awbno = strCell(row, 1);
        if (!autoGenerate && (awbno == null || awbno.isBlank())) {
            throw new ParseException("Row " + humanRow + ": awbno is required when isAutoGenerate is false");
        }

        SenderInformation sender = new SenderInformation();
        sender.setAccountNo(strCell(row, 3));
        sender.setName(strCell(row, 4));
        sender.setCompanyName(strCell(row, 5));
        sender.setCountry(strCell(row, 6));
        sender.setCity(strCell(row, 7));
        sender.setAddress(strCell(row, 8));
        sender.setPostalCode(strCell(row, 9));
        sender.setContact(strCell(row, 10));
        sender.setEmail(strCell(row, 11));

        ShipmentInformation info = new ShipmentInformation();
        info.setService(strCell(row, 12));
        info.setNumberOfItems((int) numCell(row, 13));
        info.setWeight(numCell(row, 14));
        info.setGoodsDescription(strCell(row, 15));
        info.setCurrency(strCell(row, 16));
        info.setCodAmount(numCell(row, 17));
        info.setHsCode(strCell(row, 18));

        ReceiverInformation receiver = new ReceiverInformation();
        receiver.setName(strCell(row, 19));
        receiver.setCountry(strCell(row, 20));
        receiver.setCity(strCell(row, 21));
        receiver.setAddress(strCell(row, 22));
        receiver.setPostalCode(strCell(row, 23));
        receiver.setPhone(strCell(row, 24));
        receiver.setEmail(strCell(row, 25));

        Shipment s = new Shipment();
        s.setAutoGenerate(autoGenerate);
        s.setAwbno(awbno);
        s.setAltRefNo(strCell(row, 2));
        s.setSenderInformation(sender);
        s.setShipmentInformation(info);
        s.setReceiverInformation(receiver);
        return s;
    }

    private String strCell(Row row, int col) {
        Cell cell = row.getCell(col, Row.MissingCellPolicy.RETURN_BLANK_AS_NULL);
        if (cell == null) return null;
        if (cell.getCellType() == CellType.NUMERIC) {
            return String.valueOf((long) cell.getNumericCellValue());
        }
        return cell.getStringCellValue().trim();
    }

    private double numCell(Row row, int col) {
        Cell cell = row.getCell(col, Row.MissingCellPolicy.RETURN_BLANK_AS_NULL);
        if (cell == null) return 0;
        if (cell.getCellType() == CellType.NUMERIC) return cell.getNumericCellValue();
        try { return Double.parseDouble(cell.getStringCellValue().trim()); } catch (Exception e) { return 0; }
    }

    private boolean boolCell(Row row, int col) {
        Cell cell = row.getCell(col, Row.MissingCellPolicy.RETURN_BLANK_AS_NULL);
        if (cell == null) return false;
        if (cell.getCellType() == CellType.BOOLEAN) return cell.getBooleanCellValue();
        if (cell.getCellType() == CellType.STRING) return Boolean.parseBoolean(cell.getStringCellValue().trim());
        return false;
    }
}
