/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008, 2009 Armin Burgmeier <armin@arbur.net>
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

#include <infinoted/infinoted-creds.h>

#include <glib.h>

#include <gnutls/x509.h>

#include <string.h>
#include <errno.h>
#include <time.h>

static const unsigned int DAYS = 24 * 60 * 60;

static void
infinoted_creds_gnutls_error(int gnutls_error_code,
                             GError** error)
{
  g_set_error(
    error,
    g_quark_from_static_string("GNUTLS_ERROR"),
    gnutls_error_code,
    "%s",
    gnutls_strerror(gnutls_error_code)
  );
}

static int
infinoted_creds_create_self_signed_certificate_impl(gnutls_x509_crt_t cert,
                                                    gnutls_x509_privkey_t key)
{
  gint32 default_serial;
  char buffer[20];
  const gchar* hostname;
  int res;
  
  res = gnutls_x509_crt_set_key(cert, key);
  if(res != 0) return res;

  default_serial = (gint32)time(NULL);
  buffer[4] = (default_serial      ) & 0xff;
  buffer[3] = (default_serial >>  8) & 0xff;
  buffer[2] = (default_serial >> 16) & 0xff;
  buffer[1] = (default_serial >> 24) & 0xff;
  buffer[0] = 0;

  res = gnutls_x509_crt_set_serial(cert, buffer, 5);
  if(res != 0) return res;

  res = gnutls_x509_crt_set_activation_time(cert, time(NULL));
  if(res != 0) return res;

  res = gnutls_x509_crt_set_expiration_time(cert, time(NULL) + 365 * DAYS);
  if(res != 0) return res;

  res = gnutls_x509_crt_set_basic_constraints(cert, 0, -1);
  if(res != 0) return res;

  res = gnutls_x509_crt_set_key_usage(cert, GNUTLS_KEY_DIGITAL_SIGNATURE);
  if(res != 0) return res;

  res = gnutls_x509_crt_set_version(cert, 3);
  if(res != 0) return res;

  hostname = g_get_host_name();
  res = gnutls_x509_crt_set_dn_by_oid(
    cert,
    GNUTLS_OID_X520_COMMON_NAME,
    0,
    hostname,
    strlen(hostname)
  );
  if(res != 0) return res;

#if 0
  /* TODO: We set the alternative name always to hostname.local, because this
   * is what avahi yields when resolving that host. However, we rather should
   * find out the real DNS name, perhaps by doing a reverse DNS resolve
   * for 127.0.0.1? */
  dnsname = g_strdup_printf("%s.local", hostname);
  res = gnutls_x509_crt_set_subject_alternative_name(
    cert,
    GNUTLS_SAN_DNSNAME,
    dnsname
  );

  g_free(dnsname);
  if(res != 0) return res;
#else
  res = gnutls_x509_crt_set_subject_alternative_name(
    cert,
    GNUTLS_SAN_DNSNAME,
    g_get_host_name()
  );

  if(res != 0) return res;
#endif

  res = gnutls_x509_crt_sign2(cert, cert, key, GNUTLS_DIG_SHA1, 0);
  if(res != 0) return res;

  return 0;
}

#define READ_FUNC_IMPL(type, path, init_func, deinit_func, import_func) \
  type obj; \
  gnutls_datum_t datum; \
  gchar* data; \
  gsize size; \
  int res; \
  \
  obj = NULL; \
  \
  if(g_file_get_contents(path, &data, &size, error) == FALSE) \
    return NULL; \
  \
  res = init_func(&obj); \
  if(res != 0) goto error; \
  \
  datum.data = (unsigned char*)data; \
  datum.size = (unsigned int)size; \
  res = import_func(obj, &datum, GNUTLS_X509_FMT_PEM); \
  if(res != 0) goto error; \
  \
  return obj; \
error: \
  if(obj != NULL) deinit_func(obj); \
  infinoted_creds_gnutls_error(res, error); \
  return NULL;

