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

/**
 * SECTION:inf-xmpp-connection
 * @title: InfXmppConnection
 * @short_description: Implementation of the XMPP core protocol
 * @include: libinfinity/common/inf-xmpp-connection.h
 * @stability: Unstable
 *
 * This class implements the XMPP protocol as defined by RFC 3920. It handles
 * the authentication and authorization. Once the connection is established,
 * XML messages can be sent and received with the API of the
 * #InfXmlConnection interface.
 *
 * Note that any sort of XML messages can be exchanged, and that the XML does
 * not need to adhere to the XMPP standard. It is in the responsibility of the
 * user of this class to send only XML message that the remote counterpart can
 * understand.
 **/

#include <libinfinity/common/inf-xmpp-connection.h>
#include <libinfinity/common/inf-xml-connection.h>
#include <libinfinity/common/inf-xml-util.h>
#include <libinfinity/common/inf-ip-address.h>
#include <libinfinity/common/inf-error.h>

#include <libinfinity/inf-marshal.h>
#include <libinfinity/inf-i18n.h>
#include <libinfinity/inf-signals.h>

#include <gnutls/x509.h>

#include <errno.h>
#include <string.h>
#include <ctype.h>

#include "config.h"

/* This is set in inf_init() in inf-init.c based on the existance of the
 * environment variable LIBINFINITY_DEBUG_PRINT_TRAFFIC. */
gboolean INF_XMPP_CONNECTION_PRINT_TRAFFIC = FALSE;

/* This is an implementation of the XMPP protocol as specified in RFC 3920.
 * Note that it is neither complete nor very standard-compliant at this time.
 */

typedef enum _InfXmppConnectionStatus {
  /* Underlaying TCP connection is being established */
  INF_XMPP_CONNECTION_CONNECTING,
  /* Underlaying TCP connection is established */
  INF_XMPP_CONNECTION_CONNECTED,
  /* Same as above, but the stream has already been authenticated */
  INF_XMPP_CONNECTION_AUTH_CONNECTED,
  /* Initial <stream:stream> has been sent */
  INF_XMPP_CONNECTION_INITIATED,
  /* Same as above, but the stream has already been authenticated */
  INF_XMPP_CONNECTION_AUTH_INITIATED,
  /* <stream:stream> has been received, waiting for features (client only) */
  INF_XMPP_CONNECTION_AWAITING_FEATURES,
  /* Same as above, but the stream has already been authenticated */
  INF_XMPP_CONNECTION_AUTH_AWAITING_FEATURES,
  /* <starttls> request has been sent (client only) */
  INF_XMPP_CONNECTION_ENCRYPTION_REQUESTED,
  /* TLS handshake is being performed */
  INF_XMPP_CONNECTION_HANDSHAKING,
  /* SASL authentication is in progress */
  INF_XMPP_CONNECTION_AUTHENTICATING,
  /* Connection is ready to send XML */
  INF_XMPP_CONNECTION_READY,
  /* Connection is being closed, but we did not yet get </stream:stream>
   * from the other site */
  INF_XMPP_CONNECTION_CLOSING_STREAM,
  /* Connection is being closed, we got </stream:stream> but are still
   * waiting for any final data to be sent. */
  /* TODO: Rename this. */
  INF_XMPP_CONNECTION_CLOSING_GNUTLS,
  /* Connection is closed */
  INF_XMPP_CONNECTION_CLOSED
} InfXmppConnectionStatus;

typedef void(*InfXmppConnectionSentFunc)(InfXmppConnection* xmpp,
                                         gpointer user_data);

typedef void(*InfXmppConnectionFreeFunc)(InfXmppConnection* xmpp,
                                         gpointer user_data);

typedef struct _InfXmppConnectionMessage InfXmppConnectionMessage;
struct _InfXmppConnectionMessage {
  InfXmppConnectionMessage* next;
  guint position;
  gboolean sent;

  InfXmppConnectionSentFunc sent_func;
  InfXmppConnectionFreeFunc free_func;
  gpointer user_data;
};

typedef struct _InfXmppConnectionPrivate InfXmppConnectionPrivate;
struct _InfXmppConnectionPrivate {
  InfTcpConnection* tcp;
  InfXmppConnectionSite site;
  gchar* local_hostname;
  gchar* remote_hostname;
  InfXmppConnectionSecurityPolicy security_policy;

  InfXmppConnectionStatus status;
  gnutls_certificate_request_t certificate_request;
  InfXmppConnectionCrtCallback certificate_callback;
  gpointer certificate_callback_user_data;

  /* The number of chars given to the TCP connection 
   * waiting for being sent. */
  guint position;

  /* Message queue */
  xmlDocPtr doc;
  xmlBufferPtr buf;
  InfXmppConnectionMessage* messages;
  InfXmppConnectionMessage* last_message;

  /* XML parsing */
  guint parsing; /* Whether we are currently in an XML parser or GnuTLS callback */
  xmlParserCtxtPtr parser;
  xmlNodePtr root;
  xmlNodePtr cur;

  /* Transport layer security */
  gnutls_session_t session;
  InfCertificateCredentials* creds;
  gnutls_x509_crt_t own_cert;
  InfCertificateChain* peer_cert;
  const gchar* pull_data;
  gsize pull_len;

  /* SASL */
  InfSaslContext* sasl_context;
  InfSaslContext* sasl_own_context;
  InfSaslContextSession* sasl_session;
  gchar* sasl_local_mechanisms;
  gchar* sasl_remote_mechanisms;

  GError* sasl_error;
};

enum {
  PROP_0,

  PROP_TCP,
  PROP_SITE,
  PROP_LOCAL_HOSTNAME,
  PROP_REMOTE_HOSTNAME,
  PROP_SECURITY_POLICY,

  PROP_TLS_ENABLED,
  PROP_CREDENTIALS,

  PROP_SASL_CONTEXT,
  PROP_SASL_MECHANISMS,

  /* From InfXmlConnection */
  PROP_STATUS,
  PROP_NETWORK,
  PROP_LOCAL_ID,
  PROP_REMOTE_ID,
  PROP_LOCAL_CERTIFICATE,
  PROP_REMOTE_CERTIFICATE
};

#define INF_XMPP_CONNECTION_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TYPE_XMPP_CONNECTION, InfXmppConnectionPrivate))

static GObjectClass* parent_class;

static GQuark inf_xmpp_connection_error_quark;
static GQuark inf_xmpp_connection_stream_error_quark;
static GQuark inf_xmpp_connection_auth_error_quark;

/*
 * XMPP error handling
 */

typedef struct _InfXmppConnectionErrorCondition {
  const gchar* condition;
  guint code;
} InfXmppConnectionErrorCondition;

static const InfXmppConnectionErrorCondition
  inf_xmpp_connection_stream_error_conditions[] = {
    {
      "bad-format",
      INF_XMPP_CONNECTION_STREAM_ERROR_BAD_FORMAT
    }, {
      "bad-namespace-prefix",
      INF_XMPP_CONNECTION_STREAM_ERROR_BAD_NAMESPACE_PREFIX
    }, {
      "conflict",
      INF_XMPP_CONNECTION_STREAM_ERROR_CONFLICT
    }, {
      "connection-timeout",
      INF_XMPP_CONNECTION_STREAM_ERROR_CONNECTION_TIMEOUT
    }, {
      "host-gone",
      INF_XMPP_CONNECTION_STREAM_ERROR_HOST_GONE
    }, {
      "host-unknown",
      INF_XMPP_CONNECTION_STREAM_ERROR_HOST_UNKNOWN
    }, {
      "improper-addressing",
      INF_XMPP_CONNECTION_STREAM_ERROR_IMPROPER_ADDRESSING
    }, {
      "internal-server-error",
      INF_XMPP_CONNECTION_STREAM_ERROR_INTERNAL_SERVER_ERROR
    }, {
      "invalid-from",
      INF_XMPP_CONNECTION_STREAM_ERROR_INVALID_FROM
    }, {
      "invalid-id",
      INF_XMPP_CONNECTION_STREAM_ERROR_INVALID_ID
    }, {
      "invalid-namespace",
      INF_XMPP_CONNECTION_STREAM_ERROR_INVALID_NAMESPACE
    }, {
      "invalid-xml",
      INF_XMPP_CONNECTION_STREAM_ERROR_INVALID_XML
    }, {
      "not-authorized",
      INF_XMPP_CONNECTION_STREAM_ERROR_NOT_AUTHORIZED
    }, {
      "policy-violation",
      INF_XMPP_CONNECTION_STREAM_ERROR_POLICY_VIOLATION
    }, {
      "remote-connection-failed",
      INF_XMPP_CONNECTION_STREAM_ERROR_REMOTE_CONNECTION_FAILED
    }, {
      "resource-constraint",
      INF_XMPP_CONNECTION_STREAM_ERROR_RESOURCE_CONSTRAINT
    }, {
      "restricted-xml",
      INF_XMPP_CONNECTION_STREAM_ERROR_RESTRICTED_XML
    }, {
      "see-other-host",
      INF_XMPP_CONNECTION_STREAM_ERROR_SEE_OTHER_HOST
    }, {
      "system-shutdown",
      INF_XMPP_CONNECTION_STREAM_ERROR_SYSTEM_SHUTDOWN
    }, {
      "undefined-condition",
      INF_XMPP_CONNECTION_STREAM_ERROR_UNDEFINED_CONDITION
    }, {
      /* Also map unknown failure to undefined-condition */
      "undefined-condition",
      INF_XMPP_CONNECTION_STREAM_ERROR_FAILED
    }, {
      "unsupported-encoding",
      INF_XMPP_CONNECTION_STREAM_ERROR_UNSUPPORTED_ENCODING
    }, {
      "unsupported-stanza-type",
      INF_XMPP_CONNECTION_STREAM_ERROR_UNSUPPORTED_STANZA_TYPE
    }, {
      "unsupported-version",
      INF_XMPP_CONNECTION_STREAM_ERROR_UNSUPPORTED_VERSION
    }, {
      "xml-not-well-formed",
      INF_XMPP_CONNECTION_STREAM_ERROR_XML_NOT_WELL_FORMED
    }, {
      NULL,
      0
    }
  };

static const InfXmppConnectionErrorCondition
  inf_xmpp_connection_auth_error_conditions[] = {
    {
      "aborted",
      INF_XMPP_CONNECTION_AUTH_ERROR_ABORTED
    }, {
      "incorrect-encoding",
      INF_XMPP_CONNECTION_AUTH_ERROR_INCORRECT_ENCODING
    }, {
      "invalid-authzid",
      INF_XMPP_CONNECTION_AUTH_ERROR_INVALID_AUTHZID
    }, {
      "invalid-mechanism",
      INF_XMPP_CONNECTION_AUTH_ERROR_INVALID_MECHANISM
    }, {
      "mechanism-too-weak",
      INF_XMPP_CONNECTION_AUTH_ERROR_MECHANISM_TOO_WEAK
    }, {
      "not-authorized",
      INF_XMPP_CONNECTION_AUTH_ERROR_NOT_AUTHORIZED
    }, {
      "temporary-auth-failure",
      INF_XMPP_CONNECTION_AUTH_ERROR_TEMPORARY_AUTH_FAILURE
    }, {
      NULL,
      0
    }
  };

static InfXmppConnectionStreamError
inf_xmpp_connection_stream_error_from_condition(const gchar* condition)
{
  const InfXmppConnectionErrorCondition* cond;
  for(cond = inf_xmpp_connection_stream_error_conditions;
      cond->condition != NULL;
      ++ cond)
  {
    if(strcmp(condition, cond->condition) == 0)
      return cond->code;
  }

  return INF_XMPP_CONNECTION_STREAM_ERROR_FAILED;
}

static const gchar*
inf_xmpp_connection_stream_error_to_condition(InfXmppConnectionStreamError code)
{
  const InfXmppConnectionErrorCondition* cond;
  for(cond = inf_xmpp_connection_stream_error_conditions;
      cond->condition != NULL;
      ++ cond)
  {
    if(code == cond->code)
      return cond->condition;
  }

  g_assert_not_reached();
  return NULL;
}

/* TODO: These are directly copied from RFC 3920, section 4.7.3, probably
 * they have to be adjusted to be more useful to the user. */
static const gchar*
inf_xmpp_connection_stream_strerror(InfXmppConnectionStreamError code)
{
  switch(code)
  {
  case INF_XMPP_CONNECTION_STREAM_ERROR_BAD_FORMAT:
    return _("The entity has sent XML that cannot be processed");
  case INF_XMPP_CONNECTION_STREAM_ERROR_BAD_NAMESPACE_PREFIX:
    return _("The entity has sent a namespace prefix that is unsupported, or "
             "has sent no namespace prefix on an element that requires such "
             "a prefix");
  case INF_XMPP_CONNECTION_STREAM_ERROR_CONFLICT:
    return _("The server is closing the active stream for this entity "
             "because a new stream has been initiated that conflicts with "
             "the existing stream");
  case INF_XMPP_CONNECTION_STREAM_ERROR_CONNECTION_TIMEOUT:
    return _("The entity has not generated any traffic over the stream for "
             "some period of time");
  case INF_XMPP_CONNECTION_STREAM_ERROR_HOST_GONE:
    return _("The value of the 'to' attribute provided by the initiating "
             "entity in the stream header corresponds to a hostname that is "
             "no longer hosted by the server");
  case INF_XMPP_CONNECTION_STREAM_ERROR_HOST_UNKNOWN:
    return _("The value of the 'to' attribute provided by the initiating "
             "entity  in the stream header does not correspond to a hostname "
             "that is hosted by the server");
  case INF_XMPP_CONNECTION_STREAM_ERROR_IMPROPER_ADDRESSING:
    return _("A stanza sent between two servers lacks a 'to' or 'from'"
             "attribute");
  case INF_XMPP_CONNECTION_STREAM_ERROR_INTERNAL_SERVER_ERROR:
    return _("The server has experienced a misconfiguration or an otherwise-"
             "undefined internal error that prevents it from servicing "
             "the stream");
  case INF_XMPP_CONNECTION_STREAM_ERROR_INVALID_FROM:
    return _("The JID or hostname provided in a 'from' address does not "
             "match an authorized JID or validated domain negotiated between "
             "servers via SASL or dialback, or between a client and a "
             "server via authentication and resource binding");
  case INF_XMPP_CONNECTION_STREAM_ERROR_INVALID_ID:
    return _("The stream ID or dialback ID is invalid or does not match an "
             "ID previously provided");
  case INF_XMPP_CONNECTION_STREAM_ERROR_INVALID_NAMESPACE:
    return _("The streams namespace is something other than "
             "\"http://etherx.jabber.org/streams\" or the dialback namespace "
             "name is something other than \"jabber:server:dialback\"");
  case INF_XMPP_CONNECTION_STREAM_ERROR_INVALID_XML:
    return _("The entity has sent invalid XML over the stream to a server "
             "that performs validation");
  case INF_XMPP_CONNECTION_STREAM_ERROR_NOT_AUTHORIZED:
    return _("The entity has attempted to send data before the stream has "
             "been authenticated, or otherwise is not authorized to perform "
             "an action related to stream negotiation");
  case INF_XMPP_CONNECTION_STREAM_ERROR_POLICY_VIOLATION:
    return _("The entity has violated some local service policy");
  case INF_XMPP_CONNECTION_STREAM_ERROR_REMOTE_CONNECTION_FAILED:
    return _("The server is unable to properly connect to a remote entity "
             "that is required for authentication or authorization");
  case INF_XMPP_CONNECTION_STREAM_ERROR_RESOURCE_CONSTRAINT:
    return _("The server lacks the system resources necessary to service the "
             "stream");
  case INF_XMPP_CONNECTION_STREAM_ERROR_RESTRICTED_XML:
    return _("The entity has attempted to send restricted XML features");
  case INF_XMPP_CONNECTION_STREAM_ERROR_SEE_OTHER_HOST:
    return _("The server will not provide service to the initiating "
             "entity but is redirecting traffic to another host");
  case INF_XMPP_CONNECTION_STREAM_ERROR_SYSTEM_SHUTDOWN:
    return _("The server is being shut down and all active streams are being "
             "closed");
  case INF_XMPP_CONNECTION_STREAM_ERROR_UNDEFINED_CONDITION:
  case INF_XMPP_CONNECTION_STREAM_ERROR_FAILED:
    return _("The error condition is not one of those defined by the other "
             "conditions");
  case INF_XMPP_CONNECTION_STREAM_ERROR_UNSUPPORTED_ENCODING:
    return _("The initiating entity has encoded the stream in an encoding "
             "that is not supported by the server");
  case INF_XMPP_CONNECTION_STREAM_ERROR_UNSUPPORTED_STANZA_TYPE:
    return _("The initiating entity has sent a first-level child of the "
             "stream that is not supported by the server.");
  case INF_XMPP_CONNECTION_STREAM_ERROR_UNSUPPORTED_VERSION:
    return _("The value of the 'version' attribute provided by the "
             "initiating entity in the stream header specifies a version of "
             "XMPP that is not supported by the server");
  case INF_XMPP_CONNECTION_STREAM_ERROR_XML_NOT_WELL_FORMED:
    return _("The initiating entity has sent XML that is not well-formed");
  default:
    g_assert_not_reached();
    break;
  }
}

static InfXmppConnectionAuthError
inf_xmpp_connection_auth_error_from_condition(const gchar* condition)
{
  const InfXmppConnectionErrorCondition* cond;
  for(cond = inf_xmpp_connection_auth_error_conditions;
      cond->condition != NULL;
      ++ cond)
  {
    if(strcmp(condition, cond->condition) == 0)
      return cond->code;
  }

  return INF_XMPP_CONNECTION_AUTH_ERROR_FAILED;
}

static const gchar*
inf_xmpp_connection_auth_error_to_condition(InfXmppConnectionAuthError code)
{
  const InfXmppConnectionErrorCondition* cond;
  for(cond = inf_xmpp_connection_auth_error_conditions;
      cond->condition != NULL;
      ++ cond)
  {
    if(cond->code == code)
      return cond->condition;
  }

  g_assert_not_reached();
  return NULL;
}

/* TODO: Again, this is only copied from RFC 3920, section 6.4 */
static const gchar*
inf_xmpp_connection_auth_strerror(InfXmppConnectionAuthError code)
{
  switch(code)
  {
  case INF_XMPP_CONNECTION_AUTH_ERROR_ABORTED:
    return _("The receiving entity acknowledged an <abort/> element sent by "
             "the initiating entity");
  case INF_XMPP_CONNECTION_AUTH_ERROR_INCORRECT_ENCODING:
    return _("The data provided by the initiating entity could not be "
             "processed because the Base64 encoding is incorrect");
  case INF_XMPP_CONNECTION_AUTH_ERROR_INVALID_AUTHZID:
    return _("The authzid provided by the initiating entity is invalid, "
             "either because it is incorrectly formatted or because the "
             "initiating entity does not have permissions to authorize "
             "that ID");
  case INF_XMPP_CONNECTION_AUTH_ERROR_INVALID_MECHANISM:
    return _("The initiating entity did not provide a mechanism or requested "
             "a mechanism that is not supported by the receiving entity");
  case INF_XMPP_CONNECTION_AUTH_ERROR_MECHANISM_TOO_WEAK:
    return _("The mechanism requsted by the initiating entity is weaker than "
             "server policy permits for that initiating entity");
  case INF_XMPP_CONNECTION_AUTH_ERROR_NOT_AUTHORIZED:
    return _("The authentication failed because the initiating entity did "
             "not provide valid credentials");
  case INF_XMPP_CONNECTION_AUTH_ERROR_TEMPORARY_AUTH_FAILURE:
    return _("The authentication failed because of a temporary error condition "
             "within the receiving entity");
  case INF_XMPP_CONNECTION_AUTH_ERROR_FAILED:
    return _("An unknown authentication error has occured");
  default:
    g_assert_not_reached();
    return NULL;
  }
}

