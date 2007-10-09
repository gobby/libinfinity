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

#include <infinoted/infinoted-creds.h>

#include <glib/gfileutils.h>
#include <glib/gutils.h>
#include <glib/gmem.h>
#include <glib/gmessages.h>

#include <string.h>
#include <errno.h>

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

/** infinoted_creds_read_key:
 *
 * @key_path: A path to a X.509 private key file
 * @error: Location for error information, if any.
 *
 * Reads the key located at @key_path into a gnutls_x509_privkey_t
 * structure.
 *
 * Return Value: A private key. Free with gnutls_x509_privkey_deinit().
 **/
gnutls_x509_privkey_t
infinoted_creds_read_key(const gchar* key_path,
                         GError** error)
{
  gnutls_x509_privkey_t key;
  gnutls_datum_t key_datum;
  gchar* key_data;
  gsize key_size;
  int res;

  if(g_file_get_contents(key_path, &key_data, &key_size, error) == FALSE)
    return NULL;

  res = gnutls_x509_privkey_init(&key);
  if(res != 0)
  {
    infinoted_creds_gnutls_error(res, error);
    g_free(key_data);
    return NULL;
  }
  
  key_datum.data = (unsigned char*)key_data;
  key_datum.size = key_size;

  res = gnutls_x509_privkey_import(key, &key_datum, GNUTLS_X509_FMT_PEM);
  g_free(key_data);

  if(res != 0)
  {
    infinoted_creds_gnutls_error(res, error);
    gnutls_x509_privkey_deinit(key);
    return NULL;
  }

  return key;
}

/** infinoted_creds_create_key:
 *
 * @key_path: A path to store the generated key at, or %NULL.
 * @error: Location to store error information, if any.
 *
 * Generates a new, random X.509 private key. The created key is stored at
 * @key_path, if @key_path is non-%NULL. Otherwise, the new key is just
 * returned.
 *
 * Return Value: A new key to be freed with gnutls_x509_privkey_deinit(),
 * or %NULL if an error occured.
 **/
gnutls_x509_privkey_t
infinoted_creds_create_key(const gchar* key_path,
                           GError** error)
{
  gnutls_x509_privkey_t key;
  gchar* key_data;
  size_t output_size;
  gchar* dirname;
  int save_errno;
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

  if(key_path != NULL)
  {
    dirname = g_path_get_dirname(key_path);
    res = g_mkdir_with_parents(dirname, 0700);
    save_errno = errno;

    if(res != 0)
    {
      gnutls_x509_privkey_deinit(key);

      g_set_error(
        error,
        g_quark_from_static_string("ERRNO_ERROR"),
        save_errno,
        "Could not create directory `%s': %s",
        dirname,
        strerror(save_errno)
      );

      g_free(dirname);
      return NULL;
    }

    g_free(dirname);

    output_size = 0;

    res = gnutls_x509_privkey_export(
      key,
      GNUTLS_X509_FMT_PEM,
      NULL,
      &output_size
    );

    g_assert(res != 0); /* cannot succeed */
    if(res != GNUTLS_E_SHORT_MEMORY_BUFFER)
    {
      /* Some real error */
      infinoted_creds_gnutls_error(res, error);
      gnutls_x509_privkey_deinit(key);
      return NULL;
    }

    key_data = g_malloc(output_size);
    res = gnutls_x509_privkey_export(
      key,
      GNUTLS_X509_FMT_PEM,
      key_data,
      &output_size
    );

    if(res != 0)
    {
      infinoted_creds_gnutls_error(res, error);
      g_free(key_data);
      gnutls_x509_privkey_deinit(key);
      return NULL;
    }

    if(g_file_set_contents(key_path, key_data, output_size, error) == FALSE)
    {
      g_free(key_data);
      gnutls_x509_privkey_deinit(key);
      return NULL;
    }

    g_free(key_data);
  }

  return key;
}

/** infinoted_creds_read_certificate:
 *
 * @cert_path: A path to a X.509 certificate file.
 * @error: Location to store error information, if any.
 *
 * Reads the certificate at @cert_path into a gnutls_x509_crt_t structure.
 *
 * Return Value: A certificate to be freed with gnutls_x509_crt_deinit(),
 * or %NULL on error.
 **/
