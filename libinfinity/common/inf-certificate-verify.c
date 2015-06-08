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
 * SECTION:inf-certificate-verify
 * @title: InfCertificateVerify
 * @short_description: Server certificate verification.
 * @include: libinfinity/common/inf-certificate-verify.h
 * @see_also: #InfXmppConnection
 * @stability: Unstable
 *
 * #InfCertificateVerify attempts to verify server certificates. It is
 * associated to a #InfXmppManager, and all client-side connections managed
 * by that manager will be checked.
 *
 * The policy enforced by #InfCertificateVerify is such that if the
 * certificate is not valid, signed with a weak algorithm, expired, or not
 * yet activated, to reject the certificate. On the other hand, if the
 * certificate itself is valid and the hostname that it is issued to matches
 * the hostname of the connection, and its CA is trusted, then the certificate
 * is accepted.
 *
 * However, if the certificate as such is valid but either the hostname
 * does not match or the CA is not trusted (for example if a self-signed
 * certificate is used), then certificate pinning takes effect. If the
 * certificate is not pinned, the #InfCertificateVerify::check-certificate
 * is emitted, which should cause the certificate to be checked manually,
 * e.g. by presenting it to the user. Once the check is complete, call
 * inf_certificate_verify_checked(). If the check is positive, the certificate
 * is pinned, so that next time a connection to the same hostname presents
 * the same certificate, it is accepted automatically. If a different
 * certificate than the pinned one is being presented, then
 * the #InfCertificateVerify::check-certificate signal is emitted again.
 */

/* TODO: OCSP. We probably should only do OCSP stapling, and support
 * OCSP MUST STAPLE. Not all can be implemented here, but if the server's
 * certificate has OCSP MUST STAPLE set and no good OCSP response is
 * stapled, then reject the certificate. Also, always request a stapled OCSP
 * response. If OCSP MUST STAPLE is not set, and no OCSP response is
 * stapled, then accept the connection nevertheless (soft fail). The policy
 * is that if you want hard-fail, then use OCSP-MUST-STAPLE. */

#include <libinfinity/common/inf-certificate-verify.h>
#include <libinfinity/common/inf-cert-util.h>
#include <libinfinity/common/inf-file-util.h>
#include <libinfinity/common/inf-xml-connection.h>
#include <libinfinity/common/inf-error.h>
#include <libinfinity/inf-define-enum.h>
#include <libinfinity/inf-signals.h>
#include <libinfinity/inf-i18n.h>

#include <gnutls/x509.h>

static const GFlagsValue inf_certificate_verify_flags_values[] = {
  {
    INF_CERTIFICATE_VERIFY_HOSTNAME_MISMATCH,
    "INF_CERTIFICATE_VERIFY_HOSTNAME_MISMATCH",
    "hostname-mismatch"
  }, {
    INF_CERTIFICATE_VERIFY_ISSUER_NOT_KNOWN,
    "INF_CERTIFICATE_VERIFY_ISSUER_NOT_KNOWN",
    "issuer-not-known"
  }, {
    INF_CERTIFICATE_VERIFY_NOT_PINNED,
    "INF_CERTIFICATE_VERIFY_NOT_PINNED",
    "not-pinned"
  }, {
    0,
    NULL,
    NULL
  }
};

typedef struct _InfCertificateVerifyQuery InfCertificateVerifyQuery;
struct _InfCertificateVerifyQuery {
  InfCertificateVerify* verify;
  GHashTable* known_hosts;
  InfXmppConnection* connection;
  InfCertificateChain* certificate_chain;
};

typedef struct _InfCertificateVerifyPrivate InfCertificateVerifyPrivate;
struct _InfCertificateVerifyPrivate {
  InfXmppManager* xmpp_manager;
  gchar* known_hosts_filename;
  GSList* queries;
};

enum {
  PROP_0,

  PROP_XMPP_MANAGER,
  PROP_KNOWN_HOSTS_FILE
};

enum {
  CHECK_CERTIFICATE,
  CHECK_CANCELLED,

