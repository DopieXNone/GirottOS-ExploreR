/* navigation.c – directory listing, file operations, This PC, Trash */

#include "myfm.h"

#include <glib/gstdio.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

/* Column widths of the "details" (list) view */
#define ICON_W  22
#define DATE_W 150
#define TYPE_W 120
#define SIZE_W  90

/* Rows past this index appear instantly: keeps huge folders fluid. */
#define MAX_ANIMATED_ROWS 48

static void show_directory(AppState *st, const char *path);
static void show_this_pc(AppState *st);
static void show_trash(AppState *st);
static void update_crumbs(AppState *st, const char *path);
static void update_crumbs_special(AppState *st, const char *label);
static void update_status(AppState *st);
static void clear_selection(AppState *st);
static void rename_dialog(AppState *st, const char *path);


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
static gboolean
path_is_inside(const char *child, const char *parent)
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
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

    GtkWidget *ic = gtk_label_new(icon ? icon : " ");
    gtk_widget_add_css_class(ic, "ctx-icon");
    gtk_widget_set_valign(ic, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(row), ic);

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
    GtkWidget *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_set_margin_start(vb, 4);
    gtk_widget_set_margin_end(vb, 4);
    gtk_widget_set_margin_top(vb, 4);
    gtk_widget_set_margin_bottom(vb, 4);
    gtk_popover_set_child(GTK_POPOVER(pop), vb);
    return vb;
}


/* ═══════════════════════════════════════════
 * SELECTION & ACTION STATE
 * ═══════════════════════════════════════════ */

void
myfm_update_actions(AppState *st)
{
    gboolean in_folder = (st->place == PLACE_FOLDER && st->current_path != NULL);
    gboolean file_sel  = (st->selected_kind == ITEM_FILE && st->selected_path);
    gboolean vol_sel   = (st->selected_kind == ITEM_VOLUME);

    /* Volumes and partitions never expose file operations. */
    if (st->cmd_new)
        gtk_widget_set_sensitive(st->cmd_new, in_folder);
    if (st->cmd_cut)
        gtk_widget_set_sensitive(st->cmd_cut, in_folder && file_sel);
    if (st->cmd_copy)
        gtk_widget_set_sensitive(st->cmd_copy, in_folder && file_sel);
    if (st->cmd_rename)
        gtk_widget_set_sensitive(st->cmd_rename, in_folder && file_sel);
    if (st->cmd_trash)
        gtk_widget_set_sensitive(st->cmd_trash, in_folder && file_sel);
    if (st->cmd_paste)
        gtk_widget_set_sensitive(st->cmd_paste, in_folder && st->clip_path != NULL);
    if (st->cmd_props)
        gtk_widget_set_sensitive(st->cmd_props,
            st->selected_path != NULL || in_folder);
    if (st->cmd_eject)
        gtk_widget_set_visible(st->cmd_eject, vol_sel && st->selected_removable);
}

static void
clear_selection(AppState *st)
{
    if (st->selected_widget && GTK_IS_WIDGET(st->selected_widget))
        gtk_widget_remove_css_class(st->selected_widget, "item-selected");
    st->selected_widget    = NULL;
    st->selected_kind      = ITEM_NONE;
    st->selected_removable = FALSE;
    st->selected_is_dir    = FALSE;
    g_clear_pointer(&st->selected_path, g_free);
}

static void
update_status(AppState *st)
{
    if (!st->status_right) return;

    if (st->selected_path) {
        char *base = g_path_get_basename(st->selected_path);
        char *txt  = g_strdup_printf("%s selected   |   %s",
            st->selected_kind == ITEM_VOLUME ? "1 drive" : "1 item", base);
        gtk_label_set_text(GTK_LABEL(st->status_right), txt);
        g_free(txt);
        g_free(base);
    } else {
        gtk_label_set_text(GTK_LABEL(st->status_right), "");
    }
}

