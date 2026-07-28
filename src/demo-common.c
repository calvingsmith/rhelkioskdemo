#include "demo-common.h"
#include <math.h>
#include <string.h>

static const char *CALLSIGNS[] = {
    "CIRRUS", "NORTHSTAR", "SKYLANE", "VECTOR", "HORIZON",
    "BLUEJAY", "ECHO", "ORBIT", "LANCER", "PIONEER"
};

static const char *AIRPORTS[] = {
    "KATL", "KJFK", "KORD", "KDFW", "KDEN", "KLAX", "KSEA", "KMIA"
};

static const char *WAYPOINTS[] = {
    "JAGES", "WILER", "TRUVE", "BEECH", "SULEN", "MUSIK", "RUGBY", "PARCH"
};

static const struct {
    DemoWindowKind kind;
    const char *id;
    const char *title;
    const char *role;
    const char *button_label;
    int default_width;
    int default_height;
} WINDOW_SPECS[] = {
    { WIN_RADAR, "radar", "EXTERNAL INTERFACES", "radar", "RAD", 1100, 760 },
    { WIN_TRAFFIC, "traffic", "TRAFFIC OVERVIEW", "radar", "TRF", 420, 260 },
    { WIN_CLEARANCE, "clearance", "CLEARANCE QUEUE", "tower", "CLR", 420, 260 },
    { WIN_GROUND, "ground", "GROUND OPS", "dispatch", "GRD", 420, 260 },
    { WIN_STATUS, "status", "SYSTEM STATUS", "control", "STS", 760, 170 },
};

static GtkWidget *build_radar_window_content(DemoApp *app);
static GtkWidget *build_menu_toolbar(DemoApp *app);
static void build_menu_bar_window(DemoApp *app);
static void refresh_menu_bar_chrome(DemoApp *app);
static gboolean initial_menu_bar_chrome_cb(gpointer user_data);
static void show_default_windows(DemoApp *app);
static void apply_factory_layout(DemoApp *app);
static gchar *generate_callsign(void);
static GtkWidget *build_window_header(const char *title);
static GtkWidget *build_thin_titlebar(DemoWindow *win);
static void clear_radar_cache(DemoApp *app);

static double
normalize_heading(double heading)
{
    while (heading < 0.0)
        heading += 360.0;
    while (heading >= 360.0)
        heading -= 360.0;
    return heading;
}

static void
init_contacts(DemoApp *app)
{
    for (guint i = 0; i < G_N_ELEMENTS(app->contacts); i++) {
        RadarContact *c = &app->contacts[i];
        g_autofree gchar *callsign = generate_callsign();
        g_strlcpy(c->callsign, callsign, sizeof(c->callsign));
        c->x = g_random_double_range(-0.75, 0.75);
        c->y = g_random_double_range(-0.75, 0.75);
        c->heading = g_random_double_range(0.0, 360.0);
        c->speed = g_random_double_range(210.0, 490.0);
    }
}

static void
advance_contacts(DemoApp *app)
{
    for (guint i = 0; i < G_N_ELEMENTS(app->contacts); i++) {
        RadarContact *c = &app->contacts[i];
        double step = (c->speed / 500.0) * 0.018;
        double angle = c->heading * (G_PI / 180.0);

        c->x += cos(angle) * step;
        c->y += sin(angle) * step;

        if (c->x > 0.92 || c->x < -0.92) {
            c->x = CLAMP(c->x, -0.92, 0.92);
            c->heading = normalize_heading(180.0 - c->heading + g_random_double_range(-6.0, 6.0));
        }
        if (c->y > 0.92 || c->y < -0.92) {
            c->y = CLAMP(c->y, -0.92, 0.92);
            c->heading = normalize_heading(-c->heading + g_random_double_range(-6.0, 6.0));
        }

        c->heading = normalize_heading(c->heading + g_random_double_range(-2.0, 2.0));
    }
}

static gchar *
config_path_for_app(void)
{
    gchar *dir = g_build_filename(g_get_user_config_dir(), CONFIG_DIR_NAME, NULL);
    g_mkdir_with_parents(dir, 0700);
    gchar *path = g_build_filename(dir, CONFIG_FILE_NAME, NULL);
    g_free(dir);
    return path;
}

static DemoWindow *
find_window(DemoApp *app, DemoWindowKind kind)
{
    return &app->windows[kind];
}

static gboolean
target_to_kind(const char *target, DemoWindowKind *kind)
{
    if (g_strcmp0(target, "traffic") == 0)
        *kind = WIN_TRAFFIC;
    else if (g_strcmp0(target, "clearance") == 0)
        *kind = WIN_CLEARANCE;
    else if (g_strcmp0(target, "ground") == 0)
        *kind = WIN_GROUND;
    else if (g_strcmp0(target, "status") == 0)
        *kind = WIN_STATUS;
    else if (g_strcmp0(target, "radar") == 0)
        *kind = WIN_RADAR;
    else
        return FALSE;
    return TRUE;
}

