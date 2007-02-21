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

#include <libinfinity/inf-connection-manager.h>

#include <libgnetwork/gnetwork-tcp-server.h>
#include <libgnetwork/gnetwork-server.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INFD_TYPE_DIRECTORY                 (infd_directory_get_type())
#define INFD_DIRECTORY(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INFD_TYPE_DIRECTORY, InfdDirectory))
#define INFD_DIRECTORY_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INFD_TYPE_DIRECTORY, InfdDirectoryClass))
#define INFD_IS_DIRECTORY(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INFD_TYPE_DIRECTORY))
#define INFD_IS_DIRECTORY_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INFD_TYPE_DIRECTORY))
#define INFD_DIRECTORY_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INFD_TYPE_DIRECTORY, InfdDirectoryClass))

typedef struct _InfdDirectory InfdDirectory;
typedef struct _InfdDirectoryClass InfdDirectoryClass;

struct _InfdDirectoryClass {
  GObjectClass parent_class;
};

struct _InfdDirectory {
  GObject parent;
};

GType
infd_directory_get_type(void) G_GNUC_CONST;

InfdDirectory*
infd_directory_new(InfdDirectoryStorage* storage,
                   InfConnectionManager* connection_manager);

InfdDirectoryStorage*
infd_directory_get_storage(InfdDirectory* directory);

InfConnectionManager*
infd_directory_get_connection_manager(InfdDirectory* directory);

void
infd_directory_set_server(InfdDirectory* directory,
                          GNetworkServer* server);

GNetworkTcpServer*
infd_directory_open_server(InfdDirectory* directory,
                           const gchar* interface,
                           guint port);

G_END_DECLS

#endif /* __INFD_DIRECTORY_H__ */