static void
select_item(AppState *st, GtkWidget *w, const char *path,
            gboolean is_dir, ItemKind kind, gboolean removable)
{
    clear_selection(st);
    st->selected_widget    = w;
    st->selected_path      = g_strdup(path);
    st->selected_is_dir    = is_dir;
    st->selected_kind      = kind;
    st->selected_removable = removable;
    gtk_widget_add_css_class(w, "item-selected");
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

static char *
unique_child(const char *dir, const char *base_name)
{
    char *candidate = g_build_filename(dir, base_name, NULL);
    int n = 2;
    while (g_file_test(candidate, G_FILE_TEST_EXISTS)) {
        g_free(candidate);
        char *tmp = g_strdup_printf("%s (%d)", base_name, n++);
        candidate = g_build_filename(dir, tmp, NULL);
        g_free(tmp);
    }
    return candidate;
}

static void
set_clipboard(AppState *st, const char *path, gboolean cut)
{
    g_free(st->clip_path);
    st->clip_path   = g_strdup(path);
    st->clip_is_cut = cut;

    GdkClipboard *cb = gtk_widget_get_clipboard(st->window);
    gdk_clipboard_set_text(cb, path);

    set_status_left(st, "%s: %s", cut ? "Cut" : "Copied", path);
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


/* ── move to trash ── */

static void
on_trash_answer(gboolean accepted, gpointer ud)
{
    PathCtx *pc = ud;
    if (!accepted) return;

    GFile  *f   = g_file_new_for_path(pc->path);
    GError *err = NULL;
    if (!g_file_trash(f, NULL, &err)) {
        show_error(pc->st, "Could not move to the Recycle Bin", "%s",
            err ? err->message : "Unknown error");
    }
    g_clear_error(&err);
    g_object_unref(f);

    clear_selection(pc->st);
    refresh_view(pc->st);
}

static void
trash_confirm(AppState *st, const char *path)
{
    char *base = g_path_get_basename(path);
    char *body = g_strdup_printf(
        "“%s” will be moved to the Recycle Bin. "
        "You can restore it from there later.", base);

    myfm_confirm(st->window, "\xF0\x9F\x97\x91", "Move to the Recycle Bin?",
        body, "Move", FALSE,
        on_trash_answer, path_ctx_new(st, path), path_ctx_free);

    g_free(base);
    g_free(body);
}


/* ── permanent delete ── */

static void
on_delete_answer(gboolean accepted, gpointer ud)
{
    PathCtx *pc = ud;
    if (!accepted) return;

    GFile  *f   = g_file_new_for_path(pc->path);
    GError *err = NULL;
    if (!delete_recursive(f, &err))
        show_error(pc->st, "Could not delete", "%s",
            err ? err->message : "Unknown error");
    g_clear_error(&err);
    g_object_unref(f);

    clear_selection(pc->st);
    refresh_view(pc->st);
}

static void
delete_confirm(AppState *st, const char *path)
{
    char *base = g_path_get_basename(path);
    gboolean is_dir = g_file_test(path, G_FILE_TEST_IS_DIR);
    char *body = g_strdup_printf(
        "“%s” will be deleted for good.%s This cannot be undone.",
        base, is_dir ? " Everything inside it will be removed too." : "");

    myfm_confirm(st->window, "\xE2\x9D\x8C", "Delete permanently?",
        body, "Delete", TRUE,
        on_delete_answer, path_ctx_new(st, path), path_ctx_free);

    g_free(base);
    g_free(body);
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

    /* Wayland-native terminals first, no xterm / X11 fallback. */
    static const char *terms[] = {
        "ptyxis", "kgx", "gnome-terminal", "konsole", "foot",
        "alacritty", "kitty", "wezterm", "xfce4-terminal",
        "mate-terminal", "tilix", NULL
    };

    for (int i = 0; terms[i]; i++) {
        char *exe = g_find_program_in_path(terms[i]);
        if (!exe) continue;

        char *argv[4];
        argv[0] = exe;
        argv[1] = (char *)"--working-directory";
        argv[2] = dir;
        argv[3] = NULL;

        GError *err = NULL;
        gboolean ok = g_spawn_async(NULL, argv, NULL,
            G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
            NULL, NULL, NULL, &err);
        g_free(exe);
        if (err) g_error_free(err);
        if (ok) { g_free(dir); return; }
    }

    show_error(c->state, "No terminal found",
        "Install a terminal emulator to use this action.");
    g_free(dir);
}

static void ctx_cut(GtkWidget *w, gpointer p)
{ (void)w; CtxData *c = p; set_clipboard(c->state, c->path, TRUE); }

static void ctx_copy(GtkWidget *w, gpointer p)
{ (void)w; CtxData *c = p; set_clipboard(c->state, c->path, FALSE); }

static void ctx_copy_path(GtkWidget *w, gpointer p)
{
    (void)w;
    CtxData *c = p;
    GdkClipboard *cb = gtk_widget_get_clipboard(c->state->window);
    gdk_clipboard_set_text(cb, c->path);
    set_status_left(c->state, "Path copied to clipboard");
}

static void ctx_rename(GtkWidget *w, gpointer p)
{ (void)w; CtxData *c = p; rename_dialog(c->state, c->path); }

static void ctx_trash(GtkWidget *w, gpointer p)
{ (void)w; CtxData *c = p; trash_confirm(c->state, c->path); }

static void ctx_delete(GtkWidget *w, gpointer p)
{ (void)w; CtxData *c = p; delete_confirm(c->state, c->path); }

static void ctx_eject(GtkWidget *w, gpointer p)
{ (void)w; CtxData *c = p; eject_confirm(c->state, c->path); }

static void ctx_properties(GtkWidget *w, gpointer p)
{
    (void)w;
    CtxData *c = p;
    show_properties_dialog(c->state->window, c->path);
}

static void ctx_paste(GtkWidget *w, gpointer p)
{ (void)w; CtxData *c = p; myfm_paste(c->state); }

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

static void
entry_pressed(GtkGestureClick *gc, int n, double x, double y, gpointer ud)
{
    GtkWidget *w = GTK_WIDGET(ud);
    EntryData *e = g_object_get_data(G_OBJECT(w), "edata");
    if (!e) return;

    guint btn = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gc));

    if (btn == GDK_BUTTON_PRIMARY) {
        if (n == 1) {
            select_item(e->state, w, e->path, e->is_dir, e->kind, e->removable);
        } else if (n == 2) {
            if (e->is_dir) navigate_to(e->state, e->path, TRUE);
            else           open_file(e->state, e->path);
        }
    } else if (btn == GDK_BUTTON_SECONDARY && n == 1) {
        select_item(e->state, w, e->path, e->is_dir, e->kind, e->removable);
        if (e->kind == ITEM_VOLUME)
            show_volume_menu(e->state, w, x, y, e->path, e->removable);
        else
            show_item_menu(e->state, w, x, y, e->path, e->is_dir);
    }
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
}