void
update_control_button_state(DemoWindow *win)
{
    if (win->control_button == NULL || win->button_label == NULL)
        return;

    if (!win->visible) {
        g_autofree gchar *label = g_strdup_printf("(%s)", win->button_label);
        gtk_button_set_label(GTK_BUTTON(win->control_button), label);
        return;
    }

    if (win->minimized) {
        g_autofree gchar *label = g_strdup_printf("[%s]", win->button_label);
        gtk_button_set_label(GTK_BUTTON(win->control_button), label);
        return;
    }

    gtk_button_set_label(GTK_BUTTON(win->control_button), win->button_label);
}

static void
set_window_minimized(DemoWindow *win, gboolean minimized)
{
    if (win->kind == WIN_RADAR)
        return;

    /* Known open gap (2026-07-09): plain GTK4 gtk_window_minimize()/
     * unminimize() work correctly on Fedora44 (mutter 50.1) but are not
     * honored by RHEL10's mutter 49.4 under gnome-kiosk. backend_set_
     * window_minimized() is the per-build fix: gnome-kiosk.c keeps the
     * plain GTK4 calls (unchanged behavior there), gnome-shell.c routes
     * this through the Kiosk Layout Helper extension's real Meta.Window
     * primitives instead, which are reliable regardless of compositor
     * version. See CLAUDE.md "Window minimize" section. */
    backend_set_window_minimized(win, minimized);
    if (minimized) {
        win->minimized = TRUE;
        win->visible = TRUE;
    } else {
        gtk_widget_set_visible(win->window, TRUE);
        gtk_window_present(GTK_WINDOW(win->window));
        win->minimized = FALSE;
        win->visible = TRUE;
    }

    update_control_button_state(win);
}

static gchar *
generate_callsign(void)
{
    const char *prefix = CALLSIGNS[g_random_int_range(0, G_N_ELEMENTS(CALLSIGNS))];
    return g_strdup_printf("%s%u", prefix, 10 + g_random_int_range(0, 90));
}

static const char *
pick_string(const char *const *items, gsize n_items)
{
    return items[g_random_int_range(0, (gint)n_items)];
}

static gchar *
generate_traffic_text(guint generation)
{
    g_autofree gchar *callsign = generate_callsign();
    const char *origin = pick_string(AIRPORTS, G_N_ELEMENTS(AIRPORTS));
    const char *dest = pick_string(AIRPORTS, G_N_ELEMENTS(AIRPORTS));
    const char *wp1 = pick_string(WAYPOINTS, G_N_ELEMENTS(WAYPOINTS));
    const char *wp2 = pick_string(WAYPOINTS, G_N_ELEMENTS(WAYPOINTS));

    return g_strdup_printf(
        "%s\n"
        "ROLE  : radar\n"
        "ROUTE : %s -> %s\n"
        "FIXES : %s / %s\n"
        "ALT   : FL%u\n"
        "SPD   : %u KT\n"
        "HDG   : %03u\n"
        "SQUAWK: %u\n"
        "ETA   : +%02u MIN\n"
        "GEN   : %u",
        callsign,
        origin,
        dest,
        wp1,
        wp2,
        180 + g_random_int_range(0, 210),
        220 + g_random_int_range(0, 270),
        1 + g_random_int_range(0, 358),
        1000 + g_random_int_range(0, 6777),
        1 + g_random_int_range(0, 59),
        generation);
}

static gchar *
generate_clearance_text(guint generation)
{
    g_autofree gchar *callsign = generate_callsign();
    const char *origin = pick_string(AIRPORTS, G_N_ELEMENTS(AIRPORTS));
    const char *dest = pick_string(AIRPORTS, G_N_ELEMENTS(AIRPORTS));
    const char *wp1 = pick_string(WAYPOINTS, G_N_ELEMENTS(WAYPOINTS));
    const char *wp2 = pick_string(WAYPOINTS, G_N_ELEMENTS(WAYPOINTS));

    return g_strdup_printf(
        "CLEARANCE QUEUE / CLEARANCE\n"
        "FLT  : %s\n"
        "DEP  : %s\n"
        "ARR  : %s\n"
        "ROUTE: %s %s\n"
        "RWY  : %02u\n"
        "NOTE : EXPECT HOLD SHORT\n"
        "GEN  : %u",
        callsign, origin, dest, wp1, wp2,
        1 + g_random_int_range(0, 35), generation);
}

static gchar *
generate_ground_text(guint generation)
{
    g_autofree gchar *callsign = generate_callsign();
    const char *station = pick_string(AIRPORTS, G_N_ELEMENTS(AIRPORTS));

    return g_strdup_printf(
        "GROUND OPS\n"
        "ACFT : %s\n"
        "STN  : %s\n"
        "GATE : B%02u\n"
        "LOAD : %u PALLETS\n"
        "TUG  : ASSIGNED\n"
        "BAG  : RELEASE PENDING\n"
        "GEN  : %u",
        callsign,
        station,
        1 + g_random_int_range(0, 98),
        10 + g_random_int_range(0, 240),
        generation);
}

static gchar *
generate_status_text(DemoApp *app)
{
    return g_strdup_printf(
        "SESSION : kiosk demo\n"
        "ROLE    : operator console\n"
        "APP     : %s\n"
        "WINDOWS : %u\n"
        "UPDATES : %u\n"
        "NOTE    : windows are now managed by Mutter\n",
        app->config_path,
        (guint)WIN_COUNT,
        app->generation);
}

