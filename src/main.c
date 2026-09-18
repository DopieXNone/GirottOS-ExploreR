/* main.c – MyFM entry point (Wayland only) */

#include "myfm.h"

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
        myfm_window_new(GTK_APPLICATION(app), path);
        g_free(path);
    }
}

int
main(int argc, char **argv)
{
    /* Wayland-first: never fall back to X11 / XWayland. */
    if (g_getenv("WAYLAND_DISPLAY") != NULL)
        gdk_set_allowed_backends("wayland");

    GtkApplication *app = gtk_application_new(
        "com.myfm.FileManager",
        G_APPLICATION_HANDLES_OPEN);

    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    g_signal_connect(app, "open",     G_CALLBACK(on_open),     NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);

    g_object_unref(app);
    return status;
}
