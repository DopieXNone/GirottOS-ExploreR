/* main.c – ExploreR (MyFM) entry point and file-manager logic: folder
 * listing, file operations, This PC, Recycle Bin, navigation, selection.
 * The window itself and everything visual lives in gui.c, preferences in
 * settings.c.
 */

#include "myfm.h"

#include <glib/gstdio.h>
#include <stdio.h>

/* Column widths of the "details" (list) view; the header in gui.c uses
 * the same values */
#define ICON_W  18
#define CHECK_W 18
#define DATE_W 130
#define TYPE_W 120
#define SIZE_W  80

/* Rows past this index appear instantly: keeps huge folders fluid. */
#define MAX_ANIMATED_ROWS 24

static void show_directory(AppState *st, const char *path);
static void show_trash(AppState *st);
static void update_crumbs(AppState *st, const char *path);
static void update_crumbs_special(AppState *st, const char *label);
static void update_nav_buttons(AppState *st);
static void update_status(AppState *st);
static void clear_selection(AppState *st);
static void rename_dialog(AppState *st, const char *path);

void
myfm_sel_item_free(gpointer p)
{
    SelItem *s = p;
    g_free(s->path);
    g_free(s);
}

/* Explorer-style "selection checkboxes" (Settings toggle): forward decl
 * so the low-level selection helpers below can keep an optional per-row
 * checkbox in sync without the row-building code (later in the file)
 * needing to know about selection internals. */
static void on_row_checkbox_toggled(GtkCheckButton *cb, gpointer ud);

static void
sync_row_checkbox(GtkWidget *w, gboolean active)
{
    if (!GTK_IS_WIDGET(w)) return;
    GtkWidget *cb = g_object_get_data(G_OBJECT(w), "sel-checkbox");
    if (!cb) return;
    g_signal_handlers_block_by_func(cb, on_row_checkbox_toggled, w);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(cb), active);
    g_signal_handlers_unblock_by_func(cb, on_row_checkbox_toggled, w);
}


/* ═══════════════════════════════════════════
 * SMALL UTILITIES
 * ═══════════════════════════════════════════ */

static void
clear_file_view(GtkWidget *fv)
{
    GtkWidget *c;
    while ((c = gtk_widget_get_first_child(fv)) != NULL)
        gtk_box_remove(GTK_BOX(fv), c);
}

static void
show_error(AppState *st, const char *title, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *body = g_strdup_vprintf(fmt, ap);
    va_end(ap);

    myfm_message(st ? st->window : NULL, "\xE2\x9A\xA0", title, body);
    g_free(body);
}

static char *
fmt_size(guint64 sz)
{
    if (sz >= 1024ULL * 1024 * 1024)
        return g_strdup_printf("%.1f GB", (double)sz / (1024.0 * 1024 * 1024));
    if (sz >= 1024ULL * 1024)
        return g_strdup_printf("%.1f MB", (double)sz / (1024.0 * 1024));
    if (sz >= 1024ULL)
        return g_strdup_printf("%.0f KB", (double)sz / 1024.0);
    return g_strdup_printf("%" G_GUINT64_FORMAT " B", sz);
}

static char *
fmt_time(gint64 unix_time)
{
    if (unix_time <= 0) return g_strdup("");
    GDateTime *dt = g_date_time_new_from_unix_local(unix_time);
    if (!dt) return g_strdup("");
    char *s = g_date_time_format(dt, "%d/%m/%Y  %H:%M");
    g_date_time_unref(dt);
    return s;
}

static gboolean
needs_root(const char *path)
{
    if (access(path, R_OK) == 0) return FALSE;
    struct stat st;
    if (stat(path, &st) != 0) return FALSE;
    uid_t uid = getuid();
    if (uid == 0) return FALSE;
    return (st.st_uid != uid && (st.st_mode & S_IROTH) == 0);
}

/* TRUE when `child` is `parent` itself or lives inside it. */
gboolean
myfm_path_is_inside(const char *child, const char *parent)
{
    char *c = g_canonicalize_filename(child, NULL);
    char *p = g_canonicalize_filename(parent, NULL);

    gboolean r = FALSE;
    size_t pl = strlen(p);

    if (strcmp(c, p) == 0) {
        r = TRUE;
    } else if (strncmp(c, p, pl) == 0) {
        /* "/a/b" must not match "/a/bc" */
        r = (p[pl - 1] == '/') || (c[pl] == '/');
    }

    g_free(c);
    g_free(p);
    return r;
}

const char *
file_icon_for(const char *name, gboolean is_dir)
{
    if (is_dir) return "\xF0\x9F\x93\x81";              /* 📁 */
    if (!name)  return "\xF0\x9F\x93\x84";              /* 📄 */

    const char *e = g_strrstr(name, ".");
    if (!e) return "\xF0\x9F\x93\x84";

    if (!g_ascii_strcasecmp(e,".png") ||!g_ascii_strcasecmp(e,".jpg") ||
        !g_ascii_strcasecmp(e,".jpeg")||!g_ascii_strcasecmp(e,".gif") ||
        !g_ascii_strcasecmp(e,".webp")||!g_ascii_strcasecmp(e,".bmp") ||
        !g_ascii_strcasecmp(e,".svg") ||!g_ascii_strcasecmp(e,".tiff"))
        return "\xF0\x9F\x96\xBC";                      /* 🖼 */
    if (!g_ascii_strcasecmp(e,".mp3") ||!g_ascii_strcasecmp(e,".flac")||
        !g_ascii_strcasecmp(e,".ogg") ||!g_ascii_strcasecmp(e,".wav") ||
        !g_ascii_strcasecmp(e,".m4a") ||!g_ascii_strcasecmp(e,".aac") ||
        !g_ascii_strcasecmp(e,".opus"))
        return "\xF0\x9F\x8E\xB5";                      /* 🎵 */
    if (!g_ascii_strcasecmp(e,".mp4") ||!g_ascii_strcasecmp(e,".mkv") ||
        !g_ascii_strcasecmp(e,".avi") ||!g_ascii_strcasecmp(e,".mov") ||
        !g_ascii_strcasecmp(e,".webm")||!g_ascii_strcasecmp(e,".flv"))
        return "\xF0\x9F\x8E\xAC";                      /* 🎬 */
    if (!g_ascii_strcasecmp(e,".pdf")) return "\xF0\x9F\x93\x95";
    if (!g_ascii_strcasecmp(e,".zip") ||!g_ascii_strcasecmp(e,".tar") ||
        !g_ascii_strcasecmp(e,".gz")  ||!g_ascii_strcasecmp(e,".xz")  ||
        !g_ascii_strcasecmp(e,".7z")  ||!g_ascii_strcasecmp(e,".rar") ||
        !g_ascii_strcasecmp(e,".bz2") ||!g_ascii_strcasecmp(e,".zst"))
        return "\xF0\x9F\x93\xA6";                      /* 📦 */
    if (!g_ascii_strcasecmp(e,".sh")  ||!g_ascii_strcasecmp(e,".py")  ||
        !g_ascii_strcasecmp(e,".c")   ||!g_ascii_strcasecmp(e,".h")   ||
        !g_ascii_strcasecmp(e,".cpp") ||!g_ascii_strcasecmp(e,".js")  ||
        !g_ascii_strcasecmp(e,".ts")  ||!g_ascii_strcasecmp(e,".rs")  ||
        !g_ascii_strcasecmp(e,".go")  ||!g_ascii_strcasecmp(e,".java")||
        !g_ascii_strcasecmp(e,".rb")  ||!g_ascii_strcasecmp(e,".lua"))
        return "\xE2\x9A\x99";                          /* ⚙ */
    if (!g_ascii_strcasecmp(e,".txt") ||!g_ascii_strcasecmp(e,".md")  ||
        !g_ascii_strcasecmp(e,".log") ||!g_ascii_strcasecmp(e,".cfg") ||
        !g_ascii_strcasecmp(e,".conf")||!g_ascii_strcasecmp(e,".ini"))
        return "\xF0\x9F\x93\x9D";                      /* 📝 */
    if (!g_ascii_strcasecmp(e,".iso") ||!g_ascii_strcasecmp(e,".img"))
        return "\xF0\x9F\x92\xBF";                      /* 💿 */
    if (!g_ascii_strcasecmp(e,".html")||!g_ascii_strcasecmp(e,".htm") ||
        !g_ascii_strcasecmp(e,".xml") ||!g_ascii_strcasecmp(e,".json")||
        !g_ascii_strcasecmp(e,".yaml")||!g_ascii_strcasecmp(e,".toml"))
        return "\xF0\x9F\x8C\x90";                      /* 🌐 */
    if (!g_ascii_strcasecmp(e,".ttf") ||!g_ascii_strcasecmp(e,".otf") ||
        !g_ascii_strcasecmp(e,".woff"))
        return "\xF0\x9F\x94\xA4";                      /* 🔤 */
    if (!g_ascii_strcasecmp(e,".deb") ||!g_ascii_strcasecmp(e,".rpm") ||
        !g_ascii_strcasecmp(e,".appimage"))
        return "\xF0\x9F\x93\xA6";
    return "\xF0\x9F\x93\x84";
}

/* Portal-friendly open (works on Wayland / sandboxed sessions) */
static void
open_file(AppState *st, const char *path)
{
    GFile *f = g_file_new_for_path(path);
    GtkFileLauncher *l = gtk_file_launcher_new(f);
    gtk_file_launcher_launch(l,
        st && st->window ? GTK_WINDOW(st->window) : NULL,
        NULL, NULL, NULL);
    g_object_unref(l);
    g_object_unref(f);
}


/* ═══════════════════════════════════════════
 * MENU BUILDING BLOCKS (shared with ui.c)
 * ═══════════════════════════════════════════ */

GtkWidget *
myfm_menu_row(const char *icon, const char *label,
              GCallback cb, gpointer data, GtkWidget *popover)
{
    /* Menus are text only (Windows 10 context-menu style): the `icon`
     * argument is accepted for API compatibility and ignored. */
    (void)icon;
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

    GtkWidget *lb = gtk_label_new(label);
    gtk_widget_add_css_class(lb, "ctx-label");
    gtk_widget_set_halign(lb, GTK_ALIGN_START);
    gtk_widget_set_hexpand(lb, TRUE);
    gtk_box_append(GTK_BOX(row), lb);

    GtkWidget *btn = gtk_button_new();
    gtk_widget_add_css_class(btn, "context-item");
    gtk_button_set_child(GTK_BUTTON(btn), row);

    if (cb) g_signal_connect(btn, "clicked", cb, data);
    if (popover)
        g_signal_connect_swapped(btn, "clicked",
            G_CALLBACK(gtk_popover_popdown), popover);
    return btn;
}

GtkWidget *
myfm_menu_separator(void)
{
    GtkWidget *s = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(s, 3);
    gtk_widget_set_margin_bottom(s, 3);
    gtk_widget_set_margin_start(s, 6);
    gtk_widget_set_margin_end(s, 6);
    return s;
}

static gboolean
unparent_popover_idle(gpointer p)
{
    GtkWidget *w = p;
    if (GTK_IS_WIDGET(w) && gtk_widget_get_parent(w))
        gtk_widget_unparent(w);
    g_object_unref(w);
    return G_SOURCE_REMOVE;
}

static void
on_popover_closed(GtkPopover *pop, gpointer ud)
{
    (void)ud;
    g_idle_add(unparent_popover_idle, g_object_ref(pop));
}

/* Create a popover anchored at (x,y) of `origin`, parented to the window
 * so a background refresh cannot destroy it while it is open. */
static GtkWidget *
popover_at(AppState *st, GtkWidget *origin, double x, double y)
{
    GtkWidget *pop = gtk_popover_new();
    gtk_popover_set_has_arrow(GTK_POPOVER(pop), FALSE);
    gtk_popover_set_autohide(GTK_POPOVER(pop), TRUE);
    gtk_widget_add_css_class(pop, "myfm-menu");
    gtk_widget_set_halign(pop, GTK_ALIGN_START);

    graphene_point_t out;
    graphene_point_t in = GRAPHENE_POINT_INIT((float)x, (float)y);
    if (!gtk_widget_compute_point(origin, st->window, &in, &out)) {
        out.x = (float)x;
        out.y = (float)y;
    }
    GdkRectangle rect = { (int)out.x, (int)out.y, 1, 1 };
    gtk_widget_set_parent(pop, st->window);
    gtk_popover_set_pointing_to(GTK_POPOVER(pop), &rect);

    g_signal_connect(pop, "closed", G_CALLBACK(on_popover_closed), NULL);
    return pop;
}

static GtkWidget *
popover_content(GtkWidget *pop)
{
    GtkWidget *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start(vb, 3);
    gtk_widget_set_margin_end(vb, 3);
    gtk_widget_set_margin_top(vb, 3);
    gtk_widget_set_margin_bottom(vb, 3);
    gtk_popover_set_child(GTK_POPOVER(pop), vb);
    return vb;
}


/* ═══════════════════════════════════════════
 * SELECTION & ACTION STATE
 * ═══════════════════════════════════════════ */

/* Another application (or another MyFM window) took over the clipboard:
 * forget our own cut/copy list, otherwise Ctrl+V would keep pasting the
 * files *we* copied earlier instead of what is on the clipboard now.
 * Connected with g_signal_connect_object() on the window, so `win` is
 * the user data and the handler goes away together with the window. */
void
myfm_on_clipboard_changed(GdkClipboard *cb, gpointer win)
{
    AppState *st = g_object_get_data(G_OBJECT(win), "app-state");
    if (!st) return;
    if (gdk_clipboard_get_content(cb) != st->clip_provider ||
        st->clip_provider == NULL) {
        g_clear_object(&st->clip_provider);
        if (st->clip_paths->len)
            g_ptr_array_remove_range(st->clip_paths, 0, st->clip_paths->len);
    }
    myfm_update_actions(st);
}

void
myfm_update_actions(AppState *st)
{
    guint     n         = st->sel_items->len;
    gboolean  in_folder = (st->place == PLACE_FOLDER && st->current_path != NULL);
    gboolean  file_sel  = (n >= 1 && st->selected_kind == ITEM_FILE);
    gboolean  vol_sel   = (n == 1 && st->selected_kind == ITEM_VOLUME);

    /* Volumes and partitions never expose file operations. */
    if (st->cmd_new)
        gtk_widget_set_sensitive(st->cmd_new, in_folder);
    if (st->cmd_cut)
        gtk_widget_set_sensitive(st->cmd_cut, in_folder && file_sel);
    if (st->cmd_copy)
        gtk_widget_set_sensitive(st->cmd_copy, in_folder && file_sel);
    if (st->cmd_rename)
        gtk_widget_set_sensitive(st->cmd_rename, in_folder && file_sel && n == 1);
    if (st->cmd_trash)
        gtk_widget_set_sensitive(st->cmd_trash, in_folder && file_sel);
    if (st->cmd_paste)
        gtk_widget_set_sensitive(st->cmd_paste,
            in_folder && (st->clip_paths->len > 0 ||
                          gdk_content_formats_contain_gtype(
                              gdk_clipboard_get_formats(
                                  gtk_widget_get_clipboard(st->window)),
                              GDK_TYPE_FILE_LIST)));
    if (st->cmd_props)
        gtk_widget_set_sensitive(st->cmd_props,
            st->selected_path != NULL || in_folder);
    if (st->cmd_eject)
        gtk_widget_set_visible(st->cmd_eject, vol_sel && st->selected_removable);

    myfm_update_preview(st);
}

/* Adds one widget/path to the live selection set (no de-dup check). */
static void
sel_add_full(AppState *st, GtkWidget *w, const char *path, gboolean is_dir,
            ItemKind kind, gboolean removable)
{
    SelItem *si = g_new0(SelItem, 1);
    si->path      = g_strdup(path);
    si->widget    = w;
    si->is_dir    = is_dir;
    si->kind      = kind;
    si->removable = removable;
    g_ptr_array_add(st->sel_items, si);
    if (GTK_IS_WIDGET(w)) {
        gtk_widget_add_css_class(w, "item-selected");
        sync_row_checkbox(w, TRUE);
    }
}

static SelItem *
sel_find_widget(AppState *st, GtkWidget *w, guint *idx_out)
{
    for (guint i = 0; i < st->sel_items->len; i++) {
        SelItem *s = g_ptr_array_index(st->sel_items, i);
        if (s->widget == w) {
            if (idx_out) *idx_out = i;
            return s;
        }
    }
    return NULL;
}

/* Refreshes the "primary" selected_* fields (used by rename / properties /
 * open / eject, which only make sense for a single item) from the last
 * entry of the live selection set. */
static void
sel_sync_primary(AppState *st)
{
    g_clear_pointer(&st->selected_path, g_free);
    if (st->sel_items->len == 0) {
        st->selected_widget    = NULL;
        st->selected_is_dir    = FALSE;
        st->selected_kind      = ITEM_NONE;
        st->selected_removable = FALSE;
        return;
    }
    SelItem *last = g_ptr_array_index(st->sel_items, st->sel_items->len - 1);
    st->selected_widget    = last->widget;
    st->selected_path      = g_strdup(last->path);
    st->selected_is_dir    = last->is_dir;
    st->selected_kind      = last->kind;
    st->selected_removable = last->removable;
}

static void
clear_selection(AppState *st)
{
    for (guint i = 0; i < st->sel_items->len; i++) {
        SelItem *s = g_ptr_array_index(st->sel_items, i);
        if (s->widget && GTK_IS_WIDGET(s->widget)) {
            gtk_widget_remove_css_class(s->widget, "item-selected");
            sync_row_checkbox(s->widget, FALSE);
        }
    }
    if (st->sel_items->len)
        g_ptr_array_remove_range(st->sel_items, 0, st->sel_items->len);
    sel_sync_primary(st);
}

void
myfm_clear_multi_selection(AppState *st)
{
    clear_selection(st);
    myfm_update_actions(st);
}

static void
update_status(AppState *st)
{
    if (!st->status_right) return;

    guint n = st->sel_items->len;
    if (n == 1) {
        char *base = g_path_get_basename(st->selected_path);
        char *txt  = g_strdup_printf("%s selected   |   %s",
            st->selected_kind == ITEM_VOLUME ? "1 drive" : "1 item", base);
        gtk_label_set_text(GTK_LABEL(st->status_right), txt);
        g_free(txt);
        g_free(base);
    } else if (n > 1) {
        char *txt = g_strdup_printf("%u items selected", n);
        gtk_label_set_text(GTK_LABEL(st->status_right), txt);
        g_free(txt);
    } else {
        gtk_label_set_text(GTK_LABEL(st->status_right), "");
    }
}

/* Plain click: replaces the whole selection with a single item. */
static void
select_item(AppState *st, GtkWidget *w, const char *path,
            gboolean is_dir, ItemKind kind, gboolean removable)
{
    clear_selection(st);
    sel_add_full(st, w, path, is_dir, kind, removable);
    st->sel_anchor = w;
    sel_sync_primary(st);
    update_status(st);
    myfm_update_actions(st);
}

/* Ctrl+Click: adds/removes one item from the selection.  Volumes are
 * never part of a multi-selection, matching drives/partitions never
 * accepting bulk operations. */
void
myfm_toggle_item_selection(AppState *st, GtkWidget *w, const char *path,
                           gboolean is_dir, ItemKind kind, gboolean removable)
{
    if (kind == ITEM_VOLUME || st->selected_kind == ITEM_VOLUME) {
        select_item(st, w, path, is_dir, kind, removable);
        return;
    }

    guint idx = 0;
    SelItem *found = sel_find_widget(st, w, &idx);
    if (found) {
        gtk_widget_remove_css_class(w, "item-selected");
        sync_row_checkbox(w, FALSE);
        g_ptr_array_remove_index(st->sel_items, idx);
    } else {
        sel_add_full(st, w, path, is_dir, kind, removable);
    }
    st->sel_anchor = w;
    sel_sync_primary(st);
    update_status(st);
    myfm_update_actions(st);
}

static void
set_status_left(AppState *st, const char *fmt, ...)
{
    if (!st->status_left) return;
    va_list ap;
    va_start(ap, fmt);
    char *s = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    gtk_label_set_text(GTK_LABEL(st->status_left), s);
    g_free(s);
}


/* ═══════════════════════════════════════════
 * SHARED CALLBACK CONTEXTS
 * ═══════════════════════════════════════════ */

typedef struct {
    AppState *st;
    char     *path;
} PathCtx;

static PathCtx *
path_ctx_new(AppState *st, const char *path)
{
    PathCtx *p = g_new0(PathCtx, 1);
    p->st   = st;
    p->path = g_strdup(path);
    return p;
}

static void
path_ctx_free(gpointer p)
{
    PathCtx *pc = p;
    g_free(pc->path);
    g_free(pc);
}