static void
set_status_text(DemoWindow *win, const gchar *text)
{
    if (win->buffer != NULL)
        gtk_text_buffer_set_text(win->buffer, text, -1);
    else if (win->status_label != NULL)
        gtk_label_set_text(GTK_LABEL(win->status_label), text);
}

static void
update_text_windows(DemoApp *app)
{
    g_autofree gchar *traffic = generate_traffic_text(app->generation);
    g_autofree gchar *clearance = generate_clearance_text(app->generation);
    g_autofree gchar *ground = generate_ground_text(app->generation);
    g_autofree gchar *status = generate_status_text(app);

    set_status_text(find_window(app, WIN_TRAFFIC), traffic);
    set_status_text(find_window(app, WIN_CLEARANCE), clearance);
    set_status_text(find_window(app, WIN_GROUND), ground);
    set_status_text(find_window(app, WIN_STATUS), status);

    g_autofree gchar *menu_status = g_strdup_printf("Radar sweep %u complete; layout can be reset from top controls.", app->generation);
    gtk_label_set_text(GTK_LABEL(find_window(app, WIN_RADAR)->status_label), menu_status);
}

static void
show_window(DemoWindow *win)
{
    if (win->window != NULL) {
        win->visible = TRUE;
        win->minimized = FALSE;
        gtk_widget_set_visible(win->window, TRUE);
        gtk_window_unminimize(GTK_WINDOW(win->window));
        gtk_window_present(GTK_WINDOW(win->window));
        /* Plain GTK4 present()/unminimize() above are not reliably honored
         * by this compositor (the same gap already worked around for the
         * minimize button); this additionally raises/focuses via the real
         * Meta.Window path on the gnome-shell build, and re-lowers radar
         * as a side effect so it never ends up back in front. */
        backend_set_window_minimized(win, FALSE);
        update_control_button_state(win);
    }
}

static void
show_window_by_kind(DemoApp *app, DemoWindowKind kind)
{
    show_window(find_window(app, kind));
}

static void
on_menu_button_clicked(GtkButton *button, gpointer user_data)
{
    DemoApp *app = user_data;
    const char *target = g_object_get_data(G_OBJECT(button), "target");
    DemoWindowKind kind;

    if (target_to_kind(target, &kind)) {
        DemoWindow *win = find_window(app, kind);
        if (kind != WIN_RADAR) {
            if (win->minimized || !win->visible)
                set_window_minimized(win, FALSE);
            else
                set_window_minimized(win, TRUE);
        } else {
            show_window_by_kind(app, kind);
        }
    } else if (g_strcmp0(target, "all") == 0) {
        for (guint i = 0; i < WIN_COUNT; i++)
            show_window(&app->windows[i]);
    } else if (g_strcmp0(target, "save") == 0) {
        save_layout(app);
    } else if (g_strcmp0(target, "reset") == 0) {
        app->generation = 0;
        apply_factory_layout(app);
        show_default_windows(app);
        update_text_windows(app);
        if (app->use_separate_menu_bar)
            g_timeout_add(200, initial_menu_bar_chrome_cb, app);
    } else if (g_strcmp0(target, "load") == 0) {
        if (!apply_saved_layout(app))
            apply_factory_layout(app);
        show_default_windows(app);
        update_text_windows(app);
        if (app->use_separate_menu_bar)
            g_timeout_add(200, initial_menu_bar_chrome_cb, app);
    }
}

static void
on_window_minimize_clicked(GtkButton *button, gpointer user_data)
{
    (void) button;
    DemoWindow *win = user_data;
    set_window_minimized(win, TRUE);
}

static GtkWidget *
build_thin_titlebar(DemoWindow *win)
{
    GtkWidget *bar = gtk_header_bar_new();
    GtkWidget *min_btn = gtk_button_new_with_label("_");
    GtkWidget *label = gtk_label_new(win->title);

    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(bar), FALSE);
    gtk_widget_add_css_class(bar, "motif-thin-titlebar");
    gtk_widget_add_css_class(min_btn, "motif-thin-title-button");
    gtk_widget_add_css_class(label, "motif-thin-title-label");
    gtk_widget_set_tooltip_text(min_btn, "Minimize");
    gtk_widget_set_focusable(min_btn, FALSE);

    g_signal_connect(min_btn, "clicked", G_CALLBACK(on_window_minimize_clicked), win);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(bar), min_btn);
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(bar), label);

    return bar;
}

static GtkWidget *
build_window_header(const char *title)
{
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(bar, "motif-panel-header");

    GtkWidget *label = gtk_label_new(title);
    gtk_widget_add_css_class(label, "motif-panel-title");
    gtk_widget_set_hexpand(label, TRUE);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(bar), label);

    return bar;
}

