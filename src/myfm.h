#ifndef MYFM_H
#define MYFM_H

/* Must come before any libc header (S_ISVTX, readlink, ...) */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <gtk/gtk.h>

/* ── enums ── */

typedef enum {
    VIEW_LIST,
    VIEW_GRID
} ViewMode;

typedef enum {
    SORT_NAME,
    SORT_MTIME,
    SORT_TYPE,
    SORT_SIZE
} SortColumn;

typedef enum {
    PLACE_FOLDER,
    PLACE_THIS_PC,
    PLACE_TRASH
} PlaceKind;

/* What the currently selected row represents.  Volumes (drives and
 * partitions) never accept cut / copy / rename / delete operations. */
typedef enum {
    ITEM_NONE,
    ITEM_FILE,
    ITEM_VOLUME
} ItemKind;


/* ── application state (one per window) ── */

typedef struct {
    GtkApplication *app;
    GtkWidget      *window;

    /* content */
    GtkWidget *file_view;       /* GtkBox inside the scroller  */
    GtkWidget *scroller;
    GtkWidget *column_header;   /* Name / Date / Type / Size   */
    GtkWidget *col_btn[4];      /* indexed by SortColumn       */

    /* path bar */
    GtkWidget *path_stack;      /* "crumbs" <-> "entry"        */
    GtkWidget *crumbs;
    GtkWidget *path_entry;
    GtkWidget *search_entry;

    /* chrome */
    GtkWidget *btn_back;
    GtkWidget *btn_forward;
    GtkWidget *btn_up;
    GtkWidget *view_toggle_list;
    GtkWidget *view_toggle_grid;
    GtkWidget *status_left;
    GtkWidget *status_right;

    /* command bar */
    GtkWidget *cmd_new;
    GtkWidget *cmd_cut;
    GtkWidget *cmd_copy;
    GtkWidget *cmd_paste;
    GtkWidget *cmd_rename;
    GtkWidget *cmd_trash;
    GtkWidget *cmd_props;
    GtkWidget *cmd_eject;

    /* history */
    GPtrArray *history;         /* char *  */
    int        history_position;

    /* current location */
    GFileMonitor *directory_monitor;
    char         *current_path;
    PlaceKind     place;
    guint         refresh_id;

    /* view options */
    ViewMode   view_mode;
    SortColumn sort_column;
    gboolean   sort_desc;
    gboolean   show_hidden;
    char      *filter_text;

    /* selection */
    GtkWidget *selected_widget;
    char      *selected_path;
    gboolean   selected_is_dir;
    ItemKind   selected_kind;
    gboolean   selected_removable;

    /* internal clipboard */
    char     *clip_path;
    gboolean  clip_is_cut;
} AppState;


/* ── dialogs.c ── */
typedef void (*MyfmConfirmFunc)(gboolean accepted, gpointer user_data);
typedef void (*MyfmPromptFunc)(const char *text, gpointer user_data);

void myfm_message(GtkWidget *parent, const char *icon,
                  const char *title, const char *body);
void myfm_message_fmt(GtkWidget *parent, const char *icon,
                      const char *title, const char *fmt, ...) G_GNUC_PRINTF(4, 5);
void myfm_confirm(GtkWidget *parent, const char *icon,
                  const char *title, const char *body,
                  const char *ok_label, gboolean destructive,
                  MyfmConfirmFunc cb, gpointer data, GDestroyNotify data_free);
void myfm_prompt(GtkWidget *parent, const char *icon,
                 const char *title, const char *body,
                 const char *initial, const char *ok_label,
                 MyfmPromptFunc cb, gpointer data, GDestroyNotify data_free);


/* ── ui.c ── */
GtkWidget *myfm_window_new(GtkApplication *app, const char *path);
void myfm_update_columns(AppState *state);


/* ── navigation.c ── */
void navigate_to(AppState *state, const char *path, gboolean add_history);
void refresh_view(AppState *state);
void set_view_mode(AppState *state, ViewMode mode);

void go_back(GtkButton *button, gpointer user_data);
void go_forward(GtkButton *button, gpointer user_data);
void go_up(GtkButton *button, gpointer user_data);
void address_activate(GtkEntry *entry, gpointer user_data);

void directory_button_clicked(GtkButton *button, gpointer user_data);
GtkWidget *create_directory_button(const char *icon, const char *label,
                                   const char *path, AppState *state);
void this_pc_button_clicked(GtkButton *button, gpointer user_data);
void trash_button_clicked(GtkButton *button, gpointer user_data);

void myfm_set_sort(AppState *state, SortColumn column);
void myfm_toggle_sort_direction(AppState *state);
void myfm_toggle_hidden(AppState *state);
void myfm_set_filter(AppState *state, const char *text);
void myfm_update_actions(AppState *state);

void myfm_new_folder(AppState *state);
void myfm_new_file(AppState *state);
void myfm_cut_selected(AppState *state);
void myfm_copy_selected(AppState *state);
void myfm_copy_path_selected(AppState *state);
void myfm_paste(AppState *state);
void myfm_rename_selected(AppState *state);
void myfm_trash_selected(AppState *state);
void myfm_delete_selected(AppState *state);
void myfm_properties_selected(AppState *state);
void myfm_open_selected(AppState *state);
void myfm_open_terminal(AppState *state);
void myfm_empty_trash(AppState *state);
void myfm_eject_selected(AppState *state);

const char *file_icon_for(const char *name, gboolean is_dir);

GtkWidget *myfm_menu_row(const char *icon, const char *label,
                         GCallback cb, gpointer data, GtkWidget *popover);
GtkWidget *myfm_menu_separator(void);
void myfm_attach_background_menu(AppState *state);

void app_state_free(AppState *state);

/* ── properties.c ── */
void show_properties_dialog(GtkWidget *parent, const char *path);

/* ── animations.c ── */
void anim_fade_in(GtkWidget *widget);
void anim_slide_in_from_left(GtkWidget *widget);
void anim_pulse_icon(GtkWidget *widget);
void anim_row_appear(GtkWidget *widget, guint delay_ms);
void anim_view_transition(GtkWidget *file_view);

#endif /* MYFM_H */