static void
attach_edata(GtkWidget *w, const char *path, gboolean is_dir, AppState *st)
{
    attach_edata_full(w, path, is_dir, ITEM_FILE, FALSE, st);
}

static void
background_pressed(GtkGestureClick *gc, int n, double x, double y, gpointer ud)
{
    AppState *st = ud;
    guint btn = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gc));

    if (btn == GDK_BUTTON_PRIMARY && n == 1) {
        clear_selection(st);
        update_status(st);
        myfm_update_actions(st);
    } else if (btn == GDK_BUTTON_SECONDARY && n == 1) {
        clear_selection(st);
        update_status(st);
        myfm_update_actions(st);
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
} Entry;

static void
entry_free(gpointer p)
{
    Entry *e = p;
    g_free(e->name);
    g_free(e->path);
    g_free(e->type_desc);
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

static GtkWidget *
build_list_row(const Entry *e, AppState *st)
{
    gboolean root = needs_root(e->path);

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(row, "file-row");
    gtk_widget_set_hexpand(row, TRUE);
    gtk_widget_set_margin_start(row, 6);
    gtk_widget_set_margin_end(row, 6);

    GtkWidget *icon = gtk_label_new(file_icon_for(e->name, e->is_dir));
    gtk_widget_set_size_request(icon, ICON_W, -1);
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

    GtkWidget *tile = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(tile, "grid-tile");
    gtk_widget_set_size_request(tile, 104, 96);
    gtk_widget_set_halign(tile, GTK_ALIGN_CENTER);

    GtkWidget *icon = gtk_label_new(file_icon_for(e->name, e->is_dir));
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
    gtk_label_set_max_width_chars(GTK_LABEL(lbl), 13);
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
    gtk_widget_set_margin_start(l, 20);
    gtk_widget_set_margin_top(l, 24);
    return l;
}


/* ═══════════════════════════════════════════
 * SHOW DIRECTORY
 * ═══════════════════════════════════════════ */

static void
show_directory(AppState *st, const char *path)
{
    GtkWidget *fv = st->file_view;

    clear_selection(st);
    anim_view_transition(fv);
    clear_file_view(fv);
    gtk_widget_set_visible(st->column_header, st->view_mode == VIEW_LIST);
    myfm_update_columns(st);

    GFile  *dirf = g_file_new_for_path(path);
    GError *err  = NULL;
    GFileEnumerator *en = g_file_enumerate_children(dirf,
        "standard::name,standard::display-name,standard::type,"
        "standard::size,standard::content-type,standard::is-hidden,"
        "time::modified",
        G_FILE_QUERY_INFO_NONE, NULL, &err);
    g_object_unref(dirf);

    if (!en) {
        gtk_widget_set_visible(st->column_header, FALSE);
        char *m = g_strdup_printf("Unable to open this folder\n%s",
            err ? err->message : "");
        gtk_box_append(GTK_BOX(fv), info_label(m));
        g_free(m);
        g_clear_error(&err);
        set_status_left(st, "Access denied");
        myfm_update_actions(st);
        return;
    }

    GPtrArray *items = g_ptr_array_new_with_free_func(entry_free);
    GFileInfo *fi;
    while ((fi = g_file_enumerator_next_file(en, NULL, NULL)) != NULL) {
        const char *nm = g_file_info_get_name(fi);
        if (!nm) { g_object_unref(fi); continue; }

        Entry *e = g_new0(Entry, 1);
        const char *disp = g_file_info_get_display_name(fi);
        e->name      = g_strdup(disp ? disp : nm);
        e->path      = g_build_filename(path, nm, NULL);
        e->is_dir    = g_file_info_get_file_type(fi) == G_FILE_TYPE_DIRECTORY;
        e->is_hidden = g_file_info_get_is_hidden(fi) || nm[0] == '.';
        e->size      = (guint64)g_file_info_get_size(fi);

        GDateTime *dt = g_file_info_get_modification_date_time(fi);
        e->mtime = dt ? g_date_time_to_unix(dt) : 0;
        if (dt) g_date_time_unref(dt);

        if (e->is_dir) {
            e->type_desc = g_strdup("File folder");
        } else {
            const char *ct = g_file_info_get_content_type(fi);
            char *d = ct ? g_content_type_get_description(ct) : NULL;
            e->type_desc = d ? d : g_strdup("File");
        }

        g_ptr_array_add(items, e);
        g_object_unref(fi);
    }
    g_object_unref(en);

    g_ptr_array_sort_with_data(items, entry_cmp, st);

    guint shown = 0, folders = 0;

    GtkWidget *flow = NULL;
    if (st->view_mode == VIEW_GRID) {
        flow = gtk_flow_box_new();
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
        gtk_box_append(GTK_BOX(fv), flow);
    }

    for (guint i = 0; i < items->len; i++) {
        Entry *e = g_ptr_array_index(items, i);

        if (e->is_hidden && !st->show_hidden) continue;
        if (st->filter_text && st->filter_text[0]) {
            char *lname = g_utf8_strdown(e->name, -1);
            gboolean match = strstr(lname, st->filter_text) != NULL;
            g_free(lname);
            if (!match) continue;
        }

        GtkWidget *w = (st->view_mode == VIEW_GRID)
            ? build_grid_tile(e, st)
            : build_list_row(e, st);

        if (shown < MAX_ANIMATED_ROWS)
            anim_row_appear(w, shown * 14);

        if (st->view_mode == VIEW_GRID)
            gtk_flow_box_append(GTK_FLOW_BOX(flow), w);
        else
            gtk_box_append(GTK_BOX(fv), w);

        shown++;
        if (e->is_dir) folders++;
    }

    if (shown == 0) {
        GtkWidget *l = info_label(
            (st->filter_text && st->filter_text[0])
                ? "No items match your search"
                : "This folder is empty");
        anim_fade_in(l);
        gtk_box_append(GTK_BOX(fv), l);
    }

    set_status_left(st, "%u item%s   |   %u folder%s",
        shown, shown == 1 ? "" : "s",
        folders, folders == 1 ? "" : "s");
    update_status(st);
    myfm_update_actions(st);

    g_ptr_array_unref(items);
}


/* ═══════════════════════════════════════════
 * VIEW OPTIONS
 * ═══════════════════════════════════════════ */

void
set_view_mode(AppState *st, ViewMode mode)
{
    st->view_mode = mode;

    if (mode == VIEW_LIST) {
        gtk_widget_add_css_class(st->view_toggle_list, "view-btn-active");
        gtk_widget_remove_css_class(st->view_toggle_grid, "view-btn-active");
    } else {
        gtk_widget_add_css_class(st->view_toggle_grid, "view-btn-active");
        gtk_widget_remove_css_class(st->view_toggle_list, "view-btn-active");
    }
    refresh_view(st);
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
    st->show_hidden = !st->show_hidden;
    refresh_view(st);
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
        case PLACE_THIS_PC: show_this_pc(st); break;
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

        gboolean dup = FALSE;
        for (guint i = 0; i < arr->len; i++) {
            PMount *o = g_ptr_array_index(arr, i);
            if (!strcmp(o->mountpoint, mount)) { dup = TRUE; break; }
        }
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
static GtkWidget *
make_drive_tile(AppState *st, const char *icon_str, const char *label,
                const char *mountpoint, const char *device,
                gboolean optical, gboolean removable)
{
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_widget_add_css_class(card, "drive-tile");
    if (optical) gtk_widget_add_css_class(card, "drive-tile-optical");
    gtk_widget_set_size_request(card, 300, -1);

    GtkWidget *ico = gtk_label_new(icon_str);
    gtk_widget_add_css_class(ico, "drive-icon");
    gtk_widget_set_valign(ico, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(card), ico);

    GtkWidget *info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_hexpand(info, TRUE);
    gtk_widget_set_valign(info, GTK_ALIGN_CENTER);

    GtkWidget *nl = gtk_label_new(label);
    gtk_widget_add_css_class(nl, "drive-name");
    gtk_widget_set_halign(nl, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(nl), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(nl), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(info), nl);

    gboolean have_usage = FALSE;
    if (mountpoint) {
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
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    gtk_widget_set_margin_start(l, 18);
    gtk_widget_set_margin_top(l, 16);
    gtk_widget_set_margin_bottom(l, 8);
    return l;
}

static GtkWidget *
tile_flow(void)
{
    GtkWidget *flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flow), 1);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 4);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flow), 10);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flow), 10);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(flow), TRUE);
    gtk_widget_set_margin_start(flow, 18);
    gtk_widget_set_margin_end(flow, 18);
    gtk_widget_add_css_class(flow, "grid-flow");
    return flow;
}

