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

#ifndef __INF_XMPP_CONNECTION_H__
#define __INF_XMPP_CONNECTION_H__

#include <libinfinity/common/inf-tcp-connection.h>
#include <libinfinity/common/inf-certificate-chain.h>
#include <libinfinity/common/inf-certificate-credentials.h>
#include <libinfinity/common/inf-sasl-context.h>

#include <unistd.h> /* Get ssize_t on MSVC, required by gnutls.h */
#include <gnutls/gnutls.h>
/*#include <gsasl.h>*/

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_XMPP_CONNECTION                 (inf_xmpp_connection_get_type())
#define INF_XMPP_CONNECTION(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_XMPP_CONNECTION, InfXmppConnection))
#define INF_XMPP_CONNECTION_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TYPE_XMPP_CONNECTION, InfXmppConnectionClass))
#define INF_IS_XMPP_CONNECTION(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_XMPP_CONNECTION))
#define INF_IS_XMPP_CONNECTION_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TYPE_XMPP_CONNECTION))
#define INF_XMPP_CONNECTION_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TYPE_XMPP_CONNECTION, InfXmppConnectionClass))

#define INF_TYPE_XMPP_CONNECTION_SITE            (inf_xmpp_connection_site_get_type())
#define INF_TYPE_XMPP_CONNECTION_SECURITY_POLICY (inf_xmpp_connection_security_policy_get_type())

typedef struct _InfXmppConnection InfXmppConnection;
typedef struct _InfXmppConnectionClass InfXmppConnectionClass;

typedef enum _InfXmppConnectionSite {
  INF_XMPP_CONNECTION_SERVER,
  INF_XMPP_CONNECTION_CLIENT
} InfXmppConnectionSite;

typedef enum _InfXmppConnectionSecurityPolicy {
  /* Server: Do not offer TLS.
   * Client: Only connect if TLS is not required. */
  INF_XMPP_CONNECTION_SECURITY_ONLY_UNSECURED,
  /* Server: Require TLS.
   * Client: Only connect if TLS is available. */
  INF_XMPP_CONNECTION_SECURITY_ONLY_TLS,
  /* Server: Offer both.
   * Client: Use unsecured communication unless TLS is required */
  INF_XMPP_CONNECTION_SECURITY_BOTH_PREFER_UNSECURED,
  /* Server: Offer both.
   * Client: Use TLS-secured communication unless TLS is not available. */
  INF_XMPP_CONNECTION_SECURITY_BOTH_PREFER_TLS
} InfXmppConnectionSecurityPolicy;

typedef enum _InfXmppConnectionError {
  /* Server does not support TLS */
  INF_XMPP_CONNECTION_ERROR_TLS_UNSUPPORTED,
  /* The server requires TLS, but we don't want TLS */
  INF_XMPP_CONNECTION_ERROR_TLS_REQUIRED,
  /* Got <failure> as response to <starttls> */
  INF_XMPP_CONNECTION_ERROR_TLS_FAILURE,
  /* The server did not provide a certificate */
  INF_XMPP_CONNECTION_ERROR_NO_CERTIFICATE_PROVIDED,
  /* The server certificate is not trusted */
  INF_XMPP_CONNECTION_ERROR_CERTIFICATE_NOT_TRUSTED,
  /* Server does not provide authentication mechanisms */
  INF_XMPP_CONNECTION_ERROR_AUTHENTICATION_UNSUPPORTED,
  /* Server does not offer a suitable machnism */
  INF_XMPP_CONNECTION_ERROR_NO_SUITABLE_MECHANISM,

  INF_XMPP_CONNECTION_ERROR_FAILED
} InfXmppConnectionError;

