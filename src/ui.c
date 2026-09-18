/* ui.c – Windows 11 Explorer inspired shell (dark / purple) */

#include "myfm.h"

void myfm_attach_background_menu(AppState *st);

#define ICON_W  22
#define DATE_W 150
#define TYPE_W 120
#define SIZE_W  90


/* ═══════════════════════════════════════════
 * THEME
 * ═══════════════════════════════════════════ */

static const char *MYFM_CSS =
    /* ── window / base ── */
    "window.myfm { background-color: #14111f; color: #e8e3f5; }"
    "window.myfm headerbar {"
    "  background: #14111f; border-bottom: none;"
    "  box-shadow: none; min-height: 42px; padding: 0 6px;"
    "}"
    "window.myfm headerbar windowtitle { color: #cfc7e6; font-size: 12px; }"

    /* ── round icon buttons (nav) ── */
    ".icon-btn {"
    "  background: transparent; color: #d9d2ec;"
    "  border: none; border-radius: 6px;"
    "  min-width: 30px; min-height: 30px; padding: 0 6px; font-size: 14px;"
    "}"
    ".icon-btn:hover    { background: #241f36; color: #ffffff; }"
    ".icon-btn:active   { background: #2e2747; }"
    ".icon-btn:disabled { color: #574f70; background: transparent; }"

    /* ── breadcrumb bar ── */
    ".crumb-bar {"
    "  background: #1d1930; border: 1px solid #2d2747;"
    "  border-radius: 6px; padding: 2px 6px; min-height: 30px;"
    "}"
    ".crumb-bar:hover { border-color: #3b3360; }"
    ".crumb-btn {"
    "  background: transparent; color: #c9c1e0; border: none;"
    "  border-radius: 4px; padding: 2px 8px; font-size: 13px;"
    "  min-height: 22px;"
    "}"
    ".crumb-btn:hover      { background: #2e2747; color: #ffffff; }"
    ".crumb-btn-last       { color: #ffffff; font-weight: bold; }"
    ".crumb-sep            { color: #6d6490; font-size: 13px; }"

    /* ── entries ── */
    ".text-entry, .search-entry {"
    "  background: #1d1930; color: #e8e3f5;"
    "  border: 1px solid #2d2747; border-radius: 6px;"
    "  padding: 4px 10px; font-size: 13px; caret-color: #b18cff;"
    "}"
    ".text-entry:focus-within, .search-entry:focus-within {"
    "  border-color: #b18cff;"
    "}"

    /* ── command bar ── */
    ".commandbar {"
    "  background: #1a1628; border-bottom: 1px solid #262038;"
    "  padding: 5px 10px;"
    "}"
    ".cmd-btn {"
    "  background: transparent; color: #ded8ef; border: none;"
    "  border-radius: 6px; padding: 3px 10px; min-height: 28px;"
    "  font-size: 13px;"
    "}"
    ".cmd-btn:hover    { background: #272038; }"
    ".cmd-btn:active   { background: #322a4c; }"
    ".cmd-btn:disabled { color: #574f70; }"
    ".cmd-accent {"
    "  background: #3a2f63; color: #ffffff; border-radius: 6px;"
    "  padding: 3px 12px; font-weight: bold;"
    "}"
    ".cmd-accent:hover { background: #46397a; }"
    ".cmd-sep { background: #2c2545; min-width: 1px; margin: 4px 6px; }"

    /* ── view toggles ── */
    ".view-btn {"
    "  background: transparent; color: #8c84a8; border: none;"
    "  border-radius: 6px; min-width: 30px; min-height: 28px;"
    "  font-size: 14px;"
    "}"
    ".view-btn:hover     { background: #272038; color: #e8e3f5; }"
    ".view-btn-active    { background: #332b52; color: #c4a5ff; }"

    /* ── sidebar ── */
    ".sidebar { background: #171322; padding: 8px 8px; }"
    ".sidebar-title {"
    "  color: #8a80ab; font-size: 11px; font-weight: bold;"
    "  letter-spacing: 0.08em; padding-left: 10px;"
    "  margin-top: 10px; margin-bottom: 4px;"
    "}"
    ".sidebar-btn {"
    "  background: transparent; color: #cdc5e2; border: none;"
    "  border-radius: 6px; padding: 5px 10px; font-size: 13px;"
    "  min-height: 30px;"
    "}"
    ".sidebar-btn:hover  { background: #221c33; color: #ffffff; }"
    ".sidebar-btn:active { background: #2c2544; }"
    ".sidebar-icon { font-size: 13px; min-width: 18px; }"
    "paned > separator { background: #262038; min-width: 1px; }"

    /* ── content card ── */
    ".content-area {"
    "  background: #1b1729; border-top-left-radius: 10px;"
    "  border-left: 1px solid #262038; border-top: 1px solid #262038;"
    "}"
    ".file-view { background: transparent; padding: 4px 6px 12px; }"

    /* ── column headers ── */
    ".col-header {"
    "  background: transparent; border-bottom: 1px solid #262038;"
    "  padding: 4px 0 6px;"
    "}"
    ".col-btn {"
    "  background: transparent; color: #9b93b8; border: none;"
    "  border-radius: 4px; padding: 2px 6px; font-size: 12px;"
    "}"
    ".col-btn:hover { background: #241f36; color: #e8e3f5; }"

    /* ── rows ── */
    ".file-row {"
    "  border-radius: 6px; padding: 4px 8px; min-height: 28px;"
    "  color: #e8e3f5;"
    "}"
    ".file-row:hover      { background: #221d33; }"
    ".file-row, .grid-tile, .drive-tile, .sidebar-btn, .cmd-btn,"
    ".icon-btn, .crumb-btn, .view-btn, .context-item {"
    "  transition: background-color 130ms ease, border-color 130ms ease;"
    "}"
    ".file-label          { color: #ded8ef; font-size: 13px; }"
    ".file-label-root     { color: #ff8fab; font-size: 13px; }"
    ".file-meta           { color: #8d85a8; font-size: 12px; }"
    ".item-selected       { background: #352c58; }"
    ".item-selected:hover { background: #3d3266; }"

    /* ── grid ── */
    ".grid-flow  { background: transparent; }"
    ".grid-tile  { border-radius: 8px; padding: 8px 6px; }"
    ".grid-tile:hover { background: #221d33; }"
    ".grid-icon  { font-size: 26px; }"
    ".grid-label { color: #ded8ef; font-size: 11px; }"
    ".grid-lock  { font-size: 10px; }"

    /* ── drive tiles (This PC) ── */
    ".drive-tile {"
    "  background: #211c33; border: 1px solid #2c2544;"
    "  border-radius: 10px; padding: 14px 16px;"
    "}"
    ".drive-tile:hover { background: #29223e; border-color: #3b3360; }"
    ".drive-tile-optical { border-color: #6f8bff; }"
    ".drive-icon  { font-size: 30px; }"
    ".drive-name  { color: #f0ecfb; font-size: 14px; font-weight: bold; }"
    ".drive-path  { color: #6f6890; font-size: 11px; }"
    ".drive-space { color: #8d85a8; font-size: 11px; }"
    ".drive-unmounted { color: #5c5578; font-size: 11px; font-style: italic; }"
    "progressbar trough {"
    "  background: #2c2544; border-radius: 4px; min-height: 6px;"
    "}"
    "progressbar progress {"
    "  background: #b18cff; border-radius: 4px; min-height: 6px;"
    "}"
    "progressbar.progress-full progress { background: #ff6b8b; }"

    /* ── misc labels ── */
    ".section-title { color: #f0ecfb; font-size: 15px; font-weight: bold; }"
    ".info-label    { color: #6f6890; font-size: 13px; font-style: italic; }"

    /* ── status bar ── */
    ".statusbar {"
    "  background: #171322; border-top: 1px solid #262038;"
    "  padding: 4px 14px;"
    "}"
    ".status-label { color: #8d85a8; font-size: 12px; }"

    /* ── scrollbars ── */
    "scrollbar { background: transparent; border: none; }"
    "scrollbar slider {"
    "  background: #3a3358; border-radius: 4px;"
    "  min-width: 6px; min-height: 6px;"
    "}"
    "scrollbar slider:hover { background: #4b4374; }"

    /* ── menu buttons (New / Sort / View) ── */
    "menubutton.cmd-btn > button {"
    "  background: transparent; color: #ded8ef; border: none;"
    "  border-radius: 6px; padding: 3px 10px; min-height: 28px;"
    "  font-size: 13px; transition: background-color 120ms ease;"
    "}"
    "menubutton.cmd-btn > button:hover   { background: #272038; }"
    "menubutton.cmd-btn > button:checked { background: #322a4c; }"
    "menubutton.cmd-accent > button {"
    "  background: #3a2f63; color: #ffffff; border: none;"
    "  border-radius: 6px; padding: 3px 12px; min-height: 28px;"
    "  font-weight: bold; transition: background-color 120ms ease;"
    "}"
    "menubutton.cmd-accent > button:hover   { background: #46397a; }"
    "menubutton.cmd-accent > button:checked { background: #4f4088; }"
    "menubutton.cmd-btn > button:disabled,"
    "menubutton.cmd-accent > button:disabled { color: #574f70; }"

    /* ── popovers / menus / dropdown lists ── */
    "popover > contents, popover.menu > contents {"
    "  background: #231d38; border: 1px solid #352d55;"
    "  border-radius: 10px; padding: 4px; color: #e8e3f5;"
    "}"
    "popover > arrow { background: #231d38; border: 1px solid #352d55; }"
    "popover scrolledwindow, popover listview, popover list {"
    "  background: transparent; color: #e8e3f5;"
    "}"
    "popover listview > row, popover list > row, popover modelbutton {"
    "  background: transparent; color: #e8e3f5;"
    "  border-radius: 6px; padding: 5px 9px; min-height: 26px;"
    "  font-size: 13px;"
    "}"
    "popover listview > row:hover, popover list > row:hover,"
    "popover modelbutton:hover { background: #342c54; }"
    "popover listview > row:selected, popover list > row:selected {"
    "  background: #3a2f63; color: #ffffff;"
    "}"
    "popover image, popover check { color: #c4a5ff; }"
    "dropdown > popover > contents { min-width: 190px; }"
    "tooltip {"
    "  background-color: #231d38; color: #e8e3f5;"
    "  border: 1px solid #352d55; border-radius: 8px;"
    "}"
    "tooltip label { color: #e8e3f5; }"
    "entry selection, label selection { background: #6d4ed6; color: #ffffff; }"
    ".context-item {"
    "  background: transparent; color: #e8e3f5; border: none;"
    "  border-radius: 6px; padding: 5px 10px; font-size: 13px;"
    "  min-width: 200px;"
    "}"
    ".context-item:hover  { background: #342c54; }"
    ".ctx-icon  { font-size: 13px; min-width: 20px; }"
    ".ctx-label { font-size: 13px; }"
    "separator { background: #2f2850; min-height: 1px; }"

    /* ── dialogs ── */
    ".dlg-btn {"
    "  background: #2a2340; color: #e8e3f5; border: 1px solid #352d55;"
    "  border-radius: 6px; padding: 6px 16px; font-size: 13px;"
    "}"
    ".dlg-btn:hover { background: #342c54; }"
    ".dlg-btn-accent {"
    "  background: #6d4ed6; color: #ffffff; border: none;"
    "  border-radius: 6px; padding: 6px 16px; font-size: 13px;"
    "  font-weight: bold;"
    "}"
    ".dlg-btn-accent:hover { background: #7e5ff0; }"
    ".dlg-btn-danger {"
    "  background: #c04a63; color: #ffffff; border: none;"
    "  border-radius: 6px; padding: 6px 16px; font-size: 13px;"
    "  font-weight: bold;"
    "}"
    ".dlg-btn-danger:hover { background: #d4566f; }"

    /* ── MyFM dialogs (rename / create / delete / eject) ── */
    "window.myfm-dialog {"
    "  background: #1b1729; border: 1px solid #3a3160; border-radius: 14px;"
    "}"
    ".dlg-icon  { font-size: 30px; }"
    ".dlg-title { color: #f4f1fc; font-size: 16px; font-weight: bold; }"
    ".dlg-body  { color: #9b93b8; font-size: 13px; }"

    /* ── eject button on removable drive tiles ── */
    ".eject-btn {"
    "  background: #2a2340; color: #c4a5ff;"
    "  border: 1px solid #352d55; border-radius: 6px;"
    "  min-width: 30px; min-height: 30px; padding: 0 6px; font-size: 14px;"
    "}"
    ".eject-btn:hover { background: #3a2f63; color: #ffffff; }"

    /* ── properties dialog (GNOME-like) ── */
    "window.props { background: #191526; }"
    "window.props headerbar {"
    "  background: #191526; border-bottom: 1px solid #262038;"
    "  min-height: 44px;"
    "}"
    ".prop-icon     { font-size: 56px; }"
    ".prop-title    { color: #f4f1fc; font-size: 19px; font-weight: bold; }"
    ".prop-subtitle { color: #8d85a8; font-size: 12px; }"
    ".prop-group-title {"
    "  color: #9b93b8; font-size: 12px; font-weight: bold;"
    "  margin: 14px 4px 6px 4px;"
    "}"
    ".prop-card {"
    "  background: #211c33; border: 1px solid #2c2544; border-radius: 12px;"
    "}"
    ".prop-row   { padding: 10px 14px; min-height: 26px; }"
    ".prop-label { color: #ded8ef; font-size: 13px; }"
    ".prop-value { color: #8d85a8; font-size: 13px; }"
    ".prop-mono  { font-family: monospace; font-size: 12px; }"
    "dropdown > button {"
    "  background: #2a2340; color: #e8e3f5; border: 1px solid #352d55;"
    "  border-radius: 6px; padding: 2px 8px; font-size: 12px;"
    "}"
    "switch { background: #352d55; }"
    "switch:checked { background: #6d4ed6; }"

    /* ── animations ── */
    ".anim-fade-start { opacity: 0; }"
    ".anim-fade-in    {"
    "  opacity: 1; transition: opacity 200ms cubic-bezier(.2,0,0,1);"
    "}"
    ".anim-slide-start{ opacity: 0; transform: translateX(-14px); }"
    ".anim-slide-in   {"
    "  opacity: 1; transform: translateX(0px);"
    "  transition: transform 260ms cubic-bezier(.2,0,0,1),"
    "              opacity 200ms ease;"
    "}"
    ".anim-row-start  { opacity: 0; transform: translateY(6px); }"
    ".anim-row-in     {"
    "  opacity: 1; transform: translateY(0px);"
    "  transition: transform 230ms cubic-bezier(.2,0,0,1),"
    "              opacity 180ms ease;"
    "}"
    ".anim-pulse      { transition: transform 160ms cubic-bezier(.2,0,0,1); }"
    ".anim-pulse-big  { transform: scale(1.12); }";

