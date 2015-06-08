/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2015 Armin Burgmeier <armin@arbur.net>
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
#include <libinfinity/inf-i18n.h>

#include <glib/gstdio.h>

#include <gnutls/x509.h>

#include <string.h>

#define X509_BEGIN_1 "-----BEGIN CERTIFICATE-----"
#define X509_BEGIN_2 "-----BEGIN X509 CERTIFICATE-----"

#define X509_END_1   "-----END CERTIFICATE-----"
#define X509_END_2   "-----END X509 CERTIFICATE----"

typedef enum _InfCertUtilError {
  INF_CERT_UTIL_ERROR_DUPLICATE_HOST_ENTRY
} InfCertUtilError;

/*
 * Helper functions
 */

static const unsigned int DAYS = 24 * 60 * 60;

/* memrchr does not seem to be available everywhere, so we implement it
 * ourselves. */
static void*
inf_cert_util_memrchr(void* buf,
                      char c,
                      size_t len)
{
  char* pos;
  char* end;

  pos = buf + len;
  end = buf;

  while(pos > end)
  {
    if(*(pos - 1) == c)
      return pos - 1;
    --pos;
  }

  return NULL;
}


static int
inf_cert_util_create_certificate_impl(gnutls_x509_crt_t cert,
                                      gnutls_x509_privkey_t key,
                                      const InfCertUtilDescription* desc)
{
  char buffer[5];
  time_t timestamp;
  int res;

  res = gnutls_x509_crt_set_key(cert, key);
  if(res != 0) return res;

  timestamp = time(NULL);

  buffer[4] = (timestamp      ) & 0xff;
  buffer[3] = (timestamp >>  8) & 0xff;
  buffer[2] = (timestamp >> 16) & 0xff;
  buffer[1] = (timestamp >> 24) & 0xff;
  buffer[0] = (timestamp >> 32) & 0xff;

  res = gnutls_x509_crt_set_serial(cert, buffer, 5);
  if(res != 0) return res;

  /* Set the activation time a bit in the past, so that if someones
   * clock is slightly offset they don't find the certificate invalid */
  res = gnutls_x509_crt_set_activation_time(cert, timestamp - DAYS / 10);
  if(res != 0) return res;

  res = gnutls_x509_crt_set_expiration_time(cert, timestamp + desc->validity);
  if(res != 0) return res;

  res = gnutls_x509_crt_set_basic_constraints(cert, 0, -1);
  if(res != 0) return res;

  res = gnutls_x509_crt_set_key_usage(cert, GNUTLS_KEY_DIGITAL_SIGNATURE);
  if(res != 0) return res;

  res = gnutls_x509_crt_set_version(cert, 3);
  if(res != 0) return res;

  if(desc->dn_common_name != NULL)
  {
    res = gnutls_x509_crt_set_dn_by_oid(
      cert,
      GNUTLS_OID_X520_COMMON_NAME,
      0,
      desc->dn_common_name,
      strlen(desc->dn_common_name)
    );

    if(res != 0) return res;
  }

  if(desc->san_dnsname != NULL)
  {
    res = gnutls_x509_crt_set_subject_alternative_name(
      cert,
      GNUTLS_SAN_DNSNAME,
      desc->san_dnsname
    );

    if(res != 0) return res;
  }

  return 0;
}

static gboolean
inf_cert_util_write_certificates_string(gnutls_x509_crt_t* certs,
                                        guint n_certs,
                                        GString* string,
                                        GError** error)
{
  guint i;
  gnutls_x509_crt_t cert;
  int res;
  size_t size;
  gchar* buffer;
  GIOStatus status;

  for(i = 0; i < n_certs; ++ i)
  {
    if(i > 0)
    {
      g_string_append_c(string, '\n');
    }

    cert = (gnutls_x509_crt_t)certs[i];
    size = 0;
    res = gnutls_x509_crt_export(cert, GNUTLS_X509_FMT_PEM, NULL, &size);
    if(res != GNUTLS_E_SHORT_MEMORY_BUFFER)
    {
      inf_gnutls_set_error(error, res);
      return FALSE;
    }

    buffer = g_malloc(size);
    res = gnutls_x509_crt_export(cert, GNUTLS_X509_FMT_PEM, buffer, &size);
    if(res != GNUTLS_E_SUCCESS)
    {
      g_free(buffer);
      inf_gnutls_set_error(error, res);
      return FALSE;
    }

    /* TODO: We could optimize this by directly filling string's buffer,
     * instead of copying the output buffer here. */
    g_string_append_len(string, buffer, size);
    g_free(buffer);
  }

  return TRUE;
}