static void
show_this_pc(AppState *st)
{
    GtkWidget *fv = st->file_view;

    clear_selection(st);
    anim_view_transition(fv);
    clear_file_view(fv);
    gtk_widget_set_visible(st->column_header, FALSE);

    GtkWidget *title = section_title("This PC");
    anim_slide_in_from_left(title);
    gtk_box_append(GTK_BOX(fv), title);

    /* ── user folders (like Windows 11) ── */
    struct { const char *icon; const char *name; const char *sub; } folders[] = {
        { "\xF0\x9F\x8F\xA0", "Home",      NULL          },
        { "\xF0\x9F\x96\xA5", "Desktop",   "Desktop"     },
        { "\xF0\x9F\x93\x84", "Documents", "Documents"   },
        { "\xE2\xAC\x87",     "Downloads", "Downloads"   },
        { "\xF0\x9F\x96\xBC", "Pictures",  "Pictures"    },
        { "\xF0\x9F\x8E\xB5", "Music",     "Music"       },
        { "\xF0\x9F\x8E\xAC", "Videos",    "Videos"      },
    };

    GtkWidget *fflow = tile_flow();
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(fflow), 7);
    int nfolders = 0;
    for (guint i = 0; i < G_N_ELEMENTS(folders); i++) {
        char *p = folders[i].sub
            ? g_build_filename(g_get_home_dir(), folders[i].sub, NULL)
            : g_strdup(g_get_home_dir());
        if (g_file_test(p, G_FILE_TEST_IS_DIR)) {
            GtkWidget *tile = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
            gtk_widget_add_css_class(tile, "grid-tile");
            gtk_widget_set_size_request(tile, 104, 92);

            GtkWidget *ic = gtk_label_new(folders[i].icon);
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
        gtk_box_append(GTK_BOX(fv), section_title("Folders"));
        gtk_box_append(GTK_BOX(fv), fflow);
    } else {
        g_object_ref_sink(fflow);
        g_object_unref(fflow);
    }

    /* ── drives ── */
    gtk_box_append(GTK_BOX(fv), section_title("Devices and drives"));
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
            label, p->mountpoint, p->device, opt, removable);
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
            nm && nm[0] ? nm : mp, mp, NULL, opt, removable);
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
            dn ? dn : "Optical Drive", NULL, dev, TRUE, FALSE);
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
    gtk_window_set_title(GTK_WINDOW(st->window), "This PC — MyFM");

    set_status_left(st, "%d drive%s", count, count == 1 ? "" : "s");
    update_status(st);
    myfm_update_actions(st);
}

