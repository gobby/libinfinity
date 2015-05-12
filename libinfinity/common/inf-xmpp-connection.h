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

/**
 * InfXmppConnectionSite:
 * @INF_XMPP_CONNECTION_SERVER: The local site of this connection is an
 * XMPP server and the remote counterpart is a client.
 * @INF_XMPP_CONNECTION_CLIENT: The local site of this connection is a
 * XMPP client and the remote counterpart is a server.
 *
 * Specifies whether the local site of the connection is a client or a
 * server.
 */
typedef enum _InfXmppConnectionSite {
  INF_XMPP_CONNECTION_SERVER,
  INF_XMPP_CONNECTION_CLIENT
} InfXmppConnectionSite;

/**
 * InfXmppConnectionSecurityPolicy:
 * @INF_XMPP_CONNECTION_SECURITY_ONLY_UNSECURED: In the case of a server, do
 * not offer TLS to the client. As a client, only connect if the server does
 * not require TLS.
 * @INF_XMPP_CONNECTION_SECURITY_ONLY_TLS: In the case of a server, require
 * all connections to be TLS-secured. As a client, only connect if the server
 * supports TLS.
 * @INF_XMPP_CONNECTION_SECURITY_BOTH_PREFER_UNSECURED: In the case of a
 * server, offer both unsecured and secured messaging to the client. As a
 * client, use unsecured communication unless TLS is required by the server.
 * @INF_XMPP_CONNECTION_SECURITY_BOTH_PREFER_TLS: In the case of a server,
 * offer both unsecured and secured messaging to the client. As a client,
 * use TLS unless not supported by the server.
 *
 * The #InfXmppConnectionSecurityPolicy enumeration specifies various options
 * of how to deal with the other site allowing or requiring TLS-secured
 * connections. Note that if the local site is a server, then
 * @INF_XMPP_CONNECTION_SECURITY_BOTH_PREFER_UNSECURED and
 * @INF_XMPP_CONNECTION_SECURITY_BOTH_PREFER_TLS are equivalent.
 */
typedef enum _InfXmppConnectionSecurityPolicy {
  INF_XMPP_CONNECTION_SECURITY_ONLY_UNSECURED,
  INF_XMPP_CONNECTION_SECURITY_ONLY_TLS,
  INF_XMPP_CONNECTION_SECURITY_BOTH_PREFER_UNSECURED,
  INF_XMPP_CONNECTION_SECURITY_BOTH_PREFER_TLS
} InfXmppConnectionSecurityPolicy;

/**
 * InfXmppConnectionError:
 * @INF_XMPP_CONNECTION_ERROR_TLS_UNSUPPORTED: Server does not support TLS,
 * but the security policy is set to %INF_XMPP_CONNECTION_SECURITY_ONLY_TLS.
 * @INF_XMPP_CONNECTION_ERROR_TLS_REQUIRED: The server requires TLS, but the
 * security policy is set to %INF_XMPP_CONNECTION_SECURITY_ONLY_UNSECURED.
 * @INF_XMPP_CONNECTION_ERROR_TLS_FAILURE: Server cannot proceed with the TLS
 * handshake.
 * @INF_XMPP_CONNECTION_ERROR_NO_CERTIFICATE_PROVIDED: The server did not
 * provide a certificate.
 * @INF_XMPP_CONNECTION_ERROR_CERTIFICATE_NOT_TRUSTED: The server certificate
 * is not trusted. Whether the server certificate is trusted or not is defined
 * by the API user, by providing a certificate callback with
 * inf_xmpp_connection_set_certificate_callback().
 * @INF_XMPP_CONNECTION_ERROR_AUTHENTICATION_UNSUPPORTED: The server does not
 * provide any authentication mechanisms.
 * @INF_XMPP_CONNECTION_ERROR_NO_SUITABLE_MECHANISM: The server does not offer
 * a suitable authentication mechanism that is accepted by the client.
 * @INF_XMPP_CONNECTION_ERROR_FAILED: General error code for otherwise
 * unknown errors.
 *
 * Specifies the error codes in the
 * <literal>INF_XMPP_CONNECTION_ERROR</literal> error domain.
 */
