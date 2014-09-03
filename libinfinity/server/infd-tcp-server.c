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

#include <libinfinity/server/infd-tcp-server.h>
#include <libinfinity/common/inf-tcp-connection-private.h>
#include <libinfinity/common/inf-ip-address.h>
#include <libinfinity/common/inf-io.h>
#include <libinfinity/common/inf-native-socket.h>
#include <libinfinity/inf-marshal.h>
#include <libinfinity/inf-define-enum.h>
#include <config.h>

#ifndef G_OS_WIN32
# include <sys/types.h>
# include <sys/socket.h>
# include <netinet/in.h>
# include <arpa/inet.h>
# include <unistd.h>
# include <fcntl.h>

# include <errno.h>
# include <string.h>
#else
# include <ws2tcpip.h>
#endif

static const GEnumValue infd_tcp_server_status_values[] = {
  {
    INFD_TCP_SERVER_CLOSED,
    "INFD_TCP_SERVER_CLOSED",
    "closed"
  }, {
    INFD_TCP_SERVER_OPEN,
    "INFD_TCP_SERVER_OPEN",
    "open"
  }, {
    0,
    NULL,
    NULL
  }
};

typedef struct _InfdTcpServerPrivate InfdTcpServerPrivate;
struct _InfdTcpServerPrivate {
  InfIo* io;
  InfIoWatch* watch;

  InfNativeSocket socket;
  InfdTcpServerStatus status;

  InfIpAddress* local_address;
  guint local_port;
};

enum {
  PROP_0,

  PROP_IO,

  PROP_STATUS,

  PROP_LOCAL_ADDRESS,
  PROP_LOCAL_PORT
};

enum {
  NEW_CONNECTION,
  ERROR_, /* ERROR is a #define on WIN32 */

  LAST_SIGNAL
};

#define INFD_TCP_SERVER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFD_TYPE_TCP_SERVER, InfdTcpServerPrivate))

static guint tcp_server_signals[LAST_SIGNAL];

INF_DEFINE_ENUM_TYPE(InfdTcpServerStatus, infd_tcp_server_status, infd_tcp_server_status_values)
G_DEFINE_TYPE_WITH_CODE(InfdTcpServer, infd_tcp_server, G_TYPE_OBJECT,
  G_ADD_PRIVATE(InfdTcpServer))

/* TODO: The following functions are merely copied from inf-tcp-connection.c.
 * Probably they should belong into some inf-net-util.c file in
 * libinfinity/common. */

static void
infd_tcp_server_addr_info(InfNativeSocket socket,
                          gboolean local,
                          InfIpAddress** address,
                          guint* port)
{
  union {
    struct sockaddr in_generic;
    struct sockaddr_in in;
    struct sockaddr_in6 in6;
  } native_addr;
  socklen_t len;

  len = sizeof(native_addr);

  if(local == TRUE)
    getsockname(socket, &native_addr.in_generic, &len);
  else
    getpeername(socket, &native_addr.in_generic, &len);

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
}

static void
infd_tcp_server_system_error(InfdTcpServer* server,
                             int code)
{
  GError* error;
  error = NULL;

  inf_native_socket_make_error(code, &error);

  g_signal_emit(G_OBJECT(server), tcp_server_signals[ERROR_], 0, error);
  g_error_free(error);
}

