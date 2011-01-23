/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2011 Armin Burgmeier <armin@arbur.net>
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
 * Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <libinfinity/common/inf-user-table.h>
#include <libinfinity/inf-marshal.h>
#include <libinfinity/inf-signals.h>

#include <string.h>

/**
 * SECTION:inf-user-table
 * @title: InfUserTable
 * @short_description: User information storage
 * @include: libinfinity/common/inf-user-table.h
 * @see_also: #InfUser, #InfSession
 * @stability: Unstable
 *
 * #InfUserTable manages multiple #InfUser objects and provides an easy way to
 * look up users by their ID and name. All users within a user table must have
 * a unique ID and name. The user table is used by #InfSession to store the
 * users within the session.
 */

typedef struct _InfUserTableForeachUserData InfUserTableForeachUserData;
struct _InfUserTableForeachUserData {
  InfUserTableForeachUserFunc func;
  gpointer user_data;
};

typedef struct _InfUserTablePrivate InfUserTablePrivate;
struct _InfUserTablePrivate {
  GHashTable* table;
  GSList* locals;
};

enum {
  ADD_USER,
  REMOVE_USER,

  ADD_LOCAL_USER,
  REMOVE_LOCAL_USER,

  LAST_SIGNAL
};

#define INF_USER_TABLE_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TYPE_USER_TABLE, InfUserTablePrivate))

static GObjectClass* parent_class;
static guint user_table_signals[LAST_SIGNAL];

static gboolean
inf_user_table_is_local(InfUser* user)
{
  /* User counts as local when it has the local flag set and is available */
  if( (inf_user_get_flags(user) & INF_USER_LOCAL) == 0)
    return FALSE;
  if(inf_user_get_status(user) == INF_USER_UNAVAILABLE)
    return FALSE;

  return TRUE;
}

static void
inf_user_table_check_local_cb(GObject* object,
                              GParamSpec* pspec,
                              gpointer user_data)
{
  InfUserTable* user_table;
  InfUserTablePrivate* priv;
  GSList* item;

  user_table = INF_USER_TABLE(user_data);
  priv = INF_USER_TABLE_PRIVATE(user_table);
  item = g_slist_find(priv->locals, INF_USER(object));

  if(inf_user_table_is_local(INF_USER(object)) && item == NULL)
  {
    g_signal_emit(
      G_OBJECT(user_table),
      user_table_signals[ADD_LOCAL_USER],
      0,
      INF_USER(object)
    );
  }
  
  if(!inf_user_table_is_local(INF_USER(object)) && item != NULL)
  {
    g_signal_emit(
      G_OBJECT(user_table),
      user_table_signals[REMOVE_LOCAL_USER],
      0,
      INF_USER(object)
    );
  }
}

static void
inf_user_table_unref_user(InfUserTable* user_table,
                          InfUser* user)
{
  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(user),
    G_CALLBACK(inf_user_table_check_local_cb),
    user_table
  );

  g_object_unref(user);
}

static void
inf_user_table_dispose_foreach_func(gpointer key,
                                    gpointer value,
                                    gpointer user_data)
{
  inf_user_table_unref_user(INF_USER_TABLE(user_data), INF_USER(value));
}

/*
 * User table callbacks.
 */

static gboolean
inf_user_table_lookup_user_by_name_func(gpointer key,
                                        gpointer value,
                                        gpointer data)
{
  const gchar* user_name;
  user_name = inf_user_get_name(INF_USER(value));

  if(strcmp(user_name, (const gchar*)data) == 0) return TRUE;
  return FALSE;
}

static void
inf_user_table_foreach_user_func(gpointer key,
                                 gpointer value,
                                 gpointer user_data)
{
  InfUserTableForeachUserData* data;
  data = (InfUserTableForeachUserData*)user_data;

  data->func(INF_USER(value), data->user_data);
}

static void
inf_user_table_init(GTypeInstance* instance,
                    gpointer g_class)
{
  InfUserTable* user_table;
  InfUserTablePrivate* priv;

  user_table = INF_USER_TABLE(instance);
  priv = INF_USER_TABLE_PRIVATE(user_table);

  priv->table = g_hash_table_new_full(NULL, NULL, NULL, NULL);
  priv->locals = NULL;
}

