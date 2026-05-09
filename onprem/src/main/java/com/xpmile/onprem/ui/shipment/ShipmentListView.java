package com.xpmile.onprem.ui.shipment;

import com.vaadin.flow.component.button.Button;
import com.vaadin.flow.component.button.ButtonVariant;
import com.vaadin.flow.component.datepicker.DatePicker;
import com.vaadin.flow.component.grid.Grid;
import com.vaadin.flow.component.notification.Notification;
import com.vaadin.flow.component.notification.NotificationVariant;
import com.vaadin.flow.component.orderedlayout.HorizontalLayout;
import com.vaadin.flow.component.orderedlayout.VerticalLayout;
import com.vaadin.flow.component.textfield.TextField;
import com.vaadin.flow.router.PageTitle;
import com.vaadin.flow.router.Route;
import com.xpmile.onprem.model.Shipment;
import com.xpmile.onprem.service.ShipmentService;
import com.xpmile.onprem.ui.MainLayout;

import java.time.LocalDate;
import java.time.format.DateTimeFormatter;
import java.util.List;

@Route(value = "shipments", layout = MainLayout.class)
@PageTitle("Shipments | xpmile")
public class ShipmentListView extends VerticalLayout {

    private final ShipmentService shipmentService;
    private final Grid<Shipment> grid = new Grid<>(Shipment.class, false);
    private static final DateTimeFormatter FMT = DateTimeFormatter.ofPattern("yyyy-MM-dd");

    public ShipmentListView(ShipmentService shipmentService) {
        this.shipmentService = shipmentService;

        DatePicker fromDate = new DatePicker("From");
        fromDate.setValue(LocalDate.now().minusDays(30));
        fromDate.setId("fromDate");

        DatePicker toDate = new DatePicker("To");
        toDate.setValue(LocalDate.now());
        toDate.setId("toDate");

        TextField accountCode = new TextField("Account Code");
        accountCode.setPlaceholder("Optional");
        accountCode.setId("accountCode");

        Button search = new Button("Search");
        search.addThemeVariants(ButtonVariant.LUMO_PRIMARY);
        search.setId("search");
        search.addClickListener(e -> loadShipments(
                fromDate.getValue() != null ? fromDate.getValue().format(FMT) : null,
                toDate.getValue() != null ? toDate.getValue().format(FMT) : null,
                accountCode.getValue()));

        HorizontalLayout filters = new HorizontalLayout(fromDate, toDate, accountCode, search);
        filters.setDefaultVerticalComponentAlignment(Alignment.BASELINE);

        configureGrid();

        add(filters, grid);
        setSizeFull();
    }

    private void configureGrid() {
        grid.addColumn(Shipment::getAwbno).setHeader("AWB No").setSortable(true);
        grid.addColumn(Shipment::getAltRefNo).setHeader("Alt Ref");
        grid.addColumn(s -> s.getSenderInformation() != null ? s.getSenderInformation().getName() : "")
                .setHeader("Sender");
        grid.addColumn(s -> s.getReceiverInformation() != null ? s.getReceiverInformation().getName() : "")
                .setHeader("Receiver");
        grid.addColumn(s -> s.getShipmentInformation() != null ? s.getShipmentInformation().getService() : "")
                .setHeader("Service");
        grid.addColumn(s -> s.getShipmentInformation() != null ? s.getShipmentInformation().getWeight() : "")
                .setHeader("Weight");
        grid.addColumn(s -> s.getShipmentInformation() != null ? s.getShipmentInformation().getCreatedOn() : "")
                .setHeader("Created On");
        grid.setSizeFull();

        grid.addItemClickListener(e -> {
            Shipment selected = e.getItem();
            getUI().ifPresent(ui -> ui.navigate("shipments/modify?awb=" + selected.getAwbno()));
        });
    }

    private void loadShipments(String from, String to, String accountCode) {
        try {
            List<Shipment> shipments = shipmentService.getShipments(from, to, accountCode);
            grid.setItems(shipments);
        } catch (Exception ex) {
            Notification n = Notification.show("Failed to load shipments: " + ex.getMessage(), 4000,
                    Notification.Position.BOTTOM_START);
            n.addThemeVariants(NotificationVariant.LUMO_ERROR);
        }
    }
}