static void
load_css_once(void)
{
    static gboolean loaded = FALSE;
    if (loaded) return;
    loaded = TRUE;

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css, MYFM_CSS);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
}


/* ═══════════════════════════════════════════
 * SMALL CALLBACKS
 * ═══════════════════════════════════════════ */

static void on_view_list(GtkButton *b, gpointer ud)
{ (void)b; set_view_mode((AppState *)ud, VIEW_LIST); }

static void on_view_grid(GtkButton *b, gpointer ud)
{ (void)b; set_view_mode((AppState *)ud, VIEW_GRID); }

static void on_refresh(GtkButton *b, gpointer ud)
{ (void)b; refresh_view((AppState *)ud); }

static void on_home(GtkButton *b, gpointer ud)
{ (void)b; navigate_to((AppState *)ud, g_get_home_dir(), TRUE); }

static void on_cut(GtkButton *b, gpointer ud)
{ (void)b; myfm_cut_selected(ud); }

static void on_copy(GtkButton *b, gpointer ud)
{ (void)b; myfm_copy_selected(ud); }

static void on_paste(GtkButton *b, gpointer ud)
{ (void)b; myfm_paste(ud); }

static void on_rename(GtkButton *b, gpointer ud)
{ (void)b; myfm_rename_selected(ud); }

