package com.xpmile.onprem.ui.dashboard;

import com.lowagie.text.Document;
import com.lowagie.text.Element;
import com.lowagie.text.Font;
import com.lowagie.text.FontFactory;
import com.lowagie.text.PageSize;
import com.lowagie.text.Paragraph;
import com.lowagie.text.Phrase;
import com.lowagie.text.pdf.PdfPCell;
import com.lowagie.text.pdf.PdfPTable;
import com.lowagie.text.pdf.PdfWriter;
import com.vaadin.flow.component.Component;
import com.vaadin.flow.component.UI;
import com.vaadin.flow.component.button.Button;
import com.vaadin.flow.component.button.ButtonVariant;
import com.vaadin.flow.component.datepicker.DatePicker;
import com.vaadin.flow.component.html.Div;
import com.vaadin.flow.component.html.H3;
import com.vaadin.flow.component.html.Span;
import com.vaadin.flow.component.icon.Icon;
import com.vaadin.flow.component.icon.VaadinIcon;
import com.vaadin.flow.component.notification.Notification;
import com.vaadin.flow.component.notification.NotificationVariant;
import com.vaadin.flow.component.orderedlayout.FlexComponent;
import com.vaadin.flow.component.orderedlayout.HorizontalLayout;
import com.vaadin.flow.component.orderedlayout.VerticalLayout;
import com.vaadin.flow.router.PageTitle;
import com.vaadin.flow.router.Route;
import com.vaadin.flow.server.StreamResource;
import com.xpmile.onprem.model.Shipment;
import com.xpmile.onprem.model.ShipmentInformation;
import com.xpmile.onprem.service.ShipmentService;
import com.xpmile.onprem.ui.MainLayout;

import java.util.Map;

import java.awt.Color;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.time.LocalDate;
import java.time.YearMonth;
import java.time.format.DateTimeFormatter;
import java.util.List;
import java.util.Locale;

/**
 * Monthly stats dashboard for the on-prem admin tool. Mirrors the bucketing
 * the Angular ShipmentStatsService applies (currentOn in the selected month;
 * each shipment bucketed by its latest activity event). Adds an Export PDF
 * button so management can hand a printable summary around.
 */
@Route(value = "", layout = MainLayout.class)
@PageTitle("Dashboard | xpmile")
public class DashboardView extends VerticalLayout {

    private static final DateTimeFormatter PICKER_FMT = DateTimeFormatter.ofPattern("yyyy-MM");
    private static final DateTimeFormatter LABEL_FMT  = DateTimeFormatter.ofPattern("MMMM yyyy");

    private final ShipmentService shipmentService;

    private YearMonth selectedMonth = YearMonth.now();
    private Stats currentStats = new Stats();

    private final Span totalNum    = new Span("0");
    private final Span deliveredNum = new Span("0");
    private final Span inScanNum   = new Span("0");
    private final Span outNum      = new Span("0");
    private final Span returnedNum = new Span("0");
    private final Span newNum      = new Span("0");

    public DashboardView(ShipmentService shipmentService) {
        this.shipmentService = shipmentService;

        setSizeFull();
        getStyle().set("padding", "16px");

        add(buildHeader(), buildCards());
        refresh();
    }

