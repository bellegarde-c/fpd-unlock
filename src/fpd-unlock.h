/*
 * Copyright Cedric Bellegarde <cedric.bellegarde@adishatz.org>
 */

#ifndef FPD_UNLOCK_H
#define FPD_UNLOCK_H

#include <glib.h>
#include <glib-object.h>

#define TYPE_FPD_UNLOCK \
    (fpd_unlock_get_type ())
#define FPD_UNLOCK(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST \
    ((obj), TYPE_FPD_UNLOCK, FpdUnlock))
#define FPD_UNLOCK_CLASS(cls) \
    (G_TYPE_CHECK_CLASS_CAST \
    ((cls), TYPE_FPD_UNLOCK, FpdUnlockClass))
#define IS_FPD_UNLOCK(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE \
    ((obj), TYPE_FPD_UNLOCK))
#define IS_FPD_UNLOCK_CLASS(cls) \
    (G_TYPE_CHECK_CLASS_TYPE \
    ((cls), TYPE_FPD_UNLOCK))
#define FPD_UNLOCK_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS \
    ((obj), TYPE_FPD_UNLOCK, FpdUnlockClass))

G_BEGIN_DECLS

typedef struct _FpdUnlock FpdUnlock;
typedef struct _FpdUnlockClass FpdUnlockClass;
typedef struct _FpdUnlockPrivate FpdUnlockPrivate;

struct _FpdUnlock {
    GObject parent;
    FpdUnlockPrivate *priv;
};

struct _FpdUnlockClass {
    GObjectClass parent_class;
};

GType           fpd_unlock_get_type            (void) G_GNUC_CONST;

GObject*        fpd_unlock_new                 (void);

G_END_DECLS

#endif

