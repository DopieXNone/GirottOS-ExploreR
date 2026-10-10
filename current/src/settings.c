/* settings.c – persistent preferences and the Settings ("Folder Options")
 * window.
 *
 * Stored as a plain INI file, settings.ini, in the program's own folder
 * (next to the ExploreR executable), so the configuration travels with
 * the program.  When that folder is not writable (e.g. installed in
 * /usr/bin) it falls back to $XDG_CONFIG_HOME/explorer/settings.ini.
 * GKeyFile only: no GSettings schema and no dconf, so it behaves the same
 * on GNOME, KDE, Sway, Hyprland or anything else.
 */

#include "myfm.h"

#define DEFAULT_WIDTH   1000
#define DEFAULT_HEIGHT   640
#define MIN_WIDTH        480
#define MIN_HEIGHT       320
#define MAX_SIDE        8192

static MyfmSettings settings;
static gboolean     loaded = FALSE;

static const char *theme_ids[] = { "system", "light", "dark", "purple", NULL };
static const char *icon_ids[]  = { "tela", "emoji", NULL };

static char *
xdg_config_path(void)
{
    return g_build_filename(g_get_user_config_dir(), "explorer", "settings.ini", NULL);
}

/* <folder of the executable>/settings.ini if we may write there,
 * otherwise the per-user XDG location.  Decided once per run. */
static const char *
config_path(void)
{
    static char *path = NULL;
    if (path) return path;

    char *exe = g_file_read_link("/proc/self/exe", NULL);
    if (exe) {
        char *dir = g_path_get_dirname(exe);
        char *candidate = g_build_filename(dir, "settings.ini", NULL);
        gboolean usable = g_file_test(candidate, G_FILE_TEST_EXISTS)
            ? access(candidate, W_OK) == 0
            : access(dir, W_OK) == 0;
        if (usable) path = candidate;
        else        g_free(candidate);
        g_free(dir);
        g_free(exe);
    }
    if (!path) path = xdg_config_path();
    return path;
}

const char *
myfm_settings_path(void)
{
    return config_path();
}

static int
id_index(const char * const *ids, const char *value, int fallback)
{
    if (!value) return fallback;
    for (int i = 0; ids[i]; i++)
        if (!g_ascii_strcasecmp(ids[i], value)) return i;
    return fallback;
}

static gboolean
kf_bool(GKeyFile *kf, const char *group, const char *key, gboolean def)
{
    GError *err = NULL;
    gboolean v = g_key_file_get_boolean(kf, group, key, &err);
    if (err) { g_error_free(err); return def; }
    return v;
}

static int
kf_int(GKeyFile *kf, const char *group, const char *key, int def, int lo, int hi)
{
    GError *err = NULL;
    int v = g_key_file_get_integer(kf, group, key, &err);
    if (err) { g_error_free(err); return def; }
    return CLAMP(v, lo, hi);
}