    private Component buildHeader() {
        // Icon tile + title + subtitle on the left
        Div iconTile = new Div(new Icon(VaadinIcon.DASHBOARD));
        iconTile.getStyle()
                .set("width", "44px").set("height", "44px")
                .set("display", "inline-flex")
                .set("align-items", "center").set("justify-content", "center")
                .set("border-radius", "8px")
                .set("background", "linear-gradient(135deg, #1b4d8e, #0f2d52)")
                .set("color", "#fff")
                .set("flex-shrink", "0");

        H3 title = new H3("Monthly Dashboard");
        title.getStyle()
                .set("margin", "0")
                .set("font-size", "1.15rem")
                .set("color", "#0f2d52")
                .set("font-weight", "700");

        Span subtitle = new Span("Shipments created in " + LABEL_FMT.format(selectedMonth));
        subtitle.getStyle()
                .set("font-size", "0.78rem")
                .set("color", "#64748b")
                .set("display", "block")
                .set("margin-top", "2px");

        Div titleBlock = new Div(title, subtitle);

        HorizontalLayout left = new HorizontalLayout(iconTile, titleBlock);
        left.setDefaultVerticalComponentAlignment(FlexComponent.Alignment.CENTER);
        left.setSpacing(true);

        // Right side: month picker + refresh + export PDF
        DatePicker monthPicker = new DatePicker();
        monthPicker.setValue(LocalDate.of(selectedMonth.getYear(), selectedMonth.getMonth(), 1));
        monthPicker.setPlaceholder("Pick a month");
        monthPicker.setWidth("180px");
        monthPicker.addValueChangeListener(e -> {
            LocalDate d = e.getValue();
            if (d == null) return;
            selectedMonth = YearMonth.of(d.getYear(), d.getMonth());
            // Re-render subtitle text inline
            subtitle.setText("Shipments created in " + LABEL_FMT.format(selectedMonth));
            refresh();
        });

        Button refresh = new Button(new Icon(VaadinIcon.REFRESH));
        refresh.setAriaLabel("Refresh");
        refresh.addThemeVariants(ButtonVariant.LUMO_TERTIARY);
        refresh.getElement().setProperty("title", "Refresh");
        refresh.addClickListener(e -> refresh());

        Button export = new Button("Export PDF", new Icon(VaadinIcon.FILE_TEXT_O));
        export.addThemeVariants(ButtonVariant.LUMO_PRIMARY);
        export.addClickListener(e -> exportPdf());

        HorizontalLayout right = new HorizontalLayout(monthPicker, refresh, export);
        right.setDefaultVerticalComponentAlignment(FlexComponent.Alignment.CENTER);
        right.setSpacing(true);

        HorizontalLayout bar = new HorizontalLayout(left, right);
        bar.setWidthFull();
        bar.setJustifyContentMode(FlexComponent.JustifyContentMode.BETWEEN);
        bar.setDefaultVerticalComponentAlignment(FlexComponent.Alignment.CENTER);

        // Wrap in a panel with subtle gradient + accent border
        Div panel = new Div(bar);
        panel.getStyle()
                .set("padding", "14px 18px")
                .set("background", "linear-gradient(90deg, #f8fafc, #eef2f7)")
                .set("border-left", "4px solid #1b4d8e")
                .set("border-radius", "8px")
                .set("margin-bottom", "18px");
        return panel;
    }

    private Component buildCards() {
        HorizontalLayout row = new HorizontalLayout(
                card("Total",            totalNum,    VaadinIcon.PACKAGE,       "#03A9F4", "#027bb1"),
                card("Delivered",        deliveredNum, VaadinIcon.CHECK_CIRCLE, "#66bb6a", "#43a047"),
                card("In Scan",          inScanNum,   VaadinIcon.RECORDS,       "#26c6da", "#00838f"),
                card("Out For Delivery", outNum,      VaadinIcon.TRUCK,         "#7e57c2", "#4527a0"),
                card("Returned",         returnedNum, VaadinIcon.REPLY,         "#FFC312", "#fb8c00"),
                card("New",              newNum,      VaadinIcon.PLUS_CIRCLE,   "#ef5350", "#EA2027"));
        row.setWidthFull();
        row.setSpacing(true);
        row.getChildren().forEach(c -> ((Div) c).getStyle().set("flex", "1"));
        return row;
    }

    /**
     * Build one stat card. Icon tile (gradient) on the left, label + big
     * number on the right. Subtle box-shadow + rounded corners; no border
     * accents (the icon-tile colour carries the identity).
     */
    private Div card(String label, Span numSpan, VaadinIcon iconName,
                     String colorFrom, String colorTo) {
        // Icon block
        Icon icon = new Icon(iconName);
        icon.setSize("22px");
        Div iconBox = new Div(icon);
        iconBox.getStyle()
                .set("width", "44px").set("height", "44px")
                .set("display", "inline-flex")
                .set("align-items", "center").set("justify-content", "center")
                .set("border-radius", "10px")
                .set("background",
                        "linear-gradient(135deg, " + colorFrom + ", " + colorTo + ")")
                .set("color", "#fff")
                .set("box-shadow", "0 4px 12px -4px " + colorTo)
                .set("flex-shrink", "0");

        // Text block
        Span labelEl = new Span(label);
        labelEl.getStyle()
                .set("font-size", "0.75rem")
                .set("font-weight", "600")
                .set("color", "#64748b")
                .set("text-transform", "uppercase")
                .set("letter-spacing", "0.04em")
                .set("display", "block");

        numSpan.getStyle()
                .set("font-size", "1.85rem")
                .set("font-weight", "700")
                .set("color", "#0f2d52")
                .set("line-height", "1.1")
                .set("display", "block")
                .set("margin-top", "2px");

        Div text = new Div(labelEl, numSpan);
        text.getStyle().set("min-width", "0").set("flex", "1");

        Div card = new Div();
        card.add(iconBox, text);
        card.getStyle()
                .set("display", "flex")
                .set("align-items", "center")
                .set("gap", "14px")
                .set("padding", "16px 18px")
                .set("background", "#fff")
                .set("border-radius", "10px")
                .set("box-shadow", "0 1px 3px rgba(0,0,0,0.06), 0 8px 24px -16px rgba(15,45,82,0.18)")
                .set("min-height", "92px")
                .set("transition", "transform 0.15s, box-shadow 0.15s");
        return card;
    }

