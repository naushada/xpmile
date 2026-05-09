package com.xpmile.onprem.model;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import java.util.List;

@JsonIgnoreProperties(ignoreUnknown = true)
public class ShipmentInformation {
    private List<Object> activity;
    private String skuNo;
    private String service;
    private int numberOfItems;
    private String goodsDescription;
    private double goodsValue;
    private double customsValue;
    private double codAmount;
    private String vat;
    private String currency;
    private double weight;
    private String weightUnits;
    private double cubicWeight;
    private String createdOn;
    private String createdBy;
    private String hsCode;

    public ShipmentInformation() {}

    public List<Object> getActivity() { return activity; }
    public void setActivity(List<Object> activity) { this.activity = activity; }

    public String getSkuNo() { return skuNo; }
    public void setSkuNo(String skuNo) { this.skuNo = skuNo; }

    public String getService() { return service; }
    public void setService(String service) { this.service = service; }

    public int getNumberOfItems() { return numberOfItems; }
    public void setNumberOfItems(int numberOfItems) { this.numberOfItems = numberOfItems; }

    public String getGoodsDescription() { return goodsDescription; }
    public void setGoodsDescription(String goodsDescription) { this.goodsDescription = goodsDescription; }

    public double getGoodsValue() { return goodsValue; }
    public void setGoodsValue(double goodsValue) { this.goodsValue = goodsValue; }

    public double getCustomsValue() { return customsValue; }
    public void setCustomsValue(double customsValue) { this.customsValue = customsValue; }

    public double getCodAmount() { return codAmount; }
    public void setCodAmount(double codAmount) { this.codAmount = codAmount; }

    public String getVat() { return vat; }
    public void setVat(String vat) { this.vat = vat; }

    public String getCurrency() { return currency; }
    public void setCurrency(String currency) { this.currency = currency; }

    public double getWeight() { return weight; }
    public void setWeight(double weight) { this.weight = weight; }

    public String getWeightUnits() { return weightUnits; }
    public void setWeightUnits(String weightUnits) { this.weightUnits = weightUnits; }

    public double getCubicWeight() { return cubicWeight; }
    public void setCubicWeight(double cubicWeight) { this.cubicWeight = cubicWeight; }

    public String getCreatedOn() { return createdOn; }
    public void setCreatedOn(String createdOn) { this.createdOn = createdOn; }

    public String getCreatedBy() { return createdBy; }
    public void setCreatedBy(String createdBy) { this.createdBy = createdBy; }

    public String getHsCode() { return hsCode; }
    public void setHsCode(String hsCode) { this.hsCode = hsCode; }
}
