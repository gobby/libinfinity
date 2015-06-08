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
 * SECTION:inf-keepalive
 * @title: InfKeepalive
 * @short_description: Platform-independent configuration of TCP keep-alive
 * probes
 * @include: libinfinity/common/inf-keepalive.h
 * @stability: Unstable
 *
 * The functions in this section can be used to enable and configure
 * TCP keepalives in a platform-independent way. This allows to detect
 * inactive connections, and to maintain some activity in case a firewall is
 * dropping the connection after some inactivity.
 *
 * Typically, these functions do not need to be called directly, but the
 * keep-alive settings can be configured with
 * inf_tcp_connection_set_keepalive(), infd_tcp_server_set_keepalive() and
 * inf_discovery_avahi_set_keepalive().
 *
 * The #InfKeepalive structure can be safely allocated on the stack and
 * copied by value.
 */

#include <libinfinity/common/inf-keepalive.h>
#include <libinfinity/inf-define-enum.h>

#if defined(G_OS_WIN32)
# include <in6addr.h>
# include <mstcpip.h>
#elif defined(__linux__)
# include <sys/types.h>
# include <sys/socket.h>
# include <netinet/tcp.h>
# include <stdlib.h>
# include <errno.h>
#else
# warning "Keepalive support not implemented for this platform"
#endif

static const GFlagsValue inf_keepalive_mask_values[] = {
  {
    INF_KEEPALIVE_ENABLED,
    "INF_KEEPALIVE_ENABLED",
    "enabled"
  }, {
    INF_KEEPALIVE_TIME,
    "INF_KEEPALIVE_TIME",
    "time"
  }, {
    INF_KEEPALIVE_INTERVAL,
    "INF_KEEPALIVE_INTERVAL",
    "interval"
  }, {
    INF_KEEPALIVE_ALL,
    "INF_KEEPALIVE_ALL",
    "all"
  }, {
    0,
    NULL,
    NULL
  }
};

INF_DEFINE_FLAGS_TYPE(InfKeepaliveMask, inf_keepalive_mask, inf_keepalive_mask_values)
G_DEFINE_BOXED_TYPE(InfKeepalive, inf_keepalive, inf_keepalive_copy, inf_keepalive_free)

#ifdef G_OS_WIN32
static guint
inf_keepalive_read_registry_dword(HKEY key,
                                  const gchar* name,
                                  guint default_value)
{
  GError* error;
  DWORD out;
  DWORD size;
  LONG result;

  if(key == NULL) return default_value;

  size = sizeof(out);
  result = RegQueryValueEx(key, name, NULL, NULL, (LPBYTE)&out, &size);
  if(result != ERROR_SUCCESS)
  {
    if(result != ERROR_FILE_NOT_FOUND)
    {
      error = NULL;
      inf_native_socket_make_error(result, &error);

      g_warning(
        "Failed to read registry key \"%s\": %s",
        name,
        error->message
      );

      g_error_free(error);
    }

    return default_value;
  }

  return (guint)out;
}

static gboolean
inf_keepalive_apply_win32(const InfKeepalive* keepalive,
                          InfNativeSocket* socket,
                          GError** error)
{
  InfKeepalive resolved;
  struct tcp_keepalive keep;
  DWORD bytes_returned;
  int result;
  int code;

  /* Nothing to do */
  if(keepalive->mask == 0)
    return TRUE;

  /* If we change something, we need to set all values... we cannot set only
   * the time but not the interval, for example. */
  /* Resolve defaults */
  resolved = *keepalive;
  if(~resolved.mask & INF_KEEPALIVE_ALL != 0)
    inf_keepalive_load_default(&resolved, ~resolved.mask & INF_KEEPALIVE_ALL);

  keep.onoff = resolved.enabled;
  keep.keepalivetime = resolved.time * 1000;
  keep.keepaliveinterval = resolved.interval * 1000;

  result = WSAIoctl(
    *socket,
    SIO_KEEPALIVE_VALS,
    &keep,
    sizeof(keep),
    NULL,
    0,
    &bytes_returned,
    NULL,
    NULL
  );

  if(result != 0)
  {
    code = WSAGetLastError();
    inf_native_socket_make_error(code, error);
    return FALSE;
  }

  return TRUE;
}

