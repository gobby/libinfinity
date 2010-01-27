/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008, 2009 Armin Burgmeier <armin@arbur.net>
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

#include <libinfinity/server/infd-xmpp-server.h>
#include <libinfinity/server/infd-tcp-server.h>
#include <libinfinity/server/infd-xml-server.h>
#include <libinfinity/common/inf-xmpp-connection.h>
#include <libinfinity/inf-marshal.h>
#include <libinfinity/inf-signals.h>

/* Some Windows header #defines ERROR for no good */
#ifdef G_OS_WIN32
# ifdef ERROR
#  undef ERROR
# endif
#endif

typedef enum InfdXmppServerStatus_ {
  INFD_XMPP_SERVER_CLOSED,
  INFD_XMPP_SERVER_OPEN
} InfdXmppServerStatus;

typedef struct _InfdXmppServerPrivate InfdXmppServerPrivate;
struct _InfdXmppServerPrivate {
  InfdTcpServer* tcp;
  InfdXmppServerStatus status;
  gchar* local_hostname;
  InfXmppConnectionSecurityPolicy security_policy;

  InfCertificateCredentials* tls_creds;

  Gsasl* sasl_context;
  Gsasl* sasl_own_context;
  gchar* sasl_mechanisms;
};

enum {
  PROP_0,

  PROP_TCP,
  PROP_LOCAL_HOSTNAME,

  PROP_CREDENTIALS,
  PROP_SASL_CONTEXT,
  PROP_SASL_MECHANISMS,

  PROP_SECURITY_POLICY,

  /* Overridden from XML server */
  PROP_STATUS
};

enum {
  ERROR,
  CONNECTION_USER_AUTHENTICATED,

  LAST_SIGNAL
};

#define INFD_XMPP_SERVER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFD_TYPE_XMPP_SERVER, InfdXmppServerPrivate))

static GObjectClass* parent_class;
static guint xmpp_server_signals[LAST_SIGNAL];