static gboolean
inf_cert_util_write_private_key_string(gnutls_x509_privkey_t key,
                                       GString* string,
                                       GError** error)
{
  unsigned char* data;
  size_t size;
  int res;
  gboolean bres;
  GIOStatus status;

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
    return FALSE;
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
    {
      g_free(data);
      inf_gnutls_set_error(error, res);
      return FALSE;
    }
    else
    {
      /* TODO: We could optimize this by directly filling string's buffer,
       * instead of copying the output buffer here. */
      g_string_append_len(string, data, size);
      g_free(data);
    }
  }

  return TRUE;
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
 * Returns: (transfer full): New dhparams to be freed
 * with gnutls_dh_params_deinit(), or %NULL in case of error.
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
 * @filename: (type filename): A path to a DH parameters file.
 * @error: Location to store error information, if any.
 *
 * Reads the Diffie-Hellman parameters located at @filename into a
 * gnutls_dh_params_t structure.
 *
 * Returns: (transfer full): New dhparams to be freed with
 * gnutls_dh_params_deinit(), or %NULL in case of error.
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
 * @params: (transfer none): An initialized #gnutls_dh_params_t
 * structure.
 * @filename: (type filename): The path at which so store @params.
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
 * @algo: The key algorithm to use (RSA or DSA).
 * @bits: The length of the key to generate.
 * @error: Location to store error information, if any.
 *
 * Generates a new, random X.509 private key. This function is a thin
 * wrapper around gnutls_x509_privkey_generate() which provides GError-style
 * error reporting.
 *
 * Returns: (transfer full): A new key to be freed with
 * gnutls_x509_privkey_deinit(), or %NULL if an error occured.
 */
gnutls_x509_privkey_t
inf_cert_util_create_private_key(gnutls_pk_algorithm_t algo,
                                 unsigned int bits,
                                 GError** error)
{
  gnutls_x509_privkey_t key;
  int res;

  res = gnutls_x509_privkey_init(&key);
  if(res != 0)
  {
    inf_gnutls_set_error(error, res);
    return NULL;
  }

  res = gnutls_x509_privkey_generate(key, algo, bits, 0);
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
 * @filename: (type filename): A path to a X.509 private key file
 * @error: Location for error information, if any.
 *
 * Reads the key located at @filename into a gnutls_x509_privkey_t
 * structure.
 *
 * Returns: (transfer full): A private key. Free with
 * gnutls_x509_privkey_deinit().
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
 * @key: (transfer none): An initialized #gnutls_x509_privkey_t
 * structure.
 * @filename: (type filename): The path at which so store the key.
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
  GString* string;
  gboolean result;

  string = g_string_sized_new(4096);

  result = inf_cert_util_write_private_key_string(key, string, error);
  if(result == FALSE)
  {
    g_string_free(string, TRUE);
    return FALSE;
  }

  result = g_file_set_contents(
    filename,
    string->str,
    string->len,
    error
  );

  g_string_free(string, TRUE);
  return result;
}

/**
 * inf_cert_util_create_certificate:
 * @key: (transfer none): The private key to be used for the new
 * certificate.
 * @desc: The certificate properties.
 * @error: Location to store error information, if any, or %NULL.
 *
 * Creates a new X.509 certificate with the given key and properties. If
 * an error occurs the function returns %NULL and @error is set. The
 * returned certificate will not be signed.
 *
 * Returns: (transfer full): A new #gnutls_x509_crt_t, or %NULL.
 * Free with gnutls_x509_crt_deinit() when no longer needed.
 */
