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
 * SECTION:inf-certificate-credentials
 * @title: InfCertificateCredentials
 * @short_description: Reference-counted wrapper for
 * #gnutls_certificate_credentials_t
 * @include: libinfinity/common/inf-certificate-credentials.h
 * @stability: Unstable
 *
 * This is a thin wrapper class for #gnutls_certificate_credentials_t. It
 * provides reference counting and a boxed GType for it.
 **/

#include <libinfinity/common/inf-certificate-credentials.h>

G_DEFINE_BOXED_TYPE(InfCertificateCredentials, inf_certificate_credentials, inf_certificate_credentials_ref, inf_certificate_credentials_unref)

struct _InfCertificateCredentials {
  guint ref_count;
  gnutls_certificate_credentials_t creds;
};

/**
 * inf_certificate_credentials_new:
 *
 * Creates a new #InfCertificateCredentials with an initial reference count
 * of 1. Use inf_certificate_credentials_get() to access the underlying
 * #gnutls_certificate_credentials_t.
 *
 * Returns: A new #InfCertificateCredentials. Free with
 * inf_certificate_credentials_unref() when no longer needed.
 */
InfCertificateCredentials*
inf_certificate_credentials_new(void)
{
  InfCertificateCredentials* creds;
  creds = g_slice_new(InfCertificateCredentials);

  creds->ref_count = 1;
  gnutls_certificate_allocate_credentials(&creds->creds);

  return creds;
}

/**
 * inf_certificate_credentials_ref:
 * @creds: A #InfCertificateCredentials.
 *
 * Increases the reference count of @creds by 1.
 *
 * Returns: The passed #InfCertificateCredentials, @creds.
 */
InfCertificateCredentials*
inf_certificate_credentials_ref(InfCertificateCredentials* creds)
{
  g_return_val_if_fail(creds != NULL, NULL);
  ++creds->ref_count;
  return creds;
}

/**
 * inf_certificate_credentials_unref:
 * @creds: A #InfCertificateCredentials.
 *
 * Decreases the reference count of @creds by 1. If its reference count
 * reaches 0, then the #InfCertificateCredentials will be freed.
 */
void
inf_certificate_credentials_unref(InfCertificateCredentials* creds)
{
  g_return_if_fail(creds != NULL);
  if(!--creds->ref_count)
  {
    gnutls_certificate_free_credentials(creds->creds);
    g_slice_free(InfCertificateCredentials, creds);
  }
}

/**
 * inf_certificate_credentials_get:
 * @creds: A #InfCertificateCredentials.
 *
 * Provides access to the @creds' underlying
 * #gnutls_certificate_credentials_t.
 *
 * Returns: @creds' #gnutls_certificate_credentials_t.
 */
gnutls_certificate_credentials_t
inf_certificate_credentials_get(InfCertificateCredentials* creds)
{
  g_return_val_if_fail(creds != NULL, NULL);
  return creds->creds;
}

/* vim:set et sw=2 ts=2: */
