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

/* TODO: Rename to InfGtkCertificateChecker */
/* TODO: Put the non-GUI-relevant parts of this code into libinfinity */
/* TODO: Support CRLs/OCSP */

#include <libinfgtk/inf-gtk-certificate-manager.h>
#include <libinfgtk/inf-gtk-certificate-dialog.h>

#include <libinfinity/common/inf-xml-connection.h>
#include <libinfinity/common/inf-cert-util.h>
#include <libinfinity/common/inf-file-util.h>
#include <libinfinity/common/inf-error.h>
#include <libinfinity/inf-i18n.h>
#include <libinfinity/inf-signals.h>

#include <gnutls/x509.h>

#include <string.h>
#include <errno.h>

typedef struct _InfGtkCertificateManagerQuery InfGtkCertificateManagerQuery;
struct _InfGtkCertificateManagerQuery {
  InfGtkCertificateManager* manager;
  GHashTable* known_hosts;
  InfXmppConnection* connection;
  InfGtkCertificateDialog* dialog;
  GtkWidget* checkbutton;
  InfCertificateChain* certificate_chain;
};

typedef struct _InfGtkCertificateManagerPrivate
  InfGtkCertificateManagerPrivate;
struct _InfGtkCertificateManagerPrivate {
  GtkWindow* parent_window;
  InfXmppManager* xmpp_manager;

  gchar* known_hosts_file;
  GSList* queries;
};

typedef enum _InfGtkCertificateManagerError {
  INF_GTK_CERTIFICATE_MANAGER_ERROR_DUPLICATE_HOST_ENTRY
} InfGtkCertificateManagerError;

enum {
  PROP_0,

  PROP_PARENT_WINDOW,
  PROP_XMPP_MANAGER,

  PROP_KNOWN_HOSTS_FILE
};

#define INF_GTK_CERTIFICATE_MANAGER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_GTK_TYPE_CERTIFICATE_MANAGER, InfGtkCertificateManagerPrivate))

G_DEFINE_TYPE_WITH_CODE(InfGtkCertificateManager, inf_gtk_certificate_manager, G_TYPE_OBJECT,
  G_ADD_PRIVATE(InfGtkCertificateManager))

/* When a host presents a certificate different from one that we have pinned,
 * usually we warn the user that something fishy is going on. However, if the
 * pinned certificate has expired or will expire soon, then we kind of expect
 * the certificate to change, and issue a less "flashy" warning message. This
 * value defines how long before the pinned certificate expires we show a
 * less dramatic warning message. */
static const unsigned int
INF_GTK_CERTIFICATE_MANAGER_EXPIRATION_TOLERANCE = 3 * 24 * 3600; /* 3 days */

/* memrchr does not seem to be available everywhere, so we implement it
 * ourselves. */
static void*
inf_gtk_certificate_manager_memrchr(void* buf,
                                    char c,
                                    size_t len)
{
  char* pos;
  char* end;

  pos = buf + len;
  end = buf;

  while(pos >= end)
  {
    if(*(pos - 1) == c)
      return pos - 1;
    --pos;
  }

  return NULL;
}

static GQuark
inf_gtk_certificate_manager_verify_error_quark(void)
{
  return g_quark_from_static_string(
    "INF_GTK_CERTIFICATE_MANAGER_VERIFY_ERROR"
  );
}

#if 0
static InfGtkCertificateManagerQuery*
inf_gtk_certificate_manager_find_query(InfGtkCertificateManager* manager,
                                       InfXmppConnection* connection)
{
  InfGtkCertificateManagerPrivate* priv;
  GSList* item;
  InfGtkCertificateManagerQuery* query;

  priv = INF_GTK_CERTIFICATE_MANAGER_PRIVATE(manager);
  for(item = priv->queries; item != NULL; item == item->next)
  {
    query = (InfGtkCertificateManagerQuery*)item->data;
    if(query->connection == connection)
      return query;
  }

  return NULL;
}
#endif

static void
inf_gtk_certificate_manager_notify_status_cb(GObject* object,
                                             GParamSpec* pspec,
                                             gpointer user_data);

