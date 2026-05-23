package com.xpmile.onprem.ui.idp;

import com.vaadin.flow.component.Component;
import com.vaadin.flow.component.button.Button;
import com.vaadin.flow.component.button.ButtonVariant;
import com.vaadin.flow.component.confirmdialog.ConfirmDialog;
import com.vaadin.flow.component.datepicker.DatePicker;
import com.vaadin.flow.component.dialog.Dialog;
import com.vaadin.flow.component.grid.Grid;
import com.vaadin.flow.component.html.H3;
import com.vaadin.flow.component.html.Span;
import com.vaadin.flow.component.notification.Notification;
import com.vaadin.flow.component.notification.NotificationVariant;
import com.vaadin.flow.component.orderedlayout.FlexComponent;
import com.vaadin.flow.component.orderedlayout.HorizontalLayout;
import com.vaadin.flow.component.orderedlayout.VerticalLayout;
import com.vaadin.flow.router.PageTitle;
import com.vaadin.flow.router.Route;
import com.xpmile.onprem.service.IdpSigningKeyService;
import com.xpmile.onprem.ui.MainLayout;
import org.bson.Document;

import java.time.Instant;
import java.time.LocalDate;
import java.time.ZoneId;
import java.time.format.DateTimeFormatter;
import java.util.List;

/**
 * On-prem admin for {@code idp.idp_signing_keys} (Phase B).
 *
 * Operator surface: generate, activate (mutually exclusive), set
 * notAfter, delete. The cloud-side IdP publishes every non-expired key
 * via JWKS and signs with the {@code active:true} row. Routine
 * rotation is two clicks — "Generate" (with auto-activate) — and the
 * old key stays in JWKS until its {@code notAfter} expires so any
 * in-flight RP token still verifies.
 *
 * Read-only display of {@code publicKeyPem}; {@code privateKeyPem} is
 * never returned to the UI (the service strips it).
 */
@Route(value = "idp-signing-keys", layout = MainLayout.class)
@PageTitle("IdP Signing Keys | xpmile")
public class IdpSigningKeysView extends VerticalLayout {

    private static final DateTimeFormatter HUMAN_DATE =
            DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss z").withZone(ZoneId.systemDefault());

    private final IdpSigningKeyService service;
    private final Grid<Document> grid = new Grid<>();

    public IdpSigningKeysView(IdpSigningKeyService service) {
        this.service = service;
        setSizeFull();
        getStyle().set("padding", "16px");
        add(buildHeader(), buildToolbar(), buildGrid());
        reload();
    }

    private Component buildHeader() {
        H3 title = new H3("IdP Signing Keys");
        title.getStyle().set("margin", "0 0 4px 0");
        Span note = new Span(
                "RSA-2048 keypairs used by the IdP to sign id_tokens. The "
              + "wsdbagent reads these directly — private key bytes never "
              + "leave on-prem. Activation is mutually exclusive — clicking "
              + "Activate on one row clears the others.");
        note.getStyle()
                .set("font-size", "var(--lumo-font-size-s)")
                .set("color", "var(--lumo-secondary-text-color)");
        return new VerticalLayout(title, note);
    }

    private Component buildToolbar() {
        Button generate = new Button("Generate new key", e -> openGenerateDialog());
        generate.addThemeVariants(ButtonVariant.LUMO_PRIMARY);

        Button refresh = new Button("Refresh", e -> reload());

        HorizontalLayout bar = new HorizontalLayout(generate, refresh);
        bar.setAlignItems(FlexComponent.Alignment.CENTER);
        return bar;
    }

    private Component buildGrid() {
        grid.addColumn(d -> d.getString("kid"))
                .setHeader("kid").setAutoWidth(true);
        grid.addColumn(d -> d.getString("alg"))
                .setHeader("alg").setWidth("80px").setFlexGrow(0);
        grid.addColumn(d -> Boolean.TRUE.equals(d.getBoolean("active")) ? "✓" : "")
                .setHeader("active").setWidth("80px").setFlexGrow(0)
                .setTextAlign(com.vaadin.flow.component.grid.ColumnTextAlign.CENTER);
        grid.addColumn(d -> formatEpoch(d.get("createdAt")))
                .setHeader("created").setAutoWidth(true);
        grid.addColumn(d -> {
            Object n = d.get("notAfter");
            long v = (n instanceof Number) ? ((Number) n).longValue() : 0;
            return v == 0 ? "never" : formatEpoch(v);
        }).setHeader("notAfter").setAutoWidth(true);

        grid.addComponentColumn(this::buildRowActions)
                .setHeader("actions").setAutoWidth(true).setFlexGrow(0);

        grid.setSizeFull();
        return grid;
    }

