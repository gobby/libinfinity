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

/**
 * SECTION:inf-acl-table
 * @title: InfAclTable
 * @short_description: Managing permissions on notes
 * @include: libinfinity/common/inf-acl-table.h
 * @see_also: #InfBrowser
 * @stability: Unstable
 *
 * #InfAclTable manages the permissions set for different users for each
 * node in the directory tree. This class is mostly a helper class used by
 * #InfcBrowser and #InfdDirectory. Normally it should not be necessary to
 * use methods of this class directly, since it is fully exposed by the
 * #InfBrowser API.
 */

#include <libinfinity/common/inf-acl-table.h>
#include <libinfinity/common/inf-xml-util.h>
#include <libinfinity/common/inf-error.h>
#include <libinfinity/inf-marshal.h>
#include <libinfinity/inf-i18n.h>

typedef struct _InfAclTableGetUserListData InfAclTableGetUserListData;
struct _InfAclTableGetUserListData {
  const InfAclUser** users;
  guint index;
};

typedef struct _InfAclTablePrivate InfAclTablePrivate;
struct _InfAclTablePrivate {
  GHashTable* users;
  GHashTable* sheet_sets;
};

enum {
  USER_ADDED,
  ACL_CHANGED,

  LAST_SIGNAL
};

#define INF_ACL_TABLE_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TYPE_ACL_TABLE, InfAclTablePrivate))

static GObjectClass* parent_class;
static guint acl_table_signals[LAST_SIGNAL];

static gint64
inf_acl_table_get_real_time()
{
  /* TODO: Replace by g_get_real_time() once we depend on glib >=2.28 */
  GTimeVal timeval;
  g_get_current_time(&timeval);
  return (gint64)timeval.tv_sec * 1000000 + timeval.tv_usec;
}

static void
inf_acl_table_get_user_list_foreach_func(gpointer key,
                                         gpointer value,
                                         gpointer user_data)
{
  InfAclTableGetUserListData* data;
  data = (InfAclTableGetUserListData*)user_data;

  data->users[data->index++] = (const InfAclUser*)value;
}

static void
inf_acl_table_insert_sheets_impl(InfAclTable* table,
                                 const InfBrowserIter* iter,
                                 const InfAclSheet* sheets,
                                 guint n_sheets)
{
  InfAclTablePrivate* priv;
  InfAclSheet* sheet;
  InfAclSheet* announce_sheet;
  InfAclSheetSet* sheet_set;
  InfAclSheetSet* announce_set;
  InfAclSheetSet fixed_set;
  guint i;

  priv = INF_ACL_TABLE_PRIVATE(table);
  sheet_set = g_hash_table_lookup(
    priv->sheet_sets,
    GUINT_TO_POINTER(iter->node_id)
  );

  if(sheet_set == NULL)
  {
    sheet_set = inf_acl_sheet_set_new();

    g_hash_table_insert(
      priv->sheet_sets,
      GUINT_TO_POINTER(iter->node_id),
      sheet_set
    );
  }

  announce_set = inf_acl_sheet_set_new();
  for(i = 0; i < n_sheets; ++i)
  {
    if(sheets[i].mask == 0)
    {
      InfAclSheet* sheet =
        inf_acl_sheet_set_find_sheet(sheet_set, sheets[i].user);

      if(sheet != NULL)
      {
        inf_acl_sheet_set_remove_sheet(sheet_set, sheet);

        announce_sheet =
          inf_acl_sheet_set_add_sheet(announce_set, sheets[i].user);
        announce_sheet->mask = sheets[i].mask;
        announce_sheet->perms = sheets[i].perms;
      }
    }
    else
    {
      sheet = inf_acl_sheet_set_add_sheet(sheet_set, sheets[i].user);

      if(sheet->mask != sheets[i].mask ||
         ((sheet->perms & sheet->mask) != (sheets[i].perms & sheets[i].mask)))
      {
        sheet->mask = sheets[i].mask;
        sheet->perms = sheets[i].perms;

        announce_sheet =
          inf_acl_sheet_set_add_sheet(announce_set, sheets[i].user);
        announce_sheet->mask = sheets[i].mask;
        announce_sheet->perms = sheets[i].perms;
      }
    }
  }

  if(sheet_set->n_sheets == 0)
    g_hash_table_remove(priv->sheet_sets, GUINT_TO_POINTER(iter->node_id));

  if(announce_set->n_sheets > 0)
  {
    /* Announce with a fixed set, to prevent the sheets from being
     * modified. */
    fixed_set.own_sheets = NULL;
    fixed_set.sheets = announce_set->sheets;
    fixed_set.n_sheets = announce_set->n_sheets;

    g_signal_emit(table, acl_table_signals[ACL_CHANGED], 0, iter, &fixed_set);
  }

  inf_acl_sheet_set_free(announce_set);
}

