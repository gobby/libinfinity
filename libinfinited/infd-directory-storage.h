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

#ifndef __INFD_DIRECTORY_STORAGE_H__
#define __INFD_DIRECTORY_STORAGE_H__

#include <libinfinity/inf-text-buffer.h>
#include <libinfinity/inf-ink-buffer.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INFD_TYPE_DIRECTORY_STORAGE                 (infd_directory_storage_get_type())
#define INFD_DIRECTORY_STORAGE(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INFD_TYPE_DIRECTORY_STORAGE, InfdDirectoryStorage))
#define INFD_IS_DIRECTORY_STORAGE(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INFD_TYPE_DIRECTORY_STORAGE))
#define INFD_DIRECTORY_STORAGE_GET_IFACE(inst)      (G_TYPE_INSTANCE_GET_INTERFACE((inst), INFD_TYPE_DIRECTORY_STORAGE, InfdDirectoryStorageIface))

#define INFD_TYPE_DIRECTORY_STORAGE_NODE_TYPE       (infd_directory_storage_node_type_get_type())
#define INFD_TYPE_DIRECTORY_STORAGE_NODE            (infd_directory_storage_node_get_type())

typedef struct _InfdDirectoryStorage InfdDirectoryStorage;
typedef struct _InfdDirectoryStorageIface InfdDirectoryStorageIface;

typedef enum _InfdDirectoryStorageNodeType {
  INFD_DIRECTORY_STORAGE_NODE_SUBDIRECTORY,
  INFD_DIRECTORY_STORAGE_NODE_TEXT,
  INFD_DIRECTORY_STORAGE_NODE_INK
} InfdDirectoryStorageNodeType;

typedef struct _InfdDirectoryStorageNode InfdDirectoryStorageNode;
struct _InfdDirectoryStorageNode {
  InfdDirectoryStorageNodeType type;
  gchar* path;
};

struct _InfdDirectoryStorageIface {
  GTypeInterface parent;

  /* All these calls are supposed to be synchronous, e.g. completly perform
   * the required task. Some day, we could implement asynchronous
   * behaviour in InfdDirectory (e.g. it caches operations and executes
   * them via the storage in the background). */

  /* Virtual Table */
  GSList* (*read_subdirectory)(InfdDirectoryStorage* storage,
                               const gchar* path,
                               GError** error);

  gboolean (*read_text)(InfdDirectoryStorage* storage,
                        const gchar* path,
                        InfTextBuffer* buffer,
                        GError** error);

  gboolean (*read_ink)(InfdDirectoryStorage* storage,
                       const gchar* path,
                       InfInkBuffer* buffer,
                       GError** error);

  gboolean (*create_subdirectory)(InfdDirectoryStorage* storage,
                                  const gchar* path,
                                  GError** error);

  gboolean (*write_text)(InfdDirectoryStorage* storage,
                         const gchar* path,
                         InfTextBuffer* buffer,
                         GError** error);

  gboolean (*write_ink)(InfdDirectoryStorage* storage,
                        const gchar* path,
                        InfInkBuffer* buffer,
                        GError** error);

  gboolean (*remove_node)(InfdDirectoryStorage* storage,
                          const gchar* path,
                          GError** error);

  /* TODO: Add further methods to copy, move and expunge nodes */
};

GType
infd_directory_storage_node_type_get_type(void) G_GNUC_CONST;

GType
infd_directory_storage_node_get_type(void) G_GNUC_CONST;

GType
infd_directory_storage_get_type(void) G_GNUC_CONST;

InfdDirectoryStorageNode*
infd_directory_storage_node_new_subdirectory(const gchar* path);

InfdDirectoryStorageNode*
infd_directory_storage_node_new_ink(const gchar* path);

InfdDirectoryStorageNode*
infd_directory_storage_node_new_text(const gchar* path);

InfdDirectoryStorageNode*
infd_directory_storage_node_copy(InfdDirectoryStorageNode* node);

void
infd_directory_storage_node_free(InfdDirectoryStorageNode* node);

void
infd_directory_storage_free_node_list(GSList* node_list);

GSList*
infd_directory_storage_read_subdirectory(InfdDirectoryStorage* storage,
                                         const gchar* path,
                                         GError** error);

gboolean
infd_directory_storage_read_text(InfdDirectoryStorage* storage,
                                 const gchar* path,
                                 InfTextBuffer* buffer,
                                 GError** error);

gboolean
infd_directory_storage_read_ink(InfdDirectoryStorage* storage,
                                const gchar* path,
                                InfInkBuffer* buffer,
                                GError** error);

gboolean
infd_directory_storage_create_subdirectory(InfdDirectoryStorage* storage,
                                           const gchar* path,
                                           GError** error);

gboolean
infd_directory_storage_write_text(InfdDirectoryStorage* storage,
                                  const gchar* path,
                                  InfTextBuffer* buffer,
                                  GError** error);

gboolean
infd_directory_storage_write_ink(InfdDirectoryStorage* storage,
                                 const gchar* path,
                                 InfInkBuffer* buffer,
                                 GError** error);

gboolean
infd_directory_storage_remove_node(InfdDirectoryStorage* storage,
                                   const gchar* path,
                                   GError** error);

G_END_DECLS

#endif /* __INFD_DIRECTORY_STORAGE_H__ */
