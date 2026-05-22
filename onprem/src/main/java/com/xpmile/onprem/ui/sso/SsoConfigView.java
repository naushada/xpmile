package com.xpmile.onprem.ui.sso;

import com.vaadin.flow.component.Component;
import com.vaadin.flow.component.button.Button;
import com.vaadin.flow.component.button.ButtonVariant;
import com.vaadin.flow.component.dialog.Dialog;
import com.vaadin.flow.component.formlayout.FormLayout;
import com.vaadin.flow.component.grid.Grid;
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
import com.vaadin.flow.component.select.Select;
import com.vaadin.flow.component.textfield.PasswordField;
import com.vaadin.flow.component.textfield.TextArea;
import com.vaadin.flow.component.textfield.TextField;
import com.vaadin.flow.router.PageTitle;
import com.vaadin.flow.router.Route;
import com.xpmile.onprem.service.SsoConfigService;
import com.xpmile.onprem.ui.MainLayout;
import org.bson.Document;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.stream.Collectors;

/**
 * On-prem SSO-configuration admin: edit the {@code sso_config} document the
 * C++ backend reads and hot-reloads (docs/design/sso/sso-design.md §10).
 *
 * This is not SSO <em>login</em> for the on-prem console — that stays
 * unauthenticated behind the customer's physical access controls. It is an
 * SSO <em>configuration</em> surface. {@code clientSecret} is write-only: it
 * is never rendered back into the form; leaving it blank on save keeps the
 * stored value.
 */
@Route(value = "sso-config", layout = MainLayout.class)
@PageTitle("SSO Configuration | xpmile")
public class SsoConfigView extends VerticalLayout {

    private static final List<String> PROTOCOLS = List.of("oidc", "saml");

    private final SsoConfigService service;

    private final TextField publicBaseUrl = new TextField("Public base URL");
    private final Grid<Document> grid = new Grid<>();
    private List<Document> providers = new ArrayList<>();

    public SsoConfigView(SsoConfigService service) {
        this.service = service;

        setSizeFull();
        getStyle().set("padding", "16px");

        add(buildHeader(), buildConfigForm(), buildToolbar(), buildGrid(), buildFooter());
        load();
    }

    private Component buildHeader() {
        H3 title = new H3("SSO Configuration");
        title.getStyle().set("margin", "0 0 4px 0");

        Span note = new Span("Edits are written straight to the on-prem MongoDB "
                + "and picked up by the backend within ~60 s. Provider secrets "
                + "are write-only — leave a secret blank to keep the stored value.");
        note.getStyle()
                .set("font-size", "var(--lumo-font-size-s)")
                .set("color", "var(--lumo-secondary-text-color)");

        Div header = new Div(title, note);
        header.getStyle().set("margin-bottom", "12px");
        return header;
    }

    private Component buildConfigForm() {
        publicBaseUrl.setWidth("440px");
        publicBaseUrl.setPlaceholder("https://your-app.herokuapp.com");
        publicBaseUrl.setHelperText("Callback URLs are pinned to this origin.");
        return publicBaseUrl;
    }

    private Component buildToolbar() {
        Button add = new Button("Add provider", new Icon(VaadinIcon.PLUS));
        add.addThemeVariants(ButtonVariant.LUMO_PRIMARY);
        add.addClickListener(e -> openEditor(new Document("protocol", "oidc"), true));

        Button reload = new Button("Reload", new Icon(VaadinIcon.REFRESH));
        reload.addClickListener(e -> load());

        HorizontalLayout bar = new HorizontalLayout(add, reload);
        bar.setDefaultVerticalComponentAlignment(FlexComponent.Alignment.CENTER);
        bar.getStyle().set("margin", "12px 0");
        return bar;
    }

    private Component buildGrid() {
        grid.addColumn(p -> orEmpty(p.getString("id")))
                .setHeader("ID").setAutoWidth(true).setFlexGrow(0);
        grid.addColumn(p -> orEmpty(p.getString("displayName")))
                .setHeader("Display name").setAutoWidth(true);
        grid.addColumn(p -> orEmpty(p.getString("protocol")))
                .setHeader("Protocol").setAutoWidth(true).setFlexGrow(0);
        grid.addColumn(p -> "oidc".equals(p.getString("protocol"))
                        ? orEmpty(p.getString("issuer"))
                        : orEmpty(p.getString("idpEntityId")))
                .setHeader("Issuer / IdP entity ID").setAutoWidth(true);

        grid.addComponentColumn(provider -> {
            Button edit = new Button("Edit", new Icon(VaadinIcon.EDIT));
            edit.addThemeVariants(ButtonVariant.LUMO_TERTIARY, ButtonVariant.LUMO_SMALL);
            edit.addClickListener(e -> openEditor(provider, false));

            Button del = new Button("Remove", new Icon(VaadinIcon.TRASH));
            del.addThemeVariants(ButtonVariant.LUMO_TERTIARY, ButtonVariant.LUMO_SMALL,
                                  ButtonVariant.LUMO_ERROR);
            del.addClickListener(e -> {
                providers.remove(provider);
                grid.setItems(providers);
            });

            HorizontalLayout actions = new HorizontalLayout(edit, del);
            actions.setSpacing(false);
            return actions;
        }).setHeader("Actions").setAutoWidth(true).setFlexGrow(0);

        grid.setSizeFull();
        return grid;
    }

