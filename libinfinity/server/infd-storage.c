/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008, 2009 Armin Burgmeier <armin@arbur.net>
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

#include <libinfinity/server/infd-storage.h>

GType
infd_storage_node_type_get_type(void)
{
  static GType storage_node_type_type = 0;

  if(!storage_node_type_type)
  {
    static const GEnumValue storage_node_type_values[] = {
      {
        INFD_STORAGE_NODE_SUBDIRECTORY,
        "INFD_STORAGE_NODE_SUBDIRECTORY",
        "subdirectory"
      }, {
        INFD_STORAGE_NODE_NOTE,
        "INFD_STORAGE_NODE_NOTE",
        "note"
      }, {
        0,
        NULL,
        NULL
      }
    };

    storage_node_type_type = g_enum_register_static(
      "InfdStorageNodeType",
      storage_node_type_values
    );
  }

  return storage_node_type_type;
}

GType
infd_storage_node_get_type(void)
{
  static GType storage_node_type = 0;

  if(!storage_node_type)
  {
    storage_node_type = g_boxed_type_register_static(
      "InfdStorageNode",
      (GBoxedCopyFunc)infd_storage_node_copy,
      (GBoxedFreeFunc)infd_storage_node_free
    );
  }

  return storage_node_type;
}

GType
infd_storage_get_type(void)
{
  static GType storage_type = 0;

  if(!storage_type)
  {
    static const GTypeInfo storage_info = {
      sizeof(InfdStorageIface),  /* class_size */
      NULL,                      /* base_init */
      NULL,                      /* base_finalize */
      NULL,                      /* class_init */
      NULL,                      /* class_finalize */
      NULL,                      /* class_data */
      0,                         /* instance_size */
      0,                         /* n_preallocs */
      NULL,                      /* instance_init */
      NULL                       /* value_table */
    };

    storage_type = g_type_register_static(
      G_TYPE_INTERFACE,
      "InfdStorage",
      &storage_info,
      0
    );

    g_type_interface_add_prerequisite(storage_type, G_TYPE_OBJECT);
  }

  return storage_type;
}

/**
 * infd_storage_node_new_subdirectory:
 * @path: Path to the node.
 *
 * Creates a new #InfdStorageNode with type
 * %INFD_STORAGE_NODE_SUBDIRECTORY and the given path. This
 * is most likely only going to be used by #InfdStorage
 * implementations.
 *
 * Return Value: A new #InfdStorageNode.
 **/
InfdStorageNode*
infd_storage_node_new_subdirectory(const gchar* path)
{
  InfdStorageNode* node;

  g_return_val_if_fail(path != NULL, NULL);

  node = g_slice_new(InfdStorageNode);

  node->type = INFD_STORAGE_NODE_SUBDIRECTORY;
  node->name = g_strdup(path);

  return node;
}

/**
 * infd_storage_node_new_note:
 * @path: Path to the node.
 * @identifier: Identifier of the note type, for example 'InfText' for text
 * notes.
 *
 * Creates a new #InfdStorageNode with type
 * %INFD_STORAGE_NODE_NOTE and the given path and identifier. This
 * is most likely only going to be used by #InfdStorage
 * implementations.
 *
 * Return Value: A new #InfdStorageNode.
 **/
InfdStorageNode*
infd_storage_node_new_note(const gchar* path,
                           const gchar* identifier)
{
  InfdStorageNode* node;

  g_return_val_if_fail(path != NULL, NULL);
  g_return_val_if_fail(identifier != NULL, NULL);

  node = g_slice_new(InfdStorageNode);

  node->type = INFD_STORAGE_NODE_NOTE;
  node->name = g_strdup(path);
  node->identifier = g_strdup(identifier);

  return node;
}

/**
 * infd_storage_node_copy:
 * @node: Node from which to make a copy.
 *
 * Creates a copy of a #InfdStorageNode object.
 *
 * Return Value: A copy of @node.
 **/
InfdStorageNode*
infd_storage_node_copy(InfdStorageNode* node)
{
  InfdStorageNode* new_node;

  g_return_val_if_fail(node != NULL, NULL);
  new_node = g_slice_new(InfdStorageNode);

  new_node->type = node->type;
  new_node->name = g_strdup(node->name);

  if(node->type == INFD_STORAGE_NODE_NOTE)
    new_node->identifier = g_strdup(node->identifier);

  return new_node;
}

