/* gui.c – everything visual in ExploreR (MyFM): animations, dialogs, the
 * properties dialog, the theme engine (Windows 10 light / dark, purple),
 * icons, the preview pane and the main window itself.
 *
 * Nothing here depends on GNOME: the window controls are laid out by us
 * (minimize / maximize / close on the right, whatever the desktop's own
 * button layout is), every widget is styled by our own CSS, and the
 * light/dark preference is read from GtkSettings, which GTK fills from
 * the cross-desktop xdg-desktop-portal "color-scheme" setting.
 */

#include "myfm.h"

#include <pwd.h>
#include <grp.h>
#include <time.h>

/* lightweight CSS transition animations */



/* ─────────────────────────────────────────
 * INTERNAL HELPERS
 * ───────────────────────────────────────── */

typedef struct {
    GtkWidget *widget;      /* owned reference */
    char      *add_class;
    char      *remove_class;
} TimerData;

static void
timer_data_free(gpointer p)
{
    TimerData *td = p;
    g_clear_object(&td->widget);
    g_free(td->add_class);
    g_free(td->remove_class);
    g_free(td);
}

/* Called after a delay: adds/removes CSS classes on the widget */
static gboolean
delayed_class_swap(gpointer user_data)
{
    TimerData *td = user_data;

    if (!GTK_IS_WIDGET(td->widget))
        return G_SOURCE_REMOVE;

    if (td->remove_class)
        gtk_widget_remove_css_class(td->widget, td->remove_class);
    if (td->add_class)
        gtk_widget_add_css_class(td->widget, td->add_class);

    return G_SOURCE_REMOVE;
}

static void
schedule_class_swap(GtkWidget  *w,
                    const char *remove_cls,
                    const char *add_cls,
                    guint       delay_ms)
{
    TimerData *td = g_new0(TimerData, 1);

    /* Keep the widget alive until the timeout fires – otherwise a fast
     * directory refresh can free it under our feet. */
    td->widget       = g_object_ref(w);
    td->remove_class = remove_cls ? g_strdup(remove_cls) : NULL;
    td->add_class    = add_cls    ? g_strdup(add_cls)    : NULL;

    g_timeout_add_full(G_PRIORITY_DEFAULT, delay_ms,
        delayed_class_swap, td, timer_data_free);
}


/* ─────────────────────────────────────────
 * PUBLIC ANIMATIONS
 * ───────────────────────────────────────── */

/* Animations are optional (Settings > Animations) and also follow the
 * desktop's own "reduce animations" switch (gtk-enable-animations). */
gboolean
anim_enabled(void)
{
    if (!myfm_settings()->animations) return FALSE;
    gboolean on = TRUE;
    GtkSettings *gs = gtk_settings_get_default();
    if (gs) g_object_get(gs, "gtk-enable-animations", &on, NULL);
    return on;
}

void
anim_fade_in(GtkWidget *widget)
{
    if (!GTK_IS_WIDGET(widget) || !anim_enabled()) return;

    gtk_widget_remove_css_class(widget, "anim-fade-in");
    gtk_widget_add_css_class(widget, "anim-fade-start");

    schedule_class_swap(widget, "anim-fade-start", "anim-fade-in", 16);
}

void
anim_slide_in_from_left(GtkWidget *widget)
{
    if (!GTK_IS_WIDGET(widget) || !anim_enabled()) return;

    gtk_widget_remove_css_class(widget, "anim-slide-in");
    gtk_widget_add_css_class(widget, "anim-slide-start");

    schedule_class_swap(widget, "anim-slide-start", "anim-slide-in", 16);
}

void
anim_pulse_icon(GtkWidget *widget)
{
    if (!GTK_IS_WIDGET(widget) || !anim_enabled()) return;

    gtk_widget_add_css_class(widget, "anim-pulse");
    gtk_widget_add_css_class(widget, "anim-pulse-big");

    schedule_class_swap(widget, "anim-pulse-big", NULL, 160);
}

/* Staggered row appearance – pass i * 18 for rows 0,1,2… */
void
anim_row_appear(GtkWidget *widget, guint delay_ms)
{
    if (!GTK_IS_WIDGET(widget) || !anim_enabled()) return;

    gtk_widget_remove_css_class(widget, "anim-row-in");
    gtk_widget_add_css_class(widget, "anim-row-start");

    schedule_class_swap(widget, "anim-row-start", "anim-row-in", delay_ms + 16);
}

/* Cross-fade the whole file view when switching directories */
void
anim_view_transition(GtkWidget *file_view)
{
    if (!GTK_IS_WIDGET(file_view) || !anim_enabled()) return;

    /* Drop the class left by the previous transition, otherwise
     * .anim-fade-in (opacity 1) wins and every later fade is skipped. */
    gtk_widget_remove_css_class(file_view, "anim-fade-in");
    gtk_widget_add_css_class(file_view, "anim-fade-start");
    schedule_class_swap(file_view, "anim-fade-start", "anim-fade-in", 30);
}

/* ════════════════════════════════════════════════════════════
 * DIALOGS
 * ════════════════════════════════════════════════════════════ */

/* MyFM's own dialogs.
 *
 * Nothing here uses GtkAlertDialog / GtkMessageDialog, so every window the
 * file manager shows is drawn with the application theme and looks the same
 * on any desktop environment.
 */



typedef struct {
    GtkWidget       *win;
    GtkWidget       *entry;
    MyfmConfirmFunc  confirm_cb;
    MyfmPromptFunc   prompt_cb;
    gpointer         data;
    GDestroyNotify   data_free;
    gboolean         answered;
} DlgCtx;


static void
dlg_finish(DlgCtx *d, gboolean accepted)
{
    if (d->answered) return;
    d->answered = TRUE;

    if (d->prompt_cb) {
        const char *txt = (accepted && d->entry)
            ? gtk_editable_get_text(GTK_EDITABLE(d->entry)) : NULL;
        char *copy = txt ? g_strdup(txt) : NULL;
        d->prompt_cb(copy, d->data);
        g_free(copy);
    } else if (d->confirm_cb) {
        d->confirm_cb(accepted, d->data);
    }
}

static void
dlg_free(gpointer p)
{
    DlgCtx *d = p;
    dlg_finish(d, FALSE);            /* closed without choosing */
    if (d->data_free) d->data_free(d->data);
    g_free(d);
}

static void
on_dlg_ok(GtkButton *b, gpointer ud)
{
    (void)b;
    DlgCtx *d = ud;
    dlg_finish(d, TRUE);
    gtk_window_destroy(GTK_WINDOW(d->win));
}

static void
on_dlg_cancel(GtkButton *b, gpointer ud)
{
    (void)b;
    DlgCtx *d = ud;
    dlg_finish(d, FALSE);
    gtk_window_destroy(GTK_WINDOW(d->win));
}

static gboolean
on_dlg_key(GtkEventControllerKey *c, guint keyval, guint code,
           GdkModifierType mod, gpointer ud)
{
    (void)c; (void)code; (void)mod;
    DlgCtx *d = ud;
    if (keyval == GDK_KEY_Escape) {
        dlg_finish(d, FALSE);
        gtk_window_destroy(GTK_WINDOW(d->win));
        return TRUE;
    }
    return FALSE;
}

/* Select the file name without its extension, like Explorer does. */
static void
select_basename(GtkWidget *entry)
{
    const char *t = gtk_editable_get_text(GTK_EDITABLE(entry));
    if (!t || !t[0]) return;

    const char *dot = g_strrstr(t, ".");
    int end = (dot && dot != t)
        ? (int)g_utf8_pointer_to_offset(t, dot)
        : -1;
    gtk_editable_select_region(GTK_EDITABLE(entry), 0, end);
}


static void
dialog_show(GtkWidget       *parent,
            const char      *icon,
            const char      *title,
            const char      *body,
            const char      *initial,      /* NULL -> no entry */
            const char      *ok_label,
            gboolean         destructive,
            gboolean         has_cancel,
            MyfmConfirmFunc  ccb,
            MyfmPromptFunc   pcb,
            gpointer         data,
            GDestroyNotify   data_free)
{
    DlgCtx *d = g_new0(DlgCtx, 1);
    d->confirm_cb = ccb;
    d->prompt_cb  = pcb;
    d->data       = data;
    d->data_free  = data_free;

    GtkWidget *win = gtk_window_new();
    d->win = win;
    gtk_widget_add_css_class(win, "myfm-dialog");
    gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(win), 430, -1);
    gtk_window_set_title(GTK_WINDOW(win), title);
    myfm_window_titlebar(win, title);
    myfm_window_set_owner(win, parent);
    g_object_set_data_full(G_OBJECT(win), "dlg-ctx", d, dlg_free);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 14);
    gtk_window_set_child(GTK_WINDOW(win), box);

    /* icon + texts */
    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);

    GtkWidget *ic = gtk_label_new(icon ? icon : "\xE2\x84\xB9");
    gtk_widget_add_css_class(ic, "dlg-icon");
    gtk_widget_set_valign(ic, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(head), ic);

    GtkWidget *texts = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_hexpand(texts, TRUE);

    GtkWidget *tl = gtk_label_new(title);
    gtk_widget_add_css_class(tl, "dlg-title");
    gtk_widget_set_halign(tl, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(tl), TRUE);
    gtk_label_set_xalign(GTK_LABEL(tl), 0.0f);
    gtk_label_set_max_width_chars(GTK_LABEL(tl), 42);
    gtk_box_append(GTK_BOX(texts), tl);

    if (body && body[0]) {
        GtkWidget *bl = gtk_label_new(body);
        gtk_widget_add_css_class(bl, "dlg-body");
        gtk_widget_set_halign(bl, GTK_ALIGN_START);
        gtk_label_set_wrap(GTK_LABEL(bl), TRUE);
        gtk_label_set_xalign(GTK_LABEL(bl), 0.0f);
        gtk_label_set_max_width_chars(GTK_LABEL(bl), 46);
        gtk_box_append(GTK_BOX(texts), bl);
    }

    gtk_box_append(GTK_BOX(head), texts);
    gtk_box_append(GTK_BOX(box), head);

    /* optional entry */
    if (initial) {
        d->entry = gtk_entry_new();
        gtk_widget_add_css_class(d->entry, "text-entry");
        gtk_editable_set_text(GTK_EDITABLE(d->entry), initial);
        gtk_entry_set_activates_default(GTK_ENTRY(d->entry), FALSE);
        gtk_box_append(GTK_BOX(box), d->entry);
    }

    /* buttons */
    GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btns, GTK_ALIGN_END);
    gtk_widget_set_margin_top(btns, 4);

    if (has_cancel) {
        GtkWidget *cancel = gtk_button_new_with_label("Cancel");
        gtk_widget_add_css_class(cancel, "dlg-btn");
        g_signal_connect(cancel, "clicked", G_CALLBACK(on_dlg_cancel), d);
        gtk_box_append(GTK_BOX(btns), cancel);
    }

    GtkWidget *ok = gtk_button_new_with_label(ok_label ? ok_label : "OK");
    gtk_widget_add_css_class(ok,
        destructive ? "dlg-btn-danger" : "dlg-btn-accent");
    g_signal_connect(ok, "clicked", G_CALLBACK(on_dlg_ok), d);
    gtk_box_append(GTK_BOX(btns), ok);

    gtk_box_append(GTK_BOX(box), btns);

    if (d->entry)
        g_signal_connect_swapped(d->entry, "activate",
            G_CALLBACK(gtk_widget_activate), ok);

    GtkEventController *kc = gtk_event_controller_key_new();
    g_signal_connect(kc, "key-pressed", G_CALLBACK(on_dlg_key), d);
    gtk_widget_add_controller(win, kc);

    gtk_window_present(GTK_WINDOW(win));

    if (d->entry) {
        gtk_widget_grab_focus(d->entry);
        select_basename(d->entry);
    } else {
        gtk_widget_grab_focus(ok);
    }
}


/* ═══════════════════════════════════════════
 * PUBLIC API
 * ═══════════════════════════════════════════ */

void
myfm_message(GtkWidget *parent, const char *icon,
             const char *title, const char *body)
{
    dialog_show(parent, icon, title, body, NULL,
        "OK", FALSE, FALSE, NULL, NULL, NULL, NULL);
}

void
myfm_message_fmt(GtkWidget *parent, const char *icon,
                 const char *title, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *body = g_strdup_vprintf(fmt, ap);
    va_end(ap);

    myfm_message(parent, icon, title, body);
    g_free(body);
}

void
myfm_confirm(GtkWidget *parent, const char *icon,
             const char *title, const char *body,
             const char *ok_label, gboolean destructive,
             MyfmConfirmFunc cb, gpointer data, GDestroyNotify data_free)
{
    dialog_show(parent, icon, title, body, NULL,
        ok_label, destructive, TRUE, cb, NULL, data, data_free);
}

void
myfm_prompt(GtkWidget *parent, const char *icon,
            const char *title, const char *body,
            const char *initial, const char *ok_label,
            MyfmPromptFunc cb, gpointer data, GDestroyNotify data_free)
{
    dialog_show(parent, icon, title, body, initial ? initial : "",
        ok_label, FALSE, TRUE, NULL, cb, data, data_free);
}

/* ════════════════════════════════════════════════════════════
 * PROPERTIES DIALOG
 * ════════════════════════════════════════════════════════════ */

/* properties dialog (boxed-list style) */


#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <time.h>
#include <string.h>


/* ═══════════════════════════════════════════
 * FORMATTING HELPERS
 * ═══════════════════════════════════════════ */

static char *
format_size_prop(guint64 bytes)
{
    if (bytes >= 1024ULL * 1024ULL * 1024ULL)
        return g_strdup_printf("%.1f GB (%" G_GUINT64_FORMAT " bytes)",
            (double)bytes / (1024.0 * 1024.0 * 1024.0), bytes);
    if (bytes >= 1024ULL * 1024ULL)
        return g_strdup_printf("%.1f MB (%" G_GUINT64_FORMAT " bytes)",
            (double)bytes / (1024.0 * 1024.0), bytes);
    if (bytes >= 1024ULL)
        return g_strdup_printf("%.1f kB (%" G_GUINT64_FORMAT " bytes)",
            (double)bytes / 1024.0, bytes);
    return g_strdup_printf("%" G_GUINT64_FORMAT " bytes", bytes);
}

