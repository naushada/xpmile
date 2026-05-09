package com.xpmile.onprem.model;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonProperty;

@JsonIgnoreProperties(ignoreUnknown = true)
public class Account {
    @JsonProperty("isAccountCodeAutoGen")
    private boolean isAccountCodeAutoGen;
    private String awbPrefix;
    private LoginCredentials loginCredentials;
    private PersonalInfo personalInfo;
    private CustomerInfo customerInfo;

    public Account() {}

    public boolean isAccountCodeAutoGen() { return isAccountCodeAutoGen; }
    public void setAccountCodeAutoGen(boolean accountCodeAutoGen) { isAccountCodeAutoGen = accountCodeAutoGen; }

    public String getAwbPrefix() { return awbPrefix; }
    public void setAwbPrefix(String awbPrefix) { this.awbPrefix = awbPrefix; }

    public LoginCredentials getLoginCredentials() { return loginCredentials; }
    public void setLoginCredentials(LoginCredentials loginCredentials) { this.loginCredentials = loginCredentials; }

    public PersonalInfo getPersonalInfo() { return personalInfo; }
    public void setPersonalInfo(PersonalInfo personalInfo) { this.personalInfo = personalInfo; }

    public CustomerInfo getCustomerInfo() { return customerInfo; }
    public void setCustomerInfo(CustomerInfo customerInfo) { this.customerInfo = customerInfo; }
}