void
this_pc_button_clicked(GtkButton *b, gpointer ud)
{
    (void)b;
    AppState *st = ud;
    st->place = PLACE_THIS_PC;
    show_this_pc(st);
}


/* ═══════════════════════════════════════════
 * TRASH
 * ═══════════════════════════════════════════ */

typedef struct {
    char     *uri;
    char     *orig;
    char     *name;
    AppState *state;
} TrashItem;

static void
trash_item_free(gpointer p)
{
    TrashItem *t = p;
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
    if (!g_file_move(src, dst, G_FILE_COPY_NONE, NULL, NULL, NULL, &err))
        show_error(t->state, "Could not restore", "%s",
            err ? err->message : "Unknown error");
    g_clear_error(&err);
    g_object_unref(src);
    g_object_unref(dst);
    refresh_view(t->state);
}

typedef struct {
    AppState *st;
    char     *uri;
} TrashDelCtx;

static void
trash_del_ctx_free(gpointer p)
{
    TrashDelCtx *c = p;
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
show_trash(AppState *st)
{
    GtkWidget *fv = st->file_view;

    clear_selection(st);
    anim_view_transition(fv);
    clear_file_view(fv);
    gtk_widget_set_visible(st->column_header, FALSE);

    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_end(head, 18);
    GtkWidget *title = section_title("\xF0\x9F\x97\x91  Recycle Bin");
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

    GFile *trash = g_file_new_for_uri("trash:///");
    GFileEnumerator *en = g_file_enumerate_children(trash,
        "standard::name,standard::display-name,standard::type,"
        "standard::size,trash::orig-path",
        G_FILE_QUERY_INFO_NONE, NULL, NULL);

    if (!en) {
        gtk_box_append(GTK_BOX(fv), info_label("Unable to open the Recycle Bin"));
        g_object_unref(trash);
        set_status_left(st, "Recycle Bin unavailable");
        gtk_widget_set_sensitive(empty_btn, FALSE);
        return;
    }

    guint n = 0;
    GFileInfo *fi;
    while ((fi = g_file_enumerator_next_file(en, NULL, NULL)) != NULL) {
        const char *disp = g_file_info_get_display_name(fi);
        const char *name = g_file_info_get_name(fi);
        gboolean is_dir =
            g_file_info_get_file_type(fi) == G_FILE_TYPE_DIRECTORY;
        const char *orig = g_file_info_get_attribute_byte_string(fi,
            "trash::orig-path");

        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_add_css_class(row, "file-row");
        gtk_widget_set_margin_start(row, 6);
        gtk_widget_set_margin_end(row, 6);

        GtkWidget *ico = gtk_label_new(
            file_icon_for(disp ? disp : name, is_dir));
        gtk_widget_set_size_request(ico, ICON_W, -1);
        gtk_box_append(GTK_BOX(row), ico);

        GtkWidget *l = gtk_label_new(disp ? disp : name);
        gtk_widget_add_css_class(l, "file-label");
        gtk_widget_set_hexpand(l, TRUE);
        gtk_widget_set_halign(l, GTK_ALIGN_START);
        gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
        gtk_box_append(GTK_BOX(row), l);

        GtkWidget *o = gtk_label_new(orig ? orig : "");
        gtk_widget_add_css_class(o, "file-meta");
        gtk_widget_set_size_request(o, 320, -1);
        gtk_label_set_xalign(GTK_LABEL(o), 0.0f);
        gtk_label_set_ellipsize(GTK_LABEL(o), PANGO_ELLIPSIZE_MIDDLE);
        gtk_box_append(GTK_BOX(row), o);

        TrashItem *t = g_new0(TrashItem, 1);
        t->uri   = g_strconcat("trash:///", name, NULL);
        t->orig  = orig ? g_strdup(orig) : NULL;
        t->name  = g_strdup(disp ? disp : name);
        t->state = st;
        g_object_set_data_full(G_OBJECT(row), "trash-item", t, trash_item_free);

        GtkGesture *gc = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gc), 0);
        g_signal_connect(gc, "pressed", G_CALLBACK(trash_pressed), row);
        gtk_widget_add_controller(row, GTK_EVENT_CONTROLLER(gc));

        if (n < MAX_ANIMATED_ROWS)
            anim_row_appear(row, n * 14);
        gtk_box_append(GTK_BOX(fv), row);

        n++;
        g_object_unref(fi);
    }
    g_object_unref(en);
    g_object_unref(trash);

    if (n == 0) {
        GtkWidget *l = info_label("The Recycle Bin is empty");
        anim_fade_in(l);
        gtk_box_append(GTK_BOX(fv), l);
        gtk_widget_set_sensitive(empty_btn, FALSE);
    }

    st->place = PLACE_TRASH;
    set_status_left(st, "%u item%s", n, n == 1 ? "" : "s");
    update_status(st);
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
    gtk_window_set_title(GTK_WINDOW(st->window), "Recycle Bin — MyFM");
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

