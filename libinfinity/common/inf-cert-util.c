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

/**
 * SECTION:inf-cert-util
 * @title: Certificate utility functions
 * @short_description: Helper functions to read and write information from
 * X.509 certificates.
 * @include: libinfinity/common/inf-cert-util.h
 * @stability: Unstable
 *
 * These functions are utility functions that can be used when dealing with
 * certificates, private key and Diffie-Hellman parameters for key exchange.
 * The functionality these functions provide include creating, reading and
 * writing these data structures to disk in PEM format, or to read values from
 * certificates.
 **/

#include <libinfinity/common/inf-cert-util.h>
#include <libinfinity/common/inf-error.h>

#include <glib/gstdio.h>

#include <gnutls/x509.h>

#include <string.h>

#define X509_BEGIN_1 "-----BEGIN CERTIFICATE-----"
#define X509_BEGIN_2 "-----BEGIN X509 CERTIFICATE-----"

#define X509_END_1   "-----END CERTIFICATE-----"
#define X509_END_2   "-----END X509 CERTIFICATE----"

/*
 * Helper functions
 */

static const unsigned int DAYS = 24 * 60 * 60;

static int
inf_cert_util_create_self_signed_certificate_impl(gnutls_x509_crt_t cert,
                                                  gnutls_x509_privkey_t key)
{
  gint32 default_serial;
  char buffer[5];
  time_t timestamp;
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

  timestamp = time(NULL);

  res = gnutls_x509_crt_set_activation_time(cert, timestamp);
  if(res != 0) return res;

  res = gnutls_x509_crt_set_expiration_time(cert, timestamp + 365 * DAYS);
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

  res = gnutls_x509_crt_set_subject_alternative_name(
    cert,
    GNUTLS_SAN_DNSNAME,
    hostname
  );
  if(res != 0) return res;

  res = gnutls_x509_crt_sign2(cert, cert, key, GNUTLS_DIG_SHA1, 0);
  if(res != 0) return res;

  return 0;
}

static gchar*
inf_cert_util_format_time(time_t time)
{
  struct tm* tm;
  gsize alloc;
  gchar* result;
  gchar* converted;
  size_t ret;

  tm = localtime(&time);

  alloc = 128;
  result = NULL;

  do
  {
    result = g_realloc(result, alloc);
    ret = strftime(result, alloc, "%c", tm);
    alloc *= 2;
  } while(ret == 0);

  converted = g_locale_to_utf8(result, -1, NULL, NULL, NULL);
  g_free(result);

  /* The conversion sequence should be always valid, otherwise strftime
   * is screwed!? */
  g_assert(converted != NULL);
  return converted;
}

static gchar*
inf_cert_util_format_hexadecimal(const guchar* data,
                                 gsize size)
{
  gchar* formatted;
  gchar* cur;
  gsize i;

  formatted = g_malloc(3 * size);
  cur = formatted;

  for(i = 0; i < size; ++ i)
  {
    g_sprintf(cur, "%.2X", (unsigned int)data[i]);
    cur[2] = ':';
    cur += 3;
  }

  cur[-1] = '\0';
  return formatted;
}

static void
inf_cert_util_free_array(GPtrArray* array,
                         GPtrArray* current,
                         guint current_len)
{
  /* If current is given, then free all entries from current, keeping only
   * the first current_len entries. If not, free array completely. */
  guint i;
  if(current != NULL)
  {
    for(i = current_len; i < current->len; ++ i)
      gnutls_x509_crt_deinit((gnutls_x509_crt_t)g_ptr_array_index(array, i));
  }
  else
  {
    for(i = 0; i < array->len; ++ i)
      gnutls_x509_crt_deinit((gnutls_x509_crt_t)g_ptr_array_index(array, i));
    g_ptr_array_free(array, TRUE);
  }
}

/*
 * Public API.
 */

/**
 * inf_cert_util_create_dh_params:
 * @error: Location to store error information, if any.
 *
 * Creates new, random Diffie-Hellman parameters.
 *
 * Returns: New dhparams to be freed with gnutls_dh_params_deinit(),
 * or %NULL in case of error.
 */