static void
settings_load(void)
{
    loaded = TRUE;

    settings.theme           = THEME_SYSTEM;
    settings.icon_style      = ICONS_TELA;
    settings.animations      = FALSE;        /* calm by default */
    settings.win_width       = DEFAULT_WIDTH;
    settings.win_height      = DEFAULT_HEIGHT;
    settings.remember_size   = TRUE;
    settings.win_maximized   = FALSE;
    settings.show_hidden     = FALSE;
    settings.show_checkboxes = FALSE;
    settings.preview_visible = FALSE;
    settings.view_mode       = VIEW_LIST;

    const char *path = config_path();
    GKeyFile *kf = g_key_file_new();
    gboolean have = g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL);
    if (!have) {
        /* first run with the file next to the program: take over the
         * preferences saved earlier in ~/.config, if any */
        char *old = xdg_config_path();
        if (strcmp(old, path) != 0)
            have = g_key_file_load_from_file(kf, old, G_KEY_FILE_NONE, NULL);
        g_free(old);
    }
    if (have) {
        char *v = g_key_file_get_string(kf, "Appearance", "theme", NULL);
        settings.theme = (MyfmTheme)id_index(theme_ids, v, THEME_SYSTEM);
        g_free(v);
        v = g_key_file_get_string(kf, "Appearance", "icons", NULL);
        /* "system" was the old name of the icon-theme option */
        settings.icon_style = (v && !g_ascii_strcasecmp(v, "emoji"))
            ? ICONS_EMOJI : ICONS_TELA;
        g_free(v);
        settings.animations = kf_bool(kf, "Appearance", "animations", settings.animations);

        settings.win_width  = kf_int(kf, "Window", "width",  DEFAULT_WIDTH,  MIN_WIDTH,  MAX_SIDE);
        settings.win_height = kf_int(kf, "Window", "height", DEFAULT_HEIGHT, MIN_HEIGHT, MAX_SIDE);
        settings.remember_size = kf_bool(kf, "Window", "remember-size", settings.remember_size);
        settings.win_maximized = kf_bool(kf, "Window", "maximized", FALSE);

        settings.show_hidden     = kf_bool(kf, "View", "show-hidden", FALSE);
        settings.show_checkboxes = kf_bool(kf, "View", "checkboxes", FALSE);
        settings.preview_visible = kf_bool(kf, "View", "preview-pane", FALSE);
        v = g_key_file_get_string(kf, "View", "layout", NULL);
        settings.view_mode = (v && !g_ascii_strcasecmp(v, "icons")) ? VIEW_GRID : VIEW_LIST;
        g_free(v);
    }
    g_key_file_unref(kf);

    /* make sure the file exists, so it can be found and edited by hand */
    if (!g_file_test(path, G_FILE_TEST_EXISTS))
        myfm_settings_save();
}

MyfmSettings *
myfm_settings(void)
{
    if (!loaded) settings_load();
    return &settings;
}

void
myfm_settings_save(void)
{
    MyfmSettings *s = myfm_settings();
    GKeyFile *kf = g_key_file_new();

    g_key_file_set_string (kf, "Appearance", "theme", theme_ids[s->theme]);
    g_key_file_set_string (kf, "Appearance", "icons", icon_ids[s->icon_style]);
    g_key_file_set_boolean(kf, "Appearance", "animations", s->animations);

    g_key_file_set_integer(kf, "Window", "width",  s->win_width);
    g_key_file_set_integer(kf, "Window", "height", s->win_height);
    g_key_file_set_boolean(kf, "Window", "remember-size", s->remember_size);
    g_key_file_set_boolean(kf, "Window", "maximized", s->win_maximized);

    g_key_file_set_boolean(kf, "View", "show-hidden",  s->show_hidden);
    g_key_file_set_boolean(kf, "View", "checkboxes",   s->show_checkboxes);
    g_key_file_set_boolean(kf, "View", "preview-pane", s->preview_visible);
    g_key_file_set_string (kf, "View", "layout",
        s->view_mode == VIEW_GRID ? "icons" : "details");

    const char *path = config_path();
    char *dir  = g_path_get_dirname(path);
    GError *err = NULL;
    if (g_mkdir_with_parents(dir, 0700) != 0 ||
        !g_key_file_save_to_file(kf, path, &err)) {
        g_warning("Could not save settings to %s: %s", path,
            err ? err->message : g_strerror(errno));
    }
    g_clear_error(&err);
    g_free(dir);
    g_key_file_unref(kf);
}

static void settings_ui_sync(void);

static void
apply_all_full(gboolean icons_changed)
{
    myfm_gui_apply_theme();

    GApplication *app = g_application_get_default();
    if (!app || !GTK_IS_APPLICATION(app)) return;

    for (GList *l = gtk_application_get_windows(GTK_APPLICATION(app)); l; l = l->next) {
        AppState *st = g_object_get_data(G_OBJECT(l->data), "app-state");
        if (st) myfm_gui_sync_settings(st, icons_changed);
    }
    settings_ui_sync();
}

void
myfm_settings_apply_all(void)
{
    apply_all_full(FALSE);
}


/* ═══════════════════════════════════════════
 * SETTINGS WINDOW
 * ═══════════════════════════════════════════ */

