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

#include <libinfinity/server/infd-xmpp-server.h>
#include <libinfinity/server/infd-tcp-server.h>
#include <libinfinity/server/infd-xml-server.h>
#include <libinfinity/common/inf-xmpp-connection.h>
#include <libinfinity/inf-marshal.h>

typedef enum InfdXmppServerStatus_ {
  INFD_XMPP_SERVER_CLOSED,
  INFD_XMPP_SERVER_OPEN
} InfdXmppServerStatus;

typedef struct _InfdXmppServerPrivate InfdXmppServerPrivate;
struct _InfdXmppServerPrivate {
  InfdTcpServer* tcp;
  InfdXmppServerStatus status;
  gchar* local_hostname;

  gnutls_certificate_credentials_t tls_creds;
  gnutls_certificate_credentials_t tls_own_creds;

  Gsasl* sasl_context;
  Gsasl* sasl_own_context;
};

enum {
  PROP_0,

  PROP_TCP,
  PROP_LOCAL_HOSTNAME,

  PROP_CREDENTIALS,
  PROP_SASL_CONTEXT,

  /* Overridden from XML server */
  PROP_STATUS
};

enum {
  ERROR,

  LAST_SIGNAL
};

#define INFD_XMPP_SERVER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFD_TYPE_XMPP_SERVER, InfdXmppServerPrivate))

static GObjectClass* parent_class;
static guint xmpp_server_signals[LAST_SIGNAL];

static void
infd_xmpp_server_new_connection_cb(InfdTcpServer* tcp_server,
                                   InfTcpConnection* tcp_connection,
                                   gpointer user_data)
{
  InfdXmppServer* xmpp_server;
  InfdXmppServerPrivate* priv;
  InfXmppConnection* xmpp_connection;
  InfIpAddress* addr;
  gchar* addr_str;

  xmpp_server = INFD_XMPP_SERVER(user_data);
  priv = INFD_XMPP_SERVER_PRIVATE(xmpp_server);

  /* TODO: We could perform a reverse DNS lookup to find the client hostname
   * here. */
  g_object_get(G_OBJECT(tcp_connection), "remote-address", &addr, NULL);
  addr_str = inf_ip_address_to_string(addr);
  inf_ip_address_free(addr);

  xmpp_connection = inf_xmpp_connection_new(
    tcp_connection,
    INF_XMPP_CONNECTION_SERVER,
    priv->local_hostname,
    addr_str,
    priv->tls_creds,
    priv->sasl_context
  );

  g_free(addr_str);

  /* We could, alternatively, keep the connection around until authentication
   * has completed and emit the new_connection signal after that, to guarantee
   * that the connection is open when new_connection is emitted. */
  infd_xml_server_new_connection(
    INFD_XML_SERVER(xmpp_server),
    INF_XML_CONNECTION(xmpp_connection)
  );

  g_object_unref(G_OBJECT(xmpp_connection));
}

static void
infd_xmpp_server_error_cb(InfdTcpServer* tcp_server,
                          GError* error,
                          gpointer user_data)
{
  g_signal_emit(
    G_OBJECT(user_data),
    xmpp_server_signals[ERROR],
    0,
    error
  );
}

static void
infd_xmpp_server_notify_status_cb(InfdTcpServer* tcp_server,
                                  GParamSpec* pspec,
                                  gpointer user_data)
{
  InfdXmppServer* xmpp;
  InfdXmppServerPrivate* priv;
  InfdTcpServerStatus status;

  xmpp = INFD_XMPP_SERVER(user_data);
  priv = INFD_XMPP_SERVER_PRIVATE(xmpp);
  g_object_get(G_OBJECT(tcp_server), "status", &status, NULL);

  switch(status)
  {
  case INFD_TCP_SERVER_CLOSED:
    if(priv->status != INFD_XMPP_SERVER_CLOSED)
    {
      priv->status = INFD_XMPP_SERVER_CLOSED;
      g_object_notify(G_OBJECT(xmpp), "status");
    }

    break;
  case INFD_TCP_SERVER_OPEN:
    if(priv->status != INFD_XMPP_SERVER_OPEN)
    {
      priv->status = INFD_XMPP_SERVER_OPEN;
      g_object_notify(G_OBJECT(xmpp), "status");
    }

    break;
  default:
    g_assert_not_reached();
    break;
  }
}