void myfm_cut_selected(AppState *st)
{ if (selection_is_file(st)) set_clipboard(st, st->selected_path, TRUE); }

void myfm_copy_selected(AppState *st)
{ if (selection_is_file(st)) set_clipboard(st, st->selected_path, FALSE); }

void
myfm_copy_path_selected(AppState *st)
{
    if (!st->selected_path) return;
    GdkClipboard *cb = gtk_widget_get_clipboard(st->window);
    gdk_clipboard_set_text(cb, st->selected_path);
    set_status_left(st, "Path copied to clipboard");
}

void
myfm_paste(AppState *st)
{
    if (!st->clip_path || st->place != PLACE_FOLDER || !st->current_path)
        return;

    if (!g_file_test(st->clip_path, G_FILE_TEST_EXISTS)) {
        show_error(st, "Nothing to paste",
            "The item on the clipboard no longer exists.");
        g_clear_pointer(&st->clip_path, g_free);
        myfm_update_actions(st);
        return;
    }

    /* A folder can never be pasted into itself or into one of its own
     * subfolders: that would recurse forever. */
    if (g_file_test(st->clip_path, G_FILE_TEST_IS_DIR) &&
        path_is_inside(st->current_path, st->clip_path)) {
        char *base = g_path_get_basename(st->clip_path);
        show_error(st, "Cannot paste here",
            "“%s” cannot be copied into itself or into one of its "
            "own subfolders.", base);
        g_free(base);
        return;
    }

    char *base = g_path_get_basename(st->clip_path);
    char *dest = g_build_filename(st->current_path, base, NULL);

    if (g_file_test(dest, G_FILE_TEST_EXISTS)) {
        if (st->clip_is_cut) {
            show_error(st, "Name already in use",
                "“%s” already exists in this folder.", base);
            g_free(base);
            g_free(dest);
            return;
        }
        g_free(dest);
        char *alt = g_strdup_printf("%s (copy)", base);
        dest = unique_child(st->current_path, alt);
        g_free(alt);
    }

    GFile *src = g_file_new_for_path(st->clip_path);
    GFile *dst = g_file_new_for_path(dest);
    GError *err = NULL;
    gboolean ok;

    if (st->clip_is_cut) {
        ok = g_file_move(src, dst, G_FILE_COPY_NOFOLLOW_SYMLINKS,
                NULL, NULL, NULL, &err);
        if (!ok && g_error_matches(err, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED)) {
            g_clear_error(&err);
            ok = copy_recursive(src, dst, &err);
            if (ok) delete_recursive(src, NULL);
        }
        if (ok) {
            g_clear_pointer(&st->clip_path, g_free);
            myfm_update_actions(st);
        }
    } else {
        ok = copy_recursive(src, dst, &err);
    }

    if (!ok)
        show_error(st, "Paste failed", "%s",
            err ? err->message : "Unknown error");

    g_clear_error(&err);
    g_object_unref(src);
    g_object_unref(dst);
    g_free(base);
    g_free(dest);
    refresh_view(st);
}

