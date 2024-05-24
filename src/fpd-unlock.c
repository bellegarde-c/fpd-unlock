/*
 * Copyright Cedric Bellegarde <cedric.bellegarde@adishatz.org>
 */

#include <stdio.h>
#include <stdarg.h>

#include <gio/gio.h>

#include "config.h"
#include "fpd-unlock.h"

#define DBUS_PROPERTIES               "org.freedesktop.DBus.Properties"

#define LOGIND_DBUS_NAME              "org.freedesktop.login1"

#define LOGIND_DBUS_SESSION_INTERFACE "org.freedesktop.login1.Session"

#define LOGIND_DBUS_MANAGER_PATH      "/org/freedesktop/login1"
#define LOGIND_DBUS_MANAGER_INTERFACE "org.freedesktop.login1.Manager"

#define FINGERPRINT_DBUS_NAME         "org.droidian.fingerprint"
#define FINGERPRINT_DBUS_PATH         "/org/droidian/fingerprint"
#define FINGERPRINT_DBUS_INTERFACE    "org.droidian.fingerprint"

#define FEEDBACKD_DBUS_NAME           "org.sigxcpu.Feedback"
#define FEEDBACKD_DBUS_PATH           "/org/sigxcpu/Feedback"
#define FEEDBACKD_DBUS_INTERFACE      "org.sigxcpu.Feedback"

struct _FpdUnlockPrivate {
    GDBusProxy *logind_manager_proxy;
    GDBusProxy *logind_session_proxy;
    GDBusProxy *fingerprint_proxy;
    GDBusProxy *feedbackd_proxy;

    const gchar *session_id;
    const gchar *session_path;

    gboolean idle_hint;
    gboolean locked_hint;

    guint bus_timeout_id;
};

G_DEFINE_TYPE_WITH_CODE (
    FpdUnlock,
    fpd_unlock,
    G_TYPE_OBJECT,
    G_ADD_PRIVATE (FpdUnlock)
)

static void
fpd_unlock_feedback (FpdUnlock *self, const gchar *event)
{
    g_autoptr(GError) error = NULL;

    g_return_if_fail (self->priv->feedbackd_proxy != NULL);

    g_dbus_proxy_call_sync (
        self->priv->feedbackd_proxy,
        "TriggerFeedback",
        g_variant_new ("(&s&sa{sv}i)", APP_ID, event, NULL, -1),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error
    );

    if (error != NULL) {
        g_warning ("Can't send feedback: %s", error->message);
    }
}

static void
fpd_unlock_do (FpdUnlock *self)
{
    g_autoptr(GError) error = NULL;

    g_dbus_proxy_call_sync (
        self->priv->logind_manager_proxy,
        "UnlockSession",
        g_variant_new ("(&s)", self->priv->session_id),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error
    );

    if (error != NULL) {
        g_warning ("Can't unlock current session: %s", error->message);
    }
}

static void
fpd_unlock_start_unlocking (FpdUnlock *self)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) identify = NULL;

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
        g_warning ("Can't start fingerprint auth: %s", error->message);
    }
}

static void
fpd_unlock_stop_unlocking (FpdUnlock *self)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) abort = NULL;

    abort = g_dbus_proxy_call_sync (
        self->priv->fingerprint_proxy,
        "Abort",
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error
    );

    if (error != NULL) {
        g_warning ("Can't stop fingerprint auth: %s", error->message);
    }
}

static void
fpd_set_active_session (FpdUnlock *self)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) value = NULL;
    g_autoptr (GVariantIter) iter;

    value = g_dbus_proxy_call_sync (
        self->priv->logind_manager_proxy,
        "ListSessions",
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error
    );

    if (error != NULL) {
        g_warning ("Can't get active session id: %s", error->message);
    }

    g_variant_get (value, "(a(susso))", &iter);
    while (g_variant_iter_loop (iter, "(susso)", &self->priv->session_id,
                                NULL, NULL, NULL, &self->priv->session_path)) {
        if (g_strcmp0 (self->priv->session_id, "c2") == 0)
            return;
    }

    self->priv->session_id = NULL;
}