static void
inf_user_table_dispose(GObject* object)
{
  InfUserTable* user_table;
  InfUserTablePrivate* priv;

  user_table = INF_USER_TABLE(object);
  priv = INF_USER_TABLE_PRIVATE(user_table);

  g_slist_free(priv->locals);
  priv->locals = NULL;

  g_hash_table_foreach(
    priv->table,
    inf_user_table_dispose_foreach_func,
    user_table
  );

  g_hash_table_remove_all(priv->table);
  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_user_table_finalize(GObject* object)
{
  InfUserTable* user_table;
  InfUserTablePrivate* priv;

  user_table = INF_USER_TABLE(object);
  priv = INF_USER_TABLE_PRIVATE(user_table);

  g_hash_table_destroy(priv->table);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_user_table_add_user_handler(InfUserTable* user_table,
                                InfUser* user)
{
  InfUserTablePrivate* priv;
  guint id;

  priv = INF_USER_TABLE_PRIVATE(user_table);
  id = inf_user_get_id(user);

  g_assert(id > 0);
  g_assert(g_hash_table_lookup(priv->table, GUINT_TO_POINTER(id)) == NULL);

  g_hash_table_insert(priv->table, GUINT_TO_POINTER(id), user);
  g_object_ref(user);

  g_signal_connect(
    G_OBJECT(user),
    "notify::status",
    G_CALLBACK(inf_user_table_check_local_cb),
    user_table
  );

  g_signal_connect(
    G_OBJECT(user),
    "notify::flags",
    G_CALLBACK(inf_user_table_check_local_cb),
    user_table
  );

  if(inf_user_table_is_local(user))
  {
    g_signal_emit(
      G_OBJECT(user_table),
      user_table_signals[ADD_LOCAL_USER],
      0,
      user
    );
  }
}

static void
inf_user_table_remove_user_handler(InfUserTable* user_table,
                                   InfUser* user)
{
  InfUserTablePrivate* priv;
  guint id;

  priv = INF_USER_TABLE_PRIVATE(user_table);
  id = inf_user_get_id(user);

  if(inf_user_table_is_local(user))
  {
    g_signal_emit(
      G_OBJECT(user_table),
      user_table_signals[REMOVE_LOCAL_USER],
      0,
      user
    );
  }

  inf_user_table_unref_user(user_table, user);
  g_assert(g_hash_table_lookup(priv->table, GUINT_TO_POINTER(id)) == user);
  g_hash_table_remove(priv->table, GUINT_TO_POINTER(id));
}

static void
inf_user_table_add_local_user(InfUserTable* user_table,
                              InfUser* user)
{
  InfUserTablePrivate* priv;
  priv = INF_USER_TABLE_PRIVATE(user_table);

  g_assert(g_slist_find(priv->locals, user) == NULL);
  priv->locals = g_slist_prepend(priv->locals, user);
}

static void
inf_user_table_remove_local_user(InfUserTable* user_table,
                                 InfUser* user)
{
  InfUserTablePrivate* priv;
  priv = INF_USER_TABLE_PRIVATE(user_table);

  g_assert(g_slist_find(priv->locals, user) != NULL);
  priv->locals = g_slist_remove(priv->locals, user);
}

static void
inf_user_table_class_init(gpointer g_class,
                          gpointer class_data)
{
  GObjectClass* object_class;
  InfUserTableClass* user_table_class;

  object_class = G_OBJECT_CLASS(g_class);
  user_table_class = INF_USER_TABLE_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfUserTablePrivate));

  object_class->dispose = inf_user_table_dispose;
  object_class->finalize = inf_user_table_finalize;

  user_table_class->add_user = inf_user_table_add_user_handler;
  user_table_class->remove_user = inf_user_table_remove_user_handler;
  user_table_class->add_local_user = inf_user_table_add_local_user;
  user_table_class->remove_local_user = inf_user_table_remove_local_user;

  /**
   * InfUserTable::add-user:
   * @user_table: The #InfUserTable into which @user has been added
   * @user: The #InfUser that has been added into @user_table
   *
   * This signal is emitted when inf_user_table_add_user() is called. Note
   * that this does not happen if @user rejoins the session and has already
   * been added to @user_table previously.
   *
   * #InfUserTable::add-local-user may also be emitted at this point if
   * @user has the %INF_USER_LOCAL flag set.
   */
  user_table_signals[ADD_USER] = g_signal_new(
    "add-user",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfUserTableClass, add_user),
    NULL, NULL,
    inf_marshal_VOID__OBJECT,
    G_TYPE_NONE,
    1,
    INF_TYPE_USER
  );

  /**
   * InfUserTable::remove-user:
   * @user_table: The #InfUserTable from which @user has been remove
   * @user: The #InfUser that has been removed from @user_table
   *
   * This signal is emitted when inf_user_table_remove_user() is called. This
   * does not usually happen, as users leaving a session do not get removed
   * from the table.
   *
   * #InfUserTable::remove-local-user may also be emitted at this point if
   * @user has the %INF_USER_LOCAL flag set.
   */
  user_table_signals[REMOVE_USER] = g_signal_new(
    "remove-user",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfUserTableClass, remove_user),
    NULL, NULL,
    inf_marshal_VOID__OBJECT,
    G_TYPE_NONE,
    1,
    INF_TYPE_USER
  );

  /**
   * InfUserTable::add-local-user:
   * @user_table: The #InfUserTable in which @user has been set as local
   * @user: The #InfUser that has set as local
   *
   * This signal is emitted when a user is added to the user table and has the
   * %INF_USER_LOCAL flag set. In this case, #InfUserTable::add-user is
   * emitted as well.
   *
   * This signal is also emitted when an existing user receives the
   * %INF_USER_LOCAL flag. This occurs when a user rejoins locally after
   * leaving the session (possibly having the %INF_USER_LOCAL flag removed
   * during their absence). #InfUserTable::add-user is not emitted in this
   * case.
   */
  user_table_signals[ADD_LOCAL_USER] = g_signal_new(
    "add-local-user",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfUserTableClass, add_local_user),
    NULL, NULL,
    inf_marshal_VOID__OBJECT,
    G_TYPE_NONE,
    1,
    INF_TYPE_USER
  );

  /**
   * InfUserTable::remove-local-user:
   * @user_table: The #InfUserTable in which @user is no longer local
   * @user: The #InfUser that is no longer local
   *
   * This signal is emitted when a user is removed from the user table and
   * had the %INF_USER_LOCAL flag set. In this case,
   * #InfUserTable::remove-user is emitted as well.
   *
   * This signal is also emitted when @user loses the %INF_USER_LOCAL flag.
   * This occurs when the local @user leaves the session.
   * #InfUserTable::remove-user is not emitted and the status of @user is set
   * to %INF_USER_UNAVAILABLE.
   */
  user_table_signals[REMOVE_LOCAL_USER] = g_signal_new(
    "remove-local-user",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfUserTableClass, remove_local_user),
    NULL, NULL,
    inf_marshal_VOID__OBJECT,
    G_TYPE_NONE,
    1,
    INF_TYPE_USER
  );
}