typedef enum _InfXmppConnectionError {
  INF_XMPP_CONNECTION_ERROR_TLS_UNSUPPORTED,
  INF_XMPP_CONNECTION_ERROR_TLS_REQUIRED,
  INF_XMPP_CONNECTION_ERROR_TLS_FAILURE,
  INF_XMPP_CONNECTION_ERROR_NO_CERTIFICATE_PROVIDED,
  INF_XMPP_CONNECTION_ERROR_CERTIFICATE_NOT_TRUSTED,
  INF_XMPP_CONNECTION_ERROR_AUTHENTICATION_UNSUPPORTED,
  INF_XMPP_CONNECTION_ERROR_NO_SUITABLE_MECHANISM,

  INF_XMPP_CONNECTION_ERROR_FAILED
} InfXmppConnectionError;

/**
 * InfXmppConnectionStreamError:
 * @INF_XMPP_CONNECTION_STREAM_ERROR_BAD_FORMAT: The entity has sent XML that
 * cannot be processed.
 * @INF_XMPP_CONNECTION_STREAM_ERROR_BAD_NAMESPACE_PREFIX: The entity has sent
 * a namespace prefix that is unsupported, or has sent no namespace prefix on
 * an element that requires such a prefix.
 * @INF_XMPP_CONNECTION_STREAM_ERROR_CONFLICT: The server is closing the
 * active stream for this entity because a new stream has been initiated
 * that conflicts with the existing stream.
 * @INF_XMPP_CONNECTION_STREAM_ERROR_CONNECTION_TIMEOUT: The entity has not
 * generated any traffic over the stream for some period of time.
 * @INF_XMPP_CONNECTION_STREAM_ERROR_HOST_GONE: The value of the 'to'
 * attribute provided by the initiating entity in the stream header
 * corresponds to a hostname that is no longer hosted by the server.
 * @INF_XMPP_CONNECTION_STREAM_ERROR_HOST_UNKNOWN: The value of the 'to'
 * attribute provided by the initiating entity in the stream header does
 * not correspond to a hostname that is hosted by the server.
 * @INF_XMPP_CONNECTION_STREAM_ERROR_IMPROPER_ADDRESSING: A stanza sent
 * between two servers lacks a 'to' or 'from' attribute.
 * @INF_XMPP_CONNECTION_STREAM_ERROR_INTERNAL_SERVER_ERROR: The server has
 * experienced a misconfiguration or an otherwise-undefined internal error
 * that prevents it from servicing the stream.
 * @INF_XMPP_CONNECTION_STREAM_ERROR_INVALID_FROM: The JID or hostname
 * provided in a 'from' address does not match an authorized JID or
 * validated domain negotiated between servers via SASL or dialback, or
 * between a client and a server via authentication and resource binding.
 * @INF_XMPP_CONNECTION_STREAM_ERROR_INVALID_ID: The stream ID or dialback
 * ID is invalid or does not match an ID previously provided.
 * @INF_XMPP_CONNECTION_STREAM_ERROR_INVALID_NAMESPACE: The streams namespace
 * is something other than <literal>http://etherx.jabber.org/streams</literal>
 * or the dialback namespace name is something other than
 * <literal>jabber:server:dialback</literal>.
 * @INF_XMPP_CONNECTION_STREAM_ERROR_INVALID_XML: The entity has sent invalid
 * XML over the stream to a server that performs validation.
 * @INF_XMPP_CONNECTION_STREAM_ERROR_NOT_AUTHORIZED: The entity has attempted
 * to send data before the stream has been authenticated, or otherwise is not
 * authorized to perform an action related to stream negotiation.
 * @INF_XMPP_CONNECTION_STREAM_ERROR_POLICY_VIOLATION: The entity has violated
 * some local service policy.
 * @INF_XMPP_CONNECTION_STREAM_ERROR_REMOTE_CONNECTION_FAILED: The server is
 * unable to properly connect to a remote entity that is required for
 * authentication or authorization.
 * @INF_XMPP_CONNECTION_STREAM_ERROR_RESOURCE_CONSTRAINT: The server lacks the
 * system resources necessary to service the stream.
 * @INF_XMPP_CONNECTION_STREAM_ERROR_RESTRICTED_XML: The entity has attempted
 * to send restricted XML features.
 * @INF_XMPP_CONNECTION_STREAM_ERROR_SEE_OTHER_HOST: The server will not
 * provide service to the initiating entity but is redirecting traffic
 * to another host.
 * @INF_XMPP_CONNECTION_STREAM_ERROR_SYSTEM_SHUTDOWN: The server is being
 * shut down and all active streams are being closed.
 * @INF_XMPP_CONNECTION_STREAM_ERROR_UNDEFINED_CONDITION: The error condition
 * is not one of those defined by the other conditions.
 * @INF_XMPP_CONNECTION_STREAM_ERROR_UNSUPPORTED_ENCODING: The initiating
 * entity has encoded the stream in an encoding that is not supported by
 * the server.
 * @INF_XMPP_CONNECTION_STREAM_ERROR_UNSUPPORTED_STANZA_TYPE: The initiating
 * entity has sent a first-level child of the stream that is not supported
 * by the server.
 * @INF_XMPP_CONNECTION_STREAM_ERROR_UNSUPPORTED_VERSION: The value of the
 * 'version' attribute provided by the initiating entity in the stream header
 * specifies a version of XMPP that is not supported by the server.
 * @INF_XMPP_CONNECTION_STREAM_ERROR_XML_NOT_WELL_FORMED: The initiating
 * entity has sent XML that is not well-formed.
 * @INF_XMPP_CONNECTION_STREAM_ERROR_FAILED: General error code for otherwise
 * unknown errors.
 *
 * Specifies the error codes in the
 * <literal>INF_XMPP_CONNECTION_STREAM_ERROR</literal> error domain. These
 * errors correspond to the ones defined in RFC 3920, section 4.7.3.
 */
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