char *
myfm_format_size(guint64 bytes)
{
    if (bytes >= 1024ULL * 1024ULL * 1024ULL)
        return g_strdup_printf("%.1f GB",
            (double)bytes / (1024.0 * 1024.0 * 1024.0));
    if (bytes >= 1024ULL * 1024ULL)
        return g_strdup_printf("%.1f MB", (double)bytes / (1024.0 * 1024.0));
    if (bytes >= 1024ULL)
        return g_strdup_printf("%.1f kB", (double)bytes / 1024.0);
    return g_strdup_printf("%" G_GUINT64_FORMAT " B", bytes);
}

static char *
format_time_prop(time_t t)
{
    if (t <= 0) return g_strdup("—");
    GDateTime *dt = g_date_time_new_from_unix_local((gint64)t);
    if (!dt) return g_strdup("—");
    char *s = g_date_time_format(dt, "%e %B %Y, %H:%M");
    g_date_time_unref(dt);
    return g_strstrip(s);
}

static guint
count_dir_items(const char *path)
{
    GDir *d = g_dir_open(path, 0, NULL);
    if (!d) return 0;
    guint n = 0;
    while (g_dir_read_name(d) != NULL) n++;
    g_dir_close(d);
    return n;
}


/* ═══════════════════════════════════════════
 * "BOXED LIST" BUILDING BLOCKS
 * ═══════════════════════════════════════════ */

GtkWidget *
myfm_group_title(const char *text)
{
    GtkWidget *l = gtk_label_new(text);
    gtk_widget_add_css_class(l, "prop-group-title");
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    return l;
}

GtkWidget *
myfm_card_new(void)
{
    GtkWidget *c = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(c, "prop-card");
    return c;
}

/* Appends `child` as a new row, adding a separator when needed. */
static void
card_append(GtkWidget *card, GtkWidget *row)
{
    if (gtk_widget_get_first_child(card) != NULL) {
        GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_box_append(GTK_BOX(card), sep);
    }
    gtk_box_append(GTK_BOX(card), row);
}

static GtkWidget *
row_box(const char *label_text)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(row, "prop-row");

    GtkWidget *l = gtk_label_new(label_text);
    gtk_widget_add_css_class(l, "prop-label");
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    gtk_widget_set_valign(l, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(row), l);

    return row;
}

GtkWidget *
myfm_card_add_value(GtkWidget *card, const char *label_text,
               const char *value_text, gboolean mono)
{
    GtkWidget *row = row_box(label_text);

    GtkWidget *v = gtk_label_new(value_text ? value_text : "—");
    gtk_widget_add_css_class(v, "prop-value");
    if (mono) gtk_widget_add_css_class(v, "prop-mono");
    gtk_widget_set_halign(v, GTK_ALIGN_END);
    gtk_widget_set_hexpand(v, TRUE);
    gtk_label_set_xalign(GTK_LABEL(v), 1.0f);
    gtk_label_set_selectable(GTK_LABEL(v), TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(v), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_max_width_chars(GTK_LABEL(v), 40);
    gtk_box_append(GTK_BOX(row), v);

    card_append(card, row);
    return v;
}

void
myfm_card_add_widget(GtkWidget *card, const char *label_text, GtkWidget *w)
{
    GtkWidget *row = row_box(label_text);
    gtk_widget_set_halign(w, GTK_ALIGN_END);
    gtk_widget_set_valign(w, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(w, TRUE);
    gtk_box_append(GTK_BOX(row), w);
    card_append(card, row);
}


/* ═══════════════════════════════════════════
 * PERMISSIONS (GNOME style dropdowns)
 * ═══════════════════════════════════════════ */

typedef struct {
    char      *path;
    gboolean   is_dir;
    mode_t     mode;
    GtkWidget *window;
    GtkWidget *dd[3];          /* owner, group, others */
    GtkWidget *exec_switch;
    GtkWidget *mode_label;
    gboolean   updating;
} PermUI;

static void
perm_ui_free(gpointer p)
{
    PermUI *pu = p;
    g_free(pu->path);
    g_free(pu);
}

/* access level -> rwx triplet */
static unsigned
level_to_bits(const PermUI *pu, guint level)
{
    if (pu->is_dir) {
        switch (level) {
            case 1:  return 4;          /* list files only   */
            case 2:  return 5;          /* access files      */
            case 3:  return 7;          /* create and delete */
            default: return 0;
        }
    }
    switch (level) {
        case 1:  return 4;              /* read-only      */
        case 2:  return 6;              /* read and write */
        default: return 0;
    }
}

static guint
bits_to_level(const PermUI *pu, unsigned bits)
{
    bits &= 7;
    if (pu->is_dir) {
        if ((bits & 7) == 7)  return 3;
        if ((bits & 5) == 5)  return 2;
        if (bits & 4)         return 1;
        return 0;
    }
    if ((bits & 6) == 6) return 2;
    if (bits & 4)        return 1;
    return 0;
}

static void
perm_apply(PermUI *pu)
{
    if (pu->updating) return;

    unsigned o = level_to_bits(pu,
        gtk_drop_down_get_selected(GTK_DROP_DOWN(pu->dd[0])));
    unsigned g = level_to_bits(pu,
        gtk_drop_down_get_selected(GTK_DROP_DOWN(pu->dd[1])));
    unsigned x = level_to_bits(pu,
        gtk_drop_down_get_selected(GTK_DROP_DOWN(pu->dd[2])));

    if (!pu->is_dir && pu->exec_switch &&
        gtk_switch_get_active(GTK_SWITCH(pu->exec_switch))) {
        if (o & 4) o |= 1;
        if (g & 4) g |= 1;
        if (x & 4) x |= 1;
    }

    mode_t new_mode = (mode_t)((o << 6) | (g << 3) | x);
    /* keep setuid/setgid/sticky bits */
    new_mode |= (pu->mode & (S_ISUID | S_ISGID | S_ISVTX));

    if (chmod(pu->path, new_mode) != 0) {
        myfm_message_fmt(pu->window, "\xE2\x9A\xA0",
            "Could not change the permissions", "%s", g_strerror(errno));
    } else {
        pu->mode = new_mode;
        if (pu->mode_label) {
            char octal[8];
            g_snprintf(octal, sizeof(octal), "%04o",
                (unsigned)(new_mode & 07777));
            gtk_label_set_text(GTK_LABEL(pu->mode_label), octal);
        }
    }
}

static void
on_perm_changed(GObject *obj, GParamSpec *pspec, gpointer ud)
{
    (void)obj; (void)pspec;
    perm_apply((PermUI *)ud);
}

static GtkWidget *
perm_dropdown(PermUI *pu, unsigned bits)
{
    static const char *file_levels[] = {
        "None", "Read-only", "Read and write", NULL
    };
    static const char *dir_levels[] = {
        "None", "List files only", "Access files",
        "Create and delete files", NULL
    };

    GtkWidget *dd = gtk_drop_down_new_from_strings(
        pu->is_dir ? dir_levels : file_levels);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dd), bits_to_level(pu, bits));
    return dd;
}

static GtkWidget *
build_permissions_card(PermUI *pu, const struct stat *stp)
{
    GtkWidget *card = myfm_card_new();

    struct passwd *pw = getpwuid(stp->st_uid);
    struct group  *gr = getgrgid(stp->st_gid);

    char *owner = pw ? g_strdup(pw->pw_name)
                     : g_strdup_printf("uid %u", (unsigned)stp->st_uid);
    char *group = gr ? g_strdup(gr->gr_name)
                     : g_strdup_printf("gid %u", (unsigned)stp->st_gid);

    myfm_card_add_value(card, "Owner", owner, FALSE);
    myfm_card_add_value(card, "Group", group, FALSE);
    g_free(owner);
    g_free(group);

    gboolean editable = (getuid() == 0 || getuid() == stp->st_uid);

    pu->updating = TRUE;
    pu->dd[0] = perm_dropdown(pu, (stp->st_mode >> 6) & 7);
    pu->dd[1] = perm_dropdown(pu, (stp->st_mode >> 3) & 7);
    pu->dd[2] = perm_dropdown(pu,  stp->st_mode       & 7);

    myfm_card_add_widget(card, "Owner access",  pu->dd[0]);
    myfm_card_add_widget(card, "Group access",  pu->dd[1]);
    myfm_card_add_widget(card, "Others access", pu->dd[2]);

    if (!pu->is_dir) {
        pu->exec_switch = gtk_switch_new();
        gtk_switch_set_active(GTK_SWITCH(pu->exec_switch),
            (stp->st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0);
        myfm_card_add_widget(card, "Executable as program", pu->exec_switch);
    }

    for (int i = 0; i < 3; i++) {
        gtk_widget_set_sensitive(pu->dd[i], editable);
        g_signal_connect(pu->dd[i], "notify::selected",
            G_CALLBACK(on_perm_changed), pu);
    }
    if (pu->exec_switch) {
        gtk_widget_set_sensitive(pu->exec_switch, editable);
        g_signal_connect(pu->exec_switch, "notify::active",
            G_CALLBACK(on_perm_changed), pu);
    }
    pu->updating = FALSE;

    char octal[8];
    g_snprintf(octal, sizeof(octal), "%04o", (unsigned)(stp->st_mode & 07777));
    pu->mode_label = myfm_card_add_value(card, "Mode", octal, TRUE);

    return card;
}


/* ═══════════════════════════════════════════
 * MAIN DIALOG
 * ═══════════════════════════════════════════ */

void
show_properties_dialog(GtkWidget *parent, const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0) {
        myfm_message_fmt(parent, "\xE2\x9A\xA0",
            "Cannot read the properties of this item",
            "%s", g_strerror(errno));
        return;
    }

    gboolean is_link = S_ISLNK(st.st_mode);
    if (is_link) {
        struct stat target;
        if (stat(path, &target) == 0) st = target;
    }
    gboolean is_dir = S_ISDIR(st.st_mode);

    char *base = g_path_get_basename(path);
    char *parent_dir = g_path_get_dirname(path);

    /* ── window ── */
    GtkWidget *win = gtk_window_new();
    gtk_widget_add_css_class(win, "props");
    gtk_window_set_title(GTK_WINDOW(win), "Properties");
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 560);
    char *wt = g_strdup_printf("%s Properties", base);
    gtk_window_set_title(GTK_WINDOW(win), wt);
    myfm_window_titlebar(win, wt);
    g_free(wt);
    myfm_window_set_owner(win, parent);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_window_set_child(GTK_WINDOW(win), scroll);

    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start(page, 22);
    gtk_widget_set_margin_end(page, 22);
    gtk_widget_set_margin_top(page, 8);
    gtk_widget_set_margin_bottom(page, 22);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), page);

    /* ── header: icon + name + type ── */
    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_halign(head, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(head, 10);
    gtk_widget_set_margin_bottom(head, 12);

    GtkWidget *icon = myfm_icon_widget(base, is_dir, NULL, 48);
    gtk_widget_add_css_class(icon, "prop-icon");
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    anim_pulse_icon(icon);
    gtk_box_append(GTK_BOX(head), icon);

    GtkWidget *name_lbl = gtk_label_new(base);
    gtk_widget_add_css_class(name_lbl, "prop-title");
    gtk_label_set_wrap(GTK_LABEL(name_lbl), TRUE);
    gtk_label_set_justify(GTK_LABEL(name_lbl), GTK_JUSTIFY_CENTER);
    gtk_label_set_max_width_chars(GTK_LABEL(name_lbl), 34);
    gtk_label_set_selectable(GTK_LABEL(name_lbl), TRUE);
    gtk_box_append(GTK_BOX(head), name_lbl);

    /* content type */
    char *type_desc = NULL;
    if (is_dir) {
        type_desc = g_strdup("Folder");
    } else {
        GFile *gf = g_file_new_for_path(path);
        GFileInfo *fi = g_file_query_info(gf,
            G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
            G_FILE_QUERY_INFO_NONE, NULL, NULL);
        if (fi) {
            const char *ct = g_file_info_get_content_type(fi);
            if (ct) type_desc = g_content_type_get_description(ct);
            g_object_unref(fi);
        }
        g_object_unref(gf);
        if (!type_desc) type_desc = g_strdup("File");
    }

    char *subtitle = is_link
        ? g_strdup_printf("%s · Symbolic link", type_desc)
        : g_strdup(type_desc);
    GtkWidget *sub = gtk_label_new(subtitle);
    gtk_widget_add_css_class(sub, "prop-subtitle");
    gtk_box_append(GTK_BOX(head), sub);
    g_free(subtitle);

    gtk_box_append(GTK_BOX(page), head);
    anim_fade_in(head);

    /* ── basic info ── */
    GtkWidget *card1 = myfm_card_new();

    if (is_dir) {
        guint n = count_dir_items(path);
        char *c = g_strdup_printf("%u item%s", n, n == 1 ? "" : "s");
        myfm_card_add_value(card1, "Contents", c, FALSE);
        g_free(c);
    } else {
        char *sz = format_size_prop((guint64)st.st_size);
        myfm_card_add_value(card1, "Size", sz, FALSE);
        g_free(sz);
    }

    myfm_card_add_value(card1, "Parent folder", parent_dir, FALSE);

    if (is_link) {
        char target[4096] = { 0 };
        ssize_t len = readlink(path, target, sizeof(target) - 1);
        if (len > 0) {
            target[len] = '\0';
            myfm_card_add_value(card1, "Link target", target, FALSE);
        }
    }

    struct statvfs sv;
    if (statvfs(path, &sv) == 0 && sv.f_blocks > 0) {
        guint64 free_b = (guint64)sv.f_bavail * sv.f_frsize;
        char *fs = myfm_format_size(free_b);
        myfm_card_add_value(card1, "Free space", fs, FALSE);
        g_free(fs);
    }

    gtk_box_append(GTK_BOX(page), card1);

    /* ── times ── */
    gtk_box_append(GTK_BOX(page), myfm_group_title("Times"));
    GtkWidget *card2 = myfm_card_new();

    char *t_mod = format_time_prop(st.st_mtime);
    char *t_acc = format_time_prop(st.st_atime);
    char *t_chg = format_time_prop(st.st_ctime);
    myfm_card_add_value(card2, "Modified", t_mod, FALSE);
    myfm_card_add_value(card2, "Accessed", t_acc, FALSE);
    myfm_card_add_value(card2, "Changed",  t_chg, FALSE);
    g_free(t_mod);
    g_free(t_acc);
    g_free(t_chg);

    gtk_box_append(GTK_BOX(page), card2);

    /* ── permissions ── */
    gtk_box_append(GTK_BOX(page), myfm_group_title("Permissions"));

    PermUI *pu = g_new0(PermUI, 1);
    pu->path   = g_strdup(path);
    pu->is_dir = is_dir;
    pu->mode   = st.st_mode;
    pu->window = win;

    GtkWidget *card3 = build_permissions_card(pu, &st);
    g_object_set_data_full(G_OBJECT(win), "perm-ui", pu, perm_ui_free);
    gtk_box_append(GTK_BOX(page), card3);

    /* ── details ── */
    gtk_box_append(GTK_BOX(page), myfm_group_title("Details"));
    GtkWidget *card4 = myfm_card_new();

    myfm_card_add_value(card4, "Full path", path, FALSE);
    if (!is_dir) myfm_card_add_value(card4, "Type", type_desc, FALSE);

    char *inode = g_strdup_printf("%lu", (unsigned long)st.st_ino);
    char *links = g_strdup_printf("%lu", (unsigned long)st.st_nlink);
    char *disk  = myfm_format_size((guint64)st.st_blocks * 512ULL);
    myfm_card_add_value(card4, "Inode",      inode, TRUE);
    myfm_card_add_value(card4, "Hard links", links, TRUE);
    myfm_card_add_value(card4, "Disk usage", disk,  FALSE);
    g_free(inode);
    g_free(links);
    g_free(disk);

    gtk_box_append(GTK_BOX(page), card4);

    g_free(type_desc);
    g_free(base);
    g_free(parent_dir);

    gtk_window_present(GTK_WINDOW(win));
}


