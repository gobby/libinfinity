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

#include <libinfinited/infd-directory-storage.h>

GType
infd_directory_storage_node_type_get_type(void)
{
  static GType directory_storage_node_type_type = 0;

  if(!directory_storage_node_type_type)
  {
    static const GEnumValue directory_storage_node_type_values[] = {
      {
        INFD_DIRECTORY_STORAGE_NODE_SUBDIRECTORY,
        "INFD_DIRECTORY_STORAGE_NODE_SUBDIRECTORY",
        "subdirectory"
      }, {
        INFD_DIRECTORY_STORAGE_NODE_TEXT,
        "INFD_DIRECTORY_STORAGE_NODE_TEXT",
        "text"
      }, {
        INFD_DIRECTORY_STORAGE_NODE_INK,
        "INFD_DIRECTORY_STORAGE_NODE_INK",
        "ink"
      }, {
        0,
        NULL,
        NULL
      }
    };

    directory_storage_node_type_type = g_enum_register_static(
      "InfdDirectoryStorageNodeType",
      directory_storage_node_type_values
    );
  }

  return directory_storage_node_type_type;
}

GType
infd_directory_storage_node_get_type(void)
{
  static GType directory_storage_node_type = 0;

  if(!directory_storage_node_type)
  {
    directory_storage_node_type = g_boxed_type_register_static(
      "InfdDirectoryStorageNode",
      (GBoxedCopyFunc)infd_directory_storage_node_copy,
      (GBoxedFreeFunc)infd_directory_storage_node_free
    );
  }

  return directory_storage_node_type;
}

GType
infd_directory_storage_get_type(void)
{
  static GType directory_storage_type = 0;

  if(!directory_storage_type)
  {
    static const GTypeInfo directory_storage_info = {
      sizeof(InfdDirectoryStorageIface),  /* class_size */
      NULL,                               /* base_init */
      NULL,                               /* base_finalize */
      NULL,                               /* class_init */
      NULL,                               /* class_finalize */
      NULL,                               /* class_data */
      0,                                  /* instance_size */
      0,                                  /* n_preallocs */
      NULL,                               /* instance_init */
      NULL                                /* value_table */
    };

    directory_storage_type = g_type_register_static(
      G_TYPE_INTERFACE,
      "InfdDirectoryStorage",
      &directory_storage_info,
      0
    );

    g_type_interface_add_prerequisite(directory_storage_type, G_TYPE_OBJECT);
  }

  return directory_storage_type;
}

/** infd_directory_storage_node_new_subdirectory:
 *
 * @path: Path to the node.
 *
 * Creates a new #InfdDirectoryStorageNode with type
 * %INFD_DIRECTORY_STORAGE_NODE_SUBDIRECTORY and the given path. This
 * is most likely only going to be used by #InfdDirectoryStorage
 * implementations.
 *
 * Return Value: A new #InfdDirectoryStorageNode.
 **/
InfdDirectoryStorageNode*
infd_directory_storage_node_new_subdirectory(const gchar* path)
{
  InfdDirectoryStorageNode* node;

  g_return_val_if_fail(path != NULL, NULL);

  node = g_slice_new(InfdDirectoryStorageNode);

  node->type = INFD_DIRECTORY_STORAGE_NODE_SUBDIRECTORY;
  node->path = g_strdup(path);

  return node;
}

/** infd_directory_storage_node_new_text:
 *
 * @path: Path to the node.
 *
 * Creates a new #InfdDirectoryStorageNode with type
 * %INFD_DIRECTORY_STORAGE_NODE_TEXT and the given path. This
 * is most likely only going to be used by #InfdDirectoryStorage
 * implementations.
 *
 * Return Value: A new #InfdDirectoryStorageNode.
 **/
InfdDirectoryStorageNode*
infd_directory_storage_node_new_text(const gchar* path)
{
  InfdDirectoryStorageNode* node;

  g_return_val_if_fail(path != NULL, NULL);

  node = g_slice_new(InfdDirectoryStorageNode);

  node->type = INFD_DIRECTORY_STORAGE_NODE_TEXT;
  node->path = g_strdup(path);

  return node;
}

/** infd_directory_storage_node_new_subdirectory:
 *
 * @path: Path to the node.
 *
 * Creates a new #InfdDirectoryStorageNode with type
 * %INFD_DIRECTORY_STORAGE_NODE_INK and the given path. This
 * is most likely only going to be used by #InfdDirectoryStorage
 * implementations.
 *
 * Return Value: A new #InfdDirectoryStorageNode.
 **/
