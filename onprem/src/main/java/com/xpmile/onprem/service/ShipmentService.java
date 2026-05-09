package com.xpmile.onprem.service;

import com.xpmile.onprem.config.BackendConfig;
import com.xpmile.onprem.model.BulkResult;
import com.xpmile.onprem.model.Shipment;
import org.springframework.http.HttpStatus;
import org.springframework.stereotype.Service;
import org.springframework.web.client.HttpClientErrorException;
import org.springframework.web.client.RestTemplate;
import org.springframework.web.util.UriComponentsBuilder;

import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Map;

@Service
public class ShipmentService {

    private final RestTemplate restTemplate;
    private final BackendConfig backendConfig;

    public ShipmentService(RestTemplate restTemplate, BackendConfig backendConfig) {
        this.restTemplate = restTemplate;
        this.backendConfig = backendConfig;
    }

    public List<Shipment> getShipments(String fromDate, String toDate, String accountCode) {
        UriComponentsBuilder builder = UriComponentsBuilder
                .fromHttpUrl(backendConfig.getBaseUrl() + "/api/v1/shipment/shipping")
                .queryParam("fromDate", fromDate)
                .queryParam("toDate", toDate);
        if (accountCode != null && !accountCode.isBlank()) {
            builder.queryParam("accountCode", accountCode);
        }
        try {
            Shipment[] result = restTemplate.getForObject(builder.toUriString(), Shipment[].class);
            if (result == null) return Collections.emptyList();
            return Arrays.asList(result);
        } catch (HttpClientErrorException e) {
            // backend returns 400 when no shipments match — treat as empty result
            return Collections.emptyList();
        }
    }

    public String createShipment(Shipment shipment) {
        Map<?, ?> response = restTemplate.postForObject(
                backendConfig.getBaseUrl() + "/api/v1/shipment/shipping",
                Map.of("shipment", shipment),
                Map.class);
        if (response != null && response.containsKey("awbno")) {
            return response.get("awbno").toString();
        }
        return null;
    }

    public BulkResult createBulkShipment(List<Shipment> shipments) {
        BulkResult result = restTemplate.postForObject(
                backendConfig.getBaseUrl() + "/api/v1/shipment/bulk/shipping",
                shipments,
                BulkResult.class);
        return result != null ? result : new BulkResult(0, 0);
    }

    public void updateShipment(String awbno, Shipment shipment) {
        String url = UriComponentsBuilder
                .fromHttpUrl(backendConfig.getBaseUrl() + "/api/v1/shipment/shipping")
                .queryParam("shipmentNo", awbno)
                .queryParam("isSingleShipment", true)
                .toUriString();
        restTemplate.put(url, Map.of("shipment", shipment));
    }

    public Shipment getShipmentByAwb(String awbno) {
        String url = UriComponentsBuilder
                .fromHttpUrl(backendConfig.getBaseUrl() + "/api/v1/shipment/shipping")
                .queryParam("awbNo", awbno)
                .toUriString();
        try {
            Shipment[] result = restTemplate.getForObject(url, Shipment[].class);
            if (result == null || result.length == 0) return null;
            return result[0];
        } catch (HttpClientErrorException e) {
            return null;
        }
    }
}