#define WRITE_FUNC_IMPL(obj, path, export_func) \
  unsigned char* data; \
  size_t size; \
  int res; \
  gboolean bres; \
  \
  size = 0; \
  data = NULL; \
  \
  res = export_func(obj, GNUTLS_X509_FMT_PEM, NULL, &size); \
  g_assert(res != 0); /* cannot succeed */ \
  if(res != GNUTLS_E_SHORT_MEMORY_BUFFER) \
    goto error; /* real error */ \
  \
  data = g_malloc(size); \
  res = export_func(obj, GNUTLS_X509_FMT_PEM, data, &size); \
  if(res != 0) goto error; \
  \
  bres = g_file_set_contents(path, (gchar*)data, size, error); \
  g_free(data); \
  \
  return bres; \
error: \
  if(data != NULL) g_free(data); \
  infinoted_creds_gnutls_error(res, error); \
  return FALSE;

/**
 * infinoted_creds_create_dh_params:
 * @error: Loctation to store error information, if any.
 *
 * Creates new, random Diffie-Hellman parameters.
 *
 * Returns: New dhparams to be freed with gnutls_dh_params_deinit(),
 * or %NULL in case of error.
 **/
gnutls_dh_params_t
infinoted_creds_create_dh_params(GError** error)
{
  gnutls_dh_params_t params;
  int res;

  params = NULL;
  res = gnutls_dh_params_init(&params);
    
  if(res != 0)
  {
    infinoted_creds_gnutls_error(res, error);
    return NULL;
  }

  res = gnutls_dh_params_generate2(params, 2048);
  if(res != 0)
  {
    gnutls_dh_params_deinit(params);
    infinoted_creds_gnutls_error(res, error);
    return NULL;
  }

  return params;
}

/**
 * infinoted_creds_read_dh_params:
 * @dhparams_path: A path to a DH parameters file.
 * @error: Location to store error information, if any.
 *
 * Reads the Diffie-Hellman parameters located at @dhparams_path into a
 * gnutls_dh_params_t structure.
 *
 * Returns: New dhparams to be freed with gnutls_dh_params_deinit(),
 * or %NULL in case of error.
 **/
gnutls_dh_params_t
infinoted_creds_read_dh_params(const gchar* dhparams_path,
                               GError** error)
{
  READ_FUNC_IMPL(
    gnutls_dh_params_t,
    dhparams_path,
    gnutls_dh_params_init,
    gnutls_dh_params_deinit,
    gnutls_dh_params_import_pkcs3
  )
}

/**
 * infinoted_creds_write_dh_params:
 * @params: An initialized #gnutls_dh_params_t structure.
 * @dhparams_path: The path at which so store @params.
 * @error: Location to store error information, if any.
 *
 * Writes the given Diffie-Hellman parameters to the given path on the
 * filesystem. If an error occurs, @error is set and %FALSE is returned.
 *
 * Returns: %TRUE on success, %FALSE otherwise.
 **/
gboolean
infinoted_creds_write_dh_params(gnutls_dh_params_t params,
                                const gchar* dhparams_path,
                                GError** error)
{
  WRITE_FUNC_IMPL(
    params,
    dhparams_path,
    gnutls_dh_params_export_pkcs3
  )
}

/**
 * infinoted_creds_create_key:
 * @error: Location to store error information, if any.
 *
 * Generates a new, random X.509 private key.
 *
 * Returns: A new key to be freed with gnutls_x509_privkey_deinit(),
 * or %NULL if an error occured.
 **/
gnutls_x509_privkey_t
infinoted_creds_create_key(GError** error)
{
  gnutls_x509_privkey_t key;
  int res;

  res = gnutls_x509_privkey_init(&key);
  if(res != 0)
  {
    infinoted_creds_gnutls_error(res, error);
    return NULL;
  }

  res = gnutls_x509_privkey_generate(key, GNUTLS_PK_RSA, 2048, 0);
  if(res != 0)
  {
    infinoted_creds_gnutls_error(res, error);
    gnutls_x509_privkey_deinit(key);
    return NULL;
  }

  return key;
}