static GtkWidget *
build_text_window_content(DemoWindow *win, GtkTextBuffer **out_buffer)
{
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *scroller = gtk_scrolled_window_new();
    GtkWidget *view = gtk_text_view_new();

    gtk_widget_add_css_class(outer, "motif-panel");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(scroller, TRUE);
    gtk_widget_set_vexpand(scroller, TRUE);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_NONE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    gtk_widget_add_css_class(view, "motif-text");

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), view);
    gtk_box_append(GTK_BOX(outer), scroller);

    if (win->kind == WIN_GROUND) {
        GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget *cb1 = gtk_check_button_new_with_label("Suppress Date");
        GtkWidget *cb2 = gtk_check_button_new_with_label("Suppress Time");
        GtkWidget *cb3 = gtk_check_button_new_with_label("Suppress Processor");

        gtk_widget_add_css_class(controls, "motif-control-row");
        gtk_widget_add_css_class(cb1, "motif-check");
        gtk_widget_add_css_class(cb2, "motif-check");
        gtk_widget_add_css_class(cb3, "motif-check");

        gtk_box_append(GTK_BOX(controls), cb1);
        gtk_box_append(GTK_BOX(controls), cb2);
        gtk_box_append(GTK_BOX(controls), cb3);
        gtk_box_append(GTK_BOX(outer), controls);
    }

    *out_buffer = buffer;
    return outer;
}

static GtkWidget *
build_status_window_content(DemoWindow *win, GtkWidget **out_label)
{
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *label = gtk_label_new("Ready");
    GtkWidget *form = gtk_grid_new();
    GtkWidget *user_label = gtk_label_new("USER ID:");
    GtkWidget *pass_label = gtk_label_new("PASSWORD:");
    GtkWidget *user_entry = gtk_entry_new();
    GtkWidget *pass_entry = gtk_entry_new();
    GtkWidget *sign_on = gtk_button_new_with_label("SIGN ON");
    GtkWidget *close = gtk_button_new_with_label("MINIMIZE");

    gtk_widget_add_css_class(outer, "motif-panel");
    gtk_widget_add_css_class(label, "motif-status-body");
    gtk_widget_add_css_class(form, "motif-control-row");
    gtk_widget_add_css_class(user_entry, "motif-entry");
    gtk_widget_add_css_class(pass_entry, "motif-entry");
    gtk_widget_add_css_class(sign_on, "motif-button");
    gtk_widget_add_css_class(close, "motif-button");
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_entry_set_visibility(GTK_ENTRY(pass_entry), FALSE);

    gtk_grid_set_column_spacing(GTK_GRID(form), 8);
    gtk_grid_set_row_spacing(GTK_GRID(form), 4);
    gtk_grid_attach(GTK_GRID(form), user_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(form), user_entry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(form), pass_label, 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(form), pass_entry, 3, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(form), sign_on, 4, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(form), close, 5, 0, 1, 1);

    g_signal_connect(close, "clicked", G_CALLBACK(on_window_minimize_clicked), win);

    gtk_box_append(GTK_BOX(outer), label);
    gtk_box_append(GTK_BOX(outer), form);

    *out_label = label;
    return outer;
}

static gboolean
on_close_request(GtkWindow *window, gpointer user_data)
{
    DemoWindow *win = user_data;
    gtk_widget_set_visible(GTK_WIDGET(window), FALSE);
    win->visible = FALSE;
    win->minimized = FALSE;
    update_control_button_state(win);
    return TRUE;
}

static void
clear_radar_cache(DemoApp *app)
{
    if (app->radar_static_surface != NULL) {
        cairo_surface_destroy(app->radar_static_surface);
        app->radar_static_surface = NULL;
    }
    app->radar_cache_width = 0;
    app->radar_cache_height = 0;
}

static void
draw_radar_static_layer(cairo_t *cr, int width, int height)
{
    double cx = width / 2.0;
    double cy = height / 2.0;
    double radius = MIN(width, height) / 2.0 - 40.0;

    cairo_set_source_rgb(cr, 0.60, 0.60, 0.60);
    cairo_paint(cr);

    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, 0.24, 0.24, 0.24, 0.75);
    for (int i = 1; i <= 5; i++) {
        cairo_arc(cr, cx, cy, radius * i / 5.0, 0, G_PI * 2.0);
        cairo_stroke(cr);
    }

    cairo_move_to(cr, 20, cy);
    cairo_line_to(cr, width - 20, cy);
    cairo_move_to(cr, cx, 20);
    cairo_line_to(cr, cx, height - 20);
    cairo_stroke(cr);

    for (int i = 0; i < 10; i++) {
        double angle = (i * G_PI) / 5.0;
        cairo_move_to(cr, cx, cy);
        cairo_line_to(cr, cx + cos(angle) * radius, cy + sin(angle) * radius);
    }
    cairo_stroke(cr);

    cairo_set_source_rgba(cr, 0.12, 0.12, 0.12, 0.9);
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 13.0);
    cairo_move_to(cr, 28, 32);
    cairo_show_text(cr, "EXTERNAL INTERFACES");
    cairo_move_to(cr, 28, 52);
    cairo_show_text(cr, "RADAR IMAGE / FLIGHT CONTROL SURFACE");
}

static void
ensure_radar_cache(DemoApp *app, int width, int height)
{
    if (width <= 0 || height <= 0)
        return;

    if (app->radar_static_surface != NULL &&
        app->radar_cache_width == width &&
        app->radar_cache_height == height)
        return;

    clear_radar_cache(app);
    app->radar_static_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    app->radar_cache_width = width;
    app->radar_cache_height = height;

    cairo_t *cache_cr = cairo_create(app->radar_static_surface);
    draw_radar_static_layer(cache_cr, width, height);
    cairo_destroy(cache_cr);
}