gnutls_x509_crt_t
inf_cert_util_create_certificate(gnutls_x509_privkey_t key,
                                 const InfCertUtilDescription* desc,
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

  res = inf_cert_util_create_certificate_impl(cert, key, desc);
  if(res != 0)
  {
    gnutls_x509_crt_deinit(cert);
    inf_gnutls_set_error(error, res);
    return NULL;
  }

  return cert;
}

/**
 * inf_cert_util_create_signed_certificate:
 * @key: (transfer none): The private key to be used for the new
 * certificate.
 * @desc: The certificate properties.
 * @sign_cert: (transfer none): A certificate used to sign the newly
 * created certificate.
 * @sign_key: (transfer none): The private key for @sign_cert.
 * @error: Location to store error information, if any, or %NULL.
 *
 * Creates a new X.509 certificate with the given key and properties. If
 * an error occurs the function returns %NULL and @error is set. The
 * returned certificate will be signed by @sign_cert.
 *
 * Returns: (transfer full): A new #gnutls_x509_crt_t, or %NULL.
 * Free with gnutls_x509_crt_deinit() when no longer needed.
 */
gnutls_x509_crt_t
inf_cert_util_create_signed_certificate(gnutls_x509_privkey_t key,
                                        const InfCertUtilDescription* desc,
                                        gnutls_x509_crt_t sign_cert,
                                        gnutls_x509_privkey_t sign_key,
                                        GError** error)
{
  gnutls_x509_crt_t cert;
  int res;

  cert = inf_cert_util_create_certificate(key, desc, error);
  if(cert == NULL) return NULL;

  res = gnutls_x509_crt_sign2(
    cert,
    sign_cert,
    sign_key,
    GNUTLS_DIG_SHA256,
    0
  );

  if(res != 0)
  {
    gnutls_x509_crt_deinit(cert);
    inf_gnutls_set_error(error, res);
    return NULL;
  }

  return cert;
}

/**
 * inf_cert_util_create_self_signed_certificate:
 * @key: (transfer none): The private key to be used for the new
 * certificate.
 * @desc: The certificate properties.
 * @error: Location to store error information, if any, or %NULL.
 *
 * Creates a new X.509 certificate with the given key and properties. If
 * an error occurs the function returns %NULL and @error is set. The
 * returned certificate will be signed by itself.
 *
 * Returns: (transfer full): A new #gnutls_x509_crt_t, or %NULL.
 * Free with gnutls_x509_crt_deinit() when no longer needed.
 */
gnutls_x509_crt_t
inf_cert_util_create_self_signed_certificate(gnutls_x509_privkey_t key,
                                             const InfCertUtilDescription* desc,
                                             GError** error)
{
  gnutls_x509_crt_t cert;
  int res;

  cert = inf_cert_util_create_certificate(key, desc, error);
  if(cert == NULL) return NULL;

  res = gnutls_x509_crt_sign2(
    cert,
    cert,
    key,
    GNUTLS_DIG_SHA256,
    0
  );

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
 * @filename: (type filename): A path to a X.509 certificate file.
 * @current: (element-type gnutls_x509_crt_t) (allow-none) (transfer full): An
 * array of #gnutls_x509_crt_t objects, or %NULL.
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
 * Returns: (transfer full) (element-type gnutls_x509_crt_t): An array of the
 * read certificates, or %NULL on error.
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
 * @certs: (array length=n_certs) (transfer none): An array of
 * #gnutls_x509_crt_t objects.
 * @n_certs: Number of certificates in the error.
 * @filename: (type filename): The path at which to store the certificates.
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
  GString* string;
  gboolean result;

  string = g_string_sized_new(n_certs * 4096);

  result = inf_cert_util_write_certificates_string(
    certs,
    n_certs,
    string,
    error
  );

  if(result == FALSE)
  {
    g_string_free(string, TRUE);
    return FALSE;
  }

  result = g_file_set_contents(
    filename,
    string->str,
    string->len,
    error
  );

  g_string_free(string, TRUE);
  return result;
}

