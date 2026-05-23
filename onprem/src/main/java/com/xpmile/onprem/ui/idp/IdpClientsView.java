package com.xpmile.onprem.ui.idp;

import com.vaadin.flow.component.Component;
import com.vaadin.flow.component.button.Button;
import com.vaadin.flow.component.button.ButtonVariant;
import com.vaadin.flow.component.confirmdialog.ConfirmDialog;
import com.vaadin.flow.component.dialog.Dialog;
import com.vaadin.flow.component.formlayout.FormLayout;
import com.vaadin.flow.component.grid.Grid;
import com.vaadin.flow.component.html.H3;
import com.vaadin.flow.component.html.Span;
import com.vaadin.flow.component.notification.Notification;
import com.vaadin.flow.component.notification.NotificationVariant;
import com.vaadin.flow.component.orderedlayout.FlexComponent;
import com.vaadin.flow.component.orderedlayout.HorizontalLayout;
import com.vaadin.flow.component.orderedlayout.VerticalLayout;
import com.vaadin.flow.component.textfield.TextArea;
import com.vaadin.flow.component.textfield.TextField;
import com.vaadin.flow.router.PageTitle;
import com.vaadin.flow.router.Route;
import com.xpmile.onprem.service.IdpClientService;
import com.xpmile.onprem.ui.MainLayout;
import org.bson.Document;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.stream.Collectors;

/**
 * On-prem admin for {@code idp.idp_clients} (Phase J).
 *
 * Operator registers an RP by clientId + clientName + arrays of
 * allowed redirect / post-logout URIs + scopes + grant types. The
 * cloud-side {@code IdpClientRegistry} hot-reloads ~60 s. URI fields
 * are taken as one-per-line; the cloud matches by exact byte equality
 * so trailing slashes and case matter.
 *
 * v1 simplification: no clientSecret hashing surface — the
 * {@code clientSecretHash} field is shown read-only and stays whatever
 * the document already has. The cloud's /token endpoint doesn't
 * validate client_secret yet (slice 3 follow-up).
 */
@Route(value = "idp-clients", layout = MainLayout.class)
@PageTitle("IdP Clients | xpmile")
public class IdpClientsView extends VerticalLayout {

    private final IdpClientService service;
    private final Grid<Document> grid = new Grid<>();

    public IdpClientsView(IdpClientService service) {
        this.service = service;
        setSizeFull();
        getStyle().set("padding", "16px");
        add(buildHeader(), buildToolbar(), buildGrid());
        reload();
    }

    private Component buildHeader() {
        H3 title = new H3("IdP Clients");
        title.getStyle().set("margin", "0 0 4px 0");
        Span note = new Span(
                "Registered relying parties the in-house IdP issues tokens "
              + "to. URI matching is EXACT (byte-equality) — trailing "
              + "slashes and case differences will be rejected at /authorize "
              + "and /end_session. The cloud-side registry hot-reloads "
              + "every ~60 s.");
        note.getStyle()
                .set("font-size", "var(--lumo-font-size-s)")
                .set("color", "var(--lumo-secondary-text-color)");
        return new VerticalLayout(title, note);
    }

    private Component buildToolbar() {
        Button add = new Button("Register client…", e -> openEditDialog(null));
        add.addThemeVariants(ButtonVariant.LUMO_PRIMARY);

        Button refresh = new Button("Refresh", e -> reload());

        HorizontalLayout bar = new HorizontalLayout(add, refresh);
        bar.setAlignItems(FlexComponent.Alignment.CENTER);
        return bar;
    }

    private Component buildGrid() {
        grid.addColumn(d -> d.get("_id", "")).setHeader("clientId").setAutoWidth(true);
        grid.addColumn(d -> d.getString("clientName")).setHeader("name").setAutoWidth(true);
        grid.addColumn(d -> sizeOf(d, "redirectUris"))
                .setHeader("#redirects").setWidth("110px").setFlexGrow(0);
        grid.addColumn(d -> sizeOf(d, "scopes"))
                .setHeader("#scopes").setWidth("100px").setFlexGrow(0);
        grid.addComponentColumn(this::buildRowActions)
                .setHeader("actions").setAutoWidth(true).setFlexGrow(0);

        grid.setSizeFull();
        return grid;
    }

    private Component buildRowActions(Document row) {
        Button edit = new Button("Edit", e -> openEditDialog(row));
        Button del  = new Button("Delete", e -> confirmDelete(row));
        del.addThemeVariants(ButtonVariant.LUMO_ERROR, ButtonVariant.LUMO_TERTIARY);
        HorizontalLayout actions = new HorizontalLayout(edit, del);
        actions.setSpacing(true);
        return actions;
    }

