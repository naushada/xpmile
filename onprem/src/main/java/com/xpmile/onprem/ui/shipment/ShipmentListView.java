package com.xpmile.onprem.ui.shipment;

import com.vaadin.flow.component.AttachEvent;
import com.vaadin.flow.component.DetachEvent;
import com.vaadin.flow.component.button.Button;
import com.vaadin.flow.component.button.ButtonVariant;
import com.vaadin.flow.component.datepicker.DatePicker;
import com.vaadin.flow.component.grid.Grid;
import com.vaadin.flow.component.html.H3;
import com.vaadin.flow.component.html.Span;
import com.vaadin.flow.component.icon.Icon;
import com.vaadin.flow.component.icon.VaadinIcon;
import com.vaadin.flow.component.notification.Notification;
import com.vaadin.flow.component.notification.NotificationVariant;
import com.vaadin.flow.component.orderedlayout.FlexComponent;
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

/**
 * Read-only shipment list for the on-prem admin tool. Polls the backend
 * every 60 s while the view is attached so operators see new shipments
 * appear without having to refresh.
 *
 * Note: this view is read-only by design (no create/modify). Daily ops
 * flows live in the Angular cloud app.
 */
@Route(value = "shipments", layout = MainLayout.class)
@PageTitle("Shipments | xpmile")
public class ShipmentListView extends VerticalLayout {

    private static final int POLL_INTERVAL_MS = 60_000;
    private static final DateTimeFormatter FMT = DateTimeFormatter.ofPattern("yyyy-MM-dd");

    private final ShipmentService shipmentService;
    private final Grid<Shipment> grid = new Grid<>(Shipment.class, false);

    private final DatePicker fromDate = new DatePicker("From");
    private final DatePicker toDate   = new DatePicker("To");
    private final TextField  accountCode = new TextField("Account Code");

    private final Span lastUpdated = new Span();
    private final Span livePulse   = new Span();

    public ShipmentListView(ShipmentService shipmentService) {
        this.shipmentService = shipmentService;

        setSizeFull();
        getStyle().set("padding", "16px");

        add(buildHeader(), buildFilters(), buildGrid());
    }

    private com.vaadin.flow.component.Component buildHeader() {
        H3 title = new H3("Shipments");
        title.getStyle().set("margin", "0");

        livePulse.setText("● LIVE");
        livePulse.getStyle()
                .set("color", "#dc2626")
                .set("font-weight", "600")
                .set("font-size", "var(--lumo-font-size-s)")
                .set("background", "rgba(220, 38, 38, 0.08)")
                .set("padding", "2px 10px")
                .set("border-radius", "10px");

        lastUpdated.getStyle()
                .set("color", "var(--lumo-secondary-text-color)")
                .set("font-size", "var(--lumo-font-size-s)");

        HorizontalLayout row = new HorizontalLayout(title, livePulse, lastUpdated);
        row.setDefaultVerticalComponentAlignment(FlexComponent.Alignment.CENTER);
        row.setSpacing(true);
        row.getStyle().set("margin-bottom", "10px");
        return row;
    }

    private com.vaadin.flow.component.Component buildFilters() {
        fromDate.setValue(LocalDate.now().minusDays(30));
        toDate.setValue(LocalDate.now());
        accountCode.setPlaceholder("Optional");

        fromDate.setId("fromDate");
        toDate.setId("toDate");
        accountCode.setId("accountCode");

        Button search = new Button("Search", new Icon(VaadinIcon.SEARCH));
        search.addThemeVariants(ButtonVariant.LUMO_PRIMARY);
        search.setId("search");
        search.addClickListener(e -> refresh());

        HorizontalLayout filters = new HorizontalLayout(fromDate, toDate, accountCode, search);
        filters.setDefaultVerticalComponentAlignment(FlexComponent.Alignment.BASELINE);
        return filters;
    }

    private com.vaadin.flow.component.Component buildGrid() {
        grid.addColumn(Shipment::getAwbno).setHeader("AWB No").setAutoWidth(true).setSortable(true);
        grid.addColumn(Shipment::getAltRefNo).setHeader("Alt Ref").setAutoWidth(true);
        grid.addColumn(s -> s.getSenderInformation() != null
                ? s.getSenderInformation().getName() : "").setHeader("Sender").setAutoWidth(true);
        grid.addColumn(s -> s.getReceiverInformation() != null
                ? s.getReceiverInformation().getName() : "").setHeader("Receiver").setAutoWidth(true);
        grid.addColumn(s -> s.getShipmentInformation() != null
                ? s.getShipmentInformation().getService() : "").setHeader("Service").setAutoWidth(true);
        grid.addColumn(s -> s.getShipmentInformation() != null
                ? s.getShipmentInformation().getWeight() : "").setHeader("Weight").setAutoWidth(true);
        grid.addColumn(s -> s.getShipmentInformation() != null
                ? s.getShipmentInformation().getCreatedOn() : "").setHeader("Created On").setAutoWidth(true);
        grid.setSizeFull();
        return grid;
    }

    private void refresh() {
        try {
            String from = fromDate.getValue() != null ? fromDate.getValue().format(FMT) : null;
            String to   = toDate.getValue()   != null ? toDate.getValue().format(FMT)   : null;
            List<Shipment> shipments = shipmentService.getShipments(from, to, accountCode.getValue());
            grid.setItems(shipments);
            lastUpdated.setText("· refreshed "
                    + java.time.LocalTime.now().withNano(0));
        } catch (Exception ex) {
            Notification n = Notification.show(
                    "Failed to load shipments: " + ex.getMessage(),
                    4000, Notification.Position.BOTTOM_START);
            n.addThemeVariants(NotificationVariant.LUMO_ERROR);
        }
    }

    /**
     * Vaadin's UI poll fires while the view is attached. We hijack the
     * existing 30s status poll's listener by registering our own; the
     * poll-interval set by MainLayout is the floor (30s), so a 60s
     * refresh skips every other tick.
     */
    private long lastRefreshMs = 0L;
    private com.vaadin.flow.shared.Registration pollReg;

    @Override
    protected void onAttach(AttachEvent attachEvent) {
        super.onAttach(attachEvent);
        refresh();
        pollReg = attachEvent.getUI().addPollListener(e -> {
            long now = System.currentTimeMillis();
            if (now - lastRefreshMs >= POLL_INTERVAL_MS) {
                lastRefreshMs = now;
                refresh();
            }
        });
    }

    @Override
    protected void onDetach(DetachEvent detachEvent) {
        if (pollReg != null) pollReg.remove();
        super.onDetach(detachEvent);
    }
}