static void
inf_gtk_certificate_manager_query_free(InfGtkCertificateManagerQuery* query)
{
  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(query->connection),
    G_CALLBACK(inf_gtk_certificate_manager_notify_status_cb),
    query
  );

  g_object_unref(query->connection);
  inf_certificate_chain_unref(query->certificate_chain);
  gtk_widget_destroy(GTK_WIDGET(query->dialog));
  g_hash_table_unref(query->known_hosts);
  g_slice_free(InfGtkCertificateManagerQuery, query);
}

static gboolean
inf_gtk_certificate_manager_compare_fingerprint(gnutls_x509_crt_t cert1,
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

static void
inf_gtk_certificate_manager_set_known_hosts(InfGtkCertificateManager* manager,
                                            const gchar* known_hosts_file)
{
  InfGtkCertificateManagerPrivate* priv;
  priv = INF_GTK_CERTIFICATE_MANAGER_PRIVATE(manager);

  /* TODO: If there are running queries, the we need to load the new hosts
   * file and then change it in all queries. */
  g_assert(priv->queries == NULL);

  g_free(priv->known_hosts_file);
  priv->known_hosts_file = g_strdup(known_hosts_file);
}

static GHashTable*
inf_gtk_certificate_manager_load_known_hosts(InfGtkCertificateManager* mgr,
                                             GError** error)
{
  InfGtkCertificateManagerPrivate* priv;
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

  priv = INF_GTK_CERTIFICATE_MANAGER_PRIVATE(mgr);

  table = g_hash_table_new_full(
    g_str_hash,
    g_str_equal,
    g_free,
    (GDestroyNotify)gnutls_x509_crt_deinit
  );

  local_error = NULL;
  g_file_get_contents(priv->known_hosts_file, &content, &size, &local_error);
  if(local_error != NULL)
  {
    if(local_error->domain == G_FILE_ERROR &&
       local_error->code == G_FILE_ERROR_NOENT)
    {
      return table;
    }

    g_propagate_prefixed_error(
      error,
      local_error,
      _("Failed to open known hosts file \"%s\": "),
      priv->known_hosts_file
    );

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

    sep = inf_gtk_certificate_manager_memrchr(prev, ':', pos - prev);
    if(sep == NULL) continue; /* ignore line */

    *sep = '\0';
    if(g_hash_table_lookup(table, prev) != NULL)
    {
      g_set_error(
        error,
        g_quark_from_static_string("INF_GTK_CERTIFICATE_MANAGER_ERROR"),
        INF_GTK_CERTIFICATE_MANAGER_ERROR_DUPLICATE_HOST_ENTRY,
        _("Certificate for host \"%s\" appears twice in "
          "known hosts file \"%s\""),
        prev,
        priv->known_hosts_file
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
        _("Failed to read certificate for host \"%s\" from "
          "known hosts file \"%s\": "),
        prev,
        priv->known_hosts_file
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

static GHashTable*
inf_gtk_certificate_manager_ref_known_hosts(InfGtkCertificateManager* mgr,
                                            GError** error)
{
  InfGtkCertificateManagerPrivate* priv;
  InfGtkCertificateManagerQuery* query;

  priv = INF_GTK_CERTIFICATE_MANAGER_PRIVATE(mgr);
  if(priv->queries != NULL)
  {
    query = (InfGtkCertificateManagerQuery*)priv->queries->data;
    g_hash_table_ref(query->known_hosts);
    return query->known_hosts;
  }
  else
  {
    return inf_gtk_certificate_manager_load_known_hosts(mgr, error);
  }
}

static gboolean
inf_gtk_certificate_manager_write_known_hosts(InfGtkCertificateManager* mgr,
                                              GHashTable* table,
                                              GError** error)
{
  InfGtkCertificateManagerPrivate* priv;
  gchar* dirname;
  GIOChannel* channel;
  GIOStatus status;

  GHashTableIter iter;
  gpointer key;
  gpointer value;
  const gchar* hostname;
  gnutls_x509_crt_t cert;

  size_t size;
  int res;
  gchar* buffer;
  gchar* encoded_cert;

  priv = INF_GTK_CERTIFICATE_MANAGER_PRIVATE(mgr);
  
  /* Note that we pin the whole certificate and not only the public key of
   * our known hosts. This allows us to differentiate two cases when a
   * host presents a new certificate:
   *    a) The old certificate has expired or is very close to expiration. In
   *       this case we still show a message to the user asking whether they
   *       trust the new certificate.
   *    b) The old certificate was perfectly valid. In this case we show a
   *       message saying that the certificate change was unexpected, and
   *       unless it was expected the host should not be trusted.
   */
  dirname = g_path_get_dirname(priv->known_hosts_file);
  if(!inf_file_util_create_directory(dirname, 0755, error))
  {
    g_free(dirname);
    return FALSE;
  }

  g_free(dirname);

  channel = g_io_channel_new_file(priv->known_hosts_file, "w", error);
  if(channel == NULL) return FALSE;

  status = g_io_channel_set_encoding(channel, NULL, error);
  if(status != G_IO_STATUS_NORMAL)
  {
    g_io_channel_unref(channel);
    return FALSE;
  }

  g_hash_table_iter_init(&iter, table);
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
      g_io_channel_unref(channel);
      inf_gnutls_set_error(error, res);
      return FALSE;
    }

    encoded_cert = g_base64_encode(buffer, size);
    g_free(buffer);

    status = g_io_channel_write_chars(channel, hostname, strlen(hostname), NULL, error);
    if(status == G_IO_STATUS_NORMAL)
      status = g_io_channel_write_chars(channel, ":", 1, NULL, error);
    if(status == G_IO_STATUS_NORMAL)
      status = g_io_channel_write_chars(channel, encoded_cert, strlen(encoded_cert), NULL, error);
    if(status == G_IO_STATUS_NORMAL)
      status = g_io_channel_write_chars(channel, "\n", 1, NULL, error);

    g_free(encoded_cert);

    if(status != G_IO_STATUS_NORMAL)
    {
      g_io_channel_unref(channel);
      return FALSE;
    }
  }

  g_io_channel_unref(channel);
  return TRUE;
}

static void
inf_gtk_certificate_manager_write_known_hosts_with_warning(
  InfGtkCertificateManager* mgr,
  GHashTable* table)
{
  InfGtkCertificateManagerPrivate* priv;
  GError* error;
  gboolean result;

  priv = INF_GTK_CERTIFICATE_MANAGER_PRIVATE(mgr);
  error = NULL;

  result = inf_gtk_certificate_manager_write_known_hosts(
    mgr,
    table,
    &error
  );

  if(error != NULL)
  {
    g_warning(
      _("Failed to write file with known hosts \"%s\": %s"),
      priv->known_hosts_file,
      error->message
    );

    g_error_free(error);
  }
}

static void
inf_gtk_certificate_manager_response_cb(GtkDialog* dialog,
                                        int response_id,
                                        gpointer user_data)
{
  InfGtkCertificateManagerQuery* query;
  InfGtkCertificateManagerPrivate* priv;
  InfXmppConnection* connection;

  gchar* hostname;
  gnutls_x509_crt_t cert;
  gnutls_x509_crt_t known_cert;
  GError* error;
  gboolean cert_equal;

  query = (InfGtkCertificateManagerQuery*)user_data;
  priv = INF_GTK_CERTIFICATE_MANAGER_PRIVATE(query->manager);

  connection = query->connection;
  g_object_ref(connection);

  switch(response_id)
  {
  case GTK_RESPONSE_ACCEPT:
    g_object_get(
      G_OBJECT(query->connection),
      "remote-hostname", &hostname,
      NULL
    );

    /* Add the certificate to the known hosts file, but only if it is not
     * already, to avoid unnecessary disk I/O. */
    cert =
      inf_certificate_chain_get_own_certificate(query->certificate_chain);
    known_cert = g_hash_table_lookup(query->known_hosts, hostname);

    error = NULL;
    cert_equal = FALSE;
    if(known_cert != NULL)
    {
      cert_equal = inf_gtk_certificate_manager_compare_fingerprint(
        cert,
        known_cert,
        &error
      );
    }

    if(error != NULL)
    {
      g_warning(
        _("Failed to add certificate to list of known hosts: %s"),
        error->message
      );
    }
    else if(!cert_equal)
    {
      cert = inf_cert_util_copy_certificate(cert, &error);
      g_hash_table_insert(query->known_hosts, hostname, cert);

      inf_gtk_certificate_manager_write_known_hosts_with_warning(
        query->manager,
        query->known_hosts
      );
    }
    else
    {
      g_free(hostname);
    }

    priv->queries = g_slist_remove(priv->queries, query);
    inf_gtk_certificate_manager_query_free(query);

    inf_xmpp_connection_certificate_verify_continue(connection);
    break;
  case GTK_RESPONSE_REJECT:
  case GTK_RESPONSE_DELETE_EVENT:
    priv->queries = g_slist_remove(priv->queries, query);
    inf_gtk_certificate_manager_query_free(query);
    inf_xmpp_connection_certificate_verify_cancel(connection, NULL);
    break;
  default:
    g_assert_not_reached();
    break;
  }

  g_object_unref(connection);
}

static void
inf_gtk_certificate_manager_notify_status_cb(GObject* object,
                                             GParamSpec* pspec,
                                             gpointer user_data)
{
  InfGtkCertificateManagerQuery* query;
  InfGtkCertificateManagerPrivate* priv;
  InfXmppConnection* connection;
  InfXmlConnectionStatus status;

  query = (InfGtkCertificateManagerQuery*)user_data;
  priv = INF_GTK_CERTIFICATE_MANAGER_PRIVATE(query->manager);
  connection = INF_XMPP_CONNECTION(object);

  g_object_get(G_OBJECT(connection), "status", &status, NULL);

  if(status == INF_XML_CONNECTION_CLOSING ||
     status == INF_XML_CONNECTION_CLOSED)
  {
    priv->queries = g_slist_remove(priv->queries, query);
    inf_gtk_certificate_manager_query_free(query);
  }
}

static void
inf_gtk_certificate_manager_certificate_func(InfXmppConnection* connection,
                                             gnutls_session_t session,
                                             InfCertificateChain* chain,
                                             gpointer user_data)
{
  InfGtkCertificateManager* manager;
  InfGtkCertificateManagerPrivate* priv;

  InfGtkCertificateDialogFlags flags;
  gnutls_x509_crt_t presented_cert;
  gnutls_x509_crt_t known_cert;
  gchar* hostname;

  gboolean match_hostname;
  gboolean issuer_known;
  gnutls_x509_crt_t root_cert;

  int ret;
  unsigned int verify;
  GHashTable* table;
  gboolean cert_equal;
  time_t expiration_time;

  InfGtkCertificateManagerQuery* query;
  gchar* text;
  GtkWidget* vbox;
  GtkWidget* label;

  GError* error;

  manager = INF_GTK_CERTIFICATE_MANAGER(user_data);
  priv = INF_GTK_CERTIFICATE_MANAGER_PRIVATE(manager);

  g_object_get(G_OBJECT(connection), "remote-hostname", &hostname, NULL);
  presented_cert = inf_certificate_chain_get_own_certificate(chain);

  match_hostname = gnutls_x509_crt_check_hostname(presented_cert, hostname);

  /* First, validate the certificate */
  ret = gnutls_certificate_verify_peers2(session, &verify);
  error = NULL;

  if(ret != GNUTLS_E_SUCCESS)
    inf_gnutls_set_error(&error, ret);

  /* Remove the GNUTLS_CERT_ISSUER_NOT_KNOWN flag from the verification
   * result, and if the certificate is still invalid, then set an error. */
  if(error == NULL)
  {
    issuer_known = TRUE;
    if(verify & GNUTLS_CERT_SIGNER_NOT_FOUND)
    {
      issuer_known = FALSE;

      /* Re-validate the certificate for other failure reasons --
       * unfortunately the gnutls_certificate_verify_peers2() call
       * does not tell us whether the certificate is otherwise invalid
       * if a signer is not found already. */
      /* TODO: Here it would be good to use the verify flags from the
       * certificate credentials, but GnuTLS does not have API to
       * retrieve them. */
      root_cert = inf_certificate_chain_get_root_certificate(chain);

      ret = gnutls_x509_crt_list_verify(
        inf_certificate_chain_get_raw(chain),
        inf_certificate_chain_get_n_certificates(chain),
        &root_cert,
        1,
        NULL,
        0,
        GNUTLS_VERIFY_ALLOW_X509_V1_CA_CRT,
        &verify
      );

      if(ret != GNUTLS_E_SUCCESS)
        inf_gnutls_set_error(&error, ret);
    }

    if(error == NULL)
      if(verify & GNUTLS_CERT_INVALID)
        inf_gnutls_certificate_verification_set_error(&error, verify);
  }

  /* Look up the host in our database of pinned certificates if we could not
   * fully verify the certificate, i.e. if either the issuer is not known or
   * the hostname of the connection does not match the certificate. */
  table = NULL;
  if(error == NULL)
  {
    known_cert = NULL;
    if(!match_hostname || !issuer_known)
    {
      /* If we cannot load the known host file, then cancel the connection.
       * Otherwise it might happen that someone shows us a certificate that we
       * tell the user we don't know, if though actually for that host we expect
       * a different certificate. */
      table = inf_gtk_certificate_manager_ref_known_hosts(manager, &error);
      if(table != NULL)
        known_cert = g_hash_table_lookup(table, hostname);
    }
  }

  /* Next, configure the flags for the dialog to be shown based on the
   * verification result, and on whether the pinned certificate matches
   * the one presented by the host or not. */
  flags = 0;
  if(error == NULL)
  {
    if(known_cert != NULL)
    {
      cert_equal = inf_gtk_certificate_manager_compare_fingerprint(
        known_cert,
        presented_cert,
        &error
      );

      if(error == NULL && cert_equal == FALSE)
      {
        if(!match_hostname)
          flags |= INF_GTK_CERTIFICATE_DIALOG_CERT_HOSTNAME_MISMATCH;
        if(!issuer_known)
          flags |= INF_GTK_CERTIFICATE_DIALOG_CERT_ISSUER_NOT_KNOWN;

        flags |= INF_GTK_CERTIFICATE_DIALOG_CERT_UNEXPECTED;
        expiration_time = gnutls_x509_crt_get_expiration_time(known_cert);
        if(expiration_time != (time_t)(-1))
        {
          expiration_time -= INF_GTK_CERTIFICATE_MANAGER_EXPIRATION_TOLERANCE;
          if(time(NULL) > expiration_time)
          {
            flags |= INF_GTK_CERTIFICATE_DIALOG_CERT_OLD_EXPIRED;
          }
        }
      }
    }
    else
    {
      if(!match_hostname)
        flags |= INF_GTK_CERTIFICATE_DIALOG_CERT_HOSTNAME_MISMATCH;
      if(!issuer_known)
        flags |= INF_GTK_CERTIFICATE_DIALOG_CERT_ISSUER_NOT_KNOWN;
    }
  }

  /* Now proceed either by accepting the connection, rejecting it, or
   * bothering the user with an annoying dialog. */
  if(error == NULL)
  {
    if(flags == 0)
    {
      if(match_hostname && issuer_known)
      {
        /* Remove the pinned entry if we now have a valid certificate for
         * this host. */
        if(table != NULL && g_hash_table_remove(table, hostname) == TRUE)
        {
          inf_gtk_certificate_manager_write_known_hosts_with_warning(
            manager,
            table
          );
        }
      }

      inf_xmpp_connection_certificate_verify_continue(connection);
    }
    else
    {
      query = g_slice_new(InfGtkCertificateManagerQuery);
      query->manager = manager;
      query->known_hosts = table;
      query->connection = connection;
      query->dialog = inf_gtk_certificate_dialog_new(
        priv->parent_window,
        0,
        flags,
        hostname,
        chain
      );
      query->certificate_chain = chain;

      table = NULL;

      g_object_ref(query->connection);
      inf_certificate_chain_ref(chain);

      g_signal_connect(
        G_OBJECT(connection),
        "notify::status",
        G_CALLBACK(inf_gtk_certificate_manager_notify_status_cb),
        query
      );

      g_signal_connect(
        G_OBJECT(query->dialog),
        "response",
        G_CALLBACK(inf_gtk_certificate_manager_response_cb),
        query
      );

      gtk_dialog_add_button(
        GTK_DIALOG(query->dialog),
        _("_Cancel connection"),
        GTK_RESPONSE_REJECT
      );

      gtk_dialog_add_button(
        GTK_DIALOG(query->dialog),
        _("C_ontinue connection"),
        GTK_RESPONSE_ACCEPT
      );

      text = g_strdup_printf(
        _("Do you want to continue the connection to host \"%s\"? If you "
          "choose to continue, this certificate will be trusted in the "
          "future when connecting to this host."),
        hostname
      );

      label = gtk_label_new(text);
      gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
      gtk_label_set_line_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
      gtk_label_set_max_width_chars(GTK_LABEL(label), 60);
      gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.0);
      gtk_widget_show(label);
      g_free(text);

      vbox = gtk_dialog_get_content_area(GTK_DIALOG(query->dialog));
      gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

      priv->queries = g_slist_prepend(priv->queries, query);
      gtk_window_present(GTK_WINDOW(query->dialog));
    }
  }
  else
  {
    inf_xmpp_connection_certificate_verify_cancel(connection, error);
    g_error_free(error);
  }

  if(table != NULL) g_hash_table_unref(table);
  g_free(hostname);
}

static void
inf_gtk_certificate_manager_connection_added_cb(InfXmppManager* manager,
                                                InfXmppConnection* connection,
                                                gpointer user_data)
{
  InfXmppConnectionSite site;
  g_object_get(G_OBJECT(connection), "site", &site, NULL);

  if(site == INF_XMPP_CONNECTION_CLIENT)
  {
    inf_xmpp_connection_set_certificate_callback(
      connection,
      GNUTLS_CERT_REQUIRE, /* require a server certificate */
      inf_gtk_certificate_manager_certificate_func,
      user_data,
      NULL
    );
  }
}

static void
inf_gtk_certificate_manager_init(InfGtkCertificateManager* manager)
{
  InfGtkCertificateManagerPrivate* priv;
  priv = INF_GTK_CERTIFICATE_MANAGER_PRIVATE(manager);

  priv->parent_window = NULL;
  priv->xmpp_manager = NULL;
  priv->known_hosts_file = NULL;
}

static void
inf_gtk_certificate_manager_dispose(GObject* object)
{
  InfGtkCertificateManager* manager;
  InfGtkCertificateManagerPrivate* priv;
  GSList* item;

  manager = INF_GTK_CERTIFICATE_MANAGER(object);
  priv = INF_GTK_CERTIFICATE_MANAGER_PRIVATE(manager);

  if(priv->parent_window != NULL)
  {
    g_object_unref(priv->parent_window);
    priv->parent_window = NULL;
  }

  if(priv->xmpp_manager != NULL)
  {
    g_object_unref(priv->xmpp_manager);
    priv->xmpp_manager = NULL;
  }

  for(item = priv->queries; item != NULL; item = g_slist_next(item))
  {
    inf_gtk_certificate_manager_query_free(
      (InfGtkCertificateManagerQuery*)item->data
    );
  }

  g_slist_free(priv->queries);
  priv->queries = NULL;

  G_OBJECT_CLASS(inf_gtk_certificate_manager_parent_class)->dispose(object);
}

static void
inf_gtk_certificate_manager_finalize(GObject* object)
{
  InfGtkCertificateManager* manager;
  InfGtkCertificateManagerPrivate* priv;

  manager = INF_GTK_CERTIFICATE_MANAGER(object);
  priv = INF_GTK_CERTIFICATE_MANAGER_PRIVATE(manager);

  inf_gtk_certificate_manager_set_known_hosts(manager, NULL);
  g_assert(priv->known_hosts_file == NULL);

  G_OBJECT_CLASS(inf_gtk_certificate_manager_parent_class)->finalize(object);
}

static void
inf_gtk_certificate_manager_set_property(GObject* object,
                                         guint prop_id,
                                         const GValue* value,
                                         GParamSpec* pspec)
{
  InfGtkCertificateManager* manager;
  InfGtkCertificateManagerPrivate* priv;

  manager = INF_GTK_CERTIFICATE_MANAGER(object);
  priv = INF_GTK_CERTIFICATE_MANAGER_PRIVATE(manager);

  switch(prop_id)
  {
  case PROP_PARENT_WINDOW:
    g_assert(priv->parent_window == NULL); /* construct/only */
    priv->parent_window = GTK_WINDOW(g_value_dup_object(value));
    break;
  case PROP_XMPP_MANAGER:
    g_assert(priv->xmpp_manager == NULL); /* construct/only */
    priv->xmpp_manager = INF_XMPP_MANAGER(g_value_dup_object(value));

    g_signal_connect(
      G_OBJECT(priv->xmpp_manager),
      "connection-added",
      G_CALLBACK(inf_gtk_certificate_manager_connection_added_cb),
      manager
    );

    break;
  case PROP_KNOWN_HOSTS_FILE:
    inf_gtk_certificate_manager_set_known_hosts(
      manager,
      g_value_get_string(value)
    );

    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_gtk_certificate_manager_get_property(GObject* object,
                                         guint prop_id,
                                         GValue* value,
                                         GParamSpec* pspec)
{
  InfGtkCertificateManager* manager;
  InfGtkCertificateManagerPrivate* priv;

  manager = INF_GTK_CERTIFICATE_MANAGER(object);
  priv = INF_GTK_CERTIFICATE_MANAGER_PRIVATE(manager);

  switch(prop_id)
  {
  case PROP_PARENT_WINDOW:
    g_value_set_object(value, G_OBJECT(priv->parent_window));
    break;
  case PROP_XMPP_MANAGER:
    g_value_set_object(value, G_OBJECT(priv->xmpp_manager));
    break;
  case PROP_KNOWN_HOSTS_FILE:
    g_value_set_string(value, priv->known_hosts_file);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

/*
 * GType registration
 */

static void
inf_gtk_certificate_manager_class_init(
  InfGtkCertificateManagerClass* certificate_manager_class)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(certificate_manager_class);

  object_class->dispose = inf_gtk_certificate_manager_dispose;
  object_class->finalize = inf_gtk_certificate_manager_finalize;
  object_class->set_property = inf_gtk_certificate_manager_set_property;
  object_class->get_property = inf_gtk_certificate_manager_get_property;

  g_object_class_install_property(
    object_class,
    PROP_PARENT_WINDOW,
    g_param_spec_object(
      "parent-window",
      "Parent window",
      "The parent window for certificate approval dialogs",
      GTK_TYPE_WINDOW,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_XMPP_MANAGER,
    g_param_spec_object(
      "xmpp-manager",
      "XMPP manager",
      "The XMPP manager of registered connections",
      INF_TYPE_XMPP_MANAGER,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_KNOWN_HOSTS_FILE,
    g_param_spec_string(
      "known-hosts-file",
      "Known hosts file",
      "File containing certificates of known hosts",
      NULL,
      G_PARAM_READWRITE
    )
  );
}

/*
 * Public API.
 */

/**
 * inf_gtk_certificate_manager_new: (constructor)
 * @parent_window: The #GtkWindow to which to make certificate approval
 * dialogs transient to.
 * @xmpp_manager: The #InfXmppManager whose #InfXmppConnection<!-- -->s to
 * manage the certificates for.
 * @known_hosts_file: (type filename) (allow-none): Path pointing to a file
 * that contains certificates of known hosts, or %NULL.
 *
 * Creates a new #InfGtkCertificateManager. For each new client-side
 * #InfXmppConnection in @xmpp_manager, the certificate manager will verify
 * the server's certificate.
 *
 * If the certificate is contained in @known_hosts_file, then
 * the certificate is accepted automatically. Otherwise, the user is asked for
 * approval. If the user approves the certificate, then it is inserted into
 * the @known_hosts_file.
 *
 * Returns: (transfer full): A new #InfGtkCertificateManager.
 **/
InfGtkCertificateManager*
inf_gtk_certificate_manager_new(GtkWindow* parent_window,
                                InfXmppManager* xmpp_manager,
                                const gchar* known_hosts_file)
{
  GObject* object;

  object = g_object_new(
    INF_GTK_TYPE_CERTIFICATE_MANAGER,
    "parent-window", parent_window,
    "xmpp-manager", xmpp_manager,
    "known-hosts-file", known_hosts_file,
    NULL
  );

  return INF_GTK_CERTIFICATE_MANAGER(object);
}

/* vim:set et sw=2 ts=2: */
