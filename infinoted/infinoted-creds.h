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

#ifndef __INFINOTED_CREDS_H__
#define __INFINOTED_CREDS_H__

#include <glib/gtypes.h>
#include <glib/gerror.h>

#include <gnutls/gnutls.h>

G_BEGIN_DECLS

gnutls_dh_params_t
infinoted_creds_create_dh_params(GError** error);

gnutls_dh_params_t
infinoted_creds_read_dh_params(const gchar* dhparams_path,
                               GError** error);

gboolean
infinoted_creds_write_dh_params(gnutls_dh_params_t params,
                                const gchar* dhparams_path,
                                GError** error);

gnutls_x509_privkey_t
infinoted_creds_create_key(GError** error);

gnutls_x509_privkey_t
infinoted_creds_read_key(const gchar* key_path,
                         GError** error);

gboolean
infinoted_creds_write_key(gnutls_x509_privkey_t key,
                          const gchar* key_path,
                          GError** error);

gnutls_x509_crt_t
infinoted_creds_create_self_signed_certificate(gnutls_x509_privkey_t key,
                                               GError** error);

gnutls_certificate_credentials_t
infinoted_creds_create_credentials(gnutls_dh_params_t dh_params,
                                   gnutls_x509_privkey_t key,
                                   gnutls_x509_crt_t* certs,
                                   guint n_certs,
                                   GError** error);

G_END_DECLS

#endif /* __INFINOTED_CREDS_H__ */

/* vim:set et sw=2 ts=2: */
