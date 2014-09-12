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

/**
 * SECTION:inf-tcp-connection
 * @title: InfTcpConnection
 * @short_description: Represents a TCP-based connection to a remote host
 * @include: libinfinity/common/inf-tcp-connection.h
 * @stability: Unstable
 *
 * #InfTcpConnection represents a TCP connection to a remove host. It is a
 * wrapper around a native socket object and integrates into the main loop
 * provided by #InfIo. An arbitrary amount of data can be sent with the
 * object, extra data will be buffered and automatically transmitted once
 * kernel space becomes available.
 *
 * The TCP connection properties should be set and then
 * inf_tcp_connection_open() be called to open a connection. If the
 * #InfTcpConnection:resolver property is set, then
 * #InfTcpConnection:remote-address and #InfTcpConnection:remote-port are
 * ignored, and the hostname as configured in the resolver will be resolved.
 * When the hostname has been resolved and a connection has been made, the
 * #InfTcpConnection:remote-address and #InfTcpConnection:remote-port
 * properties are updated to reflect the address actually connected to.
 **/

#include <libinfinity/common/inf-tcp-connection.h>
#include <libinfinity/common/inf-tcp-connection-private.h>
#include <libinfinity/common/inf-name-resolver.h>
#include <libinfinity/common/inf-ip-address.h>
#include <libinfinity/common/inf-io.h>
#include <libinfinity/common/inf-native-socket.h>
#include <libinfinity/inf-signals.h>
#include <libinfinity/inf-i18n.h>
#include <libinfinity/inf-define-enum.h>

#include <unistd.h> /* For ssize_t */

#include "config.h"

#ifndef G_OS_WIN32
# include <sys/types.h>
# include <sys/socket.h>
# include <netinet/in.h>
# include <net/if.h>
# include <arpa/inet.h>
# include <unistd.h>
# include <fcntl.h>

# include <errno.h>
# include <string.h>
#else
# include <ws2tcpip.h>
#endif

static const GEnumValue inf_tcp_connection_status_values[] = {
  {
    INF_TCP_CONNECTION_CONNECTING,
    "INF_TCP_CONNECTION_CONNECTING",
    "connecting"
  }, {
    INF_TCP_CONNECTION_CONNECTED,
    "INF_TCP_CONNECTION_CONNECTED",
    "connected"
  }, {
    INF_TCP_CONNECTION_CLOSED,
    "INF_TCP_CONNECTION_CLOSED",
    "closed"
  }, {
    0,
    NULL,
    NULL
  }
};

typedef struct _InfTcpConnectionPrivate InfTcpConnectionPrivate;
struct _InfTcpConnectionPrivate {
  InfIo* io;
  InfIoEvent events;
  InfIoWatch* watch;

  InfNameResolver* resolver;
  guint resolver_index;

  InfTcpConnectionStatus status;
  InfNativeSocket socket;
  InfKeepalive keepalive;

  InfIpAddress* remote_address;
  guint remote_port;
  unsigned int device_index;

  guint8* queue;
  gsize front_pos;
  gsize back_pos;
  gsize alloc;
};

enum {
  PROP_0,

  PROP_IO,
  PROP_RESOLVER,

  PROP_STATUS,
  PROP_KEEPALIVE,

  PROP_REMOTE_ADDRESS,
  PROP_REMOTE_PORT,
  PROP_LOCAL_ADDRESS,
  PROP_LOCAL_PORT,

  PROP_DEVICE_INDEX,
  PROP_DEVICE_NAME
};

enum {
  SENT,
  RECEIVED,
  ERROR_, /* ERROR is a #define on Win32 */

  LAST_SIGNAL
};

#define INF_TCP_CONNECTION_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TYPE_TCP_CONNECTION, InfTcpConnectionPrivate))

static guint tcp_connection_signals[LAST_SIGNAL];

INF_DEFINE_ENUM_TYPE(InfTcpConnectionStatus, inf_tcp_connection_status, inf_tcp_connection_status_values)
G_DEFINE_TYPE_WITH_CODE(InfTcpConnection, inf_tcp_connection, G_TYPE_OBJECT,
  G_ADD_PRIVATE(InfTcpConnection))

static gboolean
inf_tcp_connection_addr_info(InfNativeSocket socket,
                             gboolean local,
                             InfIpAddress** address,
                             guint* port,
                             GError** error)
{
  union {
    struct sockaddr in_generic;
    struct sockaddr_in in;
    struct sockaddr_in6 in6;
  } native_addr;
  socklen_t len;
  int res;
  int code;

  len = sizeof(native_addr);

  if(local == TRUE)
    res = getsockname(socket, &native_addr.in_generic, &len);
  else
    res = getpeername(socket, &native_addr.in_generic, &len);

  if(res == -1)
  {
    code = INF_NATIVE_SOCKET_LAST_ERROR;
    inf_native_socket_make_error(code, error);
    return FALSE;
  }

  switch(native_addr.in_generic.sa_family)
  {
  case AF_INET:
    if(address != NULL)
      *address = inf_ip_address_new_raw4(native_addr.in.sin_addr.s_addr);
    if(port != NULL)
      *port = ntohs(native_addr.in.sin_port);
    break;
  case AF_INET6:
    if(address != NULL)
      *address = inf_ip_address_new_raw6(native_addr.in6.sin6_addr.s6_addr);
    if(port != NULL)
      *port = ntohs(native_addr.in6.sin6_port);
    break;
  default:
    g_assert_not_reached();
    break;
  }

  return TRUE;
}

static gboolean
inf_tcp_connection_configure_socket(InfNativeSocket socket,
                                    const InfKeepalive* keepalive,
                                    GError** error)
{

#ifdef G_OS_WIN32
  u_long argp;
#else
  int result;
#endif
  int errcode;
  GError* local_error;

  /* Configure the connection's underlying socket, by setting keepalive and
   * and nonblocking. */
#ifndef G_OS_WIN32
  result = fcntl(socket, F_GETFL);
  if(result == INVALID_SOCKET)
  {
    errcode = INF_NATIVE_SOCKET_LAST_ERROR;
    inf_native_socket_make_error(errcode, error);
    return FALSE;
  }

  if(fcntl(socket, F_SETFL, result | O_NONBLOCK) == -1)
  {
    errcode = INF_NATIVE_SOCKET_LAST_ERROR;
    inf_native_socket_make_error(errcode, error);
    return FALSE;
  }
#else
  argp = 1;
  if(ioctlsocket(socket, FIONBIO, &argp) != 0)
  {
    errcode = INF_NATIVE_SOCKET_LAST_ERROR;
    inf_native_socket_make_error(errcode, error);
    return FALSE;
  }
#endif

  /* Error setting keepalives is not fatal */
  local_error = NULL;
  if(inf_keepalive_apply(keepalive, &socket, 0, &local_error) == FALSE)
  {
    g_warning("Failed to set keepalive on socket: %s", local_error->message);
    g_error_free(local_error);
  }

  return TRUE;
}

