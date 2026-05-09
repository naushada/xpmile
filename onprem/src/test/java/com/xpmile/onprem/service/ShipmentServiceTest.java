package com.xpmile.onprem.service;

import com.github.tomakehurst.wiremock.WireMockServer;
import com.github.tomakehurst.wiremock.core.WireMockConfiguration;
import com.xpmile.onprem.config.BackendConfig;
import com.xpmile.onprem.model.BulkResult;
import com.xpmile.onprem.model.Shipment;
import org.junit.jupiter.api.*;
import org.springframework.web.client.RestTemplate;

import java.util.List;

import static com.github.tomakehurst.wiremock.client.WireMock.*;
import static org.assertj.core.api.Assertions.*;

class ShipmentServiceTest {

    static WireMockServer wireMock;
    ShipmentService service;

    @BeforeAll
    static void startWireMock() {
        wireMock = new WireMockServer(WireMockConfiguration.options().dynamicPort());
        wireMock.start();
    }

    @AfterAll
    static void stopWireMock() { wireMock.stop(); }

    @BeforeEach
    void setUp() {
        wireMock.resetAll();
        BackendConfig config = new BackendConfig() {
            @Override public String getBaseUrl() { return "http://localhost:" + wireMock.port(); }
        };
        service = new ShipmentService(new RestTemplate(), config);
    }

    @Test
    void getShipmentsWithDateRangeReturnsList() {
        wireMock.stubFor(get(urlPathEqualTo("/api/v1/shipment/shipping"))
                .willReturn(okJson("[{\"awbno\":\"A1\"},{\"awbno\":\"A2\"},{\"awbno\":\"A3\"}]")));

        List<Shipment> result = service.getShipments("2025-01-01", "2025-01-31", null);

        assertThat(result).hasSize(3);
        assertThat(result.get(0).getAwbno()).isEqualTo("A1");
    }

    @Test
    void getShipmentsWithAccountCodePassesParam() {
        wireMock.stubFor(get(urlPathEqualTo("/api/v1/shipment/shipping"))
                .withQueryParam("accountCode", equalTo("ACC001"))
                .willReturn(okJson("[{\"awbno\":\"X1\"}]")));

        List<Shipment> result = service.getShipments("2025-01-01", "2025-01-31", "ACC001");

        assertThat(result).hasSize(1);
        wireMock.verify(getRequestedFor(urlPathEqualTo("/api/v1/shipment/shipping"))
                .withQueryParam("accountCode", equalTo("ACC001")));
    }

    @Test
    void createShipmentPostsBodyAndReturnsAwb() {
        wireMock.stubFor(post(urlPathEqualTo("/api/v1/shipment/shipping"))
                .willReturn(okJson("{\"awbno\":\"XP0001\"}")));

        Shipment s = new Shipment();
        s.setAutoGenerate(true);
        String awb = service.createShipment(s);

        assertThat(awb).isEqualTo("XP0001");
        wireMock.verify(postRequestedFor(urlPathEqualTo("/api/v1/shipment/shipping"))
                .withRequestBody(containing("isAutoGenerate")));
    }

    @Test
    void createBulkShipmentReturnsResult() {
        wireMock.stubFor(post(urlPathEqualTo("/api/v1/shipment/bulk/shipping"))
                .willReturn(okJson("{\"created\":5,\"failed\":0}")));

        List<Shipment> shipments = List.of(new Shipment(), new Shipment(), new Shipment(),
                new Shipment(), new Shipment());
        BulkResult result = service.createBulkShipment(shipments);

        assertThat(result.getCreated()).isEqualTo(5);
        assertThat(result.getFailed()).isEqualTo(0);
    }

    @Test
    void updateShipmentSendsPut() {
        wireMock.stubFor(put(urlPathEqualTo("/api/v1/shipment/shipping"))
                .withQueryParam("shipmentNo", equalTo("XP001"))
                .willReturn(ok()));

        assertThatNoException().isThrownBy(() -> service.updateShipment("XP001", new Shipment()));
        wireMock.verify(putRequestedFor(urlPathEqualTo("/api/v1/shipment/shipping"))
                .withQueryParam("shipmentNo", equalTo("XP001")));
    }

    @Test
    void getShipmentsEmptyResponseReturnsEmptyList() {
        wireMock.stubFor(get(urlPathEqualTo("/api/v1/shipment/shipping"))
                .willReturn(okJson("[]")));

        List<Shipment> result = service.getShipments("2025-01-01", "2025-01-31", null);

        assertThat(result).isEmpty();
    }
}
