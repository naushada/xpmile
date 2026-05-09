package com.xpmile.onprem.ui.account;

import com.vaadin.flow.component.button.Button;
import com.vaadin.flow.component.button.ButtonVariant;
import com.vaadin.flow.component.checkbox.Checkbox;
import com.vaadin.flow.component.combobox.ComboBox;
import com.vaadin.flow.component.formlayout.FormLayout;
import com.vaadin.flow.component.html.H4;
import com.vaadin.flow.component.notification.Notification;
import com.vaadin.flow.component.notification.NotificationVariant;
import com.vaadin.flow.component.orderedlayout.VerticalLayout;
import com.vaadin.flow.component.textfield.PasswordField;
import com.vaadin.flow.component.textfield.TextField;
import com.vaadin.flow.router.PageTitle;
import com.vaadin.flow.router.Route;
import com.xpmile.onprem.model.*;
import com.xpmile.onprem.service.AccountService;
import com.xpmile.onprem.ui.MainLayout;

import java.util.List;

@Route(value = "accounts/create", layout = MainLayout.class)
@PageTitle("Create Account | xpmile")
public class CreateAccountView extends VerticalLayout {

    private final AccountService accountService;

    // Login credentials
    private final Checkbox autoGenCode = new Checkbox("Auto-generate Account Code");
    private final TextField accountCode = new TextField("Account Code");
    private final PasswordField accountPassword = new PasswordField("Password");

    // Personal info
    private final ComboBox<String> role = new ComboBox<>("Role");
    private final TextField name = new TextField("Name");
    private final TextField contact = new TextField("Contact");
    private final TextField email = new TextField("Email");
    private final TextField address = new TextField("Address");
    private final TextField city = new TextField("City");
    private final TextField state = new TextField("State");
    private final TextField postalCode = new TextField("Postal Code");
    private final TextField eventLocation = new TextField("Event Location");

    // Customer info
    private final TextField companyName = new TextField("Company Name");
    private final TextField quotedAmount = new TextField("Quoted Amount");
    private final TextField tradingLicense = new TextField("Trading License");
    private final TextField vat = new TextField("VAT");
    private final TextField currency = new TextField("Currency");
    private final TextField bankAccount = new TextField("Bank Account Number");
    private final TextField iban = new TextField("IBAN");
    private final TextField awbPrefix = new TextField("AWB Prefix");

    public CreateAccountView(AccountService accountService) {
        this.accountService = accountService;

        role.setItems(List.of("admin", "user", "operator"));
        autoGenCode.addValueChangeListener(e -> accountCode.setEnabled(!e.getValue()));

        accountCode.setId("accountCode");
        accountPassword.setId("accountPassword");
        autoGenCode.setId("autoGenCode");

        FormLayout credSection = section("Login Credentials",
                autoGenCode, accountCode, accountPassword);

        FormLayout personalSection = section("Personal Information",
                role, name, contact, email, address, city, state, postalCode, eventLocation);

        FormLayout customerSection = section("Customer Information",
                companyName, quotedAmount, tradingLicense, vat,
                currency, bankAccount, iban, awbPrefix);

        Button create = new Button("Create Account");
        create.addThemeVariants(ButtonVariant.LUMO_PRIMARY);
        create.setId("createAccount");
        create.addClickListener(e -> submit());

        Button reset = new Button("Reset");
        reset.addClickListener(e -> resetForm());

        add(credSection, personalSection, customerSection, create, reset);
        setSizeFull();
        getStyle().set("overflow-y", "auto");
    }

    private FormLayout section(String title, com.vaadin.flow.component.Component... fields) {
        FormLayout form = new FormLayout();
        form.getElement().insertChild(0, new H4(title).getElement());
        form.add(fields);
        form.setResponsiveSteps(
                new FormLayout.ResponsiveStep("0", 1),
                new FormLayout.ResponsiveStep("500px", 2),
                new FormLayout.ResponsiveStep("900px", 3));
        return form;
    }

    private void submit() {
        Account account = new Account();
        account.setAccountCodeAutoGen(autoGenCode.getValue());
        account.setAwbPrefix(awbPrefix.getValue());

        LoginCredentials creds = new LoginCredentials(accountCode.getValue(), accountPassword.getValue());
        account.setLoginCredentials(creds);

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
            Notification n = Notification.show("Account created successfully", 3000,
                    Notification.Position.BOTTOM_START);
            n.addThemeVariants(NotificationVariant.LUMO_SUCCESS);
            n.setId("success-notification");
            resetForm();
        } catch (Exception ex) {
            Notification n = Notification.show("Error: " + ex.getMessage(), 4000,
                    Notification.Position.BOTTOM_START);
            n.addThemeVariants(NotificationVariant.LUMO_ERROR);
        }
    }

    private void resetForm() {
        autoGenCode.clear();
        accountCode.clear(); accountCode.setEnabled(true);
        accountPassword.clear();
        role.clear(); name.clear(); contact.clear(); email.clear();
        address.clear(); city.clear(); state.clear(); postalCode.clear(); eventLocation.clear();
        companyName.clear(); quotedAmount.clear(); tradingLicense.clear();
        vat.clear(); currency.clear(); bankAccount.clear(); iban.clear(); awbPrefix.clear();
    }
}