static void
inf_tcp_connection_system_error(InfTcpConnection* connection,
                                int code)
{
  GError* error;
  error = NULL;

  inf_native_socket_make_error(code, &error);

  g_signal_emit(
    G_OBJECT(connection),
    tcp_connection_signals[ERROR_],
    0,
    error
  );

  g_error_free(error);
}

static void
inf_tcp_connection_io(InfNativeSocket* socket,
                      InfIoEvent events,
                      gpointer user_data);


static void
inf_tcp_connection_connected(InfTcpConnection* connection)
{
  InfTcpConnectionPrivate* priv;
  priv = INF_TCP_CONNECTION_PRIVATE(connection);

  priv->status = INF_TCP_CONNECTION_CONNECTED;
  priv->front_pos = 0;
  priv->back_pos = 0;

  priv->events = INF_IO_INCOMING | INF_IO_ERROR;

  if(priv->watch == NULL)
  {
    priv->watch = inf_io_add_watch(
      priv->io,
      &priv->socket,
      priv->events,
      inf_tcp_connection_io,
      connection,
      NULL
    );
  }
  else
  {
    inf_io_update_watch(priv->io, priv->watch, priv->events);
  }

  g_object_freeze_notify(G_OBJECT(connection));

  /* Update adresses from resolver */
  if(priv->resolver != NULL)
  {
    if(priv->remote_address != NULL)
      inf_ip_address_free(priv->remote_address);

    priv->remote_address = inf_ip_address_copy(
      inf_name_resolver_get_address(priv->resolver, priv->resolver_index)
    );

    priv->remote_port =
      inf_name_resolver_get_port(priv->resolver, priv->resolver_index);

    g_object_notify(G_OBJECT(connection), "remote-address");
    g_object_notify(G_OBJECT(connection), "remote-port");

    priv->resolver_index = 0;
  }

  g_object_notify(G_OBJECT(connection), "status");
  g_object_notify(G_OBJECT(connection), "local-address");
  g_object_notify(G_OBJECT(connection), "local-port");
  g_object_thaw_notify(G_OBJECT(connection));
}

static gboolean
inf_tcp_connection_open_with_resolver(InfTcpConnection* connection,
                                      GError** error);

/* Handles when an error occurred during connection. Returns FALSE when the
 * error was fatal. In this case, it has already emitted the "error" signal.
 * Returns TRUE, if another connection attempt is made. */
static gboolean
inf_tcp_connection_connection_error(InfTcpConnection* connection,
                                    const GError* error)
{
  InfTcpConnectionPrivate* priv;

  priv = INF_TCP_CONNECTION_PRIVATE(connection);

  if(priv->socket != INVALID_SOCKET)
  {
    closesocket(priv->socket);
    priv->socket = INVALID_SOCKET;
  }

  if(priv->watch != NULL)
  {
    priv->events = 0;

    inf_io_remove_watch(priv->io, priv->watch);
    priv->watch = NULL;
  }

  if(priv->resolver != NULL)
  {
    /* Try next address, if there is one */
    if(priv->resolver_index <
       inf_name_resolver_get_n_addresses(priv->resolver))
    {
      ++priv->resolver_index;
      if(inf_tcp_connection_open_with_resolver(connection, NULL) == TRUE)
        return TRUE;
    }

    /* No new addresses available */
    priv->resolver_index = 0;
  }

  g_signal_emit(
    G_OBJECT(connection),
    tcp_connection_signals[ERROR_],
    0,
    error
  );

  return FALSE;
}

static gboolean
inf_tcp_connection_open_real(InfTcpConnection* connection,
                             const InfIpAddress* address,
                             guint port,
                             GError** error)
{
  InfTcpConnectionPrivate* priv;

  union {
    struct sockaddr_in in;
    struct sockaddr_in6 in6;
  } native_address;

  struct sockaddr* addr;
  socklen_t addrlen;
  int result;
  int errcode;
  const InfKeepalive* keepalive;
  GError* local_error;

  priv = INF_TCP_CONNECTION_PRIVATE(connection);

  g_assert(priv->status == INF_TCP_CONNECTION_CLOSED ||
           priv->status == INF_TCP_CONNECTION_CONNECTING);

  /* Close previous socket */
  if(priv->socket != INVALID_SOCKET)
    closesocket(priv->socket);

  switch(inf_ip_address_get_family(address))
  {
  case INF_IP_ADDRESS_IPV4:
    priv->socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    addr = (struct sockaddr*)&native_address.in;
    addrlen = sizeof(struct sockaddr_in);

    memcpy(
      &native_address.in.sin_addr,
      inf_ip_address_get_raw(address),
      sizeof(struct in_addr)
    );

    native_address.in.sin_family = AF_INET;
    native_address.in.sin_port = htons(port);

    break;
  case INF_IP_ADDRESS_IPV6:
    priv->socket = socket(PF_INET6, SOCK_STREAM, IPPROTO_TCP);
    addr = (struct sockaddr*)&native_address.in6;
    addrlen = sizeof(struct sockaddr_in6);

    memcpy(
      &native_address.in6.sin6_addr,
      inf_ip_address_get_raw(address),
      sizeof(struct in6_addr)
    );

    native_address.in6.sin6_family = AF_INET6;
    native_address.in6.sin6_port = htons(port);
    native_address.in6.sin6_flowinfo = 0;
    native_address.in6.sin6_scope_id = priv->device_index;

    break;
  default:
    g_assert_not_reached();
    break;
  }

  if(priv->socket == INVALID_SOCKET)
  {
    inf_native_socket_make_error(INF_NATIVE_SOCKET_LAST_ERROR, error);
    return FALSE;
  }

  /* Set socket non-blocking and keepalive */
  keepalive = &priv->keepalive;
  if(!inf_tcp_connection_configure_socket(priv->socket, keepalive, error))
  {
    closesocket(priv->socket);
    priv->socket = INVALID_SOCKET;
    return FALSE;
  }

  /* Connect */
  do
  {
    result = connect(priv->socket, addr, addrlen);
    errcode = INF_NATIVE_SOCKET_LAST_ERROR;
    if(result == -1 &&
       errcode != INF_NATIVE_SOCKET_EINTR &&
       errcode != INF_NATIVE_SOCKET_EINPROGRESS)
    {
      local_error = NULL;
      inf_native_socket_make_error(errcode, &local_error);
      if(inf_tcp_connection_connection_error(connection, local_error) == TRUE)
      {
        /* In this case, we could recover from the error by connecting to a
         * different address. */
        g_error_free(local_error);
        return TRUE;
      }

      g_propagate_error(error, local_error);
      return FALSE;
    }
  } while(result == -1 && errcode != INF_NATIVE_SOCKET_EINPROGRESS);

  if(result == 0)
  {
    /* Connection fully established */
    inf_tcp_connection_connected(connection);
  }
  else
  {
    g_assert(priv->watch == NULL);

    /* Connection establishment in progress */
    priv->events = INF_IO_OUTGOING | INF_IO_ERROR;

    priv->watch = inf_io_add_watch(
      priv->io,
      &priv->socket,
      priv->events,
      inf_tcp_connection_io,
      connection,
      NULL
    );

    if(priv->status != INF_TCP_CONNECTION_CONNECTING)
    {
      priv->status = INF_TCP_CONNECTION_CONNECTING;
      g_object_notify(G_OBJECT(connection), "status");
    }
  }

  return TRUE;
}