    private Component buildRowActions(Document row) {
        String kid    = row.getString("kid");
        boolean active = Boolean.TRUE.equals(row.getBoolean("active"));

        Button activate = new Button("Activate", e -> {
            service.activate(kid);
            notify("Activated " + kid);
            reload();
        });
        activate.setEnabled(!active);

        Button expire = new Button("Set expiry…", e -> openExpiryDialog(kid));
        Button del    = new Button("Delete", e -> confirmDelete(kid, active));
        del.addThemeVariants(ButtonVariant.LUMO_ERROR, ButtonVariant.LUMO_TERTIARY);

        HorizontalLayout actions = new HorizontalLayout(activate, expire, del);
        actions.setSpacing(true);
        return actions;
    }

    // ── dialogs ──────────────────────────────────────────────────────────

    private void openGenerateDialog() {
        Dialog d = new Dialog();
        d.setHeaderTitle("Generate signing key");

        com.vaadin.flow.component.checkbox.Checkbox autoActivate =
                new com.vaadin.flow.component.checkbox.Checkbox(
                        "Activate immediately (deactivates current key)", true);

        Button generate = new Button("Generate", e -> {
            try {
                Document created = service.generate(autoActivate.getValue());
                notify("Generated " + (created != null ? created.getString("kid")
                                                       : "new key"));
                d.close();
                reload();
            } catch (RuntimeException ex) {
                notifyError("Key generation failed: " + ex.getMessage());
            }
        });
        generate.addThemeVariants(ButtonVariant.LUMO_PRIMARY);

        Button cancel = new Button("Cancel", e -> d.close());

        d.add(autoActivate);
        d.getFooter().add(cancel, generate);
        d.open();
    }

    private void openExpiryDialog(String kid) {
        Dialog d = new Dialog();
        d.setHeaderTitle("Set notAfter for " + kid);

        DatePicker date = new DatePicker("Expires on");
        date.setHelperText("Empty = never expires (clears any current notAfter).");

        Button save = new Button("Save", e -> {
            LocalDate v = date.getValue();
            long epoch = (v == null) ? 0L
                    : v.atStartOfDay(ZoneId.systemDefault()).toEpochSecond();
            service.setNotAfter(kid, epoch);
            notify("Updated notAfter for " + kid);
            d.close();
            reload();
        });
        save.addThemeVariants(ButtonVariant.LUMO_PRIMARY);

        Button cancel = new Button("Cancel", e -> d.close());

        d.add(date);
        d.getFooter().add(cancel, save);
        d.open();
    }

    private void confirmDelete(String kid, boolean active) {
        ConfirmDialog d = new ConfirmDialog();
        d.setHeader("Delete " + kid + "?");
        d.setText(active
                ? "This is the ACTIVE key — deleting it will break /token "
                + "until you generate + activate a new one. Tokens already "
                + "issued under this kid will fail RP verification."
                : "Tokens already issued under this kid will fail RP "
                + "verification once the JWKS poll drops the kid. For a "
                + "graceful drop, use 'Set expiry…' instead.");
        d.setConfirmText("Delete");
        d.setConfirmButtonTheme("error primary");
        d.setCancelable(true);
        d.addConfirmListener(e -> {
            service.delete(kid);
            notify("Deleted " + kid);
            reload();
        });
        d.open();
    }

    // ── shared helpers ───────────────────────────────────────────────────

    private void reload() {
        List<Document> rows = service.list();
        grid.setItems(rows);
    }

    private static String formatEpoch(Object epochSeconds) {
        if (!(epochSeconds instanceof Number)) return "";
        long s = ((Number) epochSeconds).longValue();
        if (s == 0) return "";
        return HUMAN_DATE.format(Instant.ofEpochSecond(s));
    }

    private void notify(String msg) {
        Notification n = Notification.show(msg, 2500, Notification.Position.TOP_CENTER);
        n.addThemeVariants(NotificationVariant.LUMO_SUCCESS);
    }

    private void notifyError(String msg) {
        Notification n = Notification.show(msg, 4000, Notification.Position.TOP_CENTER);
        n.addThemeVariants(NotificationVariant.LUMO_ERROR);
    }
}
