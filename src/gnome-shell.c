/*
 * gnome-shell build entry point. Adds exact window position and reliable
 * minimize/maximize on top of the plain GTK4 persistence, backed by the
 * "Kiosk Layout Helper" GNOME Shell extension
 * (gnome-shell-extension/kiosk-layout-helper@gnomekiosk.demo), since those
 * require in-process Meta.Window access that plain GTK4/Wayland client
 * code cannot get (see CLAUDE.md "Window minimize" /
 * "Layout persistence behavior").
 */

#include "demo-common.h"
#include <gio/gio.h>

static GDBusProxy *
get_layout_helper_proxy(void)
{
    static GDBusProxy *proxy = NULL;
    static gboolean tried = FALSE;

    if (!tried) {
        tried = TRUE;
        g_autoptr(GError) error = NULL;
        proxy = g_dbus_proxy_new_for_bus_sync(
            G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_NONE, NULL,
            "org.gnome.Shell", "/org/gnomekiosk/LayoutHelper",
            "org.gnomekiosk.LayoutHelper", NULL, &error);
        if (proxy == NULL)
            g_debug("layout helper unavailable: %s", error->message);
    }
    return proxy;
}

/* Pins the standalone menu bar window as an unmovable always-on-top DOCK
 * (Meta.WindowType.DOCK + make_above + stick), and resizes the radar
 * window to fill the screen minus the bar's height, so the two don't
 * overlap. Both are matched by title inside the extension's
 * ConfigureKioskChrome — see gnome-shell-extension/. */
void
configure_menu_bar_window(DemoApp *app)
{
    if (!app->use_separate_menu_bar)
        return;

    GDBusProxy *helper = get_layout_helper_proxy();
    if (helper == NULL)
        return;

    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) result = g_dbus_proxy_call_sync(
        helper, "ConfigureKioskChrome", g_variant_new("(i)", MENU_BAR_HEIGHT),
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    if (result == NULL)
        g_debug("ConfigureKioskChrome failed: %s", error->message);
}

/* Startup diagnostic: log clearly, once, whether the "Kiosk Layout Helper"
 * extension is actually installed/enabled/reachable, rather than only
 * discovering this indirectly the first time SAVE/LOAD quietly no-ops. */
static void
check_layout_helper_available(void)
{
    GDBusProxy *proxy = get_layout_helper_proxy();
    if (proxy == NULL) {
        g_warning("Kiosk Layout Helper extension not reachable on the session bus "
                  "(org.gnome.Shell / /org/gnomekiosk/LayoutHelper) — "
                  "exact window position/state will not be available this run.");
        return;
    }

    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) result = g_dbus_proxy_call_sync(
        proxy, "Ping", NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    if (result == NULL) {
        g_warning("Kiosk Layout Helper extension did not respond to Ping: %s — "
                  "is it installed and enabled? (gnome-extensions list --enabled)",
                  error->message);
        return;
    }

    const char *version = NULL;
    g_variant_get(result, "(&s)", &version);
    g_message("Kiosk Layout Helper extension found: %s", version);
}