void
myfm_rename_selected(AppState *st)
{ if (selection_is_file(st)) rename_dialog(st, st->selected_path); }

void
myfm_trash_selected(AppState *st)
{ if (selection_is_file(st)) trash_confirm(st, st->selected_path); }

void
myfm_delete_selected(AppState *st)
{ if (selection_is_file(st)) delete_confirm(st, st->selected_path); }

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

    if (g_str_has_prefix(path, home)) {
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

    if (st->refresh_id) g_source_remove(st->refresh_id);
    st->refresh_id = g_timeout_add(180, do_refresh, st);
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
    gtk_widget_set_sensitive(st->btn_back, st->history_position > 0);
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
    char *title = g_strdup_printf("%s — MyFM",
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
    if (st->history_position <= 0) return;
    st->history_position--;
    navigate_to(st, g_ptr_array_index(st->history, st->history_position), FALSE);
    update_nav_buttons(st);
}

void
go_forward(GtkButton *b, gpointer ud)
{
    (void)b;
    AppState *st = ud;
    if (st->history_position >= (int)st->history->len - 1) return;
    st->history_position++;
    navigate_to(st, g_ptr_array_index(st->history, st->history_position), FALSE);
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
create_directory_button(const char *icon, const char *label,
                        const char *path, AppState *st)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

    GtkWidget *ic = gtk_label_new(icon);
    gtk_widget_add_css_class(ic, "sidebar-icon");
    gtk_box_append(GTK_BOX(row), ic);

    GtkWidget *lb = gtk_label_new(label);
    gtk_widget_set_halign(lb, GTK_ALIGN_START);
    gtk_widget_set_hexpand(lb, TRUE);
    gtk_box_append(GTK_BOX(row), lb);

    GtkWidget *btn = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(btn), row);
    gtk_widget_add_css_class(btn, "sidebar-btn");

    if (path)
        g_object_set_data_full(G_OBJECT(btn), "directory-path",
            g_strdup(path), g_free);
    g_signal_connect(btn, "clicked",
        G_CALLBACK(directory_button_clicked), st);
    return btn;
}


/* ═══════════════════════════════════════════
 * CLEANUP
 * ═══════════════════════════════════════════ */

void
app_state_free(AppState *st)
{
    if (!st) return;
    if (st->refresh_id) g_source_remove(st->refresh_id);
    if (st->directory_monitor) {
        g_file_monitor_cancel(st->directory_monitor);
        g_clear_object(&st->directory_monitor);
    }
    if (st->history) g_ptr_array_unref(st->history);
    g_free(st->current_path);
    g_free(st->selected_path);
    g_free(st->filter_text);
    g_free(st->clip_path);
    g_free(st);
}