static void
infd_xmpp_server_set_tcp(InfdXmppServer* xmpp,
                         InfdTcpServer* tcp)
{
  InfdXmppServerPrivate* priv;
  InfdTcpServerStatus tcp_status;

  priv = INFD_XMPP_SERVER_PRIVATE(xmpp);

  if(priv->tcp != NULL)
  {
    g_object_freeze_notify(G_OBJECT(xmpp));
    g_object_get(G_OBJECT(priv->tcp), "status", &tcp_status, NULL);

    /* This will cause a notify that will adjust the XMPP status later */
    if(tcp_status != INFD_TCP_SERVER_CLOSED)
      infd_tcp_server_close(priv->tcp);
    
    g_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->tcp),
      G_CALLBACK(infd_xmpp_server_new_connection_cb),
      xmpp
    );

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->tcp),
      G_CALLBACK(infd_xmpp_server_error_cb),
      xmpp
    );

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->tcp),
      G_CALLBACK(infd_xmpp_server_notify_status_cb),
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
      "new-connection",
      G_CALLBACK(infd_xmpp_server_new_connection_cb),
      xmpp
    );

    g_signal_connect(
      G_OBJECT(tcp),
      "error",
      G_CALLBACK(infd_xmpp_server_error_cb),
      xmpp
    );

    g_signal_connect(
      G_OBJECT(tcp),
      "notify::status",
      G_CALLBACK(infd_xmpp_server_notify_status_cb),
      xmpp
    );

    g_object_get(G_OBJECT(tcp), "status", &tcp_status, NULL);
    switch(tcp_status)
    {
    case INFD_TCP_SERVER_CLOSED:
      g_assert(priv->status == INFD_XMPP_SERVER_CLOSED);
      break;
    case INFD_TCP_SERVER_OPEN:
      priv->status = INFD_XMPP_SERVER_OPEN;
      g_object_notify(G_OBJECT(xmpp), "status");
      break;
    default:
      g_assert_not_reached();
      break;
    }
  }
}

static int
infd_xmpp_server_sasl_cb(Gsasl* ctx,
                         Gsasl_session* sctx,
                         Gsasl_property prop)
{
  InfdXmppServer* xmpp;
  InfdXmppServerPrivate* priv;

  xmpp = INFD_XMPP_SERVER(gsasl_callback_hook_get(ctx));
  priv = INFD_XMPP_SERVER_PRIVATE(xmpp);

  switch(prop)
  {
  case GSASL_ANONYMOUS_TOKEN:
    gsasl_property_set(sctx, GSASL_ANONYMOUS_TOKEN, priv->local_hostname);
    return GSASL_OK;
  case GSASL_VALIDATE_ANONYMOUS:
    /* Authentaction always successful */
    return GSASL_OK;
  default:
    /* This is only used when using built-in SASL context, and this one
     * only supports anonymous authentication. */
    g_assert_not_reached();
    return GSASL_NO_CALLBACK;
  }
}

static GObject*
infd_xmpp_server_constructor(GType type,
                             guint n_construct_properties,
                             GObjectConstructParam* construct_properties)
{
  static const guint xmpp_server_dh_bits = 1024;

  InfdXmppServerPrivate* priv;
  gnutls_dh_params_t dh_params;
  GObject* obj;

  obj = G_OBJECT_CLASS(parent_class)->constructor(
    type,
    n_construct_properties,
    construct_properties
  );

  priv = INFD_XMPP_SERVER_PRIVATE(obj);
  g_assert(priv->tcp != NULL);

  /* Make sure TLS credentials are present */
  /* TODO: Perhaps we should do this in a thread, and stay in status OPENING
   * until the generation has completed. */
  if(priv->tls_creds == NULL)
  {
    gnutls_certificate_allocate_credentials(&priv->tls_own_creds);
    priv->tls_creds = priv->tls_own_creds;

    gnutls_dh_params_init(&dh_params);
    gnutls_dh_params_generate2(dh_params, xmpp_server_dh_bits);
    gnutls_certificate_set_dh_params(priv->tls_creds, dh_params);

    /* TODO: Create a new random key/certificate here. */
    gnutls_certificate_set_x509_key_file(
      priv->tls_creds,
      "cert.pem",
      "key.pem",
      GNUTLS_X509_FMT_PEM
    );

    g_object_notify(G_OBJECT(obj), "credentials");
  }

  if(priv->sasl_context == NULL)
  {
    /* Failure does not matter too much because every XMPP connection will
     * generate an own SASL context in this case. */
    if(gsasl_init(&priv->sasl_own_context) == GSASL_OK)
    {
      priv->sasl_context = priv->sasl_own_context;
      gsasl_callback_set(priv->sasl_context, infd_xmpp_server_sasl_cb);
      gsasl_callback_hook_set(priv->sasl_context, obj);
      /* TODO: Only allow ANONYMOUS authentaction. This probably has to be
       * solved via a mechanisms list in XMPP connection. */
      g_object_notify(G_OBJECT(obj), "sasl-context");
    }
  }

  return obj;
}

