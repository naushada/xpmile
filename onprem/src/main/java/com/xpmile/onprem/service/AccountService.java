package com.xpmile.onprem.service;

import com.xpmile.onprem.config.BackendConfig;
import com.xpmile.onprem.model.Account;
import com.xpmile.onprem.model.LoginCredentials;
import org.springframework.stereotype.Service;
import org.springframework.web.client.HttpClientErrorException;
import org.springframework.web.client.RestTemplate;
import org.springframework.web.util.UriComponentsBuilder;

import java.util.Arrays;
import java.util.Collections;
import java.util.List;

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

    /**
     * Fetch every account. Backend GET /api/v1/account/account without
     * filter params returns the whole collection. Returns an empty list
     * on 4xx (e.g. when zero docs exist).
     */
    public List<Account> getAllAccounts() {
        String url = backendConfig.getBaseUrl() + "/api/v1/account/account";
        try {
            Account[] result = restTemplate.getForObject(url, Account[].class);
            if (result == null) return Collections.emptyList();
            return Arrays.asList(result);
        } catch (HttpClientErrorException e) {
            return Collections.emptyList();
        }
    }

    public void updateAccount(String userId, Account account) {
        String url = UriComponentsBuilder
                .fromHttpUrl(backendConfig.getBaseUrl() + "/api/v1/account/account")
                .queryParam("userId", userId)
                .toUriString();
        restTemplate.put(url, account);
    }

    /**
     * Delete an account by its accountCode. The C++ backend's
     * handle_DELETE branch for /api/v1/account/account requires the
     * accountCode query parameter — calling without it returns 400,
     * which prevents an empty-filter from wiping the whole collection.
     */
    public void deleteAccount(String accountCode) {
        String url = UriComponentsBuilder
                .fromHttpUrl(backendConfig.getBaseUrl() + "/api/v1/account/account")
                .queryParam("accountCode", accountCode)
                .toUriString();
        restTemplate.delete(url);
    }

    /**
     * Reset a user's password from the on-prem admin tool.
     *
     * Sends only the new plaintext password in the PUT body — the backend
     * (handle_account_PUT) hashes it via PBKDF2-SHA256 before storing. The
     * password never appears in the request URL, so it doesn't end up in
     * router or proxy logs.
     *
     * This is the primary justification for the on-prem Vaadin tool: the
     * cloud-deployed Angular app does not expose a self-service "forgot
     * password" flow, so a forgotten-password recovery has to happen
     * here, where the operator is physically on the customer's premises.
     */
    public void resetPassword(String userId, String newPassword) {
        String url = UriComponentsBuilder
                .fromHttpUrl(backendConfig.getBaseUrl() + "/api/v1/account/account")
                .queryParam("userId", userId)
                .toUriString();

        Account body = new Account();
        LoginCredentials lc = new LoginCredentials();
        lc.setAccountPassword(newPassword);
        body.setLoginCredentials(lc);

        restTemplate.put(url, body);
    }
}