static gboolean
inf_tcp_connection_open_with_resolver(InfTcpConnection* connection,
                                      GError** error)
{
  InfTcpConnectionPrivate* priv;
  GError* local_error;
  gboolean success;

  priv = INF_TCP_CONNECTION_PRIVATE(connection);

  g_assert(priv->status == INF_TCP_CONNECTION_CLOSED ||
           priv->status == INF_TCP_CONNECTION_CONNECTING);

  if(inf_name_resolver_finished(priv->resolver))
  {
    if(priv->resolver_index < 
       inf_name_resolver_get_n_addresses(priv->resolver))
    {
      return inf_tcp_connection_open_real(
        connection,
        inf_name_resolver_get_address(priv->resolver, priv->resolver_index),
        inf_name_resolver_get_port(priv->resolver, priv->resolver_index),
        error
      );
    }

    /* We need to look up more addresses */
    g_object_freeze_notify(G_OBJECT(connection));
    if(priv->status != INF_TCP_CONNECTION_CONNECTING)
    {
      priv->status = INF_TCP_CONNECTION_CONNECTING;
      g_object_notify(G_OBJECT(connection), "status");
    }

    local_error = NULL;
    if(priv->resolver_index == 0)
      success = inf_name_resolver_start(priv->resolver, &local_error);
    else
      success = inf_name_resolver_lookup_backup(priv->resolver, &local_error);

    if(local_error != NULL)
    {
      inf_tcp_connection_connection_error(connection, local_error);
      g_propagate_error(error, local_error);
    }

    g_object_thaw_notify(G_OBJECT(connection));
    return success;
  }

  /* The resolver is currently doing something. Wait until it finishes, and
   * then try again. */
  return TRUE;
}

static gboolean
inf_tcp_connection_send_real(InfTcpConnection* connection,
                             gconstpointer data,
                             guint* len)
{
  InfTcpConnectionPrivate* priv;
  gconstpointer send_data;
  guint send_len;
  int errcode;
  ssize_t result;

  priv = INF_TCP_CONNECTION_PRIVATE(connection);
  g_assert(priv->status == INF_TCP_CONNECTION_CONNECTED);

  g_assert(data != NULL);
  g_assert(len != NULL);

  send_data = data;
  send_len = *len;

  do
  {
    result = send(
      priv->socket,
      send_data,
      send_len,
      INF_NATIVE_SOCKET_SENDRECV_FLAGS
    );

    /* Preserve error code so that it is not modified by future calls */
    errcode = INF_NATIVE_SOCKET_LAST_ERROR;

    if(result < 0 &&
       errcode != INF_NATIVE_SOCKET_EINTR &&
       errcode != INF_NATIVE_SOCKET_EAGAIN)
    {
      inf_tcp_connection_system_error(connection, errcode);
      return FALSE;
    }
    else if(result == 0)
    {
      inf_tcp_connection_close(connection);
      return FALSE;
    }
    else if(result > 0)
    {
      send_data = (const char*)send_data + result;
      send_len -= result;
    }
  } while( (send_len > 0) &&
           (result > 0 || errcode == INF_NATIVE_SOCKET_EINTR) &&
           (priv->socket != INVALID_SOCKET) );

  *len -= send_len;
  return TRUE;
}

static void
inf_tcp_connection_io_incoming(InfTcpConnection* connection)
{
  InfTcpConnectionPrivate* priv;
  gchar buf[2048];
  int errcode;
  ssize_t result;

  priv = INF_TCP_CONNECTION_PRIVATE(connection);

  g_assert(priv->status == INF_TCP_CONNECTION_CONNECTED);

  do
  {
    result = recv(priv->socket, buf, 2048, INF_NATIVE_SOCKET_SENDRECV_FLAGS);
    errcode = INF_NATIVE_SOCKET_LAST_ERROR;

    if(result < 0 &&
       errcode != INF_NATIVE_SOCKET_EINTR &&
       errcode != INF_NATIVE_SOCKET_EAGAIN)
    {
      inf_tcp_connection_system_error(connection, errcode);
    }
    else if(result == 0)
    {
      inf_tcp_connection_close(connection);
    }
    else if(result > 0)
    {
      g_signal_emit(
        G_OBJECT(connection),
        tcp_connection_signals[RECEIVED],
        0,
        buf,
        (guint)result
      );
    }
  } while( ((result > 0) ||
            (result < 0 && errcode == INF_NATIVE_SOCKET_EINTR)) &&
           (priv->status != INF_TCP_CONNECTION_CLOSED));
}