InfdDirectoryStorageNode*
infd_directory_storage_node_new_ink(const gchar* path)
{
  InfdDirectoryStorageNode* node;

  g_return_val_if_fail(path != NULL, NULL);

  node = g_slice_new(InfdDirectoryStorageNode);

  node->type = INFD_DIRECTORY_STORAGE_NODE_INK;
  node->path = g_strdup(path);

  return node;
}

/** infd_directory_storage_node_copy:
 *
 * @node: Node from which to make a copy.
 *
 * Creates a copy of a #InfdDirectoryStorageNode object.
 *
 * Return Value: A copy of @node.
 **/
InfdDirectoryStorageNode*
infd_directory_storage_node_copy(InfdDirectoryStorageNode* node)
{
  InfdDirectoryStorageNode* new_node;

  g_return_val_if_fail(node != NULL, NULL);
  new_node = g_slice_new(InfdDirectoryStorageNode);

  new_node->type = node->type;
  new_node->path = g_strdup(node->path);

  return new_node;
}

/** infd_directory_storage_node_free:
 *
 * @node: A #InfdDirectoryStorageNode.
 *
 * Frees a #InfdDirectoryStorageNode allocated with
 * infd_directory_storage_node_new() or infd_directory_storage_node_copy().
 **/
void
infd_directory_storage_node_free(InfdDirectoryStorageNode* node)
{
  g_return_if_fail(node != NULL);

  g_free(node->path);
  g_slice_free(InfdDirectoryStorageNode, node);
}

/** infd_directory_storage_free_node_list:
 *
 * @node_list A list of #InfdDirectoryStorageNode objects.
 *
 * Frees a singly-linked list of #InfdDirectoryStorageNode as returned by
 * infd_directory_storage_read_subdirectory().
 **/
void
infd_directory_storage_free_node_list(GSList* node_list)
{
  GSList* iter;
  GSList* next;

  for(iter = node_list; iter != NULL; iter = next)
  {
    next = g_slist_next(iter);

    infd_directory_storage_node_free(iter->data);
    g_slist_free_1(iter);
  }
}

/** infd_directory_read_subdirectory:
 *
 * @storage: A #InfdDirectoryStorage
 * @path: A path pointing to a subdirectory node.
 * @error: Location to store error information.
 *
 * Reads a subdirectory from the storage. Returns a list of
 * InfdDirectoryStorageNode objects. Both the list and the objects need to
 * be freed by the caller via infd_directory_free_node_list().
 *
 * Return Value: A #GSList that contains #InfdDirectoryStorageNode objects,
 * or %NULL if either the subdirectory does not contain any node or an
 * error occured.
 **/
GSList*
infd_directory_read_subdirectory(InfdDirectoryStorage* storage,
                                 const gchar* path,
                                 GError** error)
{
  InfdDirectoryStorageIface* iface;

  g_return_val_if_fail(INFD_IS_DIRECTORY_STORAGE(storage), NULL);
  g_return_val_if_fail(path != NULL, NULL);

  iface = INFD_DIRECTORY_STORAGE_GET_IFACE(storage);
  g_return_val_if_fail(iface->read_subdirectory != NULL, NULL);

  return iface->read_subdirectory(storage, path, error);
}

/** infd_directory_storage_read_text:
 *
 * @storage: A #InfdDirectoryStorage.
 * @path: A path pointing to a text node.
 * @buffer: A #InfTextBuffer.
 * @error: Location to store error information.
 *
 * Reads a text note from the storage into @buffer.
 *
 * Return Value: %TRUE on success.
 **/
gboolean
infd_directory_storage_read_text(InfdDirectoryStorage* storage,
                                 const gchar* path,
                                 InfTextBuffer* buffer,
                                 GError** error)
{
  InfdDirectoryStorageIface* iface;

  g_return_val_if_fail(INFD_IS_DIRECTORY_STORAGE(storage), FALSE);
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(INF_IS_TEXT_BUFFER(buffer), FALSE);

  iface = INFD_DIRECTORY_STORAGE_GET_IFACE(storage);
  g_return_val_if_fail(iface->read_text != NULL, FALSE);

  return iface->read_text(storage, path, buffer, error);
}

/** infd_directory_storage_read_ink:
 *
 * @storage: A #InfdDirectoryStorage.
 * @path: A path pointing to an ink node.
 * @buffer: A #InfInkBuffer.
 * @error: Location to store error information.
 *
 * Reads an ink note from the storage into @buffer.
 *
 * Return Value: %TRUE on success.
 **/