    private Component buildFooter() {
        Button save = new Button("Save configuration", new Icon(VaadinIcon.CHECK));
        save.addThemeVariants(ButtonVariant.LUMO_PRIMARY);
        save.addClickListener(e -> save());

        HorizontalLayout bar = new HorizontalLayout(save);
        bar.getStyle().set("margin-top", "12px");
        return bar;
    }

    // ── Persistence ──────────────────────────────────────────────────────────

    private void load() {
        try {
            Document doc = service.load();
            publicBaseUrl.setValue(orEmpty(doc.getString("publicBaseUrl")));
            List<Document> loaded = doc.getList("providers", Document.class);
            providers = new ArrayList<>(loaded != null ? loaded : List.of());
            grid.setItems(providers);
        } catch (Exception ex) {
            notify("Cannot read sso_config from MongoDB: " + ex.getMessage(),
                    NotificationVariant.LUMO_ERROR);
        }
    }

    private void save() {
        Document doc = new Document("publicBaseUrl", publicBaseUrl.getValue().trim());
        doc.put("providers", providers);
        try {
            service.save(doc);
            notify("SSO configuration saved — the backend hot-reloads within ~60 s",
                    NotificationVariant.LUMO_SUCCESS);
            load();  // reload so the write-only-secret flags reflect the saved state
        } catch (Exception ex) {
            notify("Save failed: " + ex.getMessage(), NotificationVariant.LUMO_ERROR);
        }
    }

    // ── Provider editor dialog ───────────────────────────────────────────────

    private void openEditor(Document provider, boolean isNew) {
        Dialog dlg = new Dialog();
        dlg.setHeaderTitle(isNew ? "Add provider" : "Edit provider — "
                + orEmpty(provider.getString("id")));
        dlg.setWidth("520px");

        TextField id = new TextField("Provider ID");
        id.setHelperText("Stable id, used in the callback URL path.");
        TextField displayName = new TextField("Display name");
        Select<String> protocol = new Select<>();
        protocol.setLabel("Protocol");
        protocol.setItems(PROTOCOLS);

        TextField issuer = new TextField("Issuer URL");
        TextField clientId = new TextField("Client ID");
        PasswordField clientSecret = new PasswordField("Client secret");
        clientSecret.setPlaceholder(provider.getBoolean("_hasClientSecret", false)
                ? "•••••• — leave blank to keep the stored secret"
                : "Client secret");
        TextField scopes = new TextField("Scopes");
        scopes.setHelperText("Comma-separated, e.g. openid, email, profile, groups");

        TextField idpEntityId = new TextField("IdP entity ID");
        TextField idpSsoUrl = new TextField("IdP SSO URL");
        TextArea idpSigningCert = new TextArea("IdP signing certificate (PEM)");
        TextField spEntityId = new TextField("SP entity ID");

        TextField defaultRole = new TextField("Default role");
        TextField allowedEmailDomains = new TextField("Allowed email domains");
        allowedEmailDomains.setHelperText("Comma-separated; blank means no restriction.");
        TextArea groupRoleMap = new TextArea("Group → role map");
        groupRoleMap.setHelperText("One mapping per line, e.g. xpmile-admins=Admin");

        // Populate from the provider document. clientSecret stays blank — it is
        // write-only and is never rendered back into the form.
        id.setValue(orEmpty(provider.getString("id")));
        displayName.setValue(orEmpty(provider.getString("displayName")));
        protocol.setValue(provider.getString("protocol") != null
                ? provider.getString("protocol") : "oidc");
        issuer.setValue(orEmpty(provider.getString("issuer")));
        clientId.setValue(orEmpty(provider.getString("clientId")));
        scopes.setValue(joinCsv(provider.get("scopes")));
        idpEntityId.setValue(orEmpty(provider.getString("idpEntityId")));
        idpSsoUrl.setValue(orEmpty(provider.getString("idpSsoUrl")));
        idpSigningCert.setValue(orEmpty(provider.getString("idpSigningCert")));
        spEntityId.setValue(orEmpty(provider.getString("spEntityId")));
        defaultRole.setValue(orEmpty(provider.getString("defaultRole")));
        allowedEmailDomains.setValue(joinCsv(provider.get("allowedEmailDomains")));
        groupRoleMap.setValue(mapText(provider.get("groupRoleMap")));

        FormLayout common = singleColumnForm(id, displayName, protocol);
        FormLayout oidc = singleColumnForm(issuer, clientId, clientSecret, scopes);
        FormLayout saml = singleColumnForm(idpEntityId, idpSsoUrl, idpSigningCert, spEntityId);
        FormLayout provisioning =
                singleColumnForm(defaultRole, allowedEmailDomains, groupRoleMap);

        Div oidcSection = new Div(sectionLabel("OIDC"), oidc);
        Div samlSection = new Div(sectionLabel("SAML"), saml);

        Runnable applyProtocol = () -> {
            boolean isOidc = "oidc".equals(protocol.getValue());
            oidcSection.setVisible(isOidc);
            samlSection.setVisible(!isOidc);
        };
        protocol.addValueChangeListener(e -> applyProtocol.run());
        applyProtocol.run();

        dlg.add(common, oidcSection, samlSection,
                sectionLabel("Provisioning"), provisioning);

        Button save = new Button("Apply", e -> {
            if (id.getValue() == null || id.getValue().isBlank()) {
                notify("Provider ID is required", NotificationVariant.LUMO_ERROR);
                return;
            }
            boolean isOidc = "oidc".equals(protocol.getValue());

            provider.put("id", id.getValue().trim());
            provider.put("displayName", displayName.getValue().trim());
            provider.put("protocol", protocol.getValue());

            if (isOidc) {
                provider.put("issuer", issuer.getValue().trim());
                provider.put("clientId", clientId.getValue().trim());
                // Blank secret -> leave the key absent; SsoConfigService keeps
                // the stored value on save.
                if (!clientSecret.getValue().isBlank()) {
                    provider.put("clientSecret", clientSecret.getValue());
                } else {
                    provider.remove("clientSecret");
                }
                provider.put("scopes", splitCsv(scopes.getValue()));
                provider.remove("idpEntityId");
                provider.remove("idpSsoUrl");
                provider.remove("idpSigningCert");
                provider.remove("spEntityId");
            } else {
                provider.put("idpEntityId", idpEntityId.getValue().trim());
                provider.put("idpSsoUrl", idpSsoUrl.getValue().trim());
                provider.put("idpSigningCert", idpSigningCert.getValue().trim());
                provider.put("spEntityId", spEntityId.getValue().trim());
                provider.remove("issuer");
                provider.remove("clientId");
                provider.remove("clientSecret");
                provider.remove("scopes");
            }

            provider.put("defaultRole", defaultRole.getValue().trim());
            provider.put("allowedEmailDomains", splitCsv(allowedEmailDomains.getValue()));
            Map<String, String> roleMap = parseMap(groupRoleMap.getValue());
            if (roleMap.isEmpty()) {
                provider.remove("groupRoleMap");
            } else {
                Document roleMapDoc = new Document();
                roleMapDoc.putAll(roleMap);
                provider.put("groupRoleMap", roleMapDoc);
            }

            if (isNew) {
                providers.add(provider);
            }
            grid.setItems(providers);
            dlg.close();
        });
        save.addThemeVariants(ButtonVariant.LUMO_PRIMARY);

        Button cancel = new Button("Cancel", e -> dlg.close());
        dlg.getFooter().add(cancel, save);
        dlg.open();
    }