/* As defined in RFC 3920, section 4.7.3 */
typedef enum _InfXmppConnectionStreamError {
  INF_XMPP_CONNECTION_STREAM_ERROR_BAD_FORMAT,
  INF_XMPP_CONNECTION_STREAM_ERROR_BAD_NAMESPACE_PREFIX,
  INF_XMPP_CONNECTION_STREAM_ERROR_CONFLICT,
  INF_XMPP_CONNECTION_STREAM_ERROR_CONNECTION_TIMEOUT,
  INF_XMPP_CONNECTION_STREAM_ERROR_HOST_GONE,
  INF_XMPP_CONNECTION_STREAM_ERROR_HOST_UNKNOWN,
  INF_XMPP_CONNECTION_STREAM_ERROR_IMPROPER_ADDRESSING,
  INF_XMPP_CONNECTION_STREAM_ERROR_INTERNAL_SERVER_ERROR,
  INF_XMPP_CONNECTION_STREAM_ERROR_INVALID_FROM,
  INF_XMPP_CONNECTION_STREAM_ERROR_INVALID_ID,
  INF_XMPP_CONNECTION_STREAM_ERROR_INVALID_NAMESPACE,
  INF_XMPP_CONNECTION_STREAM_ERROR_INVALID_XML,
  INF_XMPP_CONNECTION_STREAM_ERROR_NOT_AUTHORIZED,
  INF_XMPP_CONNECTION_STREAM_ERROR_POLICY_VIOLATION,
  INF_XMPP_CONNECTION_STREAM_ERROR_REMOTE_CONNECTION_FAILED,
  INF_XMPP_CONNECTION_STREAM_ERROR_RESOURCE_CONSTRAINT,
  INF_XMPP_CONNECTION_STREAM_ERROR_RESTRICTED_XML,
  INF_XMPP_CONNECTION_STREAM_ERROR_SEE_OTHER_HOST,
  INF_XMPP_CONNECTION_STREAM_ERROR_SYSTEM_SHUTDOWN,
  INF_XMPP_CONNECTION_STREAM_ERROR_UNDEFINED_CONDITION,
  INF_XMPP_CONNECTION_STREAM_ERROR_UNSUPPORTED_ENCODING,
  INF_XMPP_CONNECTION_STREAM_ERROR_UNSUPPORTED_STANZA_TYPE,
  INF_XMPP_CONNECTION_STREAM_ERROR_UNSUPPORTED_VERSION,
  INF_XMPP_CONNECTION_STREAM_ERROR_XML_NOT_WELL_FORMED,

  INF_XMPP_CONNECTION_STREAM_ERROR_FAILED
} InfXmppConnectionStreamError;

/* As defined in RFC 3920, section 6.4 */
typedef enum _InfXmppConnectionAuthError {
  INF_XMPP_CONNECTION_AUTH_ERROR_ABORTED,
  INF_XMPP_CONNECTION_AUTH_ERROR_INCORRECT_ENCODING,
  INF_XMPP_CONNECTION_AUTH_ERROR_INVALID_AUTHZID,
  INF_XMPP_CONNECTION_AUTH_ERROR_INVALID_MECHANISM,
  INF_XMPP_CONNECTION_AUTH_ERROR_MECHANISM_TOO_WEAK,
  INF_XMPP_CONNECTION_AUTH_ERROR_NOT_AUTHORIZED,
  INF_XMPP_CONNECTION_AUTH_ERROR_TEMPORARY_AUTH_FAILURE,

  INF_XMPP_CONNECTION_AUTH_ERROR_FAILED
} InfXmppConnectionAuthError;


struct _InfXmppConnectionClass {
  GObjectClass parent_class;
};

struct _InfXmppConnection {
  GObject parent;
};

typedef void(*InfXmppConnectionCrtCallback)(InfXmppConnection* xmpp,
                                            gnutls_session_t session,
                                            InfCertificateChain* chain,
                                            gpointer user_data);

GType
inf_xmpp_connection_security_policy_get_type(void) G_GNUC_CONST;

GType
inf_xmpp_connection_site_get_type(void) G_GNUC_CONST;

GType
inf_xmpp_connection_get_type(void) G_GNUC_CONST;

InfXmppConnection*
inf_xmpp_connection_new(InfTcpConnection* tcp,
                        InfXmppConnectionSite site,
                        const gchar* local_hostname,
                        const gchar* remote_hostname,
                        InfXmppConnectionSecurityPolicy security_policy,
                        InfCertificateCredentials* creds,
                        InfSaslContext* sasl_context,
                        const gchar* sasl_mechanisms);

gboolean
inf_xmpp_connection_get_tls_enabled(InfXmppConnection* xmpp);

gnutls_x509_crt_t
inf_xmpp_connection_get_own_certificate(InfXmppConnection* xmpp);

InfCertificateChain*
inf_xmpp_connection_get_peer_certificate(InfXmppConnection* xmpp);

void
inf_xmpp_connection_set_certificate_callback(InfXmppConnection* xmpp,
                                             InfXmppConnectionCrtCallback cb,
                                             gpointer user_data);

void
inf_xmpp_connection_certificate_verify_continue(InfXmppConnection* xmpp);

void
inf_xmpp_connection_certificate_verify_cancel(InfXmppConnection* xmpp);

void
inf_xmpp_connection_reset_sasl_authentication(InfXmppConnection* xmpp,
                                              InfSaslContext* new_context,
                                              const gchar* new_mechanisms);

gboolean
inf_xmpp_connection_retry_sasl_authentication(InfXmppConnection* xmpp,
                                              GError** error);

void
inf_xmpp_connection_set_sasl_error(InfXmppConnection* xmpp,
                                   const GError* error);

const GError*
inf_xmpp_connection_get_sasl_error(InfXmppConnection* xmpp);

G_END_DECLS

#endif /* __INF_XMPP_CONNECTION_H__ */

/* vim:set et sw=2 ts=2: */
