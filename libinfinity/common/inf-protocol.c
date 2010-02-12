/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2010 Armin Burgmeier <armin@arbur.net>
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
 * SECTION:inf-protocol
 * @short_description: Infinote protocol parameters
 * @include: libinfinity/common/inf-protocol.h
 * @see_also: <link linkend="libinfinity-04-InfError">InfError</link>
 * @stability: Unstable
 *
 * This section defines common protocol parameters used by libinfinity.
 **/

#include <libinfinity/common/inf-protocol.h>
#include <libinfinity/common/inf-error.h>
#include <libinfinity/inf-i18n.h>

#include <stdlib.h>
#include <errno.h>

/**
 * inf_protocol_get_version:
 *
 * Returns the version of the Infinote protocol implemented by this
 * version of libinfinity.
 *
 * Returns: The supported infinote version.
 */
const gchar*
inf_protocol_get_version(void)
{
  return "1.0";
}

/**
 * inf_protocol_parse_version:
 * @version: A version string, such as "1.0"
 * @major: A location to store the major version number to.
 * @minor: A location to store the minor version number to
 * @error: Location to store error information, if any.
 *
 * Splits the given version string into it's major and minor version number.
 * If the string is badly formatted then the function returns %FALSE, @error
 * is set and @major and @minor are left untouched.
 *
 * It is guaranteed that, if @version is inf_protocol_get_version(), the
 * function does not fail.
 *
 * Returns: %TRUE on success, or %FALSE on error.
 */
gboolean
inf_protocol_parse_version(const gchar* version,
                           guint* major,
                           guint* minor,
                           GError** error)
{
  gchar* endptr;
  unsigned long maj;
  unsigned long min;

  errno = 0;
  maj = strtoul(version, &endptr, 10);
  if(errno == ERANGE || maj > (unsigned long)G_MAXUINT)
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_INVALID_NUMBER,
      "%s",
      _("Major part of version number causes overflow")
    );

    return FALSE;
  }

  if(*endptr != '.')
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_INVALID_NUMBER,
      "%s",
      _("Version number parts are not separated by '.'")
    );

    return FALSE;
  }

  errno = 0;
  min = strtoul(endptr+1, &endptr, 10);
  if(errno == ERANGE || min > (unsigned long)G_MAXUINT)
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_INVALID_NUMBER,
      "%s",
      _("Minor part of version number causes overflow")
    );

    return FALSE;
  }

  if(*endptr != '\0')
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_INVALID_NUMBER,
      "%s",
      _("Trailing characters after version number")
    );

    return FALSE;
  }

  if(major) *major = maj;
  if(minor) *minor = min;
  return TRUE;
}

/* vim:set et sw=2 ts=2: */