static void
inf_tcp_connection_io_outgoing(InfTcpConnection* connection)
{
  InfTcpConnectionPrivate* priv;

  socklen_t len;
  int errcode;

  gconstpointer data;
  guint data_len;

  priv = INF_TCP_CONNECTION_PRIVATE(connection);
  switch(priv->status)
  {
  case INF_TCP_CONNECTION_CONNECTING:
    len = sizeof(int);
#ifdef G_OS_WIN32
    getsockopt(priv->socket, SOL_SOCKET, SO_ERROR, (char*)&errcode, &len);
#else
    getsockopt(priv->socket, SOL_SOCKET, SO_ERROR, &errcode, &len);
#endif

    if(errcode == 0)
    {
      inf_tcp_connection_connected(connection);
    }
    else
    {
      inf_tcp_connection_system_error(connection, errcode);
    }

    break;
  case INF_TCP_CONNECTION_CONNECTED:
    g_assert(priv->back_pos < priv->front_pos);
    g_assert(priv->events & INF_IO_OUTGOING);

    data = priv->queue + priv->back_pos;
    data_len = priv->front_pos - priv->back_pos;
    if(inf_tcp_connection_send_real(connection, data, &data_len) == TRUE)
    {
      priv->back_pos += data_len;

      if(priv->front_pos == priv->back_pos)
      {
        /* sent everything */
        priv->front_pos = 0;
        priv->back_pos = 0;

        priv->events &= ~INF_IO_OUTGOING;

        inf_io_update_watch(priv->io, priv->watch, priv->events);
      }

      g_signal_emit(
        G_OBJECT(connection),
        tcp_connection_signals[SENT],
        0,
        data,
        data_len
      );
    }

    break;
  case INF_TCP_CONNECTION_CLOSED:
  default:
    g_assert_not_reached();
    break;
  }
}

static void
inf_tcp_connection_io(InfNativeSocket* socket,
                      InfIoEvent events,
                      gpointer user_data)
{
  InfTcpConnection* connection;
  InfTcpConnectionPrivate* priv;
  socklen_t len;
  int errcode;
  GError* error;

  connection = INF_TCP_CONNECTION(user_data);
  priv = INF_TCP_CONNECTION_PRIVATE(connection);
  g_object_ref(G_OBJECT(connection));

  g_assert(priv->status != INF_TCP_CONNECTION_CLOSED);

  if(events & INF_IO_ERROR)
  {
    len = sizeof(int);
#ifdef G_OS_WIN32
    getsockopt(priv->socket, SOL_SOCKET, SO_ERROR, (char*)&errcode, &len);
#else
    getsockopt(priv->socket, SOL_SOCKET, SO_ERROR, &errcode, &len);
#endif

    /* On Windows, we get INF_IO_ERROR on disconnection (at least with the
     * InfGtkIo, because FD_CLOSE is mapped to G_IO_HUP) with errcode
     * being 0. */
    /* TODO: Maybe we should change this by mapping G_IO_HUP to
     * INF_IO_INCOMING, hoping recv() does the right thing then. */
    if(errcode != 0)
    {
      error = NULL;
      inf_native_socket_make_error(errcode, &error);

      if(priv->status == INF_TCP_CONNECTION_CONNECTING)
      {
        inf_tcp_connection_connection_error(connection, error);
      }
      else
      {
        g_signal_emit(
          G_OBJECT(connection),
          tcp_connection_signals[ERROR_],
          0,
          error
        );
      }

      /* Error has been reported via signal emission, and there is nothing
       * else to do with it. */
      g_error_free(error);
    }
    else
    {
      inf_tcp_connection_close(connection);
    }
  }
  else
  {
    if(events & INF_IO_INCOMING)
    {
      inf_tcp_connection_io_incoming(connection);
    }

    /* It may happen that the above closes the connection and we received
     * events for both INCOMING & OUTGOING here. */
    if((priv->status != INF_TCP_CONNECTION_CLOSED) &&
       (events & INF_IO_OUTGOING))
    {
      inf_tcp_connection_io_outgoing(connection);
    }
  }

  g_object_unref(G_OBJECT(connection));
}

static void
inf_tcp_connection_resolved_cb(InfNameResolver* resolver,
                               const GError* error,
                               gpointer user_data)
{
  InfTcpConnection* connection;
  InfTcpConnectionPrivate* priv;

  connection = INF_TCP_CONNECTION(user_data);
  priv = INF_TCP_CONNECTION_PRIVATE(connection);

  /* Note that the connection could even be closed here, namely if
   * tcp_connection_close() was called while we are still resolving. */

  if(priv->status == INF_TCP_CONNECTION_CONNECTING)
  {
    if(error != NULL)
    {
      /* If there was an error, no additional addresses are available */
      g_assert(
        priv->resolver_index == inf_name_resolver_get_n_addresses(resolver)
      );

      inf_tcp_connection_connection_error(connection, error);
    }
    else
    {
      /* If there was no error, try opening a connection to the resolved
       * address(es). */
      inf_tcp_connection_open_with_resolver(connection, NULL);
    }
  }
}

static void
inf_tcp_connection_set_resolver(InfTcpConnection* connection,
                                InfNameResolver* resolver)
{
  InfTcpConnectionPrivate* priv;
  priv = INF_TCP_CONNECTION_PRIVATE(connection);

  if(priv->resolver != NULL)
  {
    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->resolver),
      G_CALLBACK(inf_tcp_connection_resolved_cb),
      connection
    );

    g_object_unref(priv->resolver);
  }

  priv->resolver = resolver;

  if(resolver != NULL)
  {
    g_object_ref(resolver);

    g_signal_connect(
      G_OBJECT(resolver),
      "resolved",
      G_CALLBACK(inf_tcp_connection_resolved_cb),
      connection
    );
  }
}

static void
inf_tcp_connection_init(InfTcpConnection* connection)
{
  InfTcpConnectionPrivate* priv;
  priv = INF_TCP_CONNECTION_PRIVATE(connection);

  priv->io = NULL;
  priv->events = 0;
  priv->watch = NULL;
  priv->resolver = NULL;
  priv->resolver_index = 0;
  priv->status = INF_TCP_CONNECTION_CLOSED;
  priv->socket = INVALID_SOCKET;
  priv->keepalive.mask = 0;

  priv->remote_address = NULL;
  priv->remote_port = 0;
  priv->device_index = 0;

  priv->queue = g_malloc(1024);
  priv->front_pos = 0;
  priv->back_pos = 0;
  priv->alloc = 1024;
}

