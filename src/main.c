#include <gtk/gtk.h>
#include <math.h>
#include <string.h>

#define APP_ID "com.demo.GnomeKioskDemo"
#define CONFIG_DIR_NAME "gnome-kiosk-demo"
#define CONFIG_FILE_NAME "layout.ini"

typedef enum {
    WIN_RADAR = 0,
    WIN_TRAFFIC,
    WIN_CLEARANCE,
    WIN_GROUND,
    WIN_STATUS,
    WIN_COUNT
} DemoWindowKind;

typedef struct {
    DemoWindowKind kind;
    const char *id;
    const char *title;
    const char *role;
    int default_width;
    int default_height;
    GtkWidget *window;
    GtkWidget *status_label;
    GtkWidget *text_view;
    GtkTextBuffer *buffer;
    GtkWidget *radar_area;
    gboolean visible;
} DemoWindow;

typedef struct {
    gchar callsign[16];
    double x;
    double y;
    double heading;
    double speed;
} RadarContact;

typedef struct {
    GtkApplication *application;
    GtkCssProvider *css_provider;
    gchar *config_path;
    guint radar_tick_id;
    guint data_tick_id;
    guint generation;
    double sweep_angle;
    RadarContact contacts[12];
    DemoWindow windows[WIN_COUNT];
} DemoApp;

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
    int default_width;
    int default_height;
} WINDOW_SPECS[] = {
    { WIN_RADAR, "radar", "EXTERNAL INTERFACES", "radar", 1100, 760 },
    { WIN_TRAFFIC, "traffic", "TRAFFIC OVERVIEW", "radar", 420, 260 },
    { WIN_CLEARANCE, "clearance", "CLEARANCE QUEUE", "tower", 420, 260 },
    { WIN_GROUND, "ground", "GROUND OPS", "dispatch", 420, 260 },
    { WIN_STATUS, "status", "SYSTEM STATUS", "control", 760, 170 },
};

static GtkWidget *build_radar_window_content(DemoApp *app);
static void show_default_windows(DemoApp *app);
static void reset_default_window_layout(DemoApp *app);
static gchar *generate_callsign(void);

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
        gtk_widget_set_visible(win->window, TRUE);
        gtk_window_present(GTK_WINDOW(win->window));
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

    if (g_strcmp0(target, "traffic") == 0)
        show_window_by_kind(app, WIN_TRAFFIC);
    else if (g_strcmp0(target, "clearance") == 0)
        show_window_by_kind(app, WIN_CLEARANCE);
    else if (g_strcmp0(target, "ground") == 0)
        show_window_by_kind(app, WIN_GROUND);
    else if (g_strcmp0(target, "status") == 0)
        show_window_by_kind(app, WIN_STATUS);
    else if (g_strcmp0(target, "radar") == 0)
        show_window_by_kind(app, WIN_RADAR);
    else if (g_strcmp0(target, "all") == 0) {
        for (guint i = 0; i < WIN_COUNT; i++)
            show_window(&app->windows[i]);
    } else if (g_strcmp0(target, "reset") == 0) {
        app->generation = 0;
        reset_default_window_layout(app);
        show_default_windows(app);
        update_text_windows(app);
    }
}

static GtkWidget *
build_panel_header(const char *title)
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
build_text_window_content(const char *title, GtkTextBuffer **out_buffer)
{
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *header = build_panel_header(title);
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
    gtk_box_append(GTK_BOX(outer), header);
    gtk_box_append(GTK_BOX(outer), scroller);

    *out_buffer = buffer;
    return outer;
}

static GtkWidget *
build_status_window_content(GtkWidget **out_label)
{
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *header = build_panel_header("SYSTEM STATUS");
    GtkWidget *label = gtk_label_new("Ready");

    gtk_widget_add_css_class(outer, "motif-panel");
    gtk_widget_add_css_class(label, "motif-status-body");
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(outer), header);
    gtk_box_append(GTK_BOX(outer), label);

    *out_label = label;
    return outer;
}

static gboolean
on_close_request(GtkWindow *window, gpointer user_data)
{
    DemoWindow *win = user_data;
    gtk_widget_set_visible(GTK_WIDGET(window), FALSE);
    win->visible = FALSE;
    return TRUE;
}