gnutls_dh_params_t
inf_cert_util_create_dh_params(GError** error)
{
  gnutls_dh_params_t params;
  int res;

  params = NULL;
  res = gnutls_dh_params_init(&params);
    
  if(res != 0)
  {
    inf_gnutls_set_error(error, res);
    return NULL;
  }

  res = gnutls_dh_params_generate2(params, 2048);
  if(res != 0)
  {
    gnutls_dh_params_deinit(params);
    inf_gnutls_set_error(error, res);
    return NULL;
  }

  return params;
}

/**
 * inf_cert_util_read_dh_params:
 * @filename: A path to a DH parameters file.
 * @error: Location to store error information, if any.
 *
 * Reads the Diffie-Hellman parameters located at @filename into a
 * gnutls_dh_params_t structure.
 *
 * Returns: New dhparams to be freed with gnutls_dh_params_deinit(),
 * or %NULL in case of error.
 */
gnutls_dh_params_t
inf_cert_util_read_dh_params(const gchar* filename,
                             GError** error)
{
  gnutls_dh_params_t params;
  gnutls_datum_t datum;
  gchar* data;
  gsize size;
  int res;

  params = NULL;
  data = NULL;

  if(g_file_get_contents(filename, &data, &size, error) == TRUE)
  {
    res = gnutls_dh_params_init(&params);
    if(res != 0)
    {
      inf_gnutls_set_error(error, res);
    }
    else
    {
      datum.data = (unsigned char*)data;
      datum.size = (unsigned int)size;

      res = gnutls_dh_params_import_pkcs3(
        params,
        &datum,
        GNUTLS_X509_FMT_PEM
      );

      if(res != 0)
      {
        gnutls_dh_params_deinit(params);
        inf_gnutls_set_error(error, res);
        params = NULL;
      }
    }

    g_free(data);
  }

  return params;
}

/**
 * inf_cert_util_write_dh_params:
 * @params: An initialized #gnutls_dh_params_t structure.
 * @filename: The path at which so store @params.
 * @error: Location to store error information, if any.
 *
 * Writes the given Diffie-Hellman parameters to the given path on the
 * filesystem. If an error occurs, @error is set and %FALSE is returned.
 *
 * Returns: %TRUE on success or %FALSE otherwise.
 */
gboolean
inf_cert_util_write_dh_params(gnutls_dh_params_t params,
                              const gchar* filename,
                              GError** error)
{
  unsigned char* data;
  size_t size;
  int res;
  gboolean bres;

  size = 0;
  data = NULL;
  bres = FALSE;

  res = gnutls_dh_params_export_pkcs3(
    params,
    GNUTLS_X509_FMT_PEM,
    NULL,
    &size
  );

  g_assert(res != 0); /* cannot succeed */
  if(res != GNUTLS_E_SHORT_MEMORY_BUFFER)
  {
    inf_gnutls_set_error(error, res);
  }
  else
  {
    data = g_malloc(size);
    res = gnutls_dh_params_export_pkcs3(
      params,
      GNUTLS_X509_FMT_PEM,
      data,
      &size
    );

    if(res != 0)
      inf_gnutls_set_error(error, res);
    else
      bres = g_file_set_contents(filename, (gchar*)data, size, error);

    g_free(data);
  }

  return bres;
}

/**
 * inf_cert_util_create_private_key:
 * @error: Location to store error information, if any.
 *
 * Generates a new, random X.509 private key.
 *
 * Returns: A new key to be freed with gnutls_x509_privkey_deinit(),
 * or %NULL if an error occured.
 */
gnutls_x509_privkey_t
inf_cert_util_create_private_key(GError** error)
{
  gnutls_x509_privkey_t key;
  int res;

  res = gnutls_x509_privkey_init(&key);
  if(res != 0)
  {
    inf_gnutls_set_error(error, res);
    return NULL;
  }

  res = gnutls_x509_privkey_generate(key, GNUTLS_PK_RSA, 2048, 0);
  if(res != 0)
  {
    inf_gnutls_set_error(error, res);
    gnutls_x509_privkey_deinit(key);
    return NULL;
  }

  return key;
}

/**
 * inf_cert_util_read_private_key:
 * @filename: A path to a X.509 private key file
 * @error: Location for error information, if any.
 *
 * Reads the key located at @filename into a gnutls_x509_privkey_t
 * structure.
 *
 * Returns: A private key. Free with gnutls_x509_privkey_deinit().
 */