static GError*
inf_xmpp_server_connection_user_authenticated_cb(InfXmppConnection* xmpp_conn,
                                                 Gsasl_session* sasl_session,
                                                 gpointer user_data)
{
  InfdXmppServer* xmpp_server = INFD_XMPP_SERVER(user_data);
  GError* error;

  /* simply forward the decision to the slots of our own
   * CONNECTION_USER_AUTHENTICATED signal*/
  g_signal_emit(
    G_OBJECT(xmpp_server),
    xmpp_server_signals[CONNECTION_USER_AUTHENTICATED],
    0,
    xmpp_conn,
    sasl_session,
    &error
  );

  return error;
}

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
    priv->security_policy,
    priv->tls_creds,
    priv->sasl_context,
    priv->sasl_own_context != NULL ? "ANONYMOUS" : priv->sasl_mechanisms
  );

  g_free(addr_str);

  /* We could, alternatively, keep the connection around until authentication
   * has completed and emit the new_connection signal after that, to guarantee
   * that the connection is open when new_connection is emitted. */
  infd_xml_server_new_connection(
    INFD_XML_SERVER(xmpp_server),
    INF_XML_CONNECTION(xmpp_connection)
  );

  g_signal_connect(
    G_OBJECT(xmpp_connection),
    "user-authenticated",
    G_CALLBACK(inf_xmpp_server_connection_user_authenticated_cb),
    xmpp_server
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
  case INFD_TCP_SERVER_BOUND:
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

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->tcp),
      G_CALLBACK(infd_xmpp_server_new_connection_cb),
      xmpp
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->tcp),
      G_CALLBACK(infd_xmpp_server_error_cb),
      xmpp
    );

    inf_signal_handlers_disconnect_by_func(
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
    case INFD_TCP_SERVER_BOUND:
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

/* Set own SASL context based on whether an external one is given or not */
static void
infd_xmpp_server_setup_own_sasl_context(InfdXmppServer* xmpp)
{
  InfdXmppServerPrivate* priv;
  priv = INFD_XMPP_SERVER_PRIVATE(xmpp);

  if(priv->sasl_context == NULL && priv->sasl_own_context == NULL)
  {
    /* Failure does not matter too much because every XMPP connection will
     * generate an own SASL context in this case. */
    if(gsasl_init(&priv->sasl_own_context) == GSASL_OK)
    {
      priv->sasl_context = priv->sasl_own_context;
      gsasl_callback_set(priv->sasl_context, infd_xmpp_server_sasl_cb);
      gsasl_callback_hook_set(priv->sasl_context, xmpp);
      g_object_notify(G_OBJECT(xmpp), "sasl-context");
    }
  }
  else if(priv->sasl_context != NULL && priv->sasl_own_context != NULL &&
          priv->sasl_context != priv->sasl_own_context)
  {
    /* TODO: Make sure that the Gsasl context is no longer in use by a XMPP
     * connection object. */
    gsasl_done(priv->sasl_own_context);
    priv->sasl_own_context = NULL;
  }
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
  priv->security_policy = INF_XMPP_CONNECTION_SECURITY_ONLY_UNSECURED;

  priv->tls_creds = NULL;
  priv->sasl_context = NULL;
  priv->sasl_own_context = NULL;
  priv->sasl_mechanisms = NULL;
}

static GObject*
infd_xmpp_server_constructor(GType type,
                             guint n_construct_properties,
                             GObjectConstructParam* construct_properties)
{
  InfdXmppServerPrivate* priv;
  GObject* obj;

  obj = G_OBJECT_CLASS(parent_class)->constructor(
    type,
    n_construct_properties,
    construct_properties
  );

  priv = INFD_XMPP_SERVER_PRIVATE(obj);
  g_assert(priv->tcp != NULL);

  g_assert(
    priv->security_policy == INF_XMPP_CONNECTION_SECURITY_ONLY_UNSECURED ||
    priv->tls_creds != NULL
  );

  infd_xmpp_server_setup_own_sasl_context(INFD_XMPP_SERVER(obj));
  return obj;
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
  {
    g_object_unref(G_OBJECT(priv->tcp));
    priv->tcp = NULL;
  }

  if(priv->tls_creds != NULL)
  {
    inf_certificate_credentials_unref(priv->tls_creds);
    priv->tls_creds = NULL;
  }

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

  g_free(priv->sasl_mechanisms);

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
    if(priv->tls_creds != NULL)
      inf_certificate_credentials_unref(priv->tls_creds);
    priv->tls_creds = g_value_dup_boxed(value);
    break;
  case PROP_SASL_CONTEXT:
    priv->sasl_context = g_value_get_pointer(value);
    infd_xmpp_server_setup_own_sasl_context(xmpp);
    break;
  case PROP_SASL_MECHANISMS:
    g_free(priv->sasl_mechanisms);
    priv->sasl_mechanisms = g_value_dup_string(value);
    break;
  case PROP_SECURITY_POLICY:
    infd_xmpp_server_set_security_policy(xmpp, g_value_get_enum(value));
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
    g_value_set_boxed(value, priv->tls_creds);
    break;
  case PROP_SASL_CONTEXT:
    g_value_set_pointer(value, priv->sasl_context);
    break;
  case PROP_SASL_MECHANISMS:
    g_value_set_string(value, priv->sasl_mechanisms);
    break;
  case PROP_SECURITY_POLICY:
    g_value_set_enum(value, priv->security_policy);
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

static gboolean
inf_xmpp_server_connection_user_authenticated_accumulator(
    GSignalInvocationHint* ih,
    GValue* return_accu,
    const GValue* h_return,
    gpointer data)
{
  if (g_value_get_pointer(h_return) == NULL)
  {
    /* in case this is the last call */
    g_value_set_pointer(return_accu, NULL);

    return TRUE;
  }

  g_value_copy(h_return, return_accu);
  return FALSE;
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
    g_param_spec_pointer(
      "sasl-context",
      "GnuSASL context",
      "The GnuSASL context used for authentaction",
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_SASL_MECHANISMS,
    g_param_spec_string(
      "sasl-mechanisms",
      "SASL mechanisms",
      "The SASL mechanisms offered to the client for authentication",
      NULL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_SECURITY_POLICY,
    g_param_spec_enum(
      "security-policy",
      "Security policy",
      "Whether to offer or require TLS",
      INF_TYPE_XMPP_CONNECTION_SECURITY_POLICY,
      INF_XMPP_CONNECTION_SECURITY_ONLY_UNSECURED,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT
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

  xmpp_server_signals[CONNECTION_USER_AUTHENTICATED] = g_signal_new(
    "connection-user-authenticated",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfdXmppServerClass, connection_user_authenticated),
    inf_xmpp_server_connection_user_authenticated_accumulator, NULL,
    inf_marshal_POINTER__OBJECT_POINTER,
    G_TYPE_POINTER, /* actually a GError* */
    2,
    INF_TYPE_XMPP_CONNECTION,
    G_TYPE_POINTER /* actually a Gsasl_session */
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
 * @policy: The initial security policy.
 * @creds: Certificate credentials used to secure any communication.
 * @sasl_context: A SASL context used for authentication.
 * @sasl_mechanisms: A whitespace-sparated list of SASL mechanisms.
 *
 * Creates a new #InfdXmppServer with @tcp as underlaying TCP server object.
 * No attempt is being made to open @tcp, if it is not already open. When a
 * new connection comes in, the XMPP server creates a XMPP connection that
 * may be used to communicate with the client. Note however that the
 * resulting connection will be in status OPENING until authentication has
 * completed.
 *
 * If @policy is %INF_XMPP_CONNECTION_SECURITY_ONLY_UNSECURED, then @creds may
 * be %NULL. If @creds is non-%NULL nevertheless, then it is possible to change
 * the security policy later using infd_xmpp_server_set_security_policy().
 * @creds can also be changed later while the server is running. So just set
 * valid credentials before changing @policy to allow TLS.
 *
 * If @sasl_context is %NULL, the server uses a built-in context that only
 * supports ANONYMOUS authentication. If @sasl_context is not %NULL, then
 * @sasl_mechanisms specifies the mechanisms offered to clients. If
 * @sasl_mechanisms is %NULL, then all available mechanims will be offered.
 * If @sasl_context is %NULL, then this parameter is ignored.
 *
 * Return Value: A new #InfdXmppServer.
 **/
InfdXmppServer*
infd_xmpp_server_new(InfdTcpServer* tcp,
                     InfXmppConnectionSecurityPolicy policy,
                     InfCertificateCredentials* creds,
                     Gsasl* sasl_context,
                     const gchar* sasl_mechanisms)
{
  GObject* object;

  g_return_val_if_fail(INFD_IS_TCP_SERVER(tcp), NULL);

  g_return_val_if_fail(
    policy == INF_XMPP_CONNECTION_SECURITY_ONLY_UNSECURED || creds != NULL,
    NULL
  );

  object = g_object_new(
    INFD_TYPE_XMPP_SERVER,
    "tcp-server", tcp,
    "credentials", creds,
    "security-policy", policy,
    "sasl-context", sasl_context,
    "sasl-mechanisms", sasl_mechanisms,
    NULL
  );

  return INFD_XMPP_SERVER(object);
}

/**
 * infd_xmpp_server_set_security_policy:
 * @server: A #InfdXmppServer.
 * @policy: The new security policy.
 *
 * Sets the security policy for newly accepted #InfXmppConnection<!-- -->s.
 * Does not already established connections.
 */
void
infd_xmpp_server_set_security_policy(InfdXmppServer* server,
                                     InfXmppConnectionSecurityPolicy policy)
{
  InfdXmppServerPrivate* priv;

  g_return_if_fail(INFD_IS_XMPP_SERVER(server));

  priv = INFD_XMPP_SERVER_PRIVATE(server);

  if(policy != priv->security_policy)
  {
    g_return_if_fail(
      policy == INF_XMPP_CONNECTION_SECURITY_ONLY_UNSECURED ||
      priv->tls_creds != NULL
    );

    priv->security_policy = policy;
    g_object_notify(G_OBJECT(server), "security-policy");
  }
}

/**
 * infd_xmpp_server_get_security_policy:
 * @server: A #InfdXmppServer.
 *
 * Returns the current security policy for newly accepted
 * #InfXmppConnection<!-- -->s.
 *
 * Returns: The current security policy.
 */
InfXmppConnectionSecurityPolicy
infd_xmpp_server_get_security_policy(InfdXmppServer* server)
{
  g_return_val_if_fail(
    INFD_IS_XMPP_SERVER(server),
    INF_XMPP_CONNECTION_SECURITY_BOTH_PREFER_TLS
  );

  return INFD_XMPP_SERVER_PRIVATE(server)->security_policy;
}

/* vim:set et sw=2 ts=2: */