/* ════════════════════════════════════════════════════════════
 * THEME ENGINE
 * ════════════════════════════════════════════════════════════
 *
 * One CSS template, filled in from a palette.  Every widget the app uses
 * is styled here, so the look does not depend on which GTK theme the
 * desktop happens to use (Adwaita on GNOME, Breeze on KDE, nothing at all
 * on a bare compositor…).
 */

typedef struct {
    const char *key;
    const char *value;
} PaletteEntry;

/* Windows 10 File Explorer, light */
static const PaletteEntry PAL_LIGHT[] = {
    { "bg",        "#ffffff" },   /* file list / window          */
    { "chrome",    "#ffffff" },   /* title bar                   */
    { "toolbar",   "#f5f6f7" },   /* command bar (ribbon)        */
    { "nav",       "#ffffff" },   /* navigation pane             */
    { "fg",        "#1a1a1a" },
    { "dim",       "#6d6d6d" },
    { "faint",     "#a0a0a0" },
    { "border",    "#e5e5e5" },
    { "border2",   "#d9d9d9" },
    { "hover",     "#e5f3ff" },
    { "sel",       "#cce8ff" },
    { "selb",      "#99d1ff" },
    { "selhov",    "#d9eeff" },
    { "accent",    "#0078d7" },
    { "accentfg",  "#ffffff" },
    { "accenthov", "#1a86d9" },
    { "input",     "#ffffff" },
    { "menu",      "#f2f2f2" },
    { "menub",     "#cccccc" },
    { "menuhov",   "#91c9f7" },
    { "btnhov",    "#e5f3ff" },
    { "btnhovb",   "#cce4f7" },
    { "btnact",    "#cce4f7" },
    { "slider",    "#c2c2c2" },
    { "sliderhov", "#a6a6a6" },
    { "danger",    "#e81123" },
    { "lock",      "#c42b1c" },
    { "track",     "#e6e6e6" },
    { "dialog",    "#ffffff" },
    { "card",      "#fbfbfb" },
    { NULL, NULL }
};

/* Windows 10 File Explorer, dark */
static const PaletteEntry PAL_DARK[] = {
    { "bg",        "#191919" },
    { "chrome",    "#202020" },
    { "toolbar",   "#2b2b2b" },
    { "nav",       "#191919" },
    { "fg",        "#ffffff" },
    { "dim",       "#a6a6a6" },
    { "faint",     "#6e6e6e" },
    { "border",    "#2f2f2f" },
    { "border2",   "#454545" },
    { "hover",     "#2d2d2d" },
    { "sel",       "#4d4d4d" },
    { "selb",      "#6b6b6b" },
    { "selhov",    "#575757" },
    { "accent",    "#0078d7" },
    { "accentfg",  "#ffffff" },
    { "accenthov", "#1a86d9" },
    { "input",     "#2b2b2b" },
    { "menu",      "#2b2b2b" },
    { "menub",     "#4a4a4a" },
    { "menuhov",   "#414141" },
    { "btnhov",    "#3a3a3a" },
    { "btnhovb",   "#4a4a4a" },
    { "btnact",    "#4a4a4a" },
    { "slider",    "#5a5a5a" },
    { "sliderhov", "#7a7a7a" },
    { "danger",    "#e81123" },
    { "lock",      "#ff99a4" },
    { "track",     "#3a3a3a" },
    { "dialog",    "#202020" },
    { "card",      "#262626" },
    { NULL, NULL }
};

/* the original MyFM look */
static const PaletteEntry PAL_PURPLE[] = {
    { "bg",        "#1b1729" },
    { "chrome",    "#14111f" },
    { "toolbar",   "#1a1628" },
    { "nav",       "#171322" },
    { "fg",        "#e8e3f5" },
    { "dim",       "#8d85a8" },
    { "faint",     "#574f70" },
    { "border",    "#262038" },
    { "border2",   "#2d2747" },
    { "hover",     "#221d33" },
    { "sel",       "#352c58" },
    { "selb",      "#4b4374" },
    { "selhov",    "#3d3266" },
    { "accent",    "#6d4ed6" },
    { "accentfg",  "#ffffff" },
    { "accenthov", "#7e5ff0" },
    { "input",     "#1d1930" },
    { "menu",      "#231d38" },
    { "menub",     "#352d55" },
    { "menuhov",   "#342c54" },
    { "btnhov",    "#272038" },
    { "btnhovb",   "#352d55" },
    { "btnact",    "#322a4c" },
    { "slider",    "#3a3358" },
    { "sliderhov", "#4b4374" },
    { "danger",    "#c04a63" },
    { "lock",      "#ff8fab" },
    { "track",     "#2c2544" },
    { "dialog",    "#1b1729" },
    { "card",      "#211c33" },
    { NULL, NULL }
};

static const char *MYFM_CSS_TEMPLATE =
    /* ── base: compact, Segoe-like, square ── */
    "window.myfm, window.myfm-dialog, window.props {"
    "  font-family: \"Segoe UI\", \"Noto Sans\", \"DejaVu Sans\", \"Liberation Sans\", sans-serif;"
    "  font-size: 12px; background-color: {bg}; color: {fg};"
    "}"
    "window.myfm, window.props, window.myfm-dialog { border-radius: 0; }"
    "window.myfm.csd, window.props.csd, window.myfm-dialog.csd {"
    "  border-radius: 0; box-shadow: 0 0 0 1px {border2}, 0 2px 10px rgba(0,0,0,0.25);"
    "}"
    "label { color: inherit; }"

    /* ── title bar + Windows 10 caption buttons ── */
    "window.myfm headerbar, window.props headerbar, window.myfm-dialog headerbar {"
    "  background: {chrome}; color: {fg}; border: none; box-shadow: none;"
    "  min-height: 30px; padding: 0; margin: 0; border-radius: 0;"
    "}"
    "window.myfm headerbar:backdrop, window.props headerbar:backdrop,"
    "window.myfm-dialog headerbar:backdrop { color: {faint}; }"
    ".title-icon { margin-left: 8px; margin-right: 6px; }"
    ".title-text { font-size: 12px; color: {fg}; }"
    "headerbar:backdrop .title-text { color: {faint}; }"
    "windowcontrols { margin: 0; padding: 0; border-spacing: 0; }"
    "windowcontrols > button {"
    "  min-width: 44px; min-height: 30px; margin: 0; padding: 0;"
    "  border-radius: 0; border: none; box-shadow: none;"
    "  background: transparent; color: {fg};"
    "}"
    "windowcontrols > button > image {"
    "  background: none; box-shadow: none; border-radius: 0;"
    "  padding: 0; margin: 0; color: inherit; -gtk-icon-size: 12px;"
    "}"
    "windowcontrols > button:hover  { background: {btnhov}; }"
    "windowcontrols > button:active { background: {btnact}; }"
    "windowcontrols > button.close:hover  { background: #e81123; color: #ffffff; }"
    "windowcontrols > button.close:active { background: #f1707a; color: #ffffff; }"

    /* ── generic buttons ── */
    "button {"
    "  background: transparent; color: {fg}; border: 1px solid transparent;"
    "  border-radius: 0; box-shadow: none; padding: 2px 6px; min-height: 20px;"
    "  text-shadow: none; -gtk-icon-shadow: none; outline-color: {accent};"
    "}"
    "button:hover    { background: {btnhov}; border-color: {btnhovb}; }"
    "button:active, button:checked { background: {btnact}; border-color: {btnhovb}; }"
    "button:disabled { color: {faint}; background: transparent; border-color: transparent; }"
    "button:disabled label { color: {faint}; }"

    /* ── command bar (ribbon-lite) ── */
    ".commandbar {"
    "  background: {toolbar}; border-bottom: 1px solid {border2};"
    "  padding: 2px 4px; min-height: 26px;"
    "}"
    ".cmd-btn { padding: 1px 6px; min-height: 22px; }"
    ".cmd-btn-icon { margin-right: 4px; }"
    ".cmd-sep { background: {border2}; min-width: 1px; margin: 3px 4px; }"
    "menubutton > button { padding: 1px 6px; min-height: 22px; }"
    "menubutton.cmd-accent > button { color: {accent}; font-weight: bold; }"
    "menubutton > button:disabled label { color: {faint}; }"
    ".view-btn { min-width: 22px; min-height: 22px; padding: 0 3px; }"
    ".view-btn-active { background: {sel}; border-color: {selb}; }"

    /* ── address row ── */
    ".addressbar { background: {bg}; padding: 4px 4px; border-bottom: 1px solid {border}; }"
    ".nav-btn { min-width: 24px; min-height: 22px; padding: 0 2px; font-size: 14px; }"
    ".crumb-bar {"
    "  background: {input}; border: 1px solid {border2}; border-radius: 0;"
    "  padding: 0 2px; min-height: 22px;"
    "}"
    ".crumb-bar:hover { border-color: {accent}; }"
    ".crumb-btn { padding: 0 4px; min-height: 20px; font-size: 12px; }"
    ".crumb-btn-last { font-weight: normal; }"
    ".crumb-sep { color: {dim}; font-size: 11px; margin: 0 1px; }"
    ".crumb-refresh { min-width: 22px; min-height: 20px; padding: 0; border-left: 1px solid {border}; }"

    /* ── entries ── */
    "entry, .text-entry, .search-entry, spinbutton {"
    "  background: {input}; color: {fg}; caret-color: {fg};"
    "  border: 1px solid {border2}; border-radius: 0; box-shadow: none;"
    "  min-height: 22px; padding: 0 6px; outline: none;"
    "}"
    "entry:focus-within, spinbutton:focus-within { border-color: {accent}; }"
    "entry text, spinbutton text { color: {fg}; }"
    "entry placeholder, entry > text > placeholder { color: {faint}; }"
    "entry image { color: {dim}; }"
    "spinbutton > button { border-radius: 0; border: none; min-width: 20px; }"
    "selection, entry selection, label selection, text selection {"
    "  background: {accent}; color: {accentfg};"
    "}"

    /* ── navigation pane ── */
    ".sidebar { background: {nav}; padding: 4px 0; }"
    ".sidebar-title { color: {dim}; font-size: 11px; padding: 6px 0 2px 10px; }"
    ".sidebar-btn {"
    "  padding: 1px 8px 1px 14px; min-height: 22px; border-radius: 0;"
    "  border: 1px solid transparent; margin: 0 2px;"
    "}"
    ".sidebar-btn:hover { background: {hover}; border-color: transparent; }"
    ".sidebar-btn.current { background: {sel}; border-color: {selb}; }"
    "paned > separator {"
    "  background: {border}; background-image: none; border: none; box-shadow: none;"
    "  min-width: 1px; min-height: 1px; margin: 0; padding: 0;"
    "}"

    /* ── content ── */
    ".content-area { background: {bg}; }"
    ".file-view { background: {bg}; padding: 0 2px 8px 2px; }"
    ".col-header { background: {bg}; padding: 0; margin-bottom: 1px; }"
    ".col-btn {"
    "  color: {dim}; padding: 1px 6px; min-height: 22px;"
    "  border: none; border-right: 1px solid {border};"
    "}"
    ".col-btn:hover { background: {hover}; color: {fg}; border-color: {border}; }"

    ".file-row {"
    "  border: 1px solid transparent; border-radius: 0;"
    "  padding: 0 4px; min-height: 22px; color: {fg};"
    "}"
    ".file-row:hover { background: {hover}; }"
    ".file-label      { color: {fg}; }"
    ".file-label-root { color: {lock}; }"
    ".file-meta       { color: {dim}; }"
    ".item-selected, .item-selected:hover {"
    "  background: {sel}; border: 1px solid {selb};"
    "}"
    ".item-selected:hover { background: {selhov}; }"

    ".grid-flow { background: transparent; }"
    "flowboxchild { padding: 0; border-radius: 0; background: transparent; outline: none; }"
    ".grid-tile {"
    "  border: 1px solid transparent; border-radius: 0; padding: 4px 2px;"
    "}"
    ".grid-tile:hover { background: {hover}; }"
    ".grid-icon  { font-size: 30px; }"
    ".grid-label { color: {fg}; font-size: 12px; }"
    ".grid-lock  { font-size: 9px; }"

    /* ── This PC ── */
    ".section-title {"
    "  color: {fg}; font-size: 12px; font-weight: normal;"
    "  padding-bottom: 2px; border-bottom: 1px solid {border};"
    "}"
    ".drive-tile {"
    "  border: 1px solid transparent; border-radius: 0; padding: 4px 6px;"
    "}"
    ".drive-tile:hover { background: {hover}; }"
    ".drive-icon  { font-size: 28px; }"
    ".drive-name  { color: {fg}; font-size: 12px; }"
    ".drive-path  { color: {faint}; font-size: 11px; }"
    ".drive-space { color: {dim}; font-size: 11px; }"
    ".drive-unmounted { color: {faint}; font-size: 11px; font-style: italic; }"
    "progressbar trough {"
    "  background: {track}; border: 1px solid {border2}; border-radius: 0; min-height: 12px;"
    "}"
    "progressbar progress {"
    "  background: #26a0da; border-radius: 0; min-height: 12px; border: none;"
    "  margin: 0; min-width: 0;"
    "}"
    "progressbar.progress-full progress { background: #da2626; }"
    ".eject-btn { min-width: 22px; min-height: 22px; padding: 0; }"

    ".info-label { color: {dim}; font-size: 12px; }"

    /* ── status bar ── */
    ".statusbar {"
    "  background: {bg}; border-top: 1px solid {border};"
    "  padding: 1px 8px; min-height: 20px;"
    "}"
    ".status-label { color: {fg}; font-size: 11px; }"

    /* ── scrollbars (thin, square) ── */
    "scrollbar { background: transparent; border: none; }"
    "scrollbar slider {"
    "  background: {slider}; border-radius: 0; border: none;"
    "  min-width: 6px; min-height: 6px;"
    "}"
    "scrollbar slider:hover  { background: {sliderhov}; }"

    /* ── popovers, menus, dropdowns, tooltips ── */
    "popover > contents {"
    "  background: {menu}; color: {fg}; border: 1px solid {menub};"
    "  border-radius: 0; padding: 2px; box-shadow: 2px 2px 4px rgba(0,0,0,0.25);"
    "}"
    "popover > arrow { background: {menu}; border: 1px solid {menub}; }"
    "popover listview, popover list, popover scrolledwindow, popover viewport {"
    "  background: transparent; color: {fg};"
    "}"
    "popover listview > row, popover list > row, popover modelbutton {"
    "  background: transparent; color: {fg}; border-radius: 0;"
    "  padding: 2px 8px; min-height: 20px;"
    "}"
    "popover listview > row:hover, popover list > row:hover,"
    "popover listview > row:selected, popover list > row:selected,"
    "popover modelbutton:hover { background: {menuhov}; }"
    ".context-item {"
    "  border: none; border-radius: 0; padding: 2px 16px 2px 22px;"
    "  min-width: 180px; min-height: 20px;"
    "}"
    ".context-item:hover { background: {menuhov}; border: none; }"
    ".ctx-icon  { min-width: 20px; }"
    "separator { background: {border2}; min-height: 1px; min-width: 1px; }"
    "tooltip {"
    "  background: {menu}; color: {fg}; border: 1px solid {menub}; border-radius: 0;"
    "  box-shadow: none; padding: 2px 4px;"
    "}"
    "tooltip label { color: {fg}; }"
    "dropdown > button { border: 1px solid {border2}; background: {input}; }"
    "dropdown > button:hover { border-color: {accent}; }"

    /* ── checks, switches, text view ── */
    "checkbutton check {"
    "  min-width: 12px; min-height: 12px; border-radius: 0;"
    "  border: 1px solid {dim}; background: {input}; color: {accentfg};"
    "  box-shadow: none;"
    "}"
    "checkbutton check:checked { background: {accent}; border-color: {accent}; }"
    ".sel-checkbox { margin-right: 2px; padding: 0; }"
    "switch {"
    "  background: {input}; border: 1px solid {dim}; border-radius: 10px;"
    "  box-shadow: none; min-height: 18px;"
    "}"
    "switch:checked { background: {accent}; border-color: {accent}; }"
    "switch > slider {"
    "  background: {dim}; border: none; box-shadow: none;"
    "  min-width: 12px; min-height: 12px; margin: 3px;"
    "}"
    "switch:checked > slider { background: {accentfg}; }"
    "textview, textview text { background: {bg}; color: {fg}; }"

    /* ── dialogs ── */
    "window.myfm-dialog { background: {dialog}; }"
    ".dlg-icon  { font-size: 26px; }"
    ".dlg-title { color: {fg}; font-size: 14px; }"
    ".dlg-body  { color: {fg}; font-size: 12px; }"
    ".dlg-btn, .dlg-btn-accent, .dlg-btn-danger {"
    "  min-width: 70px; min-height: 22px; padding: 1px 10px; border-radius: 0;"
    "  background: {toolbar}; border: 1px solid {border2}; color: {fg};"
    "}"
    ".dlg-btn:hover, .dlg-btn-accent:hover, .dlg-btn-danger:hover {"
    "  background: {btnhov}; border-color: {accent};"
    "}"
    ".dlg-btn-accent { border-color: {accent}; }"
    ".dlg-btn-danger { border-color: {danger}; }"
    ".dlg-btn-danger:hover { background: {danger}; color: #ffffff; border-color: {danger}; }"

    /* ── properties / settings ── */
    "window.props { background: {dialog}; }"
    ".prop-title    { color: {fg}; font-size: 15px; }"
    ".prop-subtitle { color: {dim}; font-size: 11px; }"
    ".prop-group-title { color: {accent}; font-size: 12px; margin: 10px 2px 4px 2px; }"
    ".prop-card { background: {card}; border: 1px solid {border}; border-radius: 0; }"
    ".prop-row   { padding: 4px 10px; min-height: 22px; }"
    ".prop-label { color: {fg}; }"
    ".prop-value { color: {dim}; }"
    ".prop-mono  { font-family: monospace; font-size: 11px; }"

    /* ── rubber band ── */
    ".marquee { background: alpha({accent}, 0.15); border: 1px solid {accent}; }"

    /* ── preview pane ── */
    ".preview-box   { background: {bg}; }"
    ".preview-title { color: {fg}; font-size: 13px; }"
    ".preview-meta  { color: {dim}; font-size: 11px; }"
    ".preview-text, .preview-text text { background: {bg}; color: {fg}; font-size: 11px; }"

    /* ── animations (only used when enabled in Settings) ── */
    ".anim-fade-start { opacity: 0; }"
    ".anim-fade-in    { opacity: 1; transition: opacity 120ms ease-out; }"
    ".anim-slide-start{ opacity: 0; }"
    ".anim-slide-in   { opacity: 1; transition: opacity 120ms ease-out; }"
    ".anim-row-start  { opacity: 0; }"
    ".anim-row-in     { opacity: 1; transition: opacity 120ms ease-out; }"
    ".anim-pulse      { }"
    ".anim-pulse-big  { }";

