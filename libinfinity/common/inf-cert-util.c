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

#include <libinfinity/common/inf-cert-util.h>

#include <glib/gstdio.h>

#include <gnutls/x509.h>

static gchar*
inf_cert_util_format_time(time_t time)
{
  struct tm* tm;
  gsize alloc;
  gchar* result;
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

  return result;
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

int
inf_cert_util_copy(gnutls_x509_crt_t* dest,
                   gnutls_x509_crt_t src)
{
  int ret;
  size_t der_size;
  gpointer data;
  gnutls_datum_t tmp;

  ret = gnutls_x509_crt_export(src, GNUTLS_X509_FMT_DER, NULL, &der_size);
  if (ret != GNUTLS_E_SHORT_MEMORY_BUFFER)
      return ret;

  data = g_malloc(der_size);

  ret = gnutls_x509_crt_export(src, GNUTLS_X509_FMT_DER, data, &der_size);
  if (ret < 0)
  {
    g_free(data);
    return ret;
  }

  gnutls_x509_crt_init(dest);

  tmp.data = data;
  tmp.size = der_size;
  ret = gnutls_x509_crt_import(*dest, &tmp, GNUTLS_X509_FMT_DER);

  g_free(data);

  if (ret < 0)
  {
    gnutls_x509_crt_deinit(*dest);
    return ret;
  }

  return 0;
}

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

/* TODO: Error reporting for the following functions? */

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

gchar*
inf_cert_util_get_activation_time(gnutls_x509_crt_t cert)
{
  time_t time;
  time = gnutls_x509_crt_get_activation_time(cert);
  if(time == (time_t)(-1)) return NULL;
  return inf_cert_util_format_time(time);
}

gchar*
inf_cert_util_get_expiration_time(gnutls_x509_crt_t cert)
{
  time_t time;
  time = gnutls_x509_crt_get_expiration_time(cert);
  if(time == (time_t)(-1)) return NULL;
  return inf_cert_util_format_time(time);
}

/* vim:set et sw=2 ts=2: */
