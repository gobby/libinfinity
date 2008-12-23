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

#ifndef __INF_CERTIFICATE_CHAIN_H__
#define __INF_CERTIFICATE_CHAIN_H__

#include <gnutls/gnutls.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_CERTIFICATE_CHAIN                 (inf_certificate_chain_get_type())

/**
 * InfCertificateChain:
 *
 * #InfCertifiacteChain is an opaque data type. You should only access it
 * via the public API functions.
 */
typedef struct _InfCertificateChain InfCertificateChain;

GType
inf_certificate_chain_get_type(void) G_GNUC_CONST;

InfCertificateChain*
inf_certificate_chain_new(gnutls_x509_crt_t* certs,
                          guint n_certs);

InfCertificateChain*
inf_certificate_chain_ref(InfCertificateChain* chain);

void
inf_certificate_chain_unref(InfCertificateChain* chain);

gnutls_x509_crt_t*
inf_certificate_chain_get_raw(const InfCertificateChain* chain);

gnutls_x509_crt_t
inf_certificate_chain_get_root_certificate(const InfCertificateChain* chain);

gnutls_x509_crt_t
inf_certificate_chain_get_own_certificate(const InfCertificateChain* chain);

gnutls_x509_crt_t
inf_certificate_chain_get_nth_certificate(const InfCertificateChain* chain,
                                          guint n);

guint
inf_certificate_chain_get_n_certificates(const InfCertificateChain* chain);

G_END_DECLS

#endif /* __INF_CERTIFICATE_CHAIN_H__ */

/* vim:set et sw=2 ts=2: */
