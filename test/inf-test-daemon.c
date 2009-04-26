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
 * Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <libinfinity/inf-config.h>
#include <libinfinity/server/infd-server-pool.h>
#include <libinfinity/server/infd-directory.h>
#include <libinfinity/server/infd-filesystem-storage.h>
#include <libinfinity/server/infd-xmpp-server.h>
#include <libinfinity/server/infd-xml-server.h>
#include <libinfinity/server/infd-tcp-server.h>
#include <libinfinity/communication/inf-communication-manager.h>
#include <libinfinity/common/inf-standalone-io.h>

#ifdef LIBINFINITY_HAVE_AVAHI
#include <libinfinity/common/inf-discovery-avahi.h>
#endif

int
main(int argc, char* argv[])
{
  InfStandaloneIo* io;
  InfdTcpServer* server;
  InfdXmppServer* xmpp;
  InfCommunicationManager* manager;
  InfdServerPool* pool;
  InfdFilesystemStorage* storage;
  InfdDirectory* directory;
  gchar* root_directory;
#ifdef LIBINFINITY_HAVE_AVAHI
  InfDiscoveryAvahi* avahi;
  InfXmppManager* xmpp_manager;
#endif
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
    manager = inf_communication_manager_new();
    storage = infd_filesystem_storage_new(root_directory);

    directory = infd_directory_new(
      INF_IO(io),
      INFD_STORAGE(storage),
      manager
    );

    g_free(root_directory);
    g_object_unref(G_OBJECT(storage));
    g_object_unref(G_OBJECT(manager));

    pool = infd_server_pool_new(directory);
    g_object_unref(G_OBJECT(directory));

    xmpp = infd_xmpp_server_new(
      server,
      INF_XMPP_CONNECTION_SECURITY_ONLY_UNSECURED,
      NULL,
      NULL,
      NULL
    );

    g_object_unref(G_OBJECT(server));

    infd_server_pool_add_server(pool, INFD_XML_SERVER(xmpp));

#ifdef LIBINFINITY_HAVE_AVAHI
    xmpp_manager = inf_xmpp_manager_new();
    avahi = inf_discovery_avahi_new(INF_IO(io), xmpp_manager, NULL, NULL, NULL);
    g_object_unref(G_OBJECT(xmpp_manager));

    infd_server_pool_add_local_publisher(
      pool,
      INFD_XMPP_SERVER(xmpp),
      INF_LOCAL_PUBLISHER(avahi)
    );

    g_object_unref(G_OBJECT(avahi));
#endif
    g_object_unref(G_OBJECT(xmpp));

    inf_standalone_io_loop(io);
    g_object_unref(G_OBJECT(pool));
  }

  g_object_unref(G_OBJECT(io));
  return 0;
}

/* vim:set et sw=2 ts=2: */
