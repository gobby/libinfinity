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

#include <libinfinity/common/inf-xmpp-connection.h>
#include <libinfinity/common/inf-xml-connection.h>
#include <libinfinity/common/inf-ip-address.h>
#include <libinfinity/inf-marshal.h>

#include <errno.h>
#include <string.h>

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
   * waiting for the GnuTLS bye to be sent out. */
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

  InfXmppConnectionSentFunc sent_func;
  InfXmppConnectionFreeFunc free_func;
  gpointer user_data;
};

typedef struct _InfXmppConnectionPrivate InfXmppConnectionPrivate;
struct _InfXmppConnectionPrivate {
  InfTcpConnection* tcp;
  InfXmppConnectionSite site;
  gchar* jid;

  InfXmppConnectionStatus status;

  /* The number of chars given to the TCP connection 
   * waiting for being sent. */
  guint position;

  /* Message queue */
  xmlDocPtr doc;
  xmlBufferPtr buf;
  InfXmppConnectionMessage* messages;
  InfXmppConnectionMessage* last_message;

  /* XML parsing */
  xmlParserCtxtPtr parser;
  xmlNodePtr root;
  xmlNodePtr cur;

  /* Transport layer security */
  gnutls_session_t session;
  gnutls_certificate_credentials_t cred;
  gnutls_certificate_credentials_t own_cred;
  const gchar* pull_data;
  gsize pull_len;

  /* SASL */
  Gsasl* sasl_context;
  Gsasl* sasl_own_context;
  Gsasl_session* sasl_session;
};

enum {
  PROP_0,

  PROP_TCP,
  PROP_SITE,
  PROP_JID,

  /* gnutls_certificate_credentials_t */
  PROP_CREDENTIALS,
  /* Gsasl* */
  PROP_SASL_CONTEXT,

  /* From InfXmlConnection */
  PROP_STATUS
};

enum {
  ERROR,

  LAST_SIGNAL
};

#define INF_XMPP_CONNECTION_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TYPE_XMPP_CONNECTION, InfXmppConnectionPrivate))

static GObjectClass* parent_class;
static guint xmpp_connection_signals[LAST_SIGNAL] = { 0 };

static GQuark inf_xmpp_connection_error_quark;
static GQuark inf_xmpp_connection_stream_error_quark;
static GQuark inf_xmpp_connection_auth_error_quark;
static GQuark inf_xmpp_connection_gnutls_error_quark;
static GQuark inf_xmpp_connection_gsasl_error_quark;

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
      "invalid-mechansim",
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
    return "The entity has sent XML that cannot be processed";
  case INF_XMPP_CONNECTION_STREAM_ERROR_BAD_NAMESPACE_PREFIX:
    return "The entity has sent a namespace prefix that is unsupported, or "
           "has sent no namespace prefix on an element that requires such a "
           "prefix";
  case INF_XMPP_CONNECTION_STREAM_ERROR_CONFLICT:
    return "The server is closing the active stream for this entity because "
           "a new stream has been initiated that conflicts with the existing "
           "stream";
  case INF_XMPP_CONNECTION_STREAM_ERROR_CONNECTION_TIMEOUT:
    return "The entity has not generated any traffic over the stream for "
           "some period of time";
  case INF_XMPP_CONNECTION_STREAM_ERROR_HOST_GONE:
    return "The value of the 'to' attribute provided by the initiating "
           "entity in the stream header corresponds to a hostname that is "
           "no longer hosted by the server";
  case INF_XMPP_CONNECTION_STREAM_ERROR_HOST_UNKNOWN:
    return "The value of the 'to' attribute provided by the initiating "
           "entity  in the stream header does not correspond to a hostname "
           "that is hosted by the server";
  case INF_XMPP_CONNECTION_STREAM_ERROR_IMPROPER_ADDRESSING:
    return "A stanza sent between two servers lacks a 'to' or 'from'"
           "attribute";
  case INF_XMPP_CONNECTION_STREAM_ERROR_INTERNAL_SERVER_ERROR:
    return "The server has experienced a misconfiguration or an otherwise-"
           "undefined internal error that prevents it from servicing "
           "the stream";
  case INF_XMPP_CONNECTION_STREAM_ERROR_INVALID_FROM:
    return "The JID or hostname provided in a 'from' address does not match "
           "an authorized JID or validated domain negotiated between servers "
           "via SASL or dialback, or between a client and a server via "
           "authentication and resource binding";
  case INF_XMPP_CONNECTION_STREAM_ERROR_INVALID_ID:
    return "The stream ID or dialback ID is invalid or does not match an ID "
           "previously provided";
  case INF_XMPP_CONNECTION_STREAM_ERROR_INVALID_NAMESPACE:
    return "The streams namespace is something other than "
           "\"http://etherx.jabber.org/streams\" or the dialback namespace "
           "name is something other than \"jabber:server:dialback\"";
  case INF_XMPP_CONNECTION_STREAM_ERROR_INVALID_XML:
    return "The entity has sent invalid XML over the stream to a server "
           "that performs validation";
  case INF_XMPP_CONNECTION_STREAM_ERROR_NOT_AUTHORIZED:
    return "The entity has attempted to send data before the stream has "
           "been authenticated, or otherwise is not authorized to perform "
           "an action related to stream negotiation";
  case INF_XMPP_CONNECTION_STREAM_ERROR_POLICY_VIOLATION:
    return "The entity has violated some local service policy";
  case INF_XMPP_CONNECTION_STREAM_ERROR_REMOTE_CONNECTION_FAILED:
    return "The server is unable to property connect to a remote entity "
           "that is required for authentication or authorization";
  case INF_XMPP_CONNECTION_STREAM_ERROR_RESOURCE_CONSTRAINT:
    return "The server lacks the system resources necessary to service the "
           "stream";
  case INF_XMPP_CONNECTION_STREAM_ERROR_RESTRICTED_XML:
    return "The entity has attempted to send restricted XML features";
  case INF_XMPP_CONNECTION_STREAM_ERROR_SEE_OTHER_HOST:
    return "The server will not provide service to the initiating "
           "entity but is redirecting traffic to another host";
  case INF_XMPP_CONNECTION_STREAM_ERROR_SYSTEM_SHUTDOWN:
    return "The server is being shut down and all active streams are being "
           "closed";
  case INF_XMPP_CONNECTION_STREAM_ERROR_UNDEFINED_CONDITION:
  case INF_XMPP_CONNECTION_STREAM_ERROR_FAILED:
    return "The error condition is not one of those defined by the other "
           "conditions";
  case INF_XMPP_CONNECTION_STREAM_ERROR_UNSUPPORTED_ENCODING:
    return "The initiating entity has encoded the stream in an encoding "
           "that is not supported by the server";
  case INF_XMPP_CONNECTION_STREAM_ERROR_UNSUPPORTED_STANZA_TYPE:
    return "The initiating entity has sent a first-level child of the stream "
           "that is not supported by the server.";
  case INF_XMPP_CONNECTION_STREAM_ERROR_UNSUPPORTED_VERSION:
    return "The value of the 'version' attribute provided by the initiating "
           "entity in the stream header specifies a version of XMPP that is "
           "not supported by the server";
  case INF_XMPP_CONNECTION_STREAM_ERROR_XML_NOT_WELL_FORMED:
    return "The initiating entity has sent XML that is not well-formed";
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
    return "The receiving entity acknowledged an <abort/> element sent by "
           "the initiating entity";
  case INF_XMPP_CONNECTION_AUTH_ERROR_INCORRECT_ENCODING:
    return "The data provided by the initiating entity could not be "
           "processed because the Base64 encoding is incorrect";
  case INF_XMPP_CONNECTION_AUTH_ERROR_INVALID_AUTHZID:
    return "The authzid provided by the initiating entity is invalid, "
           "either because it is incorrectly formatted or because the "
           "initiating entity does not have permissions to authorize that ID";
  case INF_XMPP_CONNECTION_AUTH_ERROR_INVALID_MECHANISM:
    return "The initiating entity did not provide a mechanism or requested "
           "a mechanism that is not supported by the receiving entity";
  case INF_XMPP_CONNECTION_AUTH_ERROR_MECHANISM_TOO_WEAK:
    return "The mechanism requsted by the initiating entity is weaker than "
           "server policy permits for that initiating entity";
  case INF_XMPP_CONNECTION_AUTH_ERROR_NOT_AUTHORIZED:
    return "The authentication failed because the initiating entity did not "
           "provide valid credentials";
  case INF_XMPP_CONNECTION_AUTH_ERROR_TEMPORARY_AUTH_FAILURE:
    return "The authentication failed because of a temporary error condition "
           "within the receiving entity";
  case INF_XMPP_CONNECTION_AUTH_ERROR_FAILED:
    return "An unknown authentication error has occured";
  default:
    g_assert_not_reached();
    return NULL;
  }
}

