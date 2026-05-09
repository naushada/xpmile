package com.xpmile.onprem.ui.shipment;

import com.vaadin.flow.component.html.Paragraph;
import com.vaadin.flow.component.notification.Notification;
import com.vaadin.flow.component.notification.NotificationVariant;
import com.vaadin.flow.component.orderedlayout.VerticalLayout;
import com.vaadin.flow.component.upload.Upload;
import com.vaadin.flow.component.upload.receivers.MemoryBuffer;
import com.vaadin.flow.router.PageTitle;
import com.vaadin.flow.router.Route;
import com.xpmile.onprem.model.BulkResult;
import com.xpmile.onprem.model.Shipment;
import com.xpmile.onprem.service.BulkShipmentParser;
import com.xpmile.onprem.service.ShipmentService;
import com.xpmile.onprem.ui.MainLayout;

import java.util.List;

@Route(value = "shipments/bulk", layout = MainLayout.class)
@PageTitle("Bulk Upload | xpmile")
public class BulkShipmentView extends VerticalLayout {

    private final ShipmentService shipmentService;
    private final BulkShipmentParser parser;
    private final Paragraph status = new Paragraph();

    public BulkShipmentView(ShipmentService shipmentService, BulkShipmentParser parser) {
        this.shipmentService = shipmentService;
        this.parser = parser;

        MemoryBuffer buffer = new MemoryBuffer();
        Upload upload = new Upload(buffer);
        upload.setAcceptedFileTypes(".xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet");
        upload.setMaxFiles(1);
        upload.setDropLabel(new Paragraph("Drop Excel file here or click to upload"));

        upload.addSucceededListener(e -> {
            try {
                List<Shipment> shipments = parser.parse(buffer.getInputStream());
                if (shipments.isEmpty()) {
                    status.setText("No shipments found in file.");
                    return;
                }
                BulkResult result = shipmentService.createBulkShipment(shipments);
                String msg = "Uploaded " + shipments.size() + " rows. Created: "
                        + result.getCreated() + ", Failed: " + result.getFailed();
                status.setText(msg);
                Notification n = Notification.show(msg, 5000, Notification.Position.BOTTOM_START);
                n.addThemeVariants(result.getFailed() == 0
                        ? NotificationVariant.LUMO_SUCCESS
                        : NotificationVariant.LUMO_WARNING);
            } catch (BulkShipmentParser.ParseException ex) {
                status.setText("Parse error: " + ex.getMessage());
                Notification n = Notification.show("Parse error: " + ex.getMessage(), 5000,
                        Notification.Position.BOTTOM_START);
                n.addThemeVariants(NotificationVariant.LUMO_ERROR);
            } catch (Exception ex) {
                Notification n = Notification.show("Upload failed: " + ex.getMessage(), 5000,
                        Notification.Position.BOTTOM_START);
                n.addThemeVariants(NotificationVariant.LUMO_ERROR);
            }
        });

        add(new Paragraph("Upload an Excel file (.xlsx) with shipment data."), upload, status);
        setSpacing(true);
    }
}
