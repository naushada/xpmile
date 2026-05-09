package com.xpmile.onprem.ui;

import com.vaadin.flow.component.Key;
import com.vaadin.flow.component.button.Button;
import com.vaadin.flow.component.button.ButtonVariant;
import com.vaadin.flow.component.html.H1;
import com.vaadin.flow.component.html.Paragraph;
import com.vaadin.flow.component.notification.Notification;
import com.vaadin.flow.component.notification.NotificationVariant;
import com.vaadin.flow.component.orderedlayout.VerticalLayout;
import com.vaadin.flow.component.textfield.PasswordField;
import com.vaadin.flow.component.textfield.TextField;
import com.vaadin.flow.router.PageTitle;
import com.vaadin.flow.router.Route;
import com.vaadin.flow.server.VaadinSession;
import com.xpmile.onprem.model.Account;
import com.xpmile.onprem.service.AuthException;
import com.xpmile.onprem.service.AuthService;

@Route("")
@PageTitle("xpmile | Login")
public class LoginView extends VerticalLayout {

    private final AuthService authService;

    public LoginView(AuthService authService) {
        this.authService = authService;

        setSizeFull();
        setAlignItems(Alignment.CENTER);
        setJustifyContentMode(JustifyContentMode.CENTER);

        H1 title = new H1("xpmile");
        Paragraph subtitle = new Paragraph("On-Premise Logistics Management");

        TextField usernameField = new TextField("Username");
        usernameField.setWidth("320px");
        usernameField.setId("username");

        PasswordField passwordField = new PasswordField("Password");
        passwordField.setWidth("320px");
        passwordField.setId("password");

        Button loginButton = new Button("Login");
        loginButton.addThemeVariants(ButtonVariant.LUMO_PRIMARY);
        loginButton.setWidth("320px");
        loginButton.addClickShortcut(Key.ENTER);

        loginButton.addClickListener(e -> doLogin(usernameField.getValue(), passwordField.getValue()));

        add(title, subtitle, usernameField, passwordField, loginButton);
    }

    private void doLogin(String username, String password) {
        try {
            Account account = authService.login(username, password);
            VaadinSession.getCurrent().setAttribute("account", account);
            getUI().ifPresent(ui -> ui.navigate("shipments"));
        } catch (AuthException ex) {
            Notification notification = Notification.show(ex.getMessage(), 3000, Notification.Position.MIDDLE);
            notification.addThemeVariants(NotificationVariant.LUMO_ERROR);
            notification.setId("login-error");
        }
    }
}