GType
inf_user_table_get_type(void)
{
  static GType user_table_type = 0;

  if(!user_table_type)
  {
    static const GTypeInfo user_table_type_info = {
      sizeof(InfUserTableClass),   /* class_size */
      NULL,                        /* base_init */
      NULL,                        /* base_finalize */
      inf_user_table_class_init,   /* class_init */
      NULL,                        /* class_finalize */
      NULL,                        /* class_data */
      sizeof(InfUserTable),        /* instance_size */
      0,                           /* n_preallocs */
      inf_user_table_init,         /* instance_init */
      NULL                         /* value_table */
    };

    user_table_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfUserTable",
      &user_table_type_info,
      0
    );
  }

  return user_table_type;
}

/**
 * inf_user_table_new:
 *
 * Creates a new, empty user table.
 *
 * Return Value: A #InfUserTable.
 **/
InfUserTable*
inf_user_table_new(void)
{
  return INF_USER_TABLE(g_object_new(INF_TYPE_USER_TABLE, NULL));
}

/**
 * inf_user_table_add_user:
 * @user_table: A #InfUserTable.
 * @user: A #InfUser not already contained in @user_table.
 *
 * Inserts @user into @user_table.
 */
void
inf_user_table_add_user(InfUserTable* user_table,
                        InfUser* user)
{
  g_return_if_fail(INF_IS_USER_TABLE(user_table));
  g_return_if_fail(INF_IS_USER(user));

  g_signal_emit(G_OBJECT(user_table), user_table_signals[ADD_USER], 0, user);
}

