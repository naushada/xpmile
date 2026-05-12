package com.xpmile.onprem.ui;

import com.vaadin.flow.component.UI;
import com.vaadin.flow.component.applayout.AppLayout;
import com.vaadin.flow.component.applayout.DrawerToggle;
import com.vaadin.flow.component.html.Div;
import com.vaadin.flow.component.html.H2;
import com.vaadin.flow.component.html.Span;
import com.vaadin.flow.component.orderedlayout.FlexComponent;
import com.vaadin.flow.component.orderedlayout.HorizontalLayout;
import com.vaadin.flow.component.sidenav.SideNav;
import com.vaadin.flow.component.sidenav.SideNavItem;
import com.xpmile.onprem.service.StatusService;
import com.xpmile.onprem.service.StatusService.BackendStatus;
import com.xpmile.onprem.ui.account.AccountsView;
import com.xpmile.onprem.ui.dashboard.DashboardView;
import com.xpmile.onprem.ui.shipment.ShipmentListView;

/**
 * Application shell for the on-prem Vaadin tool.
 *
 * No authentication: this UI is deployed on the customer's premises behind
 * their physical access controls. It exists as an admin / recovery tool —
 * the cloud-deployed Angular app has no self-service password reset, so
 * "I forgot my password" is handled here, where the operator is local.
 *
 * Scope is intentionally narrower than the Angular app: read-only
 * shipments (with live polling), account list + create + password reset,
 * and a monthly dashboard with PDF export. No daily-create-shipment flows.
 */
public class MainLayout extends AppLayout {

    private static final int POLL_INTERVAL_MS = 30_000;

    private final StatusService statusService;

    public MainLayout(StatusService statusService) {
        this.statusService = statusService;

        DrawerToggle toggle = new DrawerToggle();
        toggle.getStyle().set("color", "white");

        H2 appName = new H2("xpmile · on-prem");
        appName.getStyle()
                .set("font-size", "var(--lumo-font-size-l)")
                .set("margin", "0")
                .set("color", "white")
                .set("font-weight", "700")
                .set("letter-spacing", "-0.3px");

        Div agentBadge = createBadge("Agent");
        Div dbBadge    = createBadge("DB");

        // One full-width HorizontalLayout containing everything; this is the
        // only thing added to the navbar so the dark background stretches the
        // full row instead of just the inner content block.
        HorizontalLayout navbar = new HorizontalLayout(toggle, appName, agentBadge, dbBadge);
        navbar.setDefaultVerticalComponentAlignment(FlexComponent.Alignment.CENTER);
        navbar.setWidthFull();
        navbar.setSpacing(true);
        navbar.expand(appName);
        navbar.getStyle()
                .set("padding", "0 var(--lumo-space-m)")
                .set("background", "#0f2744")
                .set("min-height", "var(--lumo-size-xl)");
        addToNavbar(navbar);

        SideNav nav = new SideNav();
        nav.addItem(new SideNavItem("Dashboard", DashboardView.class));
        nav.addItem(new SideNavItem("Shipments", ShipmentListView.class));
        nav.addItem(new SideNavItem("Accounts",  AccountsView.class));
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
}