static void
draw_radar(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data)
{
    DemoApp *app = user_data;
    (void)area;

    double cx = width / 2.0;
    double cy = height / 2.0;
    double radius = MIN(width, height) / 2.0 - 40.0;

    cairo_set_source_rgb(cr, 0.62, 0.62, 0.62);
    cairo_paint(cr);

    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, 0.28, 0.28, 0.28, 0.75);
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

    cairo_set_source_rgba(cr, 0.12, 0.36, 0.2, 0.18);
    cairo_arc(cr, cx, cy, radius * 0.82, app->sweep_angle - 0.16, app->sweep_angle);
    cairo_line_to(cr, cx, cy);
    cairo_close_path(cr);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, 0.14, 0.28, 0.14, 0.92);
    for (guint i = 0; i < G_N_ELEMENTS(app->contacts); i++) {
        RadarContact *c = &app->contacts[i];
        double px = cx + c->x * radius;
        double py = cy + c->y * radius;
        char label[64];

        cairo_arc(cr, px, py, 2.8, 0, G_PI * 2.0);
        cairo_fill(cr);

        g_snprintf(label, sizeof(label), "%s H%03u %uKT",
            c->callsign,
            (guint)c->heading,
            (guint)c->speed);
        cairo_set_font_size(cr, 11.0);
        cairo_move_to(cr, px + 6.0, py - 6.0);
        cairo_show_text(cr, label);
        cairo_set_source_rgba(cr, 0.14, 0.28, 0.14, 0.92);
    }

    cairo_set_source_rgba(cr, 0.12, 0.12, 0.12, 0.9);
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 13.0);
    cairo_move_to(cr, 28, 32);
    cairo_show_text(cr, "EXTERNAL INTERFACES");
    cairo_move_to(cr, 28, 52);
    cairo_show_text(cr, "RADAR IMAGE / FLIGHT CONTROL SURFACE");

}

static void
apply_css(DemoApp *app)
{
    app->css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(
        app->css_provider,
        ".motif-panel { background: #b4b4b4; border: 1px solid #eeeeee; border-right-color: #5f5f5f; border-bottom-color: #5f5f5f; border-radius: 0; padding: 4px; }"
        ".motif-panel-header { background: #85aeb0; border: 1px solid #b9d3d4; border-right-color: #4e6e70; border-bottom-color: #4e6e70; border-radius: 0; padding: 2px 6px; min-height: 22px; }"
        ".motif-panel-title { color: #f2f2f2; font-weight: 700; }"
        ".motif-toolbar { background: #a6a6a6; padding: 1px; border: 1px solid #dddddd; border-right-color: #666666; border-bottom-color: #666666; border-radius: 0; }"
        ".motif-button { background: #c2c2c2; color: #111; border: 1px solid #dddddd; border-top-color: #f8f8f8; border-left-color: #f8f8f8; border-right-color: #565656; border-bottom-color: #565656; border-radius: 0; min-height: 22px; padding: 2px 6px; }"
        ".motif-button:hover { background: #c8c8c8; }"
        ".motif-text { background: #d8d8d8; color: #111; padding: 6px; font-family: monospace; font-size: 10pt; }"
        ".motif-status-body { background: #d8d8d8; color: #111; padding: 6px; }"
        ".radar-surface { background: #9d9d9d; }");

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
    gtk_window_set_decorated(GTK_WINDOW(win->window), TRUE);
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
        win->default_width = WINDOW_SPECS[i].default_width;
        win->default_height = WINDOW_SPECS[i].default_height;
        win->visible = TRUE;

        create_window(app, win, win->title, win->default_width, win->default_height);
    }

    gtk_window_set_child(GTK_WINDOW(app->windows[WIN_RADAR].window), build_radar_window_content(app));
    gtk_window_set_child(GTK_WINDOW(app->windows[WIN_TRAFFIC].window), build_text_window_content("TRAFFIC OVERVIEW", &app->windows[WIN_TRAFFIC].buffer));
    gtk_window_set_child(GTK_WINDOW(app->windows[WIN_CLEARANCE].window), build_text_window_content("CLEARANCE QUEUE", &app->windows[WIN_CLEARANCE].buffer));
    gtk_window_set_child(GTK_WINDOW(app->windows[WIN_GROUND].window), build_text_window_content("GROUND OPS", &app->windows[WIN_GROUND].buffer));
    gtk_window_set_child(GTK_WINDOW(app->windows[WIN_STATUS].window), build_status_window_content(&app->windows[WIN_STATUS].status_label));

    gtk_window_set_resizable(GTK_WINDOW(app->windows[WIN_TRAFFIC].window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(app->windows[WIN_CLEARANCE].window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(app->windows[WIN_GROUND].window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(app->windows[WIN_STATUS].window), FALSE);

}

static GtkWidget *
build_radar_window_content(DemoApp *app)
{
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *header = build_panel_header("M & C GLOBAL MENU");
    GtkWidget *button_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *status = gtk_label_new("Booting...");
    GtkWidget *radar_header = build_panel_header("EXTERNAL INTERFACES");
    GtkWidget *drawing = gtk_drawing_area_new();
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
        { "RESET", "reset" },
    };

    gtk_widget_add_css_class(outer, "motif-panel");
    gtk_widget_add_css_class(button_row, "motif-toolbar");
    gtk_label_set_wrap(GTK_LABEL(status), TRUE);
    gtk_widget_set_hexpand(status, TRUE);
    gtk_widget_set_halign(status, GTK_ALIGN_FILL);
    gtk_widget_add_css_class(status, "motif-status-body");

    for (guint i = 0; i < G_N_ELEMENTS(buttons); i++) {
        GtkWidget *btn = gtk_button_new_with_label(buttons[i].label);
        gtk_widget_add_css_class(btn, "motif-button");
        g_object_set_data_full(G_OBJECT(btn), "target", g_strdup(buttons[i].target), g_free);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_menu_button_clicked), app);
        gtk_box_append(GTK_BOX(button_row), btn);
    }

    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(drawing), draw_radar, app, NULL);
    gtk_widget_set_hexpand(drawing, TRUE);
    gtk_widget_set_vexpand(drawing, TRUE);

    gtk_box_append(GTK_BOX(outer), header);
    gtk_box_append(GTK_BOX(outer), button_row);
    gtk_box_append(GTK_BOX(outer), status);
    gtk_box_append(GTK_BOX(outer), radar_header);
    gtk_widget_add_css_class(drawing, "radar-surface");
    gtk_box_append(GTK_BOX(outer), drawing);

    app->windows[WIN_RADAR].radar_area = drawing;
    app->windows[WIN_RADAR].status_label = status;
    return outer;
}