static GtkCssProvider *theme_provider = NULL;

/* What the desktop prefers.  GTK fills GtkSettings from the
 * xdg-desktop-portal "color-scheme" key, which GNOME, KDE and most
 * wlroots setups (via xdg-desktop-portal-gtk/-wlr) all provide. */
static gboolean
system_prefers_dark(void)
{
    GtkSettings *gs = gtk_settings_get_default();
    if (!gs) return FALSE;

    GObjectClass *klass = G_OBJECT_GET_CLASS(gs);
    if (g_object_class_find_property(klass, "gtk-interface-color-scheme")) {
        int scheme = 0;
        g_object_get(gs, "gtk-interface-color-scheme", &scheme, NULL);
        if (scheme == 2) return TRUE;    /* GTK_INTERFACE_COLOR_SCHEME_DARK  */
        if (scheme == 3) return FALSE;   /* GTK_INTERFACE_COLOR_SCHEME_LIGHT */
    }

    gboolean dark = FALSE;
    char *theme = NULL;
    if (g_object_class_find_property(klass, "gtk-application-prefer-dark-theme"))
        g_object_get(gs, "gtk-application-prefer-dark-theme", &dark, NULL);
    g_object_get(gs, "gtk-theme-name", &theme, NULL);
    if (theme && g_str_has_suffix(g_ascii_strdown(theme, -1), "dark"))
        dark = TRUE;
    g_free(theme);
    return dark;
}

gboolean
myfm_gui_theme_is_dark(void)
{
    switch (myfm_settings()->theme) {
        case THEME_LIGHT:  return FALSE;
        case THEME_DARK:
        case THEME_PURPLE: return TRUE;
        case THEME_SYSTEM:
        default:           return system_prefers_dark();
    }
}

static void
on_system_scheme_changed(GObject *o, GParamSpec *ps, gpointer ud)
{
    (void)o; (void)ps; (void)ud;
    if (myfm_settings()->theme == THEME_SYSTEM)
        myfm_gui_apply_theme();
}

void
myfm_gui_apply_theme(void)
{
    GdkDisplay *display = gdk_display_get_default();
    if (!display) return;

    if (!theme_provider) {
        theme_provider = gtk_css_provider_new();
        gtk_style_context_add_provider_for_display(display,
            GTK_STYLE_PROVIDER(theme_provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

        GtkSettings *gs = gtk_settings_get_default();
        if (gs) {
            GObjectClass *klass = G_OBJECT_GET_CLASS(gs);
            if (g_object_class_find_property(klass, "gtk-interface-color-scheme"))
                g_signal_connect(gs, "notify::gtk-interface-color-scheme",
                    G_CALLBACK(on_system_scheme_changed), NULL);
            g_signal_connect(gs, "notify::gtk-theme-name",
                G_CALLBACK(on_system_scheme_changed), NULL);
        }
    }

    const PaletteEntry *pal;
    switch (myfm_settings()->theme) {
        case THEME_LIGHT:  pal = PAL_LIGHT;  break;
        case THEME_DARK:   pal = PAL_DARK;   break;
        case THEME_PURPLE: pal = PAL_PURPLE; break;
        case THEME_SYSTEM:
        default: pal = system_prefers_dark() ? PAL_DARK : PAL_LIGHT; break;
    }

    GString *css = g_string_new(MYFM_CSS_TEMPLATE);
    for (int i = 0; pal[i].key; i++) {
        char *tok = g_strdup_printf("{%s}", pal[i].key);
        g_string_replace(css, tok, pal[i].value, 0);
        g_free(tok);
    }
    gtk_css_provider_load_from_string(theme_provider, css->str);
    g_string_free(css, TRUE);
}


/* ════════════════════════════════════════════════════════════
 * ICONS
 * ════════════════════════════════════════════════════════════
 *
 * "System icons" uses whatever icon theme is installed (Adwaita, Breeze,
 * Papirus…).  Any icon the theme lacks falls back to an emoji, so the
 * view never shows "missing image" placeholders on a minimal system.
 */

static GtkWidget *
emoji_label(const char *emoji, int pixel_size)
{
    GtkWidget *l = gtk_label_new(emoji);
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs,
        pango_attr_size_new_absolute((int)(pixel_size * 0.8) * PANGO_SCALE));
    gtk_label_set_attributes(GTK_LABEL(l), attrs);
    pango_attr_list_unref(attrs);
    gtk_widget_set_size_request(l, pixel_size, pixel_size);
    return l;
}

/* The file manager draws its icons from the Tela icon theme
 * (https://github.com/vinceliuice/Tela-icon-theme) through a private
 * GtkIconTheme: the desktop's own icon theme setting is left untouched,
 * and the rest of GTK (window buttons, dropdown arrows…) keeps using it.
 * Only icons actually shipped by Tela are used; anything else, or a
 * system without Tela, falls back to the emoji set. */
static GtkIconTheme *
tela_theme(void)
{
    static GtkIconTheme *theme = NULL;
    static gboolean tried = FALSE;
    if (tried) return theme;

    GdkDisplay *display = gdk_display_get_default();
    if (!display) return NULL;      /* try again once there is one */
    tried = TRUE;

    GtkIconTheme *t = g_object_new(GTK_TYPE_ICON_THEME,
        "display", display, "theme-name", "Tela", NULL);
    if (gtk_icon_theme_has_icon(t, "folder")) {
        GtkIconPaintable *ip = gtk_icon_theme_lookup_icon(t, "folder", NULL,
            16, 1, GTK_TEXT_DIR_NONE, 0);
        GFile *f = ip ? gtk_icon_paintable_get_file(ip) : NULL;
        char *path = f ? g_file_get_path(f) : NULL;
        if (path && strstr(path, "/Tela/"))
            theme = t;
        g_free(path);
        g_clear_object(&f);
        g_clear_object(&ip);
    }
    if (!theme) g_object_unref(t);
    return theme;
}

gboolean
myfm_tela_available(void)
{
    return tela_theme() != NULL;
}

static GtkWidget *
image_for_gicon(GIcon *gicon, int pixel_size)
{
    GtkIconTheme *theme = gicon ? tela_theme() : NULL;
    if (!theme) return NULL;

    /* Tela's 16/22/24 px "places" and "devices" are monochrome panel
     * icons; the colourful ones live in scalable/.  Asking for >= 32 px
     * selects those, and they are then drawn at pixel_size. */
    GtkIconPaintable *ip = gtk_icon_theme_lookup_by_gicon(theme, gicon,
        MAX(pixel_size, 32), 1, GTK_TEXT_DIR_NONE, 0);
    if (!ip) return NULL;

    GtkWidget *img = NULL;
    GFile *f = gtk_icon_paintable_get_file(ip);
    char *path = f ? g_file_get_path(f) : NULL;
    /* Tela inherits Adwaita/hicolor: refuse icons that came from those */
    if (path && strstr(path, "/Tela")) {
        /* Hand GtkImage the file itself so it renders it at the right
         * size for the screen's scale (sharp on HiDPI). */
        GIcon *fi = g_file_icon_new(f);
        img = gtk_image_new_from_gicon(fi);
        gtk_image_set_pixel_size(GTK_IMAGE(img), pixel_size);
        g_object_unref(fi);
    }
    g_free(path);
    g_clear_object(&f);
    g_object_unref(ip);
    return img;
}

GtkWidget *
myfm_icon_widget(const char *name, gboolean is_dir,
                 const char *content_type, int pixel_size)
{
    if (myfm_settings()->icon_style == ICONS_TELA) {
        GIcon *gicon = NULL;
        if (is_dir) {
            gicon = g_themed_icon_new("folder");
        } else {
            char *guessed = NULL;
            const char *ct = content_type;
            if (!ct && name) {
                guessed = g_content_type_guess(name, NULL, 0, NULL);
                ct = guessed;
            }
            gicon = ct ? g_content_type_get_icon(ct)
                       : g_themed_icon_new("text-x-generic");
            /* Tela has the generic icons even when not the specific one */
            if (G_IS_THEMED_ICON(gicon)) {
                g_themed_icon_append_name(G_THEMED_ICON(gicon), "application-x-generic");
                g_themed_icon_append_name(G_THEMED_ICON(gicon), "text-x-generic");
            }
            g_free(guessed);
        }
        GtkWidget *img = image_for_gicon(gicon, pixel_size);
        g_object_unref(gicon);
        if (img) return img;
    }
    return emoji_label(file_icon_for(name, is_dir), pixel_size);
}

GtkWidget *
myfm_named_icon_widget(const char *icon_name, const char *emoji, int pixel_size)
{
    if (icon_name && myfm_settings()->icon_style == ICONS_TELA) {
        GIcon *gicon = g_themed_icon_new(icon_name);
        GtkWidget *img = image_for_gicon(gicon, pixel_size);
        g_object_unref(gicon);
        if (img) return img;
    }
    return emoji_label(emoji ? emoji : "\xF0\x9F\x93\x84", pixel_size);
}


/* ════════════════════════════════════════════════════════════
 * SECONDARY WINDOWS
 * ════════════════════════════════════════════════════════════
 *
 * Dialogs, Properties, Open with and Settings are real top-level windows
 * of their own: no transient-for parent and not modal.  GNOME (Mutter)
 * glues modal transient dialogs to their parent, so dragging one moved
 * the whole program with it.  They are still registered with the
 * application (same app id / taskbar group), and a window that belongs to
 * a file-manager window closes together with it, so a dialog can never
 * outlive the window state its buttons act on.
 */

void
myfm_window_set_owner(GtkWidget *win, GtkWidget *parent)
{
    GApplication *app = g_application_get_default();
    if (app && GTK_IS_APPLICATION(app))
        gtk_window_set_application(GTK_WINDOW(win), GTK_APPLICATION(app));

    if (!parent) return;
    GtkRoot *root = gtk_widget_get_root(parent);
    if (!GTK_IS_WINDOW(root) || GTK_WIDGET(root) == win) return;
    g_signal_connect_object(root, "destroy",
        G_CALLBACK(gtk_window_destroy), win, G_CONNECT_SWAPPED);
}

/* Windows-style caption (title left, close right) — with it the window
 * can be grabbed and moved anywhere on the desktop. */
GtkWidget *
myfm_window_titlebar(GtkWidget *win, const char *title)
{
    GtkWidget *hb = gtk_header_bar_new();
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(hb), TRUE);
    gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(hb), ":close");
    GtkWidget *tl = gtk_label_new(title);
    gtk_widget_add_css_class(tl, "title-text");
    gtk_widget_set_margin_start(tl, 10);
    gtk_label_set_ellipsize(GTK_LABEL(tl), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_max_width_chars(GTK_LABEL(tl), 40);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), tl);
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(hb),
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
    gtk_window_set_titlebar(GTK_WINDOW(win), hb);
    return hb;
}