/*
 * Message queue
 */

static void
inf_xmpp_connection_push_message(InfXmppConnection* xmpp,
                                 InfXmppConnectionSentFunc sent_func,
                                 InfXmppConnectionFreeFunc free_func,
                                 gpointer user_data)
{
  InfXmppConnectionPrivate* priv;
  InfXmppConnectionMessage* message;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  if(priv->position == 0)
  {
    if(sent_func != NULL)
      sent_func(xmpp, user_data);
    if(free_func != NULL)
      free_func(xmpp, user_data);
  }
  else
  {
    message = g_slice_new(InfXmppConnectionMessage);

    message->next = NULL;
    message->position = priv->position;
    message->sent = FALSE;
    message->sent_func = sent_func;
    message->free_func = free_func;
    message->user_data = user_data;

    if(priv->last_message == NULL)
      priv->messages = message;
    else 
      priv->last_message->next = message;

    priv->last_message = message;
  }
}

static void
inf_xmpp_connection_pop_message(InfXmppConnection* connection)
{
  InfXmppConnectionPrivate* priv;
  InfXmppConnectionMessage* message;

  priv = INF_XMPP_CONNECTION_PRIVATE(connection);
  message = priv->messages;
  g_assert(message != NULL);

  priv->messages = message->next;
  if(priv->messages == NULL) priv->last_message = NULL;

  if(message->free_func != NULL)
    message->free_func(connection, message->user_data);

  g_slice_free(InfXmppConnectionMessage, message);
}

/* Note that this function does not change the state of xmpp, so it might
 * rest in a state where it expects to actually have the resources available
 * that are cleared here. Be sure to adjust state after having called
 * this function. */
static void
inf_xmpp_connection_clear(InfXmppConnection* xmpp)
{
  InfXmppConnectionPrivate* priv;
  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  g_object_freeze_notify(G_OBJECT(xmpp));

  if(priv->sasl_session != NULL)
  {
    inf_sasl_context_stop_session(priv->sasl_context, priv->sasl_session);
    priv->sasl_session = NULL;
  }

  if(priv->sasl_remote_mechanisms != NULL)
  {
    g_free(priv->sasl_remote_mechanisms);
    priv->sasl_remote_mechanisms = NULL;
  }

  /* Keep the certificates alive, in case they still need to be accessed after
   * the connection was closed. They are reset before a new connection is
   * made. */
#if 0
  if(priv->own_cert != NULL)
  {
    gnutls_x509_crt_deinit(priv->own_cert);
    priv->own_cert = NULL;

    g_object_notify(G_OBJECT(xmpp), "local-certificate");
  }

  if(priv->peer_cert != NULL)
  {
    inf_certificate_chain_unref(priv->peer_cert);
    priv->peer_cert = NULL;

    g_object_notify(G_OBJECT(xmpp), "remote-certificate");
  }
#endif

  if(priv->session != NULL)
  {
    gnutls_deinit(priv->session);
    priv->session = NULL;

    g_object_notify(G_OBJECT(xmpp), "tls-enabled");
  }

  if(priv->parser != NULL)
  {
    xmlFreeParserCtxt(priv->parser);
    priv->parser = NULL;

    if(priv->root != NULL)
    {
      xmlFreeNode(priv->root);
      priv->root = NULL;
      priv->cur = NULL;
    }
  }

  while(priv->messages != NULL)
    inf_xmpp_connection_pop_message(xmpp);

  if(priv->buf != NULL)
  {
    g_assert(priv->doc != NULL);

    xmlBufferFree(priv->buf);
    xmlFreeDoc(priv->doc);

    priv->buf = NULL;
    priv->doc = NULL;
  }

  priv->pull_data = NULL;
  priv->pull_len = 0;

  g_object_thaw_notify(G_OBJECT(xmpp));
}

static void
inf_xmpp_connection_send_chars(InfXmppConnection* xmpp,
                               gconstpointer data,
                               guint len)
{
  InfXmppConnectionPrivate* priv;
  ssize_t cur_bytes;
  GError* error;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  g_assert(priv->status != INF_XMPP_CONNECTION_HANDSHAKING &&
           priv->status != INF_XMPP_CONNECTION_CLOSED);

  if(INF_XMPP_CONNECTION_PRINT_TRAFFIC)
    printf("\033[00;34m%.*s\033[00;00m\n", (int)len, (const char*)data);

  /* From here on we go into a GnuTLS callback. Set this flag to prevent
   * premature cleanup -- make sure that if the connection is being brought
   * down from a GnuTLS callback then we keep the GnuTLS context around
   * until the gntuls_record_send() call finishes. */
  ++priv->parsing;

  if(priv->session != NULL)
  {
    do
    {
      cur_bytes = gnutls_record_send(priv->session, data, len);

      if(cur_bytes < 0)
      {
        /* A GnuTLS error occured. It does not make sense to try to send
         * </stream:stream> or a gnutls bye here, since this would again
         * have to go through GnuTLS, which would fail again, and so on. */
        error = NULL;
        inf_gnutls_set_error(&error, cur_bytes);
        inf_xml_connection_error(INF_XML_CONNECTION(xmpp), error);
        g_error_free(error);

        inf_tcp_connection_close(priv->tcp);
        break;
      }
      else if(cur_bytes == 0)
      {
        /* TODO: I am not sure whether this can actually happen and what
         * it means. */
        g_assert_not_reached();
        /*inf_tcp_connection_close(priv->tcp);*/
      }
      else
      {
        *((const char**)&data) += cur_bytes;
        len -= cur_bytes;
      }
    } while(len > 0);
  }
  else
  {
    priv->position += len;
    inf_tcp_connection_send(priv->tcp, data, len);
  }

  g_assert(priv->parsing > 0);
  if(--priv->parsing == 0)
  {
    if(priv->status == INF_XMPP_CONNECTION_CLOSED)
    {
      /* Status changed to CLOSED, so while attempting to send data we noticed
       * that the connection is down. Clear up. We didn't clean up in the
       * disconnection callback because we wanted to keep the gnutls context
       * alive until gnutls_record_send() returns. */
      inf_xmpp_connection_clear(xmpp);
    }
  }
}

static void
inf_xmpp_connection_send_xml(InfXmppConnection* xmpp,
                             xmlNodePtr xml)
{
  InfXmppConnectionPrivate* priv;
  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  g_return_if_fail(priv->doc != NULL);
  g_return_if_fail(priv->buf != NULL);

  xmlDocSetRootElement(priv->doc, xml);
  xmlNodeDump(priv->buf, priv->doc, xml, 0, 0);
  xmlUnlinkNode(xml);
  xmlSetListDoc(xml, NULL);

  /* Keep the object alive during the send_chars call, so that we can check
   * the buffer variable afterwards. */
  g_object_ref(xmpp);

  inf_xmpp_connection_send_chars(
    xmpp,
    xmlBufferContent(priv->buf),
    xmlBufferLength(priv->buf)
  );

  /* The connection might be closed & cleared as a result from
   * inf_xmpp_connection_send_chars(), so make sure the buffer still
   * exists before emptying it. */
  if(priv->buf != NULL)
    xmlBufferEmpty(priv->buf);

  g_object_unref(xmpp);
}

/*
 * Helper functions
 */
static xmlNodePtr
inf_xmpp_connection_node_new(const gchar* name,
                             const gchar* xmlns)
{
  xmlNodePtr ptr;
  ptr = xmlNewNode(NULL, (const xmlChar*)name);
  xmlNewProp(ptr, (const xmlChar*)"xmlns", (const xmlChar*)xmlns);
  return ptr;
}

static xmlNodePtr
inf_xmpp_connection_node_new_streams(const gchar* name)
{
  return inf_xmpp_connection_node_new(
    name,
    "urn:ietf:params:xml:ns:xmpp-streams"
  );
}

static xmlNodePtr
inf_xmpp_connection_node_new_tls(const gchar* name)
{
  return inf_xmpp_connection_node_new(
    name,
    "urn:ietf:params:xml:ns:xmpp-tls"
  );
}

static xmlNodePtr
inf_xmpp_connection_node_new_sasl(const gchar* name)
{
  return inf_xmpp_connection_node_new(
    name,
    "urn:ietf:params:xml:ns:xmpp-sasl"
  );
}

/*
 * XMPP deinitialization
 */

/* Terminates the XMPP session and closes the connection. */
static void
inf_xmpp_connection_terminate(InfXmppConnection* xmpp)
{
  static const gchar xmpp_connection_deinit_request[] = "</stream:stream>";

  InfXmppConnectionPrivate* priv;
  xmlNodePtr abort;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  g_assert(priv->status != INF_XMPP_CONNECTION_CLOSED &&
           priv->status != INF_XMPP_CONNECTION_CLOSING_GNUTLS &&
           priv->status != INF_XMPP_CONNECTION_CONNECTING);

  /* We cannot send </stream:stream> or a gnutls bye in these states
   * because it would interfere with the handshake. */
  if(priv->status != INF_XMPP_CONNECTION_HANDSHAKING &&
     priv->status != INF_XMPP_CONNECTION_ENCRYPTION_REQUESTED)
  {
    /* Session termination is not required in these states because the session
     * did not yet even begin or </stream:stream> has already been sent,
     * respectively. */

    if(priv->status != INF_XMPP_CONNECTION_CONNECTED &&
       priv->status != INF_XMPP_CONNECTION_AUTH_CONNECTED &&
       priv->status != INF_XMPP_CONNECTION_CLOSING_STREAM)
    {
      if(priv->status == INF_XMPP_CONNECTION_AUTHENTICATING)
      {
        /* Abort authentication before sending final </stream:stream> */
        abort = inf_xmpp_connection_node_new_sasl("abort");
        inf_xmpp_connection_send_xml(xmpp, abort);
        xmlFreeNode(abort);
      }

      /* inf_xmpp_connection_send_xml() above might have caused
       * status update: */
      if(priv->status != INF_XMPP_CONNECTION_CLOSED)
      {
        inf_xmpp_connection_send_chars(
          xmpp,
          xmpp_connection_deinit_request,
          sizeof(xmpp_connection_deinit_request) - 1
        );
      }
    }

    /* One of the send() calls above might have caused status update */
    if(priv->status != INF_XMPP_CONNECTION_CLOSED && priv->session != NULL)
      gnutls_bye(priv->session, GNUTLS_SHUT_WR);
  }

  /* Clear resources such as GnuTLS session and XML parser */
  if(priv->parsing == 0)
    inf_xmpp_connection_clear(xmpp);

  /* It can happen that the call to gnutls_bye() causes a send error because
   * the connection is already down. In that case the status is changed to
   * CLOSED, in which case we do not need further status updates at
   * this point. */
  if(priv->status != INF_XMPP_CONNECTION_CLOSED)
  {
    /* The Change from CLOSING_STREAM to CLOSING_GNUTLS does not change
     * the XML status, so we need no notify in this case. */
    if(priv->status != INF_XMPP_CONNECTION_CLOSING_STREAM)
    {
      priv->status = INF_XMPP_CONNECTION_CLOSING_GNUTLS;
      if(priv->parsing == 0)
        g_object_notify(G_OBJECT(xmpp), "status");
    }
    else
    {
      priv->status = INF_XMPP_CONNECTION_CLOSING_GNUTLS;
    }
  }
}

/* This sends a <failure> with the given error code, but does not close
 * the stream for the client to retry authentication. */
static void
inf_xmpp_connection_send_auth_error(InfXmppConnection* xmpp,
                                    InfXmppConnectionAuthError code)
{
  InfXmppConnectionPrivate* priv;
  xmlNodePtr xml;
  xmlNodePtr child;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);
  /* SASL should be present, otherwise no auth error could have occured */
  g_assert(priv->sasl_context != NULL);

  xml = inf_xmpp_connection_node_new_sasl("failure");

  child = xmlNewChild(
    xml,
    NULL,
    (const xmlChar*)inf_xmpp_connection_auth_error_to_condition(code),
    NULL
  );

  if(priv->sasl_error != NULL)
  {
    xmlAddChild(
      child,
      inf_xml_util_new_node_from_error(priv->sasl_error, NULL, "error"));
  }

  inf_xmpp_connection_send_xml(xmpp, xml);
  xmlFreeNode(xml);
}

/* Emits an error signal for the given auth error code. */
static void
inf_xmpp_connection_emit_auth_error(InfXmppConnection* xmpp,
                                    InfXmppConnectionAuthError code)
{
  GError* error;
  error = NULL;

  g_set_error(
    &error,
    inf_xmpp_connection_auth_error_quark,
    code,
    "%s",
    inf_xmpp_connection_auth_strerror(code)
  );

  inf_xml_connection_error(INF_XML_CONNECTION(xmpp), error);
  g_error_free(error);
}

/* This sends a <stream:error> and then terminates the session using
 * the inf_xmpp_connection_terminate. message may be NULL. */
static void
inf_xmpp_connection_terminate_error(InfXmppConnection* xmpp,
                                    InfXmppConnectionStreamError code,
                                    const gchar* message)
{
  InfXmppConnectionPrivate* priv;
  xmlNodePtr node;
  xmlNodePtr child;
  GError* error;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);
  g_assert(priv->parser != NULL);

  g_assert(priv->status != INF_XMPP_CONNECTION_HANDSHAKING &&
           priv->status != INF_XMPP_CONNECTION_ENCRYPTION_REQUESTED);

  error = NULL;
  g_set_error(
    &error,
    inf_xmpp_connection_stream_error_quark,
    code,
    "%s",
    message != NULL ? message : inf_xmpp_connection_stream_strerror(code)
  );

  node = xmlNewNode(NULL, (const xmlChar*)"stream:error");

  child = inf_xmpp_connection_node_new_streams(
    inf_xmpp_connection_stream_error_to_condition(code)
  );
  xmlAddChild(node, child);

  if(message != NULL)
  {
    child = inf_xmpp_connection_node_new_streams("text");
    xmlNodeAddContent(child, (const xmlChar*)message);

    /* TODO: Get real language code, probably from gettext */
    xmlNodeSetLang(child, (const xmlChar*)"en");
  }

  inf_xmpp_connection_send_xml(xmpp, node);
  xmlFreeNode(node);

  inf_xml_connection_error(INF_XML_CONNECTION(xmpp), error);
  g_error_free(error);

  inf_xmpp_connection_terminate(xmpp);
}

/* This attempts to deinitiate the stream by sending a final </stream:stream>,
 * but it waits for the </stream:stream> response of the other site. */
static void
inf_xmpp_connection_deinitiate(InfXmppConnection* xmpp)
{
  static const gchar xmpp_connection_deinitiate_request[] =
    "</stream:stream>";

  InfXmppConnectionPrivate* priv;
  xmlNodePtr abort;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  g_assert(priv->status != INF_XMPP_CONNECTION_CLOSING_GNUTLS &&
           priv->status != INF_XMPP_CONNECTION_CLOSING_STREAM &&
           priv->status != INF_XMPP_CONNECTION_CONNECTED &&
           priv->status != INF_XMPP_CONNECTION_AUTH_CONNECTED);

  /* If we are in an authentication callback and we decide not to continue
   * the connection then remove the cached SASL server mechanisms as we don't
   * need them anymore. */
  if(priv->site == INF_XMPP_CONNECTION_CLIENT &&
     priv->sasl_remote_mechanisms != NULL)
  {
    g_free(priv->sasl_remote_mechanisms);
    priv->sasl_remote_mechanisms = NULL;
  }

  if(priv->status == INF_XMPP_CONNECTION_AUTHENTICATING)
  {
    /* If the SASL session is NULL then we have already aborted the
     * authentication but are still waiting for the server to acknowledge. */
    if(priv->sasl_session != NULL)
    {
      /* Abort authentication before sending </stream:stream>. */
      /* TODO: Wait for response of the abort before sending </stream:stream> */
      abort = inf_xmpp_connection_node_new_sasl("abort");
      inf_xmpp_connection_send_xml(xmpp, abort);
    }
  }

  inf_xmpp_connection_send_chars(
    xmpp,
    xmpp_connection_deinitiate_request,
    sizeof(xmpp_connection_deinitiate_request) - 1
  );

  priv->status = INF_XMPP_CONNECTION_CLOSING_STREAM;
  g_object_notify(G_OBJECT(xmpp), "status");
}

/*
 * GnuTLS setup
 */

/* Required by inf_xmpp_connection_tls_handshake */
static void
inf_xmpp_connection_initiate(InfXmppConnection* xmpp);

static gboolean
inf_xmpp_connection_prefers_tls(InfXmppConnection* xmpp)
{
  InfXmppConnectionPrivate* priv;
  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  return
    priv->security_policy == INF_XMPP_CONNECTION_SECURITY_BOTH_PREFER_TLS ||
    priv->security_policy == INF_XMPP_CONNECTION_SECURITY_ONLY_TLS;
}

static ssize_t
inf_xmpp_connection_tls_push(gnutls_transport_ptr_t ptr,
                             const void* data,
                             size_t len)
{
  InfXmppConnection* xmpp;
  InfXmppConnectionPrivate* priv;

  xmpp = INF_XMPP_CONNECTION(ptr);
  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  priv->position += len;
  inf_tcp_connection_send(priv->tcp, data, len);

  return len;
}

static ssize_t
inf_xmpp_connection_tls_pull(gnutls_transport_ptr_t ptr,
                             void* data,
                             size_t len)
{
  InfXmppConnection* xmpp;
  InfXmppConnectionPrivate* priv;
  size_t pull_len;

  xmpp = INF_XMPP_CONNECTION(ptr);
  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  /* The data to pull is set in inf_xmpp_connection_received_cb() which then
   * calls gnutls_record_recv, however, the pull function might also be
   * called during a call to gnutls_handshake when no data is available.
   * We return EAGAIN in this case to wait for more data from
   * inf_xmpp_connection_received_cb(). */

  /* No data available */
  if(priv->pull_len == 0)
  {
    gnutls_transport_set_errno(priv->session, EAGAIN);
    return -1;
  }
  else
  {
    pull_len = priv->pull_len;
    if(len < pull_len) pull_len = len;

    memcpy(data, priv->pull_data, pull_len);
    priv->pull_len -= pull_len;
    priv->pull_data += pull_len;
    return pull_len;
  }
}

static gnutls_x509_crt_t
inf_xmpp_connection_tls_import_own_certificate(InfXmppConnection* xmpp,
                                               GError** error)
{
  InfXmppConnectionPrivate* priv;
  const gnutls_datum_t* cert_raw;
  gnutls_x509_crt_t cert;
  int res;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);
  cert_raw = gnutls_certificate_get_ours(priv->session);

  if(cert_raw == NULL)
    return NULL;

  res = gnutls_x509_crt_init(&cert);
  if(res != GNUTLS_E_SUCCESS)
  {
    inf_gnutls_set_error(error, res);
    return NULL;
  }

  res = gnutls_x509_crt_import(cert, cert_raw, GNUTLS_X509_FMT_DER);
  if(res != GNUTLS_E_SUCCESS)
  {
    gnutls_x509_crt_deinit(cert);
    inf_gnutls_set_error(error, res);
    return NULL;
  }

  return cert;
}

