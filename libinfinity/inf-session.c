/* infinote - Collaborative notetaking application
 * Copyright (C) 2007 Armin Burgmeier
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <libinfinity/inf-session.h>
#include <libinfinity/inf-marshal.h>

typedef struct _InfSessionPrivate InfSessionPrivate;
struct _InfSessionPrivate {
  GHashTable* user_table;
};

typedef struct _InfSessionForeachUserData InfSessionForeachUserData;
struct _InfSessionForeachUserData {
  InfSessionForeachUserFunc func;
  gpointer user_data;
};

enum {
  PROP_0,

  PROP_CONNECTION_MANAGER,
  PROP_BUFFER,
  PROP_SYNCHRONIZATION_MODE
};

enum {
  ADD_USER,
  REMOVE_USER,

  LAST_SIGNAL
};

#define INF_SESSION_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TYPE_USER, InfSessionPrivate))

static GObjectClass* parent_class;
static guint session_signals[LAST_SIGNAL];

static void
inf_session_foreach_user_func(gpointer key,
                              gpointer value,
                              gpointer user_data)
{
  InfSessionForeachUserData* data;
  data = (InfSessionForeachUserData*)user_data;

  data->func(INF_USER(value), data->user_data);
}

static void
inf_session_init(GTypeInstance* instance,
                 gpointer g_class)
{
  InfSession* session;
  InfSessionPrivate* priv;

  session = INF_SESSION(instance);
  priv = INF_SESSION_PRIVATE(session);

  priv->user_table = g_hash_table_new_full(
    NULL,
    NULL,
    NULL,
    (GDestroyNotify)g_object_unref
  );
}

static void
inf_session_dispose(GObject* object)
{
  InfSession* session;
  InfSessionPrivate* priv;

  session = INF_SESSION(object);
  priv = INF_SESSION_PRIVATE(session);

  g_hash_table_remove_all(priv->user_table);

  G_OBJECT_CLASS(object)->dispose(object);
}

static void
inf_session_finalize(GObject* object)
{
  InfSession* session;
  InfSessionPrivate* priv;

  session = INF_SESSION(object);
  priv = INF_SESSION_PRIVATE(session);

  g_hash_table_destroy(priv->user_table);
  priv->user_table = NULL;

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_session_add_user_handler(InfSession* session,
                             InfUser* user)
{
  InfSessionPrivate* priv;
  guint user_id;

  g_return_if_fail(INF_IS_SESSION(session));
  g_return_if_fail(INF_IS_USER(user));

  user_id = inf_user_get_id(user);
  g_return_if_fail(user_id > 0);

  priv = INF_SESSION_PRIVATE(session);

  g_return_if_fail(
    g_hash_table_lookup(priv->user_table, GUINT_TO_POINTER(user_id)) == NULL
  );

  g_hash_table_insert(priv->user_table, GUINT_TO_POINTER(user_id), user);
}

static void
inf_session_remove_user_handler(InfSession* session,
                                InfUser* user)
{
  InfSessionPrivate* priv;
  guint user_id;

  g_return_if_fail(INF_IS_SESSION(session));
  g_return_if_fail(INF_IS_USER(user));

  priv = INF_SESSION_PRIVATE(session);
  user_id = inf_user_get_id(user);

  g_return_if_fail(
    g_hash_table_lookup(priv->user_table, GUINT_TO_POINTER(user_id)) == user
  );

  g_hash_table_remove(priv->user_table, GUINT_TO_POINTER(user_id));
}

static void
inf_session_class_init(gpointer g_class,
                       gpointer class_data)
{
  GObjectClass* object_class;
  InfSessionClass* session_class;

  object_class = G_OBJECT_CLASS(g_class);
  session_class = INF_SESSION_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfSessionPrivate));

  object_class->dispose = inf_session_dispose;
  object_class->finalize = inf_session_finalize;

  session_class->add_user = inf_session_add_user_handler;
  session_class->remove_user = inf_session_remove_user_handler;

  session_signals[ADD_USER] = g_signal_new(
    "add-user",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfSessionClass, add_user),
    NULL, NULL,
    inf_marshal_VOID__OBJECT,
    G_TYPE_NONE,
    1,
    INF_TYPE_USER
  );

  session_signals[REMOVE_USER] = g_signal_new(
    "remove-user",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfSessionClass, remove_user),
    NULL, NULL,
    inf_marshal_VOID__OBJECT,
    G_TYPE_NONE,
    1,
    INF_TYPE_USER
  );
}

GType
inf_session_get_type(void)
{
  static GType session_type = 0;

  if(!session_type)
  {
    static const GTypeInfo session_type_info = {
      sizeof(InfSessionClass),  /* class_size */
      NULL,                               /* base_init */
      NULL,                               /* base_finalize */
      inf_session_class_init,  /* class_init */
      NULL,                               /* class_finalize */
      NULL,                               /* class_data */
      sizeof(InfSession),       /* instance_size */
      0,                                  /* n_preallocs */
      inf_session_init,        /* instance_init */
      NULL                                /* value_table */
    };

    session_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfSession",
      &session_type_info,
      0
    );
  }

  return session_type;
}