static void
infd_xmpp_server_init(GTypeInstance* instance,
                      gpointer g_class)
{
  InfdXmppServer* xmpp;
  InfdXmppServerPrivate* priv;

  xmpp = INFD_XMPP_SERVER(instance);
  priv = INFD_XMPP_SERVER_PRIVATE(xmpp);

  priv->tcp = NULL;
  priv->status = INFD_XMPP_SERVER_CLOSED;
  priv->local_hostname = g_strdup(g_get_host_name());

  priv->tls_creds = NULL;
  priv->tls_own_creds = NULL;
  priv->sasl_context = NULL;
  priv->sasl_own_context = NULL;
}

static void
infd_xmpp_server_dispose(GObject* object)
{
  InfdXmppServer* xmpp;
  InfdXmppServerPrivate* priv;

  xmpp = INFD_XMPP_SERVER(object);
  priv = INFD_XMPP_SERVER_PRIVATE(xmpp);

  if(priv->status != INFD_XMPP_SERVER_CLOSED)
    infd_xml_server_close(INFD_XML_SERVER(xmpp));

  if(priv->tcp != NULL)
    g_object_unref(G_OBJECT(priv->tcp));

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
infd_xmpp_server_finalize(GObject* object)
{
  InfdXmppServer* xmpp;
  InfdXmppServerPrivate* priv;

  xmpp = INFD_XMPP_SERVER(object);
  priv = INFD_XMPP_SERVER_PRIVATE(xmpp);

  g_free(priv->local_hostname);

  if(priv->sasl_own_context != NULL)
    gsasl_done(priv->sasl_own_context);

  if(priv->tls_own_creds != NULL)
    gnutls_certificate_free_credentials(priv->tls_own_creds);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
infd_xmpp_server_set_property(GObject* object,
                              guint prop_id,
                              const GValue* value,
                              GParamSpec* pspec)
{
  InfdXmppServer* xmpp;
  InfdXmppServerPrivate* priv;

  xmpp = INFD_XMPP_SERVER(object);
  priv = INFD_XMPP_SERVER_PRIVATE(xmpp);

  switch(prop_id)
  {
  case PROP_TCP:
    infd_xmpp_server_set_tcp(
      xmpp,
      INFD_TCP_SERVER(g_value_get_object(value))
    );

    break;
  case PROP_LOCAL_HOSTNAME:
    g_free(priv->local_hostname);
    priv->local_hostname = g_value_dup_string(value);
    if(priv->local_hostname == NULL)
      priv->local_hostname = g_strdup(g_get_host_name());
    break;
  case PROP_CREDENTIALS:
    /* TODO: Make sure that the credentials are no longer in use by a XMPP
     * connection object. */
    if(priv->tls_own_creds != NULL)
    {
      gnutls_certificate_free_credentials(priv->tls_own_creds);
      priv->tls_own_creds = NULL;
    }

    priv->tls_creds = g_value_get_pointer(value);
    break;
  case PROP_SASL_CONTEXT:
    /* TODO: Make sure that the Gsasl context is no longer in use by a XMPP
     * connection object. */
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
infd_xmpp_server_get_property(GObject* object,
                              guint prop_id,
                              GValue* value,
                              GParamSpec* pspec)
{
  InfdXmppServer* xmpp;
  InfdXmppServerPrivate* priv;

  xmpp = INFD_XMPP_SERVER(object);
  priv = INFD_XMPP_SERVER_PRIVATE(xmpp);

  switch(prop_id)
  {
  case PROP_STATUS:
    switch(priv->status)
    {
    case INFD_XMPP_SERVER_CLOSED:
      g_value_set_enum(value, INFD_XML_SERVER_CLOSED);
      break;
    case INFD_XMPP_SERVER_OPEN:
      g_value_set_enum(value, INFD_XML_SERVER_OPEN);
      break;
    default:
      g_assert_not_reached();
      break;
    }

    break;
  case PROP_TCP:
    g_value_set_object(value, priv->tcp);
    break;
  case PROP_LOCAL_HOSTNAME:
    g_value_set_string(value, priv->local_hostname);
    break;
  case PROP_CREDENTIALS:
    g_value_set_pointer(value, priv->tls_creds);
    break;
  case PROP_SASL_CONTEXT:
    g_value_set_pointer(value, priv->sasl_context);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infd_xmpp_server_xml_server_close(InfdXmlServer* xml)
{
  InfdXmppServerPrivate* priv;
  priv = INFD_XMPP_SERVER_PRIVATE(xml);

  g_return_if_fail(priv->status != INFD_XMPP_SERVER_CLOSED);

  switch(priv->status)
  {
  case INFD_XMPP_SERVER_OPEN:
    /* This will cause a status notify that will adjust the XMPP status */
    infd_tcp_server_close(priv->tcp);
    break;
  case INFD_XMPP_SERVER_CLOSED:
  default:
    g_assert_not_reached();
    break;
  }
}

static void
infd_xmpp_server_class_init(gpointer g_class,
                            gpointer class_data)
{
  GObjectClass* object_class;
  InfdXmppServerClass* xmpp_class;

  object_class = G_OBJECT_CLASS(g_class);
  xmpp_class = INFD_XMPP_SERVER_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfdXmppServerPrivate));

  object_class->constructor = infd_xmpp_server_constructor;
  object_class->dispose = infd_xmpp_server_dispose;
  object_class->finalize = infd_xmpp_server_finalize;
  object_class->set_property = infd_xmpp_server_set_property;
  object_class->get_property = infd_xmpp_server_get_property;

  xmpp_class->error = NULL;

  g_object_class_install_property(
    object_class,
    PROP_TCP,
    g_param_spec_object(
      "tcp-server",
      "TCP server",
      "Underlaying TCP server",
      INFD_TYPE_TCP_SERVER,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_LOCAL_HOSTNAME,
    g_param_spec_string(
      "local-hostname",
      "Local hostname",
      "Hostname of the server",
      NULL,
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
      "The GnuSASL context used for authentaction",
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_override_property(object_class, PROP_STATUS, "status");

  xmpp_server_signals[ERROR] = g_signal_new(
    "error",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfdXmppServerClass, error),
    NULL, NULL,
    inf_marshal_VOID__POINTER,
    G_TYPE_NONE,
    1,
    G_TYPE_POINTER /* actually a GError* */
  );
}

static void
infd_xmpp_server_xml_server_init(gpointer g_iface,
                                 gpointer iface_data)
{
  InfdXmlServerIface* iface;
  iface = (InfdXmlServerIface*)g_iface;

  iface->close = infd_xmpp_server_xml_server_close;
}

GType
infd_xmpp_server_get_type(void)
{
  static GType xmpp_server_type = 0;

  if(!xmpp_server_type)
  {
    static const GTypeInfo xmpp_server_type_info = {
      sizeof(InfdXmppServerClass),   /* class_size */
      NULL,                          /* base_init */
      NULL,                          /* base_finalize */
      infd_xmpp_server_class_init,   /* class_init */
      NULL,                          /* class_finalize */
      NULL,                          /* class_data */
      sizeof(InfdXmppServer),        /* instance_size */
      0,                             /* n_preallocs */
      infd_xmpp_server_init,         /* instance_init */
      NULL                           /* value_table */
    };

    static const GInterfaceInfo xml_server_info = {
      infd_xmpp_server_xml_server_init,
      NULL,
      NULL
    };

    xmpp_server_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfdXmppServer",
      &xmpp_server_type_info,
      0
    );

    g_type_add_interface_static(
      xmpp_server_type,
      INFD_TYPE_XML_SERVER,
      &xml_server_info
    );
  }

  return xmpp_server_type;
}

/**
 * infd_xmpp_server_new:
 * @tcp: A #InfdTcpServer.
 * @cred: Certificate credentials used to secure any communication.
 * @sasl_context: A SASL context used for authentication.
 *
 * Creates a new #InfdXmppServer with @tcp as underlaying TCP server object.
 * No attempt is being made to open @tcp, if it is not already open. When a
 * new connection comes in, the XMPP server creates a XMPP connection that
 * may be used to communicate with the client. Note however that the
 * resulting connection will be in status OPENING until authentication has
 * completed.
 *
 * @cred may be %NULL in which case the server creates the credentials,
 * however, this might take some time. If @sasl_context is %NULL, the server
 * uses a built-in context that only supports ANONYMOUS authentication.
 *
 * Return Value: A new #InfdXmppServer.
 **/
InfdXmppServer*
infd_xmpp_server_new(InfdTcpServer* tcp,
                     gnutls_certificate_credentials_t cred,
                     Gsasl* sasl_context)
{
  GObject* object;

  g_return_val_if_fail(INFD_IS_TCP_SERVER(tcp), NULL);

  object = g_object_new(
    INFD_TYPE_XMPP_SERVER,
    "tcp-server", tcp,
    "credentials", cred,
    "sasl-context", sasl_context,
    NULL
  );

  return INFD_XMPP_SERVER(object);
}

/* vim:set et sw=2 ts=2: */
