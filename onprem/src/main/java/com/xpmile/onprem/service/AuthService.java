package com.xpmile.onprem.service;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.xpmile.onprem.config.BackendConfig;
import com.xpmile.onprem.model.Account;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Service;
import org.springframework.web.client.RestClientException;
import org.springframework.web.client.RestTemplate;

@Service
public class AuthService {

    private static final Logger log = LoggerFactory.getLogger(AuthService.class);

    private final RestTemplate restTemplate;
    private final BackendConfig backendConfig;
    private final ObjectMapper objectMapper = new ObjectMapper();

    public AuthService(RestTemplate restTemplate, BackendConfig backendConfig) {
        this.restTemplate = restTemplate;
        this.backendConfig = backendConfig;
    }

    public Account login(String username, String password) {
        String url = backendConfig.getBaseUrl()
                + "/api/v1/account/account?userId=" + username
                + "&password=" + password;
        log.info("AuthService.login: POST to {}", url);
        try {
            String body = restTemplate.getForObject(url, String.class);
            log.info("AuthService.login: response body = {}", body);
            if (body == null || body.isBlank()) {
                throw new AuthException("Invalid credentials");
            }
            JsonNode node = objectMapper.readTree(body);
            if (node.isArray()) {
                if (node.isEmpty()) {
                    throw new AuthException("Invalid credentials");
                }
                return objectMapper.treeToValue(node.get(0), Account.class);
            }
            // error object e.g. {"status":"failure","cause":"...","error":404}
            if (node.has("cause") || node.has("error")) {
                String cause = node.path("cause").asText("Invalid credentials");
                throw new AuthException(cause);
            }
            // single account object returned directly
            return objectMapper.treeToValue(node, Account.class);
        } catch (AuthException e) {
            throw e;
        } catch (RestClientException e) {
            log.error("AuthService.login: RestClientException - {}", e.getMessage(), e);
            throw new AuthException("Backend unavailable", e);
        } catch (Exception e) {
            log.error("AuthService.login: unexpected exception - {}", e.getMessage(), e);
            throw new AuthException("Unexpected response from backend", e);
        }
    }
}