static void
draw_radar(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data)
{
    DemoApp *app = user_data;
    (void)area;

    ensure_radar_cache(app, width, height);
    if (app->radar_static_surface != NULL) {
        cairo_set_source_surface(cr, app->radar_static_surface, 0, 0);
        cairo_paint(cr);
    }

    double cx = width / 2.0;
    double cy = height / 2.0;
    double radius = MIN(width, height) / 2.0 - 40.0;

    cairo_set_source_rgba(cr, 0.08, 0.30, 0.16, 0.14);
    cairo_arc(cr, cx, cy, radius * 0.82, app->sweep_angle - 0.16, app->sweep_angle);
    cairo_line_to(cr, cx, cy);
    cairo_close_path(cr);
    cairo_fill(cr);

    for (guint i = 0; i < G_N_ELEMENTS(app->contacts); i++) {
        RadarContact *c = &app->contacts[i];
        double px = cx + c->x * radius;
        double py = cy + c->y * radius;
        char label[64];

        cairo_set_source_rgba(cr, 0.10, 0.25, 0.12, 0.95);
        cairo_arc(cr, px, py, 2.8, 0, G_PI * 2.0);
        cairo_fill(cr);

        g_snprintf(label, sizeof(label), "%s H%03u %uKT",
            c->callsign,
            (guint)c->heading,
            (guint)c->speed);
        cairo_set_source_rgba(cr, 0.14, 0.28, 0.14, 0.92);
        cairo_set_font_size(cr, 10.0);
        cairo_move_to(cr, px + 6.0, py - 6.0);
        cairo_show_text(cr, label);
    }
}

