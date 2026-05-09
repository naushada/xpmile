package com.xpmile.onprem.service;

import com.xpmile.onprem.config.BackendConfig;
import com.xpmile.onprem.model.Account;
import org.springframework.stereotype.Service;
import org.springframework.web.client.RestClientException;
import org.springframework.web.client.RestTemplate;

@Service
public class AuthService {

    private final RestTemplate restTemplate;
    private final BackendConfig backendConfig;

    public AuthService(RestTemplate restTemplate, BackendConfig backendConfig) {
        this.restTemplate = restTemplate;
        this.backendConfig = backendConfig;
    }

    public Account login(String username, String password) {
        String url = backendConfig.getBaseUrl()
                + "/api/v1/account/account?userId=" + username
                + "&password=" + password;
        try {
            Account[] accounts = restTemplate.getForObject(url, Account[].class);
            if (accounts == null || accounts.length == 0) {
                throw new AuthException("Invalid credentials");
            }
            return accounts[0];
        } catch (AuthException e) {
            throw e;
        } catch (RestClientException e) {
            throw new AuthException("Backend unavailable", e);
        }
    }
}