static void
inf_acl_table_init(GTypeInstance* instance,
                   gpointer g_class)
{
  InfAclTable* table;
  InfAclTablePrivate* priv;

  table = INF_ACL_TABLE(instance);
  priv = INF_ACL_TABLE_PRIVATE(table);

  priv->users = g_hash_table_new_full(
    g_str_hash,
    g_str_equal,
    NULL,
    (GDestroyNotify)inf_acl_user_free
  );

  priv->sheet_sets = g_hash_table_new_full(
    NULL,
    NULL,
    NULL,
    (GDestroyNotify)inf_acl_sheet_set_free
  );
}

static void
inf_acl_table_dispose(GObject* object)
{
  InfAclTable* table;
  InfAclTablePrivate* priv;

  table = INF_ACL_TABLE(object);
  priv = INF_ACL_TABLE_PRIVATE(table);

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_acl_table_finalize(GObject* object)
{
  InfAclTable* table;
  InfAclTablePrivate* priv;

  table = INF_ACL_TABLE(object);
  priv = INF_ACL_TABLE_PRIVATE(table);

  g_hash_table_destroy(priv->sheet_sets);
  g_hash_table_destroy(priv->users);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_acl_table_class_init(gpointer g_class,
                         gpointer class_data)
{
  GObjectClass* object_class;
  InfAclTableClass* acl_table_class;

  object_class = G_OBJECT_CLASS(g_class);
  acl_table_class = INF_ACL_TABLE_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfAclTablePrivate));

  object_class->dispose = inf_acl_table_dispose;
  object_class->finalize = inf_acl_table_finalize;

  acl_table_class->user_added = NULL;
  acl_table_class->acl_changed = NULL;

  /**
   * InfAclTable::user-added:
   * @table: The #InfAclTable object emitting the signal.
   * @user: The new #InfAclUser.
   *
   * This signal is emitted whenever a new user is added to the table, using
   * the inf_acl_table_add_user() function. It is also called when the user
   * was already in the table before and is updated.
   */
  acl_table_signals[USER_ADDED] = g_signal_new(
    "user-added",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfAclTableClass, user_added),
    NULL, NULL,
    inf_marshal_VOID__BOXED,
    G_TYPE_NONE,
    1,
    INF_TYPE_ACL_USER | G_SIGNAL_TYPE_STATIC_SCOPE
  );

  /**
   * InfAclTable::acl-changed:
   * @table: The #InfAclTable object emitting the signal.
   * @iter: An iterator pointing to the browser node for which the ACL has
   * changed.
   * @sheet_set: A #InfAclSheetSet containing the changed ACL sheets.
   *
   * This signal is emitted whenever an ACL for the node @iter points to
   * are changed. The @sheet_set parameter contains only the ACL sheets
   * that have changed. In order to get the new full sheet set, call
   * inf_acl_table_get_sheets().
   */
  acl_table_signals[ACL_CHANGED] = g_signal_new(
    "acl-changed",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfAclTableClass, acl_changed),
    NULL, NULL,
    inf_marshal_VOID__BOXED_BOXED,
    G_TYPE_NONE,
    2,
    INF_TYPE_BROWSER_ITER | G_SIGNAL_TYPE_STATIC_SCOPE,
    INF_TYPE_ACL_SHEET_SET | G_SIGNAL_TYPE_STATIC_SCOPE
  );
}

