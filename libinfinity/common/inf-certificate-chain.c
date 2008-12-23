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

/**
 * SECTION:inf-certificate-chain
 * @title: InfCertificateChain
 * @short_description: X.509 certificate chains
 * @see_also: #InfXmppConnection
 * @include: libinfinity/common/inf-certificate-chain.h
 * @stability: Unstable
 *
 * #InfCertificateChain is a reference-counted wrapper around an array of 
 * gnutls_x509_crt_t structures, representing a certificate chain.
 **/

#include <libinfinity/common/inf-certificate-chain.h>

#include <gnutls/x509.h>

struct _InfCertificateChain {
  guint ref_count;

  gnutls_x509_crt_t* certs;
  guint n_certs;
};

GType
inf_certificate_chain_get_type(void)
{
  static GType certificate_chain_type = 0;

  if(!certificate_chain_type)
  {
    certificate_chain_type = g_boxed_type_register_static(
      "InfCertificateChain",
      (GBoxedCopyFunc)inf_certificate_chain_ref,
      (GBoxedFreeFunc)inf_certificate_chain_unref
    );
  }

  return certificate_chain_type;
}

/**
 * inf_certificate_chain_new:
 * @certs: Array of certificates.
 * @n_certs: Number of elements in @certs.
 *
 * Creates a new #InfCertificateChain with the given certificates. The @certs
 * array needs to be allocated with g_malloc. This function takes ownership
 * of @certs.
 *
 * Return Value: A new #InfCertificateChain.
 **/
InfCertificateChain*
inf_certificate_chain_new(gnutls_x509_crt_t* certs,
                          guint n_certs)
{
  InfCertificateChain* chain;
  chain = g_slice_new(InfCertificateChain);
  chain->ref_count = 1;
  chain->certs = certs;
  chain->n_certs = n_certs;
  return chain;
}

/**
 * inf_certificate_chain_ref:
 * @chain: A #InfCertificateChain:
 *
 * Increases the reference count of @chain by one.
 *
 * Returns: The same @chain.
 */
InfCertificateChain*
inf_certificate_chain_ref(InfCertificateChain* chain)
{
  ++ chain->ref_count;
  return chain;
}

/**
 * inf_certificate_chain_unref:
 * @chain: A #InfCertificateChain.
 *
 * Decreases the reference count of @chain by one. If the reference count
 * reaches zero, then @chain is freed.
 */
void
inf_certificate_chain_unref(InfCertificateChain* chain)
{
  guint i;

  -- chain->ref_count;
  if(chain->ref_count == 0)
  {
    for(i = 0; i < chain->n_certs; ++ i)
      gnutls_x509_crt_deinit(chain->certs[i]);
    g_free(chain->certs);
    g_slice_free(InfCertificateChain, chain);
  }
}

/**
 * inf_certificate_chain_get_raw:
 * @chain: A #InfCertificateChain.
 *
 * Returns the raw array of certificates in the chain.
 *
 * Returns: An array of certificates owned by the chain.
 */
gnutls_x509_crt_t*
inf_certificate_chain_get_raw(const InfCertificateChain* chain)
{
  return chain->certs;
}

/**
 * inf_certificate_chain_get_root_certificate:
 * @chain: A #InfCertificateChain.
 *
 * Returns the last certificate in the chain.
 *
 * Returns: The last certificate in the chain.
 */
gnutls_x509_crt_t
inf_certificate_chain_get_root_certificate(const InfCertificateChain* chain)
{
  return chain->certs[chain->n_certs - 1];
}

/**
 * inf_certificate_chain_get_own_certificate:
 * @chain: A #InfCertificateChain.
 *
 * TODO: Rename this function into something more appropriate.
 *
 * Returns the first certificate in the chain.
 *
 * Returns: The first certificate in the chain.
 */
gnutls_x509_crt_t
inf_certificate_chain_get_own_certificate(const InfCertificateChain* chain)
{
  return chain->certs[0];
}

/**
 * inf_certificate_chain_get_nth_certificate:
 * @chain: A #InfCertificateChain.
 * @n: Index of the certificate to retrieve.
 *
 * Returns the @n<!-- -->th certificate in the chain.
 *
 * Returns: The nth certificate in the chain.
 */
gnutls_x509_crt_t
inf_certificate_chain_get_nth_certificate(const InfCertificateChain* chain,
                                          guint n)
{
  g_return_val_if_fail(n < chain->n_certs, NULL);
  return chain->certs[n];
}

/**
 * inf_certificate_get_n_certificates:
 * @chain: A #InfCertificateChain.
 *
 * Returns the number of certificates in @chain.
 *
 * Returns: The number of certificates in @chain.
 */
guint
inf_certificate_chain_get_n_certificates(const InfCertificateChain* chain)
{
  return chain->n_certs;
}