/**
 * infinoted_creds_read_key:
 * @key_path: A path to a X.509 private key file
 * @error: Location for error information, if any.
 *
 * Reads the key located at @key_path into a gnutls_x509_privkey_t
 * structure.
 *
 * Returns: A private key. Free with gnutls_x509_privkey_deinit().
 **/
gnutls_x509_privkey_t
infinoted_creds_read_key(const gchar* key_path,
                         GError** error)
{
  READ_FUNC_IMPL(
    gnutls_x509_privkey_t,
    key_path,
    gnutls_x509_privkey_init,
    gnutls_x509_privkey_deinit,
    gnutls_x509_privkey_import
  )
}

/**
 * infinoted_creds_write_key:
 * @key: An initialized #gnutls_x509_privkey_t structure.
 * @key_path: The path at which so store the key.
 * @error: Location to store error information, if any.
 *
 * Writes @key to the location specified by @key_path on the filesystem.
 * If an error occurs, the function returns %FALSE and @error is set.
 *
 * Returns: %TRUE on success, %FALSE otherwise.
 **/
gboolean
infinoted_creds_write_key(gnutls_x509_privkey_t key,
                          const gchar* key_path,
                          GError** error)
{
  WRITE_FUNC_IMPL(
    key,
    key_path,
    gnutls_x509_privkey_export
  )
}

/**
 * infinoted_creds_create_self_signed_certificate:
 * @key: The key with which to sign the certificate.
 * @error: Location to store error information, if any.
 *
 * Creates an new self-signed X.509 certificate signed with @key.
 *
 * Returns: A certificate to be freed with gnutls_x509_crt_deinit(),
 * or %NULL on error.
 **/
gnutls_x509_crt_t
infinoted_creds_create_self_signed_certificate(gnutls_x509_privkey_t key,
                                               GError** error)
{
  gnutls_x509_crt_t cert;
  int res;

  res = gnutls_x509_crt_init(&cert);
  if(res != 0)
  {
    infinoted_creds_gnutls_error(res, error);
    return NULL;
  }

  res = infinoted_creds_create_self_signed_certificate_impl(cert, key);
  if(res != 0)
  {
    gnutls_x509_crt_deinit(cert);
    infinoted_creds_gnutls_error(res, error);
    return NULL;
  }

  return cert;
}

/**
 * infinoted_creds_create_credentials:
 * @dh_params: Diffie-Hellman parameters for key exchange.
 * @key: The X.509 private key to use.
 * @certs: An array of X.509 certificates to use. The first certificate is the
 * server's certificate, the second the issuer's, the third the issuer's
 * issuer's, etc.
 * @n_certs: Number of certificates in @certs.
 * @error: Location to store error information, if any.
 *
 * Creates a new #gnutls_certificate_credentials_t struture suitable for
 * TLS.
 *
 * Return Value: A #gnutls_certificate_credentials_t, to be freed
 * with gnutls_certificate_free_credentials().
 **/
gnutls_certificate_credentials_t
infinoted_creds_create_credentials(gnutls_dh_params_t dh_params,
                                   gnutls_x509_privkey_t key,
                                   gnutls_x509_crt_t* certs,
                                   guint n_certs,
                                   GError** error)
{
  gnutls_certificate_credentials_t creds;
  int res;

  res = gnutls_certificate_allocate_credentials(&creds);
  if(res != 0)
  {
    infinoted_creds_gnutls_error(res, error);
    return NULL;
  }

  res = gnutls_certificate_set_x509_key(creds, certs, n_certs, key);
  if(res != 0)
  {
    gnutls_certificate_free_credentials(creds);
    infinoted_creds_gnutls_error(res, error);
    return NULL;
  }

  gnutls_certificate_set_dh_params(creds, dh_params);
  return creds;
}

/* vim:set et sw=2 ts=2: */