/* ═══════════════════════════════════════════
 * FILE OPERATIONS (helpers)
 * ═══════════════════════════════════════════ */

static gboolean
delete_recursive(GFile *file, GError **err)
{
    GFileInfo *info = g_file_query_info(file,
        G_FILE_ATTRIBUTE_STANDARD_TYPE,
        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);

    gboolean is_dir = info &&
        g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY;
    g_clear_object(&info);

    if (is_dir) {
        GFileEnumerator *en = g_file_enumerate_children(file,
            G_FILE_ATTRIBUTE_STANDARD_NAME,
            G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
        if (en) {
            GFileInfo *ci;
            while ((ci = g_file_enumerator_next_file(en, NULL, NULL))) {
                GFile *child = g_file_get_child(file, g_file_info_get_name(ci));
                delete_recursive(child, NULL);
                g_object_unref(child);
                g_object_unref(ci);
            }
            g_object_unref(en);
        }
    }
    return g_file_delete(file, NULL, err);
}

static gboolean
copy_recursive(GFile *src, GFile *dst, GError **err)
{
    GFileInfo *info = g_file_query_info(src,
        G_FILE_ATTRIBUTE_STANDARD_TYPE,
        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, err);
    if (!info) return FALSE;

    gboolean is_dir =
        g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY;
    g_object_unref(info);

    if (!is_dir)
        return g_file_copy(src, dst, G_FILE_COPY_NOFOLLOW_SYMLINKS,
            NULL, NULL, NULL, err);

    if (!g_file_make_directory_with_parents(dst, NULL, err)) {
        if (!g_error_matches(*err, G_IO_ERROR, G_IO_ERROR_EXISTS))
            return FALSE;
        g_clear_error(err);
    }

    GFileEnumerator *en = g_file_enumerate_children(src,
        G_FILE_ATTRIBUTE_STANDARD_NAME,
        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, err);
    if (!en) return FALSE;

    gboolean ok = TRUE;
    GFileInfo *ci;
    while (ok && (ci = g_file_enumerator_next_file(en, NULL, NULL)) != NULL) {
        const char *nm = g_file_info_get_name(ci);
        GFile *cs = g_file_get_child(src, nm);
        GFile *cd = g_file_get_child(dst, nm);
        ok = copy_recursive(cs, cd, err);
        g_object_unref(cs);
        g_object_unref(cd);
        g_object_unref(ci);
    }
    g_object_unref(en);
    return ok;
}

/* Windows-Explorer-style "(Copy)" / "(Copy 2)" suffix, inserted *before*
 * the file extension rather than appended after it. */
static char *
copy_suffix_name(const char *base, gboolean is_dir, int n)
{
    const char *dot = is_dir ? NULL : g_strrstr(base, ".");
    if (dot && dot == base) dot = NULL;   /* “.bashrc” has no extension */

    char *stem = dot ? g_strndup(base, (gsize)(dot - base)) : g_strdup(base);
    const char *ext = dot ? dot : "";

    char *result = (n <= 1)
        ? g_strdup_printf("%s (Copy)%s", stem, ext)
        : g_strdup_printf("%s (Copy %d)%s", stem, n, ext);
    g_free(stem);
    return result;
}

/* Builds a free destination path for a copy of `base`, always using the
 * "(Copy)" scheme above (never the previous "name.ext (copy)" form). */
static char *
unique_copy_child(const char *dir, const char *base, gboolean is_dir)
{
    int n = 1;
    char *name = copy_suffix_name(base, is_dir, n);
    char *candidate = g_build_filename(dir, name, NULL);
    while (g_file_test(candidate, G_FILE_TEST_EXISTS)) {
        g_free(name);
        g_free(candidate);
        n++;
        name = copy_suffix_name(base, is_dir, n);
        candidate = g_build_filename(dir, name, NULL);
    }
    g_free(name);
    return candidate;
}

static void
clip_paths_reset(AppState *st)
{
    if (st->clip_paths->len)
        g_ptr_array_remove_range(st->clip_paths, 0, st->clip_paths->len);
}

/* Cut / Copy: remembers the files internally *and* publishes them on the
 * real system clipboard as a GdkFileList, so cutting/copying in MyFM and
 * pasting into another application (or vice-versa) works with the actual
 * files, not just their path as text. */
static void
set_clipboard(AppState *st, GPtrArray *paths, gboolean cut)
{
    clip_paths_reset(st);
    for (guint i = 0; i < paths->len; i++)
        g_ptr_array_add(st->clip_paths, g_strdup((const char *)g_ptr_array_index(paths, i)));
    st->clip_is_cut = cut;

    GSList *flist = NULL;
    for (guint i = 0; i < paths->len; i++)
        flist = g_slist_prepend(flist,
            g_file_new_for_path((const char *)g_ptr_array_index(paths, i)));
    flist = g_slist_reverse(flist);

    GValue value = G_VALUE_INIT;
    g_value_init(&value, GDK_TYPE_FILE_LIST);
    g_value_take_boxed(&value, gdk_file_list_new_from_list(flist));
    GdkContentProvider *provider = gdk_content_provider_new_for_value(&value);
    g_value_unset(&value);
    g_slist_free_full(flist, g_object_unref);

    /* Remember our provider *before* claiming the clipboard: claiming
     * emits "changed", whose handler drops clip_paths unless the
     * clipboard still holds this provider. */
    g_clear_object(&st->clip_provider);
    st->clip_provider = provider;          /* takes the reference */

    GdkClipboard *cb = gtk_widget_get_clipboard(st->window);
    gdk_clipboard_set_content(cb, provider);

    if (paths->len == 1)
        set_status_left(st, "%s: %s", cut ? "Cut" : "Copied",
            (const char *)g_ptr_array_index(paths, 0));
    else
        set_status_left(st, "%s: %u items", cut ? "Cut" : "Copied", paths->len);

    myfm_update_actions(st);
}


/* ── rename ── */

static void
on_rename_answer(const char *text, gpointer ud)
{
    PathCtx *pc = ud;
    AppState *st = pc->st;

    if (!text || !text[0]) return;

    if (strchr(text, '/')) {
        show_error(st, "Invalid name",
            "A file name cannot contain the “/” character.");
        return;
    }

    char *dir  = g_path_get_dirname(pc->path);
    char *newp = g_build_filename(dir, text, NULL);

    if (strcmp(newp, pc->path) != 0) {
        if (g_file_test(newp, G_FILE_TEST_EXISTS)) {
            show_error(st, "Name already in use",
                "“%s” already exists in this folder.", text);
        } else if (g_rename(pc->path, newp) != 0) {
            show_error(st, "Rename failed", "%s", g_strerror(errno));
        } else {
            clear_selection(st);
        }
    }

    g_free(dir);
    g_free(newp);
    refresh_view(st);
}

static void
rename_dialog(AppState *st, const char *path)
{
    char *base = g_path_get_basename(path);
    gboolean is_dir = g_file_test(path, G_FILE_TEST_IS_DIR);

    myfm_prompt(st->window, "\xE2\x9C\x8F", "Rename",
        is_dir ? "Type a new name for this folder."
               : "Type a new name for this file.",
        base, "Rename",
        on_rename_answer, path_ctx_new(st, path), path_ctx_free);

    g_free(base);
}



/* ── eject a removable volume ── */

typedef struct {
    AppState  *st;
    GtkWidget *win;      /* weak pointer */
    char      *name;
} EjectCtx;

static void
eject_ctx_free(EjectCtx *e)
{
    if (e->win)
        g_object_remove_weak_pointer(G_OBJECT(e->win), (gpointer *)&e->win);
    g_free(e->name);
    g_free(e);
}

static void
eject_report(EjectCtx *e, gboolean ok, GError *err)
{
    if (e->win) {
        if (ok) {
            myfm_message_fmt(e->win, "\xE2\x8F\x8F", "Device ejected",
                "You can now safely remove “%s”.", e->name);
            refresh_view(e->st);
        } else {
            myfm_message_fmt(e->win, "\xE2\x9A\xA0", "Could not eject",
                "%s", err ? err->message
                          : "The device is still in use by another program.");
        }
    }
    eject_ctx_free(e);
}

static void
on_eject_done(GObject *src, GAsyncResult *res, gpointer ud)
{
    GError *err = NULL;
    gboolean ok = g_mount_eject_with_operation_finish(
        G_MOUNT(src), res, &err);
    eject_report(ud, ok, err);
    g_clear_error(&err);
}

static void
on_unmount_done(GObject *src, GAsyncResult *res, gpointer ud)
{
    GError *err = NULL;
    gboolean ok = g_mount_unmount_with_operation_finish(
        G_MOUNT(src), res, &err);
    eject_report(ud, ok, err);
    g_clear_error(&err);
}

static GMount *
mount_for_path(const char *mountpoint)
{
    GVolumeMonitor *mon = g_volume_monitor_get();
    GList *mounts = g_volume_monitor_get_mounts(mon);
    GMount *found = NULL;

    for (GList *it = mounts; it && !found; it = it->next) {
        GMount *m = G_MOUNT(it->data);
        GFile *root = g_mount_get_default_location(m);
        if (!root) continue;
        char *p = g_file_get_path(root);
        g_object_unref(root);
        if (p && strcmp(p, mountpoint) == 0)
            found = g_object_ref(m);
        g_free(p);
    }

    g_list_free_full(mounts, g_object_unref);
    g_object_unref(mon);
    return found;
}

static void
on_eject_answer(gboolean accepted, gpointer ud)
{
    PathCtx *pc = ud;
    AppState *st = pc->st;
    if (!accepted) return;

    GMount *m = mount_for_path(pc->path);
    if (!m) {
        show_error(st, "Could not eject",
            "This volume is not managed by the session and has to be "
            "unmounted manually.");
        return;
    }

    EjectCtx *e = g_new0(EjectCtx, 1);
    e->st   = st;
    e->win  = st->window;
    e->name = g_mount_get_name(m);
    if (!e->name || !e->name[0]) {
        g_free(e->name);
        e->name = g_path_get_basename(pc->path);
    }
    g_object_add_weak_pointer(G_OBJECT(st->window), (gpointer *)&e->win);

    GMountOperation *op = gtk_mount_operation_new(GTK_WINDOW(st->window));
    set_status_left(st, "Ejecting %s…", e->name);

    if (g_mount_can_eject(m))
        g_mount_eject_with_operation(m, G_MOUNT_UNMOUNT_NONE, op, NULL,
            on_eject_done, e);
    else if (g_mount_can_unmount(m))
        g_mount_unmount_with_operation(m, G_MOUNT_UNMOUNT_NONE, op, NULL,
            on_unmount_done, e);
    else {
        show_error(st, "Could not eject", "This volume cannot be ejected.");
        eject_ctx_free(e);
    }

    g_object_unref(op);
    g_object_unref(m);
}

static void
eject_confirm(AppState *st, const char *mountpoint)
{
    char *base = g_path_get_basename(mountpoint);
    char *body = g_strdup_printf(
        "“%s” will be unmounted. Make sure every file transfer is finished "
        "before you unplug the device.", base);

    myfm_confirm(st->window, "\xE2\x8F\x8F", "Eject this device?",
        body, "Eject", FALSE,
        on_eject_answer, path_ctx_new(st, mountpoint), path_ctx_free);

    g_free(base);
    g_free(body);
}


/* ═══════════════════════════════════════════
 * CONTEXT MENUS
 * ═══════════════════════════════════════════ */

typedef struct {
    char     *path;
    gboolean  is_dir;
    gboolean  removable;
    AppState *state;
} CtxData;

static void
ctx_free(gpointer p)
{
    CtxData *c = p;
    g_free(c->path);
    g_free(c);
}

static void ctx_open(GtkWidget *w, gpointer p)
{
    (void)w;
    CtxData *c = p;
    if (c->is_dir) navigate_to(c->state, c->path, TRUE);
    else           open_file(c->state, c->path);
}

static void ctx_open_with(GtkWidget *w, gpointer p)
{
    (void)w;
    CtxData *c = p;
    myfm_open_with_path(c->state, c->path);
}

static void ctx_open_new_window(GtkWidget *w, gpointer p)
{
    (void)w;
    CtxData *c = p;
    myfm_window_new(c->state->app, c->path);
}

static void ctx_terminal(GtkWidget *w, gpointer p)
{
    (void)w;
    CtxData *c = p;
    char *dir = c->is_dir ? g_strdup(c->path) : g_path_get_dirname(c->path);

    /* Wayland-native terminals first, no xterm / X11 fallback.  Each one
     * spells its "start in this folder" option differently; the child is
     * also spawned with `dir` as its cwd, which covers the rest. */
    static const struct { const char *exe; const char *flag; } terms[] = {
        { "ptyxis",         "--working-directory=" },
        { "kgx",            "--working-directory=" },
        { "gnome-terminal", "--working-directory=" },
        { "konsole",        "--workdir"            },
        { "foot",           "--working-directory=" },
        { "alacritty",      "--working-directory"  },
        { "kitty",          "--directory="         },
        { "wezterm",        NULL                   },
        { "xfce4-terminal", "--working-directory=" },
        { "mate-terminal",  "--working-directory=" },
        { "tilix",          "--working-directory=" },
        { NULL, NULL }
    };

    for (int i = 0; terms[i].exe; i++) {
        char *exe = g_find_program_in_path(terms[i].exe);
        if (!exe) continue;

        GPtrArray *argv = g_ptr_array_new_with_free_func(g_free);
        g_ptr_array_add(argv, g_strdup(exe));
        if (!strcmp(terms[i].exe, "wezterm")) {
            g_ptr_array_add(argv, g_strdup("start"));
            g_ptr_array_add(argv, g_strdup("--cwd"));
            g_ptr_array_add(argv, g_strdup(dir));
        } else if (g_str_has_suffix(terms[i].flag, "=")) {
            g_ptr_array_add(argv, g_strconcat(terms[i].flag, dir, NULL));
        } else {
            g_ptr_array_add(argv, g_strdup(terms[i].flag));
            g_ptr_array_add(argv, g_strdup(dir));
        }
        g_ptr_array_add(argv, NULL);

        /* No G_SPAWN_DO_NOT_REAP_CHILD: nobody waits for the terminal,
         * so asking GLib not to reap it left a zombie per launch. */
        GError *err = NULL;
        gboolean ok = g_spawn_async(dir, (char **)argv->pdata, NULL,
            G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &err);
        g_ptr_array_free(argv, TRUE);
        g_free(exe);
        if (err) g_error_free(err);
        if (ok) { g_free(dir); return; }
    }

    show_error(c->state, "No terminal found",
        "Install a terminal emulator to use this action.");
    g_free(dir);
}

static void ctx_cut(GtkWidget *w, gpointer p)
{ (void)w; CtxData *c = p; (void)c; myfm_cut_selected(c->state); }

static void ctx_copy(GtkWidget *w, gpointer p)
{ (void)w; CtxData *c = p; (void)c; myfm_copy_selected(c->state); }

static void ctx_copy_path(GtkWidget *w, gpointer p)
{ (void)w; CtxData *c = p; (void)c; myfm_copy_path_selected(c->state); }

static void ctx_rename(GtkWidget *w, gpointer p)
{ (void)w; CtxData *c = p; rename_dialog(c->state, c->path); }

static void ctx_trash(GtkWidget *w, gpointer p)
{ (void)w; CtxData *c = p; (void)c; myfm_trash_selected(c->state); }

static void ctx_delete(GtkWidget *w, gpointer p)
{ (void)w; CtxData *c = p; (void)c; myfm_delete_selected(c->state); }

static void ctx_eject(GtkWidget *w, gpointer p)
{ (void)w; CtxData *c = p; eject_confirm(c->state, c->path); }

static void ctx_properties(GtkWidget *w, gpointer p)
{
    (void)w;
    CtxData *c = p;
    show_properties_dialog(c->state->window, c->path);
}

static void ctx_paste(GtkWidget *w, gpointer p)
{ (void)w; CtxData *c = p; myfm_paste_into(c->state, c->path); }

static void ctx_new_folder(GtkWidget *w, gpointer p)
{ (void)w; CtxData *c = p; myfm_new_folder(c->state); }

static void ctx_new_file(GtkWidget *w, gpointer p)
{ (void)w; CtxData *c = p; myfm_new_file(c->state); }

static void ctx_refresh(GtkWidget *w, gpointer p)
{ (void)w; CtxData *c = p; refresh_view(c->state); }


/* Regular files and folders: the full menu. */
static void
show_item_menu(AppState *st, GtkWidget *origin, double x, double y,
               const char *path, gboolean is_dir)
{
    CtxData *c = g_new0(CtxData, 1);
    c->path   = g_strdup(path);
    c->is_dir = is_dir;
    c->state  = st;

    GtkWidget *pop = popover_at(st, origin, x, y);
    GtkWidget *vb  = popover_content(pop);

    gtk_box_append(GTK_BOX(vb), myfm_menu_row(
        is_dir ? "\xF0\x9F\x93\x82" : "\xF0\x9F\x93\x84", "Open",
        G_CALLBACK(ctx_open), c, pop));
    gtk_box_append(GTK_BOX(vb), myfm_menu_row(
        "\xF0\x9F\x9A\x80", "Open with\xE2\x80\xA6",
        G_CALLBACK(ctx_open_with), c, pop));

    if (is_dir) {
        gtk_box_append(GTK_BOX(vb), myfm_menu_row(
            "\xF0\x9F\xAA\x9F", "Open in new window",
            G_CALLBACK(ctx_open_new_window), c, pop));
        gtk_box_append(GTK_BOX(vb), myfm_menu_row(
            "\xE2\x8C\xA8", "Open in terminal",
            G_CALLBACK(ctx_terminal), c, pop));
    }

    gtk_box_append(GTK_BOX(vb), myfm_menu_separator());

    gtk_box_append(GTK_BOX(vb), myfm_menu_row(
        "\xE2\x9C\x82", "Cut", G_CALLBACK(ctx_cut), c, pop));
    gtk_box_append(GTK_BOX(vb), myfm_menu_row(
        "\xF0\x9F\x93\x8B", "Copy", G_CALLBACK(ctx_copy), c, pop));
    gtk_box_append(GTK_BOX(vb), myfm_menu_row(
        "\xF0\x9F\x93\x8E", "Copy as path", G_CALLBACK(ctx_copy_path), c, pop));
    if (is_dir)
        gtk_box_append(GTK_BOX(vb), myfm_menu_row(
            "\xF0\x9F\x93\xA5", "Paste into folder",
            G_CALLBACK(ctx_paste), c, pop));

    gtk_box_append(GTK_BOX(vb), myfm_menu_separator());

    gtk_box_append(GTK_BOX(vb), myfm_menu_row(
        "\xE2\x9C\x8F", "Rename", G_CALLBACK(ctx_rename), c, pop));
    gtk_box_append(GTK_BOX(vb), myfm_menu_row(
        "\xF0\x9F\x97\x91", "Move to Recycle Bin",
        G_CALLBACK(ctx_trash), c, pop));
    gtk_box_append(GTK_BOX(vb), myfm_menu_row(
        "\xE2\x9D\x8C", "Delete permanently",
        G_CALLBACK(ctx_delete), c, pop));

    gtk_box_append(GTK_BOX(vb), myfm_menu_separator());

    gtk_box_append(GTK_BOX(vb), myfm_menu_row(
        "\xE2\x84\xB9", "Properties", G_CALLBACK(ctx_properties), c, pop));

    g_object_set_data_full(G_OBJECT(pop), "ctx", c, ctx_free);
    gtk_popover_popup(GTK_POPOVER(pop));
}

/* Drives and partitions: no cut / copy / rename / delete at all. */
static void
show_volume_menu(AppState *st, GtkWidget *origin, double x, double y,
                 const char *mountpoint, gboolean removable)
{
    CtxData *c = g_new0(CtxData, 1);
    c->path      = g_strdup(mountpoint);
    c->is_dir    = TRUE;
    c->removable = removable;
    c->state     = st;

    GtkWidget *pop = popover_at(st, origin, x, y);
    GtkWidget *vb  = popover_content(pop);

    gtk_box_append(GTK_BOX(vb), myfm_menu_row(
        "\xF0\x9F\x93\x82", "Open", G_CALLBACK(ctx_open), c, pop));
    gtk_box_append(GTK_BOX(vb), myfm_menu_row(
        "\xF0\x9F\xAA\x9F", "Open in new window",
        G_CALLBACK(ctx_open_new_window), c, pop));
    gtk_box_append(GTK_BOX(vb), myfm_menu_row(
        "\xE2\x8C\xA8", "Open in terminal",
        G_CALLBACK(ctx_terminal), c, pop));

    if (removable) {
        gtk_box_append(GTK_BOX(vb), myfm_menu_separator());
        gtk_box_append(GTK_BOX(vb), myfm_menu_row(
            "\xE2\x8F\x8F", "Eject", G_CALLBACK(ctx_eject), c, pop));
    }

    gtk_box_append(GTK_BOX(vb), myfm_menu_separator());
    gtk_box_append(GTK_BOX(vb), myfm_menu_row(
        "\xE2\x84\xB9", "Properties", G_CALLBACK(ctx_properties), c, pop));

    g_object_set_data_full(G_OBJECT(pop), "ctx", c, ctx_free);
    gtk_popover_popup(GTK_POPOVER(pop));
}

static void
show_background_menu(AppState *st, GtkWidget *origin, double x, double y)
{
    if (st->place != PLACE_FOLDER || !st->current_path) return;

    CtxData *c = g_new0(CtxData, 1);
    c->path   = g_strdup(st->current_path);
    c->is_dir = TRUE;
    c->state  = st;

    GtkWidget *pop = popover_at(st, origin, x, y);
    GtkWidget *vb  = popover_content(pop);

    gtk_box_append(GTK_BOX(vb), myfm_menu_row(
        "\xF0\x9F\x93\x81", "New folder", G_CALLBACK(ctx_new_folder), c, pop));
    gtk_box_append(GTK_BOX(vb), myfm_menu_row(
        "\xF0\x9F\x93\x84", "New document", G_CALLBACK(ctx_new_file), c, pop));

    gtk_box_append(GTK_BOX(vb), myfm_menu_separator());

    gtk_box_append(GTK_BOX(vb), myfm_menu_row(
        "\xF0\x9F\x93\xA5", "Paste", G_CALLBACK(ctx_paste), c, pop));
    gtk_box_append(GTK_BOX(vb), myfm_menu_row(
        "\xF0\x9F\x94\x84", "Refresh", G_CALLBACK(ctx_refresh), c, pop));
    gtk_box_append(GTK_BOX(vb), myfm_menu_row(
        "\xE2\x8C\xA8", "Open in terminal", G_CALLBACK(ctx_terminal), c, pop));

    gtk_box_append(GTK_BOX(vb), myfm_menu_separator());

    gtk_box_append(GTK_BOX(vb), myfm_menu_row(
        "\xE2\x84\xB9", "Properties", G_CALLBACK(ctx_properties), c, pop));

    g_object_set_data_full(G_OBJECT(pop), "ctx", c, ctx_free);
    gtk_popover_popup(GTK_POPOVER(pop));
}


/* ═══════════════════════════════════════════
 * ROW / TILE INTERACTION
 * ═══════════════════════════════════════════ */

typedef struct {
    char     *path;
    gboolean  is_dir;
    ItemKind  kind;
    gboolean  removable;
    AppState *state;
} EntryData;

static void edata_free(gpointer p)
{
    EntryData *e = p;
    g_free(e->path);
    g_free(e);
}

static gboolean
sel_contains_path(AppState *st, GtkWidget *w)
{
    return sel_find_widget(st, w, NULL) != NULL;
}

static void import_files(AppState *st, GSList *files, const char *dest_dir,
                         gboolean is_drop);

typedef struct { AppState *st; char *dest; } DropCtx;

static void
drop_ctx_free(gpointer p)
{
    DropCtx *d = p;
    g_free(d->dest);
    g_free(d);
}

static gboolean
on_drop_files_fixed(GtkDropTarget *target, const GValue *value,
                    double x, double y, gpointer ud)
{
    (void)target; (void)x; (void)y;
    DropCtx *d = ud;
    if (!G_VALUE_HOLDS(value, GDK_TYPE_FILE_LIST)) return FALSE;
    GdkFileList *fl = g_value_get_boxed(value);
    GSList *files = fl ? gdk_file_list_get_files(fl) : NULL;
    if (!files) return FALSE;
    import_files(d->st, files, d->dest, TRUE);
    g_slist_free(files);
    return TRUE;
}

static gboolean
on_drop_files_current(GtkDropTarget *target, const GValue *value,
                      double x, double y, gpointer ud)
{
    (void)target; (void)x; (void)y;
    AppState *st = ud;
    if (st->place != PLACE_FOLDER || !st->current_path) return FALSE;
    if (!G_VALUE_HOLDS(value, GDK_TYPE_FILE_LIST)) return FALSE;
    GdkFileList *fl = g_value_get_boxed(value);
    GSList *files = fl ? gdk_file_list_get_files(fl) : NULL;
    if (!files) return FALSE;
    import_files(st, files, st->current_path, TRUE);
    g_slist_free(files);
    return TRUE;
}

/* Lets external applications (or another instance of MyFM) drop files or
 * folders directly onto `w`; they are copied into `dest` (fixed) or, if
 * `dest` is NULL, into whatever folder is current when the drop lands
 * (used for the background of the listing, which stays the same widget
 * across navigations). */
static void
attach_drop_target(GtkWidget *w, AppState *st, const char *dest)
{
    GtkDropTarget *dt = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
    if (dest) {
        DropCtx *d = g_new0(DropCtx, 1);
        d->st   = st;
        d->dest = g_strdup(dest);
        g_object_set_data_full(G_OBJECT(dt), "drop-ctx", d, drop_ctx_free);
        g_signal_connect(dt, "drop", G_CALLBACK(on_drop_files_fixed), d);
    } else {
        g_signal_connect(dt, "drop", G_CALLBACK(on_drop_files_current), st);
    }
    gtk_widget_add_controller(w, GTK_EVENT_CONTROLLER(dt));
}

static void collect_entry_widgets(GtkWidget *w, GPtrArray *out);

/* Shift+Click: selects every file between the primary selection (the
 * anchor) and `w`; with Ctrl held too, the range is added to the current
 * selection instead of replacing it. */
static void
select_range(AppState *st, GtkWidget *w, gboolean keep)
{
    GtkWidget *anchor = st->sel_anchor ? st->sel_anchor : st->selected_widget;
    GPtrArray *rows = g_ptr_array_new();
    collect_entry_widgets(st->file_view, rows);

    int ia = -1, iw = -1;
    for (guint i = 0; i < rows->len; i++) {
        GtkWidget *r = g_ptr_array_index(rows, i);
        if (r == anchor) ia = (int)i;
        if (r == w)      iw = (int)i;
    }
    if (iw < 0) { g_ptr_array_free(rows, TRUE); return; }
    if (ia < 0) ia = iw;

    GtkWidget *keep_anchor = g_ptr_array_index(rows, ia);
    if (!keep) clear_selection(st);
    if (st->selected_kind == ITEM_VOLUME) clear_selection(st);
    int lo = MIN(ia, iw), hi = MAX(ia, iw);
    for (int i = lo; i <= hi; i++) {
        GtkWidget *r = g_ptr_array_index(rows, i);
        EntryData *e = g_object_get_data(G_OBJECT(r), "edata");
        if (!e || e->kind != ITEM_FILE) continue;
        if (sel_find_widget(st, r, NULL)) continue;
        sel_add_full(st, r, e->path, e->is_dir, e->kind, e->removable);
    }
    g_ptr_array_free(rows, TRUE);

    /* The clicked item becomes the primary one (last in the array);
     * the anchor stays put so the next Shift+Click extends from it. */
    guint idx;
    if (sel_find_widget(st, w, &idx) && idx != st->sel_items->len - 1) {
        SelItem *si = g_ptr_array_steal_index(st->sel_items, idx);
        g_ptr_array_add(st->sel_items, si);
    }
    st->sel_anchor = keep_anchor;

    sel_sync_primary(st);
    update_status(st);
    myfm_update_actions(st);
}

static void
entry_pressed(GtkGestureClick *gc, int n, double x, double y, gpointer ud)
{
    GtkWidget *w = GTK_WIDGET(ud);
    EntryData *e = g_object_get_data(G_OBJECT(w), "edata");
    if (!e) return;

    /* A click that lands on the row's own selection checkbox must be
     * handled *only* by that checkbox's own "toggled" signal — not also
     * by the row's plain-click logic below, which would immediately
     * replace/undo the toggle (this is what made the checkbox appear to
     * "un-check itself" the instant it was clicked). */
    GtkWidget *cb = g_object_get_data(G_OBJECT(w), "sel-checkbox");
    if (cb) {
        GtkWidget *picked = gtk_widget_pick(w, x, y, GTK_PICK_DEFAULT);
        for (GtkWidget *p = picked; p && p != w; p = gtk_widget_get_parent(p)) {
            if (p == cb) return;
        }
    }

    guint btn = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gc));
    GdkModifierType mod =
        gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gc));
    gboolean ctrl  = (mod & GDK_CONTROL_MASK) != 0;
    gboolean shift = (mod & GDK_SHIFT_MASK) != 0;

    if (btn == GDK_BUTTON_PRIMARY) {
        if (n == 1) {
            if (shift && e->kind == ITEM_FILE)
                select_range(e->state, w, ctrl);
            else if (ctrl)
                myfm_toggle_item_selection(e->state, w, e->path, e->is_dir,
                    e->kind, e->removable);
            else
                select_item(e->state, w, e->path, e->is_dir, e->kind, e->removable);
        } else if (n == 2) {
            if (e->is_dir) navigate_to(e->state, e->path, TRUE);
            else           open_file(e->state, e->path);
        }
    } else if (btn == GDK_BUTTON_SECONDARY && n == 1) {
        /* Right-clicking an item that is already part of a multi-selection
         * keeps the whole selection (so Cut/Copy/Delete act on all of it);
         * otherwise it becomes the sole selection, as in Explorer. */
        if (!sel_contains_path(e->state, w))
            select_item(e->state, w, e->path, e->is_dir, e->kind, e->removable);
        /* Drives, and the Home/Desktop/... shortcut tiles on "This PC",
         * get the menu without cut / rename / delete: those tiles are
         * the user's real folders, not something to trash from there. */
        if (e->kind == ITEM_VOLUME || e->state->place != PLACE_FOLDER)
            show_volume_menu(e->state, w, x, y, e->path, e->removable);
        else
            show_item_menu(e->state, w, x, y, e->path, e->is_dir);
    }
}