/* ════════════════════════════════════════════════════════════
 * PREVIEW PANE
 * ════════════════════════════════════════════════════════════ */


/* preview.c – right-hand preview pane: every file type shows *something*
 * useful. Images and (when poppler-glib is available) PDFs render an
 * actual thumbnail; text-like files — matched against the MIME type
 * hierarchy, not just a "text/" prefix, so JSON/XML/code files are
 * included — show their content; everything else (audio, video, fonts,
 * archives, binaries…) gets a icon-and-details card, so nothing ever
 * falls back to a bare "no preview" message. */

static void
load_text_preview(const char *path, GtkWidget *textview)
{
    const gsize CAP = 65536;
    char *buf = g_malloc(CAP);
    gssize n = -1;

    GFile *gf = g_file_new_for_path(path);
    GFileInputStream *in = g_file_read(gf, NULL, NULL);
    if (in) {
        n = g_input_stream_read(G_INPUT_STREAM(in), buf, CAP, NULL, NULL);
        g_object_unref(in);
    }
    g_object_unref(gf);

    GtkTextBuffer *tb = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
    if (n > 0) {
        const char *end = buf;
        if (!g_utf8_validate(buf, n, &end)) n = end - buf;
        gtk_text_buffer_set_text(tb, buf, (int)n);
    } else {
        gtk_text_buffer_set_text(tb, "(this file could not be previewed)", -1);
    }
    g_free(buf);
}

#ifdef MYFM_HAVE_POPPLER
static gboolean
render_pdf_preview(const char *path, GtkWidget *picture)
{
    GFile *gf = g_file_new_for_path(path);
    char *uri = g_file_get_uri(gf);
    g_object_unref(gf);

    GError *err = NULL;
    PopplerDocument *doc = poppler_document_new_from_file(uri, NULL, &err);
    g_free(uri);
    g_clear_error(&err);
    if (!doc) return FALSE;

    if (poppler_document_get_n_pages(doc) < 1) {
        g_object_unref(doc);
        return FALSE;
    }

    PopplerPage *page = poppler_document_get_page(doc, 0);
    double pw = 0, ph = 0;
    poppler_page_get_size(page, &pw, &ph);
    if (pw <= 0 || ph <= 0) {
        g_object_unref(page);
        g_object_unref(doc);
        return FALSE;
    }

    double scale = 480.0 / pw;
    int iw = MAX(1, (int)(pw * scale)), ih = MAX(1, (int)(ph * scale));

    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, iw, ih);
    cairo_t *cr = cairo_create(surf);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_scale(cr, scale, scale);
    poppler_page_render(page, cr);
    cairo_destroy(cr);
    cairo_surface_flush(surf);

    /* cairo ARGB32 is premultiplied native-endian, exactly what
     * GDK_MEMORY_DEFAULT describes: no GdkPixbuf round trip needed. */
    int stride = cairo_image_surface_get_stride(surf);
    GBytes *bytes = g_bytes_new(cairo_image_surface_get_data(surf),
                                (gsize)stride * (gsize)ih);
    GdkTexture *tex = gdk_memory_texture_new(iw, ih, GDK_MEMORY_DEFAULT,
                                             bytes, (gsize)stride);
    g_bytes_unref(bytes);
    cairo_surface_destroy(surf);
    g_object_unref(page);
    g_object_unref(doc);

    gtk_picture_set_paintable(GTK_PICTURE(picture), GDK_PAINTABLE(tex));
    g_object_unref(tex);
    return TRUE;
}
#endif

/* Generic fallback: a big file-type icon plus whatever details are known
 * (kind, modified date, permissions summary).  Used for every file type
 * that has no dedicated renderer above — audio, video, fonts, archives,
 * spreadsheets, binaries, and so on — so the preview pane always shows
 * *something*, never just an apology. */
static void
build_generic_info_card(AppState *st, const char *path, const char *ctype,
                        gboolean is_dir)
{
    GtkWidget *c;
    while ((c = gtk_widget_get_first_child(st->preview_info)) != NULL)
        gtk_box_remove(GTK_BOX(st->preview_info), c);

    char *base = g_path_get_basename(path);
    GtkWidget *icon = myfm_icon_widget(base, is_dir, is_dir ? NULL : ctype, 64);
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(icon, 18);
    gtk_widget_set_margin_bottom(icon, 6);
    gtk_box_append(GTK_BOX(st->preview_info), icon);
    g_free(base);

    GtkWidget *card = myfm_card_new();

    char *desc = ctype ? g_content_type_get_description(ctype) : NULL;
    myfm_card_add_value(card, "Type", desc ? desc : (ctype ? ctype : "Unknown"), FALSE);
    g_free(desc);

    struct stat st_buf;
    if (lstat(path, &st_buf) == 0) {
        GDateTime *dt = g_date_time_new_from_unix_local((gint64)st_buf.st_mtime);
        if (dt) {
            char *mt = g_date_time_format(dt, "%e %B %Y, %H:%M");
            myfm_card_add_value(card, "Modified", g_strstrip(mt), FALSE);
            g_free(mt);
            g_date_time_unref(dt);
        }
        char octal[8];
        g_snprintf(octal, sizeof(octal), "%04o", (unsigned)(st_buf.st_mode & 07777));
        myfm_card_add_value(card, "Permissions", octal, TRUE);
    }

    gtk_box_append(GTK_BOX(st->preview_info), card);
}

/* Refreshes the preview pane to reflect the current (single-item)
 * selection.  Cheap to call often: it no-ops when the pane isn't built
 * yet or is hidden, except it still clears stale content when nothing
 * is selected. */
void
myfm_update_preview(AppState *st)
{
    if (!st->preview_stack) return;

    if (!st->preview_visible) {
        g_clear_pointer(&st->preview_path, g_free);
        return;
    }

    const char *path = (st->sel_items->len == 1 &&
                         st->selected_kind == ITEM_FILE && st->selected_path)
        ? st->selected_path : NULL;

    /* Called on every selection change, including each motion event of a
     * rubber-band drag: don't re-read / re-render (PDF!) the same file. */
    if (path && st->preview_path && !strcmp(path, st->preview_path))
        return;
    g_free(st->preview_path);
    st->preview_path = g_strdup(path);

    if (!path) {
        gtk_stack_set_visible_child_name(GTK_STACK(st->preview_stack), "empty");
        gtk_label_set_text(GTK_LABEL(st->preview_title),
            st->sel_items->len > 1 ? "Multiple items selected" : "");
        gtk_label_set_text(GTK_LABEL(st->preview_meta), "");
        return;
    }

    char *base = g_path_get_basename(path);
    gtk_label_set_text(GTK_LABEL(st->preview_title), base);
    g_free(base);

    GFile *gf = g_file_new_for_path(path);
    GFileInfo *fi = g_file_query_info(gf,
        G_FILE_ATTRIBUTE_STANDARD_SIZE "," G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
        G_FILE_QUERY_INFO_NONE, NULL, NULL);
    guint64 size = fi ? (guint64)g_file_info_get_size(fi) : 0;
    const char *raw_ctype = fi ? g_file_info_get_content_type(fi) : NULL;
    char *ctype = raw_ctype ? g_content_type_get_mime_type(raw_ctype) : NULL;

    char *sz = myfm_format_size(size);
    char *meta = g_strdup_printf("%s%s%s", sz, ctype ? "  Â·  " : "", ctype ? ctype : "");
    gtk_label_set_text(GTK_LABEL(st->preview_meta), meta);
    g_free(sz);
    g_free(meta);

    /* Very large images are decoded synchronously by GtkPicture and can
     * freeze the window for seconds; show the details card instead. */
    gboolean shown = FALSE;
    if (ctype && g_str_has_prefix(ctype, "image/") && size <= 40u * 1024 * 1024) {
        gtk_picture_set_file(GTK_PICTURE(st->preview_pic), gf);
        gtk_stack_set_visible_child_name(GTK_STACK(st->preview_stack), "image");
        shown = TRUE;
    } else if (ctype && !strcmp(ctype, "application/pdf")) {
#ifdef MYFM_HAVE_POPPLER
        if (render_pdf_preview(path, st->preview_pic)) {
            gtk_stack_set_visible_child_name(GTK_STACK(st->preview_stack), "image");
            shown = TRUE;
        }
#endif
    } else if (raw_ctype && g_content_type_is_a(raw_ctype, "text/plain")) {
        /* Catches everything shared-mime-info considers a text subtype —
         * JSON, XML, source code, markdown, logs — not just files whose
         * MIME string happens to start with "text/". */
        load_text_preview(path, st->preview_text);
        gtk_stack_set_visible_child_name(GTK_STACK(st->preview_stack), "text");
        shown = TRUE;
    }

    if (!shown) {
        build_generic_info_card(st, path, ctype ? ctype : raw_ctype,
                                st->selected_is_dir);
        gtk_stack_set_visible_child_name(GTK_STACK(st->preview_stack), "info");
    }

    g_free(ctype);
    g_clear_object(&fi);
    g_object_unref(gf);
}

/* Shows / hides the pane in one window without touching the saved
 * preference (used when settings are pushed to every window). */
static void
set_preview_visible(AppState *st, gboolean visible)
{
    st->preview_visible = visible;
    if (st->preview_box)
        gtk_widget_set_visible(st->preview_box, visible);
    if (st->cmd_preview) {
        if (visible) gtk_widget_add_css_class(st->cmd_preview, "view-btn-active");
        else         gtk_widget_remove_css_class(st->cmd_preview, "view-btn-active");
    }
    myfm_update_preview(st);
}

/* Ctrl+P / toolbar: like Explorer, the pane state is remembered and
 * applies to every window. */
void
myfm_toggle_preview(AppState *st)
{
    myfm_settings()->preview_visible = !st->preview_visible;
    myfm_settings_save();
    myfm_settings_apply_all();
}


/* ════════════════════════════════════════════════════════════
 * MAIN WINDOW
 * ════════════════════════════════════════════════════════════ */

/* Column widths of the "details" (list) view; must match main.c rows */
#define ICON_W  18
#define CHECK_W 18
#define DATE_W 130
#define TYPE_W 120
#define SIZE_W  80

void
myfm_toggle_checkboxes(AppState *st)
{
    myfm_settings()->show_checkboxes = !st->show_checkboxes;
    myfm_settings_save();
    myfm_settings_apply_all();
}

/* Pushes the saved preferences into one window (called for every window
 * by myfm_settings_apply_all()).  Only rebuilds the listing when
 * something that affects it actually changed. */
