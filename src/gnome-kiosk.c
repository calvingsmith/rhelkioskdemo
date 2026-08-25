/*
 * gnome-kiosk build entry point. Plain GTK4 window-state persistence only:
 * size, visible, minimized, maximized. No exact position, and minimize is
 * not honored by RHEL10's gnome-kiosk/mutter 49.4 (see CLAUDE.md "Window
 * minimize"). This file intentionally has no D-Bus/Shell-extension code.
 */

#include "demo-common.h"

/* Shared by apply_saved_layout() (user's layout.ini) and
 * apply_factory_layout() (admin-provisioned layout-factory.ini) -- same
 * file format, same fields, just a different path. gnome-kiosk has no
 * position control at all (see CLAUDE.md "Layout persistence behavior"),
 * so x/y keys in either file are simply not read here. */
static gboolean
apply_layout_from_path(DemoApp *app, const char *path)
{
    g_autoptr(GKeyFile) keyfile = g_key_file_new();
    if (!g_key_file_load_from_file(keyfile, path, G_KEY_FILE_NONE, NULL))
        return FALSE;

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
        if (g_key_file_has_key(keyfile, win->id, "maximized", NULL)) {
            gboolean maximized = g_key_file_get_boolean(keyfile, win->id, "maximized", NULL);
            if (maximized)
                gtk_window_maximize(GTK_WINDOW(win->window));
            else
                gtk_window_unmaximize(GTK_WINDOW(win->window));
        }
        if (g_key_file_has_key(keyfile, win->id, "minimized", NULL))
            win->minimized = g_key_file_get_boolean(keyfile, win->id, "minimized", NULL);

        update_control_button_state(win);
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
    }

    gsize len = 0;
    g_autofree gchar *data = g_key_file_to_data(keyfile, &len, NULL);
    g_file_set_contents(app->config_path, data, len, NULL);
}

/* gnome-kiosk never sets app->use_separate_menu_bar, so demo-common.c
 * never actually calls this; provided only to satisfy the shared
 * interface declared in demo-common.h (gnome-kiosk has no way to make a
 * window unmovable/always-on-top at all, see CLAUDE.md). */
void
configure_menu_bar_window(DemoApp *app)
{
    (void) app;
}

/* Plain GTK4 requests, unchanged from before the backend split. Known to be
 * silently ignored by RHEL10's mutter 49.4 (see CLAUDE.md "Window
 * minimize"); the gnome-shell build's implementation in gnome-shell.c uses
 * real Meta.Window access instead to work around that. */
void
backend_set_window_minimized(DemoWindow *win, gboolean minimized)
{
    if (minimized)
        gtk_window_minimize(GTK_WINDOW(win->window));
    else
        gtk_window_unminimize(GTK_WINDOW(win->window));
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