GType
inf_acl_table_get_type(void)
{
  static GType acl_table_type = 0;

  if(!acl_table_type)
  {
    static const GTypeInfo acl_table_type_info = {
      sizeof(InfAclTableClass),  /* class_size */
      NULL,                      /* base_init */
      NULL,                      /* base_finalize */
      inf_acl_table_class_init,  /* class_init */
      NULL,                      /* class_finalize */
      NULL,                      /* class_data */
      sizeof(InfAclTable),       /* instance_size */
      0,                         /* n_preallocs */
      inf_acl_table_init,        /* instance_init */
      NULL                       /* value_table */
    };

    acl_table_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfAclTable",
      &acl_table_type_info,
      0
    );
  }

  return acl_table_type;
}

/**
 * inf_acl_table_new:
 *
 * Creates a new #InfAclTable.
 *
 * Returns: A new #InfAclTable. Free with g_object_unref().
 **/
InfAclTable*
inf_acl_table_new(void)
{
  InfAclTable* table;

  table = INF_ACL_TABLE(
    g_object_new(
      INF_TYPE_ACL_TABLE,
      NULL
    )
  );

  return table;
}

/**
 * inf_acl_table_add_user:
 * @table: A #InfAclTable.
 * @user: The user to add.
 * @active: Whether the user is currently active (connected) or not.
 *
 * This function adds the given user to the user table. It takes ownership of
 * @user, and the function does not guarantee that the user written into the
 * user table is the same pointer as @user.
 *
 * If a user with the same user ID does not exist, then the function simply
 * adds the user into the table. If there exists already a user with the same
 * user ID, the function merges the given user with the one already in the
 * table. If @active is %TRUE, the first and last seen times of the user are
 * updated if they are at their default values.
 *
 * Returns: %TRUE if the user was inserted, or %FALSE if all fields were the
 * same and not updated.
 */
gboolean
inf_acl_table_add_user(InfAclTable* table,
                       InfAclUser* user,
                       gboolean active)
{
  InfAclTablePrivate* priv;
  InfAclUser* user_in_table;
  gboolean was_updated;

  g_return_val_if_fail(INF_IS_ACL_TABLE(table), FALSE);
  g_return_val_if_fail(user != NULL, FALSE);

  priv = INF_ACL_TABLE_PRIVATE(table);

  if(active == TRUE)
  {
    user->last_seen = inf_acl_table_get_real_time();
    if(user->first_seen == 0)
      user->first_seen = user->last_seen;
  }

  user_in_table =
    (InfAclUser*)g_hash_table_lookup(priv->users, user->user_id);
  if(user_in_table == NULL)
  {
    g_hash_table_insert(priv->users, user->user_id, user);
    g_signal_emit(table, acl_table_signals[USER_ADDED], 0, user);
    return TRUE;
  }
  else
  {
    was_updated = FALSE;

    if(user_in_table->user_name == NULL ||
       (user->user_name != NULL &&
        strcmp(user_in_table->user_name, user->user_name) != 0))
    {
      g_free(user_in_table->user_name);
      user_in_table->user_name = user->user_name;
      user->user_name = NULL;
      was_updated = TRUE;
    }

    if(user_in_table->first_seen == 0 ||
       (user->first_seen != 0 &&
        user->first_seen < user_in_table->first_seen))
    {
      user_in_table->first_seen = user->first_seen;
      was_updated = TRUE;
    }

    if(user_in_table->last_seen == 0 ||
       (user->last_seen != 0 && user->last_seen > user_in_table->last_seen))
    {
      user_in_table->last_seen = user->last_seen;
      was_updated = TRUE;
    }

    inf_acl_user_free(user);
    if(was_updated == TRUE)
    {
      g_signal_emit(table, acl_table_signals[USER_ADDED], 0, user_in_table);
    }

    return was_updated;
  }
}

/**
 * inf_acl_table_get_n_users:
 * @table: A #InfAclTable.
 *
 * Returns the number of users in @table.
 *
 * Returns: The number of users in @table.
 */
