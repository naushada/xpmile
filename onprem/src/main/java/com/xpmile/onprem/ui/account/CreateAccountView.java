package com.xpmile.onprem.ui.account;

import com.vaadin.flow.component.Component;
import com.vaadin.flow.component.UI;
import com.vaadin.flow.component.button.Button;
import com.vaadin.flow.component.button.ButtonVariant;
import com.vaadin.flow.component.combobox.ComboBox;
import com.vaadin.flow.component.formlayout.FormLayout;
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
import com.vaadin.flow.component.textfield.PasswordField;
import com.vaadin.flow.component.textfield.TextField;
import com.vaadin.flow.router.PageTitle;
import com.vaadin.flow.router.Route;
import com.xpmile.onprem.model.Account;
import com.xpmile.onprem.model.CustomerInfo;
import com.xpmile.onprem.model.LoginCredentials;
import com.xpmile.onprem.model.PersonalInfo;
import com.xpmile.onprem.service.AccountService;
import com.xpmile.onprem.ui.MainLayout;

import java.util.List;

/**
 * Dedicated full-page account creation form. Reached from AccountsView's
 * "Create Account" button. Three sections (Login / Personal / Customer)
 * matching the Angular CreateAccount layout, so the on-prem admin can
 * provision the same enterprise fields (VAT, IBAN, AWB prefix, etc.) if
 * needed.
 */
@Route(value = "accounts/create", layout = MainLayout.class)
@PageTitle("Create Account | xpmile")
public class CreateAccountView extends VerticalLayout {

    private final AccountService accountService;

    // Login credentials
    private final TextField     accountCode     = new TextField("Account Code");
    private final PasswordField accountPassword = new PasswordField("Password");

    // Personal info
    private final ComboBox<String> role        = new ComboBox<>("Role");
    private final TextField name              = new TextField("Name");
    private final TextField contact           = new TextField("Contact");
    private final TextField email             = new TextField("Email");
    private final TextField address           = new TextField("Address");
    private final TextField city              = new TextField("City");
    private final TextField state             = new TextField("State");
    private final TextField postalCode        = new TextField("Postal Code");
    private final TextField eventLocation     = new TextField("Event Location");

    // Customer info
    private final TextField companyName       = new TextField("Company Name");
    private final TextField quotedAmount      = new TextField("Quoted Amount");
    private final TextField tradingLicense    = new TextField("Trading License");
    private final TextField vat               = new TextField("VAT");
    private final TextField currency          = new TextField("Currency");
    private final TextField bankAccount       = new TextField("Bank Account Number");
    private final TextField iban              = new TextField("IBAN");
    private final TextField awbPrefix         = new TextField("AWB Prefix");

    public CreateAccountView(AccountService accountService) {
        this.accountService = accountService;

        setSizeFull();
        getStyle().set("padding", "16px").set("overflow-y", "auto");

        role.setItems(List.of("Admin", "Employee", "Customer"));
        role.setValue("Customer");

        add(buildHeader(), buildForm(), buildActionBar());
    }

    private Component buildHeader() {
        Div iconTile = new Div(new Icon(VaadinIcon.USER_CARD));
        iconTile.getStyle()
                .set("width", "44px").set("height", "44px")
                .set("display", "inline-flex")
                .set("align-items", "center").set("justify-content", "center")
                .set("border-radius", "8px")
                .set("background", "linear-gradient(135deg, #1b4d8e, #0f2d52)")
                .set("color", "#fff")
                .set("flex-shrink", "0");

        H3 title = new H3("Create Account");
        title.getStyle()
                .set("margin", "0")
                .set("font-size", "1.15rem")
                .set("color", "#0f2d52")
                .set("font-weight", "700");

        Span subtitle = new Span("Provision a new operator, employee, or customer login");
        subtitle.getStyle()
                .set("font-size", "0.78rem")
                .set("color", "#64748b")
                .set("display", "block")
                .set("margin-top", "2px");

        Div titleBlock = new Div(title, subtitle);

        HorizontalLayout bar = new HorizontalLayout(iconTile, titleBlock);
        bar.setDefaultVerticalComponentAlignment(FlexComponent.Alignment.CENTER);
        bar.setSpacing(true);
        bar.setWidthFull();

        Div panel = new Div(bar);
        panel.getStyle()
                .set("padding", "14px 18px")
                .set("background", "linear-gradient(90deg, #f8fafc, #eef2f7)")
                .set("border-left", "4px solid #1b4d8e")
                .set("border-radius", "8px")
                .set("margin-bottom", "18px");
        return panel;
    }