static InfCertificateChain*
inf_xmpp_connection_tls_import_peer_certificate(InfXmppConnection* xmpp,
                                                GError** error)
{
  InfXmppConnectionPrivate* priv;
  const gnutls_datum_t* certs_raw;
  unsigned int list_size;
  unsigned int n_certs;
  gnutls_x509_crt_t* certs;
  int res;
  guint i;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);
  certs_raw = gnutls_certificate_get_peers(priv->session, &list_size);

  if(certs_raw == NULL)
    return NULL;

  certs = g_malloc(list_size * sizeof(gnutls_x509_crt_t));

  /* TODO: The upper code only imports one certificate, even if there are
   * more. It's unclear to me why this happens. */
#if 0
  n_certs = list_size;
  res = gnutls_x509_crt_list_import(
    certs,
    &n_certs,
    certs_raw,
    GNUTLS_X509_FMT_DER,
    GNUTLS_X509_CRT_LIST_FAIL_IF_UNSORTED
  );

  if(res < 0)
  {
    g_free(certs);
    inf_gnutls_set_error(error, res);
    return NULL;
  }

  g_assert(res == n_certs);
  g_assert(res == list_size);
#else
  for(i = 0; i < list_size; ++ i)
  {
    res = gnutls_x509_crt_init(&certs[i]);

    if(res == GNUTLS_E_SUCCESS)
    {
      res = gnutls_x509_crt_import(
        certs[i],
        certs_raw + i,
        GNUTLS_X509_FMT_DER
      );

      if(res != GNUTLS_E_SUCCESS)
        gnutls_x509_crt_deinit(certs[i]);
    }

    if(res != GNUTLS_E_SUCCESS)
    {
      for(; i > 0; -- i)
        gnutls_x509_crt_deinit(certs[i - 1]);
      g_free(certs);
      inf_gnutls_set_error(error, res);
      return NULL;
    }
  }
#endif

  return inf_certificate_chain_new(certs, list_size);
}

static void
inf_xmpp_connection_tls_handshake(InfXmppConnection* xmpp)
{
  InfXmppConnectionPrivate* priv;
  int ret;

  InfCertificateChain* chain;
  GError* error;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);
  g_assert(priv->status == INF_XMPP_CONNECTION_HANDSHAKING);
  g_assert(priv->session != NULL);

  ret = gnutls_handshake(priv->session);
  switch(ret)
  {
  case GNUTLS_E_AGAIN:
    /* Wait for more data */
    break;
  case 0:
    /* Handshake finished successfully */
    priv->status = INF_XMPP_CONNECTION_CONNECTED;
    g_object_notify(G_OBJECT(xmpp), "tls-enabled");

    error = NULL;

    /* Extract own certificate */
    g_assert(priv->own_cert == NULL);
    priv->own_cert =
      inf_xmpp_connection_tls_import_own_certificate(xmpp, &error);
    if(error == NULL)
    {
      if(priv->own_cert != NULL)
        g_object_notify(G_OBJECT(xmpp), "local-certificate");
    
      /* Extract peer certificate */
      g_assert(priv->peer_cert == NULL);
      priv->peer_cert =
        inf_xmpp_connection_tls_import_peer_certificate(xmpp, &error);
      if(error == NULL)
      {
        /* Require the server to show us its certificate */
        if(priv->peer_cert == NULL)
        {
          if(priv->site == INF_XMPP_CONNECTION_CLIENT)
          {
            g_set_error(
              &error,
              inf_xmpp_connection_error_quark,
              INF_XMPP_CONNECTION_ERROR_NO_CERTIFICATE_PROVIDED,
              "%s",
              _("The server did not provide a certificate")
            );
          }
        }
        else
        {
          g_object_notify(G_OBJECT(xmpp), "remote-certificate");
        }
      }
    }

    if(error != NULL)
    {
      inf_xml_connection_error(INF_XML_CONNECTION(xmpp), error);
      g_error_free(error);
      inf_xmpp_connection_terminate(xmpp);
    }
    else
    {
      /* Ask the user to verify the peer's certificate, or, if there is no
       * certificate, whether the user still wants to accept the connection or
       * not. */
      if(priv->certificate_callback != NULL)
      {
        priv->certificate_callback(
          xmpp,
          priv->session,
          priv->peer_cert,
          priv->certificate_callback_user_data
        );
      }
      else
      {
        /* The user doesn't seem to be interested,
         * blindly accept the certificate */
        inf_xmpp_connection_initiate(xmpp);
      }
    }

    break;
  default:
    error = NULL;
    inf_gnutls_set_error(&error, ret);
    inf_xml_connection_error(INF_XML_CONNECTION(xmpp), error);
    g_error_free(error);

    gnutls_deinit(priv->session);
    priv->session = NULL;

    switch(priv->site)
    {
    case INF_XMPP_CONNECTION_CLIENT:
      /* Terminate connection when GnuTLS handshake fails. Don't wait for
       * </stream:stream> as the server might not be aware of the problem. */
      inf_xmpp_connection_terminate(xmpp);
      break;
    case INF_XMPP_CONNECTION_SERVER:
      /* TODO: Just close connection on error, without sending
       * </stream:stream>, as in the client case? */
      /* So that inf_xmpp_connection_terminate() doesn't get confused, it will
       * be overwritten to CLOSING_GNUTLS anyway. */
      priv->status = INF_XMPP_CONNECTION_INITIATED;
      /* Send terminating </stream:stream>, close XMPP session */
      inf_xmpp_connection_terminate(xmpp);
      break;
    default:
      g_assert_not_reached();
      break;
    }

    break;
  }
}

static void
inf_xmpp_connection_tls_init(InfXmppConnection* xmpp)
{
  static const guint xmpp_connection_dh_bits = 1024;

#if 0
  static const int xmpp_connection_protocol_priority[] =
    { GNUTLS_TLS1, GNUTLS_SSL3, 0 };
  static const int xmpp_connection_kx_priority[] =
    { GNUTLS_KX_RSA, 0 };
  static const int xmpp_connection_cipher_priority[] =
    { GNUTLS_CIPHER_3DES_CBC, GNUTLS_CIPHER_ARCFOUR, 0 };
  static const int xmpp_connection_comp_priority[] =
    { GNUTLS_COMP_ZLIB, GNUTLS_COMP_NULL, 0 };
  static const int xmpp_connection_mac_priority[] =
    { GNUTLS_MAC_SHA, GNUTLS_MAC_MD5, 0 };
#endif

  InfXmppConnectionPrivate* priv;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);
  g_assert(priv->session == NULL);

  /* Make sure credentials are present */
  if(priv->creds == NULL)
  {
    /* We can create built-in credentials for the client side. However, the
     * server requires a certificate, and it doesn't make sense to generate
     * one here, so we require that credentials are given for a server-side
     * XMPP connection. */
    g_assert(priv->site == INF_XMPP_CONNECTION_CLIENT);

    priv->creds = inf_certificate_credentials_new();
    g_object_notify(G_OBJECT(xmpp), "credentials");
  }

  switch(priv->site)
  {
  case INF_XMPP_CONNECTION_CLIENT:
    gnutls_init(&priv->session, GNUTLS_CLIENT);
    break;
  case INF_XMPP_CONNECTION_SERVER:
    gnutls_init(&priv->session, GNUTLS_SERVER);

    /* If the user wants to check the client's certificate, then require
     * that the client sends one. */
    if(priv->certificate_callback != NULL)
    {
      gnutls_certificate_server_set_request(
        priv->session,
        priv->certificate_request
      );
    }
    break;
  default:
    g_assert_not_reached();
    break;
  }

  gnutls_set_default_priority(priv->session);

#if 0
  gnutls_protocol_set_priority(
    priv->session,
    xmpp_connection_protocol_priority
  );
  gnutls_cipher_set_priority(priv->session, xmpp_connection_cipher_priority);
  gnutls_compression_set_priority(
    priv->session,
    xmpp_connection_comp_priority
  );
  gnutls_kx_set_priority(priv->session, xmpp_connection_kx_priority);
  gnutls_mac_set_priority(priv->session, xmpp_connection_mac_priority);
#endif

  gnutls_credentials_set(
    priv->session,
    GNUTLS_CRD_CERTIFICATE,
    inf_certificate_credentials_get(priv->creds)
  );

  gnutls_dh_set_prime_bits(priv->session, xmpp_connection_dh_bits);

  gnutls_transport_set_ptr(priv->session, xmpp);

  gnutls_transport_set_push_function(
    priv->session,
    inf_xmpp_connection_tls_push
  );

  gnutls_transport_set_pull_function(
    priv->session,
    inf_xmpp_connection_tls_pull
  );

  priv->status = INF_XMPP_CONNECTION_HANDSHAKING;
  inf_xmpp_connection_tls_handshake(xmpp);
}

/*
 * Gsasl setup
 */

static gboolean
inf_xmpp_connection_sasl_has_mechanism(const char* mechlist,
                                       const char* mechanism)
{
  size_t len;
  const char* res;

  for(len = strlen(mechanism); mechlist != NULL; mechlist = strchr(res, ' '))
  {
    res = strstr(mechlist, mechanism);

    if(res == NULL)
      return FALSE;

    if( (res == mechlist  || isspace(res[ -1])) &&
        (res[len] == '\0' || isspace(res[len])))
      return TRUE;
  }
  return FALSE;
}

static void
inf_xmpp_connection_sasl_finish(InfXmppConnection* xmpp,
                                gboolean success)
{
  InfXmppConnectionPrivate* priv;
  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  /* Can be NULL already if we have aborted the authentication before but were
   * still waiting for the server to acknowledge. */
  if(priv->sasl_session != NULL)
  {
    inf_sasl_context_stop_session(priv->sasl_context, priv->sasl_session);
    priv->sasl_session = NULL;
  }

  if(success)
  {
    if(priv->sasl_error != NULL)
    {
      g_error_free(priv->sasl_error);
      priv->sasl_error = NULL;
    }

    /* Authentication done, switch to AUTH_CONNECTED */
    priv->status = INF_XMPP_CONNECTION_AUTH_CONNECTED;

    if(priv->site == INF_XMPP_CONNECTION_CLIENT)
    {
      g_assert(priv->sasl_remote_mechanisms != NULL);
      g_free(priv->sasl_remote_mechanisms);
      priv->sasl_remote_mechanisms = NULL;
    }
    else
    {
      g_assert(priv->sasl_remote_mechanisms == NULL);
    }

    /* We might be in a XML callback here, so do not initiate the stream right
     * now because it replaces the XML parser. The stream is reinitiated in
     * received_cb(). */
    if(priv->parsing == 0)
      inf_xmpp_connection_initiate(xmpp);
  }
  else
  {
    /* Authentication failed, switch to AWAITING_FEATURES/INITIATED
     * for possible retry */
    if(priv->site == INF_XMPP_CONNECTION_CLIENT)
      priv->status = INF_XMPP_CONNECTION_AWAITING_FEATURES;
    else
      priv->status = INF_XMPP_CONNECTION_INITIATED;
  }
}

/* Emits the error signal for the given SASL error code and sends an
 * authentication failure to the other site. */
static void
inf_xmpp_connection_sasl_error(InfXmppConnection* xmpp,
                               const GError* error)
{
  InfXmppConnectionPrivate* priv;
  InfXmppConnectionAuthError auth_code;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);
  inf_xml_connection_error(INF_XML_CONNECTION(xmpp), error);

  if(priv->site == INF_XMPP_CONNECTION_SERVER)
  {
    /* Find matching auth error code to send to client */
    switch(error->code)
    {
    case GSASL_UNKNOWN_MECHANISM:
    case GSASL_MECHANISM_PARSE_ERROR:
      auth_code = INF_XMPP_CONNECTION_AUTH_ERROR_INVALID_MECHANISM;
      break;
    case GSASL_BASE64_ERROR:
      auth_code = INF_XMPP_CONNECTION_AUTH_ERROR_INCORRECT_ENCODING;
      break;
    case GSASL_AUTHENTICATION_ERROR:
      auth_code = INF_XMPP_CONNECTION_AUTH_ERROR_NOT_AUTHORIZED;
      break;
    default:
      auth_code = INF_XMPP_CONNECTION_AUTH_ERROR_TEMPORARY_AUTH_FAILURE;
      break;
    }

    inf_xmpp_connection_send_auth_error(xmpp, auth_code);

    /* Reset state to INITIATED so that the client can retry */
    priv->status = INF_XMPP_CONNECTION_INITIATED;

    inf_xmpp_connection_sasl_finish(xmpp, FALSE);
  }
  else
  {
    inf_xmpp_connection_sasl_finish(xmpp, FALSE);

    /* Just terminate session on client site when a SASL error occurs */
    /* TODO: Better deinitiate here? */
    inf_xmpp_connection_terminate(xmpp);
  }
}

static void
inf_xmpp_connection_sasl_cb(InfSaslContextSession* session,
                            Gsasl_property property,
                            gpointer session_data,
                            gpointer user_data)
{
  InfXmppConnection* xmpp;
  InfXmppConnectionPrivate* priv;

  xmpp = INF_XMPP_CONNECTION(user_data);
  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  switch(property)
  {
  case GSASL_ANONYMOUS_TOKEN:
    inf_sasl_context_session_set_property(
      session,
      GSASL_ANONYMOUS_TOKEN,
      g_get_user_name()
    );

    inf_sasl_context_session_continue(session, GSASL_OK);
    break;
  case GSASL_VALIDATE_ANONYMOUS:
    /* Authentication always successful */
    inf_sasl_context_session_continue(session, GSASL_OK);
    break;
  default:
    /* This callbackfunction is only used when using the built-in SASL
     * context, and this one only supports anonymous authentication. */
    g_assert_not_reached();
    inf_sasl_context_session_continue(session, GSASL_NO_CALLBACK);
    break;
  }
}

static gboolean
inf_xmpp_connection_sasl_ensure(InfXmppConnection* xmpp)
{
  InfXmppConnectionPrivate* priv;
  GError* error;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  if(priv->sasl_context == NULL)
  {
    g_assert(priv->sasl_own_context == NULL);

    error = NULL;
    priv->sasl_own_context = inf_sasl_context_new(&error);

    if(priv->sasl_own_context == NULL)
    {
      inf_xml_connection_error(INF_XML_CONNECTION(xmpp), error);
      g_error_free(error);

      inf_xmpp_connection_terminate(xmpp);
      return FALSE;
    }
    else
    {
      priv->sasl_context = priv->sasl_own_context;
      inf_sasl_context_ref(priv->sasl_context);

      inf_sasl_context_set_callback(
        priv->sasl_context,
        inf_xmpp_connection_sasl_cb,
        xmpp
      );

      g_object_notify(G_OBJECT(xmpp), "sasl-context");
    }
  }

  g_assert(priv->sasl_context != NULL);
  return TRUE;
}

static void
inf_xmpp_connection_sasl_request_feed_func(InfSaslContextSession* session,
                                           const char* data,
                                           gboolean needs_more,
                                           const GError* error,
                                           gpointer user_data)
{
  InfXmppConnection* xmpp;
  InfXmppConnectionPrivate* priv;
  xmlNodePtr reply;

  xmpp = INF_XMPP_CONNECTION(user_data);
  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);
  g_assert(priv->status == INF_XMPP_CONNECTION_AUTHENTICATING);
  g_assert(priv->sasl_session != NULL);

  if(error)
  {
    inf_xmpp_connection_sasl_error(xmpp, error);
  }
  else
  {
    /* We do not need to send a challenge when the authentication
     * has already been completed, but we need to response every
     * challenge. */
    if(data != NULL)
    {
      reply = NULL;
      switch(priv->site)
      {
      case INF_XMPP_CONNECTION_SERVER:
        if(needs_more)
          reply = inf_xmpp_connection_node_new_sasl("challenge");

        break;
      case INF_XMPP_CONNECTION_CLIENT:
        reply = inf_xmpp_connection_node_new_sasl("response");
        break;
      default:
        g_assert_not_reached();
        break;
      }

      if(reply != NULL)
      {
        xmlNodeAddContent(reply, (const xmlChar*)data);
        inf_xmpp_connection_send_xml(xmpp, reply);
        xmlFreeNode(reply);
      }
    }

    /* Send authentication success to client when done */
    if(!needs_more)
    {
      if(priv->site == INF_XMPP_CONNECTION_SERVER)
      {
        reply = inf_xmpp_connection_node_new_sasl("success");
        inf_xmpp_connection_send_xml(xmpp, reply);
        xmlFreeNode(reply);

        inf_xmpp_connection_sasl_finish(xmpp, TRUE);
      }

      /* Wait for <success> from server before calling finish on
       * client side. */
    }
  }
}

static void
inf_xmpp_connection_sasl_request(InfXmppConnection* xmpp,
                                 const gchar* input)
{
  InfXmppConnectionPrivate* priv;
  GError* error;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);
  g_assert(priv->status == INF_XMPP_CONNECTION_AUTHENTICATING);
  g_assert(priv->sasl_session != NULL);

  if(inf_sasl_context_session_is_processing(priv->sasl_session))
  {
    /* We cannot have two requests at the same time. SASL does not allow this,
     * the procedure is always
     * challenge -> response -> challenge -> response -> ...
     * Also, technically, InfSaslContext does not support
     * this at the moment. */
    error = NULL;
    inf_gsasl_set_error(&error, GSASL_INTEGRITY_ERROR);
    inf_xmpp_connection_sasl_error(xmpp, error);
    g_error_free(error);
  }
  else
  {
    inf_sasl_context_session_feed(
      priv->sasl_session,
      input,
      inf_xmpp_connection_sasl_request_feed_func,
      xmpp
    );

    /* Wait for feed_func to be called */
  }
}

static void
inf_xmpp_connection_sasl_init(InfXmppConnection* xmpp,
                              const gchar* mechanism)
{
  InfXmppConnectionPrivate* priv;
  InfIo* io;
  xmlNodePtr auth;
  GError* error;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  g_assert(priv->status != INF_XMPP_CONNECTION_AUTHENTICATING);
  g_assert(priv->sasl_context != NULL);
  g_assert(priv->sasl_session == NULL);
  g_assert(priv->tcp != NULL);

  if(priv->sasl_error != NULL)
  {
    g_error_free(priv->sasl_error);
    priv->sasl_error = NULL;
  }
  
  g_object_get(G_OBJECT(priv->tcp), "io", &io, NULL);
  g_assert(io != NULL);

  switch(priv->site)
  {
  case INF_XMPP_CONNECTION_CLIENT:
    auth = inf_xmpp_connection_node_new_sasl("auth");

    xmlNewProp(
      auth,
      (const xmlChar*)"mechanism",
      (const xmlChar*)mechanism
    );

    inf_xmpp_connection_send_xml(xmpp, auth);
    xmlFreeNode(auth);

    g_assert(priv->status == INF_XMPP_CONNECTION_AWAITING_FEATURES);

    error = NULL;
    priv->sasl_session = inf_sasl_context_client_start_session(
      priv->sasl_context,
      io,
      mechanism,
      xmpp,
      &error
    );

    break;
  case INF_XMPP_CONNECTION_SERVER:
    g_assert(priv->status == INF_XMPP_CONNECTION_INITIATED);
    
    error = NULL;
    priv->sasl_session = inf_sasl_context_server_start_session(
      priv->sasl_context,
      io,
      mechanism,
      xmpp,
      &error
    );

    break;
  default:
    g_assert_not_reached();
    break;
  }

  g_object_unref(io);

  if(error != NULL)
  {
    inf_xmpp_connection_sasl_error(xmpp, error);
    g_error_free(error);
  }
  else
  {
    priv->status = INF_XMPP_CONNECTION_AUTHENTICATING;

    /* Begin on server site */
    if(priv->site == INF_XMPP_CONNECTION_SERVER)
      inf_xmpp_connection_sasl_request(xmpp, NULL);
  }
}

