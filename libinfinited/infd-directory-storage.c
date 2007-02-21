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
        INFD_DIRECTORY_STORAGE_NODE_NOTE,
        "INFD_DIRECTORY_STORAGE_NODE_NOTE",
        "note"
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

/** infd_directory_storage_node_new:
 *
 * @type: The type of the storage node.
 * @path: Path to the node.
 * @empty: Only used for subdirectory nodes. This specifies whether a
 * subdirectory is empty or not.
 *
 * Creates a new #InfdDirectoryStorageNode with the given attributes. This
 * is most likely only going to be used by InfdDirectoryStorage
 * implementations.
 *
 * Return Value: A new #InfdDirectoryStorageNode.
 **/
InfdDirectoryStorageNode*
infd_directory_storage_node_new(InfdDirectoryStorageNodeType type,
                                const gchar* path,
                                gboolean empty)
{
  InfdDirectoryStorageNode* node;

  g_return_val_if_fail(path != NULL, NULL);

  node = g_slice_new(InfdDirectoryStorageNode);

  node->type = type;
  node->path = g_strdup(path);
  node->empty = empty;

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
  g_return_val_if_fail(node != NULL, NULL);
  return infd_directory_storage_node_new(node->type, node->path, node->empty);
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
  g_return_val_if_fail(INFD_IS_DIRECTORY_STORAGE(storage), NULL);
  g_return_val_if_fail(path != NULL, NULL);

  return INFD_DIRECTORY_STORAGE_IFACE(storage)->read_subdirectory(
    storage,
    path,
    error
  );
}

/** infd_directory_storage_read_note:
 *
 * @storage: A #InfdDirectoryStorage.
 * @path: A path pointing to a note node.
 * @error: Location to store error information.
 *
 * Reads a note from the storage into an #InfBuffer. The caller owns the
 * initial reference on the buffer.
 *
 * Return Value: A #InfBuffer containing the requested note.
 **/
InfBuffer*
infd_directory_storage_read_note(InfdDirectoryStorage* storage,
                                 const gchar* path,
                                 GError** error)
{
  g_return_val_if_fail(INFD_IS_DIRECTORY_STORAGE(storage), NULL);
  g_return_val_if_fail(path != NULL, NULL);

  return INFD_DIRECTORY_STORAGE_IFACE(storage)->read_note(
    storage,
    path,
    error
  );
}
