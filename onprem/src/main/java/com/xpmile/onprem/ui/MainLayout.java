package com.xpmile.onprem.ui;

import com.vaadin.flow.component.UI;
import com.vaadin.flow.component.applayout.AppLayout;
import com.vaadin.flow.component.applayout.DrawerToggle;
import com.vaadin.flow.component.button.Button;
import com.vaadin.flow.component.button.ButtonVariant;
import com.vaadin.flow.component.html.H2;
import com.vaadin.flow.component.html.Span;
import com.vaadin.flow.component.orderedlayout.FlexComponent;
import com.vaadin.flow.component.orderedlayout.HorizontalLayout;
import com.vaadin.flow.component.sidenav.SideNav;
import com.vaadin.flow.component.sidenav.SideNavItem;
import com.vaadin.flow.router.BeforeEnterEvent;
import com.vaadin.flow.router.BeforeEnterObserver;
import com.vaadin.flow.server.VaadinSession;
import com.xpmile.onprem.model.Account;
import com.xpmile.onprem.ui.account.CreateAccountView;
import com.xpmile.onprem.ui.shipment.BulkShipmentView;
import com.xpmile.onprem.ui.shipment.CreateShipmentView;
import com.xpmile.onprem.ui.shipment.ModifyShipmentView;
import com.xpmile.onprem.ui.shipment.ShipmentListView;

public class MainLayout extends AppLayout implements BeforeEnterObserver {

    public MainLayout() {
        addToNavbar(new DrawerToggle());

        H2 appName = new H2("xpmile");
        appName.getStyle().set("font-size", "var(--lumo-font-size-l)").set("margin", "0");

        Account account = (Account) VaadinSession.getCurrent().getAttribute("account");
        String userName = account != null && account.getLoginCredentials() != null
                ? account.getLoginCredentials().getAccountCode()
                : "";

        Span userSpan = new Span("Logged in as: " + userName);
        userSpan.getStyle().set("font-size", "var(--lumo-font-size-s)").set("color", "var(--lumo-secondary-text-color)");

        Button logoutButton = new Button("Logout");
        logoutButton.addThemeVariants(ButtonVariant.LUMO_TERTIARY, ButtonVariant.LUMO_ERROR);
        logoutButton.addClickListener(e -> {
            VaadinSession.getCurrent().close();
            UI.getCurrent().navigate("");
        });

        HorizontalLayout header = new HorizontalLayout(appName, userSpan, logoutButton);
        header.setDefaultVerticalComponentAlignment(FlexComponent.Alignment.CENTER);
        header.setWidthFull();
        header.expand(appName);
        header.getStyle().set("padding", "0 var(--lumo-space-m)");
        addToNavbar(header);

        SideNav nav = new SideNav();
        nav.addItem(new SideNavItem("Shipments", ShipmentListView.class));
        nav.addItem(new SideNavItem("Create Shipment", CreateShipmentView.class));
        nav.addItem(new SideNavItem("Bulk Upload", BulkShipmentView.class));
        nav.addItem(new SideNavItem("Modify Shipment", ModifyShipmentView.class));
        nav.addItem(new SideNavItem("Create Account", CreateAccountView.class));
        addToDrawer(nav);
    }

    @Override
    public void beforeEnter(BeforeEnterEvent event) {
        Account account = (Account) VaadinSession.getCurrent().getAttribute("account");
        if (account == null) {
            event.forwardTo(LoginView.class);
        }
    }
}
