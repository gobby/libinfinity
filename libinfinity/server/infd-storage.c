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

#include <libinfinity/server/infd-storage.h>
#include <libinfinity/inf-define-enum.h>

static const GEnumValue infd_storage_node_type_values[] = {
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

INF_DEFINE_ENUM_TYPE(InfdStorageNodeType, infd_storage_node_type, infd_storage_node_type_values)
G_DEFINE_BOXED_TYPE(InfdStorageNode, infd_storage_node, infd_storage_node_copy, infd_storage_node_free)
G_DEFINE_BOXED_TYPE(InfdStorageAcl, infd_storage_acl, infd_storage_acl_copy, infd_storage_acl_free)
G_DEFINE_INTERFACE(InfdStorage, infd_storage, G_TYPE_OBJECT)

static void
infd_storage_default_init(InfdStorageInterface* iface)
{
}

/**
 * infd_storage_node_new_subdirectory: (constructor)
 * @path: Path to the node.
 *
 * Creates a new #InfdStorageNode with type
 * %INFD_STORAGE_NODE_SUBDIRECTORY and the given path. This
 * is most likely only going to be used by #InfdStorage
 * implementations.
 *
 * Returns: (transfer full): A new #InfdStorageNode.
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
 * infd_storage_node_new_note: (constructor)
 * @path: Path to the node.
 * @identifier: Identifier of the note type, for example 'InfText' for text
 * notes.
 *
 * Creates a new #InfdStorageNode with type
 * %INFD_STORAGE_NODE_NOTE and the given path and identifier. This
 * is most likely only going to be used by #InfdStorage
 * implementations.
 *
 * Returns: (transfer full): A new #InfdStorageNode.
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
 * Returns: (transfer full): A copy of @node.
 **/
InfdStorageNode*
infd_storage_node_copy(const InfdStorageNode* node)
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
 * infd_storage_node_new_subdirectory(), infd_storage_node_new_note() or
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
 * @node_list: (element-type InfdStorageNode): A list of #InfdStorageNode
 * objects.
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
 * infd_storage_acl_copy:
 * @acl: ACL settings from which to make a copy.
 *
 * Creates a copy of a #InfdStorageAcl object.
 *
 * Returns: A copy of @acl.
 **/
InfdStorageAcl*
infd_storage_acl_copy(const InfdStorageAcl* acl)
{
  InfdStorageAcl* new_acl;

  g_return_val_if_fail(acl != NULL, NULL);
  new_acl = g_slice_new(InfdStorageAcl);

  new_acl->account_id = g_strdup(acl->account_id);
  new_acl->mask = acl->mask;
  new_acl->perms = acl->perms;

  return new_acl;
}

/**
 * infd_storage_acl_free:
 * @acl: A #InfdStorageAcl.
 *
 * Frees a #InfdStorageAcl allocated with
 * infd_storage_acl_copy().
 **/
void
infd_storage_acl_free(InfdStorageAcl* acl)
{
  g_return_if_fail(acl != NULL);

  g_free(acl->account_id);
  g_slice_free(InfdStorageAcl, acl);
}

/**
 * infd_storage_acl_list_free:
 * @acl_list: (element-type InfdStorageAcl): A list of #InfdStorageAcl
 * objects.
 *
 * Frees a singly-linked list of #InfdStorageAcl as returned by
 * infd_storage_read_acls().
 **/
void
infd_storage_acl_list_free(GSList* node_list)
{
  GSList* iter;
  GSList* next;

  for(iter = node_list; iter != NULL; iter = next)
  {
    next = g_slist_next(iter);

    infd_storage_acl_free(iter->data);
    g_slist_free_1(iter);
  }
}

/**
 * infd_storage_read_subdirectory:
 * @storage: A #InfdStorage
 * @path: A path pointing to a subdirectory node.
 * @error: Location to store error information.
 *
 * Reads a subdirectory from the storage. Returns a list of
 * InfdStorageNode objects. Both the list and the objects need to
 * be freed by the caller via infd_storage_node_list_free().
 *
 * Returns: (transfer full) (element-type InfdStorageNode) (allow-none): A
 * #GSList that contains #InfdStorageNode objects, or %NULL if either the
 * subdirectory is empty or an error occured.
 **/
GSList*
infd_storage_read_subdirectory(InfdStorage* storage,
                               const gchar* path,
                               GError** error)
{
  InfdStorageInterface* iface;

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
 * Returns: %TRUE on success.
 **/
gboolean
infd_storage_create_subdirectory(InfdStorage* storage,
                                 const gchar* path,
                                 GError** error)
{
  InfdStorageInterface* iface;

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
 * Returns: %TRUE on success.
 **/
gboolean
infd_storage_remove_node(InfdStorage* storage,
                         const gchar* identifier,
                         const gchar* path,
                         GError** error)
{
  InfdStorageInterface* iface;

  g_return_val_if_fail(INFD_IS_STORAGE(storage), FALSE);
  g_return_val_if_fail(path != NULL, FALSE);

  iface = INFD_STORAGE_GET_IFACE(storage);
  g_return_val_if_fail(iface->remove_node != NULL, FALSE);

  return iface->remove_node(storage, identifier, path, error);
}

/**
 * infd_storage_read_acl:
 * @storage: A #InfdStorage.
 * @path: A path pointing to an existing node.
 * @error: Location to store error information, if any.
 *
 * Reads the ACL for the node at the path @path from the storage. It returns
 * a list of #InfdStorageAcl objects.
 *
 * Returns: (transfer full) (element-type InfdStorageAcl) (allow-none): A
 * possibly empty list of #InfdStorageAcl objects. Free with
 * infd_storage_acl_list_free() when no longer needed.
 */
GSList*
infd_storage_read_acl(InfdStorage* storage,
                      const gchar* path,
                      GError** error)
{
  InfdStorageInterface* iface;

  g_return_val_if_fail(INFD_IS_STORAGE(storage), NULL);
  g_return_val_if_fail(path != NULL, NULL);
  g_return_val_if_fail(error == NULL || *error == NULL, NULL);

  iface = INFD_STORAGE_GET_IFACE(storage);
  g_return_val_if_fail(iface->read_acl != NULL, NULL);

  return iface->read_acl(storage, path, error);
}

/**
 * infd_storage_write_acl:
 * @storage: A #InfdStorage.
 * @path: A path to an existing node.
 * @sheet_set: Sheets to set for the node at @path, or %NULL.
 * @error: Location to store error information, if any.
 *
 * Writes the ACL defined by @sheet_set into storage. If @sheet_set is %NULL
 * this is equivalent to an empty set. Returns %TRUE on success or %FALSE on
 * error. If the function fails, @error is set.
 *
 * Returns: %TRUE on success or %FALSE on error.
 */
gboolean
infd_storage_write_acl(InfdStorage* storage,
                       const gchar* path,
                       const InfAclSheetSet* sheet_set,
                       GError** error)
{
  InfdStorageInterface* iface;

  g_return_val_if_fail(INFD_IS_STORAGE(storage), FALSE);
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

  iface = INFD_STORAGE_GET_IFACE(storage);
  g_return_val_if_fail(iface->write_acl != NULL, FALSE);

  return iface->write_acl(storage, path, sheet_set, error);
}

/* vim:set et sw=2 ts=2: */