/*
 * XMPP messaging
 */

/* This does actually process the start_element event after several
 * special cases have been handled in sax_start_element(). */
static void
inf_xmpp_connection_process_start_element(InfXmppConnection* xmpp,
                                          const xmlChar* name,
                                          const xmlChar** attrs)
{
  InfXmppConnectionPrivate* priv;
  xmlNodePtr node;

  const xmlChar** attr;
  const xmlChar* attr_name;
  const xmlChar* attr_value;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);
  node = xmlNewNode(NULL, name);

  if(attrs != NULL)
  {
    attr = attrs;
    while(*attr != NULL)
    {
      attr_name = *attr;
      ++ attr;

      attr_value = *attr;
      ++ attr;

      xmlNewProp(node, attr_name, attr_value);
    }
  }

  if(priv->root == NULL)
  {
    g_assert(priv->cur == NULL);

    priv->root = node;
    priv->cur = node;
  }
  else
  {
    g_assert(priv->cur != NULL);
    priv->cur = xmlAddChild(priv->cur, node);
  }
}

static void
inf_xmpp_connection_process_connected(InfXmppConnection* xmpp,
                                      const xmlChar** attrs)
{
  /* TODO: xml:lang and id field are missing here */
  static const gchar xmpp_connection_initial_request[] = 
    "<stream:stream xmlns:stream=\"http://etherx.jabber.org/streams\" "
    "xmlns=\"jabber:client\" version=\"1.0\" from=\"%s\">";

  InfXmppConnectionPrivate* priv;
  char* mech_list;
  char* begin;
  char* end;

  gchar* reply;

  xmlNodePtr features;
  xmlNodePtr starttls;
  xmlNodePtr mechanisms;
  xmlNodePtr mechanism;
  gchar* mechanism_dup;
  GError* error;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);
  g_assert(priv->site == INF_XMPP_CONNECTION_SERVER);
  g_assert(priv->parser != NULL);

  g_assert(priv->status == INF_XMPP_CONNECTION_CONNECTED ||
           priv->status == INF_XMPP_CONNECTION_AUTH_CONNECTED);

  reply = g_strdup_printf(
    xmpp_connection_initial_request,
    priv->local_hostname
  );

  inf_xmpp_connection_send_chars(xmpp, reply, strlen(reply));
  g_free(reply);

  /* <stream:stream> was sent, so change status to initiated */
  switch(priv->status)
  {
  case INF_XMPP_CONNECTION_CONNECTED:
    priv->status = INF_XMPP_CONNECTION_INITIATED;
    break;
  case INF_XMPP_CONNECTION_AUTH_CONNECTED:
    priv->status = INF_XMPP_CONNECTION_AUTH_INITIATED;
    break;
  default:
    g_assert_not_reached();
    break;
  }

  features = xmlNewNode(NULL, (const xmlChar*)"stream:features");

  /* Don't offer TLS if we have already authenticated. It's pointless now. */
  if(priv->session == NULL &&
     priv->status != INF_XMPP_CONNECTION_AUTH_INITIATED)
  {
    if(priv->security_policy != INF_XMPP_CONNECTION_SECURITY_ONLY_UNSECURED)
    {
      starttls = inf_xmpp_connection_node_new_tls("starttls");
      xmlAddChild(features, starttls);
      if(priv->security_policy == INF_XMPP_CONNECTION_SECURITY_ONLY_TLS)
        xmlNewChild(starttls, NULL, (const xmlChar*)"required", NULL);
    }
  }

  if(priv->status == INF_XMPP_CONNECTION_INITIATED)
  {
    /* Not yet authenticated, so give the client a list of authentication
     * mechanisms. */
    mechanisms = inf_xmpp_connection_node_new_sasl("mechanisms");
    xmlAddChild(features, mechanisms);

    /* Ensure that priv->sasl_context exists */
    if(inf_xmpp_connection_sasl_ensure(xmpp) == FALSE)
    {
      /* Error occured during SASL initialization */
      xmlFreeNode(features);
      return;
    }
    else if(priv->sasl_own_context != NULL)
    {
      /* Do only provide anonymous authentication when using own context */
      xmlNewTextChild(
        mechanisms,
        NULL,
        (const xmlChar*)"mechanism",
        (const xmlChar*)"ANONYMOUS"
      );
    }
    else
    {
      if(priv->sasl_local_mechanisms == NULL)
      {
        error = NULL;
        mech_list = inf_sasl_context_server_list_mechanisms(
          priv->sasl_context,
          &error
        );

        if(error != NULL)
        {
          xmlFreeNode(features);

          inf_xml_connection_error(INF_XML_CONNECTION(xmpp), error);
          g_error_free(error);

          inf_xmpp_connection_terminate(xmpp);
          return;
        }
      }
      else
      {
        mech_list = priv->sasl_local_mechanisms;
      }

      begin = end = mech_list;
      while(*end != '\0')
      {
        end = strpbrk(begin, " \t\r\n");
        if(end == NULL) end = begin + strlen(begin);
        mechanism_dup = g_strndup(begin, end - begin);

        if(inf_sasl_context_server_supports_mechanism(priv->sasl_context,
                                                      mechanism_dup))
        {
          mechanism = xmlNewChild(
            mechanisms,
            NULL,
            (const xmlChar*)"mechanism",
            NULL
          );

          xmlNodeAddContentLen(mechanism, (const xmlChar*)begin, end - begin);
        }

        g_free(mechanism_dup);
        begin = end + 1;
      }

      if(priv->sasl_local_mechanisms == NULL)
        gsasl_free(mech_list);
    }
  }

  inf_xmpp_connection_send_xml(xmpp, features);
  xmlFreeNode(features);

  if(priv->status == INF_XMPP_CONNECTION_AUTH_INITIATED)
  {
    /* Authentication done, <stream:features> sent. Session is ready. */
    priv->status = INF_XMPP_CONNECTION_READY;
    g_object_notify(G_OBJECT(xmpp), "status");
  }
}

static void
inf_xmpp_connection_process_initiated(InfXmppConnection* xmpp,
                                      xmlNodePtr xml)
{
  InfXmppConnectionPrivate* priv;
  xmlNodePtr proceed;
  xmlChar* mech;
  gboolean has_mechanism;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);
  g_assert(priv->site == INF_XMPP_CONNECTION_SERVER);
  g_assert(priv->status == INF_XMPP_CONNECTION_INITIATED);

  /* TODO: Actually, RFC 3920 specifies in 5.1.3 that we MUST offer the
   * starttls attribute if the client's stream version is at least 1.0. We
   * don't do so if security_policy is
   * INF_XMPP_CONNECTION_SECURITY_ONLY_UNSECURED to allow ad-hoc unsecured
   * infinote sessions that don't need all this certificate stuff. */

  /* I'm not totally sure how to do this in full compliance with the RFC.
   * Maybe we can ship with a simple self-signed ad-hoc certificate. */
  if(priv->session == NULL &&
     priv->security_policy != INF_XMPP_CONNECTION_SECURITY_ONLY_UNSECURED)
  {
    if(strcmp((const gchar*)xml->name, "starttls") == 0)
    {
      proceed = inf_xmpp_connection_node_new_tls("proceed");
      inf_xmpp_connection_send_xml(xmpp, proceed);
      xmlFreeNode(proceed);

      inf_xmpp_connection_tls_init(xmpp);
    }
    else if(priv->security_policy == INF_XMPP_CONNECTION_SECURITY_ONLY_TLS)
    {
      inf_xmpp_connection_terminate_error(
        xmpp,
        INF_XMPP_CONNECTION_STREAM_ERROR_NOT_AUTHORIZED,
        _("Stream is not yet secured with TLS")
      );
    }
  }

  /* If we handled one of the cases above, then we don't want to check for
   * authentication here. In that case, the status has already changed. */
  if(priv->status == INF_XMPP_CONNECTION_INITIATED)
  {
    /* This should already have been allocated before having sent the list
     * of mechanisms to the client. */
    g_assert(priv->sasl_context != NULL);
    if(strcmp((const gchar*)xml->name, "auth") == 0)
    {
      mech = xmlGetProp(xml, (const xmlChar*)"mechanism");

      has_mechanism = TRUE;
      if(mech == NULL)
      {
        has_mechanism = FALSE;
      }
      else if(!inf_sasl_context_server_supports_mechanism(priv->sasl_context,
                                                          (const char*)mech))
      {
        has_mechanism = FALSE;
      }
      else if(priv->sasl_own_context == NULL &&
              priv->sasl_local_mechanisms != NULL &&
              !inf_xmpp_connection_sasl_has_mechanism(
                  priv->sasl_local_mechanisms,
                  (const char*)mech))
      {
        has_mechanism = FALSE;
      }
      else if(priv->sasl_own_context != NULL &&
              g_ascii_strcasecmp((const char*)mech, "ANONYMOUS") != 0)
      {
        has_mechanism = FALSE;
      }

      if(has_mechanism)
      {
        inf_xmpp_connection_sasl_init(xmpp, (const gchar*)mech);
      }
      else
      {
        /* Keep state for the client to retry */
        inf_xmpp_connection_send_auth_error(
          xmpp,
          INF_XMPP_CONNECTION_AUTH_ERROR_INVALID_MECHANISM
        );

        inf_xmpp_connection_emit_auth_error(
          xmpp,
          INF_XMPP_CONNECTION_AUTH_ERROR_INVALID_MECHANISM
        );
      }

      if(mech != NULL)
        xmlFree(mech);
    }
    else
    {
      /* Got something else than <auth> */
      inf_xmpp_connection_terminate_error(
        xmpp,
        INF_XMPP_CONNECTION_STREAM_ERROR_NOT_AUTHORIZED,
        _("Stream is not yet authorized")
      );
    }
  }
}

static void
inf_xmpp_connection_load_sasl_remote_mechanisms(InfXmppConnection* xmpp,
                                                xmlNodePtr xml)
{
  InfXmppConnectionPrivate* priv;
  GString* mechanisms_string;
  xmlNodePtr child;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  g_assert(priv->sasl_remote_mechanisms == NULL);

  mechanisms_string = g_string_sized_new(128);

  for(child = xml->children; child != NULL; child = child->next)
  {
    if(strcmp((const gchar*)child->name, "mechanism") == 0 &&
       child->children != NULL &&
       child->children->content != NULL)
    {
      g_string_append(
        mechanisms_string,
        (const char*) child->children->content
      );

      if(child->next != NULL)
        g_string_append_c(mechanisms_string, ' ');
    }
  }

  priv->sasl_remote_mechanisms = g_string_free(mechanisms_string, FALSE);
}

static const gchar*
inf_xmpp_connection_sasl_suggest_mechanism(InfXmppConnection* xmpp,
                                           GError** error)
{
  InfXmppConnectionPrivate* priv;

  GString* mechanisms_string;
  GString* mechanism_string;
  char* iter;
  char* end;
  const gchar* suggestion;
  gboolean has_mechanism;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  if(priv->sasl_own_context != NULL)
  {
    /* We only support ANONYMOUS authentication when using the built-in
     * SASL context. */
    for(iter = priv->sasl_remote_mechanisms; *iter; iter = end + 1)
    {
      end = strchr(iter, ' ');
      if(end == NULL)
      {
        if(strcmp(iter, "ANONYMOUS") == 0)
          suggestion = "ANONYMOUS";
        break;
      }

      if(strncmp(iter, "ANONYMOUS",
                 MAX((size_t) (end - iter), sizeof("ANONYMOUS") - 1) == 0))
      {
        suggestion = "ANONYMOUS";
        break;
      }
    }
  }
  else
  {
    mechanisms_string = g_string_sized_new(128);
    mechanism_string = g_string_sized_new(16); /* should fit most names */
    for(end = iter = priv->sasl_remote_mechanisms; end; iter = end + 1)
    {
      end = strchr(iter, ' ');
      if(end != NULL)
      {
        g_string_overwrite_len(mechanism_string, 0, iter, end - iter);
        g_string_truncate(mechanism_string, end - iter);
        iter = mechanism_string->str;
      }

      has_mechanism =
        inf_sasl_context_client_supports_mechanism(priv->sasl_context, iter);

      if(has_mechanism == TRUE && priv->sasl_local_mechanisms != NULL)
      {
        has_mechanism = inf_xmpp_connection_sasl_has_mechanism(
          priv->sasl_local_mechanisms,
          iter
        );
      }

      if(has_mechanism == TRUE)
      {
        if(mechanisms_string->len > 0)
          g_string_append_c(mechanisms_string, ' ');

        g_string_append(mechanisms_string, iter);
      }
    }

    if(mechanisms_string->len > 0)
    {
      suggestion = inf_sasl_context_client_suggest_mechanism(
        priv->sasl_context,
        mechanisms_string->str
      );
    }

    g_string_free(mechanisms_string, TRUE);
  }

  if(suggestion == NULL)
  {
    g_set_error(
      error,
      inf_xmpp_connection_error_quark,
      INF_XMPP_CONNECTION_ERROR_NO_SUITABLE_MECHANISM,
      "%s",
      _("The server does not offer a suitable authentication mechanism")
    );
  }

  return suggestion;
}

static void
inf_xmpp_connection_process_features(InfXmppConnection* xmpp,
                                     xmlNodePtr xml)
{
  InfXmppConnectionPrivate* priv;
  xmlNodePtr child;
  xmlNodePtr req;
  xmlNodePtr starttls;
  const char* suggestion;
  GError* error;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);
  g_assert(priv->site == INF_XMPP_CONNECTION_CLIENT);
  g_assert(priv->status == INF_XMPP_CONNECTION_AWAITING_FEATURES ||
           priv->status == INF_XMPP_CONNECTION_AUTH_AWAITING_FEATURES);

  if(strcmp((const gchar*)xml->name, "stream:features") != 0)
  {
    /* Server sent something else. Don't know what it is, so let us ignore it.
     * Perhaps the <stream:features> we are waiting for follows later. */
    return;
  }
  /* Don't try TLS anymore if we are already authenticated. This can happen
   * if the server only offers TLS after authentication, but that's stupid. */
  else if(priv->status == INF_XMPP_CONNECTION_AWAITING_FEATURES &&
          priv->session == NULL)
  {
    for(child = xml->children; child != NULL; child = child->next)
      if(strcmp((const gchar*)child->name, "starttls") == 0)
        break;

    /* Server has no StartTLS feature. We don't like that. */
    if(child == NULL &&
       priv->security_policy == INF_XMPP_CONNECTION_SECURITY_ONLY_TLS)
    {
      error = g_error_new_literal(
        inf_xmpp_connection_error_quark,
        INF_XMPP_CONNECTION_ERROR_TLS_UNSUPPORTED,
        _("The server does not support transport layer security (TLS)")
      );

      inf_xml_connection_error(INF_XML_CONNECTION(xmpp), error);
      g_error_free(error);

      inf_xmpp_connection_deinitiate(xmpp);
    }
    else if(child != NULL)
    {
      for(req = child->children; req != NULL; req = req->next)
        if(strcmp((const gchar*)req->name, "required") == 0)
          break;

      if(req != NULL &&
         priv->security_policy == INF_XMPP_CONNECTION_SECURITY_ONLY_UNSECURED)
      {
        error = NULL;
        g_set_error(
          &error,
          inf_xmpp_connection_error_quark,
          INF_XMPP_CONNECTION_ERROR_TLS_REQUIRED,
          "%s",
          _("The server requires transport layer security (TLS)")
        );

        inf_xml_connection_error(INF_XML_CONNECTION(xmpp), error);
        g_error_free(error);

        inf_xmpp_connection_deinitiate(xmpp);
      }
      /* The server supports TLS. Now, request TLS if it's required or if
       * we do prefer it. */
      else if(req != NULL || inf_xmpp_connection_prefers_tls(xmpp))
      {
        starttls = inf_xmpp_connection_node_new_tls("starttls");
        inf_xmpp_connection_send_xml(xmpp, starttls);
        xmlFreeNode(starttls);

        priv->status = INF_XMPP_CONNECTION_ENCRYPTION_REQUESTED;
      }
    }
  }

  /* If we did not request TLS above, then go on with authentication */
  if(priv->status == INF_XMPP_CONNECTION_AWAITING_FEATURES)
  {
    for(child = xml->children; child != NULL; child = child->next)
      if(strcmp((const gchar*)child->name, "mechanisms") == 0)
        break;

    /* Server does not provide authentication mechanisms */
    if(child == NULL)
    {
      error = g_error_new_literal(
        inf_xmpp_connection_error_quark,
        INF_XMPP_CONNECTION_ERROR_AUTHENTICATION_UNSUPPORTED,
        _("The server does not provide any authentication mechanism")
      );

      inf_xml_connection_error(INF_XML_CONNECTION(xmpp), error);
      g_error_free(error);

      inf_xmpp_connection_deinitiate(xmpp);
    }
    else if(inf_xmpp_connection_sasl_ensure(xmpp) == TRUE)
    {
      inf_xmpp_connection_load_sasl_remote_mechanisms(xmpp, child);

      error = NULL;
      suggestion = inf_xmpp_connection_sasl_suggest_mechanism(xmpp, &error);

      if(!suggestion)
      {
        inf_xml_connection_error(INF_XML_CONNECTION(xmpp), error);
        g_error_free(error);

        /* Deinitiate if error signal handler does not retry authentication */
        if(priv->status == INF_XMPP_CONNECTION_AWAITING_FEATURES)
          inf_xmpp_connection_deinitiate(xmpp);
      }
      else
      {
        inf_xmpp_connection_sasl_init(xmpp, suggestion);
      }
    }
  }
  else if(priv->status == INF_XMPP_CONNECTION_AUTH_AWAITING_FEATURES)
  {
    priv->status = INF_XMPP_CONNECTION_READY;
    g_object_notify(G_OBJECT(xmpp), "status");
  }
}

static void
inf_xmpp_connection_process_encryption(InfXmppConnection* xmpp,
                                       xmlNodePtr xml)
{
  InfXmppConnectionPrivate* priv;
  GError* error;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);
  g_assert(priv->site == INF_XMPP_CONNECTION_CLIENT);
  g_assert(priv->status == INF_XMPP_CONNECTION_ENCRYPTION_REQUESTED);
  g_assert(priv->session == NULL);

  if(strcmp((const gchar*)xml->name, "proceed") == 0)
  {
    inf_xmpp_connection_tls_init(xmpp);
  }
  else if(strcmp((const gchar*)xml->name, "failure") == 0)
  {
    error = g_error_new_literal(
      inf_xmpp_connection_error_quark,
      INF_XMPP_CONNECTION_ERROR_TLS_FAILURE,
      _("The server cannot perform the TLS handshake")
    );

    inf_xml_connection_error(INF_XML_CONNECTION(xmpp), error);
    g_error_free(error);

    /* The server is required to close the stream after failure, so wait
     * for </stream:stream>. */
    priv->status = INF_XMPP_CONNECTION_CLOSING_STREAM;
    g_object_notify(G_OBJECT(xmpp), "status");
  }
  else
  {
    /* We got neither 'proceed' nor 'failure'. Ignore and wait for either
     * of them. */
  }
}

