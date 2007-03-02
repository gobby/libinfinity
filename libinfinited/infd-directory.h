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

#ifndef __INFD_DIRECTORY_H__
#define __INFD_DIRECTORY_H__

#include <libinfinited/infd-directory-storage.h>

#include <libinfinity/inf-session.h>
#include <libinfinity/inf-connection-manager.h>

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

  /* Virtual Table */
  InfTextBuffer* (*text_buffer_new)(InfdDirectory* directory);
  InfInkBuffer* (*ink_buffer_new)(InfdDirectory* directory);
  InfSession* (*text_session_new)(InfdDirectory* directory);
  InfSession* (*ink_session_new)(InfdDirectory* directory);
};

struct _InfdDirectory {
  GObject parent;
};

GType
infd_directory_iter_get_type(void) G_GNUC_CONST;

GType
infd_directory_get_type(void) G_GNUC_CONST;

InfdDirectoryIter*
infd_directory_iter_copy(InfdDirectoryIter* iter);

void
infd_directory_iter_free(InfdDirectoryIter* iter);

InfdDirectory*
infd_directory_new(InfdDirectoryStorage* storage,
                   InfConnectionManager* connection_manager);

InfdDirectoryStorage*
infd_directory_get_storage(InfdDirectory* directory);

InfConnectionManager*
infd_directory_get_connection_manager(InfdDirectory* directory);

void
infd_directory_add_connection(InfdDirectory* directory,
                              GNetworkConnection* connection);

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
infd_directory_add_subdirectory(InfdDirectory* directory,
                                InfdDirectoryIter* parent,
                                InfdDirectoryIter* iter,
                                const gchar* path,
                                GError** error);

gboolean
infd_directory_add_text(InfdDirectory* directory,
                        InfdDirectoryIter* parent,
                        InfdDirectoryIter* iter,
                        GError** error);

gboolean
infd_directory_add_ink(InfdDirectory* directory,
                       InfdDirectoryIter* parent,
                       InfdDirectoryIter* iter,
                       GError** error);

gboolean
infd_directory_remove_node(InfdDirectory* directory,
                           InfdDirectoryIter* iter,
                           GError** error);

InfdDirectoryStorageNodeType
infd_directory_iter_get_node_type(InfdDirectory* directory,
                                  InfdDirectoryIter* iter);

InfSession*
infd_directory_iter_get_session(InfdDirectory* directory,
                                InfdDirectoryIter* iter,
                                GError** error);

G_END_DECLS

#endif /* __INFD_DIRECTORY_H__ */