static void on_trash(GtkButton *b, gpointer ud)
{ (void)b; myfm_trash_selected(ud); }

static void on_props(GtkButton *b, gpointer ud)
{ (void)b; myfm_properties_selected(ud); }

static void on_eject(GtkButton *b, gpointer ud)
{ (void)b; myfm_eject_selected(ud); }

static void on_search_changed(GtkSearchEntry *e, gpointer ud)
{ myfm_set_filter(ud, gtk_editable_get_text(GTK_EDITABLE(e))); }

/* ── menu actions ── */
enum {
    ACT_NEW_FOLDER, ACT_NEW_FILE,
    ACT_SORT_NAME, ACT_SORT_DATE, ACT_SORT_TYPE, ACT_SORT_SIZE,
    ACT_SORT_DIR,
    ACT_VIEW_LIST, ACT_VIEW_GRID, ACT_HIDDEN,
    ACT_TERMINAL, ACT_DELETE
};

static void
on_menu_action(GtkWidget *w, gpointer ud)
{
    AppState *st = ud;
    int act = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "act"));

    switch (act) {
        case ACT_NEW_FOLDER: myfm_new_folder(st);                break;
        case ACT_NEW_FILE:   myfm_new_file(st);                  break;
        case ACT_SORT_NAME:  myfm_set_sort(st, SORT_NAME);       break;
        case ACT_SORT_DATE:  myfm_set_sort(st, SORT_MTIME);      break;
        case ACT_SORT_TYPE:  myfm_set_sort(st, SORT_TYPE);       break;
        case ACT_SORT_SIZE:  myfm_set_sort(st, SORT_SIZE);       break;
        case ACT_SORT_DIR:   myfm_toggle_sort_direction(st);     break;
        case ACT_VIEW_LIST:  set_view_mode(st, VIEW_LIST);       break;
        case ACT_VIEW_GRID:  set_view_mode(st, VIEW_GRID);       break;
        case ACT_HIDDEN:     myfm_toggle_hidden(st);             break;
        case ACT_TERMINAL:   myfm_open_terminal(st);             break;
        case ACT_DELETE:     myfm_delete_selected(st);           break;
        default: break;
    }
}