gnutls_x509_privkey_t
inf_cert_util_read_private_key(const gchar* filename,
                               GError** error)
{
  gnutls_x509_privkey_t key;
  gnutls_datum_t datum;
  gchar* data;
  gsize size;
  int res;

  key = NULL;
  data = NULL;

  if(g_file_get_contents(filename, &data, &size, error) == TRUE)
  {
    res = gnutls_x509_privkey_init(&key);
    if(res != 0)
    {
      inf_gnutls_set_error(error, res);
    }
    else
    {
      datum.data = (unsigned char*)data;
      datum.size = (unsigned int)size;

      res = gnutls_x509_privkey_import(
        key,
        &datum,
        GNUTLS_X509_FMT_PEM
      );

      if(res != 0)
      {
        gnutls_x509_privkey_deinit(key);
        inf_gnutls_set_error(error, res);
        key = NULL;
      }
    }

    g_free(data);
  }

  return key;
}

/**
 * inf_cert_util_write_private_key:
 * @key: An initialized #gnutls_x509_privkey_t structure.
 * @filename: The path at which so store the key.
 * @error: Location to store error information, if any.
 *
 * Writes @key to the location specified by @filename on the filesystem.
 * If an error occurs, the function returns %FALSE and @error is set.
 *
 * Returns: %TRUE on success, %FALSE otherwise.
 */
gboolean
inf_cert_util_write_private_key(gnutls_x509_privkey_t key,
                                const gchar* filename,
                                GError** error)
{
  unsigned char* data;
  size_t size;
  int res;
  gboolean bres;

  size = 0;
  data = NULL;
  bres = FALSE;

  res = gnutls_x509_privkey_export(
    key,
    GNUTLS_X509_FMT_PEM,
    NULL,
    &size
  );

  g_assert(res != 0); /* cannot succeed */
  if(res != GNUTLS_E_SHORT_MEMORY_BUFFER)
  {
    inf_gnutls_set_error(error, res);
  }
  else
  {
    data = g_malloc(size);
    res = gnutls_x509_privkey_export(
      key,
      GNUTLS_X509_FMT_PEM,
      data,
      &size
    );

    if(res != 0)
      inf_gnutls_set_error(error, res);
    else
      bres = g_file_set_contents(filename, (gchar*)data, size, error);

    g_free(data);
  }

  return bres;
}

/**
 * inf_cert_util_create_self_signed_certificate:
 * @key: The key with which to sign the certificate.
 * @error: Location to store error information, if any.
 *
 * Creates an new self-signed X.509 certificate signed with @key.
 *
 * Returns: A certificate to be freed with gnutls_x509_crt_deinit(),
 * or %NULL on error.
 */
gnutls_x509_crt_t
inf_cert_util_create_self_signed_certificate(gnutls_x509_privkey_t key,
                                             GError** error)
{
  gnutls_x509_crt_t cert;
  int res;

  res = gnutls_x509_crt_init(&cert);
  if(res != 0)
  {
    inf_gnutls_set_error(error, res);
    return NULL;
  }

  res = inf_cert_util_create_self_signed_certificate_impl(cert, key);
  if(res != 0)
  {
    gnutls_x509_crt_deinit(cert);
    inf_gnutls_set_error(error, res);
    return NULL;
  }

  return cert;
}

/**
 * inf_cert_util_read_certificate:
 * @filename: A path to a X.509 certificate file.
 * @current: An array of #gnutls_x509_crt_t objects, or %NULL.
 * @error: Location to store error information, if any.
 *
 * Loads X.509 certificates in PEM format from the file at @filename. There
 * can be any number of certificates in the file. If @current is not %NULL,
 * the new certificates are appended to the array. Otherwise, a new array
 * with the read certificates is returned.
 *
 * If an error occurs, the function returns %NULL and @error is set. If
 * @current is non-%NULL and the function succeeds, the return value is the
 * same as @current.
 *
 * Returns: An array of the read certificates, or %NULL on error.
 */
