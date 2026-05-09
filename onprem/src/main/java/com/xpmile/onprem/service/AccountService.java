package com.xpmile.onprem.service;

import com.xpmile.onprem.config.BackendConfig;
import com.xpmile.onprem.model.Account;
import org.springframework.stereotype.Service;
import org.springframework.web.client.RestTemplate;
import org.springframework.web.util.UriComponentsBuilder;

@Service
public class AccountService {

    private final RestTemplate restTemplate;
    private final BackendConfig backendConfig;

    public AccountService(RestTemplate restTemplate, BackendConfig backendConfig) {
        this.restTemplate = restTemplate;
        this.backendConfig = backendConfig;
    }

    public Account createAccount(Account account) {
        return restTemplate.postForObject(
                backendConfig.getBaseUrl() + "/api/v1/account/account",
                account,
                Account.class);
    }

    public Account getAccount(String userId) {
        String url = UriComponentsBuilder
                .fromHttpUrl(backendConfig.getBaseUrl() + "/api/v1/account/account")
                .queryParam("userId", userId)
                .toUriString();
        Account[] result = restTemplate.getForObject(url, Account[].class);
        if (result == null || result.length == 0) return null;
        return result[0];
    }

    public void updateAccount(String userId, Account account) {
        String url = UriComponentsBuilder
                .fromHttpUrl(backendConfig.getBaseUrl() + "/api/v1/account/account")
                .queryParam("userId", userId)
                .toUriString();
        restTemplate.put(url, account);
    }
}