typedef struct {
    GtkWidget *window;
    GtkWidget *parent;          /* weak: window the dialog was opened from */
    GtkWidget *spin_w;
    GtkWidget *spin_h;
    GtkWidget *dd_layout;
    GtkWidget *sw_hidden;
    GtkWidget *sw_checks;
    GtkWidget *sw_preview;
    gboolean   building;
} SettingsUI;

static GtkWidget *settings_win = NULL;   /* only one at a time */

/* Ctrl+H, the View menu, the status-bar toggles… change the same
 * preferences: keep an open Settings window showing the real values. */
static void
settings_ui_sync(void)
{
    if (!settings_win) return;
    SettingsUI *ui = g_object_get_data(G_OBJECT(settings_win), "settings-ui");
    if (!ui) return;
    MyfmSettings *s = myfm_settings();
    ui->building = TRUE;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(ui->dd_layout),
        s->view_mode == VIEW_GRID ? 1 : 0);
    gtk_switch_set_active(GTK_SWITCH(ui->sw_hidden),  s->show_hidden);
    gtk_switch_set_active(GTK_SWITCH(ui->sw_checks),  s->show_checkboxes);
    gtk_switch_set_active(GTK_SWITCH(ui->sw_preview), s->preview_visible);
    ui->building = FALSE;
}

static void
settings_ui_free(gpointer p)
{
    SettingsUI *ui = p;
    if (ui->parent)
        g_object_remove_weak_pointer(G_OBJECT(ui->parent), (gpointer *)&ui->parent);
    g_free(ui);
}

static void
on_theme_changed(GObject *dd, GParamSpec *ps, gpointer ud)
{
    (void)ps;
    SettingsUI *ui = ud;
    if (ui->building) return;
    myfm_settings()->theme = (MyfmTheme)gtk_drop_down_get_selected(GTK_DROP_DOWN(dd));
    myfm_settings_save();
    myfm_gui_apply_theme();
}

static void
on_icons_changed(GObject *dd, GParamSpec *ps, gpointer ud)
{
    (void)ps;
    SettingsUI *ui = ud;
    if (ui->building) return;
    myfm_settings()->icon_style =
        (MyfmIconStyle)gtk_drop_down_get_selected(GTK_DROP_DOWN(dd));
    myfm_settings_save();
    apply_all_full(TRUE);
}

static void
on_layout_changed(GObject *dd, GParamSpec *ps, gpointer ud)
{
    (void)ps;
    SettingsUI *ui = ud;
    if (ui->building) return;
    /* applies to every open window now, not only to new ones */
    myfm_settings()->view_mode =
        gtk_drop_down_get_selected(GTK_DROP_DOWN(dd)) == 1 ? VIEW_GRID : VIEW_LIST;
    myfm_settings_save();
    myfm_settings_apply_all();
}

static void
on_bool_switch(GObject *sw, GParamSpec *ps, gpointer ud)
{
    (void)ps;
    SettingsUI *ui = ud;
    if (ui->building) return;
    gboolean *target = g_object_get_data(sw, "target");
    *target = gtk_switch_get_active(GTK_SWITCH(sw));
    myfm_settings_save();
    myfm_settings_apply_all();
}

static void
on_size_changed(GtkSpinButton *spin, gpointer ud)
{
    SettingsUI *ui = ud;
    if (ui->building) return;
    MyfmSettings *s = myfm_settings();
    if (GTK_WIDGET(spin) == ui->spin_w)
        s->win_width = gtk_spin_button_get_value_as_int(spin);
    else
        s->win_height = gtk_spin_button_get_value_as_int(spin);
    s->win_maximized = FALSE;
    myfm_settings_save();
}

static void
on_apply_size(GtkButton *b, gpointer ud)
{
    (void)b;
    SettingsUI *ui = ud;
    /* make sure a value typed but not yet committed is used */
    gtk_spin_button_update(GTK_SPIN_BUTTON(ui->spin_w));
    gtk_spin_button_update(GTK_SPIN_BUTTON(ui->spin_h));
    if (!ui->parent) return;
    AppState *st = g_object_get_data(G_OBJECT(ui->parent), "app-state");
    if (st) myfm_gui_apply_window_size(st);
}