/* Files dragged OUT of MyFM: if the dragged row is part of the current
 * multi-selection, every selected file/folder goes along; otherwise just
 * that one row does — matching Cut/Copy's own selection semantics. */
static GSList *
drag_files_for_row(AppState *st, GtkWidget *w, const char *fallback_path)
{
    GSList *flist = NULL;
    if (st->sel_items->len > 0 && sel_find_widget(st, w, NULL)) {
        for (guint i = 0; i < st->sel_items->len; i++) {
            SelItem *s = g_ptr_array_index(st->sel_items, i);
            if (s->kind == ITEM_FILE)
                flist = g_slist_prepend(flist, g_file_new_for_path(s->path));
        }
        flist = g_slist_reverse(flist);
    }
    if (!flist)
        flist = g_slist_prepend(flist, g_file_new_for_path(fallback_path));
    return flist;
}

static GdkContentProvider *
on_row_drag_prepare(GtkDragSource *src, double x, double y, gpointer ud)
{
    (void)src; (void)x; (void)y;
    GtkWidget *w = GTK_WIDGET(ud);
    EntryData *e = g_object_get_data(G_OBJECT(w), "edata");
    if (!e || e->kind != ITEM_FILE) return NULL;

    GSList *flist = drag_files_for_row(e->state, w, e->path);

    GValue v = G_VALUE_INIT;
    g_value_init(&v, GDK_TYPE_FILE_LIST);
    g_value_take_boxed(&v, gdk_file_list_new_from_list(flist));
    GdkContentProvider *provider = gdk_content_provider_new_for_value(&v);
    g_value_unset(&v);
    g_slist_free_full(flist, g_object_unref);
    return provider;
}

static void
on_row_drag_begin(GtkDragSource *src, GdkDrag *drag, gpointer ud)
{
    (void)drag;
    GtkWidget *w = GTK_WIDGET(ud);
    GdkPaintable *paintable = gtk_widget_paintable_new(w);
    gtk_drag_source_set_icon(src, paintable, 0, 0);
    g_object_unref(paintable);
}

static void
attach_edata_full(GtkWidget *w, const char *path, gboolean is_dir,
                  ItemKind kind, gboolean removable, AppState *st)
{
    EntryData *e = g_new0(EntryData, 1);
    e->path      = g_strdup(path);
    e->is_dir    = is_dir;
    e->kind      = kind;
    e->removable = removable;
    e->state     = st;
    g_object_set_data_full(G_OBJECT(w), "edata", e, edata_free);

    GtkGesture *gc = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gc), 0);
    g_signal_connect(gc, "pressed", G_CALLBACK(entry_pressed), w);
    gtk_widget_add_controller(w, GTK_EVENT_CONTROLLER(gc));

    /* Folders (and volumes/mountpoints) accept files dropped from outside
     * MyFM straight onto them, copying into that specific folder. */
    if (is_dir)
        attach_drop_target(w, st, path);

    /* Files and folders alike can be dragged OUT to any other Wayland
     * application (a chat window, an editor, another file manager…). */
    if (kind == ITEM_FILE) {
        GtkDragSource *drag = gtk_drag_source_new();
        gtk_drag_source_set_actions(drag, GDK_ACTION_COPY);
        g_signal_connect(drag, "prepare", G_CALLBACK(on_row_drag_prepare), w);
        g_signal_connect(drag, "drag-begin", G_CALLBACK(on_row_drag_begin), w);
        gtk_widget_add_controller(w, GTK_EVENT_CONTROLLER(drag));
    }
}

static void
attach_edata(GtkWidget *w, const char *path, gboolean is_dir, AppState *st)
{
    attach_edata_full(w, path, is_dir, ITEM_FILE, FALSE, st);
}

/* Enumerates every row/tile widget currently shown in the file view,
 * regardless of list vs. grid layout (grid wraps tiles in GtkFlowBox). */
static void
collect_entry_widgets(GtkWidget *w, GPtrArray *out)
{
    if (g_object_get_data(G_OBJECT(w), "edata")) {
        g_ptr_array_add(out, w);
        return;
    }
    for (GtkWidget *c = gtk_widget_get_first_child(w); c; c = gtk_widget_get_next_sibling(c))
        collect_entry_widgets(c, out);
}

static gboolean
point_is_on_row(AppState *st, double x, double y)
{
    GtkWidget *picked = gtk_widget_pick(st->file_view, x, y, GTK_PICK_DEFAULT);
    while (picked && picked != st->file_view) {
        if (g_object_get_data(G_OBJECT(picked), "edata")) return TRUE;
        picked = gtk_widget_get_parent(picked);
    }
    return FALSE;
}

static void
marquee_apply_selection(AppState *st, double rx0, double ry0,
                        double rx1, double ry1)
{
    clear_selection(st);
    for (guint i = 0; i < st->marquee_preserve->len; i++) {
        SelItem *c = g_ptr_array_index(st->marquee_preserve, i);
        if (GTK_IS_WIDGET(c->widget))
            sel_add_full(st, c->widget, c->path, c->is_dir, c->kind, c->removable);
    }

    GPtrArray *rows = g_ptr_array_new();
    collect_entry_widgets(st->file_view, rows);
    for (guint i = 0; i < rows->len; i++) {
        GtkWidget *w = g_ptr_array_index(rows, i);
        EntryData *e = g_object_get_data(G_OBJECT(w), "edata");
        if (!e || e->kind != ITEM_FILE) continue;
        if (sel_find_widget(st, w, NULL)) continue;

        graphene_rect_t b;
        if (!gtk_widget_compute_bounds(w, st->file_view, &b)) continue;
        gboolean hit = !(b.origin.x > rx1 || b.origin.x + b.size.width  < rx0 ||
                         b.origin.y > ry1 || b.origin.y + b.size.height < ry0);
        if (hit)
            sel_add_full(st, w, e->path, e->is_dir, e->kind, e->removable);
    }
    g_ptr_array_free(rows, TRUE);

    sel_sync_primary(st);
    update_status(st);
    myfm_update_actions(st);
}

static void
on_marquee_drag_begin(GtkGestureDrag *gd, double x, double y, gpointer ud)
{
    AppState *st = ud;
    if (point_is_on_row(st, x, y)) { st->marqueeing = FALSE; return; }

    GdkModifierType mod =
        gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gd));
    st->drag_ctrl   = (mod & GDK_CONTROL_MASK) != 0;
    st->marqueeing  = TRUE;
    st->drag_start_x = x;
    st->drag_start_y = y;

    if (st->marquee_preserve->len)
        g_ptr_array_remove_range(st->marquee_preserve, 0, st->marquee_preserve->len);
    if (st->drag_ctrl) {
        for (guint i = 0; i < st->sel_items->len; i++) {
            SelItem *s = g_ptr_array_index(st->sel_items, i);
            SelItem *clone = g_new0(SelItem, 1);
            clone->path = g_strdup(s->path);
            clone->widget = s->widget;
            clone->is_dir = s->is_dir;
            clone->kind = s->kind;
            clone->removable = s->removable;
            g_ptr_array_add(st->marquee_preserve, clone);
        }
    }

    gtk_widget_set_margin_start(st->marquee, (int)x);
    gtk_widget_set_margin_top(st->marquee, (int)y);
    gtk_widget_set_size_request(st->marquee, 1, 1);
    gtk_widget_set_visible(st->marquee, TRUE);
}

static void
on_marquee_drag_update(GtkGestureDrag *gd, double ox, double oy, gpointer ud)
{
    AppState *st = ud;
    if (!st->marqueeing) return;
    (void)gd;

    double x2 = st->drag_start_x + ox;
    double y2 = st->drag_start_y + oy;
    double rx0 = MIN(st->drag_start_x, x2), rx1 = MAX(st->drag_start_x, x2);
    double ry0 = MIN(st->drag_start_y, y2), ry1 = MAX(st->drag_start_y, y2);

    gtk_widget_set_margin_start(st->marquee, (int)rx0);
    gtk_widget_set_margin_top(st->marquee, (int)ry0);
    gtk_widget_set_size_request(st->marquee,
        MAX(1, (int)(rx1 - rx0)), MAX(1, (int)(ry1 - ry0)));

    marquee_apply_selection(st, rx0, ry0, rx1, ry1);
}

static void
on_marquee_drag_end(GtkGestureDrag *gd, double ox, double oy, gpointer ud)
{
    (void)gd; (void)ox; (void)oy;
    AppState *st = ud;
    if (!st->marqueeing) return;
    st->marqueeing = FALSE;
    gtk_widget_set_visible(st->marquee, FALSE);
    if (st->marquee_preserve->len)
        g_ptr_array_remove_range(st->marquee_preserve, 0, st->marquee_preserve->len);
}

static void
background_pressed(GtkGestureClick *gc, int n, double x, double y, gpointer ud)
{
    AppState *st = ud;
    guint btn = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gc));
    GdkModifierType mod =
        gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gc));
    gboolean ctrl = (mod & GDK_CONTROL_MASK) != 0;

    /* This click bubbled up from a row/tile, which already handled its own
     * selection in entry_pressed(); clearing here would immediately undo
     * it. Only truly empty background clicks should reach this point. */
    if (point_is_on_row(st, x, y)) return;

    if (btn == GDK_BUTTON_PRIMARY && n == 1) {
        if (!ctrl) myfm_clear_multi_selection(st);
    } else if (btn == GDK_BUTTON_SECONDARY && n == 1) {
        if (!ctrl) myfm_clear_multi_selection(st);
        show_background_menu(st, st->file_view, x, y);
    }
}

void
myfm_attach_background_menu(AppState *st)
{
    GtkGesture *gc = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gc), 0);
    g_signal_connect(gc, "pressed", G_CALLBACK(background_pressed), st);
    gtk_widget_add_controller(st->file_view, GTK_EVENT_CONTROLLER(gc));
}

/* Rubber-band multi-selection on the empty area of the file view, plus
 * files/folders dropped in from other applications (they land in the
 * current folder; dropping directly on a folder row copies into it). */
void
myfm_attach_marquee(AppState *st)
{
    GtkGesture *drag = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), GDK_BUTTON_PRIMARY);
    g_signal_connect(drag, "drag-begin",  G_CALLBACK(on_marquee_drag_begin), st);
    g_signal_connect(drag, "drag-update", G_CALLBACK(on_marquee_drag_update), st);
    g_signal_connect(drag, "drag-end",    G_CALLBACK(on_marquee_drag_end), st);
    gtk_widget_add_controller(st->file_view, GTK_EVENT_CONTROLLER(drag));

    attach_drop_target(st->file_view, st, NULL);
}


/* ═══════════════════════════════════════════
 * DIRECTORY ENTRIES
 * ═══════════════════════════════════════════ */

typedef struct {
    char     *name;
    char     *path;
    gboolean  is_dir;
    gboolean  is_hidden;
    guint64   size;
    gint64    mtime;
    char     *type_desc;
    char     *content_type;
} Entry;

