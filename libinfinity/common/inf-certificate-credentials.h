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

#ifndef __INF_CERTIFICATE_CREDENTIALS_H__
#define __INF_CERTIFICATE_CREDENTIALS_H__

#include <unistd.h> /* Get ssize_t on MSVC, required by gnutls.h */
#include <gnutls/gnutls.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_CERTIFICATE_CREDENTIALS                (inf_certificate_credentials_get_type())

/**
 * InfCertificateCredentials:
 *
 * #InfCertificateCredentials is an opaque data type. You should only access
 * it via the public API functions.
 */
typedef struct _InfCertificateCredentials InfCertificateCredentials;

GType
inf_certificate_credentials_get_type(void) G_GNUC_CONST;

InfCertificateCredentials*
inf_certificate_credentials_new(void);

InfCertificateCredentials*
inf_certificate_credentials_ref(InfCertificateCredentials* creds);

void
inf_certificate_credentials_unref(InfCertificateCredentials* creds);

gnutls_certificate_credentials_t
inf_certificate_credentials_get(InfCertificateCredentials* creds);

G_END_DECLS

#endif /* __INF_CERTIFICATE_CREDENTIALS_H__ */

/* vim:set et sw=2 ts=2: */