guint
inf_acl_table_get_n_users(InfAclTable* table)
{
  g_return_val_if_fail(INF_IS_ACL_TABLE(table), 0);
  return g_hash_table_size(INF_ACL_TABLE_PRIVATE(table)->users);
}

/**
 * inf_acl_table_get_user:
 * @table: A #InfAclTable.
 * @user_id: The user ID to look up.
 *
 * Returns the entity with the given ID from the table. If there is no such
 * entity the function returns %NULL.
 *
 * Returns: A #InfAclUser with the given user ID, or %NULL.
 */
const InfAclUser*
inf_acl_table_get_user(InfAclTable* table,
                       const gchar* user_id)
{
  InfAclTablePrivate* priv;

  g_return_val_if_fail(INF_IS_ACL_TABLE(table), NULL);
  g_return_val_if_fail(user_id != NULL, NULL);

  priv = INF_ACL_TABLE_PRIVATE(table);
  return (InfAclUser*)g_hash_table_lookup(priv->users, user_id);
}

/**
 * inf_acl_table_get_user_list:
 * @table: A #InfAclTable.
 * @n_users: Output parameter holding the total number of users.
 *
 * Returns an array with all the users in the ACL table. The length of the
 * array is stored in @n_users. The array is not %NULL-terminated. If there
 * are no users in @table the function returns %NULL.
 *
 * Returns: An array of #InfAclUser objects, or %NULL. Free with g_free()
 * when no longer needed.
 */
const InfAclUser**
inf_acl_table_get_user_list(InfAclTable* table,
                            guint* n_users)
{
  InfAclTablePrivate* priv;
  InfAclTableGetUserListData data;

  g_return_val_if_fail(INF_IS_ACL_TABLE(table), NULL);
  g_return_val_if_fail(n_users != NULL, NULL);

  priv = INF_ACL_TABLE_PRIVATE(table);

  *n_users = g_hash_table_size(priv->users);
  if(*n_users == 0)
    return NULL;

  data.users = g_malloc(sizeof(InfAclUser*) * (*n_users));
  data.index = 0;

  g_hash_table_foreach(
    priv->users,
    inf_acl_table_get_user_list_foreach_func,
    &data
  );

  return data.users;
}

/**
 * inf_acl_table_insert_sheet:
 * @table: A #InfAclTable.
 * @iter: A #InfBrowserIter pointing to the browser node for which to insert
 * a sheet.
 * @sheet: The ACL sheet to insert.
 *
 * Inserts @sheet into the ACL for the node @iter points to. If a sheet for
 * the same user exists already, it will be replaced by the new sheet. If you
 * want to insert more than one sheet at the same time for the same node,
 * consider using inf_acl_table_insert_sheets() instead.
 *
 * In order to remove a sheet for a user from a table, insert a sheet with
 * the permission mask set to 0.
 */
void
inf_acl_table_insert_sheet(InfAclTable* table,
                           const InfBrowserIter* iter,
                           const InfAclSheet* sheet)
{
  g_return_if_fail(INF_IS_ACL_TABLE(table));
  g_return_if_fail(iter != NULL);
  g_return_if_fail(sheet != NULL);

  inf_acl_table_insert_sheets_impl(table, iter, sheet, 1);
}

/**
 * inf_acl_table_insert_sheets:
 * @table: A #InfAclTable.
 * @iter: A #InfBrowserIter pointing to the browser node for which to insert
 * sheets.
 * @sheet_set: A sheet set containing the ACL sheets to insert.
 *
 * Inserts the sheets contained in @sheet_set into the ACL table for the
 * node @iter points to. This function does the same as calling
 * inf_acl_table_insert_sheet() repeatedly, but it is more efficient and
 * avoids intermediate states with only some of the sheets changed. The
 * #InfAclTable::acl-changed signal will only be emitted once after the full
 * operation has completed.
 *
 * In order to remove sheets for certain users from the table, insert sheets
 * with the permission mask set to 0.
 */