/**
 * inf_cert_util_write_certificate_mem:
 * @certs: (transfer none): An
 * array of #gnutls_x509_crt_t objects.
 * @n_certs: Number of certificates in the error.
 * @error: Location to store error information, if any.
 *
 * This function writes the certificates in the array @certs into memory, in
 * PEM format. If an error occurs the function returns %NULL and @error
 * is set.
 *
 * Returns: (transfer full): A string with PEM-encoded certificate data, or
 * %NULL on error. Free with g_free().
 */
gchar*
inf_cert_util_write_certificate_mem(gnutls_x509_crt_t* certs,
                                    guint n_certs,
                                    GError** error)
{
  GString* string;
  gboolean result;

  string = g_string_sized_new(n_certs * 4096);

  result = inf_cert_util_write_certificates_string(
    certs,
    n_certs,
    string,
    error
  );

  if(result == FALSE)
  {
    g_string_free(string, TRUE);
    return FALSE;
  }

  return g_string_free(string, FALSE);
}

/**
 * inf_cert_util_write_certificate_with_key:
 * @key: (transfer none): An initialized #gnutls_x509_privkey_t
 * structure.
 * @certs: (transfer none) (array length=n_certs): An array of
 * #gnutls_x509_crt_t objects.
 * @n_certs: Number of certificates in the error.
 * @filename: (type filename): The path at which to store the certificates.
 * @error: Location to store error information, if any.
 *
 * This function writes both the private key @key as well as the
 * certificates in the array @certs to disk, in PEM format. If an error
 * occurs the function returns %FALSE and @error is set.
 *
 * Returns: %TRUE on success or %FALSE otherwise.
 */
gboolean
inf_cert_util_write_certificate_with_key(gnutls_x509_privkey_t key,
                                         gnutls_x509_crt_t* certs,
                                         guint n_certs,
                                         const gchar* filename,
                                         GError** error)
{
  GString* string;
  gboolean result;

  string = g_string_sized_new( (n_certs + 1) * 4096);

  result = inf_cert_util_write_private_key_string(key, string, error);
  if(result == FALSE)
  {
    g_string_free(string, TRUE);
    return FALSE;
  }

  result = inf_cert_util_write_certificates_string(
    certs,
    n_certs,
    string,
    error
  );

  if(result == FALSE)
  {
    g_string_free(string, TRUE);
    return FALSE;
  }

  g_file_set_contents(
    filename,
    string->str,
    string->len,
    error
  );

  g_string_free(string, TRUE);
  return result;
}

/**
 * inf_cert_util_copy_certificate:
 * @src: (transfer none): The certificate to copy.
 * @error: Location to store error information, if any.
 *
 * Creates a copy of the certificate @src and returns the copy. If the
 * function fails %FALSE is returned and @error is set.
 *
 * Returns: (transfer full): A copy of @src, or %NULL on error. Free
 * with gnutls_x509_crt_deinit() when no longer in use.
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

  der_size = 0;
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
 * inf_cert_util_read_certificate_map:
 * @filename: The filename to read the certificate map from.
 * @error: Location to store error information, if any.
 *
 * Reads a certificate map, i.e. a mapping from hostname to certificate,
 * from the given file. The format of the file is expected to be one entry
 * per line, where each entry consists of the hostname, then a colon
 * character (':'), and then the base64-encoded certificate in DER format.
 *
 * If the file with the given filename does not exist, an empty hash table
 * is returned and the function succeeds.
 *
 * Returns: (transfer container) (element-type string gnutls_x509_crt_t):
 * A hash table with the read mapping, or %NULL on error. Use
 * g_hash_table_unref() to free the hash table when no longer needed.
 */
