/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2010 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INF_USER_TABLE_H__
#define __INF_USER_TABLE_H__

#include <libinfinity/common/inf-user.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_USER_TABLE                 (inf_user_table_get_type())
#define INF_USER_TABLE(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_USER_TABLE, InfUserTable))
#define INF_USER_TABLE_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TYPE_USER_TABLE, InfUserTableClass))
#define INF_IS_USER_TABLE(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_USER_TABLE))
#define INF_IS_USER_TABLE_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TYPE_USER_TABLE))
#define INF_USER_TABLE_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TYPE_USER_TABLE, InfUserTableClass))

typedef struct _InfUserTable InfUserTable;
typedef struct _InfUserTableClass InfUserTableClass;

/**
 * InfUserTableClass:
 * @add_user: Default signal handler for the #InfUserTable::add_user signal.
 * @remove_user: Default signal handler for the #InfUserTable::remove_user
 * signal.
 * @add_local_user: Default signal handler for the
 * #InfUserTable::add_local_user signal.
 * @remove_local_user: Default signal handler for the
 * #InfUserTable::remove_local_user signal.
 *
 * Signals for the #InfUserTable class.
 */
struct _InfUserTableClass {
  /*< private >*/

  GObjectClass parent_class;

  /*< public >*/
  void(*add_user)(InfUserTable* user_table,
                  InfUser* user);

  void(*remove_user)(InfUserTable* user_table,
                     InfUser* user);

  void(*add_local_user)(InfUserTable* user_table,
                        InfUser* user);

  void(*remove_local_user)(InfUserTable* user_table,
                           InfUser* user);
};

/**
 * InfUserTable:
 *
 * #InfUserTable is an opaque data type. You should only access it via the
 * public API functions.
 */
struct _InfUserTable {
  /*< private >*/
  GObject parent;
};

/**
 * InfUserTableForeachUserFunc:
 * @user: The #InfUser for the current iteration.
 * @user_data: The user_data passed to inf_user_table_foreach_user() or
 * inf_user_table_foreach_local_user().
 *
 * This callback is called for every user iterated by
 * inf_user_table_foreach_user() or inf_user_table_foreach_local_user().
 */
typedef void(*InfUserTableForeachUserFunc)(InfUser* user, gpointer user_data);

GType
inf_user_table_get_type(void) G_GNUC_CONST;

InfUserTable*
inf_user_table_new(void);

void
inf_user_table_add_user(InfUserTable* user_table,
                        InfUser* user);

void
inf_user_table_remove_user(InfUserTable* user_table,
                           InfUser* user);

InfUser*
inf_user_table_lookup_user_by_id(InfUserTable* user_table,
                                 guint id);

InfUser*
inf_user_table_lookup_user_by_name(InfUserTable* user_table,
                                   const gchar* name);

void
inf_user_table_foreach_user(InfUserTable* user_table,
                            InfUserTableForeachUserFunc func,
                            gpointer user_data);

void
inf_user_table_foreach_local_user(InfUserTable* user_table,
                                  InfUserTableForeachUserFunc func,
                                  gpointer user_data);

G_END_DECLS

#endif /* __INF_USER_TABLE_H__ */

/* vim:set et sw=2 ts=2: */
