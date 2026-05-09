package com.xpmile.onprem.model;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;

@JsonIgnoreProperties(ignoreUnknown = true)
public class CustomerInfo {
    private String companyName;
    private String quotedAmount;
    private String tradingLicense;
    private String vat;
    private String currency;
    private String bankAccountNumber;
    private String iban;

    public CustomerInfo() {}

    public String getCompanyName() { return companyName; }
    public void setCompanyName(String companyName) { this.companyName = companyName; }

    public String getQuotedAmount() { return quotedAmount; }
    public void setQuotedAmount(String quotedAmount) { this.quotedAmount = quotedAmount; }

    public String getTradingLicense() { return tradingLicense; }
    public void setTradingLicense(String tradingLicense) { this.tradingLicense = tradingLicense; }

    public String getVat() { return vat; }
    public void setVat(String vat) { this.vat = vat; }

    public String getCurrency() { return currency; }
    public void setCurrency(String currency) { this.currency = currency; }

    public String getBankAccountNumber() { return bankAccountNumber; }
    public void setBankAccountNumber(String bankAccountNumber) { this.bankAccountNumber = bankAccountNumber; }

    public String getIban() { return iban; }
    public void setIban(String iban) { this.iban = iban; }
}
