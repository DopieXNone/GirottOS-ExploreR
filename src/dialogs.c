/* dialogs.c – MyFM's own dialogs.
 *
 * Nothing here uses GtkAlertDialog / GtkMessageDialog, so every window the
 * file manager shows is drawn with the application theme and looks the same
 * on any desktop environment.
 */

#include "myfm.h"


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
    gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
    gtk_window_set_modal(GTK_WINDOW(win), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(win), 430, -1);
    gtk_window_set_title(GTK_WINDOW(win), title);

    if (parent) {
        GtkRoot *root = gtk_widget_get_root(parent);
        if (GTK_IS_WINDOW(root))
            gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(root));
    }
    gtk_window_set_destroy_with_parent(GTK_WINDOW(win), TRUE);
    g_object_set_data_full(G_OBJECT(win), "dlg-ctx", d, dlg_free);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 22);
    gtk_widget_set_margin_bottom(box, 20);
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