GPtrArray*
inf_cert_util_read_certificate(const gchar* filename,
                               GPtrArray* current,
                               GError** error)
{
  gchar* contents;
  gsize length;
  GPtrArray* result;
  guint current_len;

  gchar* begin;
  gchar* end;

  int ret;
  gnutls_datum_t import_data;
  gnutls_x509_crt_t crt;

  if(!g_file_get_contents(filename, &contents, &length, error))
    return NULL;

  if(current == NULL)
  {
    result = g_ptr_array_new();
  }
  else
  {
    result = current;
    current_len = current->len;
  }

  end = contents;
  for(;;)
  {
    begin = g_strstr_len(end, length - (end - contents), X509_BEGIN_1);
    if(begin)
    {
      end = g_strstr_len(begin, length - (begin - contents), X509_END_1);
      if(!end) break;
      end += sizeof(X509_END_1);
    }
    else
    {
      begin = g_strstr_len(end, length - (end - contents), X509_BEGIN_2);
      if(!begin) break;
      end = g_strstr_len(begin, length - (begin - contents), X509_END_2);
      if(!end) break;
      end += sizeof(X509_END_2);
    }

    import_data.data = (unsigned char*)begin;
    import_data.size = end - begin;

    ret = gnutls_x509_crt_init(&crt);
    if(ret != GNUTLS_E_SUCCESS)
    {
      inf_cert_util_free_array(result, current, current_len);
      inf_gnutls_set_error(error, ret);
      g_free(contents);
      return NULL;
    }

    ret = gnutls_x509_crt_import(crt, &import_data, GNUTLS_X509_FMT_PEM);
    if(ret != GNUTLS_E_SUCCESS)
    {
      gnutls_x509_crt_deinit(crt);
      inf_cert_util_free_array(result, current, current_len);
      inf_gnutls_set_error(error, ret);
      g_free(contents);
      return NULL;
    }

    g_ptr_array_add(result, crt);
  }

  g_free(contents);
  return result;
}

/**
 * inf_cert_util_write_certificate:
 * @certs: An array of #gnutls_x509_crt_t objects.
 * @n_certs: Number of certificates in the error.
 * @filename: The path at which to store the certificates.
 * @error: Location to store error information, if any.
 *
 * This function writes the certificates in the array @certs to disk, in
 * PEM format. If an error occurs the function returns %FALSE and @error
 * is set.
 *
 * Returns: %TRUE on success or %FALSE otherwise.
 */
gboolean
inf_cert_util_write_certificate(gnutls_x509_crt_t* certs,
                                guint n_certs,
                                const gchar* filename,
                                GError** error)
{
  GIOChannel* channel;
  guint i;
  gnutls_x509_crt_t cert;
  int res;
  size_t size;
  gchar* buffer;
  GIOStatus status;

  channel = g_io_channel_new_file(filename, "w", error);
  if(channel == NULL) return FALSE;

  status = g_io_channel_set_encoding(channel, NULL, error);
  if(status != G_IO_STATUS_NORMAL)
  {
    g_io_channel_unref(channel);
    return FALSE;
  }

  for(i = 0; i < n_certs; ++ i)
  {
    if(i > 0)
    {
      status = g_io_channel_write_chars(channel, "\n", 1, NULL, error);
      if(status != G_IO_STATUS_NORMAL)
      {
        g_io_channel_unref(channel);
        return FALSE;
      }
    }

    cert = (gnutls_x509_crt_t)certs[i];
    size = 0;
    res = gnutls_x509_crt_export(cert, GNUTLS_X509_FMT_PEM, NULL, &size);
    if(res != GNUTLS_E_SHORT_MEMORY_BUFFER)
    {
      g_io_channel_unref(channel);
      inf_gnutls_set_error(error, res);
      return FALSE;
    }

    buffer = g_malloc(size);
    res = gnutls_x509_crt_export(cert, GNUTLS_X509_FMT_PEM, buffer, &size);
    if(res != GNUTLS_E_SUCCESS)
    {
      g_free(buffer);
      g_io_channel_unref(channel);
      inf_gnutls_set_error(error, res);
      return FALSE;
    }

    status = g_io_channel_write_chars(channel, buffer, size, NULL, error);
    g_free(buffer);

    if(status != G_IO_STATUS_NORMAL)
    {
      g_io_channel_unref(channel);
      return FALSE;
    }
  }

  g_io_channel_unref(channel);
  return TRUE;
}

/**
 * inf_cert_util_copy_certificate:
 * @src: The certificate to copy.
 * @error: Location to store error information, if any.
 *
 * Creates a copy of the certificate @src and returns the copy. If the
 * function fails %FALSE is returned and @error is set.
 *
 * Returns: A copy of @src, or %NULL on error. Free with
 * gnutls_x509_crt_deinit() when no longer in use.
 */