  LAST_SIGNAL
};

static guint certificate_verify_signals[LAST_SIGNAL];

#define INF_CERTIFICATE_VERIFY_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TYPE_CERTIFICATE_VERIFY, InfCertificateVerifyPrivate))

INF_DEFINE_FLAGS_TYPE(InfCertificateVerifyFlags, inf_certificate_verify_flags, inf_certificate_verify_flags_values)
G_DEFINE_TYPE_WITH_CODE(InfCertificateVerify, inf_certificate_verify, G_TYPE_OBJECT,
  G_ADD_PRIVATE(InfCertificateVerify))

static InfCertificateVerifyQuery*
inf_certificate_verify_find_query(InfCertificateVerify* verify,
                                  InfXmppConnection* connection)
{
  InfCertificateVerifyPrivate* priv;
  GSList* item;
  InfCertificateVerifyQuery* query;

  priv = INF_CERTIFICATE_VERIFY_PRIVATE(verify);
  for(item = priv->queries; item != NULL; item == item->next)
  {
    query = (InfCertificateVerifyQuery*)item->data;
    if(query->connection == connection)
      return query;
  }

  return NULL;
}

static void
inf_certificate_verify_notify_status_cb(GObject* object,
                                        GParamSpec* pspec,
                                        gpointer user_data);

static void
inf_certificate_verify_query_free(InfCertificateVerifyQuery* query,
                                  gboolean emit_cancelled)
{
  InfCertificateVerify* verify;
  InfXmppConnection* connection;

  verify = query->verify;

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(query->connection),
    G_CALLBACK(inf_certificate_verify_notify_status_cb),
    query
  );

  connection = query->connection;
  inf_certificate_chain_unref(query->certificate_chain);
  g_hash_table_unref(query->known_hosts);
  g_slice_free(InfCertificateVerifyQuery, query);

  if(emit_cancelled)
  {
    g_signal_emit(
      verify,
      certificate_verify_signals[CHECK_CANCELLED],
      0,
      connection
    );
  }

  g_object_unref(connection);
}

static void
inf_certificate_verify_set_known_hosts(InfCertificateVerify* verify,
                                       const gchar* known_hosts_filename)
{
  InfCertificateVerifyPrivate* priv;
  priv = INF_CERTIFICATE_VERIFY_PRIVATE(verify);

  /* TODO: If there are running queries, the we need to load the new hosts
   * file and then change it in all queries. */
  g_assert(priv->queries == NULL);

  g_free(priv->known_hosts_filename);
  priv->known_hosts_filename = g_strdup(known_hosts_filename);
}

static GHashTable*
inf_certificate_verify_ref_known_hosts(InfCertificateVerify* verify,
                                       GError** error)
{
  InfCertificateVerifyPrivate* priv;
  InfCertificateVerifyQuery* query;

  priv = INF_CERTIFICATE_VERIFY_PRIVATE(verify);
  if(priv->queries != NULL)
  {
    query = (InfCertificateVerifyQuery*)priv->queries->data;
    g_hash_table_ref(query->known_hosts);
    return query->known_hosts;
  }
  else
  {
    return inf_cert_util_read_certificate_map(
      priv->known_hosts_filename,
      error
    );
  }
}

static gboolean
inf_certificate_verify_write_known_hosts(InfCertificateVerify* verify,
                                         GHashTable* table,
                                         GError** error)
{
  InfCertificateVerifyPrivate* priv;
  gchar* dirname;

  priv = INF_CERTIFICATE_VERIFY_PRIVATE(verify);
  
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
  dirname = g_path_get_dirname(priv->known_hosts_filename);
  if(!inf_file_util_create_directory(dirname, 0755, error))
  {
    g_free(dirname);
    return FALSE;
  }

  g_free(dirname);

  return inf_cert_util_write_certificate_map(
    table,
    priv->known_hosts_filename,
    error
  );
}