void
myfm_gui_sync_settings(AppState *st, gboolean icons_changed)
{
    MyfmSettings *s = myfm_settings();
    gboolean relist = icons_changed;

    if (st->view_mode != s->view_mode) {
        myfm_apply_view_mode(st, s->view_mode);
        relist = TRUE;
    }

    if (st->show_hidden != s->show_hidden) {
        st->show_hidden = s->show_hidden;
        relist = TRUE;
    }
    if (st->show_checkboxes != s->show_checkboxes) {
        st->show_checkboxes = s->show_checkboxes;
        relist = TRUE;
    }
    if (st->preview_visible != s->preview_visible)
        set_preview_visible(st, s->preview_visible);
    else if (icons_changed) {
        g_clear_pointer(&st->preview_path, g_free);
        myfm_update_preview(st);
    }

    if (relist) refresh_view(st);
}

/* Settings > "Apply to this window": resize the window right away. */
void
myfm_gui_apply_window_size(AppState *st)
{
    MyfmSettings *s = myfm_settings();
    if (gtk_window_is_maximized(GTK_WINDOW(st->window)))
        gtk_window_unmaximize(GTK_WINDOW(st->window));
    gtk_window_set_default_size(GTK_WINDOW(st->window), s->win_width, s->win_height);
}


/* ── small callbacks ── */

static void on_view_list(GtkButton *b, gpointer ud)
{ (void)b; set_view_mode((AppState *)ud, VIEW_LIST); }

static void on_view_grid(GtkButton *b, gpointer ud)
{ (void)b; set_view_mode((AppState *)ud, VIEW_GRID); }

static void on_refresh(GtkButton *b, gpointer ud)
{ (void)b; refresh_view((AppState *)ud); }

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

static void on_open_settings(GtkButton *b, gpointer ud)
{ (void)b; myfm_open_settings(((AppState *)ud)->window); }


/* ── menu actions ── */
enum {
    ACT_NEW_FOLDER, ACT_NEW_FILE,
    ACT_SORT_NAME, ACT_SORT_DATE, ACT_SORT_TYPE, ACT_SORT_SIZE,
    ACT_SORT_DIR,
    ACT_VIEW_LIST, ACT_VIEW_GRID, ACT_HIDDEN,
    ACT_TERMINAL, ACT_DELETE, ACT_SELECT_ALL,
    ACT_PREVIEW, ACT_CHECKBOXES, ACT_SETTINGS
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
        case ACT_SELECT_ALL: myfm_select_all(st);                break;
        case ACT_PREVIEW:    myfm_toggle_preview(st);            break;
        case ACT_CHECKBOXES: myfm_toggle_checkboxes(st);         break;
        case ACT_SETTINGS:   myfm_open_settings(st->window);     break;
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
    if (css) gtk_widget_add_css_class(mb, css);
    return mb;
}

static GtkWidget *
popover_box(GtkWidget *pop)
{
    gtk_popover_set_has_arrow(GTK_POPOVER(pop), FALSE);
    GtkWidget *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_popover_set_child(GTK_POPOVER(pop), vb);
    return vb;
}


/* ── column header ── */

static void
on_col_clicked(GtkButton *b, gpointer ud)
{
    AppState *st = ud;
    int col = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "col"));
    myfm_set_sort(st, (SortColumn)col);
}

static GtkWidget *
col_button(AppState *st, const char *label, SortColumn col,
           int width, gboolean expand)
{
    GtkWidget *b = gtk_button_new_with_label(label);
    gtk_widget_add_css_class(b, "col-btn");
    if (width > 0) gtk_widget_set_size_request(b, width, -1);
    gtk_widget_set_hexpand(b, expand);
    gtk_widget_set_halign(b, GTK_ALIGN_FILL);
    gtk_widget_set_focus_on_click(b, FALSE);

    GtkWidget *child = gtk_button_get_child(GTK_BUTTON(b));
    if (GTK_IS_LABEL(child))
        gtk_label_set_xalign(GTK_LABEL(child), 0.0f);

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
                st->sort_desc ? "\xE2\x8C\x84" : "\xE2\x8C\x83")
            : g_strdup(names[i]);

        gtk_button_set_label(GTK_BUTTON(st->col_btn[i]), label);
        g_free(label);

        GtkWidget *child = gtk_button_get_child(GTK_BUTTON(st->col_btn[i]));
        if (GTK_IS_LABEL(child))
            gtk_label_set_xalign(GTK_LABEL(child), i == 3 ? 1.0f : 0.0f);
    }

    /* keep the header aligned with rows that have a checkbox column */
    GtkWidget *check_sp = g_object_get_data(G_OBJECT(st->column_header), "check-spacer");
    if (check_sp) gtk_widget_set_visible(check_sp, st->show_checkboxes);
}

static GtkWidget *
build_column_header(AppState *st)
{
    /* Same geometry as a list row (see build_list_row in main.c): view
     * padding 2 + row margin 2 + border 1 + padding 4 = 9px, then
     * [check] icon name date type size with 6px spacing; each column
     * button carries 6px of padding, hence the "+ 6" widths. */
    GtkWidget *hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(hdr, "col-header");
    gtk_widget_set_margin_start(hdr, 9);
    gtk_widget_set_margin_end(hdr, 9);

    GtkWidget *check_sp = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_size_request(check_sp, CHECK_W + 6, -1);
    g_object_set_data(G_OBJECT(hdr), "check-spacer", check_sp);
    gtk_box_append(GTK_BOX(hdr), check_sp);

    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_size_request(spacer, ICON_W, -1);
    gtk_box_append(GTK_BOX(hdr), spacer);

    gtk_box_append(GTK_BOX(hdr), col_button(st, "Name", SORT_NAME, -1, TRUE));
    gtk_box_append(GTK_BOX(hdr), col_button(st, "Date modified", SORT_MTIME, DATE_W + 6, FALSE));
    gtk_box_append(GTK_BOX(hdr), col_button(st, "Type", SORT_TYPE, TYPE_W + 6, FALSE));
    gtk_box_append(GTK_BOX(hdr), col_button(st, "Size", SORT_SIZE, SIZE_W + 6, FALSE));

    return hdr;
}


/* ── path bar (breadcrumbs <-> editable entry) ── */

static void
path_edit_mode(AppState *st, gboolean edit)
{
    gtk_stack_set_visible_child_name(GTK_STACK(st->path_stack),
        edit ? "entry" : "crumbs");
    if (edit) {
        gtk_widget_grab_focus(st->path_entry);
        gtk_editable_select_region(GTK_EDITABLE(st->path_entry), 0, -1);
    } else if (st->addr_popover) {
        gtk_popover_popdown(GTK_POPOVER(st->addr_popover));
    }
}

static void
on_crumbbar_pressed(GtkGestureClick *gc, int n, double x, double y, gpointer ud)
{
    (void)n;
    AppState *st = ud;
    /* clicks on a crumb button navigate; only the empty part of the
     * bar switches to typing mode, like Explorer */
    GtkWidget *bar = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gc));
    GtkWidget *picked = gtk_widget_pick(bar, x, y, GTK_PICK_DEFAULT);
    for (GtkWidget *p = picked; p && p != bar; p = gtk_widget_get_parent(p))
        if (GTK_IS_BUTTON(p)) return;
    path_edit_mode(st, TRUE);
}

static void
clear_addr_popover_box(AppState *st)
{
    GtkWidget *c;
    while ((c = gtk_widget_get_first_child(st->addr_popover_box)) != NULL)
        gtk_box_remove(GTK_BOX(st->addr_popover_box), c);
}

static void
addr_suggestion_clicked(GtkWidget *btn, gpointer ud)
{
    AppState *st = ud;
    const char *full = g_object_get_data(G_OBJECT(btn), "full-path");
    if (!full) return;

    st->addr_updating = TRUE;
    gtk_editable_set_text(GTK_EDITABLE(st->path_entry), full);
    gtk_editable_set_position(GTK_EDITABLE(st->path_entry), -1);
    st->addr_updating = FALSE;

    gtk_popover_popdown(GTK_POPOVER(st->addr_popover));
    gtk_widget_grab_focus(st->path_entry);
}

static int
addr_match_cmp(gconstpointer a, gconstpointer b)
{
    const char *sa = *(const char * const *)a;
    const char *sb = *(const char * const *)b;
    return g_ascii_strcasecmp(sa, sb);
}

/* Lists folders (and files) under the directory the user is currently
 * typing, matching whatever partial name comes after the last "/", and
 * shows them in a popover under the address bar. */
static void
on_path_entry_changed(GtkEditable *e, gpointer ud)
{
    AppState *st = ud;
    if (st->addr_updating) return;

    const char *raw = gtk_editable_get_text(e);
    clear_addr_popover_box(st);

    if (!raw || !raw[0] ||
        strcmp(gtk_stack_get_visible_child_name(GTK_STACK(st->path_stack)), "entry") != 0) {
        gtk_popover_popdown(GTK_POPOVER(st->addr_popover));
        return;
    }

    char *expanded;
    if (g_str_has_prefix(raw, "~/"))
        expanded = g_build_filename(g_get_home_dir(), raw + 2, NULL);
    else if (!strcmp(raw, "~"))
        expanded = g_strdup(g_get_home_dir());
    else
        expanded = g_strdup(raw);

    char *slash = g_strrstr(expanded, "/");
    char *dir_part, *prefix;
    if (slash) {
        dir_part = g_strndup(expanded, (gsize)(slash - expanded));
        if (dir_part[0] == '\0') { g_free(dir_part); dir_part = g_strdup("/"); }
        prefix = g_strdup(slash + 1);
    } else {
        dir_part = g_strdup(st->current_path ? st->current_path : g_get_home_dir());
        prefix = g_strdup(expanded);
    }
    g_free(expanded);

    guint shown = 0;
    GDir *d = g_dir_open(dir_part, 0, NULL);
    if (d) {
        char *lower_prefix = g_utf8_strdown(prefix, -1);
        GPtrArray *matches = g_ptr_array_new_with_free_func(g_free);
        const char *name;
        /* stop early: a huge folder must not stall typing */
        guint scanned = 0;
        while ((name = g_dir_read_name(d)) != NULL && scanned++ < 5000) {
            if (name[0] == '.' && prefix[0] != '.') continue;
            char *lower_name = g_utf8_strdown(name, -1);
            gboolean match = g_str_has_prefix(lower_name, lower_prefix);
            g_free(lower_name);
            if (match) g_ptr_array_add(matches, g_strdup(name));
        }
        g_dir_close(d);
        g_free(lower_prefix);
        g_ptr_array_sort(matches, addr_match_cmp);

        for (guint i = 0; i < matches->len && shown < 8; i++, shown++) {
            const char *nm = g_ptr_array_index(matches, i);
            char *full = g_build_filename(dir_part, nm, NULL);
            gboolean is_dir = g_file_test(full, G_FILE_TEST_IS_DIR);

            GtkWidget *row = myfm_menu_row(is_dir ? "\xF0\x9F\x93\x81" : "\xF0\x9F\x93\x84",
                nm, G_CALLBACK(addr_suggestion_clicked), st, NULL);
            /* Taking focus on click would fire the entry's focus-leave
             * handler, which leaves edit mode and hides this popover
             * before the click lands. */
            gtk_widget_set_focus_on_click(row, FALSE);
            char *full_disp = is_dir ? g_strconcat(full, "/", NULL) : g_strdup(full);
            g_object_set_data_full(G_OBJECT(row), "full-path", full_disp, g_free);
            gtk_box_append(GTK_BOX(st->addr_popover_box), row);
            g_free(full);
        }
        g_ptr_array_free(matches, TRUE);
    }
    g_free(dir_part);
    g_free(prefix);

    if (shown > 0) {
        graphene_rect_t b;
        if (gtk_widget_compute_bounds(st->path_entry, st->window, &b)) {
            GdkRectangle rect = {
                (int)b.origin.x, (int)(b.origin.y + b.size.height),
                (int)b.size.width, 1
            };
            gtk_popover_set_pointing_to(GTK_POPOVER(st->addr_popover), &rect);
        }
        gtk_popover_popup(GTK_POPOVER(st->addr_popover));
    } else {
        gtk_popover_popdown(GTK_POPOVER(st->addr_popover));
    }
}

static void
on_path_activate(GtkEntry *e, gpointer ud)
{
    AppState *st = ud;
    gtk_popover_popdown(GTK_POPOVER(st->addr_popover));
    address_activate(e, st);
    path_edit_mode(st, FALSE);
}

static void
on_path_focus_leave(GtkEventControllerFocus *c, gpointer ud)
{
    (void)c;
    AppState *st = ud;
    gtk_popover_popdown(GTK_POPOVER(st->addr_popover));
    const char *shown = st->current_path ? st->current_path
                      : st->place == PLACE_TRASH ? "Recycle Bin" : "This PC";
    st->addr_updating = TRUE;
    gtk_editable_set_text(GTK_EDITABLE(st->path_entry), shown);
    st->addr_updating = FALSE;
    path_edit_mode(st, FALSE);
}


/* ── keyboard shortcuts ── */

