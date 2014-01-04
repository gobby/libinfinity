/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2014 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INFD_STORAGE_H__
#define __INFD_STORAGE_H__

#include <glib-object.h>

#include <libinfinity/server/infd-acl-account-info.h>
#include <libinfinity/common/inf-acl.h>

G_BEGIN_DECLS

#define INFD_TYPE_STORAGE                 (infd_storage_get_type())
#define INFD_STORAGE(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INFD_TYPE_STORAGE, InfdStorage))
#define INFD_IS_STORAGE(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INFD_TYPE_STORAGE))
#define INFD_STORAGE_GET_IFACE(inst)      (G_TYPE_INSTANCE_GET_INTERFACE((inst), INFD_TYPE_STORAGE, InfdStorageIface))

#define INFD_TYPE_STORAGE_NODE_TYPE       (infd_storage_node_type_get_type())
#define INFD_TYPE_STORAGE_NODE            (infd_storage_node_get_type())
#define INFD_TYPE_STORAGE_ACL             (infd_storage_acl_get_type())

typedef struct _InfdStorage InfdStorage;
typedef struct _InfdStorageIface InfdStorageIface;

typedef enum _InfdStorageNodeType {
  INFD_STORAGE_NODE_SUBDIRECTORY,
  INFD_STORAGE_NODE_NOTE
} InfdStorageNodeType;

typedef struct _InfdStorageNode InfdStorageNode;
struct _InfdStorageNode {
  InfdStorageNodeType type;
  gchar* name;

  gchar* identifier; /* Only set when type == INFD_STORAGE_NODE_NOTE */
};

typedef struct _InfdStorageAcl InfdStorageAcl;
struct _InfdStorageAcl {
  gchar* account_id;
  InfAclMask mask;
  InfAclMask perms;  
};

struct _InfdStorageIface {
  GTypeInterface parent;

  /* All these calls are supposed to be synchronous, e.g. completly perform
   * the required task. Some day, we could implement asynchronous
   * behaviour in InfdDirectory (e.g. it caches operations and executes
   * them via the storage in the background). */

  /* Virtual Table */
  GSList* (*read_subdirectory)(InfdStorage* storage,
                               const gchar* path,
                               GError** error);

  gboolean (*create_subdirectory)(InfdStorage* storage,
                                  const gchar* path,
                                  GError** error);

  /* TODO: Remove the identifier here */
  gboolean (*remove_node)(InfdStorage* storage,
                          const gchar* identifier,
                          const gchar* path,
                          GError** error);

  /* TODO: Add further methods to copy, move and expunge nodes */

  /* TODO: There should be a method to only add/change one account. */

  InfdAclAccountInfo** (*read_account_list)(InfdStorage* storage,
                                            guint* n_accounts,
                                            GError** error);

  gboolean (*write_account_list)(InfdStorage* storage,
                                 const InfdAclAccountInfo** accounts,
                                 guint n_accounts,
                                 GError** error);

  GSList* (*read_acl)(InfdStorage* storage,
                      const gchar* path,
                      GError** error);

  gboolean (*write_acl)(InfdStorage* storage,
                        const gchar* path,
                        const InfAclSheetSet* sheet_set,
                        GError** error);
};

GType
infd_storage_node_type_get_type(void) G_GNUC_CONST;

GType
infd_storage_node_get_type(void) G_GNUC_CONST;

GType
infd_storage_acl_get_type(void) G_GNUC_CONST;

GType
infd_storage_get_type(void) G_GNUC_CONST;

InfdStorageNode*
infd_storage_node_new_subdirectory(const gchar* path);

InfdStorageNode*
infd_storage_node_new_note(const gchar* path,
                           const gchar* identifier);

InfdStorageNode*
infd_storage_node_copy(InfdStorageNode* node);

void
infd_storage_node_free(InfdStorageNode* node);

void
infd_storage_node_list_free(GSList* node_list);

InfdStorageAcl*
infd_storage_acl_copy(InfdStorageAcl* acl);

void
infd_storage_acl_free(InfdStorageAcl* acl);

void
infd_storage_acl_list_free(GSList* acl_list);

GSList*
infd_storage_read_subdirectory(InfdStorage* storage,
                               const gchar* path,
                               GError** error);

gboolean
infd_storage_create_subdirectory(InfdStorage* storage,
                                 const gchar* path,
                                 GError** error);

gboolean
infd_storage_remove_node(InfdStorage* storage,
                         const gchar* identifier,
                         const gchar* path,
                         GError** error);

InfdAclAccountInfo**
infd_storage_read_account_list(InfdStorage* storage,
                               guint* n_accounts,
                               GError** error);

gboolean
infd_storage_write_account_list(InfdStorage* storage,
                                const InfdAclAccountInfo** accounts,
                                guint n_accounts,
                                GError** error);

GSList*
infd_storage_read_acl(InfdStorage* storage,
                      const gchar* path,
                      GError** error);

gboolean
infd_storage_write_acl(InfdStorage* storage,
                       const gchar* path,
                       const InfAclSheetSet* sheet_set,
                       GError** error);

G_END_DECLS

#endif /* __INFD_STORAGE_H__ */

/* vim:set et sw=2 ts=2: */
