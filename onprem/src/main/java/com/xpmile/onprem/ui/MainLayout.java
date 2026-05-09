package com.xpmile.onprem.ui;

import com.vaadin.flow.component.UI;
import com.vaadin.flow.component.applayout.AppLayout;
import com.vaadin.flow.component.applayout.DrawerToggle;
import com.vaadin.flow.component.button.Button;
import com.vaadin.flow.component.button.ButtonVariant;
import com.vaadin.flow.component.html.Div;
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
import com.xpmile.onprem.service.StatusService;
import com.xpmile.onprem.service.StatusService.BackendStatus;
import com.xpmile.onprem.ui.account.CreateAccountView;
import com.xpmile.onprem.ui.shipment.BulkShipmentView;
import com.xpmile.onprem.ui.shipment.CreateShipmentView;
import com.xpmile.onprem.ui.shipment.ModifyShipmentView;
import com.xpmile.onprem.ui.shipment.ShipmentListView;

public class MainLayout extends AppLayout implements BeforeEnterObserver {

    private static final int POLL_INTERVAL_MS = 30_000;

    private final StatusService statusService;

    public MainLayout(StatusService statusService) {
        this.statusService = statusService;

        addToNavbar(new DrawerToggle());

        H2 appName = new H2("xpmile");
        appName.getStyle().set("font-size", "var(--lumo-font-size-l)").set("margin", "0");

        Account account = (Account) VaadinSession.getCurrent().getAttribute("account");
        String userName = account != null && account.getLoginCredentials() != null
                ? account.getLoginCredentials().getAccountCode()
                : "";

        Span userSpan = new Span("Logged in as: " + userName);
        userSpan.getStyle()
                .set("font-size", "var(--lumo-font-size-s)")
                .set("color", "var(--lumo-secondary-text-color)");

        Div agentBadge = createBadge("Agent");
        Div dbBadge    = createBadge("DB");

        Button logoutButton = new Button("Logout");
        logoutButton.addThemeVariants(ButtonVariant.LUMO_TERTIARY, ButtonVariant.LUMO_ERROR);
        logoutButton.addClickListener(e -> {
            VaadinSession.getCurrent().close();
            UI.getCurrent().navigate("");
        });

        HorizontalLayout header = new HorizontalLayout(appName, userSpan, agentBadge, dbBadge, logoutButton);
        header.setDefaultVerticalComponentAlignment(FlexComponent.Alignment.CENTER);
        header.setWidthFull();
        header.expand(appName);
        header.getStyle().set("padding", "0 var(--lumo-space-m)");
        addToNavbar(header);

        SideNav nav = new SideNav();
        nav.addItem(new SideNavItem("Shipments",       ShipmentListView.class));
        nav.addItem(new SideNavItem("Create Shipment", CreateShipmentView.class));
        nav.addItem(new SideNavItem("Bulk Upload",     BulkShipmentView.class));
        nav.addItem(new SideNavItem("Modify Shipment", ModifyShipmentView.class));
        nav.addItem(new SideNavItem("Create Account",  CreateAccountView.class));
        addToDrawer(nav);

        UI ui = UI.getCurrent();
        ui.setPollInterval(POLL_INTERVAL_MS);
        ui.addPollListener(e -> scheduleStatusRefresh(ui, agentBadge, dbBadge));

        // initial check runs off the UI thread so it doesn't block first render
        scheduleStatusRefresh(ui, agentBadge, dbBadge);
    }

    private void scheduleStatusRefresh(UI ui, Div agentBadge, Div dbBadge) {
        new Thread(() -> {
            BackendStatus status = statusService.check();
            ui.access(() -> applyStatus(agentBadge, dbBadge, status));
        }).start();
    }

    private static void applyStatus(Div agentBadge, Div dbBadge, BackendStatus status) {
        setDotColor(agentBadge, status.agentOk());
        setDotColor(dbBadge,    status.dbOk());
    }

    private static Div createBadge(String label) {
        Span dot = new Span();
        dot.getStyle()
                .set("display", "inline-block")
                .set("width", "8px")
                .set("height", "8px")
                .set("border-radius", "50%")
                .set("background", "var(--lumo-disabled-text-color)")
                .set("margin-right", "4px");

        Span text = new Span(label);
        text.getStyle()
                .set("font-size", "var(--lumo-font-size-xs)")
                .set("color", "var(--lumo-secondary-text-color)");

        Div badge = new Div(dot, text);
        badge.getStyle().set("display", "flex").set("align-items", "center").set("gap", "2px");
        return badge;
    }

    private static void setDotColor(Div badge, boolean ok) {
        badge.getChildren().findFirst().ifPresent(dot ->
                dot.getElement().getStyle().set("background",
                        ok ? "var(--lumo-success-color)" : "var(--lumo-error-color)"));
    }

    @Override
    public void beforeEnter(BeforeEnterEvent event) {
        Account account = (Account) VaadinSession.getCurrent().getAttribute("account");
        if (account == null) {
            event.forwardTo(LoginView.class);
        }
    }
}