static void
entry_free(gpointer p)
{
    Entry *e = p;
    g_free(e->name);
    g_free(e->path);
    g_free(e->type_desc);
    g_free(e->content_type);
    g_free(e);
}

static int
entry_cmp(gconstpointer a, gconstpointer b, gpointer ud)
{
    AppState *st = ud;
    const Entry *ea = *(const Entry * const *)a;
    const Entry *eb = *(const Entry * const *)b;

    if (ea->is_dir != eb->is_dir)
        return ea->is_dir ? -1 : 1;

    int r = 0;
    switch (st->sort_column) {
        case SORT_SIZE:
            r = (ea->size < eb->size) ? -1 : (ea->size > eb->size) ? 1 : 0;
            break;
        case SORT_MTIME:
            r = (ea->mtime < eb->mtime) ? -1 : (ea->mtime > eb->mtime) ? 1 : 0;
            break;
        case SORT_TYPE:
            r = g_ascii_strcasecmp(ea->type_desc ? ea->type_desc : "",
                                   eb->type_desc ? eb->type_desc : "");
            break;
        case SORT_NAME:
        default:
            r = 0;
            break;
    }
    if (r == 0)
        r = g_ascii_strcasecmp(ea->name, eb->name);

    return st->sort_desc ? -r : r;
}

static GtkWidget *
fixed_label(const char *text, int width, gboolean right_align, const char *css)
{
    GtkWidget *l = gtk_label_new(text ? text : "");
    if (css) gtk_widget_add_css_class(l, css);
    gtk_widget_set_size_request(l, width, -1);
    gtk_label_set_xalign(GTK_LABEL(l), right_align ? 1.0f : 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
    gtk_widget_set_halign(l, right_align ? GTK_ALIGN_END : GTK_ALIGN_START);
    return l;
}

static void
on_row_checkbox_toggled(GtkCheckButton *cb, gpointer ud)
{
    GtkWidget *row = GTK_WIDGET(ud);
    EntryData *e = g_object_get_data(G_OBJECT(row), "edata");
    if (!e) return;
    (void)cb;
    myfm_toggle_item_selection(e->state, row, e->path, e->is_dir, e->kind, e->removable);
}

static GtkWidget *
make_selection_checkbox(GtkWidget *row)
{
    GtkWidget *cb = gtk_check_button_new();
    gtk_widget_add_css_class(cb, "sel-checkbox");
    gtk_widget_set_valign(cb, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(cb, CHECK_W, -1);
    g_object_set_data(G_OBJECT(row), "sel-checkbox", cb);
    g_signal_connect(cb, "toggled", G_CALLBACK(on_row_checkbox_toggled), row);
    return cb;
}

static GtkWidget *
build_list_row(const Entry *e, AppState *st)
{
    gboolean root = needs_root(e->path);

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(row, "file-row");
    gtk_widget_set_hexpand(row, TRUE);
    gtk_widget_set_margin_start(row, 2);
    gtk_widget_set_margin_end(row, 2);

    if (st->show_checkboxes)
        gtk_box_append(GTK_BOX(row), make_selection_checkbox(row));

    GtkWidget *icon = myfm_icon_widget(e->name, e->is_dir, e->content_type, 16);
    gtk_widget_set_size_request(icon, ICON_W, -1);
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(row), icon);

    GtkWidget *name = gtk_label_new(e->name);
    gtk_widget_add_css_class(name, root ? "file-label-root" : "file-label");
    gtk_widget_set_hexpand(name, TRUE);
    gtk_widget_set_halign(name, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(row), name);

    if (root) {
        GtkWidget *lock = gtk_label_new("\xF0\x9F\x94\x92");
        gtk_widget_set_opacity(lock, 0.65);
        gtk_box_append(GTK_BOX(row), lock);
    }

    char *dt = fmt_time(e->mtime);
    gtk_box_append(GTK_BOX(row), fixed_label(dt, DATE_W, FALSE, "file-meta"));
    g_free(dt);

    gtk_box_append(GTK_BOX(row),
        fixed_label(e->type_desc, TYPE_W, FALSE, "file-meta"));

    char *sz = e->is_dir ? g_strdup("") : fmt_size(e->size);
    gtk_box_append(GTK_BOX(row), fixed_label(sz, SIZE_W, TRUE, "file-meta"));
    g_free(sz);

    attach_edata(row, e->path, e->is_dir, st);
    return row;
}

static GtkWidget *
build_grid_tile(const Entry *e, AppState *st)
{
    gboolean root = needs_root(e->path);

    GtkWidget *tile = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_add_css_class(tile, "grid-tile");
    gtk_widget_set_size_request(tile, 84, 76);
    gtk_widget_set_halign(tile, GTK_ALIGN_CENTER);

    if (st->show_checkboxes) {
        GtkWidget *cb = make_selection_checkbox(tile);
        gtk_widget_set_halign(cb, GTK_ALIGN_END);
        gtk_box_append(GTK_BOX(tile), cb);
    }

    GtkWidget *icon = myfm_icon_widget(e->name, e->is_dir, e->content_type, 48);
    gtk_widget_add_css_class(icon, "grid-icon");
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(tile), icon);

    if (root) {
        GtkWidget *lock = gtk_label_new("\xF0\x9F\x94\x92");
        gtk_widget_add_css_class(lock, "grid-lock");
        gtk_widget_set_halign(lock, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(tile), lock);
    }

    GtkWidget *lbl = gtk_label_new(e->name);
    gtk_widget_add_css_class(lbl, "grid-label");
    gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(lbl), 11);
    gtk_label_set_justify(GTK_LABEL(lbl), GTK_JUSTIFY_CENTER);
    gtk_widget_set_halign(lbl, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(tile), lbl);

    attach_edata(tile, e->path, e->is_dir, st);
    return tile;
}

static GtkWidget *
info_label(const char *text)
{
    GtkWidget *l = gtk_label_new(text);
    gtk_widget_add_css_class(l, "info-label");
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    gtk_widget_set_margin_start(l, 12);
    gtk_widget_set_margin_top(l, 12);
    return l;
}


/* ═══════════════════════════════════════════
 * SHOW DIRECTORY
 * ═══════════════════════════════════════════ */

typedef struct {
    AppState     *st;          /* only ever read after confirming !cancelled */
    GPtrArray    *items;       /* Entry*, accumulated across async batches  */
    GCancellable *cancellable; /* owned ref; the single source of truth for
                                 * whether `st` might have been freed       */
    char         *path;
    GFileEnumerator *en;
    GHashTable   *keep_sel;    /* paths to re-select (same-folder refresh) */
    double        scroll;      /* vertical scroll to restore, or < 0      */
} DirLoadCtx;

static void
dir_load_ctx_free(gpointer p)
{
    DirLoadCtx *c = p;
    if (c->items) g_ptr_array_unref(c->items);
    g_clear_object(&c->cancellable);
    g_clear_pointer(&c->keep_sel, g_hash_table_unref);
    g_free(c->path);
    g_free(c);
}

/* Row/tile construction is spread over idle callbacks (ROWS_PER_IDLE at a
 * time) so a folder with thousands of entries never freezes the window:
 * the first screenful appears immediately and the rest streams in. */
#define ROWS_PER_IDLE 120

typedef struct {
    AppState     *st;          /* only touched while !cancelled              */
    GPtrArray    *items;       /* all Entry*, owned                          */
    GPtrArray    *visible;     /* borrowed Entry* that pass hidden/filter    */
    GCancellable *cancellable; /* owned ref; cancelled => st may be gone     */
    GtkWidget    *flow;        /* grid mode container, or NULL               */
    guint         next;        /* next index in `visible` to build           */
    guint         folders;
    GHashTable   *keep_sel;    /* paths to re-select, or NULL                */
    double        scroll;      /* vertical scroll to restore, or < 0         */
} RowBuildCtx;

static void
row_build_ctx_free(RowBuildCtx *c)
{
    g_ptr_array_unref(c->visible);
    g_ptr_array_unref(c->items);
    g_clear_object(&c->cancellable);
    g_clear_pointer(&c->keep_sel, g_hash_table_unref);
    g_free(c);
}

/* Restores the scroll position after a same-folder refresh.  Retried on
 * a few frames, because the rows only get their size once laid out. */
typedef struct { double target; int tries; } ScrollRestore;

static gboolean
restore_scroll_tick(GtkWidget *scroller, GdkFrameClock *fc, gpointer ud)
{
    (void)fc;
    ScrollRestore *r = ud;
    GtkAdjustment *adj =
        gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroller));
    double max = gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj);
    if (max >= r->target || ++r->tries > 10) {
        gtk_adjustment_set_value(adj, MIN(r->target, MAX(max, 0.0)));
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static void
finalize_dir_status(AppState *st, guint shown, guint folders)
{
    if (shown == 0) {
        GtkWidget *l = info_label(
            (st->filter_text && st->filter_text[0])
                ? "No items match your search"
                : "This folder is empty");
        anim_fade_in(l);
        gtk_box_append(GTK_BOX(st->file_view), l);
    }
    set_status_left(st, "%u item%s   |   %u folder%s",
        shown, shown == 1 ? "" : "s",
        folders, folders == 1 ? "" : "s");
    update_status(st);
    myfm_update_actions(st);
}

static gboolean
build_rows_idle(gpointer ud)
{
    RowBuildCtx *c = ud;

    /* Checked first and on the cancellable alone: once cancelled the
     * AppState (window) may already have been freed. */
    if (g_cancellable_is_cancelled(c->cancellable)) {
        row_build_ctx_free(c);
        return G_SOURCE_REMOVE;
    }

    AppState *st = c->st;
    guint end = MIN(c->next + ROWS_PER_IDLE, c->visible->len);

    for (; c->next < end; c->next++) {
        Entry *e = g_ptr_array_index(c->visible, c->next);
        GtkWidget *w = (st->view_mode == VIEW_GRID)
            ? build_grid_tile(e, st)
            : build_list_row(e, st);

        /* A refresh of the folder already on screen (file monitor, F5,
         * sort, filter…) keeps the selection and skips the entrance
         * animation, which made every background change flash. */
        if (c->keep_sel) {
            if (g_hash_table_contains(c->keep_sel, e->path))
                sel_add_full(st, w, e->path, e->is_dir, ITEM_FILE, FALSE);
        } else if (c->next < MAX_ANIMATED_ROWS) {
            anim_row_appear(w, c->next * 14);
        }

        if (c->flow) gtk_flow_box_append(GTK_FLOW_BOX(c->flow), w);
        else         gtk_box_append(GTK_BOX(st->file_view), w);

        if (e->is_dir) c->folders++;
    }

    if (c->next < c->visible->len)
        return G_SOURCE_CONTINUE;

    if (c->keep_sel) sel_sync_primary(st);
    if (c->scroll > 0) {
        ScrollRestore *r = g_new0(ScrollRestore, 1);
        r->target = c->scroll;
        gtk_widget_add_tick_callback(st->scroller, restore_scroll_tick, r, g_free);
    }
    finalize_dir_status(st, c->visible->len, c->folders);
    row_build_ctx_free(c);
    return G_SOURCE_REMOVE;
}

/* Runs once the (async) enumeration has delivered every entry: sorts,
 * filters, then hands row construction to build_rows_idle().  Takes
 * ownership of nothing — `items` is ref'd for the idle job. */
static void
finish_show_directory(AppState *st, GPtrArray *items, GCancellable *cancellable,
                      GHashTable *keep_sel, double scroll)
{
    g_ptr_array_sort_with_data(items, entry_cmp, st);

    RowBuildCtx *c = g_new0(RowBuildCtx, 1);
    c->st          = st;
    c->items       = g_ptr_array_ref(items);
    c->visible     = g_ptr_array_new();
    c->cancellable = g_object_ref(cancellable);
    c->keep_sel    = keep_sel ? g_hash_table_ref(keep_sel) : NULL;
    c->scroll      = scroll;

    for (guint i = 0; i < items->len; i++) {
        Entry *e = g_ptr_array_index(items, i);
        if (e->is_hidden && !st->show_hidden) continue;
        if (st->filter_text && st->filter_text[0]) {
            char *lname = g_utf8_strdown(e->name, -1);
            gboolean match = strstr(lname, st->filter_text) != NULL;
            g_free(lname);
            if (!match) continue;
        }
        g_ptr_array_add(c->visible, e);
    }

    if (st->view_mode == VIEW_GRID) {
        GtkWidget *flow = gtk_flow_box_new();
        gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);
        gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flow), 2);
        gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 24);
        gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(flow), TRUE);
        gtk_widget_set_hexpand(flow, TRUE);
        gtk_widget_set_valign(flow, GTK_ALIGN_START);
        gtk_widget_add_css_class(flow, "grid-flow");
        gtk_widget_set_margin_start(flow, 8);
        gtk_widget_set_margin_end(flow, 8);
        gtk_widget_set_margin_top(flow, 8);
        gtk_box_append(GTK_BOX(st->file_view), flow);
        c->flow = flow;
    }

    if (c->visible->len == 0) {
        finalize_dir_status(st, 0, 0);
        row_build_ctx_free(c);
        return;
    }

    /* First chunk right away (so the folder visibly opens), the rest in idle. */
    if (build_rows_idle(c) == G_SOURCE_CONTINUE)
        g_idle_add(build_rows_idle, c);
}

static void
dir_load_append_batch(DirLoadCtx *ctx, GList *files)
{
    for (GList *it = files; it; it = it->next) {
        GFileInfo *fi = G_FILE_INFO(it->data);
        const char *nm = g_file_info_get_name(fi);
        if (!nm) continue;

        Entry *e = g_new0(Entry, 1);
        const char *disp = g_file_info_get_display_name(fi);
        e->name      = g_strdup(disp ? disp : nm);
        e->path      = g_build_filename(ctx->path, nm, NULL);
        e->is_dir    = g_file_info_get_file_type(fi) == G_FILE_TYPE_DIRECTORY;
        e->is_hidden = g_file_info_get_is_hidden(fi) || nm[0] == '.';
        e->size      = (guint64)g_file_info_get_size(fi);

        GDateTime *dt = g_file_info_get_modification_date_time(fi);
        e->mtime = dt ? g_date_time_to_unix(dt) : 0;
        if (dt) g_date_time_unref(dt);

        if (e->is_dir) {
            e->type_desc = g_strdup("File folder");
        } else {
            /* fast-content-type: guessed from the name only.  The full
             * content type sniffs the first bytes of every single file,
             * which on big folders (and on every automatic refresh) is a
             * lot of disk reads for a column nobody needs to be exact. */
            const char *ct = g_file_info_get_attribute_string(fi,
                G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE);
            e->content_type = ct ? g_strdup(ct) : NULL;
            char *d = ct ? g_content_type_get_description(ct) : NULL;
            e->type_desc = d ? d : g_strdup("File");
        }

        g_ptr_array_add(ctx->items, e);
    }
}

static void on_dir_next_files_ready(GObject *source, GAsyncResult *res, gpointer ud);

static void
dir_load_request_more(DirLoadCtx *ctx)
{
    g_file_enumerator_next_files_async(ctx->en, 256, G_PRIORITY_DEFAULT,
        ctx->cancellable, on_dir_next_files_ready, ctx);
}

static void
on_dir_next_files_ready(GObject *source, GAsyncResult *res, gpointer ud)
{
    DirLoadCtx *ctx = ud;
    GError *err = NULL;
    GList *files = g_file_enumerator_next_files_finish(
        G_FILE_ENUMERATOR(source), res, &err);

    /* Checked on the cancellable itself, never on ctx->st: a cancelled
     * load means the window (and AppState) this ctx points at may
     * already be gone, cancelled right before being freed. */
    gboolean cancelled = g_cancellable_is_cancelled(ctx->cancellable);
    g_clear_error(&err);

    if (files) {
        if (!cancelled) dir_load_append_batch(ctx, files);
        g_list_free_full(files, g_object_unref);
        if (!cancelled) { dir_load_request_more(ctx); return; }
    }

    /* Either an empty batch (enumeration finished) or the listing was
     * cancelled (window closed, or superseded by a newer navigation):
     * either way, we're done reading. */
    g_file_enumerator_close_async(ctx->en, G_PRIORITY_DEFAULT, NULL, NULL, NULL);
    g_object_unref(ctx->en);
    ctx->en = NULL;

    if (!cancelled)
        finish_show_directory(ctx->st, ctx->items, ctx->cancellable,
                              ctx->keep_sel, ctx->scroll);
    dir_load_ctx_free(ctx);
}

static void
on_dir_enumerate_ready(GObject *source, GAsyncResult *res, gpointer ud)
{
    DirLoadCtx *ctx = ud;
    GError *err = NULL;
    GFileEnumerator *en = g_file_enumerate_children_finish(
        G_FILE(source), res, &err);

    gboolean cancelled = g_cancellable_is_cancelled(ctx->cancellable);

    if (cancelled) {
        if (en) g_object_unref(en);
        g_clear_error(&err);
        dir_load_ctx_free(ctx);
        return;
    }

    if (!en) {
        clear_file_view(ctx->st->file_view);
        gtk_widget_set_visible(ctx->st->column_header, FALSE);
        char *m = g_strdup_printf("Unable to open this folder\n%s",
            err ? err->message : "");
        gtk_box_append(GTK_BOX(ctx->st->file_view), info_label(m));
        g_free(m);
        g_clear_error(&err);
        set_status_left(ctx->st, "Access denied");
        myfm_update_actions(ctx->st);
        dir_load_ctx_free(ctx);
        return;
    }

    ctx->en = en;
    dir_load_request_more(ctx);
}

/* Cancels whatever asynchronous directory listing might still be in
 * flight. Needed whenever the view is about to show something else —
 * a new folder, This PC, or the Recycle Bin — so a slow listing from a
 * folder the person already navigated away from can't complete late and
 * inject rows into the wrong view. */
void
myfm_cancel_pending_dir_load(AppState *st)
{
    if (!st->dir_cancellable) return;
    g_cancellable_cancel(st->dir_cancellable);
    g_clear_object(&st->dir_cancellable);
}

static void
show_directory(AppState *st, const char *path)
{
    GtkWidget *fv = st->file_view;

    /* Same folder as the one on screen => this is a refresh: remember
     * what was selected and how far down the list was scrolled. */
    GHashTable *keep_sel = NULL;
    double scroll = -1;
    if (st->listed_path && !strcmp(st->listed_path, path)) {
        keep_sel = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        for (guint i = 0; i < st->sel_items->len; i++) {
            SelItem *si = g_ptr_array_index(st->sel_items, i);
            if (si->kind == ITEM_FILE)
                g_hash_table_add(keep_sel, g_strdup(si->path));
        }
        scroll = gtk_adjustment_get_value(
            gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(st->scroller)));
    }
    char *lp = g_strdup(path);          /* path may be st->listed_path */
    g_free(st->listed_path);
    st->listed_path = lp;
    g_clear_pointer(&st->preview_path, g_free);

    clear_selection(st);
    if (!keep_sel) anim_view_transition(fv);
    clear_file_view(fv);
    gtk_widget_set_visible(st->column_header, st->view_mode == VIEW_LIST);
    myfm_update_columns(st);

    /* Supersede (and cancel) whatever directory listing might still be
     * in flight from a previous, now-abandoned navigation. */
    myfm_cancel_pending_dir_load(st);
    st->dir_cancellable = g_cancellable_new();

    DirLoadCtx *ctx = g_new0(DirLoadCtx, 1);
    ctx->st          = st;
    ctx->items       = g_ptr_array_new_with_free_func(entry_free);
    ctx->cancellable = g_object_ref(st->dir_cancellable);
    ctx->path        = g_strdup(path);
    ctx->keep_sel    = keep_sel;
    ctx->scroll      = scroll;

    GFile *dirf = g_file_new_for_path(path);
    g_file_enumerate_children_async(dirf,
        "standard::name,standard::display-name,standard::type,"
        "standard::size,standard::fast-content-type,standard::is-hidden,"
        "time::modified",
        G_FILE_QUERY_INFO_NONE, G_PRIORITY_DEFAULT,
        st->dir_cancellable, on_dir_enumerate_ready, ctx);
    g_object_unref(dirf);
}


/* ═══════════════════════════════════════════
 * VIEW OPTIONS
 * ═══════════════════════════════════════════ */

void
set_view_mode(AppState *st, ViewMode mode)
{
    /* Details / Large icons is a remembered preference and, like in
     * Explorer's Settings, applies to every open window right away. */
    (void)st;
    myfm_settings()->view_mode = mode;
    myfm_settings_save();
    myfm_settings_apply_all();
}

/* Switches one window's layout state and toggle buttons (no relisting;
 * the caller refreshes). */