    // ── Helpers ──────────────────────────────────────────────────────────────

    private static FormLayout singleColumnForm(Component... fields) {
        FormLayout form = new FormLayout(fields);
        form.setResponsiveSteps(new FormLayout.ResponsiveStep("0", 1));
        return form;
    }

    private static Span sectionLabel(String text) {
        Span label = new Span(text);
        label.getStyle()
                .set("font-weight", "600")
                .set("font-size", "var(--lumo-font-size-s)")
                .set("color", "var(--lumo-secondary-text-color)")
                .set("display", "block")
                .set("margin", "12px 0 4px 0");
        return label;
    }

    private static String orEmpty(String value) {
        return value != null ? value : "";
    }

    private static String joinCsv(Object list) {
        if (!(list instanceof List<?>)) {
            return "";
        }
        return ((List<?>) list).stream()
                .map(String::valueOf)
                .collect(Collectors.joining(", "));
    }

    private static List<String> splitCsv(String csv) {
        List<String> out = new ArrayList<>();
        if (csv == null) {
            return out;
        }
        for (String part : csv.split(",")) {
            String trimmed = part.trim();
            if (!trimmed.isEmpty()) {
                out.add(trimmed);
            }
        }
        return out;
    }

    private static String mapText(Object map) {
        if (!(map instanceof Map<?, ?>)) {
            return "";
        }
        return ((Map<?, ?>) map).entrySet().stream()
                .map(e -> e.getKey() + "=" + e.getValue())
                .collect(Collectors.joining("\n"));
    }

    private static Map<String, String> parseMap(String text) {
        Map<String, String> map = new TreeMap<>();
        if (text == null) {
            return map;
        }
        for (String line : text.split("\\R")) {
            int eq = line.indexOf('=');
            if (eq <= 0) {
                continue;
            }
            String key = line.substring(0, eq).trim();
            String value = line.substring(eq + 1).trim();
            if (!key.isEmpty()) {
                map.put(key, value);
            }
        }
        return map;
    }

    private static void notify(String message, NotificationVariant variant) {
        Notification n = Notification.show(message, 3500, Notification.Position.BOTTOM_START);
        n.addThemeVariants(variant);
    }
}