static gboolean
on_key_pressed(GtkEventControllerKey *c, guint keyval, guint code,
               GdkModifierType mod, gpointer ud)
{
    (void)c; (void)code;
    AppState *st = ud;
    gboolean ctrl  = (mod & GDK_CONTROL_MASK) != 0;
    gboolean shift = (mod & GDK_SHIFT_MASK)   != 0;
    gboolean alt   = (mod & GDK_ALT_MASK)     != 0;
    guint key = gdk_keyval_to_lower(keyval);    /* Caps Lock / Shift safe */

    if (ctrl) {
        switch (key) {
            case GDK_KEY_l: path_edit_mode(st, TRUE);            return TRUE;
            case GDK_KEY_h: myfm_toggle_hidden(st);              return TRUE;
            case GDK_KEY_a: myfm_select_all(st);                 return TRUE;
            case GDK_KEY_c: myfm_copy_selected(st);              return TRUE;
            case GDK_KEY_x: myfm_cut_selected(st);               return TRUE;
            case GDK_KEY_v: myfm_paste(st);                      return TRUE;
            case GDK_KEY_n:
                if (shift) myfm_new_folder(st);
                else       myfm_window_new(st->app, st->current_path);
                return TRUE;
            case GDK_KEY_w: gtk_window_close(GTK_WINDOW(st->window));
                                                                 return TRUE;
            case GDK_KEY_f:
            case GDK_KEY_e: gtk_widget_grab_focus(st->search_entry);
                                                                 return TRUE;
            case GDK_KEY_r: refresh_view(st);                    return TRUE;
            case GDK_KEY_p: myfm_toggle_preview(st);             return TRUE;
            case GDK_KEY_comma: myfm_open_settings(st->window);  return TRUE;
            default: break;
        }
    }

    if (alt) {
        switch (keyval) {
            case GDK_KEY_Left:   go_back(NULL, st);    return TRUE;
            case GDK_KEY_Right:  go_forward(NULL, st); return TRUE;
            case GDK_KEY_Up:     go_up(NULL, st);      return TRUE;
            case GDK_KEY_Return: myfm_properties_selected(st); return TRUE;
            default: break;
        }
    }

    switch (keyval) {
        case GDK_KEY_F2:        myfm_rename_selected(st); return TRUE;
        case GDK_KEY_F5:        refresh_view(st);         return TRUE;
        case GDK_KEY_BackSpace: go_back(NULL, st);        return TRUE;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:  myfm_open_selected(st);   return TRUE;
        case GDK_KEY_Delete:
            if (shift) myfm_delete_selected(st);
            else       myfm_trash_selected(st);
            return TRUE;
        case GDK_KEY_Escape:
            path_edit_mode(st, FALSE);
            myfm_clear_multi_selection(st);
            return TRUE;
        default: break;
    }
    return FALSE;
}


/* ── chrome building blocks ── */

static GtkWidget *
nav_btn(const char *glyph, const char *tooltip, GCallback cb, gpointer data)
{
    GtkWidget *b = gtk_button_new_with_label(glyph);
    gtk_widget_add_css_class(b, "nav-btn");
    gtk_widget_set_focus_on_click(b, FALSE);
    if (tooltip) gtk_widget_set_tooltip_text(b, tooltip);
    if (cb) g_signal_connect(b, "clicked", cb, data);
    return b;
}

/* Command-bar button: small icon + text, like a collapsed ribbon. */
static GtkWidget *
cmd_btn(const char *glyph, const char *label, const char *tooltip,
        GCallback cb, gpointer data)
{
    GtkWidget *b = gtk_button_new();
    gtk_widget_add_css_class(b, "cmd-btn");
    gtk_widget_set_focus_on_click(b, FALSE);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *ic = gtk_label_new(glyph);
    gtk_widget_add_css_class(ic, "cmd-btn-icon");
    gtk_box_append(GTK_BOX(box), ic);
    if (label) gtk_box_append(GTK_BOX(box), gtk_label_new(label));
    gtk_button_set_child(GTK_BUTTON(b), box);

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

static GtkWidget *
view_toggle(const char *glyph, const char *tooltip)
{
    GtkWidget *b = gtk_button_new_with_label(glyph);
    gtk_widget_add_css_class(b, "view-btn");
    gtk_widget_set_focus_on_click(b, FALSE);
    gtk_widget_set_tooltip_text(b, tooltip);
    return b;
}

static GtkWidget *
sidebar_title(const char *text)
{
    GtkWidget *l = gtk_label_new(text);
    gtk_widget_add_css_class(l, "sidebar-title");
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    return l;
}

/* Keep the end of the breadcrumb trail (the current folder) in view. */
static gboolean
crumbs_scroll_end_idle(gpointer p)
{
    GtkAdjustment *adj = p;
    gtk_adjustment_set_value(adj,
        gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj));
    g_object_unref(adj);
    return G_SOURCE_REMOVE;
}

static void
on_crumbs_adj_changed(GtkAdjustment *adj, gpointer ud)
{
    (void)ud;
    /* the viewport is still being allocated here; scroll once it's done */
    g_idle_add(crumbs_scroll_end_idle, g_object_ref(adj));
}

/* Mirrors the window title into our own title-bar label. */
static void
on_title_changed(GObject *win, GParamSpec *ps, gpointer ud)
{
    (void)ps;
    AppState *st = ud;
    const char *t = gtk_window_get_title(GTK_WINDOW(win));
    if (st->title_label)
        gtk_label_set_text(GTK_LABEL(st->title_label), t ? t : "");
}

/* Remember the size of the window being closed, for the next one. */
static gboolean
on_close_request(GtkWindow *win, gpointer ud)
{
    (void)ud;
    MyfmSettings *s = myfm_settings();
    if (s->remember_size) {
        s->win_maximized = gtk_window_is_maximized(win);
        if (!s->win_maximized) {
            int w = 0, h = 0;
            gtk_window_get_default_size(win, &w, &h);
            if (w > 200 && h > 150) {
                s->win_width  = w;
                s->win_height = h;
            }
        }
        myfm_settings_save();
    }
    return FALSE;       /* let it close */
}

/* Popovers are attached to the window with gtk_widget_set_parent() (the
 * address-bar suggestions, plus any context menu still open): GTK needs
 * them unparented by hand before the window is finalized.  Async work
 * and monitors are stopped here too, not at finalize time. */
static void
on_window_destroy(GtkWidget *window, gpointer ud)
{
    (void)ud;
    AppState *st = g_object_get_data(G_OBJECT(window), "app-state");
    if (st) {
        myfm_cancel_pending_dir_load(st);
        if (st->refresh_id) {
            g_source_remove(st->refresh_id);
            st->refresh_id = 0;
        }
        if (st->volume_refresh_id) {
            g_source_remove(st->volume_refresh_id);
            st->volume_refresh_id = 0;
        }
        if (st->directory_monitor) {
            g_file_monitor_cancel(st->directory_monitor);
            g_clear_object(&st->directory_monitor);
        }
        if (st->vol_monitor) {
            for (guint i = 0; i < G_N_ELEMENTS(st->vol_sig); i++)
                if (st->vol_sig[i]) {
                    g_signal_handler_disconnect(st->vol_monitor, st->vol_sig[i]);
                    st->vol_sig[i] = 0;
                }
        }
    }

    GtkWidget *c = gtk_widget_get_first_child(window);
    while (c) {
        GtkWidget *next = gtk_widget_get_next_sibling(c);
        if (GTK_IS_POPOVER(c)) gtk_widget_unparent(c);
        c = next;
    }
}

