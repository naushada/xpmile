package com.xpmile.onprem.ui.shipment;

import com.vaadin.flow.component.button.Button;
import com.vaadin.flow.component.button.ButtonVariant;
import com.vaadin.flow.component.notification.Notification;
import com.vaadin.flow.component.notification.NotificationVariant;
import com.vaadin.flow.component.orderedlayout.HorizontalLayout;
import com.vaadin.flow.component.orderedlayout.VerticalLayout;
import com.vaadin.flow.component.textfield.TextField;
import com.vaadin.flow.router.*;
import com.xpmile.onprem.model.Shipment;
import com.xpmile.onprem.service.ShipmentService;
import com.xpmile.onprem.ui.MainLayout;

@Route(value = "shipments/modify", layout = MainLayout.class)
@PageTitle("Modify Shipment | xpmile")
public class ModifyShipmentView extends VerticalLayout implements HasUrlParameter<String> {

    private final ShipmentService shipmentService;
    private final TextField awbLookup = new TextField("AWB Number");
    private final ShipmentForm form = new ShipmentForm();
    private String loadedAwb;

    public ModifyShipmentView(ShipmentService shipmentService) {
        this.shipmentService = shipmentService;

        awbLookup.setId("awbLookup");
        awbLookup.setPlaceholder("Enter AWB to load");

        Button loadBtn = new Button("Load");
        loadBtn.setId("load");
        loadBtn.addClickListener(e -> loadShipment(awbLookup.getValue()));

        Button save = new Button("Save");
        save.addThemeVariants(ButtonVariant.LUMO_PRIMARY);
        save.setId("save");
        save.addClickListener(e -> save());

        HorizontalLayout lookup = new HorizontalLayout(awbLookup, loadBtn);
        lookup.setDefaultVerticalComponentAlignment(Alignment.BOTTOM);

        HorizontalLayout actions = new HorizontalLayout(save);

        add(lookup, form, actions);
        setSizeFull();
    }

    @Override
    public void setParameter(BeforeEvent event, @OptionalParameter String parameter) {
        Location location = event.getLocation();
        location.getQueryParameters().getParameters().getOrDefault("awb", java.util.List.of())
                .stream().findFirst().ifPresent(awb -> {
                    awbLookup.setValue(awb);
                    loadShipment(awb);
                });
    }

    private void loadShipment(String awb) {
        if (awb == null || awb.isBlank()) return;
        try {
            Shipment s = shipmentService.getShipmentByAwb(awb);
            if (s == null) {
                Notification.show("No shipment found for AWB: " + awb, 3000, Notification.Position.MIDDLE);
                return;
            }
            loadedAwb = awb;
            form.populate(s);
        } catch (Exception ex) {
            Notification.show("Error loading shipment: " + ex.getMessage(), 4000, Notification.Position.BOTTOM_START);
        }
    }

    private void save() {
        if (loadedAwb == null || loadedAwb.isBlank()) {
            Notification.show("Load a shipment first", 3000, Notification.Position.MIDDLE);
            return;
        }
        try {
            Shipment s = form.toShipment();
            shipmentService.updateShipment(loadedAwb, s);
            Notification n = Notification.show("Shipment updated", 3000, Notification.Position.BOTTOM_START);
            n.addThemeVariants(NotificationVariant.LUMO_SUCCESS);
        } catch (Exception ex) {
            Notification n = Notification.show("Error: " + ex.getMessage(), 4000, Notification.Position.BOTTOM_START);
            n.addThemeVariants(NotificationVariant.LUMO_ERROR);
        }
    }
}