void
inf_acl_table_insert_sheets(InfAclTable* table,
                            const InfBrowserIter* iter,
                            const InfAclSheetSet* sheet_set)
{
  g_return_if_fail(INF_IS_ACL_TABLE(table));
  g_return_if_fail(iter != NULL);
  g_return_if_fail(sheet_set != NULL);

  inf_acl_table_insert_sheets_impl(
    table,
    iter,
    sheet_set->sheets,
    sheet_set->n_sheets
  );
}

/**
 * inf_acl_table_get_sheets:
 * @table: A #InfAclTable.
 * @iter: A #InfBrowserIter pointing to the node for which to return the
 * ACL sheets.
 *
 * This function returns all ACL sheets that are in effect for the node
 * @iter points to. If the sheet set would be empty, the function returns
 * %NULL instead.
 *
 * Returns: A #InfAclSheetSet owned by @table, or %NULL.
 */
const InfAclSheetSet*
inf_acl_table_get_sheets(InfAclTable* table,
                         const InfBrowserIter* iter)
{
  InfAclTablePrivate* priv;
  InfAclSheetSet* sheet_set;

  g_return_val_if_fail(INF_IS_ACL_TABLE(table), NULL);
  g_return_val_if_fail(iter != NULL, NULL);

  priv = INF_ACL_TABLE_PRIVATE(table);

  sheet_set = g_hash_table_lookup(
    priv->sheet_sets,
    GUINT_TO_POINTER(iter->node_id)
  );

  return sheet_set;
}

/**
 * inf_acl_table_get_sheet:
 * @table: A #InfAclTable.
 * @iter: A #InfBrowserIter pointing to the node for which to retrieve an
 * ACL sheet.
 * @user: The user whose ACL sheet to retrieve.
 *
 * Returns the #InfAclSheet corresponding to @user which contains the
 * permissions for the node @iter points to. If there is no such sheet, the
 * function returns %NULL, which is equivalent to a sheet with mask 0.
 *
 * This function is equivalent to calling inf_acl_table_get_sheets() and
 * inf_acl_sheet_set_find_const_sheet() in a row, with a %NULL check
 * for the returned sheet set.
 *
 * Returns: A #InfAclSheet owned by @table, or %NULL.
 */
const InfAclSheet*
inf_acl_table_get_sheet(InfAclTable* table,
                        const InfBrowserIter* iter,
                        const InfAclUser* user)
{
  InfAclTablePrivate* priv;
  InfAclSheetSet* sheet_set;

  g_return_val_if_fail(INF_IS_ACL_TABLE(table), NULL);
  g_return_val_if_fail(iter != NULL, NULL);

  priv = INF_ACL_TABLE_PRIVATE(table);

  sheet_set = g_hash_table_lookup(
    priv->sheet_sets,
    GUINT_TO_POINTER(iter->node_id)
  );

  if(sheet_set == NULL)
    return NULL;

  return inf_acl_sheet_set_find_const_sheet(sheet_set, user);
}

/**
 * inf_acl_table_get_clear_sheets:
 * @table: A #InfAclTable.
 * @iter: A #InfBrowserIter pointing to the node for which to create the
 * clear sheets.
 *
 * Creates the clear sheets for the node @iter points to. The clear sheets is
 * a sheet set which, when inserted into the ACL table using
 * inf_acl_table_insert_sheets(), would clear all sheets for this node. This
 * corresponds to all sheets that are present for this node with the
 * permission mask set to 0.
 *
 * The returned sheet set is non-external, i.e. more sheets can be added
 * using inf_acl_sheet_set_add_sheet(). In this way, the sheets for one node
 * can be atomically replaced by new ones.
 *
 * Returns: A #InfAclSheetSet with the permission mask for each sheet set to
 * zero. Free with inf_acl_sheet_set_free() when done.
 */
