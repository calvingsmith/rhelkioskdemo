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
            g_message("layout helper unavailable: %s", error->message);
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
        g_message("ConfigureKioskChrome failed: %s", error->message);
    else
        g_message("ConfigureKioskChrome applied (barHeight=%d)", MENU_BAR_HEIGHT);
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

/* Live per-click minimize/restore toggle, routed through the extension's
 * real Meta.Window.minimize()/unminimize() instead of GTK4's client-side
 * request (which RHEL10's mutter 49.4 silently ignores -- see CLAUDE.md
 * "Window minimize"). This was the actual point of the gnome-shell
 * migration for minimize reliability; apply_saved_layout()'s SetStates call
 * only ever covered bulk LOAD/RESET, not this live toggle. */
void
backend_set_window_minimized(DemoWindow *win, gboolean minimized)
{
    if (win->kind == WIN_RADAR)
        return;

    GDBusProxy *helper = get_layout_helper_proxy();
    if (helper == NULL) {
        g_message("backend_set_window_minimized: %s -> %d: no helper proxy", win->id, minimized);
        return;
    }

    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) result = g_dbus_proxy_call_sync(
        helper, "SetWindowMinimized", g_variant_new("(sb)", win->title, minimized),
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    if (result == NULL)
        g_message("SetWindowMinimized failed for %s: %s", win->id, error->message);
    else
        g_message("SetWindowMinimized: %s -> %d ok", win->id, minimized);
}

/* Shared by apply_saved_layout() (user's layout.ini) and
 * apply_factory_layout() (admin-provisioned layout-factory.ini) -- same
 * file format, same fields including position, just a different path.
 * RESET and LOAD are the same operation with a different data source. */
static gboolean
apply_layout_from_path(DemoApp *app, const char *path)
{
    g_autoptr(GKeyFile) keyfile = g_key_file_new();
    if (!g_key_file_load_from_file(keyfile, path, G_KEY_FILE_NONE, NULL))
        return FALSE;

    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a(siiiibb)"));
    gboolean have_positions = FALSE;

    for (guint i = 0; i < WIN_COUNT; i++) {
        DemoWindow *win = &app->windows[i];
        win->visible = TRUE;
        win->minimized = FALSE;

        /* Read once, used for both the GTK-side apply and the D-Bus
         * position/size request below -- previously the D-Bus block
         * re-queried gtk_window_get_default_size() instead of reusing
         * these, which silently sent a STALE size (whatever the window's
         * size already was) instead of the actual target whenever the two
         * differed (e.g. after a manual resize). GTK4's own docs only
         * promise default-size is "kept in sync with live resizes" -- not
         * that it reflects a just-issued set_default_size() synchronously,
         * before the compositor round-trip completes. Confirmed live
         * 2026-08-18: this exact mismatch is why move_resize_frame() had
         * zero effect specifically on windows the user had just manually
         * resized (GROUND OPS/SYSTEM STATUS), while windows still at their
         * unchanged default size appeared to work fine by coincidence. */
        int width = win->default_width, height = win->default_height;
        gboolean have_size = g_key_file_has_key(keyfile, win->id, "width", NULL);
        if (have_size) {
            width = g_key_file_get_integer(keyfile, win->id, "width", NULL);
            height = g_key_file_get_integer(keyfile, win->id, "height", NULL);
        }

        /* Radar's geometry/type are entirely owned by ConfigureKioskChrome
         * (move_resize_frame() + Meta.WindowType.DESKTOP), run moments
         * later via the retry-deferred callback. Plain GTK4
         * apply_window_size()/maximize() requests below are redundant for
         * it and were observed live (2026-08-18) to cause a visible
         * transient wrong-size glitch before ConfigureKioskChrome's
         * authoritative fix-up landed -- skip them for radar entirely. */
        if (win->kind != WIN_RADAR && have_size)
            apply_window_size(win, width, height);

        if (g_key_file_has_key(keyfile, win->id, "visible", NULL))
            win->visible = g_key_file_get_boolean(keyfile, win->id, "visible", NULL);
        gboolean maximized = FALSE;
        if (g_key_file_has_key(keyfile, win->id, "maximized", NULL)) {
            maximized = g_key_file_get_boolean(keyfile, win->id, "maximized", NULL);
            if (win->kind != WIN_RADAR) {
                if (maximized)
                    gtk_window_maximize(GTK_WINDOW(win->window));
                else
                    gtk_window_unmaximize(GTK_WINDOW(win->window));
            }
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
            g_message("apply_layout_from_path: queueing %s title='%s' x=%d y=%d w=%d h=%d minimized=%d maximized=%d",
                win->id, win->title, x, y, width, height, win->minimized, maximized);
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
                g_message("SetStates failed: %s", error->message);
            else
                g_message("SetStates: call returned ok");
        } else {
            g_variant_builder_clear(&builder);
        }
    } else {
        g_variant_builder_clear(&builder);
    }

    return TRUE;
}

gboolean
apply_saved_layout(DemoApp *app)
{
    return apply_layout_from_path(app, app->config_path);
}

gboolean
apply_factory_layout(DemoApp *app)
{
    if (apply_layout_from_path(app, app->factory_config_path))
        return TRUE;
    apply_minimal_factory_layout(app);
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
            g_message("GetStates failed: %s", error->message);
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
