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

#ifndef __INF_XMPP_CONNECTION_H__
#define __INF_XMPP_CONNECTION_H__

#include <libinfinity/common/inf-tcp-connection.h>

#include <gnutls/gnutls.h>
#include <gsasl.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_XMPP_CONNECTION                 (inf_xmpp_connection_get_type())
#define INF_XMPP_CONNECTION(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_XMPP_CONNECTION, InfXmppConnection))
#define INF_XMPP_CONNECTION_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TYPE_XMPP_CONNECTION, InfXmppConnectionClass))
#define INF_IS_XMPP_CONNECTION(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_XMPP_CONNECTION))
#define INF_IS_XMPP_CONNECTION_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TYPE_XMPP_CONNECTION))
#define INF_XMPP_CONNECTION_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TYPE_XMPP_CONNECTION, InfXmppConnectionClass))

#define INF_TYPE_XMPP_CONNECTION_SITE            (inf_xmpp_connection_site_get_type())

typedef struct _InfXmppConnection InfXmppConnection;
typedef struct _InfXmppConnectionClass InfXmppConnectionClass;

typedef enum _InfXmppConnectionSite {
  INF_XMPP_CONNECTION_SERVER,
  INF_XMPP_CONNECTION_CLIENT
} InfXmppConnectionSite;

typedef enum _InfXmppConnectionError {
  /* Server does not support TLS */
  INF_XMPP_CONNECTION_ERROR_TLS_UNSUPPORTED,
  /* Got <failure> as response to <starttls> */
  INF_XMPP_CONNECTION_ERROR_TLS_FAILURE,
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

  /* Signals */

  /* The domain can be either INF_TCP_CONNECTION_ERROR,
   * INF_XMPP_CONNECTION_ERROR, INF_XMPP_CONNECTION_STREAM_ERROR,
   * INF_XMPP_CONNECTION_AUTH_ERROR, INF_XMPP_CONNECTION_GNUTLS_ERROR or
   * INF_XMPP_CONNECTION_GSASL_ERROR. error->code is a GnuTLS error code in
   * the GNUTLS_ERROR case and a Gsasl error code in the GSASL_ERROR case.
   * 
   * An error does not necessarily mean that that the XMPP connection is
   * closed. If it is, a corresponding status notify follows. */
  void (*error)(InfXmppConnection* xmpp,
                GError* error);
};

struct _InfXmppConnection {
  GObject parent;
};

GType
inf_xmpp_connection_site_get_type(void) G_GNUC_CONST;

GType
inf_xmpp_connection_get_type(void) G_GNUC_CONST;

InfXmppConnection*
inf_xmpp_connection_new(InfTcpConnection* tcp,
			InfXmppConnectionSite site,
			const gchar* jid,
			gnutls_certificate_credentials_t cred,
			Gsasl* sasl_context);

G_END_DECLS

#endif /* __INF_XMPP_CONNECTION_H__ */

/* vim:set et sw=2 ts=2: */