/**
 * infd_storage_node_free:
 * @node: A #InfdStorageNode.
 *
 * Frees a #InfdStorageNode allocated with
 * infd_storage_node_new_subdirectory(), infd_storage_node_new_node() or
 * infd_storage_node_copy().
 **/
void
infd_storage_node_free(InfdStorageNode* node)
{
  g_return_if_fail(node != NULL);

  g_free(node->name);
  if(node->type == INFD_STORAGE_NODE_NOTE)
    g_free(node->identifier);

  g_slice_free(InfdStorageNode, node);
}

/**
 * infd_storage_node_list_free:
 * @node_list: A list of #InfdStorageNode objects.
 *
 * Frees a singly-linked list of #InfdStorageNode as returned by
 * infd_storage_read_subdirectory().
 **/
void
infd_storage_node_list_free(GSList* node_list)
{
  GSList* iter;
  GSList* next;

  for(iter = node_list; iter != NULL; iter = next)
  {
    next = g_slist_next(iter);

    infd_storage_node_free(iter->data);
    g_slist_free_1(iter);
  }
}

/**
 * infd_directory_read_subdirectory:
 * @storage: A #InfdStorage
 * @path: A path pointing to a subdirectory node.
 * @error: Location to store error information.
 *
 * Reads a subdirectory from the storage. Returns a list of
 * InfdStorageNode objects. Both the list and the objects need to
 * be freed by the caller via infd_directory_free_node_list().
 *
 * Return Value: A #GSList that contains #InfdStorageNode objects,
 * or %NULL if either the subdirectory is empty or an
 * error occured.
 **/
GSList*
infd_storage_read_subdirectory(InfdStorage* storage,
                               const gchar* path,
                               GError** error)
{
  InfdStorageIface* iface;

  g_return_val_if_fail(INFD_IS_STORAGE(storage), NULL);
  g_return_val_if_fail(path != NULL, NULL);

  iface = INFD_STORAGE_GET_IFACE(storage);
  g_return_val_if_fail(iface->read_subdirectory != NULL, NULL);

  return iface->read_subdirectory(storage, path, error);
}

/**
 * infd_storage_create_subdirectory:
 * @storage: A #InfdStorage.
 * @path: A path pointing to non-existing node.
 * @error: Location to store error information.
 *
 * Creates a new subdirectory at the given path that is initially empty.
 *
 * Return Value: %TRUE on success.
 **/
gboolean
infd_storage_create_subdirectory(InfdStorage* storage,
                                 const gchar* path,
                                 GError** error)
{
  InfdStorageIface* iface;

  g_return_val_if_fail(INFD_IS_STORAGE(storage), FALSE);
  g_return_val_if_fail(path != NULL, FALSE);

  iface = INFD_STORAGE_GET_IFACE(storage);
  g_return_val_if_fail(iface->create_subdirectory != NULL, FALSE);

  return iface->create_subdirectory(storage, path, error);
}

/**
 * infd_storage_remove_node:
 * @storage: A #InfdStorage
 * @identifier: The type of the node to remove, or %NULL to remove a
 * subdirectory (TODO: This shouldn't be necessary).
 * @path: A path pointing to an existing node.
 * @error: Location to store error information.
 *
 * Removes the node at path from storage. If it is a subdirectory node, all
 * containing nodes and subdirectory nodes are removed recursively.
 *
 * Return Value: %TRUE on success.
 **/
gboolean
infd_storage_remove_node(InfdStorage* storage,
                         const gchar* identifier,
                         const gchar* path,
                         GError** error)
{
  InfdStorageIface* iface;

  g_return_val_if_fail(INFD_IS_STORAGE(storage), FALSE);
  g_return_val_if_fail(path != NULL, FALSE);

  iface = INFD_STORAGE_GET_IFACE(storage);
  g_return_val_if_fail(iface->remove_node != NULL, FALSE);

  return iface->remove_node(storage, identifier, path, error);
}

/* vim:set et sw=2 ts=2: */