InfAclSheetSet*
inf_acl_table_get_clear_sheets(InfAclTable* table,
                               const InfBrowserIter* iter)
{
  InfAclTablePrivate* priv;
  InfAclSheetSet* sheet_set;
  guint i;

  g_return_val_if_fail(INF_IS_ACL_TABLE(table), NULL);
  g_return_val_if_fail(iter != NULL, NULL);

  priv = INF_ACL_TABLE_PRIVATE(table);

  sheet_set = g_hash_table_lookup(
    priv->sheet_sets,
    GUINT_TO_POINTER(iter->node_id)
  );

  if(sheet_set != NULL)
    sheet_set = inf_acl_sheet_set_copy(sheet_set);
  else
    sheet_set = inf_acl_sheet_set_new();
  g_assert(sheet_set->own_sheets != NULL || sheet_set->n_sheets == 0);

  for(i = 0; i < sheet_set->n_sheets; ++i)
    sheet_set->own_sheets[i].mask = 0;

  return sheet_set;
}

/**
 * inf_acl_table_clear_sheets:
 * @table: A #InfAclTable.
 * @iter: A #InfBrowserIter pointing to the node for which to clear sheets.
 *
 * Removes all sheets installed for the node @iter points to. This is
 * equivalent to calling inf_acl_table_get_clear_sheets() and
 * inf_acl_table_insert_sheets() in a row, but is more efficient.
 */
void
inf_acl_table_clear_sheets(InfAclTable* table,
                           const InfBrowserIter* iter)
{
  InfAclTablePrivate* priv;
  InfAclSheetSet* clear_sheets;

  g_return_if_fail(INF_IS_ACL_TABLE(table));
  g_return_if_fail(iter != NULL);

  priv = INF_ACL_TABLE_PRIVATE(table);
  clear_sheets = inf_acl_table_get_clear_sheets(table, iter);

  g_hash_table_remove(priv->sheet_sets, GUINT_TO_POINTER(iter->node_id));

  if(clear_sheets->n_sheets > 0)
  {
    g_signal_emit(
      table,
      acl_table_signals[ACL_CHANGED],
      0,
      iter,
      clear_sheets
    );
  }

  inf_acl_sheet_set_free(clear_sheets);
}

/**
 * inf_acl_table_sheet_to_xml:
 * @table: A #InfAclTable.
 * @sheet: The #InfAclSheet to serialize to XML.
 * @xml: The XML node into which to write @sheet.
 *
 * Seralizes @sheet as XML into the node @xml. Attributes are added which
 * correspond to the data from @sheet. The sheet can be deserialized using
 * inf_acl_table_sheet_from_xml().
 */
void
inf_acl_table_sheet_to_xml(InfAclTable* table,
                           const InfAclSheet* sheet,
                           xmlNodePtr xml)
{
  g_return_if_fail(INF_IS_ACL_TABLE(table));
  g_return_if_fail(sheet != NULL);
  g_return_if_fail(xml != NULL);

  inf_xml_util_set_attribute(xml, "id", sheet->user->user_id);
  inf_acl_sheet_perms_to_xml(sheet->mask, sheet->perms, xml);
}

/**
 * inf_acl_table_sheet_from_xml:
 * @table: A #InfAclTable.
 * @sheet: The #InfAclSheet in which to write the deserialized sheet.
 * @xml: The XML node to read from.
 * @error: Location to store error information, if any.
 *
 * Deserializes an ACL sheet serialized with inf_acl_table_sheet_to_xml().
 * If an error occurs (for example, missing XML attributes), @error is set
 * and %FALSE is returned.
 *
 * Returns: %TRUE on success or %FALSE if an error occurs.
 */
gboolean
inf_acl_table_sheet_from_xml(InfAclTable* table,
                             InfAclSheet* sheet,
                             xmlNodePtr xml,
                             GError** error)
{
  InfAclTablePrivate* priv;
  xmlChar* user_id;

  g_return_val_if_fail(INF_IS_ACL_TABLE(table), FALSE);
  g_return_val_if_fail(sheet != NULL, FALSE);
  g_return_val_if_fail(xml != NULL, FALSE);
  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

  priv = INF_ACL_TABLE_PRIVATE(table);

  user_id = inf_xml_util_get_attribute_required(xml, "id", error);
  if(user_id == NULL) return FALSE;

  sheet->user = (InfAclUser*)g_hash_table_lookup(priv->users, user_id);
  if(sheet->user == NULL)
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_INVALID_ATTRIBUTE,
      _("No such ACL user with ID \"%s\""),
      (const char*)user_id
    );

    xmlFree(user_id);
    return FALSE;
  }

  xmlFree(user_id);
  if(!inf_acl_sheet_perms_from_xml(xml, &sheet->mask, &sheet->perms, error))
    return FALSE;

  return TRUE;
}

