package com.xpmile.onprem.ui.shipment;

import com.vaadin.flow.component.checkbox.Checkbox;
import com.vaadin.flow.component.combobox.ComboBox;
import com.vaadin.flow.component.formlayout.FormLayout;
import com.vaadin.flow.component.html.H4;
import com.vaadin.flow.component.orderedlayout.VerticalLayout;
import com.vaadin.flow.component.textfield.NumberField;
import com.vaadin.flow.component.textfield.TextField;
import com.xpmile.onprem.model.*;

import java.util.List;

/**
 * Reusable shipment form used by CreateShipmentView and ModifyShipmentView.
 */
public class ShipmentForm extends VerticalLayout {

    final Checkbox autoGenerate = new Checkbox("Auto-generate AWB");
    final TextField awbno = new TextField("AWB Number");
    final TextField altRefNo = new TextField("Alt Ref No");

    // Sender
    final TextField senderAccountNo = new TextField("Account No");
    final TextField senderName = new TextField("Name");
    final TextField senderCompany = new TextField("Company");
    final TextField senderCountry = new TextField("Country");
    final TextField senderCity = new TextField("City");
    final TextField senderState = new TextField("State");
    final TextField senderAddress = new TextField("Address");
    final TextField senderPostal = new TextField("Postal Code");
    final TextField senderContact = new TextField("Contact");
    final TextField senderPhone = new TextField("Phone");
    final TextField senderEmail = new TextField("Email");
    final TextField senderTaxId = new TextField("Tax ID");

    // Shipment info
    final TextField skuNo = new TextField("SKU No");
    final ComboBox<String> service = new ComboBox<>("Service");
    final NumberField numberOfItems = new NumberField("Items");
    final TextField goodsDescription = new TextField("Goods Description");
    final NumberField weight = new NumberField("Weight");
    final ComboBox<String> weightUnits = new ComboBox<>("Weight Units");
    final NumberField codAmount = new NumberField("COD Amount");
    final TextField currency = new TextField("Currency");
    final NumberField customsValue = new NumberField("Customs Value");
    final TextField hsCode = new TextField("HS Code");

    // Receiver
    final TextField receiverName = new TextField("Name");
    final TextField receiverCountry = new TextField("Country");
    final TextField receiverCity = new TextField("City");
    final TextField receiverState = new TextField("State");
    final TextField receiverAddress = new TextField("Address");
    final TextField receiverPostal = new TextField("Postal Code");
    final TextField receiverContact = new TextField("Contact");
    final TextField receiverPhone = new TextField("Phone");
    final TextField receiverEmail = new TextField("Email");

    public ShipmentForm() {
        service.setItems(List.of("Standard", "Express", "Economy", "Overnight"));
        weightUnits.setItems(List.of("kg", "lb"));
        weightUnits.setValue("kg");

        autoGenerate.addValueChangeListener(e -> awbno.setEnabled(!e.getValue()));
        autoGenerate.setId("autoGenerate");
        awbno.setId("awbno");
        senderName.setId("senderName");
        senderName.setRequiredIndicatorVisible(true);

        FormLayout awbSection = section("AWB",
                autoGenerate, awbno, altRefNo);

        FormLayout senderSection = section("Sender Information",
                senderAccountNo, senderName, senderCompany, senderCountry,
                senderCity, senderState, senderAddress, senderPostal,
                senderContact, senderPhone, senderEmail, senderTaxId);

        FormLayout infoSection = section("Shipment Information",
                skuNo, service, numberOfItems, goodsDescription,
                weight, weightUnits, codAmount, currency, customsValue, hsCode);

        FormLayout receiverSection = section("Receiver Information",
                receiverName, receiverCountry, receiverCity, receiverState,
                receiverAddress, receiverPostal, receiverContact, receiverPhone, receiverEmail);

        setPadding(false);
        add(awbSection, senderSection, infoSection, receiverSection);
    }

    private FormLayout section(String title, com.vaadin.flow.component.Component... fields) {
        FormLayout form = new FormLayout();
        form.getElement().insertChild(0, new H4(title).getElement());
        form.add(fields);
        form.setResponsiveSteps(
                new FormLayout.ResponsiveStep("0", 1),
                new FormLayout.ResponsiveStep("500px", 2),
                new FormLayout.ResponsiveStep("900px", 3));
        return form;
    }