static void
inf_certificate_verify_write_known_hosts_with_warning(
  InfCertificateVerify* verify,
  GHashTable* table)
{
  InfCertificateVerifyPrivate* priv;
  GError* error;
  gboolean result;

  priv = INF_CERTIFICATE_VERIFY_PRIVATE(verify);
  error = NULL;

  result = inf_certificate_verify_write_known_hosts(verify, table, &error);

  if(error != NULL)
  {
    g_warning(
      _("Failed to write file with known hosts \"%s\": %s"),
      priv->known_hosts_filename,
      error->message
    );

    g_error_free(error);
  }
}

static void
inf_certificate_verify_notify_status_cb(GObject* object,
                                        GParamSpec* pspec,
                                        gpointer user_data)
{
  InfCertificateVerifyQuery* query;
  InfCertificateVerifyPrivate* priv;
  InfXmppConnection* connection;
  InfXmlConnectionStatus status;

  query = (InfCertificateVerifyQuery*)user_data;
  priv = INF_CERTIFICATE_VERIFY_PRIVATE(query->verify);
  connection = INF_XMPP_CONNECTION(object);

  g_object_get(G_OBJECT(connection), "status", &status, NULL);

  if(status == INF_XML_CONNECTION_CLOSING ||
     status == INF_XML_CONNECTION_CLOSED)
  {
    priv->queries = g_slist_remove(priv->queries, query);
    inf_certificate_verify_query_free(query, TRUE);
  }
}

static void
inf_certificate_verify_certificate_func(InfXmppConnection* connection,
                                        gnutls_session_t session,
                                        InfCertificateChain* chain,
                                        gpointer user_data)
{
  InfCertificateVerify* verify;
  InfCertificateVerifyPrivate* priv;

  InfCertificateVerifyFlags flags;
  gnutls_x509_crt_t presented_cert;
  gnutls_x509_crt_t known_cert;
  gchar* hostname;

  gboolean match_hostname;
  gboolean issuer_known;
  gnutls_x509_crt_t root_cert;

  int ret;
  unsigned int verify_result;
  GHashTable* table;
  gboolean cert_equal;
  time_t expiration_time;

  InfCertificateVerifyQuery* query;
  GError* error;

  verify = INF_CERTIFICATE_VERIFY(user_data);
  priv = INF_CERTIFICATE_VERIFY_PRIVATE(verify);

  g_object_get(G_OBJECT(connection), "remote-hostname", &hostname, NULL);
  presented_cert = inf_certificate_chain_get_own_certificate(chain);

  match_hostname = gnutls_x509_crt_check_hostname(presented_cert, hostname);

  /* First, validate the certificate */
  ret = gnutls_certificate_verify_peers2(session, &verify_result);
  error = NULL;

  if(ret != GNUTLS_E_SUCCESS)
    inf_gnutls_set_error(&error, ret);

  /* Remove the GNUTLS_CERT_ISSUER_NOT_KNOWN flag from the verification
   * result, and if the certificate is still invalid, then set an error. */
  if(error == NULL)
  {
    issuer_known = TRUE;
    if(verify_result & GNUTLS_CERT_SIGNER_NOT_FOUND)
    {
      issuer_known = FALSE;

      /* Re-validate the certificate for other failure reasons --
       * unfortunately the gnutls_certificate_verify_peers2() call
       * does not tell us whether the certificate is otherwise invalid
       * if a signer is not found already. */
      /* TODO: The above has been changed with GnuTLS 3.4.0 */
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
        &verify_result
      );

      if(ret != GNUTLS_E_SUCCESS)
        inf_gnutls_set_error(&error, ret);
    }

    if(error == NULL)
      if(verify_result & GNUTLS_CERT_INVALID)
        inf_gnutls_certificate_verification_set_error(&error, verify_result);
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
       * tell the user we don't know, if though actually for that host we
       * expect a different certificate. */
      table = inf_certificate_verify_ref_known_hosts(verify, &error);
      if(table != NULL)
        known_cert = g_hash_table_lookup(table, hostname);
    }
    else
    {
      /* Load the known hosts file so that we can remove the entry belonging
       * to this host from it, if it's in there. However, it does not really
       * matter whether opening the file succeeds or not. */
      table = inf_certificate_verify_ref_known_hosts(verify, NULL);
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
      cert_equal = inf_cert_util_compare_fingerprint(
        known_cert,
        presented_cert,
        &error
      );

      if(error == NULL && cert_equal == FALSE)
      {
        if(!match_hostname)
          flags |= INF_CERTIFICATE_VERIFY_HOSTNAME_MISMATCH;
        if(!issuer_known)
          flags |= INF_CERTIFICATE_VERIFY_ISSUER_NOT_KNOWN;

        flags |= INF_CERTIFICATE_VERIFY_NOT_PINNED;
      }
    }
    else
    {
      if(!match_hostname)
        flags |= INF_CERTIFICATE_VERIFY_HOSTNAME_MISMATCH;
      if(!issuer_known)
        flags |= INF_CERTIFICATE_VERIFY_ISSUER_NOT_KNOWN;
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
          inf_certificate_verify_write_known_hosts_with_warning(
            verify,
            table
          );
        }
      }

      inf_xmpp_connection_certificate_verify_continue(connection);
    }
    else
    {
      query = g_slice_new(InfCertificateVerifyQuery);
      query->verify = verify;
      query->known_hosts = table;
      query->connection = connection;
      query->certificate_chain = chain;

      table = NULL;

      g_object_ref(query->connection);
      inf_certificate_chain_ref(chain);
      priv->queries = g_slist_prepend(priv->queries, query);

      g_signal_connect(
        G_OBJECT(connection),
        "notify::status",
        G_CALLBACK(inf_certificate_verify_notify_status_cb),
        query
      );

      g_signal_emit(
        G_OBJECT(verify),
        certificate_verify_signals[CHECK_CERTIFICATE],
        0,
        connection,
        chain,
        known_cert,
        flags
      );
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
inf_certificate_verify_connection_added_cb(InfXmppManager* manager,
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
      inf_certificate_verify_certificate_func,
      user_data,
      NULL
    );
  }
}

