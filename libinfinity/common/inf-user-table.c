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

#include <libinfinity/common/inf-user-table.h>
#include <libinfinity/inf-marshal.h>

#include <string.h>

typedef struct _InfUserTableForeachUserData InfUserTableForeachUserData;
struct _InfUserTableForeachUserData {
  InfUserTableForeachUserFunc func;
  gpointer user_data;
};

typedef struct _InfUserTablePrivate InfUserTablePrivate;
struct _InfUserTablePrivate {
  GHashTable* table;
};

enum {
  ADD_USER,
  REMOVE_USER,

  LAST_SIGNAL
};

#define INF_USER_TABLE_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TYPE_USER_TABLE, InfUserTablePrivate))

static GObjectClass* parent_class;
static guint user_table_signals[LAST_SIGNAL];

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

  priv->table = g_hash_table_new_full(
    NULL,
    NULL,
    NULL,
    (GDestroyNotify)g_object_unref
  );
}

static void
inf_user_table_dispose(GObject* object)
{
  InfUserTable* user_table;
  InfUserTablePrivate* priv;

  user_table = INF_USER_TABLE(object);
  priv = INF_USER_TABLE_PRIVATE(user_table);

  g_hash_table_remove_all(priv->table);
  G_OBJECT_CLASS(object)->dispose(object);
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
}

static void
inf_user_table_remove_user_handler(InfUserTable* user_table,
                                   InfUser* user)
{
  InfUserTablePrivate* priv;
  guint id;

  priv = INF_USER_TABLE_PRIVATE(user_table);
  id = inf_user_get_id(user);

  g_assert(g_hash_table_lookup(priv->table, GUINT_TO_POINTER(id)) == user);
  g_hash_table_remove(priv->table, GUINT_TO_POINTER(id));
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

/** inf_user_table_new:
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

/** inf_user_table_add_user:
 *
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

/** inf_user_table_remove_user:
 *
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

/** inf_user_table_lookup_user_by_id:
 *
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

  priv = INF_USER_TABLE_PRIVATE(priv);

  return INF_USER(g_hash_table_lookup(priv->table, GUINT_TO_POINTER(id)));
}

/** inf_user_table_lookup_user_by_name:
 *
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
    (gpointer)name
  );

  return user;
}

/** inf_user_table_foreach:
 *
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

/* vim:set et sw=2 ts=2: */