static void
menu_add(GtkWidget *box, GtkWidget *pop, AppState *st,
         const char *icon, const char *label, int act)
{
    GtkWidget *b = myfm_menu_row(icon, label,
        G_CALLBACK(on_menu_action), st, pop);
    g_object_set_data(G_OBJECT(b), "act", GINT_TO_POINTER(act));
    gtk_box_append(GTK_BOX(box), b);
}

static GtkWidget *
menu_button(const char *label, const char *css)
{
    GtkWidget *mb = gtk_menu_button_new();
    gtk_menu_button_set_label(GTK_MENU_BUTTON(mb), label);
    gtk_widget_add_css_class(mb, css);
    return mb;
}

static GtkWidget *
popover_box(GtkWidget *pop)
{
    GtkWidget *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_popover_set_child(GTK_POPOVER(pop), vb);
    return vb;
}


/* ═══════════════════════════════════════════
 * COLUMN HEADER
 * ═══════════════════════════════════════════ */

static void
on_col_clicked(GtkButton *b, gpointer ud)
{
    AppState *st = ud;
    int col = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "col"));
    myfm_set_sort(st, (SortColumn)col);
}

static GtkWidget *
col_button(AppState *st, const char *label, SortColumn col,
           int width, gboolean expand, gboolean right)
{
    GtkWidget *b = gtk_button_new_with_label(label);
    gtk_widget_add_css_class(b, "col-btn");
    if (width > 0) gtk_widget_set_size_request(b, width, -1);
    gtk_widget_set_hexpand(b, expand);
    gtk_widget_set_halign(b, right ? GTK_ALIGN_END : GTK_ALIGN_FILL);

    GtkWidget *child = gtk_button_get_child(GTK_BUTTON(b));
    if (GTK_IS_LABEL(child))
        gtk_label_set_xalign(GTK_LABEL(child), right ? 1.0f : 0.0f);

    g_object_set_data(G_OBJECT(b), "col", GINT_TO_POINTER((int)col));
    g_signal_connect(b, "clicked", G_CALLBACK(on_col_clicked), st);
    st->col_btn[(int)col] = b;
    return b;
}

/* Shows the sort arrow on the active column, Explorer style. */
void
myfm_update_columns(AppState *st)
{
    static const char *names[4] = { "Name", "Date modified", "Type", "Size" };

    for (int i = 0; i < 4; i++) {
        if (!st->col_btn[i]) continue;

        char *label = (i == (int)st->sort_column)
            ? g_strdup_printf("%s  %s", names[i],
                st->sort_desc ? "\xE2\x96\xBE" : "\xE2\x96\xB4")
            : g_strdup(names[i]);

        gtk_button_set_label(GTK_BUTTON(st->col_btn[i]), label);
        g_free(label);

        GtkWidget *child = gtk_button_get_child(GTK_BUTTON(st->col_btn[i]));
        if (GTK_IS_LABEL(child))
            gtk_label_set_xalign(GTK_LABEL(child), i == 3 ? 1.0f : 0.0f);
    }
}