    // ── Data fetch + bucket ──────────────────────────────────────────────

    private void refresh() {
        try {
            // Fetch a broad range matching how Angular ShipmentStatsService
            // does it — the backend stores createdOn as DD/MM/YYYY and
            // compares lexicographically, but Angular sends DD/MM/YYYY.
            // The on-prem Java backend accepts yyyy-MM-dd too.
            String from = "2020-01-01";
            String to   = LocalDate.now().toString();
            List<Shipment> shipments = shipmentService.getShipments(from, to, null);
            currentStats = compute(shipments, selectedMonth);
            applyStats(currentStats);
        } catch (Exception ex) {
            Notification n = Notification.show(
                    "Failed to load: " + ex.getMessage(),
                    4000, Notification.Position.BOTTOM_START);
            n.addThemeVariants(NotificationVariant.LUMO_ERROR);
        }
    }

    private void applyStats(Stats s) {
        totalNum.setText(    Integer.toString(s.total));
        deliveredNum.setText(Integer.toString(s.delivered));
        inScanNum.setText(   Integer.toString(s.inScan));
        outNum.setText(      Integer.toString(s.outForDelivery));
        returnedNum.setText( Integer.toString(s.returned));
        newNum.setText(      Integer.toString(s.newCount));
    }

    /**
     * Bucket shipments whose createdOn falls in the target month, by each
     * shipment's overall latest activity event (current status). Matches
     * the Angular ShipmentStatsService.computeMonthly contract exactly.
     */
    private static Stats compute(List<Shipment> shipments, YearMonth month) {
        Stats out = new Stats();
        if (shipments == null) return out;

        for (Shipment s : shipments) {
            ShipmentInformation si = s.getShipmentInformation();
            if (si == null) continue;

            List<Object> acts = si.getActivity();
            if (acts == null || acts.isEmpty()) continue;

            String createdOn = String.valueOf(si.getCreatedOn());
            if (!inMonth(createdOn, month)) continue;

            out.total++;
            // Jackson deserialises each activity entry as Map<String,Object>
            // when the field is typed List<Object>. Extract "event" defensively.
            Object last = acts.get(acts.size() - 1);
            String evt = (last instanceof Map<?, ?> m)
                    ? String.valueOf(((Map<?, ?>) m).get("event"))
                    : null;
            bucketByEvent(evt, out);
        }
        return out;
    }

    private static void bucketByEvent(String evt, Stats out) {
        if (evt == null) return;
        switch (evt) {
            case "Document Prepared":
            case "Document Created":                          out.newCount++;       break;
            case "In Scan at HUB":
            case "Arrived in HUB":                            out.inScan++;         break;
            case "Out For Delivery":                          out.outForDelivery++; break;
            case "Proof of Delivery":                         out.delivered++;      break;
            case "Shipment Returned to Sender":
            case "Shiment Returned to Sending Station":       out.returned++;       break;
            default:                                          /* no bucket */
        }
    }

    private static boolean inMonth(String s, YearMonth target) {
        if (s == null || s.isBlank() || s.equals("null")) return false;
        try {
            // DD/MM/YYYY (xpmile canonical)
            if (s.length() >= 10 && s.charAt(2) == '/' && s.charAt(5) == '/') {
                int day = Integer.parseInt(s.substring(0, 2));
                int mon = Integer.parseInt(s.substring(3, 5));
                int yr  = Integer.parseInt(s.substring(6, 10));
                return day >= 1 && day <= 31
                        && mon == target.getMonthValue()
                        && yr  == target.getYear();
            }
            // YYYY-MM-DD
            if (s.length() >= 10 && s.charAt(4) == '-' && s.charAt(7) == '-') {
                int yr  = Integer.parseInt(s.substring(0, 4));
                int mon = Integer.parseInt(s.substring(5, 7));
                return mon == target.getMonthValue() && yr == target.getYear();
            }
        } catch (NumberFormatException e) { /* fallthrough */ }
        return false;
    }