static void
apply_css(DemoApp *app)
{
    app->css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(
        app->css_provider,
        "* { border-radius: 0; }"
        ".motif-panel, .motif-panel-header, .motif-panel-title, .motif-toolbar, .motif-button, .motif-text, .motif-status-body { font-family: 'Liberation Mono', 'Nimbus Mono PS', monospace; font-variant: small-caps; letter-spacing: 0.35px; }"
        ".motif-panel { background: #a9a9a9; border: 1px solid #efefef; border-right-color: #4f4f4f; border-bottom-color: #4f4f4f; padding: 3px; }"
        ".motif-panel-header { background: #7eaeb1; border: 1px solid #acd0d2; border-right-color: #45686a; border-bottom-color: #45686a; padding: 0px 6px; min-height: 18px; }"
        ".motif-panel-title { color: #f3f3f3; font-weight: 700; font-size: 10pt; }"
        ".motif-toolbar { background: #9f9f9f; padding: 1px; border: 1px solid #ececec; border-right-color: #595959; border-bottom-color: #595959; }"
        ".motif-layout-label { color: #202020; font-size: 8.5pt; font-weight: 700; font-family: 'Liberation Mono', 'Nimbus Mono PS', monospace; font-variant: small-caps; padding: 0px 2px; }"
        ".motif-button { background: #bebebe; color: #111; border: 1px solid #dcdcdc; border-top-color: #f6f6f6; border-left-color: #f6f6f6; border-right-color: #505050; border-bottom-color: #505050; min-height: 18px; min-width: 40px; padding: 0px 6px; font-weight: 700; font-size: 9.5pt; }"
        ".motif-button:hover { background: #c6c6c6; }"
        ".motif-button:active { background: #adadad; border-top-color: #505050; border-left-color: #505050; border-right-color: #f6f6f6; border-bottom-color: #f6f6f6; }"
        ".motif-thin-titlebar { min-height: 16px; padding: 0px 4px; border: 1px solid #c8c8c8; border-right-color: #575757; border-bottom-color: #575757; background: #bdbdbd; }"
        ".motif-thin-titlebar .title { min-height: 16px; margin: 0; }"
        ".motif-thin-title-button { background: #bebebe; color: #111; border: 1px solid #dcdcdc; border-top-color: #f6f6f6; border-left-color: #f6f6f6; border-right-color: #505050; border-bottom-color: #505050; min-height: 14px; min-width: 18px; padding: 0px 2px; font-weight: 700; }"
        ".motif-thin-title-label { color: #202020; font-size: 9pt; font-weight: 700; font-family: 'Liberation Mono', 'Nimbus Mono PS', monospace; font-variant: small-caps; }"
        ".motif-text { background: #d5d5d5; color: #101010; padding: 4px; border: 1px solid #5c5c5c; border-top-color: #f0f0f0; border-left-color: #f0f0f0; font-size: 9.5pt; }"
        ".motif-status-body { background: #d5d5d5; color: #101010; padding: 4px; border: 1px solid #5c5c5c; border-top-color: #f0f0f0; border-left-color: #f0f0f0; font-size: 9.5pt; }"
        ".motif-control-row { background: #b0b0b0; padding: 4px; border-top: 1px solid #e7e7e7; border-left: 1px solid #e7e7e7; border-right: 1px solid #5d5d5d; border-bottom: 1px solid #5d5d5d; }"
        ".motif-entry { min-width: 120px; background: #d6d6d6; color: #111; border: 1px solid #5d5d5d; border-top-color: #efefef; border-left-color: #efefef; padding: 1px 4px; }"
        ".motif-check { color: #111; }"
        ".radar-surface { background: #9b9b9b; border: 1px solid #5d5d5d; border-top-color: #f0f0f0; border-left-color: #f0f0f0; }");

    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(app->css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static void
create_window(DemoApp *app, DemoWindow *win, const char *title, int width, int height)
{
    win->window = gtk_application_window_new(app->application);
    gtk_window_set_title(GTK_WINDOW(win->window), title);
    gtk_window_set_default_size(GTK_WINDOW(win->window), width, height);
    /* Radar is a pure background layer on the gnome-shell build (its own
     * title/status moved into the standalone menu bar -- see
     * build_menu_toolbar()), so it gets no window decorations there either.
     * gnome-kiosk has no separate bar and keeps radar decorated, unchanged. */
    gtk_window_set_decorated(GTK_WINDOW(win->window),
        !(win->kind == WIN_RADAR && app->use_separate_menu_bar));
    if (win->kind != WIN_RADAR)
        gtk_window_set_titlebar(GTK_WINDOW(win->window), build_thin_titlebar(win));
    g_signal_connect(win->window, "close-request", G_CALLBACK(on_close_request), win);
}

static void
build_windows(DemoApp *app)
{
    for (guint i = 0; i < WIN_COUNT; i++) {
        DemoWindow *win = &app->windows[i];
        win->kind = WINDOW_SPECS[i].kind;
        win->id = WINDOW_SPECS[i].id;
        win->title = WINDOW_SPECS[i].title;
        win->role = WINDOW_SPECS[i].role;
        win->button_label = WINDOW_SPECS[i].button_label;
        win->default_width = WINDOW_SPECS[i].default_width;
        win->default_height = WINDOW_SPECS[i].default_height;
        win->visible = TRUE;
        win->minimized = FALSE;

        create_window(app, win, win->title, win->default_width, win->default_height);
    }

    gtk_window_set_child(GTK_WINDOW(app->windows[WIN_RADAR].window), build_radar_window_content(app));
    gtk_window_set_child(GTK_WINDOW(app->windows[WIN_TRAFFIC].window), build_text_window_content(&app->windows[WIN_TRAFFIC], &app->windows[WIN_TRAFFIC].buffer));
    gtk_window_set_child(GTK_WINDOW(app->windows[WIN_CLEARANCE].window), build_text_window_content(&app->windows[WIN_CLEARANCE], &app->windows[WIN_CLEARANCE].buffer));
    gtk_window_set_child(GTK_WINDOW(app->windows[WIN_GROUND].window), build_text_window_content(&app->windows[WIN_GROUND], &app->windows[WIN_GROUND].buffer));
    gtk_window_set_child(GTK_WINDOW(app->windows[WIN_STATUS].window), build_status_window_content(&app->windows[WIN_STATUS], &app->windows[WIN_STATUS].status_label));
    gtk_window_set_resizable(GTK_WINDOW(app->windows[WIN_TRAFFIC].window), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(app->windows[WIN_CLEARANCE].window), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(app->windows[WIN_GROUND].window), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(app->windows[WIN_STATUS].window), TRUE);

    if (app->use_separate_menu_bar)
        build_menu_bar_window(app);
}

/* The "M & C GLOBAL MENU" toolbar: window buttons (TRF/CLR/GRD/STS/RAD/ALL)
 * plus the SAVE/LOAD/RESET layout actions. Embedded at the top of the
 * radar window normally; built as the sole content of its own standalone
 * window when app->use_separate_menu_bar is set (see build_menu_bar_window
 * and configure_menu_bar_window). */
static GtkWidget *
build_menu_toolbar(DemoApp *app)
{
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *header = build_window_header("M & C GLOBAL MENU");
    GtkWidget *button_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *window_buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *layout_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    GtkWidget *layout_label = gtk_label_new("Layout");
    GtkWidget *layout_buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    const struct {
        const char *label;
        const char *target;
    } buttons[] = {
        { "TRF", "traffic" },
        { "CLR", "clearance" },
        { "GRD", "ground" },
        { "STS", "status" },
        { "RAD", "radar" },
        { "ALL", "all" },
    };
    const struct {
        const char *label;
        const char *target;
    } layout_actions[] = {
        { "SAVE", "save" },
        { "LOAD", "load" },
        { "RESET", "reset" },
    };

    gtk_widget_add_css_class(outer, "motif-panel");
    gtk_widget_add_css_class(button_row, "motif-toolbar");
    gtk_widget_add_css_class(layout_label, "motif-layout-label");
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_widget_set_halign(layout_section, GTK_ALIGN_END);
    gtk_widget_set_halign(layout_label, GTK_ALIGN_END);

    for (guint i = 0; i < G_N_ELEMENTS(buttons); i++) {
        DemoWindowKind kind;

        /* On the gnome-shell build, radar is a pure background layer that
         * is never brought to front (see lowerRadar() in the extension), so
         * a button to "show" it no longer means anything; gnome-kiosk has
         * no such concept and keeps this button, unchanged. */
        if (app->use_separate_menu_bar && g_strcmp0(buttons[i].target, "radar") == 0)
            continue;

        GtkWidget *btn = gtk_button_new_with_label(buttons[i].label);
        gtk_widget_add_css_class(btn, "motif-button");
        g_object_set_data_full(G_OBJECT(btn), "target", g_strdup(buttons[i].target), g_free);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_menu_button_clicked), app);
        gtk_box_append(GTK_BOX(window_buttons), btn);

        if (target_to_kind(buttons[i].target, &kind))
            app->windows[kind].control_button = btn;
    }

    for (guint i = 0; i < G_N_ELEMENTS(layout_actions); i++) {
        GtkWidget *btn = gtk_button_new_with_label(layout_actions[i].label);
        gtk_widget_add_css_class(btn, "motif-button");
        g_object_set_data_full(G_OBJECT(btn), "target", g_strdup(layout_actions[i].target), g_free);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_menu_button_clicked), app);
        gtk_box_append(GTK_BOX(layout_buttons), btn);
    }

    gtk_box_append(GTK_BOX(layout_section), layout_label);
    gtk_box_append(GTK_BOX(layout_section), layout_buttons);
    gtk_box_append(GTK_BOX(button_row), window_buttons);
    gtk_box_append(GTK_BOX(button_row), spacer);
    gtk_box_append(GTK_BOX(button_row), layout_section);

    /* On the gnome-shell build, radar's own title and live status text move
     * here (radar itself becomes pure background canvas -- see
     * build_radar_window_content()), sitting to the right of the layout
     * buttons so they stay visible regardless of what's on top of radar. */
    if (app->use_separate_menu_bar) {
        GtkWidget *radar_info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        GtkWidget *radar_title = gtk_label_new(app->windows[WIN_RADAR].title);
        GtkWidget *radar_status = gtk_label_new("Booting...");

        gtk_widget_add_css_class(radar_title, "motif-panel-title");
        gtk_widget_add_css_class(radar_status, "motif-status-body");
        gtk_widget_set_halign(radar_title, GTK_ALIGN_END);
        gtk_widget_set_halign(radar_status, GTK_ALIGN_END);
        gtk_widget_set_margin_start(radar_info, 12);
        gtk_box_append(GTK_BOX(radar_info), radar_title);
        gtk_box_append(GTK_BOX(radar_info), radar_status);
        gtk_box_append(GTK_BOX(button_row), radar_info);

        app->windows[WIN_RADAR].status_label = radar_status;
    }

    gtk_box_append(GTK_BOX(outer), header);
    gtk_box_append(GTK_BOX(outer), button_row);

    return outer;
}

