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

#ifndef __INFD_DIRECTORY_H__
#define __INFD_DIRECTORY_H__

#include <libinfinity/server/infd-storage.h>
#include <libinfinity/server/infd-note-plugin.h>
#include <libinfinity/server/infd-session-proxy.h>
#include <libinfinity/communication/inf-communication-manager.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INFD_TYPE_DIRECTORY                 (infd_directory_get_type())
#define INFD_DIRECTORY(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INFD_TYPE_DIRECTORY, InfdDirectory))
#define INFD_DIRECTORY_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INFD_TYPE_DIRECTORY, InfdDirectoryClass))
#define INFD_IS_DIRECTORY(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INFD_TYPE_DIRECTORY))
#define INFD_IS_DIRECTORY_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INFD_TYPE_DIRECTORY))
#define INFD_DIRECTORY_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INFD_TYPE_DIRECTORY, InfdDirectoryClass))

#define INFD_TYPE_DIRECTORY_ITER            (infd_directory_iter_get_type())

typedef struct _InfdDirectory InfdDirectory;
typedef struct _InfdDirectoryClass InfdDirectoryClass;

typedef struct _InfdDirectoryIter InfdDirectoryIter;
struct _InfdDirectoryIter {
  guint node_id;
  gpointer node;
};

struct _InfdDirectoryClass {
  GObjectClass parent_class;

  /* Signals */
  void (*node_added)(InfdDirectory* directory,
                     InfdDirectoryIter* iter);

  void (*node_removed)(InfdDirectory* directory,
                       InfdDirectoryIter* iter);

  void (*add_session)(InfdDirectory* directory,
                      InfdDirectoryIter* iter,
                      InfdSessionProxy* session);

  void (*remove_session)(InfdDirectory* directory,
                         InfdDirectoryIter* iter,
                         InfdSessionProxy* session);
};

struct _InfdDirectory {
  GObject parent;
};

GType
infd_directory_iter_get_type(void) G_GNUC_CONST;

GType
infd_directory_get_type(void) G_GNUC_CONST;

/* TODO: Do these two need to be public? */
InfdDirectoryIter*
infd_directory_iter_copy(InfdDirectoryIter* iter);

void
infd_directory_iter_free(InfdDirectoryIter* iter);

InfdDirectory*
infd_directory_new(InfIo* io,
                   InfdStorage* storage,
                   InfCommunicationManager* comm_manager);

InfIo*
infd_directory_get_io(InfdDirectory* directory);

InfdStorage*
infd_directory_get_storage(InfdDirectory* directory);

InfCommunicationManager*
infd_directory_get_communication_manager(InfdDirectory* directory);

gboolean
infd_directory_add_plugin(InfdDirectory* directory,
                          const InfdNotePlugin* plugin);

const InfdNotePlugin*
infd_directory_lookup_plugin(InfdDirectory* directory,
                             const gchar* note_type);

gboolean
infd_directory_add_connection(InfdDirectory* directory,
                              InfXmlConnection* connection);

const gchar*
infd_directory_iter_get_name(InfdDirectory* directory,
                             InfdDirectoryIter* iter);

gchar*
infd_directory_iter_get_path(InfdDirectory* directory,
                             InfdDirectoryIter* iter);

void
infd_directory_iter_get_root(InfdDirectory* directory,
                             InfdDirectoryIter* iter);

gboolean
infd_directory_iter_get_next(InfdDirectory* directory,
                             InfdDirectoryIter* iter);

gboolean
infd_directory_iter_get_prev(InfdDirectory* directory,
                             InfdDirectoryIter* iter);

gboolean
infd_directory_iter_get_parent(InfdDirectory* directory,
                               InfdDirectoryIter* iter);

gboolean
infd_directory_iter_get_child(InfdDirectory* directory,
                              InfdDirectoryIter* iter,
                              GError** error);

gboolean
infd_directory_iter_get_explored(InfdDirectory* directory,
                                 InfdDirectoryIter* iter);

gboolean
infd_directory_add_subdirectory(InfdDirectory* directory,
                                InfdDirectoryIter* parent,
                                const gchar* name,
                                InfdDirectoryIter* iter,
                                GError** error);

gboolean
infd_directory_add_note(InfdDirectory* directory,
                        InfdDirectoryIter* parent,
                        const gchar* name,
                        const InfdNotePlugin* plugin,
                        InfdDirectoryIter* iter,
                        GError** error);

gboolean
infd_directory_remove_node(InfdDirectory* directory,
                           InfdDirectoryIter* iter,
                           GError** error);

InfdStorageNodeType
infd_directory_iter_get_node_type(InfdDirectory* directory,
                                  InfdDirectoryIter* iter);

const InfdNotePlugin*
infd_directory_iter_get_plugin(InfdDirectory* directory,
                               InfdDirectoryIter* iter);

InfdSessionProxy*
infd_directory_iter_get_session(InfdDirectory* directory,
                                InfdDirectoryIter* iter,
                                GError** error);

InfdSessionProxy*
infd_directory_iter_peek_session(InfdDirectory* directory,
                                 InfdDirectoryIter* iter);

gboolean
infd_directory_iter_save_session(InfdDirectory* directory,
                                 InfdDirectoryIter* iter,
                                 GError** error);

G_END_DECLS

#endif /* __INFD_DIRECTORY_H__ */

/* vim:set et sw=2 ts=2: */
