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

#include <glib-object.h>
#include <glib/goption.h>
#include <glib/gstrfuncs.h>

#include <locale.h>
#include <stdio.h>

static gchar* key_file = NULL;
static gchar* cert_file = NULL;
static gboolean create_key = FALSE;
static gboolean create_certificate = FALSE;

static const GOptionEntry entries[] = 
{
  {
    "key-file", 'k',
    0,
    G_OPTION_ARG_FILENAME, &key_file,
    "The server's private key", "KEY-FILE"
  }, {
    "certificate-file", 'c',
    0,
    G_OPTION_ARG_FILENAME, &cert_file,
    "The server's certificate", "CERTIFICATE-FILE"
  }, {
    "create-key", 0,
    0,
    G_OPTION_ARG_NONE, &create_key,
    "Creates a new random private key", NULL
  }, {
    "create-certificate", 0,
    0,
    G_OPTION_ARG_NONE, &create_certificate,
    "Creates a new self-signed certificate using the given key", 0
  }, {
    NULL, 0,
    0,
    G_OPTION_ARG_NONE, NULL,
    NULL, 0
  }
};

int
main(int argc,
     char* argv[])
{
  GOptionContext* context;
  GError* error;

  gnutls_x509_privkey_t key;
  gnutls_x509_crt_t cert;

  setlocale(LC_ALL, "");
  context = g_option_context_new("- infinote dedicated server");
  g_option_context_add_main_entries(context, entries, NULL);

  error = NULL;
  if(g_option_context_parse(context, &argc, &argv, &error) == FALSE)
  {
    fprintf(stderr, "%s\n", error->message);
    g_error_free(error);
    return -1;
  }

  if(create_key == TRUE && create_certificate == FALSE)
  {
    fprintf(
      stderr,
      "If you want create a new key you also must create a new certificate "
      "signed with it.\n"
    );

    return -1;
  }

  if(key_file == NULL) key_file = g_strdup(DEFAULT_KEYPATH);
  if(cert_file == NULL) cert_file = g_strdup(DEFAULT_CERTPATH);

  g_type_init();
  gnutls_global_init();

  if(create_key == TRUE)
  {
    fprintf(stderr, "Generating 2048 bit RSA key...\n");
    key = infinoted_creds_create_key(key_file, &error);
  }
  else
  {
    key = infinoted_creds_read_key(key_file, &error);
  }

  g_free(key_file);
  if(key == NULL)
  {
    fprintf(stderr, "%s\n", error->message);
    g_error_free(error);
    g_free(cert_file);
    return -1;
  }

  if(create_certificate == TRUE)
  {
    fprintf(stderr, "Generating self-signed certificate...\n");

    cert = ininoted_creds_create_self_signed_certificate(
      key,
      cert_file,
      &error
    );
  }
  else
  {
    cert = infinoted_creds_read_certificate(cert_file, &error);
  }

  g_free(cert_file);
  if(cert == NULL)
  {
    fprintf(stderr, "%s\n", error->message);
    g_error_free(error);
    gnutls_x509_privkey_deinit(key);
    g_free(cert_file);
    return -1;
  }

  gnutls_x509_crt_deinit(cert);
  gnutls_x509_privkey_deinit(key);
  gnutls_global_deinit();
  return 0;
}