gnutls_x509_crt_t
inf_cert_util_copy_certificate(gnutls_x509_crt_t src,
                               GError** error)
{
  gnutls_x509_crt_t dest;
  int ret;
  size_t der_size;
  gpointer data;
  gnutls_datum_t tmp;

  ret = gnutls_x509_crt_export(src, GNUTLS_X509_FMT_DER, NULL, &der_size);
  if (ret != GNUTLS_E_SHORT_MEMORY_BUFFER)
  {
    inf_gnutls_set_error(error, ret);
    return NULL;
  }

  data = g_malloc(der_size);

  ret = gnutls_x509_crt_export(src, GNUTLS_X509_FMT_DER, data, &der_size);
  if (ret < 0)
  {
    g_free(data);
    inf_gnutls_set_error(error, ret);
    return NULL;
  }

  gnutls_x509_crt_init(&dest);

  tmp.data = data;
  tmp.size = der_size;
  ret = gnutls_x509_crt_import(dest, &tmp, GNUTLS_X509_FMT_DER);

  g_free(data);

  if (ret < 0)
  {
    gnutls_x509_crt_deinit(dest);
    inf_gnutls_set_error(error, ret);
    return NULL;
  }

  return dest;
}

/**
 * inf_cert_util_get_dn_by_oid:
 * @cert: An initialized #gnutls_x509_crt_t.
 * @oid: The name of the requested entry.
 * @index: Index of the entry to retrieve.
 *
 * Retrieves the given item from the certificate. This function is a thin
 * wrapper around gnutls_x509_crt_get_dn_by_oid(), allocating memory for the
 * return value. The function returns %NULL if there is no such entry in the
 * certificate.
 *
 * Returns: The certificate entry, or %NULL if it is not present. Free with
 * g_free() after use.
 */
gchar*
inf_cert_util_get_dn_by_oid(gnutls_x509_crt_t cert,
                            const char* oid,
                            unsigned int index)
{
  size_t size;
  gchar* buffer;
  int ret;

  buffer = NULL;
  size = 0;

  ret = gnutls_x509_crt_get_dn_by_oid(cert, oid, index, 0, buffer, &size);
  if(ret == GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE) return NULL;

  g_assert(ret == GNUTLS_E_SHORT_MEMORY_BUFFER);

  buffer = g_malloc(size);
  ret = gnutls_x509_crt_get_dn_by_oid(cert, oid, index, 0, buffer, &size);

  if(ret < 0)
  {
    g_free(buffer);
    buffer = NULL;
  }

  return buffer;
}

/**
 * inf_cert_util_get_issuer_dn_by_oid:
 * @cert: An initialized #gnutls_x509_crt_t.
 * @oid: The name of the requested entry.
 * @index: Index of the entry to retrieve.
 *
 * Retrieves the given item from the issuer of the certificate. This function
 * is a thin wrapper around gnutls_x509_crt_get_issuer_dn_by_oid(),
 * allocating memory for the return value. The functions returns %NULL if
 * there is no such entry in the certificate.
 *
 * Returns: The certificate entry, or %NULL if it is not present. Free with
 * g_free() after use.
 */
gchar*
inf_cert_util_get_issuer_dn_by_oid(gnutls_x509_crt_t cert,
                                   const char* oid,
                                   unsigned int index)
{
  size_t size;
  gchar* buffer;
  int ret;

  buffer = NULL;
  size = 0;

  ret = gnutls_x509_crt_get_issuer_dn_by_oid(
    cert,
    oid,
    index,
    0,
    buffer,
    &size
  );

  if(ret == GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE) return NULL;
  g_assert(ret == GNUTLS_E_SHORT_MEMORY_BUFFER);

  buffer = g_malloc(size);

  ret = gnutls_x509_crt_get_issuer_dn_by_oid(
    cert,
    oid,
    index,
    0,
    buffer,
    &size
  );

  if(ret < 0)
  {
    g_free(buffer);
    buffer = NULL;
  }

  return buffer;
}

/**
 * inf_cert_util_get_hostname:
 * @cert: An initialized gnutls_x509_crt_t.
 *
 * Attempts to read the hostname of a certificate. This is done by looking
 * at the DNS name and IP address SANs. If both are not available, the common
 * name of the certificate is returned.
 *
 * Returns: The best guess for the certificate's hostname, or %NULL when
 * it cannot be retrieved. Free with g_free() after use.
 */