static GtkWidget *
build_column_header(AppState *st)
{
    GtkWidget *hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(hdr, "col-header");
    gtk_widget_set_margin_start(hdr, 14);
    gtk_widget_set_margin_end(hdr, 14);

    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_size_request(spacer, ICON_W, -1);
    gtk_box_append(GTK_BOX(hdr), spacer);

    gtk_box_append(GTK_BOX(hdr),
        col_button(st, "Name", SORT_NAME, -1, TRUE, FALSE));
    gtk_box_append(GTK_BOX(hdr),
        col_button(st, "Date modified", SORT_MTIME, DATE_W, FALSE, FALSE));
    gtk_box_append(GTK_BOX(hdr),
        col_button(st, "Type", SORT_TYPE, TYPE_W, FALSE, FALSE));
    gtk_box_append(GTK_BOX(hdr),
        col_button(st, "Size", SORT_SIZE, SIZE_W, FALSE, TRUE));

    return hdr;
}


/* ═══════════════════════════════════════════
 * PATH BAR (breadcrumbs <-> editable entry)
 * ═══════════════════════════════════════════ */

static void
path_edit_mode(AppState *st, gboolean edit)
{
    gtk_stack_set_visible_child_name(GTK_STACK(st->path_stack),
        edit ? "entry" : "crumbs");
    if (edit) {
        gtk_widget_grab_focus(st->path_entry);
        gtk_editable_select_region(GTK_EDITABLE(st->path_entry), 0, -1);
    }
}

static void
on_crumbbar_pressed(GtkGestureClick *gc, int n, double x, double y, gpointer ud)
{
    (void)gc; (void)n; (void)x; (void)y;
    path_edit_mode((AppState *)ud, TRUE);
}

static void
on_path_activate(GtkEntry *e, gpointer ud)
{
    AppState *st = ud;
    address_activate(e, st);
    path_edit_mode(st, FALSE);
}

static void
on_path_focus_leave(GtkEventControllerFocus *c, gpointer ud)
{
    (void)c;
    AppState *st = ud;
    if (st->current_path)
        gtk_editable_set_text(GTK_EDITABLE(st->path_entry), st->current_path);
    path_edit_mode(st, FALSE);
}


/* ═══════════════════════════════════════════
 * KEYBOARD SHORTCUTS
 * ═══════════════════════════════════════════ */

static gboolean
on_key_pressed(GtkEventControllerKey *c, guint keyval, guint code,
               GdkModifierType mod, gpointer ud)
{
    (void)c; (void)code;
    AppState *st = ud;
    gboolean ctrl  = (mod & GDK_CONTROL_MASK) != 0;
    gboolean shift = (mod & GDK_SHIFT_MASK)   != 0;
    gboolean alt   = (mod & GDK_ALT_MASK)     != 0;

    if (ctrl) {
        switch (keyval) {
            case GDK_KEY_l: path_edit_mode(st, TRUE);            return TRUE;
            case GDK_KEY_h: myfm_toggle_hidden(st);              return TRUE;
            case GDK_KEY_c: myfm_copy_selected(st);              return TRUE;
            case GDK_KEY_x: myfm_cut_selected(st);               return TRUE;
            case GDK_KEY_v: myfm_paste(st);                      return TRUE;
            case GDK_KEY_n: myfm_window_new(st->app, st->current_path);
                                                                 return TRUE;
            case GDK_KEY_f:
            case GDK_KEY_e: gtk_widget_grab_focus(st->search_entry);
                                                                 return TRUE;
            case GDK_KEY_r: refresh_view(st);                    return TRUE;
            default: break;
        }
    }

    if (alt) {
        switch (keyval) {
            case GDK_KEY_Left:  go_back(NULL, st);    return TRUE;
            case GDK_KEY_Right: go_forward(NULL, st); return TRUE;
            case GDK_KEY_Up:    go_up(NULL, st);      return TRUE;
            default: break;
        }
    }

    switch (keyval) {
        case GDK_KEY_F2:        myfm_rename_selected(st); return TRUE;
        case GDK_KEY_F5:        refresh_view(st);         return TRUE;
        case GDK_KEY_BackSpace: go_up(NULL, st);          return TRUE;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:  myfm_open_selected(st);   return TRUE;
        case GDK_KEY_Delete:
            if (shift) myfm_delete_selected(st);
            else       myfm_trash_selected(st);
            return TRUE;
        case GDK_KEY_Escape:
            path_edit_mode(st, FALSE);
            return TRUE;
        default: break;
    }
    return FALSE;
}


/* ═══════════════════════════════════════════
 * WINDOW
 * ═══════════════════════════════════════════ */

static GtkWidget *
icon_btn(const char *glyph, const char *tooltip,
         GCallback cb, gpointer data)
{
    GtkWidget *b = gtk_button_new_with_label(glyph);
    gtk_widget_add_css_class(b, "icon-btn");
    if (tooltip) gtk_widget_set_tooltip_text(b, tooltip);
    if (cb) g_signal_connect(b, "clicked", cb, data);
    return b;
}

