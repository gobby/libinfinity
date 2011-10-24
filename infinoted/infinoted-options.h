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

#ifndef __INFINOTED_OPTIONS_H__
#define __INFINOTED_OPTIONS_H__

#include <libinfinity/common/inf-xmpp-connection.h>
#include <libinfinity/inf-config.h>

#include <glib.h>

G_BEGIN_DECLS

typedef struct _InfinotedOptions InfinotedOptions;
struct _InfinotedOptions {
  gchar* key_file;
  gchar* certificate_file;
  gchar* certificate_chain_file;
  gboolean create_key;
  gboolean create_certificate;
  guint port;
  InfXmppConnectionSecurityPolicy security_policy;
  gchar* root_directory;
  gchar* autosave_hook;
  guint autosave_interval;
  gchar* password;
#ifdef LIBINFINITY_HAVE_PAM
  gchar* pam_service;
  gchar** pam_allowed_users;
  gchar** pam_allowed_groups;
#endif /* LIBINFINITY_HAVE_PAM */

  gchar* sync_directory;
  guint sync_interval;
  gchar* sync_hook;

#ifdef LIBINFINITY_HAVE_LIBDAEMON
  gboolean daemonize;
#endif
};

typedef enum _InfinotedOptionsError {
  INFINOTED_OPTIONS_ERROR_INVALID_SECURITY_POLICY,
  INFINOTED_OPTIONS_ERROR_INVALID_PORT,
  INFINOTED_OPTIONS_ERROR_INVALID_AUTOSAVE_INTERVAL,
  INFINOTED_OPTIONS_ERROR_INVALID_CREATE_OPTIONS,
  INFINOTED_OPTIONS_ERROR_EMPTY_KEY_FILE,
  INFINOTED_OPTIONS_ERROR_EMPTY_CERTIFICATE_FILE,
  INFINOTED_OPTIONS_ERROR_INVALID_SYNC_COMBINATION,
  INFINOTED_OPTIONS_ERROR_INVALID_AUTOSAVE_COMBINATION,
  INFINOTED_OPTIONS_ERROR_INVALID_AUTHENTICATION_SETTINGS
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