    private Component buildForm() {
        VerticalLayout col = new VerticalLayout();
        col.setPadding(false);
        col.setSpacing(true);
        col.add(
                sectionCard("Login Credentials",   VaadinIcon.KEY,         accountCode, accountPassword),
                sectionCard("Personal Information", VaadinIcon.USER,
                        role, name, contact, email, address, city, state, postalCode, eventLocation),
                sectionCard("Customer Information", VaadinIcon.BUILDING,
                        companyName, quotedAmount, tradingLicense, vat,
                        currency, bankAccount, iban, awbPrefix)
        );
        return col;
    }

    /** Section card: title row with icon + sectioned FormLayout below. */
    private Div sectionCard(String title, VaadinIcon iconName, Component... fields) {
        Icon icon = new Icon(iconName);
        icon.setSize("16px");
        icon.getStyle().set("color", "#1b4d8e");

        Span titleEl = new Span(title);
        titleEl.getStyle()
                .set("font-size", "0.85rem")
                .set("font-weight", "600")
                .set("color", "#0f2d52");

        HorizontalLayout head = new HorizontalLayout(icon, titleEl);
        head.setDefaultVerticalComponentAlignment(FlexComponent.Alignment.CENTER);
        head.setSpacing(true);
        head.getStyle().set("margin-bottom", "10px");

        FormLayout form = new FormLayout();
        form.add(fields);
        form.setResponsiveSteps(
                new FormLayout.ResponsiveStep("0",     1),
                new FormLayout.ResponsiveStep("520px", 2),
                new FormLayout.ResponsiveStep("900px", 3));

        Div card = new Div(head, form);
        card.getStyle()
                .set("padding", "16px 18px")
                .set("background", "#fff")
                .set("border-radius", "8px")
                .set("box-shadow", "0 1px 3px rgba(0,0,0,0.06), 0 8px 24px -16px rgba(15,45,82,0.18)");
        return card;
    }

    private Component buildActionBar() {
        Button cancel = new Button("Cancel", e ->
                UI.getCurrent().navigate(AccountsView.class));

        Button create = new Button("Create Account", new Icon(VaadinIcon.CHECK), e -> submit());
        create.addThemeVariants(ButtonVariant.LUMO_PRIMARY);

        HorizontalLayout bar = new HorizontalLayout(cancel, create);
        bar.setJustifyContentMode(FlexComponent.JustifyContentMode.END);
        bar.setSpacing(true);
        bar.setWidthFull();
        bar.getStyle()
                .set("position", "sticky")
                .set("bottom", "0")
                .set("background", "#fff")
                .set("padding", "12px 16px")
                .set("margin-top", "12px")
                .set("border-top", "1px solid #e2e8f0")
                .set("box-shadow", "0 -2px 8px rgba(0,0,0,0.04)");
        return bar;
    }

    // ── Submit ───────────────────────────────────────────────────────────

    private void submit() {
        if (accountCode.getValue().isBlank() || accountPassword.getValue().isBlank()) {
            notify("Account Code and Password are required.", NotificationVariant.LUMO_ERROR);
            return;
        }

        Account account = new Account();
        account.setAccountCodeAutoGen(false);
        account.setAwbPrefix(awbPrefix.getValue());

        account.setLoginCredentials(new LoginCredentials(
                accountCode.getValue(), accountPassword.getValue()));

        PersonalInfo pi = new PersonalInfo();
        pi.setRole(role.getValue());
        pi.setName(name.getValue());
        pi.setContact(contact.getValue());
        pi.setEmail(email.getValue());
        pi.setAddress(address.getValue());
        pi.setCity(city.getValue());
        pi.setState(state.getValue());
        pi.setPostalCode(postalCode.getValue());
        pi.setEventLocation(eventLocation.getValue());
        account.setPersonalInfo(pi);

        CustomerInfo ci = new CustomerInfo();
        ci.setCompanyName(companyName.getValue());
        ci.setQuotedAmount(quotedAmount.getValue());
        ci.setTradingLicense(tradingLicense.getValue());
        ci.setVat(vat.getValue());
        ci.setCurrency(currency.getValue());
        ci.setBankAccountNumber(bankAccount.getValue());
        ci.setIban(iban.getValue());
        account.setCustomerInfo(ci);

        try {
            accountService.createAccount(account);
            notify("Account " + accountCode.getValue() + " created.",
                    NotificationVariant.LUMO_SUCCESS);
            UI.getCurrent().navigate(AccountsView.class);
        } catch (Exception ex) {
            notify("Create failed: " + ex.getMessage(), NotificationVariant.LUMO_ERROR);
        }
    }

    private static void notify(String msg, NotificationVariant variant) {
        Notification n = Notification.show(msg, 3500, Notification.Position.BOTTOM_START);
        n.addThemeVariants(variant);
    }
}