static GtkWidget *
cmd_btn(const char *glyph, const char *tooltip,
        GCallback cb, gpointer data)
{
    GtkWidget *b = gtk_button_new_with_label(glyph);
    gtk_widget_add_css_class(b, "cmd-btn");
    if (tooltip) gtk_widget_set_tooltip_text(b, tooltip);
    if (cb) g_signal_connect(b, "clicked", cb, data);
    return b;
}

static GtkWidget *
vsep(void)
{
    GtkWidget *s = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_widget_add_css_class(s, "cmd-sep");
    return s;
}

GtkWidget *
myfm_window_new(GtkApplication *app, const char *start_path)
{
    load_css_once();

    GtkWidget *window = gtk_application_window_new(app);
    gtk_widget_add_css_class(window, "myfm");
    gtk_window_set_title(GTK_WINDOW(window), "MyFM");
    gtk_window_set_default_size(GTK_WINDOW(window), 1180, 760);

    AppState *st = g_new0(AppState, 1);
    st->app             = app;
    st->window          = window;
    st->history         = g_ptr_array_new_with_free_func(g_free);
    st->history_position = -1;
    st->view_mode       = VIEW_LIST;
    st->sort_column     = SORT_NAME;
    st->place           = PLACE_FOLDER;

    g_object_set_data_full(G_OBJECT(window), "app-state", st,
        (GDestroyNotify)app_state_free);

    /* ── header bar ── */
    GtkWidget *hb = gtk_header_bar_new();
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(hb), TRUE);
    gtk_window_set_titlebar(GTK_WINDOW(window), hb);

    st->btn_back = icon_btn("\xE2\x86\x90", "Back (Alt+Left)",
        G_CALLBACK(go_back), st);
    st->btn_forward = icon_btn("\xE2\x86\x92", "Forward (Alt+Right)",
        G_CALLBACK(go_forward), st);
    st->btn_up = icon_btn("\xE2\x86\x91", "Up (Alt+Up)",
        G_CALLBACK(go_up), st);
    GtkWidget *btn_refresh = icon_btn("\xE2\x9F\xB3", "Refresh (F5)",
        G_CALLBACK(on_refresh), st);

    gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), st->btn_back);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), st->btn_forward);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), st->btn_up);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), btn_refresh);

    /* path bar + search live in the header title area */
    GtkWidget *center = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_hexpand(center, TRUE);

    st->path_stack = gtk_stack_new();
    gtk_widget_set_hexpand(st->path_stack, TRUE);

    GtkWidget *crumb_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(crumb_bar, "crumb-bar");
    st->crumbs = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_hexpand(st->crumbs, TRUE);
    gtk_widget_set_valign(st->crumbs, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(crumb_bar), st->crumbs);

    GtkGesture *cg = gtk_gesture_click_new();
    g_signal_connect(cg, "pressed", G_CALLBACK(on_crumbbar_pressed), st);
    gtk_widget_add_controller(crumb_bar, GTK_EVENT_CONTROLLER(cg));

    st->path_entry = gtk_entry_new();
    gtk_widget_add_css_class(st->path_entry, "text-entry");
    g_signal_connect(st->path_entry, "activate",
        G_CALLBACK(on_path_activate), st);

    GtkEventController *fc = gtk_event_controller_focus_new();
    g_signal_connect(fc, "leave", G_CALLBACK(on_path_focus_leave), st);
    gtk_widget_add_controller(st->path_entry, fc);

    gtk_stack_add_named(GTK_STACK(st->path_stack), crumb_bar, "crumbs");
    gtk_stack_add_named(GTK_STACK(st->path_stack), st->path_entry, "entry");
    gtk_stack_set_visible_child_name(GTK_STACK(st->path_stack), "crumbs");
    gtk_box_append(GTK_BOX(center), st->path_stack);

    st->search_entry = gtk_search_entry_new();
    gtk_widget_add_css_class(st->search_entry, "search-entry");
    gtk_widget_set_size_request(st->search_entry, 220, -1);
    gtk_search_entry_set_placeholder_text(
        GTK_SEARCH_ENTRY(st->search_entry), "Search");
    g_signal_connect(st->search_entry, "search-changed",
        G_CALLBACK(on_search_changed), st);
    gtk_box_append(GTK_BOX(center), st->search_entry);

    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(hb), center);

    /* ── root layout ── */
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(window), root);

    /* ── command bar ── */
    GtkWidget *cmdbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_add_css_class(cmdbar, "commandbar");

    GtkWidget *new_mb = menu_button("\xEF\xBC\x8B  New", "cmd-accent");
    GtkWidget *new_pop = gtk_popover_new();
    GtkWidget *new_box = popover_box(new_pop);
    menu_add(new_box, new_pop, st, "\xF0\x9F\x93\x81", "Folder", ACT_NEW_FOLDER);
    menu_add(new_box, new_pop, st, "\xF0\x9F\x93\x84", "Text document", ACT_NEW_FILE);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(new_mb), new_pop);
    st->cmd_new = new_mb;
    gtk_box_append(GTK_BOX(cmdbar), new_mb);

    gtk_box_append(GTK_BOX(cmdbar), vsep());

    st->cmd_cut = cmd_btn("\xE2\x9C\x82", "Cut (Ctrl+X)",
        G_CALLBACK(on_cut), st);
    gtk_box_append(GTK_BOX(cmdbar), st->cmd_cut);

    st->cmd_copy = cmd_btn("\xF0\x9F\x93\x8B", "Copy (Ctrl+C)",
        G_CALLBACK(on_copy), st);
    gtk_box_append(GTK_BOX(cmdbar), st->cmd_copy);

    st->cmd_paste = cmd_btn("\xF0\x9F\x93\xA5", "Paste (Ctrl+V)",
        G_CALLBACK(on_paste), st);
    gtk_box_append(GTK_BOX(cmdbar), st->cmd_paste);

    st->cmd_rename = cmd_btn("\xE2\x9C\x8F", "Rename (F2)",
        G_CALLBACK(on_rename), st);
    gtk_box_append(GTK_BOX(cmdbar), st->cmd_rename);

    st->cmd_trash = cmd_btn("\xF0\x9F\x97\x91", "Move to the Recycle Bin (Del)",
        G_CALLBACK(on_trash), st);
    gtk_box_append(GTK_BOX(cmdbar), st->cmd_trash);

    st->cmd_props = cmd_btn("\xE2\x84\xB9", "Properties",
        G_CALLBACK(on_props), st);
    gtk_box_append(GTK_BOX(cmdbar), st->cmd_props);

    /* only shown when a removable volume is selected */
    st->cmd_eject = cmd_btn("\xE2\x8F\x8F  Eject", "Eject the selected device",
        G_CALLBACK(on_eject), st);
    gtk_widget_set_visible(st->cmd_eject, FALSE);
    gtk_box_append(GTK_BOX(cmdbar), st->cmd_eject);

    gtk_box_append(GTK_BOX(cmdbar), vsep());

    GtkWidget *sort_mb = menu_button("\xE2\x87\x85  Sort", "cmd-btn");
    GtkWidget *sort_pop = gtk_popover_new();
    GtkWidget *sort_box = popover_box(sort_pop);
    menu_add(sort_box, sort_pop, st, " ", "Name", ACT_SORT_NAME);
    menu_add(sort_box, sort_pop, st, " ", "Date modified", ACT_SORT_DATE);
    menu_add(sort_box, sort_pop, st, " ", "Type", ACT_SORT_TYPE);
    menu_add(sort_box, sort_pop, st, " ", "Size", ACT_SORT_SIZE);
    gtk_box_append(GTK_BOX(sort_box), myfm_menu_separator());
    menu_add(sort_box, sort_pop, st, "\xE2\x87\x85",
        "Ascending / Descending", ACT_SORT_DIR);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(sort_mb), sort_pop);
    gtk_box_append(GTK_BOX(cmdbar), sort_mb);

    GtkWidget *view_mb = menu_button("\xE2\x96\xA4  View", "cmd-btn");
    GtkWidget *view_pop = gtk_popover_new();
    GtkWidget *view_box = popover_box(view_pop);
    menu_add(view_box, view_pop, st, "\xE2\x98\xB0", "Details", ACT_VIEW_LIST);
    menu_add(view_box, view_pop, st, "\xE2\x8A\x9E", "Large icons", ACT_VIEW_GRID);
    gtk_box_append(GTK_BOX(view_box), myfm_menu_separator());
    menu_add(view_box, view_pop, st, "\xF0\x9F\x91\x81",
        "Show hidden items (Ctrl+H)", ACT_HIDDEN);
    menu_add(view_box, view_pop, st, "\xE2\x8C\xA8",
        "Open in terminal", ACT_TERMINAL);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(view_mb), view_pop);
    gtk_box_append(GTK_BOX(cmdbar), view_mb);

    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(cmdbar), spacer);

    st->view_toggle_list = gtk_button_new_with_label("\xE2\x98\xB0");
    gtk_widget_add_css_class(st->view_toggle_list, "view-btn");
    gtk_widget_add_css_class(st->view_toggle_list, "view-btn-active");
    gtk_widget_set_tooltip_text(st->view_toggle_list, "Details view");
    g_signal_connect(st->view_toggle_list, "clicked",
        G_CALLBACK(on_view_list), st);
    gtk_box_append(GTK_BOX(cmdbar), st->view_toggle_list);

    st->view_toggle_grid = gtk_button_new_with_label("\xE2\x8A\x9E");
    gtk_widget_add_css_class(st->view_toggle_grid, "view-btn");
    gtk_widget_set_tooltip_text(st->view_toggle_grid, "Icons view");
    g_signal_connect(st->view_toggle_grid, "clicked",
        G_CALLBACK(on_view_grid), st);
    gtk_box_append(GTK_BOX(cmdbar), st->view_toggle_grid);

    gtk_box_append(GTK_BOX(root), cmdbar);

    /* ── body: sidebar | content ── */
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_position(GTK_PANED(paned), 225);
    gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_resize_start_child(GTK_PANED(paned), FALSE);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_box_append(GTK_BOX(root), paned);

    /* sidebar */
    GtkWidget *side_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(side_scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_add_css_class(sidebar, "sidebar");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(side_scroll), sidebar);
    gtk_paned_set_start_child(GTK_PANED(paned), side_scroll);

    GtkWidget *qa = gtk_label_new("Quick access");
    gtk_widget_add_css_class(qa, "sidebar-title");
    gtk_widget_set_halign(qa, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(sidebar), qa);

    struct { const char *icon; const char *label; const char *sub; } shortcuts[] = {
        { "\xF0\x9F\x8F\xA0", "Home",      NULL        },
        { "\xF0\x9F\x96\xA5", "Desktop",   "Desktop"   },
        { "\xF0\x9F\x93\x84", "Documents", "Documents" },
        { "\xE2\xAC\x87",     "Downloads", "Downloads" },
        { "\xF0\x9F\x96\xBC", "Pictures",  "Pictures"  },
        { "\xF0\x9F\x8E\xB5", "Music",     "Music"     },
        { "\xF0\x9F\x8E\xAC", "Videos",    "Videos"    },
    };

    for (guint i = 0; i < G_N_ELEMENTS(shortcuts); i++) {
        char *path = shortcuts[i].sub
            ? g_build_filename(g_get_home_dir(), shortcuts[i].sub, NULL)
            : g_strdup(g_get_home_dir());
        if (shortcuts[i].sub && !g_file_test(path, G_FILE_TEST_IS_DIR)) {
            g_free(path);
            continue;
        }
        gtk_box_append(GTK_BOX(sidebar), create_directory_button(
            shortcuts[i].icon, shortcuts[i].label, path, st));
        g_free(path);
    }

    GtkWidget *sysl = gtk_label_new("System");
    gtk_widget_add_css_class(sysl, "sidebar-title");
    gtk_widget_set_halign(sysl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(sidebar), sysl);

    GtkWidget *this_pc = create_directory_button(
        "\xF0\x9F\x92\xBB", "This PC", NULL, st);
    /* replace the default handler with the This PC one */
    g_signal_handlers_disconnect_by_func(this_pc,
        G_CALLBACK(directory_button_clicked), st);
    g_signal_connect(this_pc, "clicked",
        G_CALLBACK(this_pc_button_clicked), st);
    gtk_box_append(GTK_BOX(sidebar), this_pc);

    GtkWidget *root_btn = create_directory_button(
        "\xF0\x9F\x92\xBD", "Root filesystem", "/", st);
    gtk_box_append(GTK_BOX(sidebar), root_btn);

    GtkWidget *trash_btn = create_directory_button(
        "\xF0\x9F\x97\x91", "Recycle Bin", NULL, st);
    g_signal_handlers_disconnect_by_func(trash_btn,
        G_CALLBACK(directory_button_clicked), st);
    g_signal_connect(trash_btn, "clicked",
        G_CALLBACK(trash_button_clicked), st);
    gtk_box_append(GTK_BOX(sidebar), trash_btn);

    GtkWidget *home_btn = create_directory_button(
        "\xE2\x9A\x99", "Preferences", NULL, st);
    g_signal_handlers_disconnect_by_func(home_btn,
        G_CALLBACK(directory_button_clicked), st);
    g_signal_connect(home_btn, "clicked", G_CALLBACK(on_home), st);
    gtk_widget_set_visible(home_btn, FALSE);   /* reserved for later */
    gtk_box_append(GTK_BOX(sidebar), home_btn);

    /* content */
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(content, "content-area");
    gtk_paned_set_end_child(GTK_PANED(paned), content);

    st->column_header = build_column_header(st);
    gtk_box_append(GTK_BOX(content), st->column_header);

    st->scroller = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(st->scroller, TRUE);
    gtk_widget_set_hexpand(st->scroller, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(st->scroller),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_append(GTK_BOX(content), st->scroller);

    st->file_view = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(st->file_view, "file-view");
    gtk_widget_set_hexpand(st->file_view, TRUE);
    gtk_widget_set_vexpand(st->file_view, TRUE);
    gtk_widget_set_valign(st->file_view, GTK_ALIGN_FILL);
    gtk_scrolled_window_set_child(
        GTK_SCROLLED_WINDOW(st->scroller), st->file_view);

    myfm_attach_background_menu(st);

    /* ── status bar ── */
    GtkWidget *status = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(status, "statusbar");

    st->status_left = gtk_label_new("");
    gtk_widget_add_css_class(st->status_left, "status-label");
    gtk_widget_set_halign(st->status_left, GTK_ALIGN_START);
    gtk_widget_set_hexpand(st->status_left, TRUE);
    gtk_box_append(GTK_BOX(status), st->status_left);

    st->status_right = gtk_label_new("");
    gtk_widget_add_css_class(st->status_right, "status-label");
    gtk_widget_set_halign(st->status_right, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(status), st->status_right);

    gtk_box_append(GTK_BOX(root), status);

    /* ── shortcuts ── */
    GtkEventController *kc = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(kc, GTK_PHASE_BUBBLE);
    g_signal_connect(kc, "key-pressed", G_CALLBACK(on_key_pressed), st);
    gtk_widget_add_controller(window, kc);

    /* ── go ── */
    myfm_update_actions(st);

    const char *dest = start_path && g_file_test(start_path, G_FILE_TEST_IS_DIR)
        ? start_path : g_get_home_dir();
    navigate_to(st, dest, TRUE);

    gtk_window_present(GTK_WINDOW(window));
    return window;
}