    // ── PDF export ────────────────────────────────────────────────────────

    private void exportPdf() {
        String filename = "xpmile-dashboard-"
                + selectedMonth.format(PICKER_FMT) + ".pdf";

        StreamResource resource = new StreamResource(filename, () -> {
            ByteArrayOutputStream baos = new ByteArrayOutputStream();
            try {
                Document doc = new Document(PageSize.A4);
                PdfWriter.getInstance(doc, baos);
                doc.open();

                Font titleFont = FontFactory.getFont(FontFactory.HELVETICA_BOLD, 18, new Color(15, 45, 82));
                Font subFont   = FontFactory.getFont(FontFactory.HELVETICA, 11, Color.DARK_GRAY);
                Font hdrFont   = FontFactory.getFont(FontFactory.HELVETICA_BOLD, 11, Color.WHITE);
                Font cellFont  = FontFactory.getFont(FontFactory.HELVETICA, 11, Color.BLACK);

                Paragraph title = new Paragraph("xpmile — Monthly Operations Dashboard", titleFont);
                title.setSpacingAfter(4);
                doc.add(title);

                Paragraph sub = new Paragraph(
                        "Month: " + selectedMonth.format(LABEL_FMT).toUpperCase(Locale.ENGLISH)
                                + "    ·    Generated: " + LocalDate.now(),
                        subFont);
                sub.setSpacingAfter(18);
                doc.add(sub);

                PdfPTable table = new PdfPTable(2);
                table.setWidthPercentage(70);
                table.setHorizontalAlignment(Element.ALIGN_LEFT);

                addHeaderRow(table, "Bucket", "Count", hdrFont);
                addRow(table, "Total shipments created", currentStats.total,         cellFont);
                addRow(table, "Delivered",               currentStats.delivered,     cellFont);
                addRow(table, "In Scan (at HUB)",        currentStats.inScan,        cellFont);
                addRow(table, "Out For Delivery",        currentStats.outForDelivery, cellFont);
                addRow(table, "Returned to Sender",      currentStats.returned,      cellFont);
                addRow(table, "New (Document prepared)", currentStats.newCount,      cellFont);

                doc.add(table);

                Paragraph footer = new Paragraph(
                        "\nFor management review. Shipment counts are bucketed by each shipment's "
                                + "current (latest) activity event; created-in-month definition applies.",
                        subFont);
                footer.setSpacingBefore(16);
                doc.add(footer);

                doc.close();
            } catch (Exception ex) {
                throw new RuntimeException("PDF generation failed: " + ex.getMessage(), ex);
            }
            return new ByteArrayInputStream(baos.toByteArray());
        });

        // Trigger browser download via a hidden anchor.
        String registration = UI.getCurrent().getSession().getResourceRegistry()
                .registerResource(resource).getResourceUri().toString();
        UI.getCurrent().getPage().open(registration, "_blank");
    }

    private static void addHeaderRow(PdfPTable t, String c1, String c2, Font font) {
        PdfPCell a = new PdfPCell(new Phrase(c1, font));
        PdfPCell b = new PdfPCell(new Phrase(c2, font));
        for (PdfPCell c : new PdfPCell[]{a, b}) {
            c.setBackgroundColor(new Color(15, 45, 82));
            c.setPadding(7);
            c.setBorderColor(Color.LIGHT_GRAY);
        }
        b.setHorizontalAlignment(Element.ALIGN_RIGHT);
        t.addCell(a);
        t.addCell(b);
    }

    private static void addRow(PdfPTable t, String label, int value, Font font) {
        PdfPCell a = new PdfPCell(new Phrase(label, font));
        PdfPCell b = new PdfPCell(new Phrase(Integer.toString(value), font));
        a.setPadding(6);
        b.setPadding(6);
        b.setHorizontalAlignment(Element.ALIGN_RIGHT);
        a.setBorderColor(Color.LIGHT_GRAY);
        b.setBorderColor(Color.LIGHT_GRAY);
        t.addCell(a);
        t.addCell(b);
    }

    // ── DTO ──────────────────────────────────────────────────────────────

    private static class Stats {
        int total, newCount, inScan, outForDelivery, delivered, returned;
    }
}
