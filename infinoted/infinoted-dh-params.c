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

#include <infinoted/infinoted-dh-params.h>
#include <infinoted/infinoted-util.h>
#include <libinfinity/common/inf-cert-util.h>
#include <libinfinity/inf-i18n.h>

#include <glib/gstdio.h>
#include <sys/stat.h>

/**
 * infinoted_dh_params_ensure:
 * @log: A #InfinotedLog, or %NULL.
 * @credentials: A #InfCertificateCredentials.
 * @dh_params: A pointer to a gnutils_dh_params_t structure.
 * @error: Location to store error information, if any.
 *
 * Ensures that DH parameters are set in the certificate credentials. If
 * *@dh_params is non-%NULL, then this simply sets *@dh_params in
 * @credentials. Otherwise it tries to read the server's cached DH params
 * from disk. If successful, it sets them in @credentials and stores them in
 * *@dh_params. If not, then it generates new DH params, writes them to the
 * disk cache and sets them into *@dh_params. If generation fails, the
 * function returns %FALSE and @error is set.
 *
 * @log is used to write a log message about the parameters being generated
 * if they are not cached, so the user knows what's going on during this
 * lengthy operation.
 *
 * Returns: %TRUE on success or %FALSE on error.
 */
gboolean
infinoted_dh_params_ensure(InfinotedLog* log,
                           InfCertificateCredentials* credentials,
                           gnutls_dh_params_t* dh_params,
                           GError** error)
{
  gnutls_certificate_credentials_t creds;
  gchar* filename;
  struct stat st;

  creds = inf_certificate_credentials_get(credentials);
  if(*dh_params != NULL)
  {
    gnutls_certificate_set_dh_params(creds, *dh_params);
    return TRUE;
  }

  filename =
    g_build_filename(g_get_home_dir(), ".infinoted", "dh.pem", NULL);

  if(g_stat(filename, &st) == 0)
  {
    /* DH params expire every week */
    /*if(st.st_mtime + 60 * 60 * 24 * 7 > time(NULL))*/
      *dh_params = inf_cert_util_read_dh_params(filename, NULL);
  }

  if(*dh_params == NULL)
  {
    infinoted_util_create_dirname(filename, NULL);

    if(log != NULL)
    {
      infinoted_log_info(
        log, _("Generating 2048 bit Diffie-Hellman parameters..."));
    }

    *dh_params = inf_cert_util_create_dh_params(error);

    if(*dh_params == NULL)
    {
      g_free(filename);
      return FALSE;
    }

    inf_cert_util_write_dh_params(*dh_params, filename, NULL);
  }

  g_free(filename);

  gnutls_certificate_set_dh_params(creds, *dh_params);
  return TRUE;
}

/* vim:set et sw=2 ts=2: */