    public void populate(Shipment s) {
        if (s == null) return;
        autoGenerate.setValue(s.isAutoGenerate());
        awbno.setValue(nvl(s.getAwbno()));
        awbno.setEnabled(!s.isAutoGenerate());
        altRefNo.setValue(nvl(s.getAltRefNo()));

        SenderInformation si = s.getSenderInformation();
        if (si != null) {
            senderAccountNo.setValue(nvl(si.getAccountNo()));
            senderName.setValue(nvl(si.getName()));
            senderCompany.setValue(nvl(si.getCompanyName()));
            senderCountry.setValue(nvl(si.getCountry()));
            senderCity.setValue(nvl(si.getCity()));
            senderState.setValue(nvl(si.getState()));
            senderAddress.setValue(nvl(si.getAddress()));
            senderPostal.setValue(nvl(si.getPostalCode()));
            senderContact.setValue(nvl(si.getContact()));
            senderPhone.setValue(nvl(si.getPhoneNumber()));
            senderEmail.setValue(nvl(si.getEmail()));
            senderTaxId.setValue(nvl(si.getReceivingTaxId()));
        }

        ShipmentInformation info = s.getShipmentInformation();
        if (info != null) {
            skuNo.setValue(nvl(info.getSkuNo()));
            if (info.getService() != null) service.setValue(info.getService());
            numberOfItems.setValue((double) info.getNumberOfItems());
            goodsDescription.setValue(nvl(info.getGoodsDescription()));
            weight.setValue(info.getWeight());
            if (info.getWeightUnits() != null) weightUnits.setValue(info.getWeightUnits());
            codAmount.setValue(info.getCodAmount());
            currency.setValue(nvl(info.getCurrency()));
            customsValue.setValue(info.getCustomsValue());
            hsCode.setValue(nvl(info.getHsCode()));
        }

        ReceiverInformation ri = s.getReceiverInformation();
        if (ri != null) {
            receiverName.setValue(nvl(ri.getName()));
            receiverCountry.setValue(nvl(ri.getCountry()));
            receiverCity.setValue(nvl(ri.getCity()));
            receiverState.setValue(nvl(ri.getState()));
            receiverAddress.setValue(nvl(ri.getAddress()));
            receiverPostal.setValue(nvl(ri.getPostalCode()));
            receiverContact.setValue(nvl(ri.getContact()));
            receiverPhone.setValue(nvl(ri.getPhone()));
            receiverEmail.setValue(nvl(ri.getEmail()));
        }
    }

    public Shipment toShipment() {
        Shipment s = new Shipment();
        s.setAutoGenerate(autoGenerate.getValue());
        s.setAwbno(awbno.getValue());
        s.setAltRefNo(altRefNo.getValue());

        SenderInformation si = new SenderInformation();
        si.setAccountNo(senderAccountNo.getValue());
        si.setName(senderName.getValue());
        si.setCompanyName(senderCompany.getValue());
        si.setCountry(senderCountry.getValue());
        si.setCity(senderCity.getValue());
        si.setState(senderState.getValue());
        si.setAddress(senderAddress.getValue());
        si.setPostalCode(senderPostal.getValue());
        si.setContact(senderContact.getValue());
        si.setPhoneNumber(senderPhone.getValue());
        si.setEmail(senderEmail.getValue());
        si.setReceivingTaxId(senderTaxId.getValue());
        s.setSenderInformation(si);

        ShipmentInformation info = new ShipmentInformation();
        info.setSkuNo(skuNo.getValue());
        info.setService(service.getValue());
        info.setNumberOfItems(numberOfItems.getValue() != null ? numberOfItems.getValue().intValue() : 0);
        info.setGoodsDescription(goodsDescription.getValue());
        info.setWeight(weight.getValue() != null ? weight.getValue() : 0);
        info.setWeightUnits(weightUnits.getValue());
        info.setCodAmount(codAmount.getValue() != null ? codAmount.getValue() : 0);
        info.setCurrency(currency.getValue());
        info.setCustomsValue(customsValue.getValue() != null ? customsValue.getValue() : 0);
        info.setHsCode(hsCode.getValue());
        s.setShipmentInformation(info);

        ReceiverInformation ri = new ReceiverInformation();
        ri.setName(receiverName.getValue());
        ri.setCountry(receiverCountry.getValue());
        ri.setCity(receiverCity.getValue());
        ri.setState(receiverState.getValue());
        ri.setAddress(receiverAddress.getValue());
        ri.setPostalCode(receiverPostal.getValue());
        ri.setContact(receiverContact.getValue());
        ri.setPhone(receiverPhone.getValue());
        ri.setEmail(receiverEmail.getValue());
        s.setReceiverInformation(ri);

        return s;
    }

    public boolean validate() {
        return !senderName.getValue().isBlank();
    }

    public void reset() {
        autoGenerate.clear();
        awbno.clear(); awbno.setEnabled(true);
        altRefNo.clear();
        senderAccountNo.clear(); senderName.clear(); senderCompany.clear();
        senderCountry.clear(); senderCity.clear(); senderState.clear();
        senderAddress.clear(); senderPostal.clear();
        senderContact.clear(); senderPhone.clear(); senderEmail.clear(); senderTaxId.clear();
        skuNo.clear(); service.clear(); numberOfItems.clear();
        goodsDescription.clear(); weight.clear(); weightUnits.setValue("kg");
        codAmount.clear(); currency.clear(); customsValue.clear(); hsCode.clear();
        receiverName.clear(); receiverCountry.clear(); receiverCity.clear();
        receiverState.clear(); receiverAddress.clear(); receiverPostal.clear();
        receiverContact.clear(); receiverPhone.clear(); receiverEmail.clear();
    }

    private String nvl(String s) { return s != null ? s : ""; }
}