static void
on_use_current_size(GtkButton *b, gpointer ud)
{
    (void)b;
    SettingsUI *ui = ud;
    if (!ui->parent) return;
    int w = 0, h = 0;
    gtk_window_get_default_size(GTK_WINDOW(ui->parent), &w, &h);
    if (w <= 0 || h <= 0) {
        w = gtk_widget_get_width(ui->parent);
        h = gtk_widget_get_height(ui->parent);
    }
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui->spin_w), w);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui->spin_h), h);
}

static GtkWidget *
make_dropdown(const char * const *labels, guint selected)
{
    GtkWidget *dd = gtk_drop_down_new_from_strings(labels);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dd), selected);
    gtk_widget_set_size_request(dd, 170, -1);
    return dd;
}

static GtkWidget *
make_switch(SettingsUI *ui, gboolean *target)
{
    GtkWidget *sw = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(sw), *target);
    g_object_set_data(G_OBJECT(sw), "target", target);
    g_signal_connect(sw, "notify::active", G_CALLBACK(on_bool_switch), ui);
    return sw;
}

static GtkWidget *
make_spin(SettingsUI *ui, int value, int lo, int hi)
{
    GtkWidget *sp = gtk_spin_button_new_with_range(lo, hi, 10);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(sp), value);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(sp), TRUE);
    gtk_widget_set_size_request(sp, 110, -1);
    g_signal_connect(sp, "value-changed", G_CALLBACK(on_size_changed), ui);
    return sp;
}

static gboolean
on_settings_key(GtkEventControllerKey *c, guint keyval, guint code,
                GdkModifierType mod, gpointer ud)
{
    (void)c; (void)code; (void)mod;
    if (keyval == GDK_KEY_Escape) {
        gtk_window_close(GTK_WINDOW(ud));
        return TRUE;
    }
    return FALSE;
}