void
myfm_apply_view_mode(AppState *st, ViewMode mode)
{
    st->view_mode = mode;

    if (mode == VIEW_LIST) {
        gtk_widget_add_css_class(st->view_toggle_list, "view-btn-active");
        gtk_widget_remove_css_class(st->view_toggle_grid, "view-btn-active");
    } else {
        gtk_widget_add_css_class(st->view_toggle_grid, "view-btn-active");
        gtk_widget_remove_css_class(st->view_toggle_list, "view-btn-active");
    }
}

void
myfm_set_sort(AppState *st, SortColumn column)
{
    if (st->sort_column == column)
        st->sort_desc = !st->sort_desc;
    else {
        st->sort_column = column;
        st->sort_desc   = FALSE;
    }
    refresh_view(st);
}

void
myfm_toggle_sort_direction(AppState *st)
{
    st->sort_desc = !st->sort_desc;
    refresh_view(st);
}

void
myfm_toggle_hidden(AppState *st)
{
    /* remembered, and applied to every window like in Explorer */
    myfm_settings()->show_hidden = !st->show_hidden;
    myfm_settings_save();
    myfm_settings_apply_all();
}

/* Ctrl+A: every file/folder of the current listing. */
void
myfm_select_all(AppState *st)
{
    if (st->place != PLACE_FOLDER) return;
    GPtrArray *rows = g_ptr_array_new();
    collect_entry_widgets(st->file_view, rows);
    clear_selection(st);
    for (guint i = 0; i < rows->len; i++) {
        GtkWidget *w = g_ptr_array_index(rows, i);
        EntryData *e = g_object_get_data(G_OBJECT(w), "edata");
        if (e && e->kind == ITEM_FILE)
            sel_add_full(st, w, e->path, e->is_dir, e->kind, e->removable);
    }
    g_ptr_array_free(rows, TRUE);
    sel_sync_primary(st);
    update_status(st);
    myfm_update_actions(st);
}

void
myfm_set_filter(AppState *st, const char *text)
{
    g_free(st->filter_text);
    st->filter_text = (text && text[0]) ? g_utf8_strdown(text, -1) : NULL;
    refresh_view(st);
}

void
refresh_view(AppState *st)
{
    switch (st->place) {
        case PLACE_THIS_PC: myfm_show_this_pc(st); break;
        case PLACE_TRASH:   show_trash(st);   break;
        case PLACE_FOLDER:
        default:
            if (st->current_path) show_directory(st, st->current_path);
            break;
    }
}


/* ═══════════════════════════════════════════
 * THIS PC  – drives from /proc/mounts + GIO
 * ═══════════════════════════════════════════ */

typedef struct {
    char *device;
    char *mountpoint;
    char *fstype;
} PMount;

static void
pmount_free(gpointer p)
{
    PMount *m = p;
    g_free(m->device);
    g_free(m->mountpoint);
    g_free(m->fstype);
    g_free(m);
}

/* Allow-list: only real on-disk / removable / network filesystems.
 * This is what makes the main disk ("/") show up reliably. */
static gboolean
fs_is_real(const char *fs)
{
    static const char *ok[] = {
        "ext2", "ext3", "ext4", "btrfs", "xfs", "f2fs", "jfs", "reiserfs",
        "zfs", "bcachefs", "nilfs2", "vfat", "msdos", "exfat", "ntfs",
        "ntfs3", "fuseblk", "iso9660", "udf", "hfs", "hfsplus", "apfs",
        "ubifs", "erofs", "nfs", "nfs4", "cifs", "smb3", "sshfs",
        "fuse.sshfs", "lustre", "ceph", "9p", "virtiofs",
        NULL
    };
    for (int i = 0; ok[i]; i++)
        if (!g_ascii_strcasecmp(fs, ok[i])) return TRUE;
    return FALSE;
}

static gboolean
mountpoint_is_boring(const char *mp)
{
    if (g_str_has_prefix(mp, "/proc"))             return TRUE;
    if (g_str_has_prefix(mp, "/sys"))              return TRUE;
    if (g_str_has_prefix(mp, "/dev"))              return TRUE;
    if (g_str_has_prefix(mp, "/snap"))             return TRUE;
    if (g_str_has_prefix(mp, "/var/lib/docker"))   return TRUE;
    if (g_str_has_prefix(mp, "/var/lib/flatpak/")) return TRUE;
    if (g_str_has_prefix(mp, "/run/") &&
        !g_str_has_prefix(mp, "/run/media"))       return TRUE;
    return FALSE;
}

/* A mount point that must never offer "Eject". */
static gboolean
mountpoint_is_system(const char *mp)
{
    static const char *sys[] = {
        "/", "/home", "/boot", "/boot/efi", "/efi", "/usr", "/var",
        "/opt", "/srv", "/tmp", NULL
    };
    for (int i = 0; sys[i]; i++)
        if (strcmp(mp, sys[i]) == 0) return TRUE;
    return FALSE;
}

static GPtrArray *
read_proc_mounts(void)
{
    GPtrArray *arr = g_ptr_array_new_with_free_func(pmount_free);
    FILE *f = fopen("/proc/mounts", "r");
    if (!f) return arr;

    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        char dev[512], mp[512], fs[128];
        if (sscanf(line, "%511s %511s %127s", dev, mp, fs) != 3) continue;

        /* "/" is always shown, whatever the filesystem is */
        gboolean is_root = (strcmp(mp, "/") == 0);
        if (!is_root && !fs_is_real(fs)) continue;

        char *mount = g_strcompress(mp);      /* decode \040 etc. */
        if (!is_root && mountpoint_is_boring(mount)) { g_free(mount); continue; }

        /* One tile per device, like Explorer: btrfs subvolumes and bind
         * mounts (/home, /var/log, /var/cache/… on the same partition)
         * would otherwise each show up as another "drive". */
        char *device = g_strcompress(dev);
        gboolean dup = FALSE;
        for (guint i = 0; i < arr->len; i++) {
            PMount *o = g_ptr_array_index(arr, i);
            if (!strcmp(o->mountpoint, mount) ||
                (g_str_has_prefix(device, "/dev/") && !strcmp(o->device, device))) {
                dup = TRUE;
                break;
            }
        }
        g_free(device);
        if (dup) { g_free(mount); continue; }

        PMount *pm = g_new0(PMount, 1);
        pm->device     = g_strcompress(dev);
        pm->mountpoint = mount;
        pm->fstype     = g_strdup(fs);
        g_ptr_array_add(arr, pm);
    }
    fclose(f);
    return arr;
}

static gboolean
dev_is_optical(const char *dev)
{
    return dev && (g_str_has_prefix(dev, "/dev/sr")    ||
                   g_str_has_prefix(dev, "/dev/cdrom") ||
                   g_str_has_prefix(dev, "/dev/dvd"));
}

static gboolean
gdrive_is_optical(GDrive *d)
{
    if (!d) return FALSE;
    char *id = g_drive_get_identifier(d, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
    gboolean r = dev_is_optical(id);
    g_free(id);
    return r;
}

static const char *
drive_icon_for(const char *mp, const char *dev, gboolean optical)
{
    if (optical) return "\xF0\x9F\x92\xBF";                 /* 💿 */
    if (mp && (g_str_has_prefix(mp, "/media") ||
               g_str_has_prefix(mp, "/run/media")))
        return "\xF0\x9F\x94\x8C";                          /* 🔌 */
    if (mp && !strcmp(mp, "/"))
        return "\xF0\x9F\x92\xBD";                          /* 💽 */
    if (dev && (g_str_has_prefix(dev, "/dev/sd")    ||
                g_str_has_prefix(dev, "/dev/nvme")  ||
                g_str_has_prefix(dev, "/dev/mmcblk")||
                g_str_has_prefix(dev, "/dev/vd")))
        return "\xF0\x9F\x92\xBE";                          /* 💾 */
    return "\xF0\x9F\x96\xA5";                              /* 🖥 */
}

static void
on_tile_eject_clicked(GtkButton *b, gpointer ud)
{
    AppState *st = g_object_get_data(G_OBJECT(b), "state");
    eject_confirm(st, (const char *)ud);
}

/* Windows-11-like drive tile */
/* Network filesystems: statvfs() on them can block for a long time (or
 * forever, with a dead NFS/SMB server), freezing the whole window.  Their
 * usage bar is simply not shown. */
static gboolean
fs_is_network(const char *fs)
{
    static const char *net[] = {
        "nfs", "nfs4", "cifs", "smb3", "smbfs", "sshfs", "fuse.sshfs",
        "9p", "ceph", "lustre", "afs", "davfs", "fuse.rclone", NULL
    };
    if (!fs) return FALSE;
    for (int i = 0; net[i]; i++)
        if (!g_ascii_strcasecmp(fs, net[i])) return TRUE;
    return FALSE;
}

/* Windows-10-like drive tile */
static GtkWidget *
make_drive_tile(AppState *st, const char *icon_str, const char *label,
                const char *mountpoint, const char *device,
                gboolean optical, gboolean removable, gboolean query_usage)
{
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(card, "drive-tile");
    if (optical) gtk_widget_add_css_class(card, "drive-tile-optical");
    gtk_widget_set_size_request(card, 250, -1);

    const char *icon_name = optical   ? "media-optical"
                          : removable ? "drive-removable-media"
                          :             "drive-harddisk";
    GtkWidget *ico = myfm_named_icon_widget(icon_name, icon_str, 32);
    gtk_widget_add_css_class(ico, "drive-icon");
    gtk_widget_set_valign(ico, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(card), ico);

    GtkWidget *info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(info, TRUE);
    gtk_widget_set_valign(info, GTK_ALIGN_CENTER);

    GtkWidget *nl = gtk_label_new(label);
    gtk_widget_add_css_class(nl, "drive-name");
    gtk_widget_set_halign(nl, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(nl), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(nl), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(info), nl);

    gboolean have_usage = FALSE;
    if (mountpoint && query_usage) {
        struct statvfs sv;
        if (statvfs(mountpoint, &sv) == 0 && sv.f_blocks > 0) {
            guint64 total = (guint64)sv.f_blocks * sv.f_frsize;
            guint64 avail = (guint64)sv.f_bavail * sv.f_frsize;
            guint64 used  = total - (guint64)sv.f_bfree * sv.f_frsize;
            double  frac  = CLAMP((double)used / (double)total, 0.0, 1.0);

            GtkWidget *prog = gtk_progress_bar_new();
            gtk_widget_set_hexpand(prog, TRUE);
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(prog), frac);
            if (frac > 0.9) gtk_widget_add_css_class(prog, "progress-full");
            gtk_box_append(GTK_BOX(info), prog);

            char *a = fmt_size(avail), *t = fmt_size(total);
            char *sp = g_strdup_printf("%s free of %s", a, t);
            GtkWidget *sl = gtk_label_new(sp);
            gtk_widget_add_css_class(sl, "drive-space");
            gtk_widget_set_halign(sl, GTK_ALIGN_START);
            gtk_label_set_xalign(GTK_LABEL(sl), 0.0f);
            gtk_box_append(GTK_BOX(info), sl);
            g_free(a); g_free(t); g_free(sp);
            have_usage = TRUE;
        }
    }

    if (!have_usage) {
        GtkWidget *s = gtk_label_new(
            optical ? "No disc inserted"
                    : (mountpoint ? mountpoint : "Not mounted"));
        gtk_widget_add_css_class(s, "drive-unmounted");
        gtk_widget_set_halign(s, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(info), s);
    } else if (device) {
        GtkWidget *d = gtk_label_new(device);
        gtk_widget_add_css_class(d, "drive-path");
        gtk_widget_set_halign(d, GTK_ALIGN_START);
        gtk_label_set_ellipsize(GTK_LABEL(d), PANGO_ELLIPSIZE_MIDDLE);
        gtk_box_append(GTK_BOX(info), d);
    }

    gtk_box_append(GTK_BOX(card), info);

    /* removable media get a dedicated eject button */
    if (mountpoint && removable) {
        GtkWidget *ej = gtk_button_new_with_label("\xE2\x8F\x8F");
        gtk_widget_add_css_class(ej, "eject-btn");
        gtk_widget_set_tooltip_text(ej, "Eject");
        gtk_widget_set_valign(ej, GTK_ALIGN_CENTER);
        g_object_set_data(G_OBJECT(ej), "state", st);
        g_object_set_data_full(G_OBJECT(ej), "mp",
            g_strdup(mountpoint), g_free);
        g_signal_connect(ej, "clicked",
            G_CALLBACK(on_tile_eject_clicked),
            g_object_get_data(G_OBJECT(ej), "mp"));
        gtk_box_append(GTK_BOX(card), ej);
    }

    if (mountpoint)
        attach_edata_full(card, mountpoint, TRUE, ITEM_VOLUME, removable, st);

    anim_fade_in(card);
    return card;
}

static GtkWidget *
section_title(const char *text)
{
    GtkWidget *l = gtk_label_new(text);
    gtk_widget_add_css_class(l, "section-title");
    gtk_widget_set_halign(l, GTK_ALIGN_FILL);
    gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
    gtk_widget_set_margin_start(l, 10);
    gtk_widget_set_margin_end(l, 10);
    gtk_widget_set_margin_top(l, 8);
    gtk_widget_set_margin_bottom(l, 4);
    return l;
}

static GtkWidget *
tile_flow(void)
{
    GtkWidget *flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flow), 1);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 4);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flow), 2);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flow), 2);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(flow), TRUE);
    gtk_widget_set_margin_start(flow, 10);
    gtk_widget_set_margin_end(flow, 10);
    gtk_widget_add_css_class(flow, "grid-flow");
    return flow;
}

void
myfm_show_this_pc(AppState *st)
{
    GtkWidget *fv = st->file_view;

    myfm_cancel_pending_dir_load(st);
    clear_selection(st);
    g_clear_pointer(&st->listed_path, g_free);
    g_clear_pointer(&st->preview_path, g_free);
    anim_view_transition(fv);
    clear_file_view(fv);
    gtk_widget_set_visible(st->column_header, FALSE);

    /* ── user folders (like Windows 10) ── */
    struct { const char *icon_name; const char *icon; const char *name; const char *sub; } folders[] = {
        { "user-home",        "\xF0\x9F\x8F\xA0", "Home",      NULL          },
        { "user-desktop",     "\xF0\x9F\x96\xA5", "Desktop",   "Desktop"     },
        { "folder-documents", "\xF0\x9F\x93\x84", "Documents", "Documents"   },
        { "folder-download",  "\xE2\xAC\x87",     "Downloads", "Downloads"   },
        { "folder-pictures",  "\xF0\x9F\x96\xBC", "Pictures",  "Pictures"    },
        { "folder-music",     "\xF0\x9F\x8E\xB5", "Music",     "Music"       },
        { "folder-videos",    "\xF0\x9F\x8E\xAC", "Videos",    "Videos"      },
    };

    GtkWidget *fflow = tile_flow();
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(fflow), 7);
    int nfolders = 0;
    for (guint i = 0; i < G_N_ELEMENTS(folders); i++) {
        char *p = folders[i].sub
            ? g_build_filename(g_get_home_dir(), folders[i].sub, NULL)
            : g_strdup(g_get_home_dir());
        if (g_file_test(p, G_FILE_TEST_IS_DIR)) {
            GtkWidget *tile = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
            gtk_widget_add_css_class(tile, "grid-tile");
            gtk_widget_set_size_request(tile, 84, 72);

            GtkWidget *ic = myfm_named_icon_widget(folders[i].icon_name,
                folders[i].icon, 48);
            gtk_widget_add_css_class(ic, "grid-icon");
            gtk_widget_set_halign(ic, GTK_ALIGN_CENTER);
            gtk_box_append(GTK_BOX(tile), ic);

            GtkWidget *nl = gtk_label_new(folders[i].name);
            gtk_widget_add_css_class(nl, "grid-label");
            gtk_widget_set_halign(nl, GTK_ALIGN_CENTER);
            gtk_box_append(GTK_BOX(tile), nl);

            attach_edata(tile, p, TRUE, st);
            anim_row_appear(tile, (guint)(nfolders * 16));
            gtk_flow_box_append(GTK_FLOW_BOX(fflow), tile);
            nfolders++;
        }
        g_free(p);
    }
    if (nfolders > 0) {
        char *t = g_strdup_printf("Folders (%d)", nfolders);
        gtk_box_append(GTK_BOX(fv), section_title(t));
        g_free(t);
        gtk_box_append(GTK_BOX(fv), fflow);
    } else {
        g_object_ref_sink(fflow);
        g_object_unref(fflow);
    }

    /* ── drives ── */
    GtkWidget *drives_title = section_title("Devices and drives");
    gtk_box_append(GTK_BOX(fv), drives_title);
    GtkWidget *dflow = tile_flow();
    gtk_box_append(GTK_BOX(fv), dflow);

    GHashTable *names = g_hash_table_new_full(
        g_str_hash, g_str_equal, g_free, g_free);
    GHashTable *ejectable = g_hash_table_new_full(
        g_str_hash, g_str_equal, g_free, NULL);
    GHashTable *seen = g_hash_table_new_full(
        g_str_hash, g_str_equal, g_free, NULL);

    GVolumeMonitor *mon = g_volume_monitor_get();
    GList *mounts = g_volume_monitor_get_mounts(mon);
    for (GList *it = mounts; it; it = it->next) {
        GMount *m = G_MOUNT(it->data);
        GFile *root = g_mount_get_default_location(m);
        if (!root) continue;
        char *mp = g_file_get_path(root);
        g_object_unref(root);
        if (!mp) continue;

        char *nm = g_mount_get_name(m);
        if (nm && nm[0])
            g_hash_table_insert(names, g_strdup(mp), g_strdup(nm));
        g_free(nm);

        GDrive *drv = g_mount_get_drive(m);
        gboolean rem = g_mount_can_eject(m) ||
            (drv && (g_drive_is_removable(drv) || g_drive_can_eject(drv)));
        if (drv) g_object_unref(drv);
        if (rem && !mountpoint_is_system(mp))
            g_hash_table_insert(ejectable, g_strdup(mp), GINT_TO_POINTER(1));

        g_free(mp);
    }
    g_list_free_full(mounts, g_object_unref);

    int count = 0;

    /* 1. real mounts from /proc/mounts – this includes "/" */
    GPtrArray *pm = read_proc_mounts();
    for (guint i = 0; i < pm->len; i++) {
        PMount *p = g_ptr_array_index(pm, i);
        if (g_hash_table_contains(seen, p->mountpoint)) continue;

        gboolean opt = dev_is_optical(p->device) ||
                       !g_ascii_strcasecmp(p->fstype, "iso9660") ||
                       !g_ascii_strcasecmp(p->fstype, "udf");

        gboolean removable =
            !mountpoint_is_system(p->mountpoint) &&
            (g_hash_table_contains(ejectable, p->mountpoint) || opt ||
             g_str_has_prefix(p->mountpoint, "/run/media") ||
             g_str_has_prefix(p->mountpoint, "/media"));

        const char *gio_name = g_hash_table_lookup(names, p->mountpoint);
        char *label;
        if (!strcmp(p->mountpoint, "/"))
            label = g_strdup("Local Disk (/)");
        else if (gio_name)
            label = g_strdup_printf("%s (%s)", gio_name, p->mountpoint);
        else {
            char *base = g_path_get_basename(p->mountpoint);
            label = g_strdup_printf("%s (%s)", base, p->mountpoint);
            g_free(base);
        }

        GtkWidget *tile = make_drive_tile(st,
            drive_icon_for(p->mountpoint, p->device, opt),
            label, p->mountpoint, p->device, opt, removable,
            !fs_is_network(p->fstype));
        gtk_flow_box_append(GTK_FLOW_BOX(dflow), tile);
        g_hash_table_insert(seen, g_strdup(p->mountpoint), GINT_TO_POINTER(1));
        g_free(label);
        count++;
    }
    g_ptr_array_unref(pm);

    /* 2. GVFS / network mounts not backed by /proc/mounts */
    mounts = g_volume_monitor_get_mounts(mon);
    for (GList *it = mounts; it; it = it->next) {
        GMount *m = G_MOUNT(it->data);
        GFile *root = g_mount_get_default_location(m);
        if (!root) continue;
        char *mp = g_file_get_path(root);
        g_object_unref(root);
        if (!mp) continue;
        if (g_hash_table_contains(seen, mp) || !strcmp(mp, g_get_home_dir())) {
            g_free(mp);
            continue;
        }
        GDrive *drv = g_mount_get_drive(m);
        gboolean opt = gdrive_is_optical(drv);
        if (drv) g_object_unref(drv);

        gboolean removable = g_hash_table_contains(ejectable, mp);
        char *nm = g_mount_get_name(m);
        GtkWidget *tile = make_drive_tile(st,
            drive_icon_for(mp, NULL, opt),
            nm && nm[0] ? nm : mp, mp, NULL, opt, removable,
            FALSE /* gvfs/FUSE mounts may sit on a slow network */);
        gtk_flow_box_append(GTK_FLOW_BOX(dflow), tile);
        g_hash_table_insert(seen, g_strdup(mp), GINT_TO_POINTER(1));
        g_free(nm);
        g_free(mp);
        count++;
    }
    g_list_free_full(mounts, g_object_unref);

    /* 3. optical drives with no disc */
    GList *drives = g_volume_monitor_get_connected_drives(mon);
    for (GList *d = drives; d; d = d->next) {
        GDrive *drv = G_DRIVE(d->data);
        if (!gdrive_is_optical(drv)) continue;

        GList *vols = g_drive_get_volumes(drv);
        gboolean mounted = FALSE;
        for (GList *v = vols; v; v = v->next) {
            GMount *m = g_volume_get_mount(G_VOLUME(v->data));
            if (m) { mounted = TRUE; g_object_unref(m); }
        }
        g_list_free_full(vols, g_object_unref);
        if (mounted) continue;

        char *dn  = g_drive_get_name(drv);
        char *dev = g_drive_get_identifier(drv,
            G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
        GtkWidget *tile = make_drive_tile(st, "\xF0\x9F\x92\xBF",
            dn ? dn : "Optical Drive", NULL, dev, TRUE, FALSE, FALSE);
        gtk_flow_box_append(GTK_FLOW_BOX(dflow), tile);
        g_free(dn);
        g_free(dev);
        count++;
    }
    g_list_free_full(drives, g_object_unref);
    g_object_unref(mon);

    g_hash_table_destroy(names);
    g_hash_table_destroy(ejectable);
    g_hash_table_destroy(seen);

    {
        char *t = g_strdup_printf("Devices and drives (%d)", count);
        gtk_label_set_text(GTK_LABEL(drives_title), t);
        g_free(t);
    }

    if (count == 0) {
        GtkWidget *l = info_label("No drives detected");
        anim_fade_in(l);
        gtk_box_append(GTK_BOX(fv), l);
    }

    /* location bookkeeping */
    st->place = PLACE_THIS_PC;
    if (st->directory_monitor) {
        g_file_monitor_cancel(st->directory_monitor);
        g_clear_object(&st->directory_monitor);
    }
    g_clear_pointer(&st->current_path, g_free);
    update_crumbs_special(st, "\xF0\x9F\x92\xBB  This PC");
    gtk_editable_set_text(GTK_EDITABLE(st->path_entry), "This PC");
    gtk_window_set_title(GTK_WINDOW(st->window), "This PC");

    set_status_left(st, "%d drive%s", count, count == 1 ? "" : "s");
    update_status(st);
    update_nav_buttons(st);
    myfm_update_actions(st);
}