static void
apply_saved_layout(DemoApp *app)
{
    g_autoptr(GKeyFile) keyfile = g_key_file_new();
    if (!g_key_file_load_from_file(keyfile, app->config_path, G_KEY_FILE_NONE, NULL))
        return;

    for (guint i = 0; i < WIN_COUNT; i++) {
        DemoWindow *win = &app->windows[i];
        win->visible = TRUE;
        if (g_key_file_has_key(keyfile, win->id, "width", NULL)) {
            int width = g_key_file_get_integer(keyfile, win->id, "width", NULL);
            int height = g_key_file_get_integer(keyfile, win->id, "height", NULL);
            gtk_window_set_default_size(GTK_WINDOW(win->window), width, height);
        }
    }
}

static void
save_layout(DemoApp *app)
{
    g_autoptr(GKeyFile) keyfile = g_key_file_new();

    for (guint i = 0; i < WIN_COUNT; i++) {
        DemoWindow *win = &app->windows[i];
        int width = 0;
        int height = 0;
        gtk_window_get_default_size(GTK_WINDOW(win->window), &width, &height);
        g_key_file_set_integer(keyfile, win->id, "width", width);
        g_key_file_set_integer(keyfile, win->id, "height", height);
        g_key_file_set_boolean(keyfile, win->id, "visible", win->visible);
    }

    gsize len = 0;
    g_autofree gchar *data = g_key_file_to_data(keyfile, &len, NULL);
    g_file_set_contents(app->config_path, data, len, NULL);
}

static gboolean
on_radar_tick(gpointer user_data)
{
    DemoApp *app = user_data;
    double previous = app->sweep_angle;
    app->sweep_angle += 0.03;
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

static void
reset_default_window_layout(DemoApp *app)
{
    for (guint i = 0; i < WIN_COUNT; i++) {
        DemoWindow *win = &app->windows[i];
        GtkWidget *child = gtk_window_get_child(GTK_WINDOW(win->window));
        gtk_window_unfullscreen(GTK_WINDOW(win->window));
        if (win->kind != WIN_RADAR)
            gtk_window_unmaximize(GTK_WINDOW(win->window));
        gtk_window_set_default_size(GTK_WINDOW(win->window), win->default_width, win->default_height);
        if (child != NULL)
            gtk_widget_set_size_request(child, win->default_width, win->default_height);
        if (win->kind == WIN_RADAR)
            gtk_window_maximize(GTK_WINDOW(win->window));
    }
}

static void
show_default_windows(DemoApp *app)
{
    for (guint i = 0; i < WIN_COUNT; i++) {
        app->windows[i].visible = TRUE;
        gtk_widget_set_visible(app->windows[i].window, TRUE);
    }

    gtk_window_present(GTK_WINDOW(app->windows[WIN_RADAR].window));
    for (guint i = 0; i < WIN_COUNT; i++) {
        if (i != WIN_RADAR)
            gtk_window_present(GTK_WINDOW(app->windows[i].window));
    }
}

static void
demo_app_activate(GtkApplication *application, gpointer user_data)
{
    DemoApp *app = user_data;
    app->application = application;
    app->config_path = config_path_for_app();

    apply_css(app);
    build_windows(app);
    init_contacts(app);
    apply_saved_layout(app);
    reset_default_window_layout(app);
    update_text_windows(app);
    show_default_windows(app);

    app->radar_tick_id = g_timeout_add(33, on_radar_tick, app);
    app->data_tick_id = g_timeout_add_seconds(4, on_data_tick, app);
}

static void
demo_app_shutdown(GtkApplication *application, gpointer user_data)
{
    DemoApp *app = user_data;
    if (app->radar_tick_id)
        g_source_remove(app->radar_tick_id);
    if (app->data_tick_id)
        g_source_remove(app->data_tick_id);

    save_layout(app);
    g_clear_pointer(&app->config_path, g_free);
    g_clear_object(&app->css_provider);
    (void)application;
}

int
main(int argc, char **argv)
{
    g_autoptr(GtkApplication) application = gtk_application_new(APP_ID, G_APPLICATION_DEFAULT_FLAGS);
    DemoApp app = {0};

    g_signal_connect(application, "activate", G_CALLBACK(demo_app_activate), &app);
    g_signal_connect(application, "shutdown", G_CALLBACK(demo_app_shutdown), &app);

    return g_application_run(G_APPLICATION(application), argc, argv);
}