GHashTable*
inf_cert_util_read_certificate_map(const gchar* filename,
                                   GError** error)
{
  GHashTable* table;
  gchar* content;
  gsize size;
  GError* local_error;

  gchar* out_buf;
  gsize out_buf_len;
  gchar* pos;
  gchar* prev;
  gchar* next;
  gchar* sep;

  gsize len;
  gsize out_len;
  gint base64_state;
  guint base64_save;

  gnutls_datum_t data;
  gnutls_x509_crt_t cert;
  int res;

  table = g_hash_table_new_full(
    g_str_hash,
    g_str_equal,
    g_free,
    (GDestroyNotify)gnutls_x509_crt_deinit
  );

  local_error = NULL;
  g_file_get_contents(filename, &content, &size, &local_error);
  if(local_error != NULL)
  {
    if(local_error->domain == G_FILE_ERROR &&
       local_error->code == G_FILE_ERROR_NOENT)
    {
      return table;
    }

    g_propagate_error(error, local_error);
    g_hash_table_destroy(table);
    return NULL;
  }

  out_buf = NULL;
  out_buf_len = 0;
  prev = content;
  for(prev = content; prev != NULL; prev = next)
  {
    pos = strchr(prev, '\n');
    next = NULL;

    if(pos == NULL)
      pos = content + size;
    else
      next = pos + 1;

    sep = inf_cert_util_memrchr(prev, ':', pos - prev);
    if(sep == NULL) continue; /* ignore line */

    *sep = '\0';
    if(g_hash_table_lookup(table, prev) != NULL)
    {
      g_set_error(
        error,
        g_quark_from_static_string("INF_CERT_UTIL_ERROR"),
        INF_CERT_UTIL_ERROR_DUPLICATE_HOST_ENTRY,
        _("Certificate for host \"%s\" appears twice"),
        prev
      );

      g_hash_table_destroy(table);
      g_free(out_buf);
      g_free(content);
      return NULL;
    }

    /* decode base64, import DER certificate */
    len = (pos - (sep + 1));
    out_len = len * 3 / 4;

    if(out_len > out_buf_len)
    {
      out_buf = g_realloc(out_buf, out_len);
      out_buf_len = out_len;
    }

    base64_state = 0;
    base64_save = 0;

    out_len = g_base64_decode_step(
      sep + 1,
      len,
      out_buf,
      &base64_state,
      &base64_save
    );

    cert = NULL;
    res = gnutls_x509_crt_init(&cert);
    if(res == GNUTLS_E_SUCCESS)
    {
      data.data = out_buf;
      data.size = out_len;
      res = gnutls_x509_crt_import(cert, &data, GNUTLS_X509_FMT_DER);
    }

    if(res != GNUTLS_E_SUCCESS)
    {
      inf_gnutls_set_error(&local_error, res);

      g_propagate_prefixed_error(
        error,
        local_error,
        _("Failed to read certificate for host \"%s\""),
        prev
      );

      if(cert != NULL)
        gnutls_x509_crt_deinit(cert);

      g_hash_table_destroy(table);
      g_free(out_buf);
      g_free(content);
      return NULL;
    }

    g_hash_table_insert(table, g_strdup(prev), cert);
  }

  g_free(out_buf);
  g_free(content);
  return table;
}

/**
 * inf_cert_util_write_certificate_map:
 * @cert_map: (transfer none) (element-type string gnutls_x509_crt_t): A
 * certificate mapping, i.e. a hash table mapping hostname strings to
 * #gnutls_x509_crt_t instances.
 * @filename: The name of the file to write the mapping to.
 * @error: Location to store error information, if any.
 *
 * Writes the given certificate mapping to a file with the given filename.
 * See inf_cert_util_read_certificate_map() for the format of the written
 * file. If an error occurs, @error is set and the function returns %FALSE.
 *
 * This function can be useful to implement trust-on-first-use (TOFU)
 * semantics.
 *
 * Returns: %TRUE on success or %FALSE on error.
 */