static void
inf_xmpp_connection_process_authentication_error(
  InfXmppConnection* xmpp,
  InfXmppConnectionAuthError auth_code)
{
  InfXmppConnectionPrivate* priv;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  g_assert(priv->site == INF_XMPP_CONNECTION_CLIENT);
  g_assert(priv->status == INF_XMPP_CONNECTION_AUTHENTICATING);

  inf_xmpp_connection_sasl_finish(xmpp, FALSE);
  inf_xmpp_connection_emit_auth_error(xmpp, auth_code);

  /* Deinitiate connection if the signal handler of the auth error did not
   * call inf_xmpp_connection_retry_sasl_authentication(). */
  if(priv->status == INF_XMPP_CONNECTION_AWAITING_FEATURES)
    inf_xmpp_connection_deinitiate(xmpp);
}


static void
inf_xmpp_connection_process_authentication(InfXmppConnection* xmpp,
                                           xmlNodePtr xml)
{
  InfXmppConnectionPrivate* priv;
  InfXmppConnectionAuthError auth_code;
  xmlNodePtr child;
  xmlNodePtr error_node;
  xmlChar* content;
  GError* local_error;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);
  g_assert(priv->status == INF_XMPP_CONNECTION_AUTHENTICATING);

  switch(priv->site)
  {
  case INF_XMPP_CONNECTION_CLIENT:
    if(strcmp((const gchar*)xml->name, "challenge") == 0)
    {
      /* Ignore if authentication was aborted by
       * inf_xmpp_connection_reset_sasl_authentication() */
      if(priv->sasl_session != NULL)
      {
        /* Process challenge from server */
        content = xmlNodeGetContent(xml);
        inf_xmpp_connection_sasl_request(xmpp, (const gchar*)content);
        xmlFree(content);
      }
    }
    else if(strcmp((const gchar*)xml->name, "failure") == 0)
    {
      child = xml->children;
      auth_code = INF_XMPP_CONNECTION_AUTH_ERROR_FAILED;

      if(child != NULL)
      {
        error_node = child->children;

        if(error_node != NULL &&
           strcmp((const char*)error_node->name, "error") == 0)
        {
          g_assert(priv->sasl_error == NULL);
          priv->sasl_error = inf_xml_util_new_error_from_node(error_node);
        }

        auth_code = inf_xmpp_connection_auth_error_from_condition(
          (const gchar*)child->name
        );
      }

      inf_xmpp_connection_process_authentication_error(xmpp, auth_code);
    }
    else if(strcmp((const gchar*)xml->name, "success") == 0)
    {
      inf_xmpp_connection_sasl_finish(xmpp, TRUE);
    }
    else
    {
      /* Unknown request. Ignore. */
    }

    break;
  case INF_XMPP_CONNECTION_SERVER:
    if(strcmp((const gchar*)xml->name, "response") == 0)
    {
      if(priv->sasl_session != NULL)
      {
        /* Process client reponse */
        content = xmlNodeGetContent(xml);
        inf_xmpp_connection_sasl_request(xmpp, (const gchar*)content);
        xmlFree(content);
      }
      else
      {
        /* If priv->sasl_session is NULL then the authentication was aborted
         * via inf_xmpp_connection_reset_sasl_authentication(). */
        inf_xmpp_connection_sasl_finish(xmpp, FALSE);

        /* Build and set detail error to send to client */
        local_error = g_error_new_literal(
          inf_authentication_detail_error_quark(),
          INF_AUTHENTICATION_DETAIL_ERROR_TRY_AGAIN,
          inf_authentication_detail_strerror(
            INF_AUTHENTICATION_DETAIL_ERROR_TRY_AGAIN)
        );

        inf_xmpp_connection_set_sasl_error(xmpp, local_error);
        g_error_free(local_error);

        /* Notify client that the authentication failed and ask it to try again */
        inf_xmpp_connection_send_auth_error(
          xmpp,
          INF_XMPP_CONNECTION_AUTH_ERROR_TEMPORARY_AUTH_FAILURE
        );
        inf_xmpp_connection_emit_auth_error(
          xmpp,
          INF_XMPP_CONNECTION_AUTH_ERROR_TEMPORARY_AUTH_FAILURE
        );

        /* Can be reset if a signal handler retried or
         * reset the authentication. */
        if(priv->sasl_error)
        {
          g_error_free(priv->sasl_error);
          priv->sasl_error = NULL;
        }
      }
    }
    else if(strcmp((const gchar*)xml->name, "abort") == 0)
    {
      /* Fall back to initiated state, wait for another auth request. */
      inf_xmpp_connection_sasl_finish(xmpp, FALSE);

      inf_xmpp_connection_send_auth_error(
        xmpp,
        INF_XMPP_CONNECTION_AUTH_ERROR_ABORTED
      );

      inf_xmpp_connection_emit_auth_error(
        xmpp,
        INF_XMPP_CONNECTION_AUTH_ERROR_ABORTED
      );
    }

    break;
  default:
    g_assert_not_reached();
    break;
  }
}

/* This actually processes the end element after having handled some
 * special cases in sax_end_element(). */
static void
inf_xmpp_connection_process_end_element(InfXmppConnection* xmpp,
                                        const xmlChar* name)
{
  InfXmppConnectionPrivate* priv;
  InfXmppConnectionStreamError stream_code;
  GError* error;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  g_assert(priv->cur != NULL);
  /* This should have raised a sax_error. */
  g_assert(strcmp((const gchar*)priv->cur->name, (const gchar*)name) == 0);

  priv->cur = priv->cur->parent;
  if(priv->cur == NULL)
  {
    /* Got a complete XML message */
    if(strcmp((const gchar*)name, "stream:error") == 0)
    {
      /* Just emit error signal in this case. If the stream is supposed to
       * be closed, a </stream:stream> should follow. */
      stream_code = INF_XMPP_CONNECTION_STREAM_ERROR_FAILED;
      if(priv->root->children != NULL)
      {
        stream_code = inf_xmpp_connection_stream_error_from_condition(
          (const gchar*)priv->root->children->name
        );
      }

      error = NULL;
      g_set_error(
        &error,
        inf_xmpp_connection_stream_error_quark,
        stream_code,
        "%s",
        inf_xmpp_connection_stream_strerror(stream_code)
      );

      /* TODO: Incorporate text child of the stream:error request, if any */

      inf_xml_connection_error(INF_XML_CONNECTION(xmpp), error);
      g_error_free(error);
    }
    else
    {
      switch(priv->status)
      {
      case INF_XMPP_CONNECTION_INITIATED:
        /* The client should be waiting for <stream:stream> from the server
         * in this state, and sax_end_element() should not have called this
         * function. */
        g_assert(priv->site == INF_XMPP_CONNECTION_SERVER);
        inf_xmpp_connection_process_initiated(xmpp, priv->root);
        break;
      case INF_XMPP_CONNECTION_AWAITING_FEATURES:
      case INF_XMPP_CONNECTION_AUTH_AWAITING_FEATURES:
        /* This is a client-only state */
        g_assert(priv->site == INF_XMPP_CONNECTION_CLIENT);
        inf_xmpp_connection_process_features(xmpp, priv->root);
        break;
      case INF_XMPP_CONNECTION_ENCRYPTION_REQUESTED:
        /* This is a client-only state */
        g_assert(priv->site == INF_XMPP_CONNECTION_CLIENT);
        inf_xmpp_connection_process_encryption(xmpp, priv->root);
        break;
      case INF_XMPP_CONNECTION_AUTHENTICATING:
        inf_xmpp_connection_process_authentication(xmpp, priv->root);
        break;
      case INF_XMPP_CONNECTION_READY:
        inf_xml_connection_received(INF_XML_CONNECTION(xmpp), priv->root);
        break;
      case INF_XMPP_CONNECTION_CLOSING_STREAM:
        /* We are waiting for </stream:stream>. It can be that we receive
         * other XML nodes from the remote side before that happens, but we
         * ignore them here. */
        break;
      case INF_XMPP_CONNECTION_AUTH_INITIATED:
        /* The client should be waiting for <stream:stream> from the server
         * in this state, and sax_end_element should not have called this
         * function. Also, this is a client-only state (the server goes
         * directly to READY after having received <stream:stream>). */
      case INF_XMPP_CONNECTION_CONNECTING:
      case INF_XMPP_CONNECTION_CONNECTED:
      case INF_XMPP_CONNECTION_AUTH_CONNECTED:
      case INF_XMPP_CONNECTION_HANDSHAKING:
      case INF_XMPP_CONNECTION_CLOSING_GNUTLS:
      case INF_XMPP_CONNECTION_CLOSED:
      default:
        g_assert_not_reached();
        break;
      }
    }

    xmlFreeNode(priv->root);
    priv->root = NULL;
    priv->cur = NULL;
  }
}

static void
inf_xmpp_connection_sax_start_element(void* context,
                                      const xmlChar* name,
                                      const xmlChar** attrs)
{
  InfXmppConnection* xmpp;
  InfXmppConnectionPrivate* priv;

  xmpp = INF_XMPP_CONNECTION(context);
  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  /* This can happen when both a chunk which causes the connection to
   * terminate and the start of an element happen in the same call to
   * xmlParseChunk */
  if(priv->status == INF_XMPP_CONNECTION_CLOSING_GNUTLS)
    return;

  switch(priv->status)
  {
  case INF_XMPP_CONNECTION_CONNECTED:
  case INF_XMPP_CONNECTION_AUTH_CONNECTED:
    /* The first thing the client does in this state is sending <stream:stream>
     * and switching to the initiated state. */
    g_assert(priv->site == INF_XMPP_CONNECTION_SERVER);
    if(strcmp((const gchar*)name, "stream:stream") != 0)
    {
      /* Did not get <stream:stream>, but something else. */
      /* TODO: Produce an error here, so the user knows what happened */
      inf_xmpp_connection_terminate(xmpp);
    }
    else
    {
      /* Got <stream:stream> from client, send response */
      inf_xmpp_connection_process_connected(xmpp, attrs);
    }

    break;
  case INF_XMPP_CONNECTION_INITIATED:
  case INF_XMPP_CONNECTION_AUTH_INITIATED:
    if(priv->site == INF_XMPP_CONNECTION_CLIENT)
    {
      /* We are waiting for <stream:stream> from the server. */
      if(strcmp((const gchar*)name, "stream:stream") != 0)
      {
        /* Did not get <stream:stream>, but something else. */
        inf_xmpp_connection_terminate(xmpp);
      }
      else
      {
        /* Got <stream:stream>, wait for <stream:features> now so that
         * we can start TLS or authentication if the server supports it. */
        /* TODO: Read server's JID, if a from field is given? However, the RFC
         * suggests we SHOULD silently ignore it. */
        if(priv->status == INF_XMPP_CONNECTION_INITIATED)
          priv->status = INF_XMPP_CONNECTION_AWAITING_FEATURES;
        else
          priv->status = INF_XMPP_CONNECTION_AUTH_AWAITING_FEATURES;
      }
    }
    else
    {
      inf_xmpp_connection_process_start_element(xmpp, name, attrs);
    }

    break;
  case INF_XMPP_CONNECTION_CLOSING_STREAM:
    /* We are still processing messages if we are waiting for
     * </stream:stream>, but are discarding them. */
  case INF_XMPP_CONNECTION_AWAITING_FEATURES:
  case INF_XMPP_CONNECTION_AUTH_AWAITING_FEATURES:
  case INF_XMPP_CONNECTION_ENCRYPTION_REQUESTED:
  case INF_XMPP_CONNECTION_AUTHENTICATING:
  case INF_XMPP_CONNECTION_READY:
    inf_xmpp_connection_process_start_element(xmpp, name, attrs);
    break;
  case INF_XMPP_CONNECTION_CLOSING_GNUTLS:
  case INF_XMPP_CONNECTION_HANDSHAKING:
    /* received_cb should not call the XML parser in these states */
  case INF_XMPP_CONNECTION_CLOSED:
  case INF_XMPP_CONNECTION_CONNECTING:
    /* We should not even receive something in these states */
  default:
    g_assert_not_reached();
    break;
  }
}

static void
inf_xmpp_connection_sax_end_element(void* context,
                                    const xmlChar* name)
{
  InfXmppConnection* xmpp;
  InfXmppConnectionPrivate* priv;

  xmpp = INF_XMPP_CONNECTION(context);
  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  g_assert(priv->status != INF_XMPP_CONNECTION_HANDSHAKING);

  /* This can happen when both a chunk which causes the connection to
   * terminate and the end of an element happen in the same call to
   * xmlParseChunk */
  if(priv->status == INF_XMPP_CONNECTION_CLOSING_GNUTLS)
    return;

  /* If we are not in the toplevel (directly in <stream:stream>) but in some
   * child, process this normally because it belongs to a child. */
  if(priv->root != NULL)
  {
    inf_xmpp_connection_process_end_element(xmpp, name);
  }
  else
  {
    /* Should have caused an error in the XML parser otherwise. The only case
     * where we get an end element for the top-level which is not
     * stream:stream is when the remote part sent a non-stream:stream opening
     * tag and the corresponding closing tag in one go so that we process both
     * in the same XML parser invocation. In that case
     * inf_xmpp_connection_sax_start_element resets the connection status
     * however so also check for that here. See also bug #546. */
    g_assert(strcmp((const gchar*)name, "stream:stream") == 0 ||
             priv->status == INF_XMPP_CONNECTION_CLOSING_GNUTLS ||
             priv->status == INF_XMPP_CONNECTION_CLOSED);

    switch(priv->status)
    {
    case INF_XMPP_CONNECTION_CLOSING_STREAM:
      /* This is the </stream:stream> we were waiting for. */
    case INF_XMPP_CONNECTION_AUTHENTICATING:
      /* I think we should receive a failure first, but some evil server
       * might send </stream:stream> directly. */
    case INF_XMPP_CONNECTION_INITIATED:
    case INF_XMPP_CONNECTION_AUTH_INITIATED:
    case INF_XMPP_CONNECTION_AWAITING_FEATURES:
    case INF_XMPP_CONNECTION_AUTH_AWAITING_FEATURES:
    case INF_XMPP_CONNECTION_ENCRYPTION_REQUESTED:
    case INF_XMPP_CONNECTION_READY:
      /* Also terminate stream in these states */
      inf_xmpp_connection_terminate(xmpp);
      break;
    case INF_XMPP_CONNECTION_CLOSED:
    case INF_XMPP_CONNECTION_CLOSING_GNUTLS:
      /* This can happen if the connection was terminated by start_element and
       * the XML parser processed the corresponding end tag in the same
       * xmlParseChunk() invocation. */
      break;
    case INF_XMPP_CONNECTION_CONNECTED:
    case INF_XMPP_CONNECTION_AUTH_CONNECTED:
      /* We should not get </stream:stream> before we got <stream:stream>,
       * which would have caused us to change into the INITIATED state. The
       * XML parser should have reported an error in this case. */
    case INF_XMPP_CONNECTION_HANDSHAKING:
      /* received_cb should not call the XML parser in these states */
    case INF_XMPP_CONNECTION_CONNECTING:
      /* We should not even receive something in these states */
    default:
      g_assert_not_reached();
      break;
    }
  }
}

static void
inf_xmpp_connection_sax_characters(void* context,
                                   const xmlChar* content,
                                   int len)
{
  InfXmppConnection* xmpp;
  InfXmppConnectionPrivate* priv;

  xmpp = INF_XMPP_CONNECTION(context);
  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  g_assert(priv->status != INF_XMPP_CONNECTION_HANDSHAKING);

  if(priv->root == NULL)
  {
    /* Someone sent content of the <stream:stream> node. Ignore. */
  }
  else
  {
    g_assert(priv->cur != NULL);
    xmlNodeAddContentLen(priv->cur, content, len);
  }
}

static void
inf_xmpp_connection_sax_warning(void* context,
                                const char* msg,
                                ...)
{
  InfXmppConnection* xmpp;
  InfXmppConnectionPrivate* priv;
  InfIpAddress* address;
  gchar* addr_str;
  gchar* warn_str;
  va_list arglist;

  xmpp = INF_XMPP_CONNECTION(context);
  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  g_object_get(G_OBJECT(priv->tcp), "remote-address", &address, NULL);
  addr_str = inf_ip_address_to_string(address);
  inf_ip_address_free(address);

  va_start(arglist, msg);
  warn_str = g_strdup_vprintf(msg, arglist);
  va_end(arglist);

  /* XML Warning from <IP Address>: <Warning Text> */
  g_warning(_("XML warning from %s: %s\n"), addr_str, warn_str);
  g_free(addr_str);
  g_free(warn_str);
}

static void
inf_xmpp_connection_sax_error(void* context,
                              const char* msg,
                              ...)
{
  InfXmppConnection* xmpp;
  InfXmppConnectionPrivate* priv;
  InfXmppConnectionStreamError stream_code;
  xmlErrorPtr error_xml;
  const gchar* message;

  xmpp = INF_XMPP_CONNECTION(context);
  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  error_xml = xmlCtxtGetLastError(priv->parser);
  g_assert(error_xml != NULL);

  /* The XML parser should not be called in this state */
  g_assert(priv->status != INF_XMPP_CONNECTION_HANDSHAKING);

  /* This can happen when both a chunk which causes the connection to
   * terminate and an error happens in the same call to xmlParseChunk */
  if(priv->status == INF_XMPP_CONNECTION_CLOSING_GNUTLS)
    return;

  /* If we are in this state, the server waits already on a GnuTLS
   * handshake, so we cannot send arbitrary XML here. Also cannot
   * send <stream:error> without having sent <stream:stream>. */
  if(priv->status != INF_XMPP_CONNECTION_ENCRYPTION_REQUESTED &&
     priv->status != INF_XMPP_CONNECTION_CONNECTED &&
     priv->status != INF_XMPP_CONNECTION_AUTH_CONNECTED)
  {
    /* TODO: Get more accurate error information from stream error */
    stream_code = INF_XMPP_CONNECTION_STREAM_ERROR_BAD_FORMAT;

    if(error_xml->domain == XML_FROM_PARSER &&
       error_xml->code == XML_ERR_DOCUMENT_EMPTY)
    {
      /* The server sent something which is not XML */
      message = _("Remote site is not an XMPP server");
    }
    else
    {
      /* TODO: Strip leading and trailing whitespace from message */
      message = error_xml->message;
    }

    inf_xmpp_connection_terminate_error(xmpp, stream_code, message);
  }
  else
  {
    /* Just terminate connection without sending stream:error */
    inf_xmpp_connection_terminate(xmpp);
  }
}