static void
inf_tcp_connection_dispose(GObject* object)
{
  InfTcpConnection* connection;
  InfTcpConnectionPrivate* priv;

  connection = INF_TCP_CONNECTION(object);
  priv = INF_TCP_CONNECTION_PRIVATE(connection);

  if(priv->status != INF_TCP_CONNECTION_CLOSED)
    inf_tcp_connection_close(connection);

  inf_tcp_connection_set_resolver(connection, NULL);

  if(priv->io != NULL)
  {
    g_object_unref(priv->io);
    priv->io = NULL;
  }

  G_OBJECT_CLASS(inf_tcp_connection_parent_class)->dispose(object);
}

static void
inf_tcp_connection_finalize(GObject* object)
{
  InfTcpConnection* connection;
  InfTcpConnectionPrivate* priv;

  connection = INF_TCP_CONNECTION(object);
  priv = INF_TCP_CONNECTION_PRIVATE(connection);

  if(priv->remote_address != NULL)
    inf_ip_address_free(priv->remote_address);

  if(priv->socket != INVALID_SOCKET)
    closesocket(priv->socket);

  g_free(priv->queue);

  G_OBJECT_CLASS(inf_tcp_connection_parent_class)->finalize(object);
}

static void
inf_tcp_connection_set_property(GObject* object,
                                guint prop_id,
                                const GValue* value,
                                GParamSpec* pspec)
{
  InfTcpConnection* connection;
  InfTcpConnectionPrivate* priv;
#ifndef G_OS_WIN32
  const gchar* device_string;
  unsigned int new_index;
#endif
  GError* error;

  connection = INF_TCP_CONNECTION(object);
  priv = INF_TCP_CONNECTION_PRIVATE(connection);

  switch(prop_id)
  {
  case PROP_IO:
    g_assert(priv->status == INF_TCP_CONNECTION_CLOSED);
    if(priv->io != NULL) g_object_unref(G_OBJECT(priv->io));
    priv->io = INF_IO(g_value_dup_object(value));
    break;
  case PROP_RESOLVER:
    g_assert(priv->status == INF_TCP_CONNECTION_CLOSED);

    inf_tcp_connection_set_resolver(
      connection,
      INF_NAME_RESOLVER(g_value_get_object(value))
    );

    break;
  case PROP_KEEPALIVE:
    error = NULL;

    inf_tcp_connection_set_keepalive(
      connection,
      (InfKeepalive*)g_value_get_boxed(value),
      &error
    );
    
    if(error != NULL)
    {
      g_warning("Failed to set keepalive settings: %s\n", error->message);
      g_error_free(error);
    }

    break;
  case PROP_REMOTE_ADDRESS:
    g_assert(priv->status == INF_TCP_CONNECTION_CLOSED);
    if(priv->remote_address != NULL)
      inf_ip_address_free(priv->remote_address);
    priv->remote_address = (InfIpAddress*)g_value_dup_boxed(value);
    break;
  case PROP_REMOTE_PORT:
    g_assert(priv->status == INF_TCP_CONNECTION_CLOSED);
    priv->remote_port = g_value_get_uint(value);
    break;
  case PROP_DEVICE_INDEX:
    g_assert(priv->status == INF_TCP_CONNECTION_CLOSED);
    /* TODO: Verify that such a device exists */
    priv->device_index = g_value_get_uint(value);
    g_object_notify(G_OBJECT(object), "device-name");
    break;
  case PROP_DEVICE_NAME:
#ifdef G_OS_WIN32
    /* TODO: We can probably implement this using GetInterfaceInfo() */
    g_warning("The device-name property is not implemented on Win32");
#else
    g_assert(priv->status == INF_TCP_CONNECTION_CLOSED);
    device_string = g_value_get_string(value);
    if(device_string == NULL) priv->device_index = 0;

    new_index = if_nametoindex(device_string);
    if(new_index == 0)
    {
      g_warning(_("Interface `%s' does not exist"), device_string);
    }
    else
    {
      priv->device_index = new_index;
      g_object_notify(G_OBJECT(object), "device-index");
    }
#endif
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_tcp_connection_get_property(GObject* object,
                                guint prop_id,
                                GValue* value,
                                GParamSpec* pspec)
{
  InfTcpConnection* connection;
  InfTcpConnectionPrivate* priv;
  InfIpAddress* address;
  guint port;
  GError* error;
#ifndef G_OS_WIN32
  char device_name[IF_NAMESIZE];
#endif

  connection = INF_TCP_CONNECTION(object);
  priv = INF_TCP_CONNECTION_PRIVATE(connection);

  switch(prop_id)
  {
  case PROP_IO:
    g_value_set_object(value, G_OBJECT(priv->io));
    break;
  case PROP_RESOLVER:
    g_value_set_object(value, G_OBJECT(priv->resolver));
    break;
  case PROP_KEEPALIVE:
    g_value_set_boxed(value, &priv->keepalive);
    break;
  case PROP_STATUS:
    g_value_set_enum(value, priv->status);
    break;
  case PROP_REMOTE_ADDRESS:
    g_value_set_static_boxed(value, priv->remote_address);
    break;
  case PROP_REMOTE_PORT:
    g_value_set_uint(value, priv->remote_port);
    break;
  case PROP_LOCAL_ADDRESS:
    g_assert(priv->socket != INVALID_SOCKET);

    error = NULL;
    inf_tcp_connection_addr_info(priv->socket, TRUE, &address, NULL, &error);

    if(error != NULL)
    {
      g_warning(_("Failed to retrieve local address: %s"), error->message);
      g_error_free(error);
      g_value_set_boxed(value, NULL);
    }
    else
    {
      g_value_take_boxed(value, address);
    }

    break;
  case PROP_LOCAL_PORT:
    g_assert(priv->socket != INVALID_SOCKET);

    error = NULL;
    inf_tcp_connection_addr_info(priv->socket, TRUE, NULL, &port, &error);

    if(error != NULL)
    {
      g_warning(_("Failed to retrieve local port: %s"), error->message);
      g_error_free(error);
      g_value_set_uint(value, 0);
    }
    else
    {
      g_value_set_uint(value, port);
    }

    break;
  case PROP_DEVICE_INDEX:
    g_value_set_uint(value, priv->device_index);
    break;
  case PROP_DEVICE_NAME:
#ifdef G_OS_WIN32
    /* TODO: We can probably implement this using GetInterfaceInfo() */
    g_warning(_("The device-name property is not implemented on Win32"));
    g_value_set_string(value, NULL);
#else
    if(priv->device_index == 0)
    {
      g_value_set_string(value, NULL);
    }
    else
    {
      if(if_indextoname(priv->device_index, device_name) == NULL)
      {
        g_warning(
          /* Failed to get name for device <Index>: <Reason> */
          _("Failed to get name for device %u: %s"),
          priv->device_index,
          strerror(errno)
        );

        g_value_set_string(value, NULL);
      }
      else
      {
        g_value_set_string(value, device_name);
      }
    }
#endif
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_tcp_connection_error(InfTcpConnection* connection,
                         GError* error)
{
  InfTcpConnectionPrivate* priv;
  priv = INF_TCP_CONNECTION_PRIVATE(connection);

  /* Normally, it would be enough to check one of both conditions, but socket
   * may be already set with status still being CLOSED during
   * inf_tcp_connection_open(). */
  if(priv->watch != NULL)
  {
    priv->events = 0;

    inf_io_remove_watch(priv->io, priv->watch);
    priv->watch = NULL;
  }

  if(priv->status != INF_TCP_CONNECTION_CLOSED)
  {
    priv->status = INF_TCP_CONNECTION_CLOSED;
    g_object_notify(G_OBJECT(connection), "status");
  }
}

static void
inf_tcp_connection_class_init(InfTcpConnectionClass* tcp_connection_class)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(tcp_connection_class);

  object_class->dispose = inf_tcp_connection_dispose;
  object_class->finalize = inf_tcp_connection_finalize;
  object_class->set_property = inf_tcp_connection_set_property;
  object_class->get_property = inf_tcp_connection_get_property;

  tcp_connection_class->sent = NULL;
  tcp_connection_class->received = NULL;
  tcp_connection_class->error = inf_tcp_connection_error;

  g_object_class_install_property(
    object_class,
    PROP_IO,
    g_param_spec_object(
      "io",
      "IO",
      "I/O handler",
      INF_TYPE_IO,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_RESOLVER,
    g_param_spec_object(
      "resolver",
      "Resolver",
      "The hostname resolver",
      INF_TYPE_NAME_RESOLVER,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_STATUS,
    g_param_spec_enum(
      "status",
      "Status",
      "Status of the TCP connection",
      INF_TYPE_TCP_CONNECTION_STATUS,
      INF_TCP_CONNECTION_CLOSED,
      G_PARAM_READABLE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_KEEPALIVE,
    g_param_spec_boxed(
      "keepalive",
      "Keepalive",
      "The keepalive settings for the connection",
      INF_TYPE_KEEPALIVE,
      G_PARAM_READWRITE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_REMOTE_ADDRESS,
    g_param_spec_boxed(
      "remote-address",
      "Remote address",
      "Address to connect to",
      INF_TYPE_IP_ADDRESS,
      G_PARAM_READWRITE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_REMOTE_PORT,
    g_param_spec_uint(
      "remote-port",
      "Remote port",
      "Port to connect to",
      0,
      65535,
      0,
      G_PARAM_READWRITE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_LOCAL_ADDRESS,
    g_param_spec_boxed(
      "local-address",
      "Local address",
      "The local address of the connection",
      INF_TYPE_IP_ADDRESS,
      G_PARAM_READABLE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_LOCAL_PORT,
    g_param_spec_uint(
      "local-port",
      "Local port",
      "The local port of the connection",
      0,
      65535,
      0,
      G_PARAM_READABLE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_DEVICE_INDEX,
    g_param_spec_uint(
      "device-index",
      "Device index",
      "The index of the device to use for the connection",
      0,
      G_MAXUINT,
      0,
      G_PARAM_READWRITE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_DEVICE_NAME,
    g_param_spec_string(
      "device-name",
      "Device name",
      "The name of the device to use for the connection, such as `eth0'",
      NULL,
      G_PARAM_READWRITE
    )
  );

  /**
   * InfTcpConnection::sent:
   * @connection: The #InfTcpConnection through which the data has been sent.
   * @data: A #gpointer refering to the data that has been sent.
   * @length: A #guint holding the number of bytes that has been sent.
   *
   * This signal is emitted whenever data has been sent over the connection.
   */
  tcp_connection_signals[SENT] = g_signal_new(
    "sent",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfTcpConnectionClass, sent),
    NULL, NULL,
    NULL,
    G_TYPE_NONE,
    2,
    G_TYPE_POINTER,
    G_TYPE_UINT
  );

  /**
   * InfTcpConnection::received:
   * @connection: The #InfTcpConnection through which the data has been
   * received.
   * @data: A #gpointer refering to the data that has been received.
   * @length: A #guint holding the number of bytes that has been received.
   *
   * This signal is emitted whenever data has been received from the
   * connection.
   */
  tcp_connection_signals[RECEIVED] = g_signal_new(
    "received",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfTcpConnectionClass, received),
    NULL, NULL,
    NULL,
    G_TYPE_NONE,
    2,
    G_TYPE_POINTER,
    G_TYPE_UINT
  );

  /**
   * InfTcpConnection::error:
   * @connection: The erroneous #InfTcpConnection.
   * @error: A pointer to a #GError object with details on the error.
   *
   * This signal is emitted when an error occured with the connection. If the
   * error is fatal, the connection will change its status to
   * %INF_TCP_CONNECTION_CLOSED.
   */
  tcp_connection_signals[ERROR_] = g_signal_new(
    "error",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfTcpConnectionClass, error),
    NULL, NULL,
    g_cclosure_marshal_VOID__POINTER,
    G_TYPE_NONE,
    1,
    G_TYPE_POINTER /* actually a GError* */
  );
}

/**
 * inf_tcp_connection_new: (constructor)
 * @io: A #InfIo object used to watch for activity.
 * @remote_addr: The address to eventually connect to.
 * @remote_port: The port to eventually connect to.
 *
 * Creates a new #InfTcpConnection. The arguments are stored as properties for
 * an eventual inf_tcp_connection_open() call, this function itself does not
 * establish a connection.
 *
 * Returns: (transfer full): A new #InfTcpConnection. Free with
 * g_object_unref().
 **/
InfTcpConnection*
inf_tcp_connection_new(InfIo* io,
                       const InfIpAddress* remote_addr,
                       guint remote_port)
{
  InfTcpConnection* tcp;

  g_return_val_if_fail(INF_IS_IO(io), NULL);
  g_return_val_if_fail(remote_addr != NULL, NULL);
  g_return_val_if_fail(remote_port <= 65535, NULL);

  tcp = INF_TCP_CONNECTION(
    g_object_new(
      INF_TYPE_TCP_CONNECTION,
      "io", io,
      "remote-address", remote_addr,
      "remote-port", remote_port,
      NULL
    )
  );

  return tcp;
}

/**
 * inf_tcp_connection_new_and_open: (constructor)
 * @io: A #InfIo object used to watch for activity.
 * @remote_addr: The address to connect to.
 * @remote_port: The port to connect to.
 * @error: Location to store error information.
 *
 * Creates a new #InfTcpConnection and connects it to the given TCP endpoint.
 * Like inf_tcp_connection_new(), but calls inf_tcp_connection_open().
 *
 * Returns: (transfer full): A new #InfTcpConnection, or %NULL on error.
 * Free with g_object_unref().
 **/
InfTcpConnection*
inf_tcp_connection_new_and_open(InfIo* io,
                                const InfIpAddress* remote_addr,
                                guint remote_port,
                                GError** error)
{
  InfTcpConnection* tcp;

  g_return_val_if_fail(INF_IS_IO(io), NULL);
  g_return_val_if_fail(remote_addr != NULL, NULL);
  g_return_val_if_fail(remote_port <= 65535, NULL);
  g_return_val_if_fail(error == NULL || *error == NULL, NULL);

  tcp = inf_tcp_connection_new(io, remote_addr, remote_port);

  if(inf_tcp_connection_open(tcp, error) == FALSE)
  {
    g_object_unref(tcp);
    return NULL;
  }

  return tcp;
}

/**
 * inf_tcp_connection_new_resolve: (constructor)
 * @io: A #InfIo object used to watch for activity.
 * @resolver: The hostname resolver object used to look up the remote
 * hostname.
 *
 * Creates a new #InfTcpConnection and instead of setting the remote IP
 * address and port number directly, a hostname resolver is used to look up
 * the remote hostname before connecting. This has the advantage that all
 * available addresses for that hostname are tried before giving up.
 *
 * The argument is stored as a property for an eventual
 * inf_tcp_connection_open() call, this function itself does not
 * establish a connection.
 *
 * Returns: (transfer full): A new #InfTcpConnection. Free with
 * g_object_unref().
 */
InfTcpConnection*
inf_tcp_connection_new_resolve(InfIo* io,
                               InfNameResolver* resolver)
{
  InfTcpConnection* tcp;

  g_return_val_if_fail(INF_IS_IO(io), NULL);
  g_return_val_if_fail(INF_IS_NAME_RESOLVER(resolver), NULL);

  tcp = INF_TCP_CONNECTION(
    g_object_new(
      INF_TYPE_TCP_CONNECTION,
      "io", io,
      "resolver", resolver,
      NULL
    )
  );

  return tcp;
}

/**
 * inf_tcp_connection_open:
 * @connection: A #InfTcpConnection.
 * @error: Location to store error information.
 *
 * Attempts to open @connection. Make sure to have set the "remote-address"
 * and "remote-port" property before calling this function. If an error
 * occurs, the function returns %FALSE and @error is set. Note however that
 * the connection might not be fully open when the function returns
 * (check the "status" property if you need to know). If an asynchronous
 * error occurs while the connection is being opened, the "error" signal
 * is emitted.
 *
 * Returns: %FALSE if an error occured and %TRUE otherwise.
 **/
gboolean
inf_tcp_connection_open(InfTcpConnection* connection,
                        GError** error)
{
  InfTcpConnectionPrivate* priv;

  g_return_val_if_fail(INF_IS_TCP_CONNECTION(connection), FALSE);
  priv = INF_TCP_CONNECTION_PRIVATE(connection);

  g_return_val_if_fail(priv->io != NULL, FALSE);
  g_return_val_if_fail(priv->status == INF_TCP_CONNECTION_CLOSED, FALSE);

  g_return_val_if_fail(
    priv->remote_address != NULL || priv->resolver != NULL,
    FALSE
  );

  g_return_val_if_fail(
    priv->remote_port != 0 ||
    priv->resolver != NULL,
    FALSE
  );

  if(priv->resolver != NULL)
  {
    g_assert(priv->resolver_index == 0);
    return inf_tcp_connection_open_with_resolver(connection, error);
  }
  else
  {
    return inf_tcp_connection_open_real(
      connection,
      priv->remote_address,
      priv->remote_port,
      error
    );
  }
}

/**
 * inf_tcp_connection_close:
 * @connection: A #InfTcpConnection.
 *
 * Closes a TCP connection that is either open or currently connecting.
 **/
void
inf_tcp_connection_close(InfTcpConnection* connection)
{
  InfTcpConnectionPrivate* priv;

  g_return_if_fail(INF_IS_TCP_CONNECTION(connection));

  priv = INF_TCP_CONNECTION_PRIVATE(connection);
  g_return_if_fail(priv->status != INF_TCP_CONNECTION_CLOSED);

  priv->events = 0;

  if(priv->watch != NULL)
  {
    inf_io_remove_watch(priv->io, priv->watch);
    priv->watch = NULL;
  }

  priv->front_pos = 0;
  priv->back_pos = 0;

  priv->status = INF_TCP_CONNECTION_CLOSED;
  g_object_notify(G_OBJECT(connection), "status");
}

/**
 * inf_tcp_connection_send:
 * @connection: A #InfTcpConnection with status %INF_TCP_CONNECTION_CONNECTED.
 * @data: (type guint8*) (array length=len): The data to send.
 * @len: Number of bytes to send.
 *
 * Sends data through the TCP connection. The data is not sent immediately,
 * but enqueued to a buffer and will be sent as soon as kernel space
 * becomes available. The "sent" signal will be emitted when data has
 * really been sent.
 **/
void
inf_tcp_connection_send(InfTcpConnection* connection,
                        gconstpointer data,
                        guint len)
{
  InfTcpConnectionPrivate* priv;
  gconstpointer sent_data;
  guint sent_len;

  g_return_if_fail(INF_IS_TCP_CONNECTION(connection));
  g_return_if_fail(len == 0 || data != NULL);

  priv = INF_TCP_CONNECTION_PRIVATE(connection);
  g_return_if_fail(priv->status == INF_TCP_CONNECTION_CONNECTED);

  g_object_ref(connection);

  /* Check whether we have data currently queued. If we have, then we need
   * to wait until that data has been sent before sending the new data. */
  if(priv->front_pos == priv->back_pos)
  {
    /* Must not be set, because otherwise we would need something to send,
     * but there is nothing in the queue. */
    g_assert(~priv->events & INF_IO_OUTGOING);

    /* Nothing in queue, send data directly. */
    sent_len = len;
    sent_data = data;

    if(inf_tcp_connection_send_real(connection, data, &sent_len) == TRUE)
    {
      data = (const char*)data + sent_len;
      len -= sent_len;
    }
    else
    {
      /* Sending failed. The error signal has been emitted. */
      /* Set len to zero so that we don't enqueue data. */
      len = 0;
      sent_len = 0;
    }
  }
  else
  {
    /* Nothing sent */
    sent_len = 0;
  }

  /* If we couldn't send all the data... */
  if(len > 0)
  {
    /* If we have not enough space for the new data, move queue data back
     * onto the beginning of the queue, if not already */
    if(priv->alloc - priv->front_pos < len && priv->back_pos > 0)
    {
      memmove(
        priv->queue,
        priv->queue + priv->back_pos,
        priv->front_pos - priv->back_pos
      );

      priv->front_pos -= priv->back_pos;
      priv->back_pos = 0;
    }

    /* Allocate more memory if there is still not enough space */
    if(priv->alloc - priv->front_pos < len)
    {
      /* Make sure we allocate enough */
      priv->alloc = priv->front_pos + len;

      /* Always allocate a multiple of 1024 */
      if(priv->alloc % 1024 != 0)
        priv->alloc = priv->alloc + (1024 - priv->alloc % 1024);

      priv->queue = g_realloc(priv->queue, priv->alloc);
    }

    memcpy(priv->queue + priv->front_pos, data, len);
    priv->front_pos += len;

    if(~priv->events & INF_IO_OUTGOING)
    {
      priv->events |= INF_IO_OUTGOING;
      inf_io_update_watch(priv->io, priv->watch, priv->events);
    }
  }

  if(sent_len > 0)
  {
    g_signal_emit(
      G_OBJECT(connection),
      tcp_connection_signals[SENT],
      0,
      sent_data,
      sent_len
    );
  }

  g_object_unref(connection);
}

/**
 * inf_tcp_connection_get_remote_address:
 * @connection: A #InfTcpConnection.
 *
 * Returns the IP address of the remote site.
 *
 * Returns: (transfer none): A #InfIpAddress owned by @connection. You do not
 * need to free it, but need to make your own copy if you want to keep it
 * longer than @connection's lifetime.
 **/
InfIpAddress*
inf_tcp_connection_get_remote_address(InfTcpConnection* connection)
{
  g_return_val_if_fail(INF_IS_TCP_CONNECTION(connection), NULL);
  return INF_TCP_CONNECTION_PRIVATE(connection)->remote_address;
}

/**
 * inf_tcp_connection_get_remote_port:
 * @connection: A #InfTcpConnection.
 *
 * Returns the port of the remote site to which @connection is (or was)
 * connected or connecting.
 *
 * Returns: The port of the remote site.
 **/
guint
inf_tcp_connection_get_remote_port(InfTcpConnection* connection)
{
  g_return_val_if_fail(INF_IS_TCP_CONNECTION(connection), 0);
  return INF_TCP_CONNECTION_PRIVATE(connection)->remote_port;
}

/**
 * inf_tcp_connection_set_keepalive:
 * @connection: A #InfTcpConnection.
 * @keepalive: New keepalive settings for the connection.
 * @error: Location to store error information, if any, or %NULL.
 *
 * Sets the keepalive settings for @connection. When this function is not
 * called, the system defaults are used. If the connection is closed, then
 * the function always succeeds and stores the keepalive values internally.
 * The values are actually set on the underlying socket when the connection
 * is opened. If the connection is already open, the function might fail if
 * the system call fails.
 *
 * Returns: %TRUE if the new keeplalive values were set, or %FALSE on error.
 */
gboolean
inf_tcp_connection_set_keepalive(InfTcpConnection* connection,
                                 const InfKeepalive* keepalive,
                                 GError** error)
{
  InfTcpConnectionPrivate* priv;
  InfKeepaliveMask mask;

  g_return_val_if_fail(INF_IS_TCP_CONNECTION(connection), FALSE);
  g_return_val_if_fail(keepalive != NULL, FALSE);
  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

  priv = INF_TCP_CONNECTION_PRIVATE(connection);

  if(priv->socket != INVALID_SOCKET)
  {
    mask = priv->keepalive.mask;
    if(inf_keepalive_apply(keepalive, &priv->socket, mask, error) != TRUE)
      return FALSE;
  }

  priv->keepalive = *keepalive;
  return TRUE;
}

/**
 * inf_tcp_connection_get_keepalive:
 * @connection: A #InfTcpConnection.
 *
 * Returns the current keepalive settings for @connection.
 *
 * Returns: The current keepalive configuration for @connection, owned by
 * @connection.
 */
const InfKeepalive*
inf_tcp_connection_get_keepalive(InfTcpConnection* connection)
{
  g_return_val_if_fail(INF_IS_TCP_CONNECTION(connection), NULL);
  return &INF_TCP_CONNECTION_PRIVATE(connection)->keepalive;
}

/* Creates a new TCP connection from an accepted socket. This is only used
 * by InfdTcpServer and should not be considered regular API. Do not call
 * this function. Language bindings should not wrap it. */
InfTcpConnection*
_inf_tcp_connection_accepted(InfIo* io,
                             InfNativeSocket socket,
                             InfIpAddress* address,
                             guint port,
                             const InfKeepalive* keepalive,
                             GError** error)
{
  InfTcpConnection* connection;
  InfTcpConnectionPrivate* priv;
  int errcode;

  g_return_val_if_fail(INF_IS_IO(io), NULL);
  g_return_val_if_fail(socket != INVALID_SOCKET, NULL);
  g_return_val_if_fail(address != NULL, NULL);
  g_return_val_if_fail(keepalive != NULL, NULL);

  if(inf_tcp_connection_configure_socket(socket, keepalive, error) != TRUE)
    return NULL;

  g_return_val_if_fail(address != NULL, NULL);
  g_return_val_if_fail(port != 0, NULL);

  connection = inf_tcp_connection_new(io, address, port);

  inf_ip_address_free(address);

  priv = INF_TCP_CONNECTION_PRIVATE(connection);
  priv->socket = socket;
  priv->keepalive = *keepalive;

  inf_tcp_connection_connected(connection);
  return connection;
}

/* vim:set et sw=2 ts=2: */