static void
on_fingerprint_proxy_signal (GDBusProxy  *proxy,
                             const gchar *sender_name,
                             const gchar *signal_name,
                             GVariant    *parameters,
                             gpointer     user_data)
{
    FpdUnlock *self = FPD_UNLOCK (user_data);

    if (g_strcmp0 (signal_name, "Identified") == 0) {
        const gchar *finger;

        g_variant_get (parameters, "(&s)", &finger);

        g_message ("Identified: %s", finger);
        fpd_unlock_do (self);
    } else if (g_strcmp0 (signal_name, "ErrorInfo") == 0) {
        const gchar *info;

        g_variant_get (parameters, "(&s)", &info);

        g_message ("ErrorInfo: %s", info);
        if (g_strcmp0 (info, "FINGER_NOT_RECOGNIZED") == 0)
            fpd_unlock_feedback (self, "bell-terminal");
    }
}

static void
on_logind_session_proxy_properties_changed (GDBusProxy *proxy,
                                            GVariant   *changed_properties,
                                            char      **invalidated_properties,
                                            gpointer    user_data)
{
    FpdUnlock *self = FPD_UNLOCK (user_data);
    GVariant *value;
    GVariantIter i;
    gchar *property;

    g_variant_iter_init (&i, changed_properties);
    while (g_variant_iter_next (&i, "{&sv}", &property, &value)) {
        if (g_strcmp0 (property, "IdleHint") == 0) {
            self->priv->idle_hint = g_variant_get_boolean (value);
            if (self->priv->idle_hint)
                fpd_unlock_stop_unlocking (self);
            else
                fpd_unlock_start_unlocking (self);
        } else if (g_strcmp0 (property, "LockedHint") == 0) {
            self->priv->locked_hint = g_variant_get_boolean (value);
            if (!self->priv->locked_hint)
                fpd_unlock_stop_unlocking (self);
        }
        g_variant_unref (value);
    }
}

static gboolean
fpd_wait_for_bus (FpdUnlock *self)
{

    if (self->priv->logind_manager_proxy == NULL)
        self->priv->logind_manager_proxy =
                    g_dbus_proxy_new_for_bus_sync (
                        G_BUS_TYPE_SYSTEM,
                        0,
                        NULL,
                        LOGIND_DBUS_NAME,
                        LOGIND_DBUS_MANAGER_PATH,
                        LOGIND_DBUS_MANAGER_INTERFACE,
                        NULL,
                        NULL
                   );

    g_return_val_if_fail (self->priv->logind_manager_proxy != NULL, TRUE);

    if (self->priv->fingerprint_proxy == NULL)
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

    g_return_val_if_fail (self->priv->fingerprint_proxy != NULL, TRUE);

    fpd_set_active_session (self);

    g_return_val_if_fail (self->priv->session_id != NULL, TRUE);

    if (self->priv->logind_session_proxy == NULL)
        self->priv->logind_session_proxy =
                    g_dbus_proxy_new_for_bus_sync (
                        G_BUS_TYPE_SYSTEM,
                        0,
                        NULL,
                        LOGIND_DBUS_NAME,
                        self->priv->session_path,
                        LOGIND_DBUS_SESSION_INTERFACE,
                        NULL,
                        NULL
                   );

    g_return_val_if_fail (self->priv->logind_session_proxy != NULL, TRUE);

    self->priv->feedbackd_proxy =
                g_dbus_proxy_new_for_bus_sync (
                    G_BUS_TYPE_SESSION,
                    0,
                    NULL,
                    FEEDBACKD_DBUS_NAME,
                    FEEDBACKD_DBUS_PATH,
                    FEEDBACKD_DBUS_INTERFACE,
                    NULL,
                    NULL
               );

    g_signal_connect (
        self->priv->fingerprint_proxy,
        "g-signal",
        G_CALLBACK (on_fingerprint_proxy_signal),
        self
    );

    g_signal_connect (
        self->priv->logind_session_proxy,
        "g-properties-changed",
        G_CALLBACK (on_logind_session_proxy_properties_changed),
        self
    );

    return FALSE;
}

static void
fpd_unlock_dispose (GObject *fpd_unlock)
{
    FpdUnlock *self = FPD_UNLOCK (fpd_unlock);

    g_clear_handle_id (&self->priv->bus_timeout_id, g_source_remove);

    g_clear_object (&self->priv->logind_session_proxy);
    g_clear_object (&self->priv->logind_manager_proxy);
    g_clear_object (&self->priv->fingerprint_proxy);
    g_clear_object (&self->priv->feedbackd_proxy);

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
    self->priv->logind_session_proxy = NULL;
    self->priv->logind_manager_proxy = NULL;
    self->priv->fingerprint_proxy = NULL;
    self->priv->feedbackd_proxy = NULL;

     self->priv->bus_timeout_id = g_timeout_add_seconds (
        1,
        (GSourceFunc) fpd_wait_for_bus,
        self
    );
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