gboolean
infd_directory_storage_read_ink(InfdDirectoryStorage* storage,
                                const gchar* path,
                                InfInkBuffer* buffer,
                                GError** error)
{
  InfdDirectoryStorageIface* iface;

  g_return_val_if_fail(INFD_IS_DIRECTORY_STORAGE(storage), FALSE);
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(INF_IS_INK_BUFFER(buffer), FALSE);

  iface = INFD_DIRECTORY_STORAGE_GET_IFACE(storage);
  g_return_val_if_fail(iface->read_ink != NULL, FALSE);

  return iface->read_ink(storage, path, buffer, error);
}

/** infd_directory_storage_create_subdirectory:
 *
 * @storage: A #InfdDirectoryStorage.
 * @path: A path pointing to non-existing node.
 * @error: Location to store error information.
 *
 * Creates a new subdirectory at the given path that is initially empty.
 *
 * Return Value: %TRUE on success.
 **/
gboolean
infd_directory_storage_create_subdirectory(InfdDirectoryStorage* storage,
                                           const gchar* path,
                                           GError** error)
{
  InfdDirectoryStorageIface* iface;

  g_return_val_if_fail(INFD_IS_DIRECTORY_STORAGE(storage), FALSE);
  g_return_val_if_fail(path != NULL, FALSE);

  iface = INFD_DIRECTORY_STORAGE_GET_IFACE(storage);
  g_return_val_if_fail(iface->create_subdirectory != NULL, FALSE);

  return iface->create_subdirectory(storage, path, error);
}

/** infd_directory_storage_write_text:
 *
 * @storage: A #InfdDirectoryStorage.
 * @path: A path pointing to a non-existing or a non-subdirectory node.
 * @buffer: A #InfTextBuffer.
 * @error: Location to store error information.
 *
 * Stores the content of @buffer into the storage. If there is already a
 * note at @path, it is overwritten.
 *
 * Return Value: %TRUE on success.
 **/
gboolean
infd_directory_storage_write_text(InfdDirectoryStorage* storage,
                                  const gchar* path,
                                  InfTextBuffer* buffer,
                                  GError** error)
{
  InfdDirectoryStorageIface* iface;

  g_return_val_if_fail(INFD_IS_DIRECTORY_STORAGE(storage), FALSE);
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(INF_IS_TEXT_BUFFER(buffer), FALSE);

  iface = INFD_DIRECTORY_STORAGE_GET_IFACE(storage);
  g_return_val_if_fail(iface->write_text != NULL, FALSE);

  return iface->write_text(storage, path, buffer, error);
}

/** infd_directory_storage_write_ink:
 *
 * @storage: A #InfdDirectoryStorage.
 * @path: A path pointing to a non-existing or a non-subdirectory node.
 * @buffer: A #InfInkBuffer.
 * @error: Location to store error information.
 *
 * Stores the content of @buffer into storage. If there is already a note
 * at @path, it is overwritetn.
 *
 * Return Value: %TRUE on success.
 **/
gboolean
infd_directory_storage_write_ink(InfdDirectoryStorage* storage,
                                 const gchar* path,
                                 InfInkBuffer* buffer,
                                 GError** error)
{
  InfdDirectoryStorageIface* iface;

  g_return_val_if_fail(INFD_IS_DIRECTORY_STORAGE(storage), FALSE);
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(INF_IS_INK_BUFFER(buffer), FALSE);

  iface = INFD_DIRECTORY_STORAGE_GET_IFACE(storage);
  g_return_val_if_fail(iface->write_ink != NULL, FALSE);

  return iface->write_ink(storage, path, buffer, error);
}

/** infd_directory_storage_remove_node:
 *
 * @storage: A #InfdDirectoryStorage
 * @path: A path pointing to an existing node.
 * @error: Location to store error information.
 *
 * Removes the node at path from storage. If it is a subdirectory node, all
 * containing nodes and subdirectory nodes are removed recursively.
 *
 * Return Value: %TRUE on success.
 **/
gboolean
infd_directory_storage_remove_node(InfdDirectoryStorage* storage,
                                   const gchar* path,
                                   GError** error)
{
  InfdDirectoryStorageIface* iface;

  g_return_val_if_fail(INFD_IS_DIRECTORY_STORAGE(storage), FALSE);
  g_return_val_if_fail(path != NULL, FALSE);

  iface = INFD_DIRECTORY_STORAGE_GET_IFACE(storage);
  g_return_val_if_fail(iface->remove_node != NULL, FALSE);

  return iface->remove_node(storage, path, error);
}
