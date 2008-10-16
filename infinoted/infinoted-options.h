/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INFINOTED_OPTIONS_H__
#define __INFINOTED_OPTIONS_H__

#include <libinfinity/common/inf-xmpp-connection.h>

#include <glib.h>

G_BEGIN_DECLS

typedef struct _InfinotedOptions InfinotedOptions;
struct _InfinotedOptions {
  gchar* key_file;
  gchar* certificate_file;
  gboolean create_key;
  gboolean create_certificate;
  guint port;
  InfXmppConnectionSecurityPolicy security_policy;
  gchar* root_directory;
};

typedef enum _InfinotedOptionsError {
  INFINOTED_OPTIONS_ERROR_INVALID_SECURITY_POLICY,
  INFINOTED_OPTIONS_ERROR_INVALID_PORT,
  INFINOTED_OPTIONS_ERROR_INVALID_CREATE_OPTIONS,
  INFINOTED_OPTIONS_ERROR_EMPTY_KEY_FILE,
  INFINOTED_OPTIONS_ERROR_EMPTY_CERTIFICATE_FILE
} InfinotedOptionsError;

InfinotedOptions*
infinoted_options_new(const gchar* const* config_files,
                      int* argc,
                      char*** argv,
                      GError** error);

void
infinoted_options_free(InfinotedOptions* options);

GQuark
infinoted_options_error_quark(void);

G_END_DECLS

#endif /* __INFINOTED_OPTIONS_H__ */

/* vim:set et sw=2 ts=2: */
