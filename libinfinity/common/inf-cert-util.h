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

#ifndef __INF_CERT_UTIL_H__
#define __INF_CERT_UTIL_H__

#include <glib.h>

#include <gnutls/gnutls.h>

G_BEGIN_DECLS

int
inf_cert_util_copy(gnutls_x509_crt_t* dest,
                   gnutls_x509_crt_t src);

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