gboolean
inf_cert_util_write_certificate_map(GHashTable* cert_map,
                                    const gchar* filename,
                                    GError** error)
{
  gchar* dirname;
  GString* string;

  GHashTableIter iter;
  gpointer key;
  gpointer value;
  const gchar* hostname;
  gnutls_x509_crt_t cert;

  size_t size;
  int res;
  gchar* buffer;
  gchar* encoded_cert;

  string = g_string_sized_new(4096 * g_hash_table_size(cert_map));

  g_hash_table_iter_init(&iter, cert_map);
  while(g_hash_table_iter_next(&iter, &key, &value))
  {
    hostname = (const gchar*)key;
    cert = (gnutls_x509_crt_t)value;

    size = 0;
    res = gnutls_x509_crt_export(cert, GNUTLS_X509_FMT_DER, NULL, &size);
    g_assert(res != GNUTLS_E_SUCCESS);

    buffer = NULL;
    if(res == GNUTLS_E_SHORT_MEMORY_BUFFER)
    {
      buffer = g_malloc(size);
      res = gnutls_x509_crt_export(cert, GNUTLS_X509_FMT_DER, buffer, &size);
    }

    if(res != GNUTLS_E_SUCCESS)
    {
      g_free(buffer);
      g_string_free(string, TRUE);
      inf_gnutls_set_error(error, res);
      return FALSE;
    }

    encoded_cert = g_base64_encode(buffer, size);
    g_free(buffer);
    
    g_string_append(string, hostname);
    g_string_append_c(string, ':');
    g_string_append(string, encoded_cert);
    g_string_append_c(string, '\n');

    g_free(encoded_cert);
  }

  g_file_set_contents(
    filename,
    string->str,
    string->len,
    error
  );

  g_string_free(string, TRUE);
  return TRUE;
}

/**
 * inf_cert_util_check_certificate_key:
 * @cert: (transfer none): The certificate to be checked.
 * @key: (transfer none): The private key to be checked.
 *
 * This function returns %TRUE if @key is the private key belonging to @cert,
 * or %FALSE otherwise.
 *
 * Returns: %TRUE if @cert was signed with @key, or %FALSE otherwise.
 */
gboolean
inf_cert_util_check_certificate_key(gnutls_x509_crt_t cert,
                                    gnutls_x509_privkey_t key)
{
  unsigned char cert_id[20];
  size_t cert_id_size;
  unsigned char key_id[20];
  size_t key_id_size;

  g_return_val_if_fail(cert != NULL, FALSE);
  g_return_val_if_fail(key != NULL, FALSE);

  cert_id_size = 20;
  if(gnutls_x509_crt_get_key_id(cert, 0, cert_id, &cert_id_size) != 0)
    return FALSE;

  key_id_size = 20;
  if(gnutls_x509_privkey_get_key_id(key, 0, key_id, &key_id_size) != 0)
    return FALSE;

  if(memcmp(cert_id, key_id, 20) != 0)
    return FALSE;

  return TRUE;
}

/**
 * inf_cert_util_compare_fingerprint:
 * @cert1: The first certificate to compare.
 * @cert2: The second certificate to compare.
 * @error: Location to store error information, if any.
 *
 * Checks whether the SHA-256 fingerprints of the two given certificates are
 * identical or not. If a fingerprint cannot be obtained, the function
 * returns %FALSE and @error is set.
 *
 * Returns: Whether the two certificates have identical fingerprints. Returns
 * %FALSE on error.
 */
gboolean
inf_cert_util_compare_fingerprint(gnutls_x509_crt_t cert1,
                                  gnutls_x509_crt_t cert2,
                                  GError** error)
{
  static const unsigned int SHA256_DIGEST_SIZE = 32;

  size_t size;
  guchar cert1_fingerprint[SHA256_DIGEST_SIZE];
  guchar cert2_fingerprint[SHA256_DIGEST_SIZE];

  int ret;
  int cmp;

  size = SHA256_DIGEST_SIZE;

  ret = gnutls_x509_crt_get_fingerprint(
    cert1,
    GNUTLS_DIG_SHA256,
    cert1_fingerprint,
    &size
  );

  if(ret == GNUTLS_E_SUCCESS)
  {
    g_assert(size == SHA256_DIGEST_SIZE);

    ret = gnutls_x509_crt_get_fingerprint(
      cert2,
      GNUTLS_DIG_SHA256,
      cert2_fingerprint,
      &size
    );
  }

  if(ret != GNUTLS_E_SUCCESS)
  {
    inf_gnutls_set_error(error, ret);
    return FALSE;
  }

  cmp = memcmp(cert1_fingerprint, cert2_fingerprint, SHA256_DIGEST_SIZE);
  if(cmp != 0) return FALSE;

  return TRUE;
}

