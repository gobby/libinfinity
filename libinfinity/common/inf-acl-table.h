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

#ifndef __INF_ACL_TABLE_H__
#define __INF_ACL_TABLE_H__

#include <glib-object.h>

#include <libinfinity/common/inf-acl-sheet.h>
#include <libinfinity/common/inf-browser-iter.h>

G_BEGIN_DECLS

#define INF_TYPE_ACL_TABLE                 (inf_acl_table_get_type())
#define INF_ACL_TABLE(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_ACL_TABLE, InfAclTable))
#define INF_ACL_TABLE_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TYPE_ACL_TABLE, InfAclTableClass))
#define INF_IS_ACL_TABLE(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_ACL_TABLE))
#define INF_IS_ACL_TABLE_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TYPE_ACL_TABLE))
#define INF_ACL_TABLE_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TYPE_ACL_TABLE, InfAclTableClass))

typedef struct _InfAclTable InfAclTable;
typedef struct _InfAclTableClass InfAclTableClass;

/**
 * InfAclTableClass:
 * @user_added: Default signal handler of the #InfAclTable::user-added signal.
 * @acl_changed: Default signal handler of the #InfAclTable::acl-changed
 * signal.
 *
 * Default signal handlers of the #InfAclTable class.
 */
struct _InfAclTableClass {
  /*< private >*/
  GObjectClass parent_class;

  /*< public >*/
  void(*user_added)(InfAclTable* table,
                    const InfAclUser* user);

  void(*acl_changed)(InfAclTable* table,
                     const InfBrowserIter* iter,
                     const InfAclSheetSet* sheet_set);
};

/**
 * InfAclTable:
 *
 * #InfAclTable is an opaque data type. You should only access it via the
 * public API functions.
 */
struct _InfAclTable {
  /*< private >*/
  GObject parent;
};

GType
inf_acl_table_get_type(void) G_GNUC_CONST;

InfAclTable*
inf_acl_table_new(void);

gboolean
inf_acl_table_add_user(InfAclTable* table,
                       InfAclUser* user,
                       gboolean active);

guint
inf_acl_table_get_n_users(InfAclTable* table);

const InfAclUser*
inf_acl_table_get_user(InfAclTable* table,
                       const gchar* user_id);

const InfAclUser**
inf_acl_table_get_user_list(InfAclTable* table,
                            guint* n_users);

void
inf_acl_table_insert_sheet(InfAclTable* table,
                           const InfBrowserIter* iter,
                           const InfAclSheet* sheet);

void
inf_acl_table_insert_sheets(InfAclTable* table,
                            const InfBrowserIter* iter,
                            const InfAclSheetSet* sheet_set);

const InfAclSheetSet*
inf_acl_table_get_sheets(InfAclTable* table,
                         const InfBrowserIter* iter);

const InfAclSheet*
inf_acl_table_get_sheet(InfAclTable* table,
                        const InfBrowserIter* iter,
                        const InfAclUser* user);

InfAclSheetSet*
inf_acl_table_get_clear_sheets(InfAclTable* table,
                               const InfBrowserIter* iter);

void
inf_acl_table_clear_sheets(InfAclTable* table,
                           const InfBrowserIter* iter);

void
inf_acl_table_sheet_to_xml(InfAclTable* table,
                           const InfAclSheet* sheet,
                           xmlNodePtr xml);

gboolean
inf_acl_table_sheet_from_xml(InfAclTable* table,
                             InfAclSheet* sheet,
                             xmlNodePtr xml,
                             GError** error);

void
inf_acl_table_sheet_set_to_xml(InfAclTable* table,
                               const InfAclSheetSet* sheet_set,
                               xmlNodePtr xml);

InfAclSheetSet*
inf_acl_table_sheet_set_from_xml(InfAclTable* table,
                                 xmlNodePtr xml,
                                 GError** error);

G_END_DECLS

#endif /* __INF_ACL_TABLE_H__ */

/* vim:set et sw=2 ts=2: */