static void
inf_keepalive_load_default_win32(InfKeepalive* keepalive,
                                 InfKeepaliveMask mask)
{
  HKEY key;
  LONG result;
  GError* error;
  guint regval;

  if(mask & INF_KEEPALIVE_ENABLED)
    keepalive->enabled = FALSE;

  if((mask & (INF_KEEPALIVE_TIME | INF_KEEPALIVE_INTERVAL)) != 0)
  {
    result = RegOpenKeyEx(
      HKEY_LOCAL_MACHINE,
      "SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters",
      0,
      KEY_READ,
      &key
    );

    if(result != ERROR_SUCCESS)
    {
      key = NULL;
      if(result != ERROR_FILE_NOT_FOUND)
      {
        error = NULL;
        inf_native_socket_make_error(result, &error);
        g_warning("Failed to open registry key: %s\n", error->message);
        g_error_free(error);
      }
    }
    
    if(mask & INF_KEEPALIVE_TIME)
    {
      regval = inf_keepalive_read_registry_dword(
        key,
        "KeepAliveTime",
        7200000
      );

      keepalive->time = (regval + 500) / 1000;
    }

    if(mask & INF_KEEPALIVE_INTERVAL)
    {
      regval = inf_keepalive_read_registry_dword(
        key,
        "KeepAliveInterval",
        1000
      );

      keepalive->interval = (regval + 500) / 1000;
    }

    RegCloseKey(key);
  }
}
#endif

#ifdef __linux__
static gboolean
inf_keepalive_read_proc_file(const gchar* filename,
                             guint* out,
                             GError** error)
{
  gchar* contents;
  unsigned long num;
  gchar* end;

  if(!g_file_get_contents(filename, &contents, NULL, error))
    return FALSE;

  errno = 0;
  num = strtoul(contents, &end, 10);

  if(errno != 0)
  {
    inf_native_socket_make_error(errno, error);
    g_free(contents);
    return FALSE;
  }

  if(*end != '\0')
  {
    inf_native_socket_make_error(EDOM, error); /* not a number */
    g_free(contents);
    return FALSE;
  }

  g_free(contents);

  if(num < G_MAXUINT)
  {
    inf_native_socket_make_error(ERANGE, error);
    return FALSE;
  }

  *out = (guint)num;
  return TRUE;
}

static gboolean
inf_keepalive_apply_linux(const InfKeepalive* keepalive,
                          InfNativeSocket* socket,
                          GError** error)
{
  int optval;
  socklen_t len;

  len = sizeof(optval);

  if(keepalive->mask & INF_KEEPALIVE_ENABLED)
  {
    if(keepalive->enabled == TRUE)
      optval = 1;
    else
      optval = 0;

    if(setsockopt(*socket, SOL_SOCKET, SO_KEEPALIVE, &optval, len) != 0)
    {
      inf_native_socket_make_error(errno, error);
      return FALSE;
    }
  }

  if(keepalive->mask & INF_KEEPALIVE_TIME)
  {
    optval = keepalive->time;
    if(setsockopt(*socket, SOL_TCP, TCP_KEEPIDLE, &optval, len) != 0)
    {
      inf_native_socket_make_error(errno, error);
      return FALSE;
    }
  }

  if(keepalive->interval & INF_KEEPALIVE_INTERVAL)
  {
    optval = keepalive->interval;
    if(setsockopt(*socket, SOL_TCP, TCP_KEEPINTVL, &optval, len) != 0)
    {
      inf_native_socket_make_error(errno, error);
      return FALSE;
    }
  }

  return TRUE;
}

static void
inf_keepalive_load_default_linux(InfKeepalive* keepalive,
                                 InfKeepaliveMask mask)
{
  gboolean success;
  GError* error;

  error = NULL;

  if(mask & INF_KEEPALIVE_ENABLED)
    keepalive->enabled = FALSE;

  if(mask & INF_KEEPALIVE_TIME)
  {
    inf_keepalive_read_proc_file(
      "/proc/sys/net/ipv4/tcp_keepalive_time",
      &keepalive->time,
      &error
    );

    if(error != NULL)
    {
      g_warning("Failed to read keepalive time: %s\n", error->message);
      g_error_free(error);
      error = NULL;

      keepalive->time = 7200; /* default system value */
    }
  }

  if(mask & INF_KEEPALIVE_INTERVAL)
  {
    inf_keepalive_read_proc_file(
      "/proc/sys/net/ipv4/tcp_keepalive_intvl",
      &keepalive->time,
      &error
    );

    if(error != NULL)
    {
      g_warning("Failed to read keepalive interval: %s\n", error->message);
      g_error_free(error);
      error = NULL;

      keepalive->interval = 75; /* default system value */
    }
  }
}
#endif