/** inf_session_add_user:
 *
 * @session A #InfSession.
 * @user: A #InfUser not already contained in @session.
 *
 * Adds @user to @session. This function will most likely only be useful
 * to types inheriting from #InfSession to add a #InfUser to @session.
 **/
void
inf_session_add_user(InfSession* session,
                     InfUser* user)
{
  InfSessionPrivate* priv;
  guint user_id;

  g_return_if_fail(INF_IS_SESSION(session));
  g_return_if_fail(INF_IS_USER(user));

  user_id = inf_user_get_id(user);
  g_return_if_fail(user_id > 0);

  priv = INF_SESSION_PRIVATE(session);

  g_return_if_fail(
    g_hash_table_lookup(priv->user_table, GUINT_TO_POINTER(user_id)) == NULL
  );

  g_signal_emit(G_OBJECT(session), session_signals[ADD_USER], 0, user);
}

/** inf_session_remove_user:
 *
 * @session: A #InfSession.
 * @user: A #InfUser contained in session.
 *
 * Removes @user from @session. This function will most likely only be useful
 * to types inheriting from #InfSession to add a #InfUser to @session.
 **/
void
inf_session_remove_user(InfSession* session,
                        InfUser* user)
{
  InfSessionPrivate* priv;
  guint user_id;

  g_return_if_fail(INF_IS_SESSION(session));
  g_return_if_fail(INF_IS_USER(user));

  priv = INF_SESSION_PRIVATE(session);
  user_id = inf_user_get_id(user);

  g_return_if_fail(
    g_hash_table_lookup(priv->user_table, GUINT_TO_POINTER(user_id)) == user
  );

  g_object_ref(G_OBJECT(user));
  g_signal_emit(G_OBJECT(session), session_signals[REMOVE_USER], 0, user);
  g_object_unref(G_OBJECT(user));
}

/** inf_session_lookup_user_by_id:
 *
 * @session: A #InfSession.
 * @user_id: User ID to lookup.
 *
 * Returns the #InfUser with the given User ID in session.
 *
 * Return Value: A #InfUser, or %NULL.
 **/
InfUser*
inf_session_lookup_user_by_id(InfSession* session,
                              guint user_id)
{
  InfSessionPrivate* priv;

  g_return_val_if_fail(INF_IS_SESSION(session), NULL);

  priv = INF_SESSION_PRIVATE(priv);

  return INF_USER(
    g_hash_table_lookup(priv->user_table, GUINT_TO_POINTER(user_id))
  );
}

/** inf_session_foreach_user:
 *
 * @session: A #InfSession.
 * @func: The function to call for each user.
 * @user_data: User data to pass to the function.
 *
 * Calls the given function for each user in the session. You should not
 * add or remove uses while this function is being executed.
 **/
void
inf_session_foreach_user(InfSession* session,
                         InfSessionForeachUserFunc func,
                         gpointer user_data)
{
  InfSessionPrivate* priv;
  InfSessionForeachUserData data;

  g_return_if_fail(INF_IS_SESSION(session));
  g_return_if_fail(func != NULL);

  priv = INF_SESSION_PRIVATE(session);

  data.func = func;
  data.user_data = user_data;

  g_hash_table_foreach(
    priv->user_table,
    inf_session_foreach_user_func,
    &data
  );
}