static void
infd_tcp_server_io(InfNativeSocket* socket,
                   InfIoEvent events,
                   gpointer user_data)
{
  InfdTcpServer* server;
  InfdTcpServerPrivate* priv;
  socklen_t len;
  InfNativeSocket new_socket;
  int errcode;
  InfTcpConnection* connection;
  GError* error;

  union {
    struct sockaddr in_generic;
    struct sockaddr_in in;
    struct sockaddr_in6 in6;
  } native_addr;

  InfIpAddress* address;
  guint port;

  server = INFD_TCP_SERVER(user_data);
  priv = INFD_TCP_SERVER_PRIVATE(server);
  g_object_ref(G_OBJECT(server));

  if(events & INF_IO_ERROR)
  {
    len = sizeof(int);
#ifdef G_OS_WIN32
    getsockopt(priv->socket, SOL_SOCKET, SO_ERROR, (char*)&errcode, &len);
#else
    getsockopt(priv->socket, SOL_SOCKET, SO_ERROR, &errcode, &len);
#endif
    /* TODO: Verify that we get senseful error codes here */
    infd_tcp_server_system_error(server, errcode);
  }
  else if(events & INF_IO_INCOMING)
  {
    do
    {
      /* Note that we do not do anything with native_addr and len. This is
       * currently only for debugging purposes since there seemingly was a
       * case where accept returned a valid but socket and errno was set to
       * EAGAIN (which might have been resulted from a previous call however
       * since the errno = 0 line was not there yet before that either).
       * I hope to get some more information this way in case this occurs
       * again. */
#ifndef G_OS_WIN32
      errno = 0;
#endif
      len = sizeof(native_addr);
      new_socket = accept(priv->socket, &native_addr.in_generic, &len);
      errcode = INF_NATIVE_SOCKET_LAST_ERROR;

      if(new_socket == INVALID_SOCKET &&
         errcode != INF_NATIVE_SOCKET_EINTR &&
         errcode != INF_NATIVE_SOCKET_EAGAIN)
      {
        infd_tcp_server_system_error(server, errcode);
      }
      else if(new_socket != INVALID_SOCKET)
      {
        switch(native_addr.in_generic.sa_family)
        {
        case AF_INET:
          address = inf_ip_address_new_raw4(native_addr.in.sin_addr.s_addr);
          port = ntohs(native_addr.in.sin_port);
          break;
        case AF_INET6:
          address = inf_ip_address_new_raw6(native_addr.in6.sin6_addr.s6_addr);
          port = ntohs(native_addr.in6.sin6_port);
          break;
        default:
          g_assert_not_reached();
          break;
        }

        error = NULL;
        connection = _inf_tcp_connection_accepted(
          priv->io,
          new_socket,
          address,
          port,
          &error
        );

        /* _inf_tcp_connection_accepted() takes ownership of address */

        if(connection != NULL)
        {
          g_signal_emit(
            G_OBJECT(server),
            tcp_server_signals[NEW_CONNECTION],
            0,
            connection
          );

          g_object_unref(connection);
        }
        else
        {
          g_signal_emit(
            G_OBJECT(server),
            tcp_server_signals[ERROR_],
            0,
            error
          );

          g_error_free(error);
          closesocket(new_socket);
        }
      }
    } while( (new_socket != INVALID_SOCKET ||
              (new_socket == INVALID_SOCKET &&
               errcode == INF_NATIVE_SOCKET_EINTR)) &&
             (priv->socket != INVALID_SOCKET));
  }

  g_object_unref(G_OBJECT(server));
}

static void
infd_tcp_server_init(InfdTcpServer* server)
{
  InfdTcpServerPrivate* priv;
  priv = INFD_TCP_SERVER_PRIVATE(server);

  priv->io = NULL;

  priv->socket = INVALID_SOCKET;
  priv->status = INFD_TCP_SERVER_CLOSED;

  priv->local_address = NULL;
  priv->local_port = 0;
}

static void
infd_tcp_server_dispose(GObject* object)
{
  InfdTcpServer* server;
  InfdTcpServerPrivate* priv;

  server = INFD_TCP_SERVER(object);
  priv = INFD_TCP_SERVER_PRIVATE(server);

  if(priv->status != INFD_TCP_SERVER_CLOSED)
    infd_tcp_server_close(server);

  if(priv->io  != NULL)
  {
    g_object_unref(G_OBJECT(priv->io));
    priv->io = NULL;
  }

  G_OBJECT_CLASS(infd_tcp_server_parent_class)->dispose(object);
}

static void
infd_tcp_server_finalize(GObject* object)
{
  InfdTcpServer* server;
  InfdTcpServerPrivate* priv;

  server = INFD_TCP_SERVER(object);
  priv = INFD_TCP_SERVER_PRIVATE(server);

  if(priv->local_address != NULL)
    inf_ip_address_free(priv->local_address);

  G_OBJECT_CLASS(infd_tcp_server_parent_class)->finalize(object);
}

