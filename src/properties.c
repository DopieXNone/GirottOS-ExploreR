/* properties.c – GNOME (Nautilus) style properties dialog */

#include "myfm.h"

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

static char *
format_size_short(guint64 bytes)
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

static GtkWidget *
group_title(const char *text)
{
    GtkWidget *l = gtk_label_new(text);
    gtk_widget_add_css_class(l, "prop-group-title");
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    return l;
}

static GtkWidget *
card_new(void)
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

static void
card_add_value(GtkWidget *card, const char *label_text,
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
}

static void
card_add_widget(GtkWidget *card, const char *label_text, GtkWidget *w)
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
    GtkWidget *card = card_new();

    struct passwd *pw = getpwuid(stp->st_uid);
    struct group  *gr = getgrgid(stp->st_gid);

    char *owner = pw ? g_strdup(pw->pw_name)
                     : g_strdup_printf("uid %u", (unsigned)stp->st_uid);
    char *group = gr ? g_strdup(gr->gr_name)
                     : g_strdup_printf("gid %u", (unsigned)stp->st_gid);

    card_add_value(card, "Owner", owner, FALSE);
    card_add_value(card, "Group", group, FALSE);
    g_free(owner);
    g_free(group);

    gboolean editable = (getuid() == 0 || getuid() == stp->st_uid);

    pu->updating = TRUE;
    pu->dd[0] = perm_dropdown(pu, (stp->st_mode >> 6) & 7);
    pu->dd[1] = perm_dropdown(pu, (stp->st_mode >> 3) & 7);
    pu->dd[2] = perm_dropdown(pu,  stp->st_mode       & 7);

    card_add_widget(card, "Owner access",  pu->dd[0]);
    card_add_widget(card, "Group access",  pu->dd[1]);
    card_add_widget(card, "Others access", pu->dd[2]);

    if (!pu->is_dir) {
        pu->exec_switch = gtk_switch_new();
        gtk_switch_set_active(GTK_SWITCH(pu->exec_switch),
            (stp->st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0);
        card_add_widget(card, "Executable as program", pu->exec_switch);
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
    card_add_value(card, "Mode", octal, TRUE);

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
    gtk_window_set_default_size(GTK_WINDOW(win), 480, 640);
    gtk_window_set_modal(GTK_WINDOW(win), TRUE);
    if (parent) {
        GtkRoot *root = gtk_widget_get_root(parent);
        if (GTK_IS_WINDOW(root))
            gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(root));
    }
    gtk_window_set_destroy_with_parent(GTK_WINDOW(win), TRUE);

    GtkWidget *hb = gtk_header_bar_new();
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(hb), TRUE);
    gtk_window_set_titlebar(GTK_WINDOW(win), hb);

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

    GtkWidget *icon = gtk_label_new(file_icon_for(base, is_dir));
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
    GtkWidget *card1 = card_new();

    if (is_dir) {
        guint n = count_dir_items(path);
        char *c = g_strdup_printf("%u item%s", n, n == 1 ? "" : "s");
        card_add_value(card1, "Contents", c, FALSE);
        g_free(c);
    } else {
        char *sz = format_size_prop((guint64)st.st_size);
        card_add_value(card1, "Size", sz, FALSE);
        g_free(sz);
    }

    card_add_value(card1, "Parent folder", parent_dir, FALSE);

    if (is_link) {
        char target[4096] = { 0 };
        ssize_t len = readlink(path, target, sizeof(target) - 1);
        if (len > 0) {
            target[len] = '\0';
            card_add_value(card1, "Link target", target, FALSE);
        }
    }

    struct statvfs sv;
    if (statvfs(path, &sv) == 0 && sv.f_blocks > 0) {
        guint64 free_b = (guint64)sv.f_bavail * sv.f_frsize;
        char *fs = format_size_short(free_b);
        card_add_value(card1, "Free space", fs, FALSE);
        g_free(fs);
    }

    gtk_box_append(GTK_BOX(page), card1);

    /* ── times ── */
    gtk_box_append(GTK_BOX(page), group_title("Times"));
    GtkWidget *card2 = card_new();

    char *t_mod = format_time_prop(st.st_mtime);
    char *t_acc = format_time_prop(st.st_atime);
    char *t_chg = format_time_prop(st.st_ctime);
    card_add_value(card2, "Modified", t_mod, FALSE);
    card_add_value(card2, "Accessed", t_acc, FALSE);
    card_add_value(card2, "Changed",  t_chg, FALSE);
    g_free(t_mod);
    g_free(t_acc);
    g_free(t_chg);

    gtk_box_append(GTK_BOX(page), card2);

    /* ── permissions ── */
    gtk_box_append(GTK_BOX(page), group_title("Permissions"));

    PermUI *pu = g_new0(PermUI, 1);
    pu->path   = g_strdup(path);
    pu->is_dir = is_dir;
    pu->mode   = st.st_mode;
    pu->window = win;

    GtkWidget *card3 = build_permissions_card(pu, &st);
    g_object_set_data_full(G_OBJECT(win), "perm-ui", pu, perm_ui_free);
    gtk_box_append(GTK_BOX(page), card3);

    /* ── details ── */
    gtk_box_append(GTK_BOX(page), group_title("Details"));
    GtkWidget *card4 = card_new();

    card_add_value(card4, "Full path", path, FALSE);
    if (!is_dir) card_add_value(card4, "Type", type_desc, FALSE);

    char *inode = g_strdup_printf("%lu", (unsigned long)st.st_ino);
    char *links = g_strdup_printf("%lu", (unsigned long)st.st_nlink);
    char *disk  = format_size_short((guint64)st.st_blocks * 512ULL);
    card_add_value(card4, "Inode",      inode, TRUE);
    card_add_value(card4, "Hard links", links, TRUE);
    card_add_value(card4, "Disk usage", disk,  FALSE);
    g_free(inode);
    g_free(links);
    g_free(disk);

    gtk_box_append(GTK_BOX(page), card4);

    g_free(type_desc);
    g_free(base);
    g_free(parent_dir);

    gtk_window_present(GTK_WINDOW(win));
}