static xmlSAXHandler inf_xmpp_connection_handler = {
  NULL,                                   /* internalSubset */
  NULL,                                   /* isStandalone */
  NULL,                                   /* hasInternalSubset */
  NULL,                                   /* hasExternalSubset */
  NULL,                                   /* resolveEntity */
  NULL,                                   /* getEntity */
  NULL,                                   /* entityDecl */
  NULL,                                   /* notationDecl */
  NULL,                                   /* attributeDecl */
  NULL,                                   /* elementDecl */
  NULL,                                   /* unparsedEntityDecl */
  NULL,                                   /* setDocumentLocator */
  NULL,                                   /* startDocument */
  NULL,                                   /* endDocument */
  inf_xmpp_connection_sax_start_element,  /* startElement */
  inf_xmpp_connection_sax_end_element,    /* endElement */
  NULL,                                   /* reference */
  inf_xmpp_connection_sax_characters,     /* characters */
  NULL,                                   /* ignorableWhitespace */
  NULL,                                   /* processingInstruction */
  NULL,                                   /* comment */
  inf_xmpp_connection_sax_warning,        /* warning */
  /* We treat error and fatal error the same */
  inf_xmpp_connection_sax_error,          /* error */
  inf_xmpp_connection_sax_error,          /* fatalError */
  NULL,                                   /* getParameterEntity */
  NULL,                                   /* cdataBlock */
  NULL,                                   /* externalSubset */
  0,                                      /* initialized */
  NULL,                                   /* _private */
  NULL,                                   /* startElementNs */
  NULL,                                   /* endElementNs */
  NULL                                    /* serror */
};

static void
inf_xmpp_connection_initiate(InfXmppConnection* xmpp)
{
  static const gchar xmpp_connection_initial_request[] =
    "<stream:stream version=\"1.0\" xmlns=\"jabber:client\" "
    "xmlns:stream=\"http://etherx.jabber.org/streams\" to=\"%s\">";

  InfXmppConnectionPrivate* priv;
  gchar* request;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  g_assert(priv->status == INF_XMPP_CONNECTION_CONNECTED ||
           priv->status == INF_XMPP_CONNECTION_AUTH_CONNECTED);

  /* Create XML parser for incoming data */
  if(priv->parser != NULL) xmlFreeParserCtxt(priv->parser);
  priv->parser = xmlCreatePushParserCtxt(
    &inf_xmpp_connection_handler,
    xmpp,
    NULL,
    0,
    NULL
  );

  /* Create XML buffer for outgoing data */
  if(priv->buf == NULL)
  {
    priv->buf = xmlBufferCreate();
    priv->doc = xmlNewDoc((const xmlChar*)"1.0");
  }

  if(priv->site == INF_XMPP_CONNECTION_CLIENT)
  {
    request = g_strdup_printf(
      xmpp_connection_initial_request,
      priv->remote_hostname
    );

    inf_xmpp_connection_send_chars(xmpp, request, strlen(request));
    g_free(request);

    switch(priv->status)
    {
    case INF_XMPP_CONNECTION_CONNECTED:
      priv->status = INF_XMPP_CONNECTION_INITIATED;
      break;
    case INF_XMPP_CONNECTION_AUTH_CONNECTED:
      priv->status = INF_XMPP_CONNECTION_AUTH_INITIATED;
      break;
    default:
      g_assert_not_reached();
      break;
    }
  }
}

/*
 * Signal handlers.
 */

static void
inf_xmpp_connection_received_cb_sent_func(InfXmppConnection* xmpp,
                                          gpointer user_data)
{
  InfXmppConnectionPrivate* priv;
  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  /* Terminating </stream:stream> and GnuTLS bye have been sent, so
   * close underlaying TCP connection. This will trigger a TCP status
   * notify. */
  inf_tcp_connection_close(priv->tcp);
}

static void
inf_xmpp_connection_sent_cb(InfTcpConnection* tcp,
                            gconstpointer data,
                            guint len,
                            gpointer user_data)
{
  InfXmppConnection* xmpp;
  InfXmppConnectionPrivate* priv;
  InfXmppConnectionMessage* message;
  gboolean have_sent;

  xmpp = INF_XMPP_CONNECTION(user_data);
  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  g_assert(priv->position >= len);
  g_object_ref(G_OBJECT(xmpp));

  priv->position -= len;
  if(priv->messages != NULL)
  {
    have_sent = priv->messages->sent;

    /* Flag all messages that have been sent by this call */
    for(message = priv->messages; message != NULL; message = message->next)
    {
      if(!message->sent)
      {
        if(message->position <= len)
          message->sent = TRUE;
        else
          message->position -= len;
      }
    }

    /* Note that a complete execution of this function doesn't keep messages
     * with sent flag set to TRUE in the queue. So if the sent flag was FALSE,
     * the method has been called recursively by a sent callback. In that
     * case, don't do anything here but let the parent call do all other sent
     * callbacks. */
    if(have_sent == FALSE)
    {
      /* Now call sent func on all flagged messages */
      while(priv->messages != NULL && priv->messages->sent)
      {
        if(priv->messages->sent_func != NULL)
          priv->messages->sent_func(xmpp, priv->messages->user_data);

        /* Note that the sent func might have called _clear() in which case all
         * messages have already been removed. */
        if(priv->messages != NULL)
          inf_xmpp_connection_pop_message(xmpp);
      }
    }
  }

  g_object_unref(G_OBJECT(xmpp));
}

static void
inf_xmpp_connection_received_cb(InfTcpConnection* tcp,
                                gconstpointer data,
                                guint len,
                                gpointer user_data)
{
  InfXmppConnection* xmpp;
  InfXmppConnectionPrivate* priv;
  gchar buffer[2048];
  ssize_t res;
  GError* error;
  gboolean receiving;

  xmpp = INF_XMPP_CONNECTION(user_data);
  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  /* We just keep the connection open to send a final gnutls bye and
   * </stream:stream> in this state, any input gets discarded. */
  if(priv->status == INF_XMPP_CONNECTION_CLOSING_GNUTLS)
    return;

  g_object_ref(xmpp);

  g_assert(priv->parsing == 0);
  g_assert(priv->parser != NULL);

  /* Let callbacks know that we start XML parsing. In case of deinitialization
   * this tells them to keep the XML parser alive. We clean up after parsing
   * in that case. */
  ++priv->parsing;

  /* If we have a GnuTLS session, prepare data to be read by
   * gnutls_record_recv(). */
  if(priv->session != NULL)
  {
    g_assert(priv->pull_len == 0);
    priv->pull_data = data;
    priv->pull_len = len;
  }

  if(priv->status == INF_XMPP_CONNECTION_HANDSHAKING)
  {
    g_assert(priv->session != NULL);
    inf_xmpp_connection_tls_handshake(xmpp);
  }

  /* Note that this is not an else branch, since if the XMPP handshake
   * finishes, the status will change and then we process initial data
   * here, if any. */
  if(priv->status != INF_XMPP_CONNECTION_HANDSHAKING &&
     priv->status != INF_XMPP_CONNECTION_CLOSING_GNUTLS)
  {
    if(priv->session != NULL)
    {
      receiving = TRUE;
      while(receiving && (priv->pull_len > 0 ||
                          gnutls_record_check_pending(priv->session) > 0))
      {
        res = gnutls_record_recv(priv->session, buffer, 2048);
        if(res < 0)
        {
          /* Just try again if we were interrupted */
          if(res != GNUTLS_E_INTERRUPTED && res != GNUTLS_E_AGAIN)
          {
            /* A TLS error occured. */
            error = NULL;
            inf_gnutls_set_error(&error, res);
            inf_xml_connection_error(INF_XML_CONNECTION(xmpp), error);
            g_error_free(error);

            /* We cannot assume that GnuTLS is working enough to send a
             * final </stream:stream> or something, so just close the
             * underlaying TCP connection. */
            inf_tcp_connection_close(priv->tcp);
            receiving = FALSE;
          }
        }
        else if(res == 0)
        {
          /* Remote site sent gnutls_bye. This involves session closure. */
          inf_tcp_connection_close(priv->tcp);
          receiving = FALSE;
        }
        else
        {
          /* Feed decoded data into XML parser */
          if(INF_XMPP_CONNECTION_PRINT_TRAFFIC)
            printf("\033[00;32m%.*s\033[00;00m\n", (int)res, buffer);
          xmlParseChunk(priv->parser, buffer, res, 0);

          /* If the callback changed made us disconnect then don't try
           * to read more data. */
          if(priv->status == INF_XMPP_CONNECTION_CLOSING_GNUTLS ||
             priv->status == INF_XMPP_CONNECTION_CLOSED)
          {
            receiving = FALSE;
          }
        }
      }
    }
    else
    {
      /* Feed input directly into XML parser */
      if(INF_XMPP_CONNECTION_PRINT_TRAFFIC)
        printf("\033[00;31m%.*s\033[00;00m\n", (int)len, (const char*)data);
      xmlParseChunk(priv->parser, data, len, 0);
    }
  }

  g_assert(priv->parsing > 0);
  if(--priv->parsing == 0)
  {
    if(priv->status == INF_XMPP_CONNECTION_CLOSING_GNUTLS ||
       priv->status == INF_XMPP_CONNECTION_CLOSED)
    {
      /* Status changed to CLOSING_GNUTLS, this means that someone called
       * _terminate(). Clean up any resources in use (XML parser, GnuTLS
       * session etc. */
      inf_xmpp_connection_clear(xmpp);

      if(priv->status != INF_XMPP_CONNECTION_CLOSED)
      {
        /* Close the TCP connection after remaining stuff has been sent out
         * in case it is not closed already. */
        inf_xmpp_connection_push_message(
          xmpp,
          inf_xmpp_connection_received_cb_sent_func,
          NULL,
          NULL
        );
      }

      g_object_notify(G_OBJECT(xmpp), "status");
    }
    else if(priv->status == INF_XMPP_CONNECTION_AUTH_CONNECTED)
    {
      /* Reinitiate connection after successful authentication */
      /* TODO: Only do this if status at the beginning of this call was
       * AUTHENTICATING */
      inf_xmpp_connection_initiate(xmpp);
    }
  }

  g_object_unref(xmpp);
}

static void
inf_xmpp_connection_error_cb(InfTcpConnection* tcp,
                             GError* error,
                             gpointer user_data)
{
  /* Do not modify status because we get a status change notify from the
   * TCP connection little later anyway. */
  inf_xml_connection_error(INF_XML_CONNECTION(user_data), error);
}

static void
inf_xmpp_connection_notify_status_cb(InfTcpConnection* tcp,
                                     GParamSpec* pspec,
                                     gpointer user_data)
{
  InfXmppConnection* xmpp;
  InfXmppConnectionPrivate* priv;
  InfTcpConnectionStatus tcp_status;

  xmpp = INF_XMPP_CONNECTION(user_data);
  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  g_object_get(G_OBJECT(tcp), "status", &tcp_status, NULL);

  switch(tcp_status)
  {
  case INF_TCP_CONNECTION_CLOSED:
    if(priv->status != INF_XMPP_CONNECTION_CLOSED)
    {
      /* If we are currently parsing XML (because this was called from a
       * signal handler) then we can't delete the XML parser here (otherwise
       * libxml2 crashes, understandably). Instead, just set the status to
       * closed and clean up after XML parsing in _received_cb(). */
      if(priv->parsing == 0)
        inf_xmpp_connection_clear(xmpp);

      priv->status = INF_XMPP_CONNECTION_CLOSED;
      priv->position = 0;

      if(priv->parsing == 0)
        g_object_notify(G_OBJECT(xmpp), "status");
    }
    else
    {
      g_assert(priv->session == NULL);
      g_assert(priv->messages == NULL);
      g_assert(priv->parser == NULL);
      g_assert(priv->doc == NULL);
      g_assert(priv->position == 0);
      g_assert(priv->sasl_session == NULL);
    }

    break;
  case INF_TCP_CONNECTION_CONNECTING:
    g_assert(priv->status == INF_XMPP_CONNECTION_CLOSED);
    if(priv->status != INF_XMPP_CONNECTION_CONNECTING)
    {
      priv->status = INF_XMPP_CONNECTION_CONNECTING;
      g_object_notify(G_OBJECT(xmpp), "status");
    }

    break;
  case INF_TCP_CONNECTION_CONNECTED:
    /* Clear previous certificates before opening a new connection */
    if(priv->own_cert != NULL)
    {
      gnutls_x509_crt_deinit(priv->own_cert);
      priv->own_cert = NULL;

      g_object_notify(G_OBJECT(xmpp), "local-certificate");
    }

    if(priv->peer_cert != NULL)
    {
      inf_certificate_chain_unref(priv->peer_cert);
      priv->peer_cert = NULL;

      g_object_notify(G_OBJECT(xmpp), "remote-certificate");
    }

    g_assert(priv->status == INF_XMPP_CONNECTION_CONNECTING);
    /* No notify required, because it does not change the xml status */
    priv->status = INF_XMPP_CONNECTION_CONNECTED;
    inf_xmpp_connection_initiate(xmpp);
    break;
  default:
    g_assert_not_reached();
    break;
  }
}

/*
 * Utility functions.
 */

static void
inf_xmpp_connection_set_tcp(InfXmppConnection* xmpp,
                            InfTcpConnection* tcp)
{
  InfXmppConnectionPrivate* priv;
  InfTcpConnectionStatus tcp_status;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  g_object_freeze_notify(G_OBJECT(xmpp));

  if(priv->tcp != NULL)
  {
    g_object_get(G_OBJECT(priv->tcp), "status", &tcp_status, NULL);

    /* This will cause a status notify which will actually delete
     * GnuTLS session (if any) and the message queue. */
    if(tcp_status != INF_TCP_CONNECTION_CLOSED)
      inf_tcp_connection_close(priv->tcp);

    /* Make sure there is no SASL session running anymore... it's not a big
     * deal if there was but it should be aborted by the above call to
     * inf_tcp_connection_close(). */
    g_assert(priv->sasl_session == NULL);

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->tcp),
      G_CALLBACK(inf_xmpp_connection_sent_cb),
      xmpp
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->tcp),
      G_CALLBACK(inf_xmpp_connection_received_cb),
      xmpp
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->tcp),
      G_CALLBACK(inf_xmpp_connection_error_cb),
      xmpp
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->tcp),
      G_CALLBACK(inf_xmpp_connection_notify_status_cb),
      xmpp
    );

    g_object_unref(G_OBJECT(priv->tcp));
  }

  priv->tcp = tcp;

  if(tcp != NULL)
  {
    g_object_ref(G_OBJECT(tcp));

    g_signal_connect(
      G_OBJECT(tcp),
      "sent",
      G_CALLBACK(inf_xmpp_connection_sent_cb),
      xmpp
    );

    g_signal_connect(
      G_OBJECT(tcp),
      "received",
      G_CALLBACK(inf_xmpp_connection_received_cb),
      xmpp
    );

    g_signal_connect(
      G_OBJECT(tcp),
      "error",
      G_CALLBACK(inf_xmpp_connection_error_cb),
      xmpp
    );

    g_signal_connect(
      G_OBJECT(tcp),
      "notify::status",
      G_CALLBACK(inf_xmpp_connection_notify_status_cb),
      xmpp
    );

    g_object_get(G_OBJECT(tcp), "status", &tcp_status, NULL);

    switch(tcp_status)
    {
    case INF_TCP_CONNECTION_CLOSED:
      g_assert(priv->status == INF_XMPP_CONNECTION_CLOSED);
      break;
    case INF_TCP_CONNECTION_CONNECTING:
      priv->status = INF_XMPP_CONNECTION_CONNECTING;
      g_object_notify(G_OBJECT(xmpp), "status");
      break;
    case INF_TCP_CONNECTION_CONNECTED:
      /* Do not call initiate, this will be done in constructor little
       * time later. */
      priv->status = INF_XMPP_CONNECTION_CONNECTED;
      g_object_notify(G_OBJECT(xmpp), "status");
      break;
    default:
      g_assert_not_reached();
      break;
    }
  }

  g_object_thaw_notify(G_OBJECT(xmpp));
}

static InfXmlConnectionStatus
inf_xmpp_connection_get_xml_status(InfXmppConnection* xmpp)
{
  InfXmppConnectionPrivate* priv;
  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  switch(priv->status)
  {
  case INF_XMPP_CONNECTION_CONNECTING:
  case INF_XMPP_CONNECTION_CONNECTED:
  case INF_XMPP_CONNECTION_AUTH_CONNECTED:
  case INF_XMPP_CONNECTION_INITIATED:
  case INF_XMPP_CONNECTION_AUTH_INITIATED:
  case INF_XMPP_CONNECTION_AWAITING_FEATURES:
  case INF_XMPP_CONNECTION_AUTH_AWAITING_FEATURES:
  case INF_XMPP_CONNECTION_ENCRYPTION_REQUESTED:
  case INF_XMPP_CONNECTION_HANDSHAKING:
  case INF_XMPP_CONNECTION_AUTHENTICATING:
    return INF_XML_CONNECTION_OPENING;
  case INF_XMPP_CONNECTION_READY:
    return INF_XML_CONNECTION_OPEN;
  case INF_XMPP_CONNECTION_CLOSING_STREAM:
  case INF_XMPP_CONNECTION_CLOSING_GNUTLS:
    return INF_XML_CONNECTION_CLOSING;
  case INF_XMPP_CONNECTION_CLOSED:
    return INF_XML_CONNECTION_CLOSED;
  default:
    g_assert_not_reached();
    return INF_XML_CONNECTION_CLOSED;
  }
}

static gchar*
inf_xmpp_connection_get_address_id(InfIpAddress* addr,
                                   guint port)
{
  gchar* addr_str;
  gchar* addr_id;

  addr_str = inf_ip_address_to_string(addr);
  switch(inf_ip_address_get_family(addr))
  {
  case INF_IP_ADDRESS_IPV4:
    addr_id = g_strdup_printf("%s:%u", addr_str, port);
    break;
  case INF_IP_ADDRESS_IPV6:
    addr_id = g_strdup_printf("[%s]:%u", addr_str, port);
    break;
  default:
    g_assert_not_reached();
    break;
  }

  g_free(addr_str);
  return addr_id;
}

/*
 * GObject overrides
 */

static void
inf_xmpp_connection_init(GTypeInstance* instance,
                         gpointer g_class)
{
  InfXmppConnection* io;
  InfXmppConnectionPrivate* priv;

  io = INF_XMPP_CONNECTION(instance);
  priv = INF_XMPP_CONNECTION_PRIVATE(io);

  priv->tcp = NULL;
  priv->site = INF_XMPP_CONNECTION_CLIENT;
  priv->status = INF_XMPP_CONNECTION_CLOSED;
  priv->local_hostname = NULL;
  priv->remote_hostname = NULL;
  priv->security_policy = INF_XMPP_CONNECTION_SECURITY_BOTH_PREFER_TLS;

  priv->certificate_request = GNUTLS_CERT_IGNORE;
  priv->certificate_callback = NULL;
  priv->certificate_callback_user_data = NULL;

  priv->position = 0;
  priv->messages = NULL;
  priv->last_message = NULL;

  priv->parsing = 0;
  priv->parser = NULL;
  priv->root = NULL;
  priv->cur = NULL;

  priv->doc = NULL;
  priv->buf = NULL;

  priv->session = NULL;
  priv->creds = NULL;
  priv->own_cert = NULL;
  priv->peer_cert = NULL;
  priv->pull_data = NULL;
  priv->pull_len = 0;

  priv->sasl_context = NULL;
  priv->sasl_own_context = NULL;
  priv->sasl_session = NULL;
  priv->sasl_local_mechanisms = NULL;
  priv->sasl_remote_mechanisms = NULL;
  priv->sasl_error = NULL;
}

