package com.xpmile.onprem.ui;

import com.vaadin.flow.component.Key;
import com.vaadin.flow.component.button.Button;
import com.vaadin.flow.component.button.ButtonVariant;
import com.vaadin.flow.component.html.H1;
import com.vaadin.flow.component.html.Paragraph;
import com.vaadin.flow.component.html.Span;
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
        setPadding(false);
        setSpacing(false);
        setAlignItems(Alignment.CENTER);
        setJustifyContentMode(JustifyContentMode.CENTER);
        getStyle().set("background", "linear-gradient(135deg, #0f2744 0%, #1e5097 100%)");

        VerticalLayout card = new VerticalLayout();
        card.setAlignItems(Alignment.CENTER);
        card.setWidth("400px");
        card.setPadding(false);
        card.setSpacing(false);
        card.getStyle()
                .set("background", "white")
                .set("border-radius", "16px")
                .set("padding", "48px 40px 36px")
                .set("box-shadow", "0 24px 80px rgba(0,0,0,0.4)");

        Span logo = new Span("✈");
        logo.getStyle()
                .set("font-size", "3rem")
                .set("line-height", "1")
                .set("color", "#1e5097");

        H1 title = new H1("xpmile");
        title.getStyle()
                .set("margin", "12px 0 4px")
                .set("color", "#0f2744")
                .set("font-size", "2rem")
                .set("font-weight", "700")
                .set("letter-spacing", "-0.5px");

        Paragraph subtitle = new Paragraph("On-Premise Logistics Management");
        subtitle.getStyle()
                .set("margin", "0 0 32px")
                .set("color", "#718096")
                .set("font-size", "0.875rem");

        TextField usernameField = new TextField("Username");
        usernameField.setWidthFull();
        usernameField.setId("username");

        PasswordField passwordField = new PasswordField("Password");
        passwordField.setWidthFull();
        passwordField.setId("password");
        passwordField.getStyle().set("margin-top", "8px");

        Button loginButton = new Button("Sign In");
        loginButton.addThemeVariants(ButtonVariant.LUMO_PRIMARY, ButtonVariant.LUMO_LARGE);
        loginButton.setWidthFull();
        loginButton.addClickShortcut(Key.ENTER);
        loginButton.getStyle().set("margin-top", "16px");
        loginButton.addClickListener(e -> doLogin(usernameField.getValue(), passwordField.getValue()));

        Paragraph footer = new Paragraph("xpmile On-Premises v1.0");
        footer.getStyle()
                .set("margin", "28px 0 0")
                .set("color", "#a0aec0")
                .set("font-size", "0.75rem");

        card.add(logo, title, subtitle, usernameField, passwordField, loginButton, footer);
        add(card);
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