void
myfm_open_settings(GtkWidget *parent)
{
    if (settings_win) {
        gtk_window_present(GTK_WINDOW(settings_win));
        return;
    }

    MyfmSettings *s = myfm_settings();
    SettingsUI *ui = g_new0(SettingsUI, 1);
    ui->building = TRUE;

    GtkWidget *win = gtk_window_new();
    ui->window = win;
    settings_win = win;
    g_object_add_weak_pointer(G_OBJECT(win), (gpointer *)&settings_win);
    g_object_set_data_full(G_OBJECT(win), "settings-ui", ui, settings_ui_free);

    gtk_widget_add_css_class(win, "props");
    gtk_window_set_title(GTK_WINDOW(win), "Settings");
    gtk_window_set_default_size(GTK_WINDOW(win), 400, -1);
    gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
    /* a window of its own (not attached to the file-manager window) */
    myfm_window_titlebar(win, "Settings");
    myfm_window_set_owner(win, NULL);
    if (parent) {
        GtkRoot *root = gtk_widget_get_root(parent);
        if (GTK_IS_WINDOW(root)) {
            ui->parent = GTK_WIDGET(root);
            g_object_add_weak_pointer(G_OBJECT(ui->parent), (gpointer *)&ui->parent);
        }
    }

    GtkEventController *kc = gtk_event_controller_key_new();
    g_signal_connect(kc, "key-pressed", G_CALLBACK(on_settings_key), win);
    gtk_widget_add_controller(win, kc);

    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start(page, 12);
    gtk_widget_set_margin_end(page, 12);
    gtk_widget_set_margin_top(page, 4);
    gtk_widget_set_margin_bottom(page, 12);
    gtk_window_set_child(GTK_WINDOW(win), page);

    /* ── Appearance ── */
    gtk_box_append(GTK_BOX(page), myfm_group_title("Appearance"));
    GtkWidget *card = myfm_card_new();

    static const char *theme_labels[] = {
        "Follow system", "Light", "Dark", "Purple (classic)", NULL
    };
    GtkWidget *dd_theme = make_dropdown(theme_labels, s->theme);
    g_signal_connect(dd_theme, "notify::selected", G_CALLBACK(on_theme_changed), ui);
    myfm_card_add_widget(card, "Theme", dd_theme);

    static const char *icon_labels[] = { "Tela", "Emoji", NULL };
    GtkWidget *dd_icons = make_dropdown(icon_labels, s->icon_style);
    if (!myfm_tela_available())
        gtk_widget_set_tooltip_text(dd_icons,
            "The Tela icon theme is not installed: emoji are shown instead.");
    g_signal_connect(dd_icons, "notify::selected", G_CALLBACK(on_icons_changed), ui);
    myfm_card_add_widget(card, "Icons", dd_icons);

    myfm_card_add_widget(card, "Animations", make_switch(ui, &s->animations));
    gtk_box_append(GTK_BOX(page), card);

    /* ── Window ── */
    gtk_box_append(GTK_BOX(page), myfm_group_title("Window size"));
    card = myfm_card_new();

    ui->spin_w = make_spin(ui, s->win_width,  MIN_WIDTH,  MAX_SIDE);
    ui->spin_h = make_spin(ui, s->win_height, MIN_HEIGHT, MAX_SIDE);
    myfm_card_add_widget(card, "Width (px)",  ui->spin_w);
    myfm_card_add_widget(card, "Height (px)", ui->spin_h);
    myfm_card_add_widget(card, "Remember size on close",
        make_switch(ui, &s->remember_size));

    GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *b_cur = gtk_button_new_with_label("Use current size");
    gtk_widget_add_css_class(b_cur, "dlg-btn");
    g_signal_connect(b_cur, "clicked", G_CALLBACK(on_use_current_size), ui);
    gtk_box_append(GTK_BOX(btns), b_cur);
    GtkWidget *b_apply = gtk_button_new_with_label("Apply to window");
    gtk_widget_add_css_class(b_apply, "dlg-btn-accent");
    g_signal_connect(b_apply, "clicked", G_CALLBACK(on_apply_size), ui);
    gtk_box_append(GTK_BOX(btns), b_apply);
    gtk_widget_set_sensitive(b_cur, ui->parent != NULL);
    gtk_widget_set_sensitive(b_apply, ui->parent != NULL);
    myfm_card_add_widget(card, "", btns);
    gtk_box_append(GTK_BOX(page), card);

    /* ── View ── */
    gtk_box_append(GTK_BOX(page), myfm_group_title("View"));
    card = myfm_card_new();

    static const char *layout_labels[] = { "Details", "Large icons", NULL };
    ui->dd_layout = make_dropdown(layout_labels, s->view_mode == VIEW_GRID ? 1 : 0);
    g_signal_connect(ui->dd_layout, "notify::selected", G_CALLBACK(on_layout_changed), ui);
    myfm_card_add_widget(card, "Layout", ui->dd_layout);

    ui->sw_hidden  = make_switch(ui, &s->show_hidden);
    ui->sw_checks  = make_switch(ui, &s->show_checkboxes);
    ui->sw_preview = make_switch(ui, &s->preview_visible);
    myfm_card_add_widget(card, "Show hidden items", ui->sw_hidden);
    myfm_card_add_widget(card, "Item check boxes",  ui->sw_checks);
    myfm_card_add_widget(card, "Preview pane",      ui->sw_preview);
    gtk_box_append(GTK_BOX(page), card);

    GtkWidget *note = gtk_label_new(NULL);
    char *esc = g_markup_printf_escaped("Saved in %s", config_path());
    gtk_label_set_markup(GTK_LABEL(note), esc);
    g_free(esc);
    gtk_widget_add_css_class(note, "prop-subtitle");
    gtk_label_set_wrap(GTK_LABEL(note), TRUE);
    gtk_label_set_xalign(GTK_LABEL(note), 0.0f);
    gtk_widget_set_margin_top(note, 8);
    gtk_box_append(GTK_BOX(page), note);

    ui->building = FALSE;
    gtk_window_present(GTK_WINDOW(win));
}