/**
 * inf_cert_util_get_dn:
 * @cert: (transfer none): An initialized #gnutls_x509_crt_t.
 *
 * Retrieves the full distinguished name (DN) from the certificate, allocating
 * memory for the return value.
 *
 * Returns: (transfer full): The DN of the certificate. Free with g_free()
 * after use.
 */
gchar*
inf_cert_util_get_dn(gnutls_x509_crt_t cert)
{
  size_t size;
  gchar* buffer;
  int ret;

  buffer = NULL;
  size = 0;

  ret = gnutls_x509_crt_get_dn(cert, buffer, &size);
  if(ret == GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE) return NULL;

  g_assert(ret == GNUTLS_E_SHORT_MEMORY_BUFFER);

  buffer = g_malloc(size);
  ret = gnutls_x509_crt_get_dn(cert, buffer, &size);

  if(ret < 0)
  {
    g_free(buffer);
    buffer = NULL;
  }

  return buffer;
}

/**
 * inf_cert_util_get_dn_by_oid:
 * @cert: (transfer none): An initialized #gnutls_x509_crt_t.
 * @oid: The name of the requested entry.
 * @index: Index of the entry to retrieve.
 *
 * Retrieves the given item from the certificate. This function is a thin
 * wrapper around gnutls_x509_crt_get_dn_by_oid(), allocating memory for the
 * return value. The function returns %NULL if there is no such entry in the
 * certificate.
 *
 * Returns: (transfer full): The certificate entry, or %NULL if it is not
 * present. Free with g_free() after use.
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
 * @cert: (transfer none): An initialized #gnutls_x509_crt_t.
 * @oid: The name of the requested entry.
 * @index: Index of the entry to retrieve.
 *
 * Retrieves the given item from the issuer of the certificate. This function
 * is a thin wrapper around gnutls_x509_crt_get_issuer_dn_by_oid(),
 * allocating memory for the return value. The functions returns %NULL if
 * there is no such entry in the certificate.
 *
 * Returns: (transfer full): The certificate entry, or %NULL if it is not
 * present. Free with g_free() after use.
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
 * @cert: (transfer none): An initialized gnutls_x509_crt_t.
 *
 * Attempts to read the hostname of a certificate. This is done by looking
 * at the DNS name and IP address SANs. If both are not available, the common
 * name of the certificate is returned.
 *
 * Returns: (transfer full) (allow-none): The best guess for the
 * certificate's hostname, or %NULL when it cannot be retrieved. Free with
 * g_free() after use.
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
 * @cert: (transfer none): An initialized #gnutls_x509_crt_t.
 *
 * Read the serial number of a certificate and return it in hexadecimal
 * format. If the serial number cannot be read %NULL is returned.
 *
 * Returns: (transfer full): The serial number of the certificate, or %NULL.
 * Free with g_free() after use.
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
 * @cert: (transfer none): An initialized #gnutls_x509_crt_t.
 * @algo: The hashing algorithm to use.
 *
 * Returns the fingerprint of the certificate hashed with the specified
 * algorithm, in hexadecimal format. If the fingerprint cannot be read %NULL
 * is returned.
 *
 * Returns: (transfer full): The fingerprint of the certificate, or %NULL.
 * Free with g_free() after use.
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
 * @cert: (transfer none): An initialized #gnutls_x509_crt_t.
 *
 * Returns the activation time of the certificate as a string in
 * human-readable format. If the activation time cannot be read %NULL is
 * returned.
 *
 * Returns: (transfer full): The activation time of the certificate, or %NULL.
 * Free with g_free() after use.
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
 * @cert: (transfer none): An initialized #gnutls_x509_crt_t.
 *
 * Returns the expiration time of the certificate as a string in
 * human-readable format. If the expiration time cannot be read %NULL is
 * returned.
 *
 * Returns: (transfer full): The expiration time of the certificate, or %NULL.
 * Free with g_free() after use.
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
