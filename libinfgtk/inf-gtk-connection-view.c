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
 * SECTION:inf-gtk-connection-view
 * @title: InfGtkConnectionView
 * @short_description: A widget showing connection parameters
 * @include: libinfgtk/inf-gtk-connection-view.h
 * @stability: Unstable
 *
 * #InfGtkConnectionView is a widget that shows parameters for a given
 * connection such as the name of the remote host, its IP address, and
 * encryption information.
 **/

#include <libinfgtk/inf-gtk-connection-view.h>
#include <libinfgtk/inf-gtk-certificate-view.h>

#include <libinfinity/common/inf-cert-util.h>

#include <libinfinity/inf-i18n.h>

#include <gnutls/x509.h>

typedef struct _InfGtkConnectionViewPrivate InfGtkConnectionViewPrivate;
struct _InfGtkConnectionViewPrivate {
  InfXmppConnection* connection;

  GtkWidget* remote_hostname;
  GtkWidget* remote_ipaddress;
  GtkWidget* local_ipaddress;

  GtkWidget* tls_version;
  GtkWidget* cipher_suite;
  GtkWidget* dh_prime_bits;

  /* TODO: This box is duplicated with InfGtkCertificateDialog, we should
   * de-duplicate it. */
  GtkTreeStore* certificate_store;
  GtkWidget* certificate_expander;
  GtkWidget* certificate_tree_view;
  GtkWidget* certificate_info_view;
  GtkCellRenderer* text_renderer;
};

enum {
  PROP_0,

  PROP_CONNECTION
};

#define INF_GTK_CONNECTION_VIEW_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_GTK_TYPE_CONNECTION_VIEW, InfGtkConnectionViewPrivate))

G_DEFINE_TYPE_WITH_CODE(InfGtkConnectionView, inf_gtk_connection_view, GTK_TYPE_GRID,
  G_ADD_PRIVATE(InfGtkConnectionView))

static gchar*
inf_gtk_connection_view_format_ipaddress(InfIpAddress* address,
                                         guint port)
{
  gchar* out;
  gchar* str;

  str = inf_ip_address_to_string(address);

  switch(inf_ip_address_get_family(address))
  {
  case INF_IP_ADDRESS_IPV4:
    out = g_strdup_printf("%s:%u", str, port);
    break;
  case INF_IP_ADDRESS_IPV6:
    out = g_strdup_printf("[%s]:%u", str, port);
    break;
  default:
    g_assert_not_reached();
    out = NULL;
    break;
  }

  g_free(str);
  return out;
}

static void
inf_gtk_connection_view_set_chain(InfGtkConnectionView* view,
                                  InfCertificateChain* chain)
{
  InfGtkConnectionViewPrivate* priv;
  guint i;
  gnutls_x509_crt_t crt;
  GtkTreeIter prev_row;
  GtkTreeIter new_row;
  GtkTreeIter* parent;
  GtkTreePath* path;

  priv = INF_GTK_CONNECTION_VIEW_PRIVATE(view);

  gtk_tree_store_clear(priv->certificate_store);
  inf_gtk_certificate_view_set_certificate(
    INF_GTK_CERTIFICATE_VIEW(priv->certificate_info_view),
    NULL
  );

  parent = NULL;
  if(chain != NULL)
  {
    for(i = inf_certificate_chain_get_n_certificates(chain); i > 0; -- i)
    {
      crt = inf_certificate_chain_get_nth_certificate(chain, i - 1);
      gtk_tree_store_append(priv->certificate_store, &new_row, parent);
      gtk_tree_store_set(priv->certificate_store, &new_row, 0, crt, -1);

      prev_row = new_row;
      parent = &prev_row;
    }

    path = gtk_tree_model_get_path(
      GTK_TREE_MODEL(priv->certificate_store),
      &new_row
    );

    gtk_tree_view_expand_to_path(
      GTK_TREE_VIEW(priv->certificate_tree_view),
      path
    );

    gtk_tree_selection_select_path(
      gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->certificate_tree_view)),
      path
    );

    gtk_tree_view_scroll_to_cell(
      GTK_TREE_VIEW(priv->certificate_tree_view),
      path,
      NULL,
      FALSE,
      0.0,
      0.0
    );

    gtk_tree_path_free(path);
    gtk_widget_show(priv->certificate_expander);
  }
  else
  {
    gtk_widget_hide(priv->certificate_expander);
  }
}