    // ── dialogs ──────────────────────────────────────────────────────────

    /** @param existing null = create-new */
    private void openEditDialog(Document existing) {
        Dialog d = new Dialog();
        d.setHeaderTitle(existing == null ? "Register client" : "Edit client");
        d.setWidth("600px");

        TextField clientId   = new TextField("clientId (_id)");
        TextField clientName = new TextField("clientName");
        TextArea redirects   = new TextArea("redirectUris (one per line)");
        TextArea postLogout  = new TextArea("postLogoutRedirectUris (one per line)");
        TextArea scopes      = new TextArea("scopes (one per line)");
        TextArea grants      = new TextArea("grantTypes (one per line)");

        clientId.setRequiredIndicatorVisible(true);
        clientName.setRequiredIndicatorVisible(true);

        if (existing != null) {
            clientId.setValue(stringOf(existing.get("_id")));
            clientId.setReadOnly(true);   // _id is the natural key
            clientName.setValue(orEmpty(existing.getString("clientName")));
            redirects.setValue(joinLines(existing.getList("redirectUris", String.class)));
            postLogout.setValue(joinLines(existing.getList("postLogoutRedirectUris", String.class)));
            scopes.setValue(joinLines(existing.getList("scopes", String.class)));
            grants.setValue(joinLines(existing.getList("grantTypes", String.class)));
        } else {
            // sensible defaults so the first-time-registered RP works out of the box
            scopes.setValue("openid\nemail\nprofile");
            grants.setValue("authorization_code");
        }

        FormLayout form = new FormLayout(clientId, clientName, redirects,
                                          postLogout, scopes, grants);
        form.setResponsiveSteps(new FormLayout.ResponsiveStep("0", 1));

        Button save = new Button("Save", e -> {
            String cid = clientId.getValue().trim();
            if (cid.isEmpty()) {
                notifyError("clientId is required.");
                return;
            }
            if (clientName.getValue().trim().isEmpty()) {
                notifyError("clientName is required.");
                return;
            }
            Document doc = new Document("_id", cid)
                    .append("clientName", clientName.getValue().trim())
                    // preserve any existing hash on edit; brand-new clients
                    // get an empty hash (no client_secret validation in v1).
                    .append("clientSecretHash",
                            existing != null
                                ? orEmpty(existing.getString("clientSecretHash"))
                                : "")
                    .append("redirectUris",           splitLines(redirects.getValue()))
                    .append("postLogoutRedirectUris", splitLines(postLogout.getValue()))
                    .append("scopes",                 splitLines(scopes.getValue()))
                    .append("grantTypes",             splitLines(grants.getValue()));
            service.upsert(doc);
            notify((existing == null ? "Registered " : "Updated ") + cid);
            d.close();
            reload();
        });
        save.addThemeVariants(ButtonVariant.LUMO_PRIMARY);

        Button cancel = new Button("Cancel", e -> d.close());

        d.add(form);
        d.getFooter().add(cancel, save);
        d.open();
    }

    private void confirmDelete(Document row) {
        String cid = stringOf(row.get("_id"));
        ConfirmDialog d = new ConfirmDialog();
        d.setHeader("Delete " + cid + "?");
        d.setText("After the cloud-side hot-reload (~60 s), /authorize calls "
                + "from this RP will start failing with `invalid_client`. "
                + "Existing access_tokens and id_tokens already issued to "
                + "this client stay valid until their TTL expires.");
        d.setConfirmText("Delete");
        d.setConfirmButtonTheme("error primary");
        d.setCancelable(true);
        d.addConfirmListener(e -> {
            service.delete(cid);
            notify("Deleted " + cid);
            reload();
        });
        d.open();
    }

    // ── helpers ──────────────────────────────────────────────────────────

    private void reload() {
        List<Document> rows = service.list();
        grid.setItems(rows);
    }

    private static String orEmpty(String s) { return s == null ? "" : s; }

    private static String stringOf(Object o) { return o == null ? "" : o.toString(); }

    private static int sizeOf(Document d, String field) {
        List<?> v = d.getList(field, Object.class);
        return v == null ? 0 : v.size();
    }

    private static String joinLines(List<String> items) {
        if (items == null || items.isEmpty()) return "";
        return String.join("\n", items);
    }

    private static List<String> splitLines(String raw) {
        if (raw == null || raw.isBlank()) return new ArrayList<>();
        return Arrays.stream(raw.split("\\r?\\n"))
                .map(String::trim)
                .filter(s -> !s.isEmpty())
                .collect(Collectors.toList());
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
