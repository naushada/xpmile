package com.xpmile.onprem.service;

import com.xpmile.onprem.model.Shipment;
import org.junit.jupiter.api.Test;

import java.io.InputStream;
import java.util.List;

import static org.assertj.core.api.Assertions.*;

class BulkShipmentParserTest {

    private final BulkShipmentParser parser = new BulkShipmentParser();

    private InputStream fixture(String name) {
        InputStream is = getClass().getResourceAsStream("/fixtures/" + name);
        assertThat(is).as("fixture file not found: " + name).isNotNull();
        return is;
    }

    @Test
    void validXlsxWithThreeRowsReturnsThreeShipments() {
        List<Shipment> shipments = parser.parse(fixture("bulk-3rows.xlsx"));

        assertThat(shipments).hasSize(3);
        assertThat(shipments.get(0).getAwbno()).isNotBlank();
        assertThat(shipments.get(0).getReceiverInformation().getName()).isNotBlank();
    }

    @Test
    void emptySheetReturnsEmptyList() {
        List<Shipment> shipments = parser.parse(fixture("bulk-empty.xlsx"));
        assertThat(shipments).isEmpty();
    }

    @Test
    void missingAwbWhenNotAutoGenerateThrowsParseException() {
        assertThatThrownBy(() -> parser.parse(fixture("bulk-missing-awb.xlsx")))
                .isInstanceOf(BulkShipmentParser.ParseException.class)
                .hasMessageContaining("awbno");
    }
}
