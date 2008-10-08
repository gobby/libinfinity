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

#include <infinoted/infinoted-startup.h>

#include <libinfinity/server/infd-directory.h>
#include <libinfinity/server/infd-tcp-server.h>
#include <libinfinity/server/infd-server-pool.h>
#include <libinfinity/common/inf-xmpp-connection.h>
#include <libinfinity/common/inf-xmpp-manager.h>
#include <libinfinity/common/inf-discovery-avahi.h>
#include <libinfinity/common/inf-standalone-io.h>
#include <libinfinity/inf-i18n.h>
#include <libinfinity/inf-config.h>

#include <gnutls/gnutls.h>
#include <glib.h>

#include <locale.h>

static const guint8 IPV6_ANY_ADDR[16] =
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

/* TODO: Move this into infinoted-run.c, and add API to quit the server
 * and access directory, etc. for infinoted-signal.c */
static gboolean
infinoted_main_run(gnutls_certificate_credentials_t credentials,
                   InfdDirectory* directory,
                   guint port,
		   InfXmppConnectionSecurityPolicy policy,
                   GError** error)
{
  InfdTcpServer* tcp;
  InfdServerPool* pool;
  InfdXmppServer* server;
  InfIpAddress* address;
#ifdef INFINOTE_HAVE_AVAHI
  InfXmppManager* xmpp_manager;
  InfDiscoveryAvahi* avahi;
#endif

  address = inf_ip_address_new_raw6(IPV6_ANY_ADDR);

  tcp = INFD_TCP_SERVER(
    g_object_new(
      INFD_TYPE_TCP_SERVER,
      "io", infd_directory_get_io(directory),
      "local-address", address,
      "local-port", port,
      NULL
    )
  );

  inf_ip_address_free(address);

  if(infd_tcp_server_open(tcp, NULL) == FALSE)
  {
    /* IPv6 failed, try IPv4 */
    g_object_set(G_OBJECT(tcp), "local-address", NULL, NULL);
    if(infd_tcp_server_open(tcp, error) == FALSE)
    {
      g_object_unref(tcp);
      return FALSE;
    }
  }

  pool = infd_server_pool_new(directory);
  server = infd_xmpp_server_new(tcp, credentials, NULL, NULL);
  infd_xmpp_server_set_security_policy(server, policy);
  g_object_unref(G_OBJECT(tcp));
  
  infd_server_pool_add_server(pool, INFD_XML_SERVER(server));

#ifdef INFINOTE_HAVE_AVAHI
  xmpp_manager = inf_xmpp_manager_new();
  avahi = inf_discovery_avahi_new(
    infd_directory_get_io(directory),
    xmpp_manager,
    credentials,
    NULL,
    NULL
  );
  g_object_unref(G_OBJECT(xmpp_manager));

  infd_server_pool_add_local_publisher(
    pool,
    server,
    INF_LOCAL_PUBLISHER(avahi)
  );
  g_object_unref(G_OBJECT(avahi));
#endif
  g_object_unref(G_OBJECT(server));

  fprintf(stderr, _("Server running on port %u\n"), port);
  inf_standalone_io_loop(INF_STANDALONE_IO(infd_directory_get_io(directory)));

  g_object_unref(G_OBJECT(pool));
  return TRUE;
}

static gboolean
infinoted_main(int argc,
               char* argv[],
               GError** error)
{
  InfinotedStartup* startup;
  gboolean result;

  startup = infinoted_startup_new(&argc, &argv, error);

  if(startup == NULL)
    return FALSE;

  result = infinoted_main_run(
    startup->credentials,
    startup->directory,
    startup->options->port,
    startup->options->security_policy,
    error
  );

  infinoted_startup_free(startup);
  return result;
}

int
main(int argc,
     char* argv[])
{
  GError* error;

  setlocale(LC_ALL, "");
  _inf_gettext_init();

  error = NULL;
  if(infinoted_main(argc, argv, &error) == FALSE)
  {
    fprintf(stderr, "%s\n", error->message);
    g_error_free(error);
    return -1;
  }

  return 0;
}