static void
inf_gtk_connection_view_selection_changed_cb(GtkTreeSelection* selection,
                                             gpointer user_data)
{
  InfGtkConnectionView* view;
  InfGtkConnectionViewPrivate* priv;
  GtkTreeIter iter;
  gnutls_x509_crt_t cert;

  view = INF_GTK_CONNECTION_VIEW(user_data);
  priv = INF_GTK_CONNECTION_VIEW_PRIVATE(view);

  if(gtk_tree_selection_get_selected(selection, NULL, &iter))
  {
    gtk_tree_model_get(
      GTK_TREE_MODEL(priv->certificate_store),
      &iter,
      0, &cert,
      -1
    );

    inf_gtk_certificate_view_set_certificate(
      INF_GTK_CERTIFICATE_VIEW(priv->certificate_info_view),
      cert
    );
  }
  else
  {
    inf_gtk_certificate_view_set_certificate(
      INF_GTK_CERTIFICATE_VIEW(priv->certificate_info_view),
      NULL
    );
  }
}

static void
inf_gtk_connection_view_chain_data_func(GtkTreeViewColumn* column,
                                        GtkCellRenderer* renderer,
                                        GtkTreeModel* tree_model,
                                        GtkTreeIter* iter,
                                        gpointer user_data)
{
  gpointer crt_ptr;
  gnutls_x509_crt_t cert;
  GValue value = { 0 };
  gchar* common_name;

  gtk_tree_model_get(tree_model, iter, 0, &crt_ptr, -1);
  cert = (gnutls_x509_crt_t)crt_ptr;

  common_name =
    inf_cert_util_get_dn_by_oid(cert, GNUTLS_OID_X520_COMMON_NAME, 0);

  g_value_init(&value, G_TYPE_STRING);

  if(common_name != NULL)
    g_value_take_string(&value, common_name);
  else
    g_value_set_static_string(&value, _("<Unknown Certificate Holder>"));

  g_object_set_property(G_OBJECT(renderer), "text", &value);
  g_value_unset(&value);
}

static void
inf_gtk_connection_view_init(InfGtkConnectionView* view)
{
  InfGtkConnectionViewPrivate* priv;
  priv = INF_GTK_CONNECTION_VIEW_PRIVATE(view);

  priv->connection = NULL;

  gtk_widget_init_template(GTK_WIDGET(view));

  gtk_tree_selection_set_mode(
    gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->certificate_tree_view)),
    GTK_SELECTION_BROWSE
  );

  gtk_tree_view_column_set_cell_data_func(
    gtk_tree_view_get_column(GTK_TREE_VIEW(priv->certificate_tree_view), 0),
    priv->text_renderer,
    inf_gtk_connection_view_chain_data_func,
    NULL,
    NULL
  );
}