/**
 * inf_user_table_remove_user:
 * @user_table: A #InfUserTable.
 * @user: A #InfUser contained in @table.
 *
 * Removes @user from @uesr_table.
 **/
void
inf_user_table_remove_user(InfUserTable* user_table,
                           InfUser* user)
{
  g_return_if_fail(INF_IS_USER_TABLE(user_table));
  g_return_if_fail(INF_IS_USER(user));

  g_signal_emit(
    G_OBJECT(user_table),
    user_table_signals[REMOVE_USER],
    0,
    user
  );
}

/**
 * inf_user_table_lookup_user_by_id:
 * @user_table: A #InfUserTable.
 * @id: User ID to lookup.
 *
 * Returns the #InfUser with the given User ID in @user_table.
 *
 * Return Value: A #InfUser, or %NULL.
 **/
InfUser*
inf_user_table_lookup_user_by_id(InfUserTable* user_table,
                                 guint id)
{
  InfUserTablePrivate* priv;

  g_return_val_if_fail(INF_IS_USER_TABLE(user_table), NULL);

  priv = INF_USER_TABLE_PRIVATE(user_table);

  return INF_USER(g_hash_table_lookup(priv->table, GUINT_TO_POINTER(id)));
}

/**
 * inf_user_table_lookup_user_by_name:
 * @user_table: A #InfUserTable.
 * @name: User name to lookup.
 *
 * Returns an #InfUser with the given name if there is one.
 *
 * Return Value: A #InfUser, or %NULL.
 **/
InfUser*
inf_user_table_lookup_user_by_name(InfUserTable* user_table,
                                   const gchar* name)
{
  InfUserTablePrivate* priv;
  InfUser* user;

  g_return_val_if_fail(INF_IS_USER_TABLE(user_table), NULL);
  g_return_val_if_fail(name != NULL, NULL);

  priv = INF_USER_TABLE_PRIVATE(user_table);

  user = g_hash_table_find(
    priv->table,
    inf_user_table_lookup_user_by_name_func,
    *(gpointer*) (gpointer) &name /* cast const away without warning */
  );

  return user;
}

/**
 * inf_user_table_foreach_user:
 * @user_table: A #InfUserTable.
 * @func: The function to call for each user.
 * @user_data: User data to pass to the function.
 *
 * Calls the given function for each user in the user_table. You should not
 * add or remove users while this function is being executed.
 **/
void
inf_user_table_foreach_user(InfUserTable* user_table,
                            InfUserTableForeachUserFunc func,
                            gpointer user_data)
{
  InfUserTablePrivate* priv;
  InfUserTableForeachUserData data;

  g_return_if_fail(INF_IS_USER_TABLE(user_table));
  g_return_if_fail(func != NULL);

  priv = INF_USER_TABLE_PRIVATE(user_table);

  data.func = func;
  data.user_data = user_data;

  g_hash_table_foreach(
    priv->table,
    inf_user_table_foreach_user_func,
    &data
  );
}

/**
 * inf_user_table_foreach_local_user:
 * @user_table: A #InfUserTable.
 * @func: The function to call for each user.
 * @user_data: User data to pass to the function.
 *
 * Calls the given function for each local user in the user_table. A local
 * user is a user that has the %INF_USER_LOCAL flag set and that has not
 * status %INF_USER_UNAVAILABLE. You should not add or remove users while this
 * function is being executed.
 **/
void
inf_user_table_foreach_local_user(InfUserTable* user_table,
                                  InfUserTableForeachUserFunc func,
                                  gpointer user_data)
{
  InfUserTablePrivate* priv;
  GSList* item;

  g_return_if_fail(INF_IS_USER_TABLE(user_table));
  g_return_if_fail(func != NULL);

  priv = INF_USER_TABLE_PRIVATE(user_table);
  
  for(item = priv->locals; item != NULL; item = g_slist_next(item))
    func(INF_USER(item->data), user_data);
}

/* vim:set et sw=2 ts=2: */