static GObject*
inf_xmpp_connection_constructor(GType type,
                                guint n_construct_properties,
                                GObjectConstructParam* construct_properties)
{
  InfXmppConnectionPrivate* priv;
  InfTcpConnectionStatus status;
  GObject* obj;

  obj = G_OBJECT_CLASS(parent_class)->constructor(
    type,
    n_construct_properties,
    construct_properties
  );

  priv = INF_XMPP_CONNECTION_PRIVATE(obj);

  g_assert(priv->tcp != NULL);

  if(priv->local_hostname == NULL)
    priv->local_hostname = g_strdup(g_get_host_name());

  g_object_get(G_OBJECT(priv->tcp), "status", &status, NULL);

  /* Initiate stream if connection is already established */
  if(status == INF_TCP_CONNECTION_CONNECTED)
    inf_xmpp_connection_initiate(INF_XMPP_CONNECTION(obj));

  /* If we are an the server and allow TLS, then we do need credentials for
   * this. We can't create them ourselves, because it requires
   * a certificate. */
  g_assert(
    priv->security_policy == INF_XMPP_CONNECTION_SECURITY_ONLY_UNSECURED ||
    priv->site == INF_XMPP_CONNECTION_CLIENT ||
    priv->creds != NULL
  );

  return obj;
}

