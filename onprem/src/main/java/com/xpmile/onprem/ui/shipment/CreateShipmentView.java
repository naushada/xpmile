package com.xpmile.onprem.ui.shipment;

import com.vaadin.flow.component.button.Button;
import com.vaadin.flow.component.button.ButtonVariant;
import com.vaadin.flow.component.notification.Notification;
import com.vaadin.flow.component.notification.NotificationVariant;
import com.vaadin.flow.component.orderedlayout.HorizontalLayout;
import com.vaadin.flow.component.orderedlayout.VerticalLayout;
import com.vaadin.flow.router.PageTitle;
import com.vaadin.flow.router.Route;
import com.xpmile.onprem.model.Shipment;
import com.xpmile.onprem.service.ShipmentService;
import com.xpmile.onprem.ui.MainLayout;

@Route(value = "shipments/create", layout = MainLayout.class)
@PageTitle("Create Shipment | xpmile")
public class CreateShipmentView extends VerticalLayout {

    private final ShipmentService shipmentService;
    private final ShipmentForm form = new ShipmentForm();

    public CreateShipmentView(ShipmentService shipmentService) {
        this.shipmentService = shipmentService;

        Button save = new Button("Save");
        save.addThemeVariants(ButtonVariant.LUMO_PRIMARY);
        save.setId("save");
        save.addClickListener(e -> submit());

        Button reset = new Button("Reset");
        reset.addClickListener(e -> form.reset());

        HorizontalLayout actions = new HorizontalLayout(save, reset);

        add(form, actions);
        setSizeFull();
        setScrollable(true);
    }

    private void setScrollable(boolean b) {
        getStyle().set("overflow-y", b ? "auto" : "hidden");
    }

    private void submit() {
        if (!form.validate()) {
            Notification.show("Sender name is required", 3000, Notification.Position.MIDDLE);
            return;
        }
        try {
            Shipment s = form.toShipment();
            String awb = shipmentService.createShipment(s);
            Notification n = Notification.show(
                    "Shipment created. AWB: " + awb, 4000, Notification.Position.BOTTOM_START);
            n.addThemeVariants(NotificationVariant.LUMO_SUCCESS);
            n.setId("success-notification");
            form.reset();
        } catch (Exception ex) {
            Notification n = Notification.show("Error: " + ex.getMessage(), 4000, Notification.Position.BOTTOM_START);
            n.addThemeVariants(NotificationVariant.LUMO_ERROR);
        }
    }
}