/**
 * InfXmppConnectionAuthError:
 * @INF_XMPP_CONNECTION_AUTH_ERROR_ABORTED: The receiving entity acknowledged
 * an <literal>&lt;abort/&gt;</literal> element sent by the initiating entity.
 * @INF_XMPP_CONNECTION_AUTH_ERROR_INCORRECT_ENCODING: The data provided by
 * the initiating entity could not be processed because the Base64 encoding
 * is incorrect.
 * @INF_XMPP_CONNECTION_AUTH_ERROR_INVALID_AUTHZID: The authzid provided by
 * the initiating entity is invalid, either because it is incorrectly
 * formatted or because the initiating entity does not have permissions
 * to authorize that ID.
 * @INF_XMPP_CONNECTION_AUTH_ERROR_INVALID_MECHANISM: The initiating entity
 * did not provide a mechanism or requested a mechanism that is not supported
 * by the receiving entity.
 * @INF_XMPP_CONNECTION_AUTH_ERROR_MECHANISM_TOO_WEAK: The mechanism requsted
 * by the initiating entity is weaker than server policy permits for that
 * initiating entity.
 * @INF_XMPP_CONNECTION_AUTH_ERROR_NOT_AUTHORIZED: The authentication failed
 * because the initiating entity did not provide valid credentials.
 * @INF_XMPP_CONNECTION_AUTH_ERROR_TEMPORARY_AUTH_FAILURE: The authentication
 * failed because of a temporary error condition within the receiving entity.
 * @INF_XMPP_CONNECTION_AUTH_ERROR_FAILED: General error code for otherwise
 * unknown errors.
 *
 * Specifies the error codes in the
 * <literal>INF_XMPP_CONNECTION_AUTH_ERROR</literal> error domain. These
 * errors correspond to the ones defined in RFC 3920, section 6.4.
 */
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

/**
 * InfXmppConnectionClass:
 *
 * This structure does not contain any public fields.
 */
struct _InfXmppConnectionClass {
  /*< private >*/
  GObjectClass parent_class;
};

/**
 * InfXmppConnection:
 *
 * #InfXmppConnection is an opaque data type. You should only access it via
 * the public API functions.
 */
struct _InfXmppConnection {
  /*< private >*/
  GObject parent;
};

/**
 * InfXmppConnectionCrtCallback:
 * @xmpp: The #InfXmppConnection validating a certificate.
 * @session: The underlying GnuTLS session.
 * @chain: The certificate chain presented by the remote host.
 * @user_data: Additional data supplied when
 * inf_xmpp_connection_set_certificate_callback() was called.
 *
 * Specifies the callback signature for the certificate callback set with
 * inf_xmpp_connection_set_certificate_callback(). The callback should decide
 * whether to trust the certificate in @chain or not.
 */
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

GQuark
inf_xmpp_connection_error_quark(void);

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
                                             gnutls_certificate_request_t req,
                                             InfXmppConnectionCrtCallback cb,
                                             gpointer user_data,
                                             GDestroyNotify notify);

void
inf_xmpp_connection_certificate_verify_continue(InfXmppConnection* xmpp);

void
inf_xmpp_connection_certificate_verify_cancel(InfXmppConnection* xmpp,
                                              const GError* error);

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