GtkWidget *
myfm_window_new(GtkApplication *app, const char *start_path)
{
    MyfmSettings *s = myfm_settings();
    myfm_gui_apply_theme();

    GtkWidget *window = gtk_application_window_new(app);
    gtk_widget_add_css_class(window, "myfm");
    gtk_window_set_title(GTK_WINDOW(window), "ExploreR");
    gtk_window_set_default_size(GTK_WINDOW(window), s->win_width, s->win_height);
    if (s->remember_size && s->win_maximized)
        gtk_window_maximize(GTK_WINDOW(window));

    AppState *st = g_new0(AppState, 1);
    st->app             = app;
    st->window          = window;
    st->history         = g_ptr_array_new_with_free_func(g_free);
    st->history_position = -1;
    st->view_mode       = s->view_mode;
    st->sort_column     = SORT_NAME;
    st->place           = PLACE_FOLDER;
    st->sel_items       = g_ptr_array_new_with_free_func(myfm_sel_item_free);
    st->clip_paths      = g_ptr_array_new_with_free_func(g_free);
    st->marquee_preserve = g_ptr_array_new_with_free_func(myfm_sel_item_free);
    st->preview_visible = s->preview_visible;
    st->show_checkboxes = s->show_checkboxes;
    st->show_hidden     = s->show_hidden;

    g_object_set_data_full(G_OBJECT(window), "app-state", st,
        (GDestroyNotify)app_state_free);
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), NULL);
    g_signal_connect(window, "close-request", G_CALLBACK(on_close_request), NULL);

    /* ── title bar: icon + title on the left, caption buttons on the
     *    right in Windows order, regardless of the desktop's layout ── */
    GtkWidget *hb = gtk_header_bar_new();
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(hb), TRUE);
    gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(hb),
        ":minimize,maximize,close");
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(hb),
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));    /* no centred title */
    gtk_window_set_titlebar(GTK_WINDOW(window), hb);

    GtkWidget *ticon = myfm_named_icon_widget("folder", "\xF0\x9F\x93\x81", 16);
    gtk_widget_add_css_class(ticon, "title-icon");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), ticon);
    st->title_label = gtk_label_new("ExploreR");
    gtk_widget_add_css_class(st->title_label, "title-text");
    gtk_label_set_ellipsize(GTK_LABEL(st->title_label), PANGO_ELLIPSIZE_END);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), st->title_label);
    g_signal_connect(window, "notify::title", G_CALLBACK(on_title_changed), st);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(window), root);

    /* ── command bar ── */
    GtkWidget *cmdbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 1);
    gtk_widget_add_css_class(cmdbar, "commandbar");

    GtkWidget *new_mb = menu_button("New", "cmd-accent");
    gtk_widget_set_tooltip_text(new_mb, "New folder (Ctrl+Shift+N) or document");
    GtkWidget *new_pop = gtk_popover_new();
    GtkWidget *new_box = popover_box(new_pop);
    menu_add(new_box, new_pop, st, "\xF0\x9F\x93\x81", "Folder", ACT_NEW_FOLDER);
    menu_add(new_box, new_pop, st, "\xF0\x9F\x93\x84", "Text document", ACT_NEW_FILE);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(new_mb), new_pop);
    st->cmd_new = new_mb;
    gtk_box_append(GTK_BOX(cmdbar), new_mb);

    gtk_box_append(GTK_BOX(cmdbar), vsep());

    st->cmd_cut = cmd_btn("\xE2\x9C\x82", "Cut", "Cut (Ctrl+X)", G_CALLBACK(on_cut), st);
    gtk_box_append(GTK_BOX(cmdbar), st->cmd_cut);
    st->cmd_copy = cmd_btn("\xF0\x9F\x97\x8E", "Copy", "Copy (Ctrl+C)", G_CALLBACK(on_copy), st);
    gtk_box_append(GTK_BOX(cmdbar), st->cmd_copy);
    st->cmd_paste = cmd_btn("\xF0\x9F\x93\x8B", "Paste", "Paste (Ctrl+V)", G_CALLBACK(on_paste), st);
    gtk_box_append(GTK_BOX(cmdbar), st->cmd_paste);

    gtk_box_append(GTK_BOX(cmdbar), vsep());

    st->cmd_rename = cmd_btn("\xE2\x9C\x8E", "Rename", "Rename (F2)", G_CALLBACK(on_rename), st);
    gtk_box_append(GTK_BOX(cmdbar), st->cmd_rename);
    st->cmd_trash = cmd_btn("\xE2\x9C\x95", "Delete", "Move to the Recycle Bin (Del)",
        G_CALLBACK(on_trash), st);
    gtk_box_append(GTK_BOX(cmdbar), st->cmd_trash);
    st->cmd_props = cmd_btn("\xE2\x93\x98", "Properties", "Properties (Alt+Enter)",
        G_CALLBACK(on_props), st);
    gtk_box_append(GTK_BOX(cmdbar), st->cmd_props);

    /* only shown when a removable volume is selected */
    st->cmd_eject = cmd_btn("\xE2\x8F\x8F", "Eject", "Eject the selected device",
        G_CALLBACK(on_eject), st);
    gtk_widget_set_visible(st->cmd_eject, FALSE);
    gtk_box_append(GTK_BOX(cmdbar), st->cmd_eject);

    gtk_box_append(GTK_BOX(cmdbar), vsep());

    GtkWidget *sort_mb = menu_button("Sort", NULL);
    GtkWidget *sort_pop = gtk_popover_new();
    GtkWidget *sort_box = popover_box(sort_pop);
    menu_add(sort_box, sort_pop, st, NULL, "Name", ACT_SORT_NAME);
    menu_add(sort_box, sort_pop, st, NULL, "Date modified", ACT_SORT_DATE);
    menu_add(sort_box, sort_pop, st, NULL, "Type", ACT_SORT_TYPE);
    menu_add(sort_box, sort_pop, st, NULL, "Size", ACT_SORT_SIZE);
    gtk_box_append(GTK_BOX(sort_box), myfm_menu_separator());
    menu_add(sort_box, sort_pop, st, "\xE2\x87\x85", "Ascending / Descending", ACT_SORT_DIR);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(sort_mb), sort_pop);
    gtk_box_append(GTK_BOX(cmdbar), sort_mb);

    GtkWidget *view_mb = menu_button("View", NULL);
    GtkWidget *view_pop = gtk_popover_new();
    GtkWidget *view_box = popover_box(view_pop);
    menu_add(view_box, view_pop, st, "\xE2\x98\xB0", "Details", ACT_VIEW_LIST);
    menu_add(view_box, view_pop, st, "\xE2\x8A\x9E", "Large icons", ACT_VIEW_GRID);
    gtk_box_append(GTK_BOX(view_box), myfm_menu_separator());
    menu_add(view_box, view_pop, st, NULL, "Hidden items (Ctrl+H)", ACT_HIDDEN);
    menu_add(view_box, view_pop, st, NULL, "Item check boxes", ACT_CHECKBOXES);
    menu_add(view_box, view_pop, st, NULL, "Preview pane (Ctrl+P)", ACT_PREVIEW);
    gtk_box_append(GTK_BOX(view_box), myfm_menu_separator());
    menu_add(view_box, view_pop, st, NULL, "Select all (Ctrl+A)", ACT_SELECT_ALL);
    menu_add(view_box, view_pop, st, NULL, "Open in terminal", ACT_TERMINAL);
    gtk_box_append(GTK_BOX(view_box), myfm_menu_separator());
    menu_add(view_box, view_pop, st, "\xE2\x9A\x99", "Settings (Ctrl+,)", ACT_SETTINGS);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(view_mb), view_pop);
    gtk_box_append(GTK_BOX(cmdbar), view_mb);

    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(cmdbar), spacer);

    GtkWidget *settings_btn = cmd_btn("\xE2\x9A\x99", NULL, "Settings (Ctrl+,)",
        G_CALLBACK(on_open_settings), st);
    gtk_box_append(GTK_BOX(cmdbar), settings_btn);

    gtk_box_append(GTK_BOX(root), cmdbar);

    /* ── address row: ← → ↑  [ crumbs … ⟳ ]  [ search ] ── */
    GtkWidget *addr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_add_css_class(addr, "addressbar");

    st->btn_back = nav_btn("\xE2\x86\x90", "Back (Alt+Left)", G_CALLBACK(go_back), st);
    st->btn_forward = nav_btn("\xE2\x86\x92", "Forward (Alt+Right)", G_CALLBACK(go_forward), st);
    st->btn_up = nav_btn("\xE2\x86\x91", "Up (Alt+Up)", G_CALLBACK(go_up), st);
    gtk_box_append(GTK_BOX(addr), st->btn_back);
    gtk_box_append(GTK_BOX(addr), st->btn_forward);
    gtk_box_append(GTK_BOX(addr), st->btn_up);

    st->path_stack = gtk_stack_new();
    gtk_widget_set_hexpand(st->path_stack, TRUE);
    gtk_widget_set_margin_start(st->path_stack, 4);

    GtkWidget *crumb_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(crumb_bar, "crumb-bar");
    st->crumbs = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_valign(st->crumbs, GTK_ALIGN_CENTER);

    /* Deep paths must not widen the window: the crumbs sit in a clipped
     * scroller (no visible scrollbar) that always shows the deepest
     * folders, the way Explorer drops the leading ones. */
    GtkWidget *crumb_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(crumb_scroll),
        GTK_POLICY_EXTERNAL, GTK_POLICY_NEVER);
    gtk_scrolled_window_set_propagate_natural_height(
        GTK_SCROLLED_WINDOW(crumb_scroll), TRUE);
    gtk_widget_set_hexpand(crumb_scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(crumb_scroll), st->crumbs);
    GtkAdjustment *cadj =
        gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(crumb_scroll));
    g_signal_connect(cadj, "changed", G_CALLBACK(on_crumbs_adj_changed), NULL);
    gtk_box_append(GTK_BOX(crumb_bar), crumb_scroll);

    GtkWidget *btn_refresh = gtk_button_new_with_label("\xE2\x9F\xB3");
    gtk_widget_add_css_class(btn_refresh, "crumb-refresh");
    gtk_widget_set_focus_on_click(btn_refresh, FALSE);
    gtk_widget_set_tooltip_text(btn_refresh, "Refresh (F5)");
    g_signal_connect(btn_refresh, "clicked", G_CALLBACK(on_refresh), st);
    gtk_box_append(GTK_BOX(crumb_bar), btn_refresh);

    GtkGesture *cg = gtk_gesture_click_new();
    g_signal_connect(cg, "pressed", G_CALLBACK(on_crumbbar_pressed), st);
    gtk_widget_add_controller(crumb_bar, GTK_EVENT_CONTROLLER(cg));

    st->path_entry = gtk_entry_new();
    gtk_widget_add_css_class(st->path_entry, "text-entry");
    g_signal_connect(st->path_entry, "activate", G_CALLBACK(on_path_activate), st);
    g_signal_connect(st->path_entry, "changed", G_CALLBACK(on_path_entry_changed), st);

    st->addr_popover = gtk_popover_new();
    gtk_popover_set_has_arrow(GTK_POPOVER(st->addr_popover), FALSE);
    gtk_popover_set_autohide(GTK_POPOVER(st->addr_popover), FALSE);
    gtk_widget_set_halign(st->addr_popover, GTK_ALIGN_START);
    gtk_widget_set_parent(st->addr_popover, window);
    st->addr_popover_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(st->addr_popover_box, 320, -1);
    gtk_popover_set_child(GTK_POPOVER(st->addr_popover), st->addr_popover_box);

    GtkEventController *fc = gtk_event_controller_focus_new();
    g_signal_connect(fc, "leave", G_CALLBACK(on_path_focus_leave), st);
    gtk_widget_add_controller(st->path_entry, fc);

    gtk_stack_add_named(GTK_STACK(st->path_stack), crumb_bar, "crumbs");
    gtk_stack_add_named(GTK_STACK(st->path_stack), st->path_entry, "entry");
    gtk_stack_set_visible_child_name(GTK_STACK(st->path_stack), "crumbs");
    gtk_box_append(GTK_BOX(addr), st->path_stack);

    st->search_entry = gtk_search_entry_new();
    gtk_widget_add_css_class(st->search_entry, "search-entry");
    gtk_widget_set_size_request(st->search_entry, 200, -1);
    gtk_widget_set_margin_start(st->search_entry, 4);
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(st->search_entry), "Search");
    g_signal_connect(st->search_entry, "search-changed", G_CALLBACK(on_search_changed), st);
    gtk_box_append(GTK_BOX(addr), st->search_entry);

    gtk_box_append(GTK_BOX(root), addr);

    /* ── body: navigation pane | content | preview ── */
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_position(GTK_PANED(paned), 180);
    gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_resize_start_child(GTK_PANED(paned), FALSE);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_box_append(GTK_BOX(root), paned);

    GtkWidget *side_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(side_scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(sidebar, "sidebar");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(side_scroll), sidebar);
    gtk_paned_set_start_child(GTK_PANED(paned), side_scroll);

    gtk_box_append(GTK_BOX(sidebar), sidebar_title("Quick access"));

    struct { const char *icon; const char *emoji; const char *label; const char *sub; } shortcuts[] = {
        { "user-home",        "\xF0\x9F\x8F\xA0", "Home",      NULL        },
        { "user-desktop",     "\xF0\x9F\x96\xA5", "Desktop",   "Desktop"   },
        { "folder-documents", "\xF0\x9F\x93\x84", "Documents", "Documents" },
        { "folder-download",  "\xE2\xAC\x87",     "Downloads", "Downloads" },
        { "folder-pictures",  "\xF0\x9F\x96\xBC", "Pictures",  "Pictures"  },
        { "folder-music",     "\xF0\x9F\x8E\xB5", "Music",     "Music"     },
        { "folder-videos",    "\xF0\x9F\x8E\xAC", "Videos",    "Videos"    },
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
            shortcuts[i].icon, shortcuts[i].emoji, shortcuts[i].label, path, st));
        g_free(path);
    }

    gtk_box_append(GTK_BOX(sidebar), sidebar_title("This PC"));

    GtkWidget *this_pc = create_directory_button(
        "computer", "\xF0\x9F\x92\xBB", "This PC", NULL, st);
    g_signal_handlers_disconnect_by_func(this_pc,
        G_CALLBACK(directory_button_clicked), st);
    g_signal_connect(this_pc, "clicked", G_CALLBACK(this_pc_button_clicked), st);
    gtk_box_append(GTK_BOX(sidebar), this_pc);

    gtk_box_append(GTK_BOX(sidebar), create_directory_button(
        "drive-harddisk", "\xF0\x9F\x92\xBD", "Local Disk (/)", "/", st));

    GtkWidget *trash_btn = create_directory_button(
        "user-trash", "\xF0\x9F\x97\x91", "Recycle Bin", NULL, st);
    g_signal_handlers_disconnect_by_func(trash_btn,
        G_CALLBACK(directory_button_clicked), st);
    g_signal_connect(trash_btn, "clicked", G_CALLBACK(trash_button_clicked), st);
    gtk_box_append(GTK_BOX(sidebar), trash_btn);

    /* content */
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(content, "content-area");

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

    /* Overlay just for the rubber-band marquee: file_view stays the
     * widget rows get appended to, so its coordinate space is unaffected. */
    st->ov_scroller = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(st->ov_scroller), st->file_view);

    st->marquee = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(st->marquee, "marquee");
    gtk_widget_set_halign(st->marquee, GTK_ALIGN_START);
    gtk_widget_set_valign(st->marquee, GTK_ALIGN_START);
    gtk_widget_set_can_target(st->marquee, FALSE);
    gtk_widget_set_visible(st->marquee, FALSE);
    gtk_overlay_add_overlay(GTK_OVERLAY(st->ov_scroller), st->marquee);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(st->scroller), st->ov_scroller);

    myfm_attach_background_menu(st);
    myfm_attach_marquee(st);

    /* ── preview pane ── */
    st->preview_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(st->preview_box, "preview-box");
    gtk_widget_set_size_request(st->preview_box, 200, -1);
    gtk_widget_set_margin_start(st->preview_box, 8);
    gtk_widget_set_margin_end(st->preview_box, 8);
    gtk_widget_set_margin_top(st->preview_box, 6);
    gtk_widget_set_margin_bottom(st->preview_box, 6);
    gtk_widget_set_visible(st->preview_box, st->preview_visible);

    st->preview_title = gtk_label_new("");
    gtk_widget_add_css_class(st->preview_title, "preview-title");
    gtk_widget_set_halign(st->preview_title, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(st->preview_title), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_xalign(GTK_LABEL(st->preview_title), 0.0f);
    gtk_box_append(GTK_BOX(st->preview_box), st->preview_title);

    st->preview_meta = gtk_label_new("");
    gtk_widget_add_css_class(st->preview_meta, "preview-meta");
    gtk_widget_set_halign(st->preview_meta, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(st->preview_meta), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(st->preview_meta), 0.0f);
    gtk_box_append(GTK_BOX(st->preview_box), st->preview_meta);

    gtk_box_append(GTK_BOX(st->preview_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    st->preview_stack = gtk_stack_new();
    gtk_widget_set_vexpand(st->preview_stack, TRUE);
    gtk_box_append(GTK_BOX(st->preview_box), st->preview_stack);

    GtkWidget *pv_empty = gtk_label_new("Select a file to preview.");
    gtk_widget_add_css_class(pv_empty, "info-label");
    gtk_label_set_wrap(GTK_LABEL(pv_empty), TRUE);
    gtk_stack_add_named(GTK_STACK(st->preview_stack), pv_empty, "empty");

    st->preview_pic = gtk_picture_new();
    gtk_picture_set_content_fit(GTK_PICTURE(st->preview_pic), GTK_CONTENT_FIT_CONTAIN);
    gtk_widget_set_vexpand(st->preview_pic, TRUE);
    gtk_stack_add_named(GTK_STACK(st->preview_stack), st->preview_pic, "image");

    GtkWidget *pv_text_scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(pv_text_scroll, TRUE);
    st->preview_text = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(st->preview_text), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(st->preview_text), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(st->preview_text), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(st->preview_text), TRUE);
    gtk_widget_add_css_class(st->preview_text, "preview-text");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(pv_text_scroll), st->preview_text);
    gtk_stack_add_named(GTK_STACK(st->preview_stack), pv_text_scroll, "text");

    GtkWidget *pv_info_scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(pv_info_scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(pv_info_scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    st->preview_info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(pv_info_scroll), st->preview_info);
    gtk_stack_add_named(GTK_STACK(st->preview_stack), pv_info_scroll, "info");

    gtk_stack_set_visible_child_name(GTK_STACK(st->preview_stack), "empty");

    st->content_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_shrink_start_child(GTK_PANED(st->content_paned), FALSE);
    gtk_paned_set_resize_start_child(GTK_PANED(st->content_paned), TRUE);
    gtk_paned_set_resize_end_child(GTK_PANED(st->content_paned), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(st->content_paned), FALSE);
    gtk_paned_set_start_child(GTK_PANED(st->content_paned), content);
    gtk_paned_set_end_child(GTK_PANED(st->content_paned), st->preview_box);
    gtk_paned_set_position(GTK_PANED(st->content_paned),
        MAX(300, s->win_width - 180 - 260));

    gtk_paned_set_end_child(GTK_PANED(paned), st->content_paned);

    /* ── status bar: counts left, selection + view toggles right ── */
    GtkWidget *status = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(status, "statusbar");

    st->status_left = gtk_label_new("");
    gtk_widget_add_css_class(st->status_left, "status-label");
    gtk_widget_set_halign(st->status_left, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(status), st->status_left);

    st->status_right = gtk_label_new("");
    gtk_widget_add_css_class(st->status_right, "status-label");
    gtk_widget_set_halign(st->status_right, GTK_ALIGN_START);
    gtk_widget_set_hexpand(st->status_right, TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(st->status_right), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(status), st->status_right);

    st->cmd_preview = view_toggle("\xE2\x96\xA5", "Preview pane (Ctrl+P)");
    if (st->preview_visible)
        gtk_widget_add_css_class(st->cmd_preview, "view-btn-active");
    g_signal_connect_swapped(st->cmd_preview, "clicked",
        G_CALLBACK(myfm_toggle_preview), st);
    gtk_box_append(GTK_BOX(status), st->cmd_preview);

    st->view_toggle_list = view_toggle("\xE2\x98\xB0", "Details");
    g_signal_connect(st->view_toggle_list, "clicked", G_CALLBACK(on_view_list), st);
    gtk_box_append(GTK_BOX(status), st->view_toggle_list);

    st->view_toggle_grid = view_toggle("\xE2\x8A\x9E", "Large icons");
    g_signal_connect(st->view_toggle_grid, "clicked", G_CALLBACK(on_view_grid), st);
    gtk_box_append(GTK_BOX(status), st->view_toggle_grid);

    myfm_apply_view_mode(st, st->view_mode);

    gtk_box_append(GTK_BOX(root), status);

    /* ── shortcuts ── */
    GtkEventController *kc = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(kc, GTK_PHASE_BUBBLE);
    g_signal_connect(kc, "key-pressed", G_CALLBACK(on_key_pressed), st);
    gtk_widget_add_controller(window, kc);

    /* keep Paste (and our internal cut/copy list) in sync with the
     * system clipboard */
    g_signal_connect_object(gtk_widget_get_clipboard(window), "changed",
        G_CALLBACK(myfm_on_clipboard_changed), window, G_CONNECT_DEFAULT);

    /* ── go ── */
    myfm_update_actions(st);
    myfm_watch_volumes(st);

    const char *dest = start_path && g_file_test(start_path, G_FILE_TEST_IS_DIR)
        ? start_path : g_get_home_dir();
    navigate_to(st, dest, TRUE);

    gtk_window_present(GTK_WINDOW(window));
    return window;
}