void
this_pc_button_clicked(GtkButton *b, gpointer ud)
{
    (void)b;
    AppState *st = ud;
    st->place = PLACE_THIS_PC;
    myfm_show_this_pc(st);
}


/* ═══════════════════════════════════════════
 * TRASH
 * ═══════════════════════════════════════════ */

typedef struct {
    char     *uri;
    char     *orig;
    char     *name;
    char     *info_path;  /* .trashinfo file, only for the local fallback */
    AppState *state;
} TrashItem;

/* Where the freedesktop.org trash of the home volume lives.  Used when
 * the "trash:///" location is unavailable, which is the case on every
 * desktop that does not run GVfs (most non-GNOME Wayland sessions);
 * g_file_trash() itself writes here without needing GVfs. */
static char *
local_trash_dir(void)
{
    return g_build_filename(g_get_user_data_dir(), "Trash", NULL);
}

static void
trash_item_free(gpointer p)
{
    TrashItem *t = p;
    g_free(t->info_path);
    g_free(t->uri);
    g_free(t->orig);
    g_free(t->name);
    g_free(t);
}

static void
trash_restore(GtkWidget *w, gpointer p)
{
    (void)w;
    TrashItem *t = p;
    if (!t->orig) {
        show_error(t->state, "Cannot restore",
            "The original location of this item is unknown.");
        return;
    }
    GFile *src = g_file_new_for_uri(t->uri);
    GFile *dst = g_file_new_for_path(t->orig);
    GError *err = NULL;

    /* The folder it came from may have been removed meanwhile. */
    GFile *dst_parent = g_file_get_parent(dst);
    if (dst_parent) {
        g_file_make_directory_with_parents(dst_parent, NULL, NULL);
        g_object_unref(dst_parent);
    }

    if (!g_file_move(src, dst, G_FILE_COPY_NONE, NULL, NULL, NULL, &err))
        show_error(t->state, "Could not restore", "%s",
            err ? err->message : "Unknown error");
    else if (t->info_path)
        g_unlink(t->info_path);
    g_clear_error(&err);
    g_object_unref(src);
    g_object_unref(dst);
    refresh_view(t->state);
}

typedef struct {
    AppState *st;
    char     *uri;
    char     *info_path;
} TrashDelCtx;

static void
trash_del_ctx_free(gpointer p)
{
    TrashDelCtx *c = p;
    g_free(c->info_path);
    g_free(c->uri);
    g_free(c);
}

static void
on_trash_delete_answer(gboolean accepted, gpointer ud)
{
    TrashDelCtx *c = ud;
    if (!accepted) return;

    GFile *f = g_file_new_for_uri(c->uri);
    GError *err = NULL;
    if (!delete_recursive(f, &err))
        show_error(c->st, "Could not delete", "%s",
            err ? err->message : "Unknown error");
    else if (c->info_path)
        g_unlink(c->info_path);
    g_clear_error(&err);
    g_object_unref(f);
    refresh_view(c->st);
}

static void
trash_delete_forever(GtkWidget *w, gpointer p)
{
    (void)w;
    TrashItem *t = p;

    TrashDelCtx *c = g_new0(TrashDelCtx, 1);
    c->st  = t->state;
    c->uri = g_strdup(t->uri);
    c->info_path = g_strdup(t->info_path);

    char *body = g_strdup_printf(
        "“%s” will be removed from the Recycle Bin for good. "
        "This cannot be undone.", t->name ? t->name : "This item");

    myfm_confirm(t->state->window, "\xE2\x9D\x8C", "Delete permanently?",
        body, "Delete", TRUE,
        on_trash_delete_answer, c, trash_del_ctx_free);

    g_free(body);
}

static void
trash_pressed(GtkGestureClick *gc, int n, double x, double y, gpointer ud)
{
    GtkWidget *w = GTK_WIDGET(ud);
    TrashItem *t = g_object_get_data(G_OBJECT(w), "trash-item");
    if (!t) return;

    guint btn = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gc));
    if (btn != GDK_BUTTON_SECONDARY || n != 1) return;

    AppState *st = t->state;
    GtkWidget *pop = popover_at(st, w, x, y);
    GtkWidget *vb  = popover_content(pop);

    gtk_box_append(GTK_BOX(vb), myfm_menu_row(
        "\xE2\x86\xA9", "Restore", G_CALLBACK(trash_restore), t, pop));
    gtk_box_append(GTK_BOX(vb), myfm_menu_row(
        "\xE2\x9D\x8C", "Delete permanently",
        G_CALLBACK(trash_delete_forever), t, pop));

    gtk_popover_popup(GTK_POPOVER(pop));
}

static void
on_empty_trash_clicked(GtkButton *b, gpointer ud)
{
    (void)b;
    myfm_empty_trash(ud);
}

static void
add_trash_row(AppState *st, GtkWidget *fv, const char *display, gboolean is_dir,
              const char *content_type, const char *orig, const char *uri,
              const char *info_path, guint index)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(row, "file-row");
    gtk_widget_set_margin_start(row, 2);
    gtk_widget_set_margin_end(row, 2);

    GtkWidget *ico = myfm_icon_widget(display, is_dir, content_type, 16);
    gtk_widget_set_size_request(ico, ICON_W, -1);
    gtk_box_append(GTK_BOX(row), ico);

    GtkWidget *l = gtk_label_new(display);
    gtk_widget_add_css_class(l, "file-label");
    gtk_widget_set_hexpand(l, TRUE);
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(row), l);

    GtkWidget *o = gtk_label_new(orig ? orig : "");
    gtk_widget_add_css_class(o, "file-meta");
    gtk_widget_set_size_request(o, 300, -1);
    gtk_label_set_xalign(GTK_LABEL(o), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(o), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_append(GTK_BOX(row), o);

    TrashItem *t = g_new0(TrashItem, 1);
    t->uri       = g_strdup(uri);
    t->orig      = g_strdup(orig);
    t->name      = g_strdup(display);
    t->info_path = g_strdup(info_path);
    t->state     = st;
    g_object_set_data_full(G_OBJECT(row), "trash-item", t, trash_item_free);

    GtkGesture *gc = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gc), 0);
    g_signal_connect(gc, "pressed", G_CALLBACK(trash_pressed), row);
    gtk_widget_add_controller(row, GTK_EVENT_CONTROLLER(gc));

    if (index < MAX_ANIMATED_ROWS)
        anim_row_appear(row, index * 14);
    gtk_box_append(GTK_BOX(fv), row);
}

static void
show_trash(AppState *st)
{
    GtkWidget *fv = st->file_view;

    myfm_cancel_pending_dir_load(st);
    clear_selection(st);
    g_clear_pointer(&st->listed_path, g_free);
    g_clear_pointer(&st->preview_path, g_free);
    anim_view_transition(fv);
    clear_file_view(fv);
    gtk_widget_set_visible(st->column_header, FALSE);

    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_end(head, 10);
    GtkWidget *title = section_title("Recycle Bin");
    gtk_widget_set_hexpand(title, TRUE);
    anim_slide_in_from_left(title);
    gtk_box_append(GTK_BOX(head), title);

    GtkWidget *empty_btn = gtk_button_new_with_label("Empty Recycle Bin");
    gtk_widget_add_css_class(empty_btn, "dlg-btn");
    gtk_widget_set_valign(empty_btn, GTK_ALIGN_CENTER);
    g_signal_connect(empty_btn, "clicked",
        G_CALLBACK(on_empty_trash_clicked), st);
    gtk_box_append(GTK_BOX(head), empty_btn);
    gtk_box_append(GTK_BOX(fv), head);

    guint n = 0;
    gboolean ok = FALSE;

    GFile *trash = g_file_new_for_uri("trash:///");
    GFileEnumerator *en = g_file_enumerate_children(trash,
        "standard::name,standard::display-name,standard::type,"
        "standard::size,standard::fast-content-type,trash::orig-path",
        G_FILE_QUERY_INFO_NONE, NULL, NULL);

    if (en) {
        ok = TRUE;
        GFileInfo *fi;
        while ((fi = g_file_enumerator_next_file(en, NULL, NULL)) != NULL) {
            const char *disp = g_file_info_get_display_name(fi);
            const char *name = g_file_info_get_name(fi);
            /* the child's real URI: names with spaces, '%', '#', … must be
             * escaped, which plain concatenation did not do */
            GFile *child = g_file_get_child(trash, name);
            char *uri = g_file_get_uri(child);
            g_object_unref(child);
            add_trash_row(st, fv, disp ? disp : name,
                g_file_info_get_file_type(fi) == G_FILE_TYPE_DIRECTORY,
                g_file_info_get_attribute_string(fi,
                    G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE),
                g_file_info_get_attribute_byte_string(fi, "trash::orig-path"),
                uri, NULL, n++);
            g_free(uri);
            g_object_unref(fi);
        }
        g_object_unref(en);
    } else {
        /* No GVfs (KDE, Sway, Hyprland… without gvfsd): read the
         * freedesktop.org trash of the home folder ourselves. */
        char *dir   = local_trash_dir();
        char *files = g_build_filename(dir, "files", NULL);
        GDir *d = g_dir_open(files, 0, NULL);
        if (d) {
            ok = TRUE;
            const char *name;
            while ((name = g_dir_read_name(d)) != NULL) {
                char *fpath = g_build_filename(files, name, NULL);
                char *iname = g_strconcat(name, ".trashinfo", NULL);
                char *ipath = g_build_filename(dir, "info", iname, NULL);

                char *orig = NULL;
                GKeyFile *kf = g_key_file_new();
                if (g_key_file_load_from_file(kf, ipath, G_KEY_FILE_NONE, NULL)) {
                    char *raw = g_key_file_get_string(kf, "Trash Info", "Path", NULL);
                    if (raw) {
                        orig = g_uri_unescape_string(raw, NULL);
                        g_free(raw);
                    }
                }
                g_key_file_unref(kf);

                GFile *ff = g_file_new_for_path(fpath);
                char *uri = g_file_get_uri(ff);
                g_object_unref(ff);
                char *disp = orig ? g_path_get_basename(orig)
                                  : g_filename_display_name(name);
                gboolean is_dir = g_file_test(fpath, G_FILE_TEST_IS_DIR) &&
                                  !g_file_test(fpath, G_FILE_TEST_IS_SYMLINK);

                add_trash_row(st, fv, disp, is_dir, NULL, orig, uri,
                    g_file_test(ipath, G_FILE_TEST_EXISTS) ? ipath : NULL, n++);

                g_free(disp);
                g_free(uri);
                g_free(orig);
                g_free(ipath);
                g_free(iname);
                g_free(fpath);
            }
            g_dir_close(d);
        } else if (!g_file_test(dir, G_FILE_TEST_EXISTS)) {
            ok = TRUE;      /* nothing was ever trashed: just empty */
        }
        g_free(files);
        g_free(dir);
    }
    g_object_unref(trash);

    if (!ok) {
        gtk_box_append(GTK_BOX(fv), info_label("Unable to open the Recycle Bin"));
        st->place = PLACE_TRASH;
        set_status_left(st, "Recycle Bin unavailable");
        gtk_widget_set_sensitive(empty_btn, FALSE);
        update_status(st);
        update_nav_buttons(st);
        myfm_update_actions(st);
        return;
    }

    if (n == 0) {
        GtkWidget *l = info_label("The Recycle Bin is empty");
        anim_fade_in(l);
        gtk_box_append(GTK_BOX(fv), l);
        gtk_widget_set_sensitive(empty_btn, FALSE);
    }

    st->place = PLACE_TRASH;
    set_status_left(st, "%u item%s", n, n == 1 ? "" : "s");
    update_status(st);
    update_nav_buttons(st);
    myfm_update_actions(st);
}

static void
on_empty_trash_answer(gboolean accepted, gpointer ud)
{
    AppState *st = ud;
    if (!accepted) return;

    GFile *trash = g_file_new_for_uri("trash:///");
    GFileEnumerator *en = g_file_enumerate_children(trash,
        G_FILE_ATTRIBUTE_STANDARD_NAME,
        G_FILE_QUERY_INFO_NONE, NULL, NULL);
    if (en) {
        GFileInfo *fi;
        while ((fi = g_file_enumerator_next_file(en, NULL, NULL)) != NULL) {
            GFile *child = g_file_get_child(trash, g_file_info_get_name(fi));
            delete_recursive(child, NULL);
            g_object_unref(child);
            g_object_unref(fi);
        }
        g_object_unref(en);
    } else {
        /* no GVfs: empty the home trash directly */
        char *dir = local_trash_dir();
        const char *subs[] = { "files", "info", "directorysizes", NULL };
        for (int i = 0; subs[i]; i++) {
            char *sub = g_build_filename(dir, subs[i], NULL);
            GFile *sf = g_file_new_for_path(sub);
            if (!strcmp(subs[i], "directorysizes")) {
                g_file_delete(sf, NULL, NULL);
            } else {
                GFileEnumerator *le = g_file_enumerate_children(sf,
                    G_FILE_ATTRIBUTE_STANDARD_NAME,
                    G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
                if (le) {
                    GFileInfo *fi;
                    while ((fi = g_file_enumerator_next_file(le, NULL, NULL)) != NULL) {
                        GFile *child = g_file_get_child(sf, g_file_info_get_name(fi));
                        delete_recursive(child, NULL);
                        g_object_unref(child);
                        g_object_unref(fi);
                    }
                    g_object_unref(le);
                }
            }
            g_object_unref(sf);
            g_free(sub);
        }
        g_free(dir);
    }
    g_object_unref(trash);
    refresh_view(st);
}

void
myfm_empty_trash(AppState *st)
{
    myfm_confirm(st->window, "\xF0\x9F\x97\x91", "Empty the Recycle Bin?",
        "Every item in the Recycle Bin will be deleted for good. "
        "This cannot be undone.",
        "Empty", TRUE, on_empty_trash_answer, st, NULL);
}

void
trash_button_clicked(GtkButton *b, gpointer ud)
{
    (void)b;
    AppState *st = ud;

    if (st->directory_monitor) {
        g_file_monitor_cancel(st->directory_monitor);
        g_clear_object(&st->directory_monitor);
    }
    g_clear_pointer(&st->current_path, g_free);
    st->place = PLACE_TRASH;
    update_crumbs_special(st, "\xF0\x9F\x97\x91  Recycle Bin");
    gtk_editable_set_text(GTK_EDITABLE(st->path_entry), "Recycle Bin");
    gtk_window_set_title(GTK_WINDOW(st->window), "Recycle Bin");
    show_trash(st);
}


/* ═══════════════════════════════════════════
 * PUBLIC FILE OPERATIONS
 * ═══════════════════════════════════════════ */

typedef struct {
    AppState *st;
    gboolean  is_folder;
} NewCtx;

/* The name is asked first and only then used to create the item, so nothing
 * is written to disk if the dialog is cancelled. */
static void
on_new_name(const char *text, gpointer ud)
{
    NewCtx *n = ud;
    AppState *st = n->st;

    if (!text || !text[0]) return;
    if (!st->current_path || st->place != PLACE_FOLDER) return;

    if (strchr(text, '/')) {
        show_error(st, "Invalid name",
            "A name cannot contain the “/” character.");
        return;
    }

    char *path = g_build_filename(st->current_path, text, NULL);

    if (g_file_test(path, G_FILE_TEST_EXISTS)) {
        show_error(st, "Name already in use",
            "“%s” already exists in this folder.", text);
        g_free(path);
        return;
    }

    if (n->is_folder) {
        if (g_mkdir_with_parents(path, 0755) != 0)
            show_error(st, "Could not create the folder",
                "%s", g_strerror(errno));
    } else {
        GError *err = NULL;
        if (!g_file_set_contents(path, "", 0, &err)) {
            show_error(st, "Could not create the file", "%s",
                err ? err->message : "Unknown error");
            g_clear_error(&err);
        }
    }

    g_free(path);
    refresh_view(st);
}

void
myfm_new_folder(AppState *st)
{
    if (st->place != PLACE_FOLDER || !st->current_path) return;

    NewCtx *n = g_new0(NewCtx, 1);
    n->st = st;
    n->is_folder = TRUE;

    myfm_prompt(st->window, "\xF0\x9F\x93\x81", "New folder",
        "Choose a name for the new folder.",
        "New folder", "Create", on_new_name, n, g_free);
}

void
myfm_new_file(AppState *st)
{
    if (st->place != PLACE_FOLDER || !st->current_path) return;

    NewCtx *n = g_new0(NewCtx, 1);
    n->st = st;
    n->is_folder = FALSE;

    myfm_prompt(st->window, "\xF0\x9F\x93\x84", "New document",
        "Choose a name for the new document.",
        "New document.txt", "Create", on_new_name, n, g_free);
}

/* Cut / copy / rename / delete are never available for a drive. */
static gboolean
selection_is_file(AppState *st)
{
    return st->selected_path != NULL && st->selected_kind == ITEM_FILE;
}

/* File operations only make sense inside a folder listing.  On "This PC"
 * the Home/Desktop/... tiles are real folders tagged ITEM_FILE, and the
 * keyboard shortcuts (Del, Shift+Del, Ctrl+X, F2) used to act on them,
 * e.g. Shift+Del there permanently deleted ~/Documents. */
static gboolean
file_ops_allowed(AppState *st)
{
    return st->place == PLACE_FOLDER && st->current_path != NULL;
}

static GPtrArray *
selected_file_paths_dup(AppState *st)
{
    GPtrArray *out = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; i < st->sel_items->len; i++) {
        SelItem *s = g_ptr_array_index(st->sel_items, i);
        if (s->kind == ITEM_FILE)
            g_ptr_array_add(out, g_strdup(s->path));
    }
    return out;
}

void
myfm_cut_selected(AppState *st)
{
    if (!file_ops_allowed(st)) return;
    GPtrArray *paths = selected_file_paths_dup(st);
    if (paths->len) set_clipboard(st, paths, TRUE);
    g_ptr_array_free(paths, TRUE);
}

void
myfm_copy_selected(AppState *st)
{
    if (!file_ops_allowed(st)) return;
    GPtrArray *paths = selected_file_paths_dup(st);
    if (paths->len) set_clipboard(st, paths, FALSE);
    g_ptr_array_free(paths, TRUE);
}

