/*
 * Copyright Cedric Bellegarde <cedric.bellegarde@adishatz.org>
 */

#include <stdio.h>
#include <stdarg.h>

#include <gio/gio.h>

#include "fpd-unlock.h"

#define DBUS_PROPERTIES              "org.freedesktop.DBus.Properties"

#define LOGIND_DBUS_NAME             "org.freedesktop.login1"
#define LOGIND_DBUS_SEAT_PATH        "/org/freedesktop/login1/session/auto"
#define LOGIND_DBUS_SEAT_INTERFACE   "org.freedesktop.login1.Session"
#define LOGIND_DBUS_UNLOCK_PATH      "/org/freedesktop/login1"
#define LOGIND_DBUS_UNLOCK_INTERFACE "org.freedesktop.login1.Manager"

#define FINGERPRINT_DBUS_NAME        "org.droidian.fingerprint"
#define FINGERPRINT_DBUS_PATH        "/org/droidian/fingerprint"
#define FINGERPRINT_DBUS_INTERFACE   "org.droidian.fingerprint"

#define UNLOCK_RATE 250


struct _FpdUnlockPrivate {
    GDBusProxy *logind_seat_proxy;
    GDBusProxy *logind_manager_proxy;
    GDBusProxy *fingerprint_proxy;

    const gchar *session_id;
    gboolean idle_hint;
    gboolean locked_hint;
    guint timeout_id;
};

G_DEFINE_TYPE_WITH_CODE (
    FpdUnlock,
    fpd_unlock,
    G_TYPE_OBJECT,
    G_ADD_PRIVATE (FpdUnlock)
)

static void
fpd_unlock_do (FpdUnlock *self)
{
    g_autoptr(GError) error = NULL;

    g_dbus_proxy_call_sync (
        self->priv->fingerprint_proxy,
        "UnlockSession",
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error
    );

    if (error != NULL) {
        g_warning ("Can't unlock current session: %s", error->message);
    }
}

static gboolean
fpd_unlock_handle_unlocking (FpdUnlock *self)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) identify = NULL;
    gint result;

    identify = g_dbus_proxy_call_sync (
        self->priv->fingerprint_proxy,
        "Identify",
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error
    );

    if (error != NULL) {
        g_warning ("Can't identify fingerprint: %s", error->message);
    }

    g_variant_get (identify, "(i)", &result);

    if (result == 0) {
        fpd_unlock_do (self);
    } else {
        self->priv->timeout_id = g_timeout_add_seconds (
            UNLOCK_RATE,
            (GSourceFunc) fpd_unlock_handle_unlocking,
            self
        );
    }

    return FALSE;
}

static void
fpd_get_active_session (FpdUnlock *self)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) value = NULL;
    g_autoptr(GVariant) session_id = NULL;

    value = g_dbus_proxy_call_sync (
        self->priv->logind_seat_proxy,
        "Get",
        g_variant_new ("(&ss)", LOGIND_DBUS_SEAT_INTERFACE, "Id"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error
    );

    if (error != NULL) {
        g_warning ("Can't get active session id: %s", error->message);
    }

    g_variant_get (value, "(v)", &session_id);
    self->priv->session_id = g_strdup (g_variant_get_string (session_id, NULL));
}

static void
fpd_get_idle_hint (FpdUnlock *self)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) value = NULL;
    g_autoptr(GVariant) idle_hint = NULL;

    value = g_dbus_proxy_call_sync (
        self->priv->logind_seat_proxy,
        "Get",
        g_variant_new ("(&ss)", LOGIND_DBUS_SEAT_INTERFACE, "IdleHint"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error
    );

    if (error != NULL) {
        g_warning ("Can't get active session id: %s", error->message);
    }

    g_variant_get (value, "(v)", &idle_hint);
    self->priv->idle_hint = g_variant_get_boolean(idle_hint);
}

static void
fpd_get_locked_hint (FpdUnlock *self)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) value = NULL;
    g_autoptr(GVariant) locked_hint = NULL;

    value = g_dbus_proxy_call_sync (
        self->priv->logind_seat_proxy,
        "Get",
        g_variant_new ("(&ss)", LOGIND_DBUS_SEAT_INTERFACE, "LockedHint"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error
    );

    if (error != NULL) {
        g_warning ("Can't get active session id: %s", error->message);
    }

    g_variant_get (value, "(v)", &locked_hint);
    self->priv->locked_hint = g_variant_get_boolean(locked_hint);
}

