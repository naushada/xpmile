package com.xpmile.onprem.model;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;

@JsonIgnoreProperties(ignoreUnknown = true)
public class Shipment {
    private boolean isAutoGenerate;
    private String awbno;
    private String altRefNo;
    private SenderInformation senderInformation;
    private ShipmentInformation shipmentInformation;
    private ReceiverInformation receiverInformation;

    public Shipment() {}

    public boolean isAutoGenerate() { return isAutoGenerate; }
    public void setAutoGenerate(boolean autoGenerate) { isAutoGenerate = autoGenerate; }

    public String getAwbno() { return awbno; }
    public void setAwbno(String awbno) { this.awbno = awbno; }

    public String getAltRefNo() { return altRefNo; }
    public void setAltRefNo(String altRefNo) { this.altRefNo = altRefNo; }

    public SenderInformation getSenderInformation() { return senderInformation; }
    public void setSenderInformation(SenderInformation senderInformation) { this.senderInformation = senderInformation; }

    public ShipmentInformation getShipmentInformation() { return shipmentInformation; }
    public void setShipmentInformation(ShipmentInformation shipmentInformation) { this.shipmentInformation = shipmentInformation; }

    public ReceiverInformation getReceiverInformation() { return receiverInformation; }
    public void setReceiverInformation(ReceiverInformation receiverInformation) { this.receiverInformation = receiverInformation; }
}