gchar*
inf_cert_util_get_hostname(gnutls_x509_crt_t cert)
{
  guint i;
  int ret;
  size_t size;
  gchar* buffer;

  buffer = NULL;
  size = 0;

  for(i = 0; ; ++ i)
  {
    ret = gnutls_x509_crt_get_subject_alt_name(cert, i, buffer, &size, NULL);
    if(ret == GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE) break;

    if(ret == GNUTLS_E_SHORT_MEMORY_BUFFER)
    {
      buffer = g_realloc(buffer, size);
      ret = gnutls_x509_crt_get_subject_alt_name(
        cert,
        i,
        buffer,
        &size,
        NULL
      );
    }

    if(ret == GNUTLS_SAN_DNSNAME || ret == GNUTLS_SAN_IPADDRESS)
      return buffer;
  }

  g_free(buffer);

  return inf_cert_util_get_dn_by_oid(cert, GNUTLS_OID_X520_COMMON_NAME, 0);
}

/**
 * inf_cert_util_get_serial_number:
 * @cert: An initialized #gnutls_x509_crt_t.
 *
 * Read the serial number of a certificate and return it in hexadecimal
 * format. If the serial number cannot be read %NULL is returned.
 *
 * Returns: The serial number of the certificate, or %NULL. Free with g_free()
 * after use.
 */
gchar*
inf_cert_util_get_serial_number(gnutls_x509_crt_t cert)
{
  int ret;
  size_t size;
  guchar* value;
  gchar* formatted;

  size = 0;
  ret = gnutls_x509_crt_get_serial(cert, NULL, &size);
  if(ret != GNUTLS_E_SHORT_MEMORY_BUFFER) return NULL;

  value = g_malloc(size);
  ret = gnutls_x509_crt_get_serial(cert, value, &size);
  if(ret != GNUTLS_E_SUCCESS) return NULL;

  formatted = inf_cert_util_format_hexadecimal(value, size);
  g_free(value);

  return formatted;
}

/**
 * inf_cert_util_get_fingerprint:
 * @cert: An initialized #gnutls_x509_crt_t.
 * @algo: The hashing algorithm to use.
 *
 * Returns the fingerprint of the certificate hashed with the specified
 * algorithm, in hexadecimal format. If the fingerprint cannot be read %NULL
 * is returned.
 *
 * Returns: The fingerprint of the certificate, or %NULL. Free with g_free()
 * after use.
 */
gchar*
inf_cert_util_get_fingerprint(gnutls_x509_crt_t cert,
                              gnutls_digest_algorithm_t algo)
{
  int ret;
  size_t size;
  guchar* value;
  gchar* formatted;

  size = 0;
  ret = gnutls_x509_crt_get_fingerprint(cert, algo, NULL, &size);
  if(ret != GNUTLS_E_SHORT_MEMORY_BUFFER) return NULL;

  value = g_malloc(size);
  ret = gnutls_x509_crt_get_fingerprint(cert, algo, value, &size);
  if(ret != GNUTLS_E_SUCCESS) return NULL;

  formatted = inf_cert_util_format_hexadecimal(value, size);
  g_free(value);

  return formatted;
}

/**
 * inf_cert_util_get_activation_time:
 * @cert: An initialized #gnutls_x509_crt_t.
 *
 * Returns the activation time of the certificate as a string in
 * human-readable format. If the activation time cannot be read %NULL is
 * returned.
 *
 * Returns: The activation time of the certificate, or %NULL. Free with
 * g_free() after use.
 */
gchar*
inf_cert_util_get_activation_time(gnutls_x509_crt_t cert)
{
  time_t time;
  time = gnutls_x509_crt_get_activation_time(cert);
  if(time == (time_t)(-1)) return NULL;
  return inf_cert_util_format_time(time);
}

/**
 * inf_cert_util_get_expiration_time:
 * @cert: An initialized #gnutls_x509_crt_t.
 *
 * Returns the expiration time of the certificate as a string in
 * human-readable format. If the expiration time cannot be read %NULL is
 * returned.
 *
 * Returns: The expiration time of the certificate, or %NULL. Free with
 * g_free() after use.
 */
gchar*
inf_cert_util_get_expiration_time(gnutls_x509_crt_t cert)
{
  time_t time;
  time = gnutls_x509_crt_get_expiration_time(cert);
  if(time == (time_t)(-1)) return NULL;
  return inf_cert_util_format_time(time);
}

/* vim:set et sw=2 ts=2: */