static void
inf_xmpp_connection_dispose(GObject* object)
{
  InfXmppConnection* xmpp;
  InfXmppConnectionPrivate* priv;

  xmpp = INF_XMPP_CONNECTION(object);
  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  inf_xmpp_connection_set_tcp(xmpp, NULL);

  g_assert(priv->session == NULL);
  g_assert(priv->sasl_session == NULL);

  if(priv->own_cert != NULL)
  {
    gnutls_x509_crt_deinit(priv->own_cert);
    priv->own_cert = NULL;

    g_object_notify(G_OBJECT(xmpp), "local-certificate");
  }

  if(priv->peer_cert != NULL)
  {
    inf_certificate_chain_unref(priv->peer_cert);
    priv->peer_cert = NULL;

    g_object_notify(G_OBJECT(xmpp), "remote-certificate");
  }

  if(priv->sasl_own_context != NULL)
  {
    inf_sasl_context_unref(priv->sasl_own_context);
    priv->sasl_own_context = NULL;
  }

  if(priv->sasl_context != NULL)
  {
    inf_sasl_context_unref(priv->sasl_context);
    priv->sasl_context = NULL;
  }

  if(priv->creds != NULL)
  {
    inf_certificate_credentials_unref(priv->creds);
    priv->creds = NULL;
  }

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_xmpp_connection_finalize(GObject* object)
{
  InfXmppConnection* xmpp;
  InfXmppConnectionPrivate* priv;

  xmpp = INF_XMPP_CONNECTION(object);
  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  g_free(priv->local_hostname);
  g_free(priv->remote_hostname);
  g_free(priv->sasl_local_mechanisms);
  g_free(priv->sasl_remote_mechanisms);

  if(priv->sasl_error)
    g_error_free(priv->sasl_error);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_xmpp_connection_set_property(GObject* object,
                                 guint prop_id,
                                 const GValue* value,
                                 GParamSpec* pspec)
{
  InfXmppConnection* xmpp;
  InfXmppConnectionPrivate* priv;

  xmpp = INF_XMPP_CONNECTION(object);
  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  switch(prop_id)
  {
  case PROP_TCP:
    inf_xmpp_connection_set_tcp(
      xmpp,
      INF_TCP_CONNECTION(g_value_get_object(value))
    );

    break;
  case PROP_SITE:
    /* Site can only been changed if the initial <stream:stream> has not
     * yet been sent. */
    g_assert(priv->status == INF_XMPP_CONNECTION_CONNECTING ||
             priv->status == INF_XMPP_CONNECTION_CONNECTED ||
             priv->status == INF_XMPP_CONNECTION_CLOSED);

    priv->site = g_value_get_enum(value);
    break;
  case PROP_LOCAL_HOSTNAME:
    /* Can only change this if the initial <stream:stream> has not
     * yet been sent. */
    g_assert(priv->status == INF_XMPP_CONNECTION_CONNECTING ||
             priv->status == INF_XMPP_CONNECTION_CONNECTED ||
             priv->status == INF_XMPP_CONNECTION_CLOSED);

    g_free(priv->local_hostname);
    priv->local_hostname = g_value_dup_string(value);
    if(priv->local_hostname == NULL)
      priv->local_hostname = g_strdup(g_get_host_name());
    break;
  case PROP_REMOTE_HOSTNAME:
    /* Can only change this if the initial <stream:stream> has not
     * yet been sent. */
    g_assert(priv->status == INF_XMPP_CONNECTION_CONNECTING ||
             priv->status == INF_XMPP_CONNECTION_CONNECTED ||
             priv->status == INF_XMPP_CONNECTION_CLOSED);

    g_free(priv->remote_hostname);
    priv->remote_hostname = g_value_dup_string(value);
    break;
  case PROP_SECURITY_POLICY:
    priv->security_policy = g_value_get_enum(value);
    break;
  case PROP_CREDENTIALS:
    /* Cannot change credentials when currently in use */
    g_assert(priv->session == NULL);

    if(priv->creds != NULL) inf_certificate_credentials_unref(priv->creds);
    priv->creds = g_value_dup_boxed(value);

    break;
  case PROP_SASL_CONTEXT:
    /* Cannot change context when currently in use */
    /* Use inf_xmpp_connection_reset_sasl_authentication()
     * to change it any time. */
    g_assert(priv->sasl_session == NULL);

    if(priv->sasl_own_context != NULL)
    {
      inf_sasl_context_unref(priv->sasl_own_context);
      priv->sasl_own_context = NULL;
    }

    if(priv->sasl_context != NULL)
      inf_sasl_context_unref(priv->sasl_context);
    priv->sasl_context = g_value_dup_boxed(value);
    break;
  case PROP_SASL_MECHANISMS:
    /* Cannot change context when currently in use */
    /* Use inf_xmpp_connection_reset_sasl_authentication()
     * to change it any time. */
    g_assert(priv->sasl_session == NULL);

    g_free(priv->sasl_local_mechanisms);
    priv->sasl_local_mechanisms = g_value_dup_string(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_xmpp_connection_get_property(GObject* object,
                                 guint prop_id,
                                 GValue* value,
                                 GParamSpec* pspec)
{
  InfXmppConnection* xmpp;
  InfXmppConnectionPrivate* priv;
  InfIpAddress* addr;
  guint port;
  gchar* id;

  xmpp = INF_XMPP_CONNECTION(object);
  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  switch(prop_id)
  {
  case PROP_TCP:
    g_value_set_object(value, G_OBJECT(priv->tcp));
    break;
  case PROP_SITE:
    g_value_set_enum(value, priv->site);
    break;
  case PROP_LOCAL_HOSTNAME:
    g_value_set_string(value, priv->local_hostname);
    break;
  case PROP_REMOTE_HOSTNAME:
    g_value_set_string(value, priv->remote_hostname);
    break;
  case PROP_SECURITY_POLICY:
    g_value_set_enum(value, priv->security_policy);
    break;
  case PROP_TLS_ENABLED:
    g_value_set_boolean(value, inf_xmpp_connection_get_tls_enabled(xmpp));
    break;
  case PROP_CREDENTIALS:
    g_value_set_boxed(value, priv->creds);
    break;
  case PROP_SASL_CONTEXT:
    g_value_set_boxed(value, priv->sasl_context);
    break;
  case PROP_SASL_MECHANISMS:
    g_value_set_string(value, priv->sasl_local_mechanisms);
    break;
  case PROP_STATUS:
    g_value_set_enum(value, inf_xmpp_connection_get_xml_status(xmpp));
    break;
  case PROP_NETWORK:
    g_value_set_static_string(value, "tcp/ip");
    break;
  case PROP_LOCAL_ID:
    /* TODO: Perhaps we could also use JIDs here, but we have to make sure
     * then that they are unique within the whole network, which is
     * not so easy, and address/port serves the purpose equally well. */
    g_object_get(
      G_OBJECT(priv->tcp),
      "local-address", &addr,
      "local-port", &port,
      NULL
    );

    id = inf_xmpp_connection_get_address_id(addr, port);
    inf_ip_address_free(addr);

    g_value_take_string(value, id);
    break;
  case PROP_REMOTE_ID:
    addr = inf_tcp_connection_get_remote_address(priv->tcp);
    port = inf_tcp_connection_get_remote_port(priv->tcp);
    id = inf_xmpp_connection_get_address_id(addr, port);
    g_value_take_string(value, id);
    break;
  case PROP_LOCAL_CERTIFICATE:
    g_value_set_pointer(value, priv->own_cert);
    break;
  case PROP_REMOTE_CERTIFICATE:
    g_value_set_boxed(value, priv->peer_cert);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

/*
 * InfXmlConnection interface implementation
 */

static void
inf_xmpp_connection_xml_connection_send_sent(InfXmppConnection* xmpp,
                                             gpointer xml)
{
  inf_xml_connection_sent(INF_XML_CONNECTION(xmpp), (xmlNodePtr)xml);
}

static void
inf_xmpp_connection_xml_connection_send_free(InfXmppConnection* xmpp,
                                             gpointer xml)
{
  xmlFreeNode((xmlNodePtr)xml);
}

static gboolean
inf_xmpp_connection_xml_connection_open(InfXmlConnection* connection,
                                        GError** error)
{
  InfXmppConnectionPrivate* priv;
  InfTcpConnectionStatus status;

  priv = INF_XMPP_CONNECTION_PRIVATE(connection);

  g_assert(priv->status == INF_XMPP_CONNECTION_CLOSED);
  g_assert(priv->tcp != NULL);

  /* TODO: If we are in CLOSING state, we could go to a state such as
   * INF_XMPP_CONNECTION_CLOSING_RECONNECT which reconnects after the
   * closing has finished. */

  g_object_get(G_OBJECT(priv->tcp), "status", &status, NULL);
  g_assert(status == INF_TCP_CONNECTION_CLOSED);

  return inf_tcp_connection_open(priv->tcp, error);
}

static void
inf_xmpp_connection_xml_connection_close(InfXmlConnection* connection)
{
  InfXmppConnectionPrivate* priv;
  priv = INF_XMPP_CONNECTION_PRIVATE(connection);

  /* Connection is already being closed */
  g_assert(priv->status != INF_XMPP_CONNECTION_CLOSING_STREAM &&
           priv->status != INF_XMPP_CONNECTION_CLOSING_GNUTLS &&
           priv->status != INF_XMPP_CONNECTION_CLOSED);

  switch(priv->status)
  {
  case INF_XMPP_CONNECTION_CONNECTING:
    inf_tcp_connection_close(priv->tcp);
    break;
  case INF_XMPP_CONNECTION_CONNECTED:
  case INF_XMPP_CONNECTION_AUTH_CONNECTED:
    g_assert(priv->parsing == 0);
    inf_xmpp_connection_terminate(INF_XMPP_CONNECTION(connection));
    /* TODO: Shouldn't we close the TCP connection here, as in
     * inf_xmpp_connection_received_cb()? */
    break;
  case INF_XMPP_CONNECTION_HANDSHAKING:
  case INF_XMPP_CONNECTION_ENCRYPTION_REQUESTED:
    /* TODO: Perhaps we should wait for the TLS handshake being finished
     * and then close the connection regularly. */
    /* I don't think we can do more here to make the closure more
     * explicit */
    g_assert(priv->session != NULL);
    gnutls_deinit(priv->session);
    priv->session = NULL;
    /* This will cause a status property notify which will actually set
     * the xmpp status */
    inf_tcp_connection_close(priv->tcp);
    break;
  case INF_XMPP_CONNECTION_AUTHENTICATING:
    /* TODO: I think we should send an <abort/> request here and then
     * wait on either successful or unsuccessful authentication result,
     * and then close the connection normally. Actually, this is what
     * inf_xmpp_connection_deinitiate is supposed to do. */
    g_assert(priv->parsing == 0);
    inf_xmpp_connection_terminate(INF_XMPP_CONNECTION(connection));
    /* TODO: Shouldn't we close the TCP connection here, as in
     * inf_xmpp_connection_received_cb()? */
    break;
  case INF_XMPP_CONNECTION_INITIATED:
  case INF_XMPP_CONNECTION_AUTH_INITIATED:
  case INF_XMPP_CONNECTION_AWAITING_FEATURES:
  case INF_XMPP_CONNECTION_AUTH_AWAITING_FEATURES:
  case INF_XMPP_CONNECTION_READY:
    inf_xmpp_connection_deinitiate(INF_XMPP_CONNECTION(connection));
    break;
  default:
    g_assert_not_reached();
    break;
  }
}

static void
inf_xmpp_connection_xml_connection_send(InfXmlConnection* connection,
                                        xmlNodePtr xml)
{
  InfXmppConnectionPrivate* priv;
  priv = INF_XMPP_CONNECTION_PRIVATE(connection);

  g_assert(priv->status == INF_XMPP_CONNECTION_READY);

  inf_xmpp_connection_send_xml(INF_XMPP_CONNECTION(connection), xml);

  /* It can happen that while calling inf_xmpp_connection_send_xml we
   * notice that the connection is down. Only proceed with sent notification
   * if the connection is still up and we could actually send the thing. */
  if(priv->status == INF_XMPP_CONNECTION_READY)
  {
    inf_xmpp_connection_push_message(
      INF_XMPP_CONNECTION(connection),
      inf_xmpp_connection_xml_connection_send_sent,
      inf_xmpp_connection_xml_connection_send_free,
      xml
    );
  }
  else
  {
    xmlFreeNode(xml);
  }
}

/*
 * GObject type registration
 */

static void
inf_xmpp_connection_class_init(gpointer g_class,
                               gpointer class_data)
{
  GObjectClass* object_class;
  InfXmppConnectionClass* xmpp_class;

  object_class = G_OBJECT_CLASS(g_class);
  xmpp_class = INF_XMPP_CONNECTION_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfXmppConnectionPrivate));

  object_class->constructor = inf_xmpp_connection_constructor;
  object_class->dispose = inf_xmpp_connection_dispose;
  object_class->finalize = inf_xmpp_connection_finalize;
  object_class->set_property = inf_xmpp_connection_set_property;
  object_class->get_property = inf_xmpp_connection_get_property;

  inf_xmpp_connection_error_quark = g_quark_from_static_string(
    "INF_XMPP_CONNECTION_ERROR"
  );

  inf_xmpp_connection_stream_error_quark = g_quark_from_static_string(
    "INF_XMPP_CONNECTION_STREAM_ERROR"
  );

  inf_xmpp_connection_auth_error_quark = g_quark_from_static_string(
    "INF_XMPP_CONNECTION_AUTH_ERROR"
  );

  g_object_class_install_property(
    object_class,
    PROP_TCP,
    g_param_spec_object(
      "tcp-connection",
      "TCP connection",
      "Underlaying TCP connection",
      INF_TYPE_TCP_CONNECTION,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_SITE,
    g_param_spec_enum(
      "site",
      "Site",
      "Site of the connection (client or server)",
      INF_TYPE_XMPP_CONNECTION_SITE,
      INF_XMPP_CONNECTION_CLIENT,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_LOCAL_HOSTNAME,
    g_param_spec_string(
      "local-hostname",
      "Local hostname",
      "The hostname of the local host",
      NULL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_REMOTE_HOSTNAME,
    g_param_spec_string(
      "remote-hostname",
      "Remote hostname",
      "The hostname of the remote host",
      NULL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_SECURITY_POLICY,
    g_param_spec_enum(
      "security-policy",
      "Security policy",
      "How to choose whether to use (or offer, as a server) TLS",
      INF_TYPE_XMPP_CONNECTION_SECURITY_POLICY,
      INF_XMPP_CONNECTION_SECURITY_BOTH_PREFER_TLS,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_TLS_ENABLED,
    g_param_spec_boolean(
      "tls-enabled",
      "TLS enabled",
      "Whether TLS is enabled for the connection or not",
      FALSE,
      G_PARAM_READABLE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_CREDENTIALS,
    g_param_spec_boxed(
      "credentials",
      "Credentials",
      "The certificate credentials for GnuTLS",
      INF_TYPE_CERTIFICATE_CREDENTIALS,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_SASL_CONTEXT,
    g_param_spec_boxed(
      "sasl-context",
      "SASL context",
      "The SASL context used for authentication",
      INF_TYPE_SASL_CONTEXT,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_SASL_MECHANISMS,
    g_param_spec_string(
      "sasl-mechanisms",
      "SASL Mechanisms",
      "Whitespace separated list of SASL mechanisms to accept/offer",
      NULL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT
    )
  );

  g_object_class_override_property(object_class, PROP_STATUS, "status");
  g_object_class_override_property(object_class, PROP_NETWORK, "network");
  g_object_class_override_property(object_class, PROP_LOCAL_ID, "local-id");
  g_object_class_override_property(object_class, PROP_REMOTE_ID, "remote-id");

  g_object_class_override_property(
    object_class,
    PROP_LOCAL_CERTIFICATE,
    "local-certificate"
  );

  g_object_class_override_property(
    object_class,
    PROP_REMOTE_CERTIFICATE,
    "remote-certificate"
  );
}

static void
inf_xmpp_connection_xml_connection_init(gpointer g_iface,
                                        gpointer iface_data)
{
  InfXmlConnectionIface* iface;
  iface = (InfXmlConnectionIface*)g_iface;

  iface->open = inf_xmpp_connection_xml_connection_open;
  iface->close = inf_xmpp_connection_xml_connection_close;
  iface->send = inf_xmpp_connection_xml_connection_send;
}

GType
inf_xmpp_connection_site_get_type(void)
{
  static GType xmpp_connection_site_type = 0;

  if(!xmpp_connection_site_type)
  {
    static const GEnumValue xmpp_connection_site_values[] = {
      {
        INF_XMPP_CONNECTION_CLIENT,
        "INF_XMPP_CONNECTION_CLIENT",
        "client"
      }, {
        INF_XMPP_CONNECTION_SERVER,
        "INF_XMPP_CONNECTION_SERVER",
        "server"
      }, {
        0,
        NULL,
        NULL
      }
    };

    xmpp_connection_site_type = g_enum_register_static(
      "InfXmppConnectionSite",
      xmpp_connection_site_values
    );
  }

  return xmpp_connection_site_type;
}

GType
inf_xmpp_connection_security_policy_get_type(void)
{
  static GType xmpp_connection_security_policy_type = 0;

  if(!xmpp_connection_security_policy_type)
  {
    static const GEnumValue xmpp_connection_security_policy_values[] = {
      {
        INF_XMPP_CONNECTION_SECURITY_ONLY_UNSECURED,
        "INF_XMPP_CONNECTION_SECURITY_ONLY_UNSECURED",
        "only-unsecured"
      }, {
        INF_XMPP_CONNECTION_SECURITY_ONLY_TLS,
        "INF_XMPP_CONNECTION_SECURITY_ONLY_TLS",
        "only-tls"
      }, {
        INF_XMPP_CONNECTION_SECURITY_BOTH_PREFER_UNSECURED,
        "INF_XMPP_CONNECTION_SECURITY_BOTH_PREFER_UNSECURED",
        "both-prefer-unsecured"
      }, {
        INF_XMPP_CONNECTION_SECURITY_BOTH_PREFER_TLS,
        "INF_XMPP_CONNECTION_SECURITY_BOTH_PREFER_TLS",
        "both-prefer-tls"
      }, {
        0,
        NULL,
        NULL
      }
    };

    xmpp_connection_security_policy_type = g_enum_register_static(
      "InfXmppConnectionSecurityPolicy",
      xmpp_connection_security_policy_values
    );
  }

  return xmpp_connection_security_policy_type;
}

GType
inf_xmpp_connection_get_type(void)
{
  static GType xmpp_connection_type = 0;

  if(!xmpp_connection_type)
  {
    static const GTypeInfo xmpp_connection_type_info = {
      sizeof(InfXmppConnectionClass),   /* class_size */
      NULL,                             /* base_init */
      NULL,                             /* base_finalize */
      inf_xmpp_connection_class_init,   /* class_init */
      NULL,                             /* class_finalize */
      NULL,                             /* class_data */
      sizeof(InfXmppConnection),        /* instance_size */
      0,                                /* n_preallocs */
      inf_xmpp_connection_init,         /* instance_init */
      NULL                              /* value_table */
    };

    static const GInterfaceInfo xml_connection_info = {
      inf_xmpp_connection_xml_connection_init,
      NULL,
      NULL
    };

    xmpp_connection_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfXmppConnection",
      &xmpp_connection_type_info,
      0
    );

    g_type_add_interface_static(
      xmpp_connection_type,
      INF_TYPE_XML_CONNECTION,
      &xml_connection_info
    );
  }

  return xmpp_connection_type;
}

/*
 * Public API
 */

/**
 * inf_xmpp_connection_new:
 * @tcp: The underlaying TCP connection to use.
 * @site: Whether this is a XMPP client or server.
 * @local_hostname: The hostname of the local host, or %NULL.
 * @remote_hostname: The hostname of the remote host.
 * @security_policy: Whether to use (or offer, as a server) TLS. See
 * #InfXmppConnectionSecurityPolicy for the meaning of this parameter.
 * @creds: Certificate credentials used to secure the communication.
 * @sasl_context: A SASL context used for authentication.
 * @sasl_mechanisms: A whitespace-separated list of SASL mechanisms to
 * accept/offer, or %NULL.
 *
 * Creates a new #InfXmppConnection with @tcp as communication channel. No
 * attempt is being made to open @tcp, if it is not already open. However,
 * communication is initiated as soon as @tcp gets into
 * %INF_TCP_CONNECTION_CONNECTED state, so you might still open it
 * lateron yourself.
 *
 * @local_hostname specifies the hostname of the local host, and
 * @remote_hostname specifies the hostname of the remote host, as known to
 * the caller. These can be a string representation of the IP address of
 * @tcp, or a DNS name such as "example.com". @local_hostname can be %NULL
 * in which case the host name as reported by g_get_host_name() is used.
 *
 * @creds may be %NULL in which case the connection creates the credentials
 * as soon as they are required. However, this only works if
 * @site is %INF_XMPP_CONNECTION_CLIENT or @security_policy is
 * %INF_XMPP_CONNECTION_SECURITY_ONLY_UNSECURED (or both, of course). For
 * server connections @creds must contain a valid server certificate in case
 * @security_policy is not %INF_XMPP_CONNECTION_SECURITY_ONLY_UNSECURED.
 * @creds can contain a certificate for the client site and, if so, is used
 * for client authentication.
 *
 * If @sasl_context is %NULL, #InfXmppConnection uses a built-in context
 * that only supports ANONYMOUS authentication. In @sasl_context's
 * callback function, the #InfXmppConnection for which the authentication
 * shall be performed can be accessed via the @session_data parameter of
 * #InfSaslContextCallbackFunc.
 *
 * If @sasl_context is not %NULL, then the @sasl_mechanisms parameter defines
 * what SASL mechanisms are used. On the server side, these are the mechanisms
 * offered to the client, and on the client side, these are the accepted
 * mechanisms (meaning that if a server does not offer any of these, the
 * connection will be closed). If @sasl_context is %NULL, then this parameter
 * is ignored. @sasl_mechanisms can be %NULL in which case all available
 * mechanisms are accepted or offered, respectively.
 *
 * Return Value: A new #InfXmppConnection.
 **/
InfXmppConnection*
inf_xmpp_connection_new(InfTcpConnection* tcp,
                        InfXmppConnectionSite site,
                        const gchar* local_hostname,
                        const gchar* remote_hostname,
                        InfXmppConnectionSecurityPolicy security_policy,
                        InfCertificateCredentials* creds,
                        InfSaslContext* sasl_context,
                        const gchar* sasl_mechanisms)
{
  GObject* object;

  g_return_val_if_fail(INF_IS_TCP_CONNECTION(tcp), NULL);

  g_return_val_if_fail(
    security_policy == INF_XMPP_CONNECTION_SECURITY_ONLY_UNSECURED ||
    site == INF_XMPP_CONNECTION_CLIENT || creds != NULL,
    NULL
  );

  object = g_object_new(
    INF_TYPE_XMPP_CONNECTION,
    "tcp-connection", tcp,
    "site", site,
    "local-hostname", local_hostname,
    "remote-hostname", remote_hostname,
    "security-policy", security_policy,
    "credentials", creds,
    "sasl-context", sasl_context,
    "sasl-mechanisms", sasl_mechanisms,
    NULL
  );

  return INF_XMPP_CONNECTION(object);
}

/**
 * inf_xmpp_connection_get_tls_enabled:
 * @xmpp: A #InfXmppConnection.
 *
 * Returns whether TLS encryption is enabled for @xmpp. This returns %TRUE
 * as soon as the TLS handshake is completed but before the server certificate
 * was verified (see inf_xmpp_connection_set_certificate_callback()).
 *
 * Returns: %TRUE if TLS is enabled and %FALSE otherwise.
 */
gboolean
inf_xmpp_connection_get_tls_enabled(InfXmppConnection* xmpp)
{
  InfXmppConnectionPrivate* priv;

  g_return_val_if_fail(INF_IS_XMPP_CONNECTION(xmpp), FALSE);

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  if(priv->status == INF_XMPP_CONNECTION_HANDSHAKING) return FALSE;
  if(priv->session == NULL) return FALSE;

  return TRUE;
}

/**
 * inf_xmpp_connection_get_own_certificate:
 * @xmpp: A #InfXmppConnection.
 *
 * Returns the local host's certificate that was used to authenticate with
 * the remote host, or %NULL if no certificate was used. This function can
 * only be used after the TLS handshake has completed, see
 * inf_xmpp_connection_get_tls_enabled().
 *
 * Returns: The certificate of the local host. The returned value should not
 * be freed, it is owned by the #InfXmppConnection.
 */
gnutls_x509_crt_t
inf_xmpp_connection_get_own_certificate(InfXmppConnection* xmpp)
{
  InfXmppConnectionPrivate* priv;

  g_return_val_if_fail(INF_IS_XMPP_CONNECTION(xmpp), NULL);
  g_return_val_if_fail(inf_xmpp_connection_get_tls_enabled(xmpp), NULL);

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);
  return priv->own_cert;
}

/**
 * inf_xmpp_connection_get_peer_certificate:
 * @xmpp: A #InfXmppConnection.
 *
 * Returns the certificate chain that the remote host authenticated itself
 * with. This function can only be used after the TLS handshake has completed,
 * see inf_xmpp_connection_get_tls_enabled().
 *
 * Returns: The certificate chain of the remote host. The returned value
 * should not be freed, it is owned by the #InfXmppConnection.
 */
InfCertificateChain*
inf_xmpp_connection_get_peer_certificate(InfXmppConnection* xmpp)
{
  InfXmppConnectionPrivate* priv;

  g_return_val_if_fail(INF_IS_XMPP_CONNECTION(xmpp), NULL);
  g_return_val_if_fail(inf_xmpp_connection_get_tls_enabled(xmpp), NULL);

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);
  return priv->peer_cert;
}

/**
 * inf_xmpp_connection_set_certificate_callback:
 * @xmpp: A #InfXmppConnection.
 * @req: Whether to request a client certificate from the peer.
 * @cb: Function to be called to verify the peer's certificate, or %NULL.
 * @user_data: Additional data to pass to the callback function.
 *
 * This function sets a callback that is called when the connection needs to
 * verify the peer's certificate. It does not need to respond immediately,
 * but can, for example, show a dialog to a user and continue when the user
 * finished with it.
 *
 * When the certificate is trusted, then call
 * inf_xmpp_connection_certificate_verify_continue(),
 * otherwise inf_xmpp_connection_certificate_verify_cancel(). This can happen
 * in the callback or some time later. The connection process is stopped until
 * either of these functions is called.
 *
 * Note that the function is also called if the peer did not send a
 * certificate, in which case the certificate chain parameter in the callback
 * will be %NULL.
 *
 * If @cb is %NULL, or this function has not been called before a certificate
 * needs to be verified, then the certificate is always trusted.
 */
void
inf_xmpp_connection_set_certificate_callback(InfXmppConnection* xmpp,
                                             gnutls_certificate_request_t req,
                                             InfXmppConnectionCrtCallback cb,
                                             gpointer user_data)
{
  InfXmppConnectionPrivate* priv;

  g_return_if_fail(INF_IS_XMPP_CONNECTION(xmpp));

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);
  priv->certificate_request = req;
  priv->certificate_callback = cb;
  priv->certificate_callback_user_data = user_data;
}

/**
 * inf_xmpp_connection_certificate_verify_continue:
 * @xmpp: A #InfXmppConnection.
 *
 * Call this function when your callback set in
 * inf_xmpp_connection_set_certificate_callback() was called and you do trust
 * the peer's certificate. The connection process will then continue.
 */
void
inf_xmpp_connection_certificate_verify_continue(InfXmppConnection* xmpp)
{
  InfXmppConnectionPrivate* priv;

  g_return_if_fail(INF_IS_XMPP_CONNECTION(xmpp));

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);
  g_return_if_fail(priv->status == INF_XMPP_CONNECTION_CONNECTED);
  g_return_if_fail(priv->session != NULL);

  inf_xmpp_connection_initiate(xmpp);
}

/**
 * inf_xmpp_connection_certificate_verify_cancel:
 * @xmpp: A #InfXmppConnection.
 * @error: Reason why the certificate is not trusted, or %NULL.
 *
 * Call this function when your callback set in
 * inf_xmpp_connection_set_certificate_callback() was called and you do not
 * trust the peer's certificate. The connection will then be closed with a
 * corresponding error.
 *
 * If @error is non-%NULL, then it should contain a reason why the certificate
 * was not trusted. If you verified the peer's certificate with
 * gnutls_certificate_verify_peers2(), or gnutls_x509_crt_list_verify(), then
 * a corresponding error can be generated with the verification result
 * with inf_gnutls_certificate_verification_set_error(). The reason is then
 * shown to the local user.
 */
void
inf_xmpp_connection_certificate_verify_cancel(InfXmppConnection* xmpp,
                                              const GError* error)
{
  InfXmppConnectionPrivate* priv;
  GError* local_error;

  g_return_if_fail(INF_IS_XMPP_CONNECTION(xmpp));

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);
  g_return_if_fail(priv->status == INF_XMPP_CONNECTION_CONNECTED);
  g_return_if_fail(priv->session != NULL);

  if(priv->site == INF_XMPP_CONNECTION_CLIENT)
  {
    if(error == NULL)
    {
      local_error = g_error_new_literal(
        inf_xmpp_connection_error_quark,
        INF_XMPP_CONNECTION_ERROR_CERTIFICATE_NOT_TRUSTED,
        _("The server certificate is not trusted")
      );
    }
    else
    {
      local_error = g_error_new(
        inf_xmpp_connection_error_quark,
        INF_XMPP_CONNECTION_ERROR_CERTIFICATE_NOT_TRUSTED,
        _("The server certificate is not trusted: %s"),
        error->message
      );
    }
  }
  else
  {
    if(error == NULL)
    {
      local_error = g_error_new_literal(
        inf_xmpp_connection_error_quark,
        INF_XMPP_CONNECTION_ERROR_CERTIFICATE_NOT_TRUSTED,
        _("The client certificate is not trusted")
      );
    }
    else
    {
      local_error = g_error_new(
        inf_xmpp_connection_error_quark,
        INF_XMPP_CONNECTION_ERROR_CERTIFICATE_NOT_TRUSTED,
        _("The client certificate is not trusted: %s"),
        error->message
      );
    }
  }

  inf_xml_connection_error(INF_XML_CONNECTION(xmpp), local_error);
  g_error_free(local_error);

  inf_xmpp_connection_terminate(xmpp);
}

/**
 * inf_xmpp_connection_reset_sasl_authentication:
 * @xmpp: A #InfXmppConnection.
 * @new_context: The new sasl context to set, or %NULL.
 * @new_mechanisms: Allowed SASL mechanisms to use. Ignored if @new_context
 * is %NULL.
 *
 * Sets a new SASL context and mechanisms to use for authentication. This does
 * not have any effect if authentication has already been performed. This can
 * be useful if a server decides to use a stricter authentication policy and
 * gets away with its previous SASL context. If @new_context is %NULL, then a
 * built-in SASL context is used which only accepts anonymous authentication.
 *
 * If the authentication is currently in progress then it is aborted. The
 * server sends an %INF_XMPP_CONNECTION_AUTH_ERROR_TEMPORARY_AUTH_FAILURE
 * error to the client with %INF_AUTHENTICATION_DETAIL_ERROR_TRY_AGAIN detail
 * (see inf_xmpp_connection_get_sasl_error()).
 *
 * On the client side, if authentication is in progress, a request to abort
 * the authentication is sent to the server. The server will then reply with
 * an %INF_XMPP_CONNECTION_AUTH_ERROR_ABORTED error. In the signal handler of
 * the #InfXmlConnection::error signal you should reinitiate the authentication
 * with inf_xmpp_connection_retry_sasl_authentication() or the connection will
 * be closed. It is also possible that the final authentication request has
 * already been sent, and the server replies with successful authentication
 * instead. In that case calling this function will have no effect apart from
 * closing and reopening the connection will use the new context and
 * mechanisms. 
 */
void
inf_xmpp_connection_reset_sasl_authentication(InfXmppConnection* xmpp,
                                              InfSaslContext* new_context,
                                              const gchar* new_mechanisms)
{
  InfXmppConnectionPrivate* priv;
  xmlNodePtr xml;

  g_return_if_fail(INF_IS_XMPP_CONNECTION(xmpp));

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  if(priv->status == INF_XMPP_CONNECTION_AUTHENTICATING)
  {
    switch(priv->site)
    {
    case INF_XMPP_CONNECTION_CLIENT:
      /* Send abort, wait for server reply (finish or failure), ignoring
       * challenges while doing so. Reset sasl_session to NULL to notify that
       * we are done with this session, but keep status while we are waiting
       * for server acknowledgement. The server might also be done with
       * authentication already. */

      if(priv->sasl_session != NULL)
      {
        xml = inf_xmpp_connection_node_new_sasl("abort");
        inf_xmpp_connection_send_xml(xmpp, xml);

        inf_sasl_context_stop_session(priv->sasl_context, priv->sasl_session);
        priv->sasl_session = NULL;
      }

      break;
    case INF_XMPP_CONNECTION_SERVER:
      /* Reset current SASL negotiation. Wait for client reply to current
       * challenge until we tell it to avoid race conditions. */
      if(priv->sasl_session != NULL)
      {
        inf_sasl_context_stop_session(priv->sasl_context, priv->sasl_session);
        priv->sasl_session = NULL;
      }

      break;
    default:
      g_assert_not_reached();
      break;
    }
  }

  if(priv->sasl_own_context != NULL)
  {
    inf_sasl_context_unref(priv->sasl_own_context);
    priv->sasl_own_context = NULL;
  }

  if(priv->sasl_context != NULL)
    inf_sasl_context_unref(priv->sasl_context);

  priv->sasl_context = new_context;
  if(new_context != NULL) inf_sasl_context_ref(new_context);

  g_free(priv->sasl_local_mechanisms);
  priv->sasl_local_mechanisms = g_strdup(new_mechanisms);

  g_object_freeze_notify(G_OBJECT(xmpp)); /* sasl_ensure also notifies */
  if(new_context == NULL)
  {
    if(!inf_xmpp_connection_sasl_ensure(xmpp))
    {
      /* OK, that's quite tough, but it should happen only rarely anyway,
       * and I don't think there is much we can do about it. This happens
       * when gsasl initialization of the built-in context fails. */
      inf_xmpp_connection_deinitiate(xmpp);
    }
  }

  g_object_notify(G_OBJECT(xmpp), "sasl-context");
  g_object_notify(G_OBJECT(xmpp), "sasl-mechanisms");

  g_object_thaw_notify(G_OBJECT(xmpp));
}

/**
 * inf_xmpp_connection_retry_sasl_authentication:
 * @xmpp: A #InfXmppConnection.
 * @error: Location to store error information, if any.
 *
 * When SASL authentication failed then the #InfXmlConnection::error signal
 * is emitted with an error from the INF_XMPP_CONNECTION_AUTH_ERROR domain.
 * If the signal handler wants to retry authentication then it should call
 * this function, possibly modifying the #InfXmppConnection:sasl-mechanisms
 * property before. If this function is not called then the connection will
 * terminate.
 *
 * The function can fail if the server does not support any of the available
 * mechanisms given in #InfXmppConnection:sasl-mechanisms. If so, the function
 * returns %FALSE and @error is set.
 *
 * Returns: %TRUE if auth retry is being performed, %FALSE otherwise.
 */
gboolean
inf_xmpp_connection_retry_sasl_authentication(InfXmppConnection* xmpp,
                                              GError** error)
{
  InfXmppConnectionPrivate* priv;
  const gchar* suggestion;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  g_return_val_if_fail(
    priv->status == INF_XMPP_CONNECTION_AWAITING_FEATURES,
    FALSE
  );

  suggestion = inf_xmpp_connection_sasl_suggest_mechanism(xmpp, error);

  if(suggestion == NULL)
    return FALSE;

  inf_xmpp_connection_sasl_init(xmpp, suggestion);
  return TRUE;
}

/**
 * inf_xmpp_connection_set_sasl_error:
 * @xmpp: A #InfXmppConnection.
 * @error: The SASL error to set.
 *
 * Sets the SASL error of @xmpp. The SASL error is an additional hint of what
 * went wrong during authentication. It should be set on the server side
 * in the gsasl callback checking the user authentication. If on the
 * client side #InfXmlConnection::error is emitted with an error from the 
 * INF_XMPP_CONNECTION_AUTH_ERROR domain then
 * inf_xmpp_connection_get_sasl_error() can be used to obtain more detailed
 * error information.
 */
void
inf_xmpp_connection_set_sasl_error(InfXmppConnection* xmpp,
                                   const GError* error)
{
  InfXmppConnectionPrivate* priv;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  g_return_if_fail(priv->sasl_context != NULL);
  g_return_if_fail(priv->sasl_error == NULL);

  priv->sasl_error = g_error_copy(error);
}

/**
 * inf_xmpp_connection_get_sasl_error:
 * @xmpp: A #InfXmppConnection.
 *
 * Gets the SASL error of @xmpp. See inf_xmpp_connection_set_sasl_error().
 *
 * Returns: A pointer to a #GError object owned by @xmpp.
 */
const GError*
inf_xmpp_connection_get_sasl_error(InfXmppConnection* xmpp)
{
  InfXmppConnectionPrivate* priv;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  g_return_val_if_fail(priv->sasl_context != NULL, NULL);

  return priv->sasl_error;
}

/* vim:set et sw=2 ts=2: */