void
myfm_copy_path_selected(AppState *st)
{
    if (st->sel_items->len == 0) return;

    GString *g = g_string_new(NULL);
    for (guint i = 0; i < st->sel_items->len; i++) {
        SelItem *s = g_ptr_array_index(st->sel_items, i);
        if (i) g_string_append_c(g, '\n');
        g_string_append(g, s->path);
    }
    GdkClipboard *cb = gtk_widget_get_clipboard(st->window);
    gdk_clipboard_set_text(cb, g->str);
    set_status_left(st, st->sel_items->len == 1
        ? "Path copied to clipboard" : "Paths copied to clipboard");
    g_string_free(g, TRUE);
}

/* Copies (or, for local GFiles that MyFM itself dropped, could move — kept
 * as copy for safety, matching normal drag&drop semantics) a batch of
 * external GFiles into `dest_dir`.  Used both for files dragged in from
 * outside MyFM and for pasting a clipboard filled by another application. */
static void
import_files(AppState *st, GSList *files, const char *dest_dir,
             gboolean is_drop)
{
    if (!dest_dir || !g_file_test(dest_dir, G_FILE_TEST_IS_DIR)) {
        show_error(st, "Cannot paste here",
            "The destination folder is not available.");
        return;
    }

    GFile *destd = g_file_new_for_path(dest_dir);
    guint ok_n = 0, err_n = 0;
    for (GSList *it = files; it; it = it->next) {
        GFile *srcf = G_FILE(it->data);
        char *src_path = g_file_get_path(srcf);   /* NULL for remote files */

        /* Dropping an item onto itself, or back into the folder it
         * already lives in (a short drag that ends on the same view),
         * is a no-op, not an error and not a request for a "(Copy)". */
        if (is_drop) {
            if (g_file_equal(srcf, destd)) { g_free(src_path); continue; }
            GFile *src_parent = g_file_get_parent(srcf);
            gboolean same_dir = src_parent && g_file_equal(src_parent, destd);
            g_clear_object(&src_parent);
            if (same_dir) { g_free(src_path); continue; }
        }

        GFileInfo *si = g_file_query_info(srcf, G_FILE_ATTRIBUTE_STANDARD_TYPE,
            G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
        gboolean src_is_dir = si &&
            g_file_info_get_file_type(si) == G_FILE_TYPE_DIRECTORY;
        g_clear_object(&si);

        if (src_is_dir && src_path && myfm_path_is_inside(dest_dir, src_path)) {
            g_free(src_path);
            err_n++;
            continue;
        }

        char *base = g_file_get_basename(srcf);
        if (!base || !strcmp(base, "/")) {
            g_free(base);
            g_free(src_path);
            err_n++;
            continue;
        }
        char *dest = g_build_filename(dest_dir, base, NULL);
        if (g_file_test(dest, G_FILE_TEST_EXISTS) ||
            g_file_test(dest, G_FILE_TEST_IS_SYMLINK)) {
            g_free(dest);
            dest = unique_copy_child(dest_dir, base, src_is_dir);
        }

        GFile *dstf = g_file_new_for_path(dest);
        GError *err = NULL;
        if (copy_recursive(srcf, dstf, &err)) ok_n++;
        else err_n++;
        g_clear_error(&err);
        g_object_unref(dstf);

        g_free(base);
        g_free(dest);
        g_free(src_path);
    }
    g_object_unref(destd);

    if (err_n)
        show_error(st, "Some items could not be copied",
            "%u of %u item%s could not be copied into this folder.",
            err_n, ok_n + err_n, (ok_n + err_n) == 1 ? "" : "s");
    if (ok_n)
        set_status_left(st, "%u item%s copied", ok_n, ok_n == 1 ? "" : "s");

    if (ok_n || err_n)
        refresh_view(st);
}

/* Paste of files another application put on the clipboard. */
void
myfm_import_paths(AppState *st, GSList *files, const char *dest_dir)
{
    import_files(st, files, dest_dir, FALSE);
}

typedef struct { AppState *st; GtkWidget *win; char *dest; } PasteCtx;

static void
on_external_paste_ready(GObject *source, GAsyncResult *res, gpointer ud)
{
    PasteCtx *ctx = ud;
    GError *err = NULL;
    const GValue *value =
        gdk_clipboard_read_value_finish(GDK_CLIPBOARD(source), res, &err);
    g_clear_error(&err);

    /* The window (and its AppState) may have been closed while the
     * clipboard owner was still sending the data. */
    if (value && ctx->win) {
        GdkFileList *fl = g_value_get_boxed(value);
        GSList *fs = fl ? gdk_file_list_get_files(fl) : NULL;
        if (fs) myfm_import_paths(ctx->st, fs, ctx->dest);
        g_slist_free(fs);
    }

    if (ctx->win)
        g_object_remove_weak_pointer(G_OBJECT(ctx->win), (gpointer *)&ctx->win);
    g_free(ctx->dest);
    g_free(ctx);
}

void
myfm_paste(AppState *st)
{
    if (st->place != PLACE_FOLDER || !st->current_path) return;
    myfm_paste_into(st, st->current_path);
}

void
myfm_paste_into(AppState *st, const char *dest_dir)
{
    if (st->place != PLACE_FOLDER || !st->current_path || !dest_dir) return;

    if (st->clip_paths->len == 0) {
        /* Nothing of ours: try the real system clipboard, so a file
         * copied in another application can be pasted here too. */
        GdkClipboard *cb = gtk_widget_get_clipboard(st->window);
        if (!gdk_content_formats_contain_gtype(gdk_clipboard_get_formats(cb),
                                               GDK_TYPE_FILE_LIST))
            return;
        PasteCtx *ctx = g_new0(PasteCtx, 1);
        ctx->st   = st;
        ctx->win  = st->window;
        ctx->dest = g_strdup(dest_dir);
        g_object_add_weak_pointer(G_OBJECT(ctx->win), (gpointer *)&ctx->win);
        gdk_clipboard_read_value_async(cb, GDK_TYPE_FILE_LIST,
            G_PRIORITY_DEFAULT, NULL, on_external_paste_ready, ctx);
        return;
    }

    guint ok_n = 0, err_n = 0;
    gboolean any_missing = FALSE;
    GPtrArray *moved = st->clip_is_cut ? g_ptr_array_new_with_free_func(g_free) : NULL;

    for (guint i = 0; i < st->clip_paths->len; i++) {
        const char *src_path = g_ptr_array_index(st->clip_paths, i);
        if (!g_file_test(src_path, G_FILE_TEST_EXISTS) &&
            !g_file_test(src_path, G_FILE_TEST_IS_SYMLINK)) {
            any_missing = TRUE;
            continue;
        }

        gboolean src_is_dir = g_file_test(src_path, G_FILE_TEST_IS_DIR) &&
                              !g_file_test(src_path, G_FILE_TEST_IS_SYMLINK);

        if (src_is_dir && myfm_path_is_inside(dest_dir, src_path)) {
            char *base = g_path_get_basename(src_path);
            show_error(st, "Cannot paste here",
                "“%s” cannot be copied into itself or into one of its "
                "own subfolders.", base);
            g_free(base);
            err_n++;
            continue;
        }

        char *base = g_path_get_basename(src_path);
        char *dest = g_build_filename(dest_dir, base, NULL);
        gboolean same_place = (strcmp(dest, src_path) == 0);

        /* Cut + paste into the folder the item already lives in is a
         * no-op (it used to "move" the file to a "(Copy)" name). */
        if (st->clip_is_cut && same_place) {
            g_free(base);
            g_free(dest);
            continue;
        }

        if (g_file_test(dest, G_FILE_TEST_EXISTS) ||
            g_file_test(dest, G_FILE_TEST_IS_SYMLINK)) {
            if (st->clip_is_cut) {
                show_error(st, "Name already in use",
                    "“%s” already exists in this folder.", base);
                g_free(base);
                g_free(dest);
                err_n++;
                continue;
            }
            /* Pasting a copy over itself (or a name clash): always use
             * the "(Copy)" naming scheme, inserted before the extension. */
            g_free(dest);
            dest = unique_copy_child(dest_dir, base, src_is_dir);
        }

        GFile *srcf = g_file_new_for_path(src_path);
        GFile *dstf = g_file_new_for_path(dest);
        GError *err = NULL;
        gboolean ok;

        if (st->clip_is_cut) {
            ok = g_file_move(srcf, dstf, G_FILE_COPY_NOFOLLOW_SYMLINKS,
                    NULL, NULL, NULL, &err);
            /* A folder moved to another filesystem fails with
             * WOULD_RECURSE (not NOT_SUPPORTED): fall back to copy +
             * delete, but only delete when the copy fully succeeded. */
            if (!ok && (g_error_matches(err, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED) ||
                        g_error_matches(err, G_IO_ERROR, G_IO_ERROR_WOULD_RECURSE))) {
                g_clear_error(&err);
                ok = copy_recursive(srcf, dstf, &err);
                /* copied, but the original could not be removed */
                if (ok && !delete_recursive(srcf, NULL))
                    ok = FALSE;
            }
        } else {
            ok = copy_recursive(srcf, dstf, &err);
        }

        if (ok) {
            ok_n++;
            if (moved) g_ptr_array_add(moved, g_strdup(src_path));
        } else {
            err_n++;
        }

        g_clear_error(&err);
        g_object_unref(srcf);
        g_object_unref(dstf);
        g_free(base);
        g_free(dest);
    }

    if (moved) {
        for (guint i = 0; i < moved->len; i++) {
            const char *p = g_ptr_array_index(moved, i);
            for (guint j = 0; j < st->clip_paths->len; j++) {
                if (!strcmp(p, (const char *)g_ptr_array_index(st->clip_paths, j))) {
                    g_ptr_array_remove_index(st->clip_paths, j);
                    break;
                }
            }
        }
        g_ptr_array_free(moved, TRUE);

        /* Everything that was cut has been moved: drop it from the system
         * clipboard as well, or the next paste would try to copy files
         * from their old (now empty) location. */
        if (st->clip_paths->len == 0) {
            GdkClipboard *cb = gtk_widget_get_clipboard(st->window);
            if (st->clip_provider &&
                gdk_clipboard_get_content(cb) == st->clip_provider)
                gdk_clipboard_set_content(cb, NULL);
            g_clear_object(&st->clip_provider);
        }
        myfm_update_actions(st);
    }

    if (any_missing)
        show_error(st, "Nothing to paste",
            "One or more items on the clipboard no longer exist.");
    else if (err_n)
        show_error(st, "Paste failed",
            "%u of %u item%s could not be pasted.", err_n, ok_n + err_n,
            (ok_n + err_n) == 1 ? "" : "s");

    refresh_view(st);
}

typedef struct { AppState *st; GPtrArray *paths; } BulkCtx;

static void
bulk_ctx_free(gpointer p)
{
    BulkCtx *b = p;
    g_ptr_array_free(b->paths, TRUE);
    g_free(b);
}

static void
on_trash_selected_answer(gboolean accepted, gpointer ud)
{
    BulkCtx *b = ud;
    if (!accepted) return;

    guint err_n = 0;
    for (guint i = 0; i < b->paths->len; i++) {
        GFile *f = g_file_new_for_path((const char *)g_ptr_array_index(b->paths, i));
        GError *err = NULL;
        if (!g_file_trash(f, NULL, &err)) err_n++;
        g_clear_error(&err);
        g_object_unref(f);
    }
    if (err_n)
        show_error(b->st, "Could not move everything to the Recycle Bin",
            "%u of %u item%s could not be moved.",
            err_n, b->paths->len, b->paths->len == 1 ? "" : "s");

    myfm_clear_multi_selection(b->st);
    refresh_view(b->st);
}

static void
on_delete_selected_answer(gboolean accepted, gpointer ud)
{
    BulkCtx *b = ud;
    if (!accepted) return;

    guint err_n = 0;
    for (guint i = 0; i < b->paths->len; i++) {
        GFile *f = g_file_new_for_path((const char *)g_ptr_array_index(b->paths, i));
        GError *err = NULL;
        if (!delete_recursive(f, &err)) err_n++;
        g_clear_error(&err);
        g_object_unref(f);
    }
    if (err_n)
        show_error(b->st, "Could not delete everything",
            "%u of %u item%s could not be deleted.",
            err_n, b->paths->len, b->paths->len == 1 ? "" : "s");

    myfm_clear_multi_selection(b->st);
    refresh_view(b->st);
}

void
myfm_rename_selected(AppState *st)
{
    if (file_ops_allowed(st) && selection_is_file(st) && st->sel_items->len == 1)
        rename_dialog(st, st->selected_path);
}

void
myfm_trash_selected(AppState *st)
{
    if (!file_ops_allowed(st)) return;
    GPtrArray *paths = selected_file_paths_dup(st);
    if (paths->len == 0) { g_ptr_array_free(paths, TRUE); return; }

    BulkCtx *b = g_new0(BulkCtx, 1);
    b->st = st;
    b->paths = paths;

    char *body;
    if (paths->len == 1) {
        char *base = g_path_get_basename((const char *)g_ptr_array_index(paths, 0));
        body = g_strdup_printf(
            "“%s” will be moved to the Recycle Bin. "
            "You can restore it from there later.", base);
        g_free(base);
    } else {
        body = g_strdup_printf(
            "%u items will be moved to the Recycle Bin. "
            "You can restore them from there later.", paths->len);
    }

    myfm_confirm(st->window, "\xF0\x9F\x97\x91",
        paths->len == 1 ? "Move to the Recycle Bin?" : "Move items to the Recycle Bin?",
        body, "Move", FALSE, on_trash_selected_answer, b, bulk_ctx_free);
    g_free(body);
}

void
myfm_delete_selected(AppState *st)
{
    if (!file_ops_allowed(st)) return;
    GPtrArray *paths = selected_file_paths_dup(st);
    if (paths->len == 0) { g_ptr_array_free(paths, TRUE); return; }

    BulkCtx *b = g_new0(BulkCtx, 1);
    b->st = st;
    b->paths = paths;

    char *body;
    if (paths->len == 1) {
        const char *p0 = (const char *)g_ptr_array_index(paths, 0);
        char *base = g_path_get_basename(p0);
        gboolean is_dir = g_file_test(p0, G_FILE_TEST_IS_DIR);
        body = g_strdup_printf(
            "“%s” will be deleted for good.%s This cannot be undone.",
            base, is_dir ? " Everything inside it will be removed too." : "");
        g_free(base);
    } else {
        body = g_strdup_printf(
            "%u items will be deleted for good. This cannot be undone.",
            paths->len);
    }

    myfm_confirm(st->window, "\xE2\x9D\x8C",
        paths->len == 1 ? "Delete permanently?" : "Delete items permanently?",
        body, "Delete", TRUE, on_delete_selected_answer, b, bulk_ctx_free);
    g_free(body);
}

void
myfm_eject_selected(AppState *st)
{
    if (st->selected_kind == ITEM_VOLUME && st->selected_removable &&
        st->selected_path)
        eject_confirm(st, st->selected_path);
}

void
myfm_properties_selected(AppState *st)
{
    const char *p = st->selected_path ? st->selected_path : st->current_path;
    if (p) show_properties_dialog(st->window, p);
}

void
myfm_open_selected(AppState *st)
{
    if (!st->selected_path) return;
    if (st->selected_is_dir) navigate_to(st, st->selected_path, TRUE);
    else                     open_file(st, st->selected_path);
}

void
myfm_open_terminal(AppState *st)
{
    if (!st->current_path) return;
    CtxData c = { st->current_path, TRUE, FALSE, st };
    ctx_terminal(NULL, &c);
}


/* ═══════════════════════════════════════════
 * "OPEN WITH"
 * ═══════════════════════════════════════════ */

typedef struct {
    AppState *st;
    char     *path;
    GtkWidget *win;
} OpenWithCtx;

static void
open_with_ctx_free(gpointer p)
{
    OpenWithCtx *o = p;
    g_free(o->path);
    g_free(o);
}

static void
on_open_with_app_clicked(GtkWidget *w, gpointer p)
{
    (void)w;
    GAppInfo *info = g_object_get_data(G_OBJECT(w), "app-info");
    OpenWithCtx *o = p;

    GFile *f = g_file_new_for_path(o->path);
    GList *files = g_list_append(NULL, f);
    GError *err = NULL;

    /* A display launch context hands the new app an xdg-activation
     * token, so on Wayland it is allowed to come to the front. */
    GdkAppLaunchContext *lc =
        gdk_display_get_app_launch_context(gtk_widget_get_display(o->win));
    gboolean launched = g_app_info_launch(info, files,
        G_APP_LAUNCH_CONTEXT(lc), &err);
    g_object_unref(lc);
    if (!launched) {
        show_error(o->st, "Could not open the file", "%s",
            err ? err->message : "Unknown error");
    }
    g_clear_error(&err);
    g_list_free(files);
    g_object_unref(f);

    gtk_window_destroy(GTK_WINDOW(o->win));
}

static gboolean
on_open_with_key(GtkEventControllerKey *c, guint keyval, guint code,
                 GdkModifierType mod, gpointer ud)
{
    (void)c; (void)code; (void)mod;
    if (keyval == GDK_KEY_Escape) {
        gtk_window_destroy(GTK_WINDOW(ud));
        return TRUE;
    }
    return FALSE;
}

static GtkWidget *
open_with_row(GAppInfo *info, OpenWithCtx *o)
{
    GtkWidget *btn = gtk_button_new();
    gtk_widget_add_css_class(btn, "context-item");

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *ic = gtk_image_new_from_gicon(g_app_info_get_icon(info));
    gtk_image_set_pixel_size(GTK_IMAGE(ic), 24);
    gtk_box_append(GTK_BOX(row), ic);

    GtkWidget *lb = gtk_label_new(g_app_info_get_display_name(info));
    gtk_widget_set_halign(lb, GTK_ALIGN_START);
    gtk_widget_set_hexpand(lb, TRUE);
    gtk_box_append(GTK_BOX(row), lb);

    gtk_button_set_child(GTK_BUTTON(btn), row);
    g_object_set_data_full(G_OBJECT(btn), "app-info", g_object_ref(info), g_object_unref);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_open_with_app_clicked), o);
    return btn;
}

/* Shows every application GIO/xdg-desktop-portal knows how to open this
 * file's type with — desktop-environment-agnostic, unlike hard-coding a
 * particular file manager's "open with" mechanism — and launches
 * whichever the person picks. */
void
myfm_open_with_path(AppState *st, const char *path)
{
    char *base = g_path_get_basename(path);
    gboolean is_dir = g_file_test(path, G_FILE_TEST_IS_DIR);

    char *content_type = NULL;
    GFile *gf = g_file_new_for_path(path);
    GFileInfo *fi = g_file_query_info(gf,
        G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
        G_FILE_QUERY_INFO_NONE, NULL, NULL);
    if (fi) {
        const char *ct = g_file_info_get_content_type(fi);
        if (ct) content_type = g_strdup(ct);
        g_object_unref(fi);
    }
    g_object_unref(gf);
    if (!content_type)
        content_type = g_content_type_guess(base, NULL, 0, NULL);

    GList *infos = content_type
        ? g_app_info_get_all_for_type(content_type)
        : NULL;

    GtkWidget *win = gtk_window_new();
    gtk_widget_add_css_class(win, "myfm-dialog");
    gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(win), 380, -1);
    gtk_window_set_title(GTK_WINDOW(win), "Open with");
    myfm_window_titlebar(win, "Open with");
    myfm_window_set_owner(win, st->window);   /* separate, closes with it */

    OpenWithCtx *o = g_new0(OpenWithCtx, 1);
    o->st   = st;
    o->path = g_strdup(path);
    o->win  = win;
    g_object_set_data_full(G_OBJECT(win), "owctx", o, open_with_ctx_free);

    GtkEventController *kc = gtk_event_controller_key_new();
    g_signal_connect(kc, "key-pressed", G_CALLBACK(on_open_with_key), win);
    gtk_widget_add_controller(win, kc);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);
    gtk_widget_set_margin_top(box, 18);
    gtk_widget_set_margin_bottom(box, 16);
    gtk_window_set_child(GTK_WINDOW(win), box);

    GtkWidget *title = gtk_label_new("Open with");
    gtk_widget_add_css_class(title, "dlg-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), title);

    char *sub = g_strdup_printf("Choose an application to open \xE2\x80\x9C%s\xE2\x80\x9D", base);
    GtkWidget *subl = gtk_label_new(sub);
    gtk_widget_add_css_class(subl, "dlg-body");
    gtk_widget_set_halign(subl, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(subl), TRUE);
    gtk_box_append(GTK_BOX(box), subl);
    g_free(sub);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_size_request(scroll, -1, 260);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_margin_top(scroll, 6);
    gtk_box_append(GTK_BOX(box), scroll);

    GtkWidget *list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);

    if (infos) {
        for (GList *it = infos; it; it = it->next)
            gtk_box_append(GTK_BOX(list), open_with_row(G_APP_INFO(it->data), o));
        g_list_free_full(infos, g_object_unref);
    } else {
        GtkWidget *none = gtk_label_new(is_dir
            ? "No applications are registered for folders."
            : "No application is registered for this file type.");
        gtk_widget_add_css_class(none, "info-label");
        gtk_label_set_wrap(GTK_LABEL(none), TRUE);
        gtk_box_append(GTK_BOX(list), none);
    }

    GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btns, GTK_ALIGN_END);
    gtk_widget_set_margin_top(btns, 6);
    GtkWidget *cancel = gtk_button_new_with_label("Cancel");
    gtk_widget_add_css_class(cancel, "dlg-btn");
    g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(gtk_window_destroy), win);
    gtk_box_append(GTK_BOX(btns), cancel);
    gtk_box_append(GTK_BOX(box), btns);

    g_free(base);
    g_free(content_type);

    gtk_window_present(GTK_WINDOW(win));
}

