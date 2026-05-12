package com.xpmile.onprem.ui.account;

import com.vaadin.flow.component.Component;
import com.vaadin.flow.component.button.Button;
import com.vaadin.flow.component.button.ButtonVariant;
import com.vaadin.flow.component.dialog.Dialog;
import com.vaadin.flow.component.formlayout.FormLayout;
import com.vaadin.flow.component.grid.Grid;
import com.vaadin.flow.component.html.Div;
import com.vaadin.flow.component.html.H3;
import com.vaadin.flow.component.icon.Icon;
import com.vaadin.flow.component.icon.VaadinIcon;
import com.vaadin.flow.component.notification.Notification;
import com.vaadin.flow.component.notification.NotificationVariant;
import com.vaadin.flow.component.orderedlayout.FlexComponent;
import com.vaadin.flow.component.orderedlayout.HorizontalLayout;
import com.vaadin.flow.component.orderedlayout.VerticalLayout;
import com.vaadin.flow.component.textfield.PasswordField;
import com.vaadin.flow.router.PageTitle;
import com.vaadin.flow.router.Route;
import com.xpmile.onprem.model.Account;
import com.xpmile.onprem.model.LoginCredentials;
import com.xpmile.onprem.model.PersonalInfo;
import com.xpmile.onprem.service.AccountService;
import com.xpmile.onprem.ui.MainLayout;

import java.util.List;

/**
 * On-prem accounts admin: list every account, reset any user's password,
 * and create new accounts.
 *
 * The reset-password flow is the load-bearing feature of this entire
 * Vaadin tool — it's why the on-prem app exists at all. The cloud-deployed
 * Angular app has no self-service "forgot password" flow, so a forgotten
 * password is recovered here by an operator on the customer's premises.
 */
@Route(value = "accounts", layout = MainLayout.class)
@PageTitle("Accounts | xpmile")
public class AccountsView extends VerticalLayout {

    private final AccountService accountService;
    private final Grid<Account> grid = new Grid<>(Account.class, false);

    public AccountsView(AccountService accountService) {
        this.accountService = accountService;

        setSizeFull();
        getStyle().set("padding", "16px");

        add(buildHeader(), buildToolbar(), buildGrid());
        refresh();
    }

    private Component buildHeader() {
        H3 title = new H3("Accounts");
        title.getStyle().set("margin", "0 0 12px 0");
        return title;
    }

    private Component buildToolbar() {
        Button refresh = new Button("Refresh", new Icon(VaadinIcon.REFRESH));
        refresh.addClickListener(e -> refresh());

        Button create = new Button("Create Account", new Icon(VaadinIcon.PLUS));
        create.addThemeVariants(ButtonVariant.LUMO_PRIMARY);
        create.addClickListener(e ->
                com.vaadin.flow.component.UI.getCurrent().navigate(CreateAccountView.class));

        HorizontalLayout bar = new HorizontalLayout(create, refresh);
        bar.setSpacing(true);
        bar.setDefaultVerticalComponentAlignment(FlexComponent.Alignment.CENTER);
        bar.getStyle().set("margin-bottom", "12px");
        return bar;
    }

    private Component buildGrid() {
        grid.addColumn(a -> safe(a.getLoginCredentials(), LoginCredentials::getAccountCode))
                .setHeader("Account Code").setAutoWidth(true).setFlexGrow(0);
        grid.addColumn(a -> safe(a.getPersonalInfo(), PersonalInfo::getName))
                .setHeader("Name").setAutoWidth(true);
        grid.addColumn(a -> safe(a.getPersonalInfo(), PersonalInfo::getRole))
                .setHeader("Role").setAutoWidth(true).setFlexGrow(0);
        grid.addColumn(a -> safe(a.getPersonalInfo(), PersonalInfo::getEmail))
                .setHeader("Email").setAutoWidth(true);
        grid.addColumn(a -> safe(a.getPersonalInfo(), PersonalInfo::getContact))
                .setHeader("Contact").setAutoWidth(true).setFlexGrow(0);

        grid.addComponentColumn(account -> {
            Button reset = new Button("Reset password", new Icon(VaadinIcon.KEY));
            reset.addThemeVariants(ButtonVariant.LUMO_TERTIARY, ButtonVariant.LUMO_SMALL);
            reset.addClickListener(e -> openResetDialog(account));

            Button del = new Button("Delete", new Icon(VaadinIcon.TRASH));
            del.addThemeVariants(ButtonVariant.LUMO_TERTIARY, ButtonVariant.LUMO_SMALL,
                                  ButtonVariant.LUMO_ERROR);
            del.addClickListener(e -> openDeleteDialog(account));

            HorizontalLayout actions = new HorizontalLayout(reset, del);
            actions.setSpacing(false);
            return actions;
        }).setHeader("Actions").setAutoWidth(true).setFlexGrow(0);

        grid.setSizeFull();
        return grid;
    }

