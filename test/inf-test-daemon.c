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

#include <libinfinity/server/infd-server-pool.h>
#include <libinfinity/server/infd-directory.h>
#include <libinfinity/server/infd-filesystem-storage.h>
#include <libinfinity/server/infd-xmpp-server.h>
#include <libinfinity/server/infd-xml-server.h>
#include <libinfinity/server/infd-tcp-server.h>
#include <libinfinity/common/inf-connection-manager.h>
#include <libinfinity/common/inf-standalone-io.h>

int
main(int argc, char* argv[])
{
  InfStandaloneIo* io;
  InfdTcpServer* server;
  InfdXmppServer* xmpp;
  InfConnectionManager* manager;
  InfdServerPool* pool;
  InfdFilesystemStorage* storage;
  InfdDirectory* directory;
  gchar* root_directory;
  GError* error;

  gnutls_global_init();
  g_type_init();

  io = inf_standalone_io_new();

  server = g_object_new(
    INFD_TYPE_TCP_SERVER,
    "io", io,
    "local-port", 6523,
    NULL
  );

  error = NULL;
  if(infd_tcp_server_open(server, &error) == FALSE)
  {
    fprintf(stderr, "Could not open server: %s\n", error->message);
    g_error_free(error);
  }
  else
  {
    root_directory = g_build_filename(g_get_home_dir(), ".infinote", NULL);
    manager = inf_connection_manager_new();
    storage = infd_filesystem_storage_new(root_directory);
    directory = infd_directory_new(INFD_STORAGE(storage), manager);

    g_free(root_directory);
    g_object_unref(G_OBJECT(storage));
    g_object_unref(G_OBJECT(manager));

    pool = infd_server_pool_new(directory);
    g_object_unref(G_OBJECT(directory));

    xmpp = infd_xmpp_server_new(server, "localhost", NULL, NULL);
    g_object_unref(G_OBJECT(server));

    infd_server_pool_add_server(pool, INFD_XML_SERVER(xmpp));
    g_object_unref(G_OBJECT(xmpp));

    inf_standalone_io_loop(io);
    g_object_unref(G_OBJECT(pool));
  }

  g_object_unref(G_OBJECT(io));
  return 0;
}
