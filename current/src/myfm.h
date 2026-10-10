/* myfm.h – shared declarations for ExploreR (MyFM)
 *
 *   main.c      entry point, folder listing, file operations, This PC,
 *               Recycle Bin, navigation and selection
 *   gui.c       window layout, theme (CSS), dialogs, properties dialog,
 *               preview pane, address bar, keyboard shortcuts
 *   settings.c  persistent preferences (INI file) and the Settings window
 *
 * Wayland only, and deliberately desktop-environment agnostic: nothing
 * here needs GNOME (no GSettings schema, no libadwaita, no gvfs-specific
 * APIs besides what GIO itself offers everywhere).
 */
#ifndef MYFM_H
#define MYFM_H

/* Must come before any libc header (S_ISVTX, readlink, ...) */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <gtk/gtk.h>

#ifdef MYFM_HAVE_POPPLER
#include <poppler.h>
#endif

#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

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

typedef enum {
    THEME_SYSTEM,       /* follow the desktop's light/dark preference */
    THEME_LIGHT,        /* Windows 10 light                           */
    THEME_DARK,         /* Windows 10 dark                            */
    THEME_PURPLE        /* the original MyFM dark/purple look         */
} MyfmTheme;

typedef enum {
    ICONS_TELA,         /* Tela icon theme, used by the file manager only
                         * (falls back to emoji when it isn't installed) */
    ICONS_EMOJI
} MyfmIconStyle;


/* ── persistent preferences (settings.c) ── */

typedef struct {
    MyfmTheme     theme;
    MyfmIconStyle icon_style;
    gboolean      animations;
    int           win_width;
    int           win_height;
    gboolean      remember_size;   /* save the size of the last closed window */
    gboolean      win_maximized;
    gboolean      show_hidden;
    gboolean      show_checkboxes;
    gboolean      preview_visible;
    ViewMode      view_mode;
} MyfmSettings;

MyfmSettings *myfm_settings(void);          /* loaded on first use */
void          myfm_settings_save(void);
void          myfm_settings_apply_all(void); /* push to every open window */
void          myfm_open_settings(GtkWidget *parent);
const char   *myfm_settings_path(void);


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
    GtkWidget *title_label;
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
    gint64        refresh_first_us; /* first change of a pending refresh  */
    guint         volume_refresh_id;
    GCancellable *dir_cancellable; /* cancels an in-flight async listing */

    /* view options */
    ViewMode   view_mode;
    SortColumn sort_column;
    gboolean   sort_desc;
    gboolean   show_hidden;
    char      *filter_text;

    /* selection (multi-select capable: rubber-band + Ctrl/Shift-click) */
    GPtrArray *sel_items;        /* SelItem*, the whole current selection */
    GtkWidget *selected_widget;  /* primary (most-recent) selected widget  */
    char      *selected_path;    /* primary (most-recent) selected path    */
    gboolean   selected_is_dir;
    ItemKind   selected_kind;
    gboolean   selected_removable;
    gboolean   show_checkboxes;  /* Explorer-style selection checkboxes    */
    GtkWidget *sel_anchor;       /* Shift+Click range start (only compared) */

    /* rubber-band (marquee) selection */
    GtkWidget *ov_scroller;      /* GtkOverlay wrapping the scroller       */
    GtkWidget *marquee;          /* semi-transparent selection rectangle   */
    gboolean   marqueeing;
    double     drag_start_x, drag_start_y;   /* in file_view coordinates   */
    gboolean   drag_ctrl;
    GPtrArray *marquee_preserve; /* SelItem*, snapshot for Ctrl+drag-select */

    /* preview pane */
    GtkWidget *content_paned;    /* file list | preview split              */
    GtkWidget *preview_box;
    GtkWidget *preview_stack;
    GtkWidget *preview_pic;
    GtkWidget *preview_text;
    GtkWidget *preview_info;    /* generic icon+details card, any file type */
    GtkWidget *preview_title;
    GtkWidget *preview_meta;
    GtkWidget *cmd_preview;
    gboolean   preview_visible;

    /* address bar autocomplete */
    GtkWidget *addr_popover;
    GtkWidget *addr_popover_box;
    gboolean   addr_updating;

    /* volume/disk hot-plug notifications */
    GVolumeMonitor *vol_monitor;
    gulong vol_sig[5];

    /* internal clipboard (supports multiple files) */
    GPtrArray *clip_paths;       /* char *, files pending cut/copy */
    gboolean   clip_is_cut;
    GdkContentProvider *clip_provider; /* what we put on the system clipboard */

    /* folder the file view is currently listing, so a refresh of the same
     * folder can keep the selection and scroll position */
    char      *listed_path;
    char      *preview_path;     /* file the preview pane currently shows */
} AppState;

/* One entry of the current multi-selection. */
typedef struct {
    char      *path;
    GtkWidget *widget;
    gboolean   is_dir;
    ItemKind   kind;
    gboolean   removable;
} SelItem;


/* ── gui.c: dialogs ── */
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