static void
inf_certificate_verify_init(InfCertificateVerify* verify)
{
  InfCertificateVerifyPrivate* priv;
  priv = INF_CERTIFICATE_VERIFY_PRIVATE(verify);

  priv->xmpp_manager = NULL;
  priv->known_hosts_filename = NULL;
}

static void
inf_certificate_verify_dispose(GObject* object)
{
  InfCertificateVerify* verify;
  InfCertificateVerifyPrivate* priv;
  GSList* item;

  verify = INF_CERTIFICATE_VERIFY(object);
  priv = INF_CERTIFICATE_VERIFY_PRIVATE(verify);

  if(priv->xmpp_manager != NULL)
  {
    g_object_unref(priv->xmpp_manager);
    priv->xmpp_manager = NULL;
  }

  for(item = priv->queries; item != NULL; item = g_slist_next(item))
  {
    inf_certificate_verify_query_free(
      (InfCertificateVerifyQuery*)item->data,
      TRUE
    );
  }

  g_slist_free(priv->queries);
  priv->queries = NULL;

  G_OBJECT_CLASS(inf_certificate_verify_parent_class)->dispose(object);
}

static void
inf_certificate_verify_finalize(GObject* object)
{
  InfCertificateVerify* verify;
  InfCertificateVerifyPrivate* priv;

  verify = INF_CERTIFICATE_VERIFY(object);
  priv = INF_CERTIFICATE_VERIFY_PRIVATE(verify);

  inf_certificate_verify_set_known_hosts(verify, NULL);
  g_assert(priv->known_hosts_filename == NULL);

  G_OBJECT_CLASS(inf_certificate_verify_parent_class)->finalize(object);
}

