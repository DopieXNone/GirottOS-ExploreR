/* animations.c – lightweight GTK4 CSS transition animations */

#include "myfm.h"


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

void
anim_fade_in(GtkWidget *widget)
{
    if (!GTK_IS_WIDGET(widget)) return;

    gtk_widget_remove_css_class(widget, "anim-fade-in");
    gtk_widget_add_css_class(widget, "anim-fade-start");

    schedule_class_swap(widget, "anim-fade-start", "anim-fade-in", 16);
}

void
anim_slide_in_from_left(GtkWidget *widget)
{
    if (!GTK_IS_WIDGET(widget)) return;

    gtk_widget_remove_css_class(widget, "anim-slide-in");
    gtk_widget_add_css_class(widget, "anim-slide-start");

    schedule_class_swap(widget, "anim-slide-start", "anim-slide-in", 16);
}

void
anim_pulse_icon(GtkWidget *widget)
{
    if (!GTK_IS_WIDGET(widget)) return;

    gtk_widget_add_css_class(widget, "anim-pulse");
    gtk_widget_add_css_class(widget, "anim-pulse-big");

    schedule_class_swap(widget, "anim-pulse-big", NULL, 160);
}

/* Staggered row appearance – pass i * 18 for rows 0,1,2… */
void
anim_row_appear(GtkWidget *widget, guint delay_ms)
{
    if (!GTK_IS_WIDGET(widget)) return;

    gtk_widget_remove_css_class(widget, "anim-row-in");
    gtk_widget_add_css_class(widget, "anim-row-start");

    schedule_class_swap(widget, "anim-row-start", "anim-row-in", delay_ms + 16);
}

/* Cross-fade the whole file view when switching directories */
void
anim_view_transition(GtkWidget *file_view)
{
    if (!GTK_IS_WIDGET(file_view)) return;

    gtk_widget_add_css_class(file_view, "anim-fade-start");
    schedule_class_swap(file_view, "anim-fade-start", "anim-fade-in", 30);
}