gboolean
apply_saved_layout(DemoApp *app)
{
    g_autoptr(GKeyFile) keyfile = g_key_file_new();
    if (!g_key_file_load_from_file(keyfile, app->config_path, G_KEY_FILE_NONE, NULL))
        return FALSE;

    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a(siiiibb)"));
    gboolean have_positions = FALSE;

    for (guint i = 0; i < WIN_COUNT; i++) {
        DemoWindow *win = &app->windows[i];
        win->visible = TRUE;
        win->minimized = FALSE;
        if (g_key_file_has_key(keyfile, win->id, "width", NULL)) {
            int width = g_key_file_get_integer(keyfile, win->id, "width", NULL);
            int height = g_key_file_get_integer(keyfile, win->id, "height", NULL);
            apply_window_size(win, width, height);
        }
        if (g_key_file_has_key(keyfile, win->id, "visible", NULL))
            win->visible = g_key_file_get_boolean(keyfile, win->id, "visible", NULL);
        gboolean maximized = FALSE;
        if (g_key_file_has_key(keyfile, win->id, "maximized", NULL)) {
            maximized = g_key_file_get_boolean(keyfile, win->id, "maximized", NULL);
            if (maximized)
                gtk_window_maximize(GTK_WINDOW(win->window));
            else
                gtk_window_unmaximize(GTK_WINDOW(win->window));
        }
        if (g_key_file_has_key(keyfile, win->id, "minimized", NULL))
            win->minimized = g_key_file_get_boolean(keyfile, win->id, "minimized", NULL);

        /* Exact position/reliable minimize: only the layout-helper Shell
         * extension can honor these (see get_layout_helper_proxy()); the
         * GTK-only calls above are the fallback when it's not running. */
        if (win->kind != WIN_RADAR &&
            g_key_file_has_key(keyfile, win->id, "x", NULL) &&
            g_key_file_has_key(keyfile, win->id, "y", NULL)) {
            int x = g_key_file_get_integer(keyfile, win->id, "x", NULL);
            int y = g_key_file_get_integer(keyfile, win->id, "y", NULL);
            int width = 0, height = 0;
            gtk_window_get_default_size(GTK_WINDOW(win->window), &width, &height);
            g_variant_builder_add(&builder, "(siiiibb)",
                win->title, x, y, width, height, win->minimized, maximized);
            have_positions = TRUE;
        }

        update_control_button_state(win);
    }

    if (have_positions) {
        GDBusProxy *helper = get_layout_helper_proxy();
        if (helper != NULL) {
            g_autoptr(GError) error = NULL;
            g_autoptr(GVariant) result = g_dbus_proxy_call_sync(
                helper, "SetStates", g_variant_new("(a(siiiibb))", &builder),
                G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
            if (result == NULL)
                g_debug("SetStates failed: %s", error->message);
        } else {
            g_variant_builder_clear(&builder);
        }
    } else {
        g_variant_builder_clear(&builder);
    }

    return TRUE;
}

void
save_layout(DemoApp *app)
{
    g_autoptr(GKeyFile) keyfile = g_key_file_new();

    g_autoptr(GVariant) states = NULL;
    GDBusProxy *helper = get_layout_helper_proxy();
    if (helper != NULL) {
        g_autoptr(GError) error = NULL;
        g_autoptr(GVariant) result = g_dbus_proxy_call_sync(
            helper, "GetStates", NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
        if (result != NULL)
            states = g_variant_get_child_value(result, 0);
        else
            g_debug("GetStates failed: %s", error->message);
    }

    for (guint i = 0; i < WIN_COUNT; i++) {
        DemoWindow *win = &app->windows[i];
        int width = 0;
        int height = 0;
        persistable_window_size(win, &width, &height);
        g_key_file_set_integer(keyfile, win->id, "width", width);
        g_key_file_set_integer(keyfile, win->id, "height", height);
        g_key_file_set_boolean(keyfile, win->id, "visible", win->visible);
        g_key_file_set_boolean(keyfile, win->id, "maximized", gtk_window_is_maximized(GTK_WINDOW(win->window)));
        g_key_file_set_boolean(keyfile, win->id, "minimized", win->minimized);

        if (states != NULL && win->kind != WIN_RADAR) {
            GVariantIter iter;
            const char *title;
            gint32 x, y, w, h;
            gboolean min, max;
            g_variant_iter_init(&iter, states);
            while (g_variant_iter_next(&iter, "(siiiibb)", &title, &x, &y, &w, &h, &min, &max)) {
                if (g_strcmp0(title, win->title) == 0) {
                    g_key_file_set_integer(keyfile, win->id, "x", x);
                    g_key_file_set_integer(keyfile, win->id, "y", y);
                    break;
                }
            }
        }
    }

    gsize len = 0;
    g_autofree gchar *data = g_key_file_to_data(keyfile, &len, NULL);
    g_file_set_contents(app->config_path, data, len, NULL);
}

int
main(int argc, char **argv)
{
    check_layout_helper_available();

    g_autoptr(GtkApplication) application = gtk_application_new(APP_ID, G_APPLICATION_DEFAULT_FLAGS);
    DemoApp app = {0};
    app.use_separate_menu_bar = TRUE;

    g_signal_connect(application, "activate", G_CALLBACK(demo_app_activate), &app);
    g_signal_connect(application, "shutdown", G_CALLBACK(demo_app_shutdown), &app);

    return g_application_run(G_APPLICATION(application), argc, argv);
}
