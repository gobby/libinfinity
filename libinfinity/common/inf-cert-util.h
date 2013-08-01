/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2013 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INF_CERT_UTIL_H__
#define __INF_CERT_UTIL_H__

#include <glib.h>

#include <unistd.h> /* Get ssize_t on MSVC, required by gnutls.h */
#include <gnutls/gnutls.h>

G_BEGIN_DECLS

gnutls_dh_params_t
inf_cert_util_create_dh_params(GError** error);

gnutls_dh_params_t
inf_cert_util_read_dh_params(const gchar* filename,
                             GError** error);

gboolean
inf_cert_util_write_dh_params(gnutls_dh_params_t params,
                              const gchar* filename,
                              GError** error);

gnutls_x509_privkey_t
inf_cert_util_create_private_key(gnutls_pk_algorithm_t algo,
                                 unsigned int bits,
                                 GError** error);

gnutls_x509_privkey_t
inf_cert_util_read_private_key(const gchar* filename,
                               GError** error);

gboolean
inf_cert_util_write_private_key(gnutls_x509_privkey_t key,
                                const gchar* filename,
                                GError** error);

gnutls_x509_crt_t
inf_cert_util_create_self_signed_certificate(gnutls_x509_privkey_t key,
                                             GError** error);

GPtrArray*
inf_cert_util_read_certificate(const gchar* filename,
                               GPtrArray* current,
                               GError** error);

gboolean
inf_cert_util_write_certificate(gnutls_x509_crt_t* certs,
                                guint n_certs,
                                const gchar* filename,
                                GError** error);

gnutls_x509_crt_t
inf_cert_util_copy_certificate(gnutls_x509_crt_t src,
                               GError** error);

gchar*
inf_cert_util_get_dn_by_oid(gnutls_x509_crt_t cert,
                            const char* oid,
                            unsigned int index);

gchar*
inf_cert_util_get_issuer_dn_by_oid(gnutls_x509_crt_t crt,
                                   const char* oid,
                                   unsigned int index);

gchar*
inf_cert_util_get_hostname(gnutls_x509_crt_t cert);

gchar*
inf_cert_util_get_serial_number(gnutls_x509_crt_t cert);

gchar*
inf_cert_util_get_fingerprint(gnutls_x509_crt_t cert,
                              gnutls_digest_algorithm_t algo);

gchar*
inf_cert_util_get_activation_time(gnutls_x509_crt_t cert);

gchar*
inf_cert_util_get_expiration_time(gnutls_x509_crt_t cert);

G_END_DECLS

#endif /* __INF_CERT_UTIL_H__ */

/* vim:set et sw=2 ts=2: */
