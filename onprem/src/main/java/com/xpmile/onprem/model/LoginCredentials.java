package com.xpmile.onprem.model;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;

@JsonIgnoreProperties(ignoreUnknown = true)
public class LoginCredentials {
    private String accountCode;
    private String accountPassword;

    public LoginCredentials() {}

    public LoginCredentials(String accountCode, String accountPassword) {
        this.accountCode = accountCode;
        this.accountPassword = accountPassword;
    }

    public String getAccountCode() { return accountCode; }
    public void setAccountCode(String accountCode) { this.accountCode = accountCode; }

    public String getAccountPassword() { return accountPassword; }
    public void setAccountPassword(String accountPassword) { this.accountPassword = accountPassword; }
}
