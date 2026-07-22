#ifndef DEMO_COMMON_H
#define DEMO_COMMON_H

#include <gtk/gtk.h>

#define APP_ID "com.demo.GnomeKioskDemo"
#define CONFIG_DIR_NAME "gnome-kiosk-demo"
#define CONFIG_FILE_NAME "layout.ini"

/* Title of the standalone menu-bar window (must match the extension's
 * MENU_BAR_TITLE) and its fixed height in pixels. Only used when
 * DemoApp.use_separate_menu_bar is set (gnome-shell build). */
#define MENU_BAR_TITLE "M & C GLOBAL MENU"
#define MENU_BAR_HEIGHT 84

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
    const char *button_label;
    int default_width;
    int default_height;
    GtkWidget *window;
    GtkWidget *status_label;
    GtkWidget *text_view;
    GtkTextBuffer *buffer;
    GtkWidget *radar_area;
    GtkWidget *control_button;
    gboolean visible;
    gboolean minimized;
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
    guint radar_frame;
    double sweep_angle;
    cairo_surface_t *radar_static_surface;
    int radar_cache_width;
    int radar_cache_height;
    RadarContact contacts[8];
    DemoWindow windows[WIN_COUNT];

    /* Set by gnome-shell.c's main() before g_application_run(); left FALSE
     * (default zero-init) by gnome-kiosk.c. When TRUE, the "M & C GLOBAL
     * MENU" toolbar is built as its own standalone, undecorated,
     * always-on-top toplevel (menu_bar_window) instead of being embedded
     * at the top of the radar window, since gnome-kiosk has no way to
     * make a window unmovable/always-on-top at all (see CLAUDE.md). */
    gboolean use_separate_menu_bar;
    GtkWidget *menu_bar_window;
} DemoApp;

/*
 * Shared application logic, defined in demo-common.c and identical
 * regardless of which compositor/session the app runs under.
 */
void demo_app_activate(GtkApplication *application, gpointer user_data);
void demo_app_shutdown(GtkApplication *application, gpointer user_data);
void apply_window_size(DemoWindow *win, int width, int height);
void persistable_window_size(DemoWindow *win, int *width, int *height);
void update_control_button_state(DemoWindow *win);

/*
 * The only pieces that differ per backend: exact window position and
 * reliable minimize/maximize require in-process Meta.Window access that
 * plain GTK4/Wayland client code cannot get (see CLAUDE.md "Window
 * minimize" / "Layout persistence behavior"). Implemented once each in
 * gnome-kiosk.c (plain GTK, size/visible/minimized/maximized only) and
 * gnome-shell.c (adds exact x/y and reliable minimize via the "Kiosk
 * Layout Helper" Shell extension). Each source file provides its own
 * main() and links against demo-common.c to produce one of the two
 * binaries; there is no preprocessor branching left in the shared code.
 */
gboolean apply_saved_layout(DemoApp *app);
void save_layout(DemoApp *app);

/* Live per-click minimize/restore toggle (as opposed to bulk LOAD/RESET,
 * handled by apply_saved_layout above). gnome-kiosk.c keeps the plain GTK4
 * gtk_window_minimize()/unminimize() calls, which do not always work (see
 * CLAUDE.md "Window minimize"); gnome-shell.c routes this through the Kiosk
 * Layout Helper extension's real Meta.Window.minimize()/unminimize(),
 * which is reliable regardless of compositor version. */
void backend_set_window_minimized(DemoWindow *win, gboolean minimized);

/* Pins/repositions the standalone menu bar + resizes the radar window to
 * make room for it. Only meaningful (and only called) when
 * app->use_separate_menu_bar is TRUE; gnome-kiosk.c provides a no-op
 * since gnome-kiosk can't make a window unmovable/always-on-top anyway. */
void configure_menu_bar_window(DemoApp *app);

#endif /* DEMO_COMMON_H */