/**
 * inf_keepalive_copy:
 * @keepalive: The #InfKeepalive to copy.
 *
 * Makes a dynamically allocated copy of @keepalive. This is typically not
 * needed, since the structure can be copied by value, but might prove useful
 * for language bindings.
 *
 * Returns: (transfer full): A copy of @keepalive. Free with
 * inf_keepalive_free().
 */
InfKeepalive*
inf_keepalive_copy(const InfKeepalive* keepalive)
{
  InfKeepalive* copy;
  copy = g_slice_new(InfKeepalive);
  *copy = *keepalive;
  return copy;
}

/**
 * inf_keepalive_free:
 * @keepalive: A dynamically allocated #InfKeepalive.
 *
 * Frees a #InfKeepalive obtained with inf_keepalive_copy().
 */
void
inf_keepalive_free(InfKeepalive* keepalive)
{
  g_slice_free(InfKeepalive, keepalive);
}

/**
 * inf_keepalive_apply:
 * @keepalive: A #InfKeepalive.
 * @socket: (in): The socket to which to apply the keepalive settings.
 * @current_mask: The mask of currently applied keepalive settings on the
 * socket, or %INF_KEEPALIVE_ALL if unknown.
 * @error: Location for error information, if any, or %NULL.
 *
 * Sets the keepalive settings of @keepalive for the socket @socket. This
 * function abstracts away the platform-dependent configuration of keepalives.
 *
 * If @current_mask is not %INF_KEEPALIVE_ALL, it can help this function to
 * not do some unneccessary system calls.
 *
 * Returns: %TRUE on success or %FALSE if an error occurred.
 */
gboolean
inf_keepalive_apply(const InfKeepalive* keepalive,
                    InfNativeSocket* socket,
                    InfKeepaliveMask current_mask,
                    GError** error)
{
  InfKeepalive set;
  set = *keepalive;

  /* Load default values for those settings which are switched
   * back to default */
  if((current_mask & ~keepalive->mask) != 0)
    inf_keepalive_load_default(&set, current_mask & ~keepalive->mask);

#if defined(G_OS_WIN32)
  return inf_keepalive_apply_win32(&set, socket, error);
#elif defined(__linux__)
  return inf_keepalive_apply_linux(&set, socket, error);
#else
  g_set_error_literal(
    error,
    g_quark_from_static_string("INF_KEEPALIVE_ERROR"),
    0,
    "Keepalive setting not supported on this platform"
  );

  return FALSE;
#endif
}

/**
 * inf_keepalive_load_default:
 * @keepalive: (inout): A #InfKeepalive.
 * @mask: A mask that specifies which values to obtain.
 *
 * This function attempts to obtain the default keepalive settings from the
 * system. If it cannot obtain the default settings, the documented standard
 * values for the host platform are used.
 *
 * Only the values specified in @mask are obtained, and other fields in
 * @keepalive are left untouched.
 */
void
inf_keepalive_load_default(InfKeepalive* keepalive,
                           InfKeepaliveMask mask)
{
#if defined(G_OS_WIN32)
  inf_keepalive_load_default_win32(keepalive, mask);
#elif defined(__linux__)
  inf_keepalive_load_default_linux(keepalive, mask);
#else
  /* Documented linux default values */
  if(mask & INF_KEEPALIVE_ENABLED)
    keepalive->enabled = FALSE;
  if(mask & INF_KEEPALIVE_TIME)
    keepalive->time = 7200;
  if(mask & INF_KEEPALIVE_INTERVAL)
    keepalive->interval = 75;
#endif
  keepalive->mask |= mask;
}

/* vim:set et sw=2 ts=2: */