    private void refresh() {
        List<Account> all = accountService.getAllAccounts();
        grid.setItems(all);
    }

    // ── Reset-password dialog ────────────────────────────────────────────

    private void openResetDialog(Account account) {
        String code = safe(account.getLoginCredentials(), LoginCredentials::getAccountCode);

        Dialog dlg = new Dialog();
        dlg.setHeaderTitle("Reset password — " + code);

        PasswordField p1 = new PasswordField("New password");
        PasswordField p2 = new PasswordField("Confirm");
        p1.setWidthFull();
        p2.setWidthFull();

        FormLayout body = new FormLayout(p1, p2);
        body.setResponsiveSteps(new FormLayout.ResponsiveStep("0", 1));
        dlg.add(body);

        Button save = new Button("Reset", e -> {
            String np = p1.getValue();
            if (np == null || np.isBlank() || !np.equals(p2.getValue())) {
                notify("Passwords don't match or are empty", NotificationVariant.LUMO_ERROR);
                return;
            }
            try {
                accountService.resetPassword(code, np);
                notify("Password reset for " + code, NotificationVariant.LUMO_SUCCESS);
                dlg.close();
            } catch (Exception ex) {
                notify("Reset failed: " + ex.getMessage(), NotificationVariant.LUMO_ERROR);
            }
        });
        save.addThemeVariants(ButtonVariant.LUMO_PRIMARY);

        Button cancel = new Button("Cancel", e -> dlg.close());
        dlg.getFooter().add(cancel, save);
        dlg.open();
    }

    // ── Delete-account dialog ────────────────────────────────────────────

    private void openDeleteDialog(Account account) {
        String code = safe(account.getLoginCredentials(), LoginCredentials::getAccountCode);

        Dialog dlg = new Dialog();
        dlg.setHeaderTitle("Delete account — " + code);

        Div msg = new Div();
        msg.setText("This will permanently delete the account \"" + code + "\". "
                + "Any future login attempts with these credentials will fail. "
                + "This cannot be undone.");
        msg.getStyle()
                .set("max-width", "420px")
                .set("color", "var(--lumo-body-text-color)");
        dlg.add(msg);

        Button confirm = new Button("Delete", e -> {
            try {
                accountService.deleteAccount(code);
                notify("Deleted account " + code, NotificationVariant.LUMO_SUCCESS);
                dlg.close();
                refresh();
            } catch (Exception ex) {
                notify("Delete failed: " + ex.getMessage(), NotificationVariant.LUMO_ERROR);
            }
        });
        confirm.addThemeVariants(ButtonVariant.LUMO_PRIMARY, ButtonVariant.LUMO_ERROR);

        Button cancel = new Button("Cancel", e -> dlg.close());
        dlg.getFooter().add(cancel, confirm);
        dlg.open();
    }

    // ── Helpers ──────────────────────────────────────────────────────────

    private static <T, R> R safe(T obj, java.util.function.Function<T, R> getter) {
        return obj == null ? null : getter.apply(obj);
    }

    private static void notify(String msg, NotificationVariant variant) {
        Notification n = Notification.show(msg, 3500, Notification.Position.BOTTOM_START);
        n.addThemeVariants(variant);
    }
}