/* Standalone, undecorated toplevel holding just the menu toolbar. Its
 * title (MENU_BAR_TITLE) is how the gnome-shell extension finds it to
 * pin as an unmovable always-on-top bar; see configure_menu_bar_window(). */
static void
build_menu_bar_window(DemoApp *app)
{
    app->menu_bar_window = gtk_application_window_new(app->application);
    gtk_window_set_title(GTK_WINDOW(app->menu_bar_window), MENU_BAR_TITLE);
    gtk_window_set_decorated(GTK_WINDOW(app->menu_bar_window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(app->menu_bar_window), FALSE);
    gtk_window_set_child(GTK_WINDOW(app->menu_bar_window), build_menu_toolbar(app));
}

/* Re-asserts the menu bar's pinned position and the radar window's
 * below-the-bar geometry. Only does anything when use_separate_menu_bar is
 * set; called once at startup and again after RESET/LOAD, since both can
 * re-maximize the radar window over the bar via apply_factory_layout(). */
static void
refresh_menu_bar_chrome(DemoApp *app)
{
    if (app->use_separate_menu_bar)
        configure_menu_bar_window(app);
}

static gboolean
initial_menu_bar_chrome_cb(gpointer user_data)
{
    refresh_menu_bar_chrome(user_data);
    return G_SOURCE_REMOVE;
}

static GtkWidget *
build_radar_window_content(DemoApp *app)
{
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *drawing = gtk_drawing_area_new();

    gtk_widget_add_css_class(outer, "motif-panel");

    if (!app->use_separate_menu_bar)
        gtk_box_append(GTK_BOX(outer), build_menu_toolbar(app));

    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(drawing), draw_radar, app, NULL);
    gtk_widget_set_hexpand(drawing, TRUE);
    gtk_widget_set_vexpand(drawing, TRUE);

    /* On the gnome-shell build, radar is purely the background surface --
     * its own title/status text moves into the standalone menu bar window
     * instead (see build_menu_toolbar()), so nothing is drawn twice and
     * app->windows[WIN_RADAR].status_label is assigned there instead.
     * gnome-kiosk has no separate bar, so radar keeps its own header and
     * status label here, unchanged from before the menu-bar split. */
    if (!app->use_separate_menu_bar) {
        GtkWidget *status = gtk_label_new("Booting...");
        gtk_label_set_wrap(GTK_LABEL(status), TRUE);
        gtk_widget_set_hexpand(status, TRUE);
        gtk_widget_set_halign(status, GTK_ALIGN_FILL);
        gtk_widget_add_css_class(status, "motif-status-body");
        gtk_box_append(GTK_BOX(outer), status);
        gtk_box_append(GTK_BOX(outer), build_window_header("EXTERNAL INTERFACES"));
        app->windows[WIN_RADAR].status_label = status;
    }

    gtk_widget_add_css_class(drawing, "radar-surface");
    gtk_box_append(GTK_BOX(outer), drawing);

    app->windows[WIN_RADAR].radar_area = drawing;
    return outer;
}