/**
 * inf_acl_table_sheet_set_to_xml:
 * @table: A #InfAclTable.
 * @sheet_set: The #InfAclSheetSet to serialize.
 * @xml: The XML node to serialize @sheet_set into.
 *
 * Serializes the sheet set given by @sheet_set into an XML node. The sheet
 * set can be deserialized again with inf_acl_table_sheet_set_from_xml().
 */
void
inf_acl_table_sheet_set_to_xml(InfAclTable* table,
                               const InfAclSheetSet* sheet_set,
                               xmlNodePtr xml)
{
  xmlNodePtr acl;
  xmlNodePtr sheet;
  guint i;

  g_return_if_fail(INF_IS_ACL_TABLE(table));
  g_return_if_fail(sheet_set != NULL);
  g_return_if_fail(xml != NULL);

  if(sheet_set->n_sheets > 0)
  {
    acl = xmlNewChild(xml, NULL, (const xmlChar*)"acl", NULL);
    for(i = 0; i < sheet_set->n_sheets; ++i)
    {
      sheet = xmlNewChild(acl, NULL, (const xmlChar*)"sheet", NULL);
      inf_acl_table_sheet_to_xml(table, &sheet_set->sheets[i], sheet);
    }
  }
}

/**
 * inf_acl_table_sheet_set_from_xml:
 * @table: A #InfAclTable.
 * @xml: The XML node from which to read the sheet set.
 * @error: Location to read error information, if any.
 *
 * Reads a sheet set from @xml that has been written with
 * inf_acl_table_sheet_set_to_xml(). If an error occurs the function returns
 * %NULL and @error is set. If there is no ACL stored in @xml, the function
 * returns %NULL without setting @error.
 *
 * Returns: A #InfAclSheetSet, or %NULL. Free with inf_acl_sheet_set_free()
 * when no longer needed.
 */
InfAclSheetSet*
inf_acl_table_sheet_set_from_xml(InfAclTable* table,
                                 xmlNodePtr xml,
                                 GError** error)
{
  xmlNodePtr acl;
  xmlNodePtr sheet;
  GArray* array;
  InfAclSheet read_sheet;
  InfAclSheetSet* sheet_set;

  g_return_val_if_fail(INF_IS_ACL_TABLE(table), NULL);
  g_return_val_if_fail(xml != NULL, NULL);
  g_return_val_if_fail(error == NULL || *error == NULL, NULL);

  for(acl = xml->children; acl != NULL; acl = acl->next)
  {
    if(acl->type != XML_ELEMENT_NODE) continue;
    if(strcmp((const char*)acl->name, "acl") != 0) continue;

    array = g_array_sized_new(FALSE, FALSE, sizeof(InfAclSheet), 16);
    for(sheet = acl->children; sheet != NULL; sheet = sheet->next)
    {
      if(sheet->type != XML_ELEMENT_NODE) continue;
      if(strcmp((const char*)sheet->name, "sheet") != 0) continue;

      if(!inf_acl_table_sheet_from_xml(table, &read_sheet, sheet, error))
      {
        g_array_free(array, TRUE);
        return NULL;
      }

      g_array_append_vals(array, &read_sheet, 1);
    }

    if(array->len == 0)
    {
      g_array_free(array, TRUE);
      return NULL;
    }

    sheet_set = inf_acl_sheet_set_new();
    sheet_set->n_sheets = array->len;
    sheet_set->own_sheets = (InfAclSheet*)g_array_free(array, FALSE);
    sheet_set->sheets = sheet_set->own_sheets;
    return sheet_set;
  }

  return NULL;
}

/* vim:set et sw=2 ts=2: */