static void
fpd_logind_proxy_seat_properties_cb (GDBusProxy *proxy,
                                     GVariant   *changed_properties,
                                     char      **invalidated_properties,
                                     gpointer    user_data)
{
    FpdUnlock *self = user_data;
    GVariant *value;
    GVariantIter i;
    gchar *property;

    g_variant_iter_init (&i, changed_properties);
    while (g_variant_iter_next (&i, "{&sv}", &property, &value)) {
        if (g_strcmp0 (property, "IdleHint") == 0) {
            g_clear_handle_id (&self->priv->timeout_id, g_source_remove);
            self->priv->idle_hint = g_variant_get_boolean (value);

            if (!self->priv->idle_hint)
                fpd_unlock_handle_unlocking (self);
        } else if (g_strcmp0 (property, "LockedHint") == 0) {
            g_clear_handle_id (&self->priv->timeout_id, g_source_remove);
            self->priv->locked_hint = g_variant_get_boolean (value);
        }
        g_variant_unref (value);
    }
}

static void
fpd_unlock_dispose (GObject *fpd_unlock)
{
    FpdUnlock *self = FPD_UNLOCK (fpd_unlock);

    g_clear_handle_id (&self->priv->timeout_id, g_source_remove);

    g_clear_object (&self->priv->logind_seat_proxy);
    g_clear_object (&self->priv->logind_manager_proxy);
    g_clear_object (&self->priv->fingerprint_proxy);

    G_OBJECT_CLASS (fpd_unlock_parent_class)->dispose (fpd_unlock);
}

static void
fpd_unlock_finalize (GObject *fpd_unlock)
{
    FpdUnlock *self = FPD_UNLOCK (fpd_unlock);

    g_free (&self->priv->session_id);

    G_OBJECT_CLASS (fpd_unlock_parent_class)->finalize (fpd_unlock);
}

static void
fpd_unlock_class_init (FpdUnlockClass *klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS (klass);
    object_class->dispose = fpd_unlock_dispose;
    object_class->finalize = fpd_unlock_finalize;
}

static void
fpd_unlock_init (FpdUnlock *self)
{
    g_autoptr(GError) error = NULL;

    self->priv = fpd_unlock_get_instance_private (self);

    self->priv->timeout_id = 0;

    self->priv->logind_seat_proxy =
                g_dbus_proxy_new_for_bus_sync (
                    G_BUS_TYPE_SYSTEM,
                    0,
                    NULL,
                    LOGIND_DBUS_NAME,
                    LOGIND_DBUS_SEAT_PATH,
                    DBUS_PROPERTIES,
                    NULL,
                    NULL
               );

    g_assert (self->priv->logind_seat_proxy != NULL);

    self->priv->logind_manager_proxy =
                g_dbus_proxy_new_for_bus_sync (
                    G_BUS_TYPE_SYSTEM,
                    0,
                    NULL,
                    LOGIND_DBUS_NAME,
                    LOGIND_DBUS_UNLOCK_PATH,
                    LOGIND_DBUS_UNLOCK_INTERFACE,
                    NULL,
                    NULL
               );

    g_assert (self->priv->logind_manager_proxy != NULL);

    self->priv->fingerprint_proxy =
                g_dbus_proxy_new_for_bus_sync (
                    G_BUS_TYPE_SYSTEM,
                    0,
                    NULL,
                    FINGERPRINT_DBUS_NAME,
                    FINGERPRINT_DBUS_PATH,
                    FINGERPRINT_DBUS_INTERFACE,
                    NULL,
                    NULL
               );

    g_assert (self->priv->fingerprint_proxy != NULL);

    g_signal_connect (
        self->priv->logind_seat_proxy,
        "g-properties-changed",
        G_CALLBACK (fpd_logind_proxy_seat_properties_cb),
        self
    );

    fpd_get_locked_hint (self);
    fpd_get_idle_hint (self);
    fpd_get_active_session (self);
}


/**
 * fpd_unlock_new:
 *
 * Creates a new #FpdUnlock
 *
 * Returns: (transfer full): a new #FpdUnlock
 *
 **/
GObject *
fpd_unlock_new (void)
{
    GObject *fpd_unlock;

    fpd_unlock = g_object_new (TYPE_FPD_UNLOCK, NULL);

    return fpd_unlock;
}