static void
inf_certificate_verify_set_property(GObject* object,
                                    guint prop_id,
                                    const GValue* value,
                                    GParamSpec* pspec)
{
  InfCertificateVerify* verify;
  InfCertificateVerifyPrivate* priv;

  verify = INF_CERTIFICATE_VERIFY(object);
  priv = INF_CERTIFICATE_VERIFY_PRIVATE(verify);

  switch(prop_id)
  {
  case PROP_XMPP_MANAGER:
    g_assert(priv->xmpp_manager == NULL); /* construct/only */
    priv->xmpp_manager = INF_XMPP_MANAGER(g_value_dup_object(value));

    g_signal_connect(
      G_OBJECT(priv->xmpp_manager),
      "connection-added",
      G_CALLBACK(inf_certificate_verify_connection_added_cb),
      verify
    );

    break;
  case PROP_KNOWN_HOSTS_FILE:
    inf_certificate_verify_set_known_hosts(
      verify,
      g_value_get_string(value)
    );

    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_certificate_verify_get_property(GObject* object,
                                    guint prop_id,
                                    GValue* value,
                                    GParamSpec* pspec)
{
  InfCertificateVerify* verify;
  InfCertificateVerifyPrivate* priv;

  verify = INF_CERTIFICATE_VERIFY(object);
  priv = INF_CERTIFICATE_VERIFY_PRIVATE(verify);

  switch(prop_id)
  {
  case PROP_XMPP_MANAGER:
    g_value_set_object(value, G_OBJECT(priv->xmpp_manager));
    break;
  case PROP_KNOWN_HOSTS_FILE:
    g_value_set_string(value, priv->known_hosts_filename);
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
inf_certificate_verify_class_init(
  InfCertificateVerifyClass* certificate_verify_class)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(certificate_verify_class);

  object_class->dispose = inf_certificate_verify_dispose;
  object_class->finalize = inf_certificate_verify_finalize;
  object_class->set_property = inf_certificate_verify_set_property;
  object_class->get_property = inf_certificate_verify_get_property;

  /**
   * InfCertificateVerify::check-certificate:
   * @verify: The #InfCertificateVerify emitting the signal.
   * @connection: The connection to the remote host whose certificate is
   * being verified.
   * @certificate_chain: The certificate chain presented by the remote host.
   * @pinned_certificate: The certificate that we have pinned for this
   * host, or %NULL.
   * @flags: Flags explaining why this certificate needs to be checked.
   *
   * This signal is emitted every time a certificate presented by a remote
   * host needs to be checked manually. This happens when the issuer of the
   * certificate is not in the list of trusted CAs, or the certificate was
   * issued for a different hostname than we expected. In this case, if the
   * certificate is accepted manually, it will be pinned, so that next time a
   * connection is made to the same host under the same hostname, the
   * certificate is accepted automatically.
   *
   * Consumers of this class should listen to this signal and call
   * inf_certificate_verify_checked() when they finished the manual
   * certificate check.
   *
   * The @pinned_certificate instance is valid until either
   * inf_certificate_verify_checked() is called or the
   * #InfCertificateVerify::check-cancelled signal is emitted.
   */
  certificate_verify_signals[CHECK_CERTIFICATE] = g_signal_new(
    "check-certificate",
    INF_TYPE_CERTIFICATE_VERIFY,
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfCertificateVerifyClass, check_certificate),
    NULL, NULL,
    NULL,
    G_TYPE_NONE,
    4,
    INF_TYPE_XMPP_CONNECTION,
    INF_TYPE_CERTIFICATE_CHAIN,
    G_TYPE_POINTER,
    INF_TYPE_CERTIFICATE_VERIFY_FLAGS
  );

  /**
   * InfCertificateVerify::check-cancelled:
   * @verify: The #InfCertificateVerify emitting the signal.
   * @connection: The connection to the remote host whose certificate is
   * being verified.
   *
   * This signal is emitted when a manual certificate check as initiated
   * with the #InfCertificateVerify::check-certificate signal should be
   * cancelled. After this signal has been emitted,
   * inf_certificate_verify_checked() should not be called anymore for the
   * specified connection.
   *
   * This typically happens when the connection to the remote host is lost
   * while the certificate check is in progress.
   */
  certificate_verify_signals[CHECK_CANCELLED] = g_signal_new(
    "check-cancelled",
    INF_TYPE_CERTIFICATE_VERIFY,
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfCertificateVerifyClass, check_cancelled),
    NULL, NULL,
    NULL,
    G_TYPE_NONE,
    1,
    INF_TYPE_XMPP_CONNECTION
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
 * inf_certificate_verify_new: (constructor)
 * @xmpp_manager: The #InfXmppManager whose #InfXmppConnection<!-- -->s to
 * manage the certificates for.
 * @known_hosts_file: (type filename) (allow-none): Path pointing to a file
 * that contains certificates of known hosts, or %NULL.
 *
 * Creates a new #InfCertificateVerify. For each new client-side
 * #InfXmppConnection in @xmpp_manager, the certificate manager will verify
 * the server's certificate.
 *
 * If the certificate is contained in @known_hosts_file, or is issued by a
 * trusted CA, then the certificate is accepted automatically. Otherwise,
 * the #InfCertificateVerify::check-certificate signal is emitted for a manual
 * check of the certificate to be performed.
 *
 * Returns: (transfer full): A new #InfCertificateVerify.
 **/
InfCertificateVerify*
inf_certificate_verify_new(InfXmppManager* xmpp_manager,
                           const gchar* known_hosts_file)
{
  GObject* object;

  g_return_val_if_fail(INF_IS_XMPP_MANAGER(xmpp_manager), NULL);

  object = g_object_new(
    INF_TYPE_CERTIFICATE_VERIFY,
    "xmpp-manager", xmpp_manager,
    "known-hosts-file", known_hosts_file,
    NULL
  );

  return INF_CERTIFICATE_VERIFY(object);
}

/**
 * inf_certificate_verify_checked:
 * @verify: A #InfCertificateVerify.
 * @connection: The #InfXmppConnection whose certificate has been checked.
 * @result: %TRUE if the certificate of @connection should be accepted, or
 * %FALSE if it should be rejected.
 *
 * This function should be called as a response to the
 * #InfCertificateVerify::check-certificate signal being emitted.
 */
void
inf_certificate_verify_checked(InfCertificateVerify* verify,
                               InfXmppConnection* connection,
                               gboolean result)
{
  InfCertificateVerifyQuery* query;
  InfCertificateVerifyPrivate* priv;

  gchar* hostname;
  gnutls_x509_crt_t cert;
  gnutls_x509_crt_t known_cert;
  GError* error;
  gboolean cert_equal;

  g_return_if_fail(INF_IS_CERTIFICATE_VERIFY(verify));
  g_return_if_fail(INF_IS_XMPP_CONNECTION(connection));

  query = inf_certificate_verify_find_query(verify, connection);
  g_return_if_fail(query != NULL);
  
  priv = INF_CERTIFICATE_VERIFY_PRIVATE(query->verify);

  g_object_ref(connection);

  if(result == TRUE)
  {
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
      cert_equal = inf_cert_util_compare_fingerprint(
        cert,
        known_cert,
        &error
      );
    }

    if(error != NULL)
    {
      g_warning(
        _("Failed to add certificate to list of pinned certificates: %s"),
        error->message
      );
    }
    else if(!cert_equal)
    {
      cert = inf_cert_util_copy_certificate(cert, &error);
      g_hash_table_insert(query->known_hosts, hostname, cert);

      inf_certificate_verify_write_known_hosts_with_warning(
        query->verify,
        query->known_hosts
      );
    }
    else
    {
      g_free(hostname);
    }
  }

  priv->queries = g_slist_remove(priv->queries, query);
  inf_certificate_verify_query_free(query, FALSE);
  
  if(result == TRUE)
    inf_xmpp_connection_certificate_verify_continue(connection);
  else
    inf_xmpp_connection_certificate_verify_cancel(connection, NULL);

  g_object_unref(connection);
}

/* vim:set et sw=2 ts=2: */
