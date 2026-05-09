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

        // Dark branded navbar
        getStyle().set("--lumo-app-layout-bar-background", "#0f2744");

        DrawerToggle toggle = new DrawerToggle();
        toggle.getStyle().set("color", "white");
        addToNavbar(toggle);

        H2 appName = new H2("xpmile");
        appName.getStyle()
                .set("font-size", "var(--lumo-font-size-l)")
                .set("margin", "0")
                .set("color", "white")
                .set("font-weight", "700")
                .set("letter-spacing", "-0.3px");

        Account account = (Account) VaadinSession.getCurrent().getAttribute("account");
        String userName = account != null && account.getLoginCredentials() != null
                ? account.getLoginCredentials().getAccountCode()
                : "";

        Span userSpan = new Span(userName);
        userSpan.getStyle()
                .set("font-size", "var(--lumo-font-size-s)")
                .set("color", "rgba(255,255,255,0.7)")
                .set("background", "rgba(255,255,255,0.12)")
                .set("padding", "3px 10px")
                .set("border-radius", "12px");

        Div agentBadge = createBadge("Agent");
        Div dbBadge    = createBadge("DB");

        Button logoutButton = new Button("Logout");
        logoutButton.addThemeVariants(ButtonVariant.LUMO_TERTIARY);
        logoutButton.getStyle()
                .set("color", "rgba(255,255,255,0.8)")
                .set("border", "1px solid rgba(255,255,255,0.3)");
        logoutButton.addClickListener(e -> {
            VaadinSession.getCurrent().close();
            UI.getCurrent().navigate("");
        });

        HorizontalLayout header = new HorizontalLayout(appName, userSpan, agentBadge, dbBadge, logoutButton);
        header.setDefaultVerticalComponentAlignment(FlexComponent.Alignment.CENTER);
        header.setWidthFull();
        header.expand(appName);
        header.getStyle()
                .set("padding", "0 var(--lumo-space-m)")
                .set("background", "#0f2744");
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
                .set("background", "rgba(255,255,255,0.3)")
                .set("flex-shrink", "0");

        Span text = new Span(label);
        text.getStyle()
                .set("font-size", "var(--lumo-font-size-xs)")
                .set("color", "rgba(255,255,255,0.7)");

        Div badge = new Div(dot, text);
        badge.getStyle()
                .set("display", "flex")
                .set("align-items", "center")
                .set("gap", "5px")
                .set("background", "rgba(255,255,255,0.08)")
                .set("padding", "3px 8px")
                .set("border-radius", "10px");
        return badge;
    }

    private static void setDotColor(Div badge, boolean ok) {
        badge.getChildren().findFirst().ifPresent(dot ->
                dot.getElement().getStyle().set("background",
                        ok ? "#48bb78" : "#fc8181"));
    }

    @Override
    public void beforeEnter(BeforeEnterEvent event) {
        Account account = (Account) VaadinSession.getCurrent().getAttribute("account");
        if (account == null) {
            event.forwardTo(LoginView.class);
        }
    }
}