static void
infd_tcp_server_set_property(GObject* object,
                             guint prop_id,
                             const GValue* value,
                             GParamSpec* pspec)
{
  InfdTcpServer* server;
  InfdTcpServerPrivate* priv;

  server = INFD_TCP_SERVER(object);
  priv = INFD_TCP_SERVER_PRIVATE(server);

  switch(prop_id)
  {
  case PROP_IO:
    g_assert(priv->status == INFD_TCP_SERVER_CLOSED);
    if(priv->io != NULL) g_object_unref(G_OBJECT(priv->io));
    priv->io = INF_IO(g_value_dup_object(value));
    break;
  case PROP_LOCAL_ADDRESS:
    g_assert(priv->status == INFD_TCP_SERVER_CLOSED);
    if(priv->local_address != NULL)
      inf_ip_address_free(priv->local_address);
    priv->local_address = (InfIpAddress*)g_value_dup_boxed(value);
    break;
  case PROP_LOCAL_PORT:
    g_assert(priv->status == INFD_TCP_SERVER_CLOSED);
    priv->local_port = g_value_get_uint(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infd_tcp_server_get_property(GObject* object,
                             guint prop_id,
                             GValue* value,
                             GParamSpec* pspec)
{
  InfdTcpServer* server;
  InfdTcpServerPrivate* priv;

  server = INFD_TCP_SERVER(object);
  priv = INFD_TCP_SERVER_PRIVATE(server);

  switch(prop_id)
  {
  case PROP_IO:
    g_value_set_object(value, G_OBJECT(priv->io));
    break;
  case PROP_STATUS:
    g_value_set_enum(value, priv->status);
    break;
  case PROP_LOCAL_ADDRESS:
    g_value_set_static_boxed(value, priv->local_address);
    break;
  case PROP_LOCAL_PORT:
    g_value_set_uint(value, priv->local_port);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infd_tcp_server_error(InfdTcpServer* server,
                      GError* error)
{
  InfdTcpServerPrivate* priv;
  priv = INFD_TCP_SERVER_PRIVATE(server);

  if(priv->status == INFD_TCP_SERVER_OPEN)
  {
    g_assert(priv->watch != NULL);
    inf_io_remove_watch(priv->io, priv->watch);
    priv->watch = NULL;
  }

  if(priv->socket != INVALID_SOCKET)
  {
    closesocket(priv->socket);
    priv->socket = INVALID_SOCKET;
  }

  if(priv->status != INFD_TCP_SERVER_CLOSED)
  {
    priv->status = INFD_TCP_SERVER_CLOSED;
    g_object_notify(G_OBJECT(server), "status");
  }
}

static void
infd_tcp_server_class_init(InfdTcpServerClass* tcp_server_class)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(tcp_server_class);

  object_class->dispose = infd_tcp_server_dispose;
  object_class->finalize = infd_tcp_server_finalize;
  object_class->set_property = infd_tcp_server_set_property;
  object_class->get_property = infd_tcp_server_get_property;

  tcp_server_class->new_connection = NULL;
  tcp_server_class->error = infd_tcp_server_error;

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
    PROP_STATUS,
    g_param_spec_enum(
      "status",
      "Status",
      "Status of the TCP server",
      INFD_TYPE_TCP_SERVER_STATUS,
      INFD_TCP_SERVER_CLOSED,
      G_PARAM_READABLE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_LOCAL_ADDRESS,
    g_param_spec_boxed(
      "local-address",
      "Local address",
      "Address to bind to",
      INF_TYPE_IP_ADDRESS,
      G_PARAM_READWRITE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_LOCAL_PORT,
    g_param_spec_uint(
      "local-port",
      "Local port",
      "Port to bind to",
      0,
      65535,
      0,
      G_PARAM_READWRITE
    )
  );

  tcp_server_signals[NEW_CONNECTION] = g_signal_new(
    "new-connection",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfdTcpServerClass, new_connection),
    NULL, NULL,
    inf_marshal_VOID__OBJECT,
    G_TYPE_NONE,
    1,
    INF_TYPE_TCP_CONNECTION
  );

  tcp_server_signals[ERROR_] = g_signal_new(
    "error",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfdTcpServerClass, error),
    NULL, NULL,
    inf_marshal_VOID__POINTER,
    G_TYPE_NONE,
    1,
    G_TYPE_POINTER /* actually a GError* */
  );
}

/**
 * infd_tcp_server_bind:
 * @server: A #InfdTcpServer.
 * @error: Location to store error information, if any.
 *
 * Binds the server to the address and port given by the
 * #InfdTcpServer:local-address and #InfdTcpServer:local-port properties. If
 * the former is %NULL, it will bind on all interfaces on IPv4. If the latter
 * is 0, a random available port will be assigned. If the function fails,
 * %FALSE is returned and an error is set.
 *
 * @server must be in %INFD_TCP_SERVER_CLOSED state for this function to be
 * called.
 *
 * Returns: %TRUE on success, or %FALSE if an error occured.
 */
gboolean
infd_tcp_server_bind(InfdTcpServer* server,
                     GError** error)
{
  InfdTcpServerPrivate* priv;

  union {
    struct sockaddr_in in;
    struct sockaddr_in6 in6;
  } native_address;

  struct sockaddr* addr;
  socklen_t addrlen;

#if !defined(G_OS_WIN32) && defined(HAVE_SO_REUSEADDR)
  int value;
#endif

  g_return_val_if_fail(INFD_IS_TCP_SERVER(server), FALSE);
  priv = INFD_TCP_SERVER_PRIVATE(server);

  g_return_val_if_fail(priv->status == INFD_TCP_SERVER_CLOSED, FALSE);

  if(priv->local_address == NULL)
  {
    priv->socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    addr = (struct sockaddr*)&native_address.in;
    addrlen = sizeof(struct sockaddr_in);

    native_address.in.sin_addr.s_addr = INADDR_ANY;
    native_address.in.sin_family = AF_INET;
    native_address.in.sin_port = htons(priv->local_port);
  }
  else
  {
    switch(inf_ip_address_get_family(priv->local_address))
    {
    case INF_IP_ADDRESS_IPV4:
      priv->socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
      addr = (struct sockaddr*)&native_address.in;
      addrlen = sizeof(struct sockaddr_in);

      memcpy(
        &native_address.in.sin_addr,
        inf_ip_address_get_raw(priv->local_address),
        sizeof(struct in_addr)
      );

      native_address.in.sin_family = AF_INET;
      native_address.in.sin_port = htons(priv->local_port);
      break;
    case INF_IP_ADDRESS_IPV6:
      priv->socket = socket(PF_INET6, SOCK_STREAM, IPPROTO_TCP);
      addr = (struct sockaddr*)&native_address.in6;
      addrlen = sizeof(struct sockaddr_in6);

      memcpy(
        &native_address.in6.sin6_addr,
        inf_ip_address_get_raw(priv->local_address),
        sizeof(struct in6_addr)
      );

      native_address.in6.sin6_family = AF_INET6;
      native_address.in6.sin6_port = htons(priv->local_port);
      native_address.in6.sin6_flowinfo = 0;
      native_address.in6.sin6_scope_id = 0;
      break;
    default:
      g_assert_not_reached();
      break;
    }
  }

  if(priv->socket == INVALID_SOCKET)
  {
    inf_native_socket_make_error(INF_NATIVE_SOCKET_LAST_ERROR, error);
    return FALSE;
  }

#if !defined(G_OS_WIN32) && defined(HAVE_SO_REUSEADDR)
  /* Allow fast restarts of servers by enabling SO_REUSEADDR */
  value = 1;

  if(setsockopt(priv->socket, SOL_SOCKET, SO_REUSEADDR, &value,
      sizeof(int)) == -1)
  {
    inf_native_socket_make_error(INF_NATIVE_SOCKET_LAST_ERROR, error);

    closesocket(priv->socket);
    priv->socket = INVALID_SOCKET;
    return FALSE;
  }
#endif

  if(bind(priv->socket, addr, addrlen) == -1)
  {
    inf_native_socket_make_error(INF_NATIVE_SOCKET_LAST_ERROR, error);

    closesocket(priv->socket);
    priv->socket = INVALID_SOCKET;
    return FALSE;
  }

  g_object_freeze_notify(G_OBJECT(server));

  /* Is assigned a few lines below, but notifications are frozen currently
   * anyway... this saves us a temporary variable here. */
  if(priv->local_port == 0) g_object_notify(G_OBJECT(server), "local-port");

  if(priv->local_address != NULL)
  {
    infd_tcp_server_addr_info(
      priv->socket,
      TRUE,
      NULL,
      &priv->local_port
    );
  }
  else
  {
    infd_tcp_server_addr_info(
      priv->socket,
      TRUE,
      &priv->local_address,
      &priv->local_port
    );

    g_object_notify(G_OBJECT(server), "local-address");
  }

  g_object_notify(G_OBJECT(server), "local-port");

  priv->status = INFD_TCP_SERVER_BOUND;
  g_object_notify(G_OBJECT(server), "status");

  g_object_thaw_notify(G_OBJECT(server));
  return TRUE;
}

/**
 * infd_tcp_server_open:
 * @server: A #InfdTcpServer.
 * @error: Location to store error information.
 *
 * Attempts to open @server. This means binding its local address and port
 * if not already (see infd_tcp_server_bind()) and accepting incoming
 * connections.
 *
 * @server needs to be in %INFD_TCP_SERVER_CLOSED or %INFD_TCP_SERVER_BOUND
 * status for this function to be called. If @server's status is
 * %INFD_TCP_SERVER_CLOSED, then infd_tcp_server_bind() is called before
 * actually opening the server.
 *
 * Returns: %TRUE on success, or %FALSE if an error occured.
 **/
gboolean
infd_tcp_server_open(InfdTcpServer* server,
                     GError** error)
{
  InfdTcpServerPrivate* priv;
  gboolean was_bound;

#ifdef G_OS_WIN32
  u_long argp;
#else
  int result;
#endif

  g_return_val_if_fail(INFD_IS_TCP_SERVER(server), FALSE);
  priv = INFD_TCP_SERVER_PRIVATE(server);

  g_return_val_if_fail(priv->io != NULL, FALSE);
  g_return_val_if_fail(priv->status != INFD_TCP_SERVER_OPEN, FALSE);
  g_assert(priv->watch == NULL);

  g_object_freeze_notify(G_OBJECT(server));

  was_bound = (priv->status != INFD_TCP_SERVER_CLOSED);
  if(!was_bound)
  {
    if(!infd_tcp_server_bind(server, error))
    {
      g_object_thaw_notify(G_OBJECT(server));
      return FALSE;
    }
  }

#ifndef G_OS_WIN32
  result = fcntl(priv->socket, F_GETFL);
  if(result == -1)
  {
    inf_native_socket_make_error(INF_NATIVE_SOCKET_LAST_ERROR, error);
    if(!was_bound)
      infd_tcp_server_close(server);
    g_object_thaw_notify(G_OBJECT(server));
    return FALSE;
  }

  if(fcntl(priv->socket, F_SETFL, result | O_NONBLOCK) == -1)
  {
    inf_native_socket_make_error(INF_NATIVE_SOCKET_LAST_ERROR, error);
    if(!was_bound)
      infd_tcp_server_close(server);
    g_object_thaw_notify(G_OBJECT(server));
    return FALSE;
  }
#else
  argp = 1;
  if(ioctlsocket(priv->socket, FIONBIO, &argp) != 0)
  {
    inf_native_socket_make_error(INF_NATIVE_SOCKET_LAST_ERROR, error);
    if(!was_bound)
      infd_tcp_server_close(server);
    g_object_thaw_notify(G_OBJECT(server));
    return FALSE;
  }
#endif

  if(listen(priv->socket, 5) == -1)
  {
    inf_native_socket_make_error(INF_NATIVE_SOCKET_LAST_ERROR, error);
    if(!was_bound)
      infd_tcp_server_close(server);
    g_object_thaw_notify(G_OBJECT(server));
    return FALSE;
  }

  priv->watch = inf_io_add_watch(
    priv->io,
    &priv->socket,
    INF_IO_INCOMING | INF_IO_ERROR,
    infd_tcp_server_io,
    server,
    NULL
  );

  priv->status = INFD_TCP_SERVER_OPEN;

  g_object_notify(G_OBJECT(server), "status");
  g_object_thaw_notify(G_OBJECT(server));

  return TRUE;
}

/**
 * infd_tcp_server_close:
 * @server: A #InfdTcpServer.
 *
 * Closes a TCP server that is open or bound.
 **/
void
infd_tcp_server_close(InfdTcpServer* server)
{
  InfdTcpServerPrivate* priv;

  g_return_if_fail(INFD_IS_TCP_SERVER(server));

  priv = INFD_TCP_SERVER_PRIVATE(server);
  g_return_if_fail(priv->status != INFD_TCP_SERVER_CLOSED);

  if(priv->status == INFD_TCP_SERVER_OPEN)
  {
    g_assert(priv->watch != NULL);
    inf_io_remove_watch(priv->io, priv->watch);
    priv->watch = NULL;
  }

  closesocket(priv->socket);
  priv->socket = INVALID_SOCKET;

  priv->status = INFD_TCP_SERVER_CLOSED;
  g_object_notify(G_OBJECT(server), "status");
}

/* vim:set et sw=2 ts=2: */