void
myfm_open_with_selected(AppState *st)
{
    if (st->selected_path) myfm_open_with_path(st, st->selected_path);
}


/* ═══════════════════════════════════════════
 * BREADCRUMBS
 * ═══════════════════════════════════════════ */

typedef struct { char *path; AppState *st; } CrumbData;

static void crumb_free(gpointer p, GClosure *c)
{
    (void)c;
    CrumbData *cd = p;
    g_free(cd->path);
    g_free(cd);
}

static void
crumb_clicked(GtkButton *b, gpointer ud)
{
    (void)b;
    CrumbData *cd = ud;
    navigate_to(cd->st, cd->path, TRUE);
}

static void
crumb_this_pc(GtkButton *b, gpointer ud)
{
    (void)b;
    this_pc_button_clicked(NULL, ud);
}

static void
clear_crumbs(AppState *st)
{
    GtkWidget *c;
    while ((c = gtk_widget_get_first_child(st->crumbs)) != NULL)
        gtk_box_remove(GTK_BOX(st->crumbs), c);
}

static void
add_crumb(AppState *st, const char *label, const char *path, gboolean last)
{
    GtkWidget *b = gtk_button_new_with_label(label);
    gtk_widget_add_css_class(b, "crumb-btn");
    if (last) gtk_widget_add_css_class(b, "crumb-btn-last");

    if (path) {
        CrumbData *cd = g_new0(CrumbData, 1);
        cd->path = g_strdup(path);
        cd->st   = st;
        g_signal_connect_data(b, "clicked", G_CALLBACK(crumb_clicked),
            cd, crumb_free, 0);
    } else {
        g_signal_connect(b, "clicked", G_CALLBACK(crumb_this_pc), st);
    }
    gtk_box_append(GTK_BOX(st->crumbs), b);
}

static void
add_crumb_sep(AppState *st)
{
    GtkWidget *l = gtk_label_new("\xE2\x80\xBA");   /* › */
    gtk_widget_add_css_class(l, "crumb-sep");
    gtk_box_append(GTK_BOX(st->crumbs), l);
}

static void
update_crumbs_special(AppState *st, const char *label)
{
    clear_crumbs(st);
    add_crumb(st, "\xF0\x9F\x92\xBB  This PC", NULL, FALSE);
    if (label && strstr(label, "This PC") == NULL) {
        add_crumb_sep(st);
        add_crumb(st, label, NULL, TRUE);
    }
}

static void
update_crumbs(AppState *st, const char *path)
{
    clear_crumbs(st);

    add_crumb(st, "\xF0\x9F\x92\xBB  This PC", NULL, path == NULL);
    if (!path) return;

    const char *home = g_get_home_dir();
    char *rest = NULL;
    char *acc  = NULL;

    if (strcmp(home, "/") != 0 && g_str_has_prefix(path, home) &&
        (path[strlen(home)] == '\0' || path[strlen(home)] == '/')) {
        add_crumb_sep(st);
        gboolean is_home = (strcmp(path, home) == 0);
        add_crumb(st, "\xF0\x9F\x8F\xA0  Home", home, is_home);
        acc  = g_strdup(home);
        rest = g_strdup(path + strlen(home));
    } else {
        acc  = g_strdup("/");
        rest = g_strdup(path);
    }

    char **parts = g_strsplit(rest, "/", -1);
    for (int i = 0; parts[i]; i++) {
        if (!parts[i][0]) continue;
        char *next = g_build_filename(acc, parts[i], NULL);
        g_free(acc);
        acc = next;
        add_crumb_sep(st);
        add_crumb(st, parts[i], acc, parts[i + 1] == NULL);
    }
    g_strfreev(parts);
    g_free(rest);
    g_free(acc);
}


/* ═══════════════════════════════════════════
 * DIRECTORY MONITOR (debounced)
 * ═══════════════════════════════════════════ */

static gboolean
do_refresh(gpointer ud)
{
    AppState *st = ud;
    st->refresh_id = 0;
    st->refresh_first_us = 0;
    refresh_view(st);
    return G_SOURCE_REMOVE;
}

static void
dir_changed(GFileMonitor *m, GFile *f, GFile *of,
            GFileMonitorEvent ev, gpointer ud)
{
    (void)m; (void)f; (void)of;
    AppState *st = ud;

    switch (ev) {
        case G_FILE_MONITOR_EVENT_CREATED:
        case G_FILE_MONITOR_EVENT_DELETED:
        case G_FILE_MONITOR_EVENT_MOVED_IN:
        case G_FILE_MONITOR_EVENT_MOVED_OUT:
        case G_FILE_MONITOR_EVENT_RENAMED:
        case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
            break;
        default:
            return;
    }

    /* Debounced (wait for 400 ms of quiet) but never postponed for more
     * than 2 s: a folder that changes constantly (a download, a build,
     * a log) is re-listed at most every couple of seconds instead of
     * being rebuilt again and again, or never. */
    gint64 now = g_get_monotonic_time();
    if (st->refresh_id) {
        if (now - st->refresh_first_us > 2 * G_USEC_PER_SEC)
            return;                 /* the pending refresh will cover it */
        g_source_remove(st->refresh_id);
    } else {
        st->refresh_first_us = now;
    }
    st->refresh_id = g_timeout_add(400, do_refresh, st);
}

static void
start_monitor(AppState *st, const char *path)
{
    if (st->directory_monitor) {
        g_file_monitor_cancel(st->directory_monitor);
        g_clear_object(&st->directory_monitor);
    }
    GFile *d = g_file_new_for_path(path);
    st->directory_monitor = g_file_monitor_directory(
        d, G_FILE_MONITOR_WATCH_MOVES, NULL, NULL);
    if (st->directory_monitor)
        g_signal_connect(st->directory_monitor, "changed",
            G_CALLBACK(dir_changed), st);
    g_object_unref(d);
}


/* ═══════════════════════════════════════════
 * NAVIGATION
 * ═══════════════════════════════════════════ */

static void
update_nav_buttons(AppState *st)
{
    /* From This PC / Recycle Bin, Back returns to the last folder. */
    gboolean special = (st->place != PLACE_FOLDER);
    gtk_widget_set_sensitive(st->btn_back,
        st->history_position > 0 || (special && st->history_position >= 0));
    gtk_widget_set_sensitive(st->btn_forward,
        st->history_position < (int)st->history->len - 1);
    gtk_widget_set_sensitive(st->btn_up,
        st->current_path && strcmp(st->current_path, "/") != 0);
}

void
navigate_to(AppState *st, const char *path, gboolean add_history)
{
    if (!path) return;

    if (!g_file_test(path, G_FILE_TEST_IS_DIR)) {
        show_error(st, "Location unavailable",
            "“%s” does not exist or is not a folder.", path);
        return;
    }

    st->place = PLACE_FOLDER;
    char *dup = g_strdup(path);     /* path may point into the history */
    g_free(st->current_path);
    st->current_path = dup;

    show_directory(st, st->current_path);
    update_crumbs(st, st->current_path);
    gtk_editable_set_text(GTK_EDITABLE(st->path_entry), st->current_path);
    start_monitor(st, st->current_path);

    char *base = g_path_get_basename(st->current_path);
    char *title = g_strdup_printf("%s",   /* Windows 10: just the folder */
        strcmp(st->current_path, g_get_home_dir()) == 0 ? "Home" : base);
    gtk_window_set_title(GTK_WINDOW(st->window), title);
    g_free(title);
    g_free(base);

    if (add_history) {
        const char *cur = (st->history_position >= 0 &&
                           st->history_position < (int)st->history->len)
            ? g_ptr_array_index(st->history, st->history_position) : NULL;

        if (!cur || strcmp(cur, st->current_path) != 0) {
            while ((int)st->history->len - 1 > st->history_position)
                g_ptr_array_remove_index(st->history, st->history->len - 1);
            g_ptr_array_add(st->history, g_strdup(st->current_path));
            st->history_position = (int)st->history->len - 1;
        }
    }
    update_nav_buttons(st);
}

void
address_activate(GtkEntry *entry, gpointer ud)
{
    AppState *st = ud;
    const char *raw = gtk_editable_get_text(GTK_EDITABLE(entry));
    if (!raw || !raw[0]) return;

    /* The entry shows "This PC" / "Recycle Bin" while those are open;
     * pressing Enter on them must not report a missing folder. */
    if (!g_ascii_strcasecmp(raw, "This PC")) {
        this_pc_button_clicked(NULL, st);
        return;
    }
    if (!g_ascii_strcasecmp(raw, "Recycle Bin") || !strcmp(raw, "trash:///")) {
        trash_button_clicked(NULL, st);
        return;
    }

    char *exp;
    if (g_str_has_prefix(raw, "~/"))
        exp = g_build_filename(g_get_home_dir(), raw + 2, NULL);
    else if (!strcmp(raw, "~"))
        exp = g_strdup(g_get_home_dir());
    else
        exp = g_strdup(raw);

    char *can = g_canonicalize_filename(exp, st->current_path);
    g_free(exp);

    /* typing the path of a file opens it instead of failing */
    if (g_file_test(can, G_FILE_TEST_EXISTS) &&
        !g_file_test(can, G_FILE_TEST_IS_DIR))
        open_file(st, can);
    else
        navigate_to(st, can, TRUE);

    g_free(can);
}

void
go_back(GtkButton *b, gpointer ud)
{
    (void)b;
    AppState *st = ud;
    int target = st->history_position - 1;

    /* On This PC / Recycle Bin the current history entry is the folder
     * we came from, so Back goes there rather than one step further. */
    if (st->place != PLACE_FOLDER) target = st->history_position;
    if (target < 0 || target >= (int)st->history->len) return;

    /* Only move in history once the folder could actually be opened,
     * otherwise a deleted folder left the position out of sync with
     * what is on screen. */
    const char *p = g_ptr_array_index(st->history, target);
    if (!g_file_test(p, G_FILE_TEST_IS_DIR)) {
        show_error(st, "Location unavailable",
            "“%s” does not exist or is not a folder.", p);
        return;
    }
    st->history_position = target;
    navigate_to(st, p, FALSE);
    update_nav_buttons(st);
}

void
go_forward(GtkButton *b, gpointer ud)
{
    (void)b;
    AppState *st = ud;
    if (st->history_position >= (int)st->history->len - 1) return;

    const char *p = g_ptr_array_index(st->history, st->history_position + 1);
    if (!g_file_test(p, G_FILE_TEST_IS_DIR)) {
        show_error(st, "Location unavailable",
            "“%s” does not exist or is not a folder.", p);
        return;
    }
    st->history_position++;
    navigate_to(st, p, FALSE);
    update_nav_buttons(st);
}

void
go_up(GtkButton *b, gpointer ud)
{
    (void)b;
    AppState *st = ud;
    if (!st->current_path) return;
    char *p = g_path_get_dirname(st->current_path);
    if (strcmp(p, st->current_path) != 0) navigate_to(st, p, TRUE);
    g_free(p);
}

void
directory_button_clicked(GtkButton *btn, gpointer ud)
{
    AppState *st = ud;
    const char *p = g_object_get_data(G_OBJECT(btn), "directory-path");
    if (p) navigate_to(st, p, TRUE);
}

GtkWidget *
create_directory_button(const char *icon_name, const char *emoji,
                        const char *label, const char *path, AppState *st)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    GtkWidget *ic = myfm_named_icon_widget(icon_name, emoji, 16);
    gtk_widget_add_css_class(ic, "sidebar-icon");
    gtk_box_append(GTK_BOX(row), ic);

    GtkWidget *lb = gtk_label_new(label);
    gtk_widget_set_halign(lb, GTK_ALIGN_START);
    gtk_widget_set_hexpand(lb, TRUE);
    gtk_box_append(GTK_BOX(row), lb);

    GtkWidget *btn = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(btn), row);
    gtk_widget_add_css_class(btn, "sidebar-btn");
    gtk_widget_set_focus_on_click(btn, FALSE);

    if (path)
        g_object_set_data_full(G_OBJECT(btn), "directory-path",
            g_strdup(path), g_free);
    g_signal_connect(btn, "clicked",
        G_CALLBACK(directory_button_clicked), st);
    return btn;
}


/* ═══════════════════════════════════════════
 * DRIVE HOT-PLUG
 * ═══════════════════════════════════════════ */

static gboolean
do_volume_refresh(gpointer ud)
{
    AppState *st = ud;
    st->volume_refresh_id = 0;
    if (st->place == PLACE_THIS_PC)
        myfm_show_this_pc(st);
    return G_SOURCE_REMOVE;
}

/* Plugging a disk fires a burst of volume/mount signals (often a dozen
 * within a second, plus "mount-changed" whenever a mount's state is
 * polled).  Rebuilding This PC — which also statvfs()es every drive —
 * for each one was needless churn; coalesce them into one refresh. */
static void
on_volume_monitor_changed(GVolumeMonitor *mon, gpointer obj, gpointer ud)
{
    (void)mon; (void)obj;
    AppState *st = ud;
    if (st->place != PLACE_THIS_PC) return;
    if (st->volume_refresh_id) g_source_remove(st->volume_refresh_id);
    st->volume_refresh_id = g_timeout_add(500, do_volume_refresh, st);
}

/* A drive was unmounted / unplugged: if this window was showing a folder
 * on it, the listing is now stale and every action would fail, so fall
 * back to This PC (Explorer does the same). */
static void
on_mount_removed(GVolumeMonitor *mon, GMount *mount, gpointer ud)
{
    AppState *st = ud;
    if (st->place == PLACE_FOLDER && st->current_path) {
        GFile *root = g_mount_get_root(mount);
        char *mp = root ? g_file_get_path(root) : NULL;
        g_clear_object(&root);
        gboolean inside = mp && strcmp(mp, "/") != 0 &&
                          myfm_path_is_inside(st->current_path, mp);
        g_free(mp);
        if (inside) {
            myfm_show_this_pc(st);
            return;
        }
    }
    on_volume_monitor_changed(mon, mount, ud);
}

void
myfm_watch_volumes(AppState *st)
{
    st->vol_monitor = g_volume_monitor_get();
    st->vol_sig[0] = g_signal_connect(st->vol_monitor, "mount-added",
        G_CALLBACK(on_volume_monitor_changed), st);
    st->vol_sig[1] = g_signal_connect(st->vol_monitor, "mount-removed",
        G_CALLBACK(on_mount_removed), st);
    st->vol_sig[2] = g_signal_connect(st->vol_monitor, "mount-changed",
        G_CALLBACK(on_volume_monitor_changed), st);
    st->vol_sig[3] = g_signal_connect(st->vol_monitor, "volume-added",
        G_CALLBACK(on_volume_monitor_changed), st);
    st->vol_sig[4] = g_signal_connect(st->vol_monitor, "volume-removed",
        G_CALLBACK(on_volume_monitor_changed), st);
}


/* ═══════════════════════════════════════════
 * CLEANUP
 * ═══════════════════════════════════════════ */

void
app_state_free(AppState *st)
{
    if (!st) return;
    if (st->refresh_id) g_source_remove(st->refresh_id);
    if (st->volume_refresh_id) g_source_remove(st->volume_refresh_id);
    if (st->directory_monitor) {
        g_file_monitor_cancel(st->directory_monitor);
        g_clear_object(&st->directory_monitor);
    }
    if (st->dir_cancellable) {
        g_cancellable_cancel(st->dir_cancellable);
        g_clear_object(&st->dir_cancellable);
    }
    if (st->vol_monitor) {
        for (guint i = 0; i < G_N_ELEMENTS(st->vol_sig); i++)
            if (st->vol_sig[i])
                g_signal_handler_disconnect(st->vol_monitor, st->vol_sig[i]);
        g_object_unref(st->vol_monitor);
    }
    if (st->history)    g_ptr_array_unref(st->history);
    if (st->sel_items)  g_ptr_array_unref(st->sel_items);
    if (st->clip_paths) g_ptr_array_unref(st->clip_paths);
    if (st->marquee_preserve) g_ptr_array_unref(st->marquee_preserve);
    g_clear_object(&st->clip_provider);
    g_free(st->listed_path);
    g_free(st->preview_path);
    g_free(st->current_path);
    g_free(st->selected_path);
    g_free(st->filter_text);
    g_free(st);
}

/* ═══════════════════════════════════════════
 * ENTRY POINT (Wayland only)
 * ═══════════════════════════════════════════ */
static GLogWriterOutput
log_writer(GLogLevelFlags level, const GLogField *fields, gsize n_fields,
           gpointer ud)
{
    if (level & G_LOG_LEVEL_WARNING) {
        const char *domain = NULL, *message = NULL;
        for (gsize i = 0; i < n_fields; i++) {
            if (!strcmp(fields[i].key, "GLIB_DOMAIN"))
                domain = fields[i].value;
            else if (!strcmp(fields[i].key, "MESSAGE"))
                message = fields[i].value;
        }
        if (domain && !strcmp(domain, "Gdk") && message &&
            strstr(message, "gdk_frame_timings_presented"))
            return G_LOG_WRITER_HANDLED;
    }
    return g_log_writer_default(level, fields, n_fields, ud);
}

static void
on_activate(GApplication *app, gpointer user_data)
{
    (void)user_data;
    myfm_window_new(GTK_APPLICATION(app), NULL);
}

static void
on_open(GApplication *app, GFile **files, int n_files,
        const char *hint, gpointer user_data)
{
    (void)hint;
    (void)user_data;

    for (int i = 0; i < n_files; i++) {
        char *path = g_file_get_path(files[i]);
        /* "myfm some/file.txt" opens the folder that contains it */
        if (path && !g_file_test(path, G_FILE_TEST_IS_DIR)) {
            char *dir = g_path_get_dirname(path);
            g_free(path);
            path = dir;
        }
        myfm_window_new(GTK_APPLICATION(app), path);
        g_free(path);
    }
}

int
main(int argc, char **argv)
{
    /* MyFM speaks Wayland only. gdk_set_allowed_backends() overrides
     * GDK_BACKEND and any other hint, so there is no silent fallback to
     * X11/XWayland on a machine that happens to have it, and nothing
     * here assumes a particular desktop environment: the Wayland core
     * protocol, xdg-desktop-portal (file opening) and GVolumeMonitor
     * (drives) are all compositor/DE-agnostic. */
    gdk_set_allowed_backends("wayland");

    /* GTK's Wayland frame clock prints "Duplicate call to
     * gdk_frame_timings_presented() with different values" when the
     * compositor's presentation-time feedback for a frame differs from
     * what GTK recorded (common on Mutter).  It is a GTK diagnostic, not
     * an error in this program, and nothing breaks; it only shows up more
     * the more often the window redraws.  We redraw much less now, and
     * keep just that one message out of the terminal. */
    g_log_set_writer_func(log_writer, NULL, NULL);

    if (!gtk_init_check()) {
        g_printerr(
            "MyFM richiede una sessione Wayland attiva.\n"
            "Nessun compositor Wayland raggiungibile: verifica che la "
            "variabile d'ambiente WAYLAND_DISPLAY sia impostata e che il "
            "compositor sia in esecuzione. MyFM non supporta X11/XWayland.\n");
        return 1;
    }

    GtkApplication *app = gtk_application_new(
        "com.myfm.FileManager",
        G_APPLICATION_HANDLES_OPEN);

    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    g_signal_connect(app, "open",     G_CALLBACK(on_open),     NULL);

    myfm_settings();            /* load ~/.config/explorer/settings.ini */
    myfm_gui_apply_theme();

    int status = g_application_run(G_APPLICATION(app), argc, argv);

    g_object_unref(app);
    return status;
}