static void
inf_gtk_connection_view_set_property(GObject* object,
                                     guint prop_id,
                                     const GValue* value,
                                     GParamSpec* pspec)
{
  InfGtkConnectionView* view;
  InfGtkConnectionViewPrivate* priv;

  view = INF_GTK_CONNECTION_VIEW(object);
  priv = INF_GTK_CONNECTION_VIEW_PRIVATE(view);

  switch(prop_id)
  {
  case PROP_CONNECTION:
    inf_gtk_connection_view_set_connection(view, INF_XMPP_CONNECTION(value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_gtk_connection_view_get_property(GObject* object,
                                     guint prop_id,
                                     GValue* value,
                                     GParamSpec* pspec)
{
  InfGtkConnectionView* view;
  InfGtkConnectionViewPrivate* priv;

  view = INF_GTK_CONNECTION_VIEW(object);
  priv = INF_GTK_CONNECTION_VIEW_PRIVATE(view);

  switch(prop_id)
  {
  case PROP_CONNECTION:
    g_value_set_object(value, priv->connection);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_gtk_connection_view_dispose(GObject* object)
{
  InfGtkConnectionView* view;
  InfGtkConnectionViewPrivate* priv;

  view = INF_GTK_CONNECTION_VIEW(object);
  priv = INF_GTK_CONNECTION_VIEW_PRIVATE(view);

  if(priv->connection != NULL)
    inf_gtk_connection_view_set_connection(view, NULL);

  G_OBJECT_CLASS(inf_gtk_connection_view_parent_class)->dispose(object);
}

/*
 * GType registration
 */

static void
inf_gtk_connection_view_class_init(
  InfGtkConnectionViewClass* connection_view_class)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(connection_view_class);

  object_class->set_property = inf_gtk_connection_view_set_property;
  object_class->get_property = inf_gtk_connection_view_get_property;
  object_class->dispose = inf_gtk_connection_view_dispose;

  gtk_widget_class_set_template_from_resource(
    GTK_WIDGET_CLASS(connection_view_class),
    "/de/0x539/libinfgtk/ui/infgtkconnectionview.ui"
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(object_class),
    InfGtkConnectionView,
    certificate_store
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(connection_view_class),
    InfGtkConnectionView,
    remote_hostname
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(connection_view_class),
    InfGtkConnectionView,
    remote_ipaddress
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(connection_view_class),
    InfGtkConnectionView,
    local_ipaddress
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(connection_view_class),
    InfGtkConnectionView,
    tls_version
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(connection_view_class),
    InfGtkConnectionView,
    cipher_suite
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(connection_view_class),
    InfGtkConnectionView,
    dh_prime_bits
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(object_class),
    InfGtkConnectionView,
    certificate_expander
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(object_class),
    InfGtkConnectionView,
    certificate_tree_view
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(object_class),
    InfGtkConnectionView,
    certificate_info_view
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(object_class),
    InfGtkConnectionView,
    text_renderer
  );

  gtk_widget_class_bind_template_callback(
    GTK_WIDGET_CLASS(object_class),
    inf_gtk_connection_view_selection_changed_cb
  );

  g_object_class_install_property(
    object_class,
    PROP_CONNECTION,
    g_param_spec_object(
      "connection",
      "Connection",
      "Connection for which to show parameters",
      INF_TYPE_XMPP_CONNECTION,
      G_PARAM_READWRITE
    )
  );
}

/*
 * Public API.
 */

/**
 * inf_gtk_connection_view_new: (constructor)
 *
 * Creates a new #InfGtkConnectionView. To show a connection, use
 * inf_gtk_connection_view_set_connection() on the returned widget.
 *
 * Returns: (transfer floating): A new #InfGtkConnectionView.
 */
GtkWidget*
inf_gtk_connection_view_new(void)
{
  GObject* object;
  object = g_object_new(INF_GTK_TYPE_CONNECTION_VIEW, NULL);
  return GTK_WIDGET(object);
}

/**
 * inf_gtk_connection_view_new_with_connection: (constructor)
 * @connection: The connection to show.
 *
 * Creates a new #InfGtkConnectionView showing the given connection. This
 * is the same as creating a new connection view and calling
 * inf_gtk_connection_view_set_connection() afterwards.
 *
 * Returns: (transfer floating): A new #InfGtkConnectionView.
 */
GtkWidget*
inf_gtk_connection_view_new_with_connection(InfXmppConnection* connection)
{
  GObject* object;
  
  g_return_val_if_fail(
    connection == NULL || INF_IS_XMPP_CONNECTION(connection),
    NULL
  );

  object = g_object_new(
    INF_GTK_TYPE_CONNECTION_VIEW,
    "connection", connection,
    NULL
  );

  return GTK_WIDGET(object);
}

/**
 * inf_gtk_connection_view_set_connection:
 * @view: A #InfGtkConnectionView.
 * @connection: The connection to show.
 *
 * Shows the given connection in @view.
 */
void
inf_gtk_connection_view_set_connection(InfGtkConnectionView* view,
                                       InfXmppConnection* connection)
{
  InfGtkConnectionViewPrivate* priv;
  InfTcpConnection* tcp;

  InfIpAddress* remote_address;
  guint remote_port;
  InfIpAddress* local_address;
  guint local_port;
  gchar* text;

  const gchar* cs;
  gnutls_kx_algorithm_t kx;
  gnutls_cipher_algorithm_t cipher;
  gnutls_mac_algorithm_t mac;
  gnutls_protocol_t ver;
  guint dh_prime_bits;

  g_return_if_fail(INF_GTK_IS_CONNECTION_VIEW(view));
  g_return_if_fail(connection == NULL || INF_IS_XMPP_CONNECTION(connection));

  priv = INF_GTK_CONNECTION_VIEW_PRIVATE(view);
  
  if(priv->connection != NULL)
    g_object_unref(priv->connection);

  priv->connection = connection;

  if(connection != NULL)
    g_object_ref(connection);

  if(connection == NULL)
  {
    gtk_label_set_text(GTK_LABEL(priv->remote_hostname), NULL);
    gtk_label_set_text(GTK_LABEL(priv->remote_ipaddress), NULL);
    gtk_label_set_text(GTK_LABEL(priv->local_ipaddress), NULL);

    gtk_label_set_text(GTK_LABEL(priv->tls_version), NULL);
    gtk_label_set_text(GTK_LABEL(priv->cipher_suite), NULL);
    gtk_label_set_text(GTK_LABEL(priv->dh_prime_bits), NULL);

    inf_gtk_connection_view_set_chain(view, NULL);
  }
  else
  {
    g_object_get(G_OBJECT(connection), "remote-hostname", &text, NULL);

    if(text != NULL)
    {
      gtk_label_set_text(GTK_LABEL(priv->remote_hostname), text);
    }
    else
    {
      text = g_markup_printf_escaped("<i>%s</i>", _("Unknown"));
      gtk_label_set_markup(GTK_LABEL(priv->remote_hostname), text);
    }

    g_free(text);
    
    g_object_get(G_OBJECT(connection), "tcp-connection", &tcp, NULL);
    g_object_get(
      G_OBJECT(tcp),
      "remote-address", &remote_address,
      "remote-port", &remote_port,
      "local-address", &local_address,
      "local-port", &local_port,
      NULL
    );

    g_object_unref(tcp);

    text = inf_gtk_connection_view_format_ipaddress(
      remote_address,
      remote_port
    );
    
    gtk_label_set_text(GTK_LABEL(priv->remote_ipaddress), text);
    g_free(text);

    text = inf_gtk_connection_view_format_ipaddress(
      local_address,
      local_port
    );

    gtk_label_set_text(GTK_LABEL(priv->local_ipaddress), text);
    g_free(text);

    inf_ip_address_free(remote_address);
    inf_ip_address_free(local_address);

    if(inf_xmpp_connection_get_tls_enabled(connection))
    {
      kx = inf_xmpp_connection_get_kx_algorithm(connection);
      cipher = inf_xmpp_connection_get_cipher_algorithm(connection);
      mac = inf_xmpp_connection_get_mac_algorithm(connection);
      ver = inf_xmpp_connection_get_tls_protocol(connection);
      dh_prime_bits = inf_xmpp_connection_get_dh_prime_bits(connection);

      gtk_label_set_text(
        GTK_LABEL(priv->tls_version),
        gnutls_protocol_get_name(ver)
      );

      cs = gnutls_cipher_suite_get_name(kx, cipher, mac);
      if(ver == GNUTLS_SSL3)
        text = g_strdup_printf("SSL_%s", cs);
      else
        text = g_strdup_printf("TLS_%s", cs);
      gtk_label_set_text(GTK_LABEL(priv->cipher_suite), text);
      g_free(text);

      if(dh_prime_bits > 0)
      {
        text = g_markup_printf_escaped("%u bit", dh_prime_bits);
        gtk_label_set_text(GTK_LABEL(priv->dh_prime_bits), text);
      }
      else
      {
        text = g_markup_printf_escaped("<i>%s</i>", _("N/A"));
        gtk_label_set_markup(GTK_LABEL(priv->dh_prime_bits), text);
      }

      g_free(text);

      inf_gtk_connection_view_set_chain(
        view,
        inf_xmpp_connection_get_peer_certificate(connection)
      );
    }
    else
    {
      text = g_markup_printf_escaped("<i>%s</i>", _("No Encryption"));
      gtk_label_set_markup(GTK_LABEL(priv->tls_version), text);
      g_free(text);

      text = g_markup_printf_escaped("<i>%s</i>", _("N/A"));
      gtk_label_set_text(GTK_LABEL(priv->cipher_suite), text);
      gtk_label_set_markup(GTK_LABEL(priv->dh_prime_bits), text);
      g_free(text);

      inf_gtk_connection_view_set_chain(view, NULL);
    }
  }

  g_object_notify(G_OBJECT(view), "connection");
}

/* vim:set et sw=2 ts=2: */