static const gchar*
inf_xmpp_connection_strerror(InfXmppConnectionError code)
{
  switch(code)
  {
  case INF_XMPP_CONNECTION_ERROR_TLS_UNSUPPORTED:
    return "The server does not support transport layer security";
  case INF_XMPP_CONNECTION_ERROR_TLS_FAILURE:
    return "The server cannot perform the TLS handshake";
  case INF_XMPP_CONNECTION_ERROR_AUTHENTICATION_UNSUPPORTED:
    return "The server does not provide any authentication mechanism";
  case INF_XMPP_CONNECTION_ERROR_NO_SUITABLE_MECHANISM:
    return "The server does not offer a suitable authentication mechanism";
  case INF_XMPP_CONNECTION_ERROR_FAILED:
    return "An unknown XMPP error occured";
  default:
    g_assert_not_reached();
    break;
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

static void
inf_xmpp_connection_send_chars(InfXmppConnection* xmpp,
                               gconstpointer data,
                               guint len)
{
  InfXmppConnectionPrivate* priv;
  guint bytes;
  ssize_t cur_bytes;
  GError* error;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  g_assert(priv->status != INF_XMPP_CONNECTION_HANDSHAKING);
  printf("\033[00;34m%.*s\033[00;00m\n", (int)len, data);

  if(priv->session != NULL)
  {
    bytes = 0;

    do
    {
      cur_bytes = gnutls_record_send(priv->session, data, len);

      if(cur_bytes < 0)
      {
        /* A GnuTLS error occured. It does not make sense to try to send
         * </stream:stream> or a gnutls bye here, since this would again
         * have to go through GnuTLS, which would fail again, and so on. */
        error = NULL;
        g_set_error(
          &error,
          inf_xmpp_connection_gnutls_error_quark,
          cur_bytes,
          "%s",
          gnutls_strerror(cur_bytes)
        );

        g_signal_emit(
          G_OBJECT(xmpp),
          xmpp_connection_signals[ERROR],
          0,
          error
        );

        g_error_free(error);

        inf_tcp_connection_close(priv->tcp);
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
        bytes += cur_bytes;
      }
    } while(bytes < len);
  }
  else
  {
    inf_tcp_connection_send(priv->tcp, data, len);
    priv->position += len;
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

  inf_xmpp_connection_send_chars(
    xmpp,
    xmlBufferContent(priv->buf),
    xmlBufferLength(priv->buf)
  );

  xmlUnlinkNode(xml);
  xmlBufferEmpty(priv->buf);
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

  if(priv->sasl_session != NULL)
  {
    gsasl_finish(priv->sasl_session);
    priv->sasl_session = NULL;
  }

  if(priv->session != NULL)
  {
    gnutls_deinit(priv->session);
    priv->session = NULL;
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
      }

      inf_xmpp_connection_send_chars(
        xmpp,
        xmpp_connection_deinit_request,
        sizeof(xmpp_connection_deinit_request) - 1
      );
    }

    if(priv->session != NULL)
      gnutls_bye(priv->session, GNUTLS_SHUT_WR);
  }

  /* Do not clear resources at this point because we might be in a XML parser
   * or GnuTLS callback, issued via received_cb(). received_cb() calls
   * _clear() if the status changes to CLOSING_GNUTLS. Make sure to call
   * _clear() on your own if you call _terminate() outside of received_cb().
   * The only point were the current code needs to do this is in
   * inf_xmpp_connection_xml_connection_close() */
  /*inf_xmpp_connection_clear(xmpp);*/

  /* The Change from CLOSING_STREAM to CLOSING_GNUTLS does not change
   * the XML status, so we need no notify in this case. */
  if(priv->status != INF_XMPP_CONNECTION_CLOSING_STREAM)
  {
    priv->status = INF_XMPP_CONNECTION_CLOSING_GNUTLS;
    g_object_notify(G_OBJECT(xmpp), "status");
  }
  else
  {
    priv->status = INF_XMPP_CONNECTION_CLOSING_GNUTLS;
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

  child = xmlNewNode(
    NULL,
    (const xmlChar*)inf_xmpp_connection_auth_error_to_condition(code)
  );

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

  g_signal_emit(G_OBJECT(xmpp), xmpp_connection_signals[ERROR], 0, error);
  g_error_free(error);
}

/* This sends a <stream:error> and then terminates the session using
 * the inf_xmpp_connection_terminate. text may be NULL. */
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

  g_signal_emit(G_OBJECT(xmpp), xmpp_connection_signals[ERROR], 0, error);
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

  if(priv->status == INF_XMPP_CONNECTION_AUTHENTICATING)
  {
    /* Abort authentication before sending </stream:stream>. */
    /* TODO: Wait for response of the abort before sending </stream:stream> */
    abort = inf_xmpp_connection_node_new_sasl("abort");
    inf_xmpp_connection_send_xml(xmpp, abort);
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

static ssize_t
inf_xmpp_connection_tls_push(gnutls_transport_ptr_t ptr,
                             const void* data,
                             size_t len)
{
  InfXmppConnection* xmpp;
  InfXmppConnectionPrivate* priv;

  xmpp = INF_XMPP_CONNECTION(ptr);
  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  inf_tcp_connection_send(priv->tcp, data, len);
  priv->position += len;

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

static void
inf_xmpp_connection_tls_handshake(InfXmppConnection* xmpp)
{
  InfXmppConnectionPrivate* priv;
  int ret;
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
    /* Reinitiate stream */
    inf_xmpp_connection_initiate(xmpp);
    break;
  default:
    error = NULL;
    g_set_error(
      &error,
      inf_xmpp_connection_gnutls_error_quark,
      ret,
      "%s",
      gnutls_strerror(ret)
    );

    g_signal_emit(G_OBJECT(xmpp), xmpp_connection_signals[ERROR], 0, error);
    g_error_free(error);

    gnutls_deinit(priv->session);
    priv->session = NULL;

    switch(priv->site)
    {
    case INF_XMPP_CONNECTION_CLIENT:
      /* Wait for terminating </stream:stream> from server */
      priv->status = INF_XMPP_CONNECTION_CLOSING_STREAM;
      g_object_notify(G_OBJECT(xmpp), "status");
      break;
    case INF_XMPP_CONNECTION_SERVER:
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
  gnutls_dh_params_t dh_params;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);
  g_assert(priv->session == NULL);

  /* Make sure credentials are present */
  if(priv->cred == NULL)
  {
    gnutls_certificate_allocate_credentials(&priv->own_cred);
    priv->cred = priv->own_cred;

    if(priv->site == INF_XMPP_CONNECTION_SERVER)
    {
      gnutls_dh_params_init(&dh_params);
      gnutls_dh_params_generate2(dh_params, xmpp_connection_dh_bits);
      gnutls_certificate_set_dh_params(priv->cred, dh_params);
    }

    g_object_notify(G_OBJECT(xmpp), "credentials");
  }

  switch(priv->site)
  {
  case INF_XMPP_CONNECTION_CLIENT:
    gnutls_init(&priv->session, GNUTLS_CLIENT);
    break;
  case INF_XMPP_CONNECTION_SERVER:
    gnutls_init(&priv->session, GNUTLS_SERVER);
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

  gnutls_credentials_set(priv->session, GNUTLS_CRD_CERTIFICATE, priv->cred);
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

/* Emits the error signal for the given SASL error code and sends an
 * authentication failure to the other site. */
static void
inf_xmpp_connection_sasl_error(InfXmppConnection* xmpp,
                               int code)
{
  InfXmppConnectionPrivate* priv;
  InfXmppConnectionAuthError auth_code;
  GError* error;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);
  g_assert(priv->sasl_session != NULL);

  error = NULL;
  g_set_error(
    &error,
    inf_xmpp_connection_gsasl_error_quark,
    code,
    "%s",
    gsasl_strerror(code)
  );

  g_signal_emit(G_OBJECT(xmpp), xmpp_connection_signals[ERROR], 0, error);
  g_error_free(error);

  gsasl_finish(priv->sasl_session);
  priv->sasl_session = NULL;

  if(priv->site == INF_XMPP_CONNECTION_SERVER)
  {
    /* Find matching auth error code to send to client */
    switch(code)
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
  }
  else
  {
    /* Just terminate session on client site when a SASL error occurs */
    /* TODO: Better deinitiate here? */
    inf_xmpp_connection_terminate(xmpp);
  }
}

static int
inf_xmpp_connection_sasl_cb(Gsasl* ctx,
                            Gsasl_session* sctx,
                            Gsasl_property prop)
{
  InfXmppConnection* xmpp;
  InfXmppConnectionPrivate* priv;

  xmpp = INF_XMPP_CONNECTION(gsasl_callback_hook_get(ctx));
  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  switch(prop)
  {
  case GSASL_ANONYMOUS_TOKEN:
    gsasl_property_set(sctx, GSASL_ANONYMOUS_TOKEN, priv->jid);
    return GSASL_OK;
  case GSASL_VALIDATE_ANONYMOUS:
    /* Authentication always successful */
    return GSASL_OK;
  default:
    /* This is only used when using the built-in SASL context, and this one
     * only supports anonymous authentication. */
    g_assert_not_reached();
    return GSASL_NO_CALLBACK;
  }
}

static gboolean
inf_xmpp_connection_sasl_ensure(InfXmppConnection* xmpp)
{
  InfXmppConnectionPrivate* priv;
  int ret;
  GError* error;
 
  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  if(priv->sasl_context == NULL)
  {
    ret = gsasl_init(&priv->sasl_own_context);
    if(ret != GSASL_OK)
    {
      error = NULL;
      g_set_error(
        &error,
        inf_xmpp_connection_gsasl_error_quark,
        ret,
        "%s",
        gsasl_strerror(ret)
      );

      g_signal_emit(G_OBJECT(xmpp), xmpp_connection_signals[ERROR], 0, error);
      g_free(error);

      inf_xmpp_connection_terminate(xmpp);
      return FALSE;
    }
    else
    {
      priv->sasl_context = priv->sasl_own_context;
      gsasl_callback_set(priv->sasl_context, inf_xmpp_connection_sasl_cb);
      gsasl_callback_hook_set(priv->sasl_context, xmpp);
      g_object_notify(G_OBJECT(xmpp), "sasl-context");
    }
  }

  g_assert(priv->sasl_context != NULL);
  return TRUE;
}

static void
inf_xmpp_connection_sasl_finish(InfXmppConnection* xmpp)
{
  InfXmppConnectionPrivate* priv;
  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  g_assert(priv->sasl_session != NULL);
  gsasl_finish(priv->sasl_session);
  priv->sasl_session = NULL;

  /* Authentication done, switch to AUTH_CONNECTED */
  priv->status = INF_XMPP_CONNECTION_AUTH_CONNECTED;
  /* We might be in a XML callback here, so do not initiate the stream right
   * now because it replaces the XML parser. The stream is reinitiated in
   * received_cb(). */
}

static void
inf_xmpp_connection_sasl_request(InfXmppConnection* xmpp,
                                 const gchar* input)
{
  InfXmppConnectionPrivate* priv;
  xmlNodePtr reply;
  gchar* output;
  int ret;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);
  g_assert(priv->status == INF_XMPP_CONNECTION_AUTHENTICATING);
  g_assert(priv->sasl_session != NULL);

  ret = gsasl_step64(priv->sasl_session, input, &output);
  if(ret != GSASL_NEEDS_MORE && ret != GSASL_OK)
  {
    inf_xmpp_connection_sasl_error(xmpp, ret);
  }
  else
  {
    /* We do not need to send a challenge when the authentication
     * has already been completed, but we need to response every
     * challenge. */
    if(output != NULL)
    {
      reply = NULL;
      switch(priv->site)
      {
      case INF_XMPP_CONNECTION_SERVER:
        if(ret == GSASL_NEEDS_MORE)
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
        xmlNodeAddContent(reply, (const xmlChar*)output);
        inf_xmpp_connection_send_xml(xmpp, reply);
        xmlFreeNode(reply);
      }

      free(output);
    }

    /* Send authentication success to client when done */
    if(ret == GSASL_OK)
    {
      if(priv->site == INF_XMPP_CONNECTION_SERVER)
      {
        reply = inf_xmpp_connection_node_new_sasl("success");
        inf_xmpp_connection_send_xml(xmpp, reply);
        xmlFreeNode(reply);

        inf_xmpp_connection_sasl_finish(xmpp);
      }

      /* Wait for <success> from server before calling finish on
       * client side. */
    }
  }
}

static void
inf_xmpp_connection_sasl_init(InfXmppConnection* xmpp,
                              const gchar* mechanism)
{
  InfXmppConnectionPrivate* priv;
  int ret;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  g_assert(priv->sasl_context != NULL);
  g_assert(priv->sasl_session == NULL);

  switch(priv->site)
  {
  case INF_XMPP_CONNECTION_CLIENT:
    g_assert(priv->status == INF_XMPP_CONNECTION_AWAITING_FEATURES);
    ret = gsasl_client_start(
      priv->sasl_context,
      mechanism,
      &priv->sasl_session
    );
    break;
  case INF_XMPP_CONNECTION_SERVER:
    g_assert(priv->status == INF_XMPP_CONNECTION_INITIATED);
    ret = gsasl_server_start(
      priv->sasl_context,
      mechanism,
      &priv->sasl_session
    );
    break;
  default:
    g_assert_not_reached();
    break;
  }

  if(ret != GSASL_OK)
  {
    inf_xmpp_connection_sasl_error(xmpp, ret);
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
  static const gchar xmpp_connection_initial_request_to[] =
    "<stream:stream xmlns:stream=\"http://etherx.jabber.org/streams\" "
    "xmlns=\"jabber:client\" version=\"1.0\" from=\"%s\" to=\"%s\">";

  static const gchar xmpp_connection_initial_request_noto[] = 
    "<stream:stream xmlns:stream=\"http://etherx.jabber.org/streams\" "
    "xmlns=\"jabber:client\" version=\"1.0\" from=\"%s\">";

  InfXmppConnectionPrivate* priv;
  char* mech_list;
  char* begin;
  char* end;
  int ret;

  gchar* reply;
  const xmlChar** to_attr;

  xmlNodePtr features;
  xmlNodePtr starttls;
  xmlNodePtr mechanisms;
  xmlNodePtr mechanism;
  GError* error;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);
  g_assert(priv->site == INF_XMPP_CONNECTION_SERVER);
  g_assert(priv->parser != NULL);

  g_assert(priv->status == INF_XMPP_CONNECTION_CONNECTED ||
           priv->status == INF_XMPP_CONNECTION_AUTH_CONNECTED);

  /* Find from attribute from incoming stream to use as to attribute in
   * outgoing stream. */
  to_attr = NULL;
  if(attrs != NULL)
  {
    to_attr = attrs;
    while(*to_attr != NULL)
    {
      if(strcmp((const gchar*)*to_attr, "from") == 0)
      {
        ++ to_attr;
        break;
      }
      else 
      {
        to_attr += 2;
      }
    }
  }

  if(to_attr == NULL || *to_attr == NULL)
  {  
    reply = g_strdup_printf(
      xmpp_connection_initial_request_noto,
      priv->jid
    );
  }
  else
  {
    reply = g_strdup_printf(
      xmpp_connection_initial_request_to,
      priv->jid,
      (const gchar*)*to_attr
    );
  }

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

  if(priv->session == NULL)
  {
    starttls = inf_xmpp_connection_node_new_tls("starttls");
    xmlAddChild(features, starttls);
    xmlNewChild(starttls, NULL, (const xmlChar*)"required", NULL);
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
      g_signal_emit(G_OBJECT(xmpp), xmpp_connection_signals[ERROR], 0, error);
      g_free(error);
      inf_xmpp_connection_terminate(xmpp);
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
      ret = gsasl_server_mechlist(priv->sasl_context, &mech_list);
      if(ret != GSASL_OK)
      {
        xmlFreeNode(features);
        error = NULL;

        g_set_error(
          &error,
          inf_xmpp_connection_gsasl_error_quark,
          ret,
          "%s",
          gsasl_strerror(ret)
        );

        g_signal_emit(
          G_OBJECT(xmpp),
          xmpp_connection_signals[ERROR],
          0,
          error
        );

        g_free(error);
        inf_xmpp_connection_terminate(xmpp);
        return;
      }
      else
      {
        begin = end = mech_list;
        while(*end != '\0')
        {
          end = strchr(begin, ' ');
          if(end == NULL) end = begin + strlen(begin);

          /* Cannot use content parameter of xmlNewChild because it does not
           * support the length parameter. */
          mechanism = xmlNewChild(
            mechanisms,
            NULL,
            (const xmlChar*)"mechanism",
            NULL
          );

          xmlNodeAddContentLen(mechanism, (const xmlChar*)begin, end - begin);

          begin = end + 1;
        }

        free(mech_list);
      }
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
  xmlChar* mechanism;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);
  g_assert(priv->site == INF_XMPP_CONNECTION_SERVER);
  g_assert(priv->status == INF_XMPP_CONNECTION_INITIATED);

  if(priv->session == NULL)
  {
    if(strcmp((const gchar*)xml->name, "starttls") == 0)
    {
      proceed = inf_xmpp_connection_node_new_tls("proceed");
      inf_xmpp_connection_send_xml(xmpp, proceed);
      xmlFreeNode(proceed);

      inf_xmpp_connection_tls_init(xmpp);
    }
    else
    {
      inf_xmpp_connection_terminate_error(
        xmpp,
        INF_XMPP_CONNECTION_STREAM_ERROR_NOT_AUTHORIZED,
        "Stream is not yet secured with TLS"
      );
    }
  }
  else
  {
    /* This should already have been allocated before having sent the list
     * of mechanisms to the client. */
    g_assert(priv->sasl_context != NULL);
    if(strcmp((const gchar*)xml->name, "auth") == 0)
    {
      mechanism = xmlGetProp(xml, (const xmlChar*)"mechanism");
      if(mechanism != NULL &&
         gsasl_server_support_p(priv->sasl_context, (const gchar*)mechanism))
      {
        inf_xmpp_connection_sasl_init(xmpp, (const gchar*)mechanism);
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

      if(mechanism != NULL)
        xmlFree(mechanism);
    }
    else
    {
      /* Got something else than <auth> */
      inf_xmpp_connection_terminate_error(
        xmpp,
        INF_XMPP_CONNECTION_STREAM_ERROR_NOT_AUTHORIZED,
        "Stream is not yet authorized"
      );
    }
  }
}

static void
inf_xmpp_connection_process_features(InfXmppConnection* xmpp,
                                     xmlNodePtr xml)
{
  InfXmppConnectionPrivate* priv;
  xmlNodePtr child;
  xmlNodePtr starttls;
  xmlNodePtr auth;
  xmlNodePtr mechanisms;
  GString* mechanisms_string;
  const gchar* content;
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
  }
  else if(priv->session == NULL)
  {
    for(child = xml->children; child != NULL; child = child->next)
      if(strcmp((const gchar*)child->name, "starttls") == 0)
        break;

    /* Server has no StartTLS feature. We don't like that. */
    if(child == NULL)
    {
      error = NULL;
      g_set_error(
        &error,
        inf_xmpp_connection_error_quark,
        INF_XMPP_CONNECTION_ERROR_TLS_UNSUPPORTED,
        "%s",
        inf_xmpp_connection_strerror(
          INF_XMPP_CONNECTION_ERROR_TLS_UNSUPPORTED
        )
      );

      g_signal_emit(G_OBJECT(xmpp), xmpp_connection_signals[ERROR], 0, error);
      g_error_free(error);

      inf_xmpp_connection_deinitiate(xmpp);
    }
    else
    {
      /* Server supports TLS, and this is what we are going to request now */
      starttls = inf_xmpp_connection_node_new_tls("starttls");
      inf_xmpp_connection_send_xml(xmpp, starttls);
      xmlFreeNode(starttls);

      priv->status = INF_XMPP_CONNECTION_ENCRYPTION_REQUESTED;
    }
  }
  else if(priv->status == INF_XMPP_CONNECTION_AWAITING_FEATURES)
  {
    for(child = xml->children; child != NULL; child = child->next)
      if(strcmp((const gchar*)child->name, "mechanisms") == 0)
        break;

    /* Server does not provide authentication mechanisms */
    if(child == NULL)
    {
      error = NULL;

      g_set_error(
        &error,
        inf_xmpp_connection_error_quark,
        INF_XMPP_CONNECTION_ERROR_AUTHENTICATION_UNSUPPORTED,
        "%s",
        inf_xmpp_connection_strerror(
          INF_XMPP_CONNECTION_ERROR_AUTHENTICATION_UNSUPPORTED
        )
      );

      g_signal_emit(G_OBJECT(xmpp), xmpp_connection_signals[ERROR], 0, error);
      g_error_free(error);

      inf_xmpp_connection_deinitiate(xmpp);
    }
    else if(inf_xmpp_connection_sasl_ensure(xmpp) == TRUE)
    {
      suggestion = NULL;
      mechanisms = child;

      if(priv->sasl_own_context != NULL)
      {
        /* We only support ANONYMOUS authentication when using the built-in
         * SASL context. */
        for(child = mechanisms->children; child != NULL; child = child->next)
        {
          if(strcmp((const gchar*)child->name, "mechanism") == 0)
          {
            content = (const gchar*)xmlNodeGetContent(child);
            if(strcmp(content, "ANONYMOUS") == 0)
              suggestion = "ANONYMOUS";
          }
        }
      }
      else
      {
        mechanisms_string = g_string_sized_new(128);
        for(child = mechanisms->children; child != NULL; child = child->next)
        {
          if(strcmp((const gchar*)child->name, "mechanism") == 0)
          {
            if(mechanisms_string->len > 0)
              g_string_append_c(mechanisms_string, ' ');

            g_string_append(mechanisms_string, (const gchar*)content);
          }
        }

        suggestion = gsasl_client_suggest_mechanism(
          priv->sasl_context,
          mechanisms_string->str
        );

        g_string_free(mechanisms_string, TRUE);
      }

      if(suggestion == NULL)
      {
        error = NULL;

        g_set_error(
          &error,
          inf_xmpp_connection_error_quark,
          INF_XMPP_CONNECTION_ERROR_NO_SUITABLE_MECHANISM,
          "%s",
          inf_xmpp_connection_strerror(
            INF_XMPP_CONNECTION_ERROR_NO_SUITABLE_MECHANISM
          )
        );

        g_signal_emit(G_OBJECT(xmpp), xmpp_connection_signals[ERROR], 0, error);
        g_error_free(error);

        inf_xmpp_connection_deinitiate(xmpp);
      }
      else
      {
        auth = inf_xmpp_connection_node_new_sasl("auth");

        xmlNewProp(
          auth,
          (const xmlChar*)"mechanism",
          (const xmlChar*)suggestion
        );

        inf_xmpp_connection_send_xml(xmpp, auth);
        xmlFreeNode(auth);

        inf_xmpp_connection_sasl_init(xmpp, suggestion);
      }
    }
  }
  else
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
    error = NULL;

    g_set_error(
      &error,
      inf_xmpp_connection_error_quark,
      INF_XMPP_CONNECTION_ERROR_TLS_FAILURE,
      "%s",
      inf_xmpp_connection_strerror(INF_XMPP_CONNECTION_ERROR_TLS_FAILURE)
    );

    g_signal_emit(G_OBJECT(xmpp), xmpp_connection_signals[ERROR], 0, error);
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
inf_xmpp_connection_process_authentication(InfXmppConnection* xmpp,
                                           xmlNodePtr xml)
{
  InfXmppConnectionPrivate* priv;
  InfXmppConnectionAuthError auth_code;
  xmlNodePtr child;

  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  switch(priv->site)
  {
  case INF_XMPP_CONNECTION_CLIENT:
    if(strcmp((const gchar*)xml->name, "challenge") == 0)
    {
      /* Process challenge from server */
      inf_xmpp_connection_sasl_request(
        xmpp,
        (const gchar*)xmlNodeGetContent(xml)
      );
    }
    else if(strcmp((const gchar*)xml->name, "failure") == 0)
    {
      child = xml->children;
      auth_code = INF_XMPP_CONNECTION_AUTH_ERROR_FAILED;
      
      if(xml->children != NULL)
      {
        auth_code = inf_xmpp_connection_auth_error_from_condition(
          (const gchar*)xml->children->name
        );
      }

      inf_xmpp_connection_emit_auth_error(xmpp, auth_code);

      /* TODO: Retry authentication, if possible */

      /* Remove SASL session */
      g_assert(priv->sasl_session != NULL);
      gsasl_finish(priv->sasl_session);
      priv->sasl_session = NULL;

      /* So that deinitiate does not try to abort the authentication */
      priv->status = INF_XMPP_CONNECTION_AWAITING_FEATURES;
      inf_xmpp_connection_deinitiate(xmpp);
    }
    else if(strcmp((const gchar*)xml->name, "success") == 0)
    {
      inf_xmpp_connection_sasl_finish(xmpp);
    }
    else
    {
      /* Unknown request. Ignore. */
    }

    break;
  case INF_XMPP_CONNECTION_SERVER:
    if(strcmp((const gchar*)xml->name, "response") == 0)
    {
      /* Process client reponse */
      inf_xmpp_connection_sasl_request(
        xmpp,
        (const gchar*)xmlNodeGetContent(xml)
      );
    }
    else if(strcmp((const gchar*)xml->name, "abort") == 0)
    {
      inf_xmpp_connection_send_auth_error(
        xmpp,
        INF_XMPP_CONNECTION_AUTH_ERROR_ABORTED
      );

      inf_xmpp_connection_emit_auth_error(
        xmpp,
        INF_XMPP_CONNECTION_AUTH_ERROR_ABORTED
      );

      /* Fall back to initiated state, wait for another auth request. */
      priv->status = INF_XMPP_CONNECTION_INITIATED;
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

      g_signal_emit(G_OBJECT(xmpp), xmpp_connection_signals[ERROR], 0, error);
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
      case INF_XMPP_CONNECTION_AUTH_INITIATED:
        /* The client should be waiting for <stream:stream> from the server
         * in this state, and sax_end_element should not have called this
         * function. Also, this is a client-only state (the server goes
         * directly to READY after having received <stream:stream>). */
      case INF_XMPP_CONNECTION_CONNECTING:
      case INF_XMPP_CONNECTION_CONNECTED:
      case INF_XMPP_CONNECTION_AUTH_CONNECTED:
      case INF_XMPP_CONNECTION_HANDSHAKING:
      case INF_XMPP_CONNECTION_CLOSING_STREAM:
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

  /* If we are not in the toplevel (directly in <stream:stream>) but in some
   * child, process this normally because it belongs to a child. */
  if(priv->root != NULL)
  {
    inf_xmpp_connection_process_end_element(xmpp, name);
  }
  else
  {
    /* Should have caused an error in the XML parser otherwise */
    g_assert(strcmp((const gchar*)name, "stream:stream") == 0);

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
    case INF_XMPP_CONNECTION_CONNECTED:
    case INF_XMPP_CONNECTION_AUTH_CONNECTED:
      /* We should not get </stream:stream> before we got <stream:stream>,
       * which would have caused us to change into the INITIATED state. The
       * XML parser should have reported an error in this case. */
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

  fprintf(stderr, "XML warning from %s: %s\n", addr_str, warn_str);
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

  xmpp = INF_XMPP_CONNECTION(context);
  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  error_xml = xmlCtxtGetLastError(priv->parser);
  g_assert(error_xml != NULL);

  /* The XML parser should not be called in this state */
  g_assert(priv->status != INF_XMPP_CONNECTION_HANDSHAKING);

  /* If we are in this state, the server waits already on a GnuTLS
   * handshake, so we cannot send arbitrary XML here. Also cannot
   * send <stream:error> without having sent <stream:stream>. */
  if(priv->status != INF_XMPP_CONNECTION_ENCRYPTION_REQUESTED &&
     priv->status != INF_XMPP_CONNECTION_CONNECTED &&
     priv->status != INF_XMPP_CONNECTION_AUTH_CONNECTED)
  {
    /* TODO: Get more accurate error information from stream error */
    stream_code = INF_XMPP_CONNECTION_STREAM_ERROR_BAD_FORMAT;

    inf_xmpp_connection_terminate_error(
      xmpp,
      stream_code,
      error_xml->message
    );
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
    "xmlns:stream=\"http://etherx.jabber.org/streams\" from=\"%s\">";

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
    request = g_strdup_printf(xmpp_connection_initial_request, priv->jid);
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

  xmpp = INF_XMPP_CONNECTION(user_data);
  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  g_assert(priv->position >= len);
  g_object_ref(G_OBJECT(xmpp));

  while(priv->messages != NULL && priv->messages->position <= len)
  {
    if(priv->messages->sent_func != NULL)
      priv->messages->sent_func(xmpp, priv->messages->user_data);

    /* Note that the sent func might have called _clear() in which case all
     * messages have already been removed. */
    if(priv->messages != NULL)
      inf_xmpp_connection_pop_message(xmpp);
  }

  for(message = priv->messages; message != NULL; message = message->next)
    message->position -= len;

  priv->position -= len;
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

  xmpp = INF_XMPP_CONNECTION(user_data);
  priv = INF_XMPP_CONNECTION_PRIVATE(xmpp);

  /* We just keep the connection open to send a final gnutls bye and
   * </stream:stream> in this state, any input gets discarded. */
  if(priv->status == INF_XMPP_CONNECTION_CLOSING_GNUTLS)
    return;

  g_assert(priv->parser != NULL);

  if(priv->status != INF_XMPP_CONNECTION_HANDSHAKING)
  {
    if(priv->session != NULL)
    {
      priv->pull_data = data;
      priv->pull_len = len;

      while(priv->pull_len > 0)
      {
        res = gnutls_record_recv(priv->session, buffer, 2048);
        if(res < 0)
        {
          /* A TLS error occured. */
          error = NULL;
          g_set_error(
            &error,
            inf_xmpp_connection_gnutls_error_quark,
            res,
            "%s",
            gnutls_strerror(res)
          );

          g_signal_emit(
            G_OBJECT(xmpp),
            xmpp_connection_signals[ERROR],
            0,
            error
          );

          g_error_free(error);

          /* We cannot assume that GnuTLS is working enough to send a
           * final </stream:stream> or something, so just close the
           * underlaying TCP connection. */
          inf_tcp_connection_close(priv->tcp);
        }
        else if(res == 0)
        {
          /* Remote site sent gnutls_bye. This involves session closure. */
          inf_tcp_connection_close(priv->tcp);
        }
        else
        {
          /* Feed decoded data into XML parser */
          printf("\033[00;32m%.*s\033[00;00m\n", (int)res, buffer);
          xmlParseChunk(priv->parser, buffer, res, 0);
        }
      }
    }
    else
    {
      /* Feed input directly into XML parser */
      printf("\033[00;31m%.*s\033[00;00m\n", (int)len, data);
      xmlParseChunk(priv->parser, data, len, 0);
    }
  }
  else
  {
    /* Perform TLS handshake */
    priv->pull_data = data;
    priv->pull_len = len;
    inf_xmpp_connection_tls_handshake(xmpp);

    /* TODO: Perhaps we should just close the connection in this case so that
     * evil people cannot trigger this assertion by modified TLS packtes. */
    g_assert(priv->pull_len == 0);
  }

  if(priv->status == INF_XMPP_CONNECTION_CLOSING_GNUTLS)
  {
    /* Status changed to CLOSING_GNUTLS, this means that someone called
     * _terminate(). Clean up any resources in use (XML parser, GnuTLS
     * session etc. */
    inf_xmpp_connection_clear(xmpp);

    /* Close the TCP connection after remaining stuff has been sent out. */
    inf_xmpp_connection_push_message(
      xmpp,
      inf_xmpp_connection_received_cb_sent_func,
      NULL,
      NULL
    );
  }
  else if(priv->status == INF_XMPP_CONNECTION_AUTH_CONNECTED)
  {
    /* Reinitiate connection after successful authentication */
    /* TODO: Only do this if status at the beginning of this call was
     * AUTHENTICATING */
    inf_xmpp_connection_initiate(xmpp);
  }
}

static void
inf_xmpp_connection_error_cb(InfTcpConnection* tcp,
                             GError* error,
                             gpointer user_data)
{
  /* Do not modify status because we get a status change notify from the
   * TCP connection little later anyway. */
  g_signal_emit(
    G_OBJECT(user_data),
    xmpp_connection_signals[ERROR],
    0,
    error
  );
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
      inf_xmpp_connection_clear(xmpp);
      priv->status = INF_XMPP_CONNECTION_CLOSED;
      g_object_notify(G_OBJECT(xmpp), "status");
    }
    else
    {
      g_assert(priv->session == NULL);
      g_assert(priv->messages == NULL);
      g_assert(priv->parser == NULL);
      g_assert(priv->doc == NULL);
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

  if(priv->tcp != NULL)
  {
    g_object_freeze_notify(G_OBJECT(xmpp));
    g_object_get(G_OBJECT(priv->tcp), "status", &tcp_status, NULL);

    /* This will cause a status notify which will actually delete
     * GnuTLS session (if any) and the message queue. */
    if(tcp_status != INF_TCP_CONNECTION_CLOSED)
      inf_tcp_connection_close(priv->tcp);

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->tcp),
      G_CALLBACK(inf_xmpp_connection_sent_cb),
      xmpp
    );

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->tcp),
      G_CALLBACK(inf_xmpp_connection_received_cb),
      xmpp
    );

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->tcp),
      G_CALLBACK(inf_xmpp_connection_error_cb),
      xmpp
    );

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->tcp),
      G_CALLBACK(inf_xmpp_connection_notify_status_cb),
      xmpp
    );

    g_object_unref(G_OBJECT(priv->tcp));
    g_object_thaw_notify(G_OBJECT(xmpp));
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
  priv->jid = g_strdup(g_get_host_name());

  priv->position = 0;
  priv->messages = NULL;
  priv->last_message = NULL;

  priv->parser = NULL;
  priv->root = NULL;
  priv->cur = NULL;

  priv->doc = NULL;
  priv->buf = NULL;

  priv->session = NULL;
  priv->cred = NULL;
  priv->own_cred = NULL;
  priv->pull_data = NULL;
  priv->pull_len = 0;

  priv->sasl_context = NULL;
  priv->sasl_own_context = NULL;
  priv->sasl_session = NULL;
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
  g_object_get(G_OBJECT(priv->tcp), "status", &status, NULL);

  /* Initiate stream if connection is already established */
  if(status == INF_TCP_CONNECTION_CONNECTED)
    inf_xmpp_connection_initiate(INF_XMPP_CONNECTION(obj));

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

  if(priv->sasl_own_context != NULL)
  {
    gsasl_done(priv->sasl_own_context);
    priv->sasl_own_context = NULL;
    priv->sasl_context = NULL;
  }

  if(priv->own_cred != NULL)
  {
    gnutls_certificate_free_credentials(priv->own_cred);
    priv->own_cred = NULL;
    priv->cred = NULL;
  }

  G_OBJECT_CLASS(parent_class)->dispose(object);
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
  case PROP_JID:
    g_free(priv->jid);
    priv->jid = g_value_dup_string(value);
    break;
  case PROP_CREDENTIALS:
    /* Cannot change credentials when currently in use */
    g_assert(priv->session == NULL);

    if(priv->own_cred != NULL)
    {
      gnutls_certificate_free_credentials(priv->own_cred);
      priv->own_cred = NULL;
    }

    priv->cred = g_value_get_pointer(value);
    break;
  case PROP_SASL_CONTEXT:
    /* Cannot change context when currently in use */
    g_assert(priv->sasl_session == NULL);

    if(priv->sasl_own_context != NULL)
    {
      gsasl_done(priv->sasl_own_context);
      priv->sasl_own_context = NULL;
    }

    priv->sasl_context = g_value_get_pointer(value);
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
  case PROP_JID:
    g_value_set_string(value, priv->jid);
    break;
  case PROP_CREDENTIALS:
    g_value_set_pointer(value, priv->cred);
    break;
  case PROP_SASL_CONTEXT:
    g_value_set_pointer(value, priv->sasl_context);
    break;
  case PROP_STATUS:
    g_value_set_enum(value, inf_xmpp_connection_get_xml_status(xmpp));
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

static void
inf_xmpp_connection_xml_connection_close(InfXmlConnection* connection)
{
  InfXmppConnectionPrivate* priv;
  priv = INF_XMPP_CONNECTION_PRIVATE(connection);

  /* Connection is already being closed */
  g_return_if_fail(priv->status != INF_XMPP_CONNECTION_CLOSING_STREAM &&
                   priv->status != INF_XMPP_CONNECTION_CLOSING_GNUTLS &&
                   priv->status != INF_XMPP_CONNECTION_CLOSED);

  switch(priv->status)
  {
  case INF_XMPP_CONNECTION_CONNECTING:
    inf_tcp_connection_close(priv->tcp);
    break;
  case INF_XMPP_CONNECTION_CONNECTED:
  case INF_XMPP_CONNECTION_AUTH_CONNECTED:
    inf_xmpp_connection_terminate(INF_XMPP_CONNECTION(connection));
    /* This is not in a XML callback, so we need to call clear explicitely */
    inf_xmpp_connection_clear(INF_XMPP_CONNECTION(connection));
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
    inf_xmpp_connection_terminate(INF_XMPP_CONNECTION(connection));
    /* This is not in a XML callback, so we need to call clear explicitely */
    inf_xmpp_connection_clear(INF_XMPP_CONNECTION(connection));
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

  inf_xmpp_connection_push_message(
    INF_XMPP_CONNECTION(connection),
    inf_xmpp_connection_xml_connection_send_sent,
    inf_xmpp_connection_xml_connection_send_free,
    xml
  );
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
  object_class->set_property = inf_xmpp_connection_set_property;
  object_class->get_property = inf_xmpp_connection_get_property;

  xmpp_class->error = NULL;

  inf_xmpp_connection_error_quark = g_quark_from_static_string(
    "INF_XMPP_CONNECTION_ERROR"
  );

  inf_xmpp_connection_stream_error_quark = g_quark_from_static_string(
    "INF_XMPP_CONNECTION_STREAM_ERROR"
  );

  inf_xmpp_connection_auth_error_quark = g_quark_from_static_string(
    "INF_XMPP_CONNECTION_AUTH_ERROR"
  );

  inf_xmpp_connection_gnutls_error_quark = g_quark_from_static_string(
    "INF_XMPP_CONNECTION_GNUTLS_ERROR"
  );

  inf_xmpp_connection_gsasl_error_quark = g_quark_from_static_string(
    "INF_XMPP_CONNECTION_GSASL_ERROR"
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
    PROP_JID,
    g_param_spec_string(
      "jid",
      "JID",
      "JID of the local entity",
      "localhost",
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_CREDENTIALS,
    g_param_spec_pointer(
      "credentials",
      "Credentials",
      "The certificate credentials for GnuTLS",
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_SASL_CONTEXT,
    g_param_spec_pointer(
      "sasl-context",
      "GnuSASL context",
      "The GnuSASL context used for authentication",
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_override_property(object_class, PROP_STATUS, "status");

  xmpp_connection_signals[ERROR] = g_signal_new(
    "error",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfXmppConnectionClass, error),
    NULL, NULL,
    inf_marshal_VOID__POINTER,
    G_TYPE_NONE,
    1,
    G_TYPE_POINTER /* actually a GError* */
  );
}

static void
inf_xmpp_connection_xml_connection_init(gpointer g_iface,
                                        gpointer iface_data)
{
  InfXmlConnectionIface* iface;
  iface = (InfXmlConnectionIface*)g_iface;

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

/** inf_xmpp_connection_new:
 *
 * @tcp: The underlaying TCP connection to use.
 * @site: Whether this is a XMPP client or server.
 * @jid: The JID of the local entity.
 * @cred: Certificate credentials used to secure the communication.
 * @sasl_context: A SASL context used for authentication.
 *
 * Creates a new #InfXmppConnection with @tcp as communication channel. No
 * attempt is being made to open @tcp, if it is not already open. However,
 * communication is initiated as soon as @tcp gets into
 * %INF_TCP_CONNECTION_CONNECTED state, so you might still open it
 * lateron yourself.
 *
 * @cred may be %NULL in which case the connection creates the credentials
 * as soon as they are required, however, this might take some time.
 * If @sasl_context is %NULL, #InfXmppConnection uses a built-in context
 * that only supports ANONYMOUS authentication.
 *
 * Return Value: A new #InfXmppConnection.
 **/
InfXmppConnection*
inf_xmpp_connection_new(InfTcpConnection* tcp,
                        InfXmppConnectionSite site,
                        const gchar* jid,
                        gnutls_certificate_credentials_t cred,
                        Gsasl* sasl_context)
{
  GObject* object;

  g_return_val_if_fail(INF_IS_TCP_CONNECTION(tcp), NULL);

  object = g_object_new(
    INF_TYPE_XMPP_CONNECTION,
    "tcp-connection", tcp,
    "site", site,
    "jid", jid,
    "credentials", cred,
    "sasl-context", sasl_context,
    NULL
  );

  return INF_XMPP_CONNECTION(object);
}