gnutls_x509_crt_t
infinoted_creds_read_certificate(const gchar* cert_path,
                                 GError** error)
{
  gnutls_x509_crt_t cert;
  gnutls_datum_t cert_datum;
  gchar* cert_data;
  gsize cert_size;
  int res;

  if(g_file_get_contents(cert_path, &cert_data, &cert_size, error) == FALSE)
    return NULL;

  res = gnutls_x509_crt_init(&cert);
  if(res != 0)
  {
    infinoted_creds_gnutls_error(res, error);
    g_free(cert_data);
    return NULL;
  }

  cert_datum.data = (unsigned char*)cert_data;
  cert_datum.size = cert_size;

  res = gnutls_x509_crt_import(cert, &cert_datum, GNUTLS_X509_FMT_PEM);
  g_free(cert_data);

  if(res != 0)
  {
    infinoted_creds_gnutls_error(res, error);
    gnutls_x509_crt_deinit(cert);
    return NULL;
  }

  return cert;
}

/** infinoted_creds_create_self_signed_certificate:
 *
 * @key: The key with which to sign the certificate.
 * @cert_path: A path at which to store the generated certificate.
 * @error: Location to store error information, if any.
 *
 * Creates an new self-signed X.509 certificate signed with @key.
 * If @cert_path is non-%NULL, the new certificate is stored at this path,
 * otherwise it is just returned.
 *
 * Return Value: A certificate to be freed with gnutls_x509_crt_deinit(),
 * or %NULL on error.
 **/
gnutls_x509_crt_t
ininoted_creds_create_self_signed_certificate(gnutls_x509_privkey_t key,
                                              const gchar* cert_path,
                                              GError** error)
{
  gnutls_x509_crt_t cert;
  gint32 default_serial;
  char buffer[20];
  gchar* cert_data;
  gchar* dirname;
  size_t output_size;
  int save_errno;
  int res;

  cert = NULL;
  cert_data = NULL;

  res = gnutls_x509_crt_init(&cert);
  if(res != 0) goto error;
  
  res = gnutls_x509_crt_set_key(cert, key);
  if(res != 0) goto error;

  default_serial = (gint32)time(NULL);
  buffer[4] = (default_serial      ) & 0xff;
  buffer[3] = (default_serial >>  8) & 0xff;
  buffer[2] = (default_serial >> 16) & 0xff;
  buffer[1] = (default_serial >> 24) & 0xff;
  buffer[0] = 0;

  res = gnutls_x509_crt_set_serial(cert, buffer, 5);
  if(res != 0) goto error;

  res = gnutls_x509_crt_set_activation_time(cert, time(NULL));
  if(res != 0) goto error;

  res = gnutls_x509_crt_set_expiration_time(cert, time(NULL) + 365 * DAYS);
  if(res != 0) goto error;

  res = gnutls_x509_crt_set_basic_constraints(cert, 0, -1);
  if(res != 0) goto error;

  res = gnutls_x509_crt_set_key_usage(cert, GNUTLS_KEY_DIGITAL_SIGNATURE);
  if(res != 0) goto error;

  res = gnutls_x509_crt_set_version(cert, 3);
  if(res != 0) goto error;

  res = gnutls_x509_crt_sign2(cert, cert, key, GNUTLS_DIG_SHA1, 0);
  if(res != 0) goto error;

  if(cert_path != NULL)
  {
    dirname = g_path_get_dirname(cert_path);
    res = g_mkdir_with_parents(dirname, 0700);
    save_errno = errno;

    if(res != 0) goto gerror;
    g_free(dirname);

    output_size = 0;
    res = gnutls_x509_crt_export(
      cert,
      GNUTLS_X509_FMT_PEM,
      NULL,
      &output_size
    );

    g_assert(res != 0); /* cannot succeed */
    if(res != GNUTLS_E_SHORT_MEMORY_BUFFER)
      goto error; /* real error */

    cert_data = g_malloc(output_size);
    res = gnutls_x509_crt_export(
      cert,
      GNUTLS_X509_FMT_PEM,
      cert_data,
      &output_size
    );

    if(res != 0) goto error;

    if(g_file_set_contents(cert_path, cert_data, output_size, error) == FALSE)
      goto error;

    g_free(cert_data);
  }

  return cert;

error:
  if(cert_data != NULL) g_free(cert_data);
  if(cert != NULL) gnutls_x509_crt_deinit(cert);
  infinoted_creds_gnutls_error(res, error);
  return NULL;

gerror:
  if(cert_data != NULL) g_free(cert_data);
  if(cert != NULL) gnutls_x509_crt_deinit(cert);

  g_set_error(
    error,
    g_quark_from_static_string("ERRNO_ERROR"),
    save_errno,
    "Could not create directory `%s': %s",
    dirname,
    strerror(save_errno)
  );

  g_free(dirname);
  return NULL;
}

/* vim:set et sw=2 ts=2: */