/* ── gui.c: window, theme, shared widgets ── */
GtkWidget *myfm_window_new(GtkApplication *app, const char *path);
void myfm_update_columns(AppState *state);
void myfm_gui_apply_theme(void);
gboolean myfm_gui_theme_is_dark(void);
void myfm_gui_apply_window_size(AppState *state);
void myfm_gui_sync_settings(AppState *state, gboolean icons_changed);
GtkWidget *myfm_icon_widget(const char *name, gboolean is_dir,
                            const char *content_type, int pixel_size);
GtkWidget *myfm_named_icon_widget(const char *icon_name, const char *emoji,
                                  int pixel_size);
gboolean   myfm_tela_available(void);

/* secondary windows (dialogs, properties, settings, open with) */
void       myfm_window_set_owner(GtkWidget *win, GtkWidget *parent);
GtkWidget *myfm_window_titlebar(GtkWidget *win, const char *title);

/* boxed-list building blocks (properties / settings / preview) */
GtkWidget *myfm_group_title(const char *text);
GtkWidget *myfm_card_new(void);
GtkWidget *myfm_card_add_value(GtkWidget *card, const char *label_text,
                               const char *value_text, gboolean mono);
void       myfm_card_add_widget(GtkWidget *card, const char *label_text,
                                GtkWidget *w);
char      *myfm_format_size(guint64 bytes);

/* ── main.c: navigation & views ── */
void navigate_to(AppState *state, const char *path, gboolean add_history);
void refresh_view(AppState *state);
void set_view_mode(AppState *state, ViewMode mode);
void myfm_apply_view_mode(AppState *state, ViewMode mode);
void myfm_show_this_pc(AppState *state);
void myfm_cancel_pending_dir_load(AppState *state);
gboolean myfm_path_is_inside(const char *child, const char *parent);

void go_back(GtkButton *button, gpointer user_data);
void go_forward(GtkButton *button, gpointer user_data);
void go_up(GtkButton *button, gpointer user_data);
void address_activate(GtkEntry *entry, gpointer user_data);

void directory_button_clicked(GtkButton *button, gpointer user_data);
GtkWidget *create_directory_button(const char *icon_name, const char *emoji,
                                   const char *label, const char *path,
                                   AppState *state);
void this_pc_button_clicked(GtkButton *button, gpointer user_data);
void trash_button_clicked(GtkButton *button, gpointer user_data);

void myfm_set_sort(AppState *state, SortColumn column);
void myfm_toggle_sort_direction(AppState *state);
void myfm_toggle_hidden(AppState *state);
void myfm_set_filter(AppState *state, const char *text);
void myfm_update_actions(AppState *state);
void myfm_on_clipboard_changed(GdkClipboard *cb, gpointer window);

void myfm_new_folder(AppState *state);
void myfm_new_file(AppState *state);
void myfm_cut_selected(AppState *state);
void myfm_copy_selected(AppState *state);
void myfm_copy_path_selected(AppState *state);
void myfm_paste(AppState *state);
void myfm_paste_into(AppState *state, const char *dest_dir);
void myfm_rename_selected(AppState *state);
void myfm_trash_selected(AppState *state);
void myfm_delete_selected(AppState *state);
void myfm_properties_selected(AppState *state);
void myfm_open_selected(AppState *state);
void myfm_open_with_path(AppState *state, const char *path);
void myfm_open_with_selected(AppState *state);
void myfm_open_terminal(AppState *state);
void myfm_empty_trash(AppState *state);
void myfm_eject_selected(AppState *state);
void myfm_select_all(AppState *state);

void myfm_toggle_preview(AppState *state);
void myfm_toggle_checkboxes(AppState *state);
void myfm_update_preview(AppState *state);
void myfm_clear_multi_selection(AppState *state);
void myfm_toggle_item_selection(AppState *state, GtkWidget *w,
                                const char *path, gboolean is_dir,
                                ItemKind kind, gboolean removable);
void myfm_import_paths(AppState *state, GSList *files, const char *dest_dir);

const char *file_icon_for(const char *name, gboolean is_dir);

GtkWidget *myfm_menu_row(const char *icon, const char *label,
                         GCallback cb, gpointer data, GtkWidget *popover);
GtkWidget *myfm_menu_separator(void);
void myfm_attach_background_menu(AppState *state);
void myfm_attach_marquee(AppState *state);
void myfm_watch_volumes(AppState *state);
void myfm_sel_item_free(gpointer item);

void app_state_free(AppState *state);

/* ── gui.c: properties dialog ── */
void show_properties_dialog(GtkWidget *parent, const char *path);

/* ── gui.c: animations (no-ops when animations are turned off) ── */
void anim_fade_in(GtkWidget *widget);
void anim_slide_in_from_left(GtkWidget *widget);
void anim_pulse_icon(GtkWidget *widget);
void anim_row_appear(GtkWidget *widget, guint delay_ms);
void anim_view_transition(GtkWidget *file_view);
gboolean anim_enabled(void);

#endif /* MYFM_H */
