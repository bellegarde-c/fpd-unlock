/*
 * Copyright Cedric Bellegarde <cedric.bellegarde@adishatz.org>
 */

#include <stdlib.h>
#include <gio/gio.h>

#include "fpd-unlock.h"
#include "config.h"

gint
main (gint argc, gchar * argv[])
{
    GMainLoop *loop;
    GObject *fpd_unlock;
    g_autoptr (GOptionContext) context = NULL;
    g_autoptr (GError) error = NULL;
    gboolean version = FALSE;
    GOptionEntry main_entries[] = {
        {"version", 0, 0, G_OPTION_ARG_NONE, &version, "Show version"},
        {NULL}
    };

    context = g_option_context_new ("fpd unlock");
    g_option_context_add_main_entries (context, main_entries, NULL);

    if (!g_option_context_parse (context, &argc, &argv, &error)) {
        g_printerr ("%s\n", error->message);
        return EXIT_FAILURE;
    }

    if (version) {
        g_printerr ("%s\n", PACKAGE_VERSION);
        return EXIT_SUCCESS;
    }

    fpd_unlock = fpd_unlock_new ();

    loop = g_main_loop_new (NULL, FALSE);
    g_main_loop_run (loop);

    g_clear_pointer (&loop, g_main_loop_unref);
    g_clear_object (&fpd_unlock);

    return EXIT_SUCCESS;
}