static gboolean
on_radar_tick(gpointer user_data)
{
    DemoApp *app = user_data;
    if (!gtk_widget_get_mapped(app->windows[WIN_RADAR].radar_area))
        return G_SOURCE_CONTINUE;

    app->radar_frame++;
    double previous = app->sweep_angle;
    app->sweep_angle += 0.06;
    if (app->sweep_angle > G_PI * 2.0)
        app->sweep_angle -= G_PI * 2.0;
    if (app->sweep_angle < previous)
        advance_contacts(app);

    gtk_widget_queue_draw(app->windows[WIN_RADAR].radar_area);
    return G_SOURCE_CONTINUE;
}

static gboolean
on_data_tick(gpointer user_data)
{
    DemoApp *app = user_data;
    app->generation++;
    update_text_windows(app);
    gtk_widget_queue_draw(app->windows[WIN_RADAR].radar_area);
    return G_SOURCE_CONTINUE;
}

void
persistable_window_size(DemoWindow *win, int *width, int *height)
{
    /* GTK4 keeps "default-size" in sync with live user/compositor resizes
     * (except while maximized/fullscreened) and its own docs warn that
     * reading the widget allocation instead "can lead to growing or
     * shrinking windows" across save/restore cycles. */
    gtk_window_get_default_size(GTK_WINDOW(win->window), width, height);
    if (*width <= 0 || *height <= 0) {
        *width = win->default_width;
        *height = win->default_height;
    }
}

void
apply_window_size(DemoWindow *win, int width, int height)
{
    gtk_window_set_default_size(GTK_WINDOW(win->window), width, height);
}

static void
apply_factory_layout(DemoApp *app)
{
    for (guint i = 0; i < WIN_COUNT; i++) {
        DemoWindow *win = &app->windows[i];
        gtk_window_unfullscreen(GTK_WINDOW(win->window));
        gtk_window_unmaximize(GTK_WINDOW(win->window));
        gtk_window_unminimize(GTK_WINDOW(win->window));
        win->minimized = FALSE;
        win->visible = TRUE;
        apply_window_size(win, win->default_width, win->default_height);
        if (win->kind == WIN_RADAR)
            gtk_window_maximize(GTK_WINDOW(win->window));
        update_control_button_state(win);
    }
}

static void
show_default_windows(DemoApp *app)
{
    for (guint i = 0; i < WIN_COUNT; i++) {
        DemoWindow *win = &app->windows[i];
        if (win->visible) {
            gtk_widget_set_visible(win->window, TRUE);
            if (win->minimized && win->kind != WIN_RADAR)
                gtk_window_minimize(GTK_WINDOW(win->window));
        } else {
            gtk_widget_set_visible(win->window, FALSE);
        }
        update_control_button_state(win);
    }

    if (app->windows[WIN_RADAR].visible && !app->windows[WIN_RADAR].minimized)
        gtk_window_present(GTK_WINDOW(app->windows[WIN_RADAR].window));

    for (guint i = 0; i < WIN_COUNT; i++) {
        if (i != WIN_RADAR && app->windows[i].visible && !app->windows[i].minimized)
            gtk_window_present(GTK_WINDOW(app->windows[i].window));
    }
}

void
demo_app_activate(GtkApplication *application, gpointer user_data)
{
    DemoApp *app = user_data;
    app->application = application;
    app->config_path = config_path_for_app();

    apply_css(app);
    build_windows(app);
    init_contacts(app);
    if (!apply_saved_layout(app))
        apply_factory_layout(app);
    update_text_windows(app);
    show_default_windows(app);

    if (app->use_separate_menu_bar) {
        gtk_widget_set_visible(app->menu_bar_window, TRUE);
        gtk_window_present(GTK_WINDOW(app->menu_bar_window));

        /* gtk_window_present() queues a map; it does not guarantee Mutter
         * has processed the resulting Wayland surface commit and created
         * the corresponding Meta.Window within this same call stack. A
         * ConfigureKioskChrome call issued synchronously here can (and in
         * testing, does) race ahead of that and silently find no window
         * to pin/resize. Defer to let the map complete first. */
        g_timeout_add(200, initial_menu_bar_chrome_cb, app);
    }

    app->radar_tick_id = g_timeout_add(1000, on_radar_tick, app);
    app->data_tick_id = g_timeout_add_seconds(4, on_data_tick, app);
}

void
demo_app_shutdown(GtkApplication *application, gpointer user_data)
{
    DemoApp *app = user_data;
    if (app->radar_tick_id)
        g_source_remove(app->radar_tick_id);
    if (app->data_tick_id)
        g_source_remove(app->data_tick_id);

    save_layout(app);
    clear_radar_cache(app);
    g_clear_pointer(&app->config_path, g_free);
    g_clear_object(&app->css_provider);
    (void)application;
}
