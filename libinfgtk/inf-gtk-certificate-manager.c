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
 * SECTION:inf-gtk-certificate-manager
 * @title: InfGtkCertificateManager
 * @short_description: Verify server certificates and warn the user
 * @include: libinfgtk/inf-gtk-certificate-manager.h
 * @stability: Unstable
 *
 * #InfGtkCertificateManager is derived from #InfCertificateVerify, and
 * as such it verifies server certificates. This class handles the
 * #InfCertificateVerify::check-certificate signal by showing a
 * #InfGtkCertificateDialog to the user to let them decide whether or not
 * to accept the server's certificate.
 **/

#include <libinfgtk/inf-gtk-certificate-manager.h>
#include <libinfgtk/inf-gtk-certificate-dialog.h>

#include <libinfinity/inf-i18n.h>

typedef struct _InfGtkCertificateManagerDialog InfGtkCertificateManagerDialog;
struct _InfGtkCertificateManagerDialog {
  InfGtkCertificateManager* manager;
  InfXmppConnection* connection;
  InfGtkCertificateDialog* dialog;
};

typedef struct _InfGtkCertificateManagerPrivate
  InfGtkCertificateManagerPrivate;
struct _InfGtkCertificateManagerPrivate {
  GtkWindow* parent_window;
  GSList* dialogs;
};

enum {
  PROP_0,

  PROP_PARENT_WINDOW
};

#define INF_GTK_CERTIFICATE_MANAGER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_GTK_TYPE_CERTIFICATE_MANAGER, InfGtkCertificateManagerPrivate))

G_DEFINE_TYPE_WITH_CODE(InfGtkCertificateManager, inf_gtk_certificate_manager, INF_TYPE_CERTIFICATE_VERIFY,
  G_ADD_PRIVATE(InfGtkCertificateManager))

static void
inf_gtk_certificate_manager_dialog_free(InfGtkCertificateManagerDialog* item)
{
  gtk_widget_destroy(GTK_WIDGET(item->dialog));
  g_slice_free(InfGtkCertificateManagerDialog, item);
}

static void
inf_gtk_certificate_manager_response_cb(GtkDialog* dialog,
                                        int response,
                                        gpointer user_data)
{
  InfGtkCertificateManagerDialog* item;
  InfGtkCertificateManager* manager;
  InfGtkCertificateManagerPrivate* priv;
  InfXmppConnection* connection;

  item = (InfGtkCertificateManagerDialog*)user_data;
  manager = INF_GTK_CERTIFICATE_MANAGER(item->manager);
  priv = INF_GTK_CERTIFICATE_MANAGER_PRIVATE(manager);

  connection = item->connection;

  switch(response)
  {
  case GTK_RESPONSE_ACCEPT:
    inf_certificate_verify_checked(
      INF_CERTIFICATE_VERIFY(manager),
      connection,
      TRUE
    );

    break;
  case GTK_RESPONSE_REJECT:
  case GTK_RESPONSE_DELETE_EVENT:
    inf_certificate_verify_checked(
      INF_CERTIFICATE_VERIFY(manager),
      connection,
      FALSE
    );

    break;
  default:
    g_assert_not_reached();
    break;
  }

  inf_gtk_certificate_manager_dialog_free(item);
  priv->dialogs = g_slist_remove(priv->dialogs, item);
}

static void
inf_gtk_certificate_manager_init(InfGtkCertificateManager* manager)
{
  InfGtkCertificateManagerPrivate* priv;
  priv = INF_GTK_CERTIFICATE_MANAGER_PRIVATE(manager);

  priv->parent_window = NULL;
  priv->dialogs = NULL;
}

static void
inf_gtk_certificate_manager_dispose(GObject* object)
{
  InfGtkCertificateManager* manager;
  InfGtkCertificateManagerPrivate* priv;

  manager = INF_GTK_CERTIFICATE_MANAGER(object);
  priv = INF_GTK_CERTIFICATE_MANAGER_PRIVATE(manager);

  if(priv->parent_window != NULL)
  {
    g_object_unref(priv->parent_window);
    priv->parent_window = NULL;
  }

  while(priv->dialogs != NULL)
  {
    inf_gtk_certificate_manager_dialog_free(priv->dialogs->data);
    priv->dialogs = g_slist_remove(priv->dialogs, priv->dialogs->data);
  }

  G_OBJECT_CLASS(inf_gtk_certificate_manager_parent_class)->dispose(object);
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
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_gtk_certificate_manager_check_certificate(
  InfCertificateVerify* verify,
  InfXmppConnection* connection,
  InfCertificateChain* certificate_chain,
  gnutls_x509_crt_t pinned_certificate,
  InfCertificateVerifyFlags flags)
{
  InfGtkCertificateManager* manager;
  InfGtkCertificateManagerPrivate* priv;
  InfGtkCertificateManagerDialog* item;
  InfGtkCertificateDialog* dialog;
  gchar* hostname;
  gchar* text;

  GtkWidget* label;
  GtkWidget* vbox;

  manager = INF_GTK_CERTIFICATE_MANAGER(verify);
  priv = INF_GTK_CERTIFICATE_MANAGER_PRIVATE(manager);

  g_object_get(G_OBJECT(connection), "remote-hostname", &hostname, NULL);

  dialog = inf_gtk_certificate_dialog_new(
    priv->parent_window,
    0,
    flags,
    hostname,
    certificate_chain,
    pinned_certificate
  );

  gtk_dialog_add_button(
    GTK_DIALOG(dialog),
    _("_Cancel connection"),
    GTK_RESPONSE_REJECT
  );

  gtk_dialog_add_button(
    GTK_DIALOG(dialog),
    _("C_ontinue connection"),
    GTK_RESPONSE_ACCEPT
  );

  text = g_strdup_printf(
    _("Do you want to continue the connection to host \"%s\"? If you "
      "choose to continue, this certificate will be trusted in the "
      "future when connecting to this host."),
    hostname
  );

  g_free(hostname);

  label = gtk_label_new(text);
  gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
  gtk_label_set_max_width_chars(GTK_LABEL(label), 75);
  gtk_widget_show(label);
  g_free(text);

  vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, TRUE, 6);

  item = g_slice_new(InfGtkCertificateManagerDialog);
  item->manager = manager;
  item->connection = connection;
  item->dialog = dialog;
  priv->dialogs = g_slist_prepend(priv->dialogs, item);

  g_signal_connect(
    G_OBJECT(dialog),
    "response",
    G_CALLBACK(inf_gtk_certificate_manager_response_cb),
    item
  );

  gtk_window_present(GTK_WINDOW(dialog));
}

static void
inf_gtk_certificate_manager_check_cancelled(InfCertificateVerify* verify,
                                            InfXmppConnection* connection)
{
  InfGtkCertificateManager* manager;
  InfGtkCertificateManagerPrivate* priv;
  InfGtkCertificateManagerDialog* item;
  GSList* iter;

  manager = INF_GTK_CERTIFICATE_MANAGER(verify);
  priv = INF_GTK_CERTIFICATE_MANAGER_PRIVATE(manager);

  for(iter = priv->dialogs; iter != NULL; iter = iter->next)
  {
    item = (InfGtkCertificateManagerDialog*)iter->data;
    if(item->connection == connection)
      break;
  }

  g_assert(iter != NULL);

  inf_gtk_certificate_manager_dialog_free(item);
  priv->dialogs = g_slist_remove(priv->dialogs, item);
}

/*
 * GType registration
 */

static void
inf_gtk_certificate_manager_class_init(
  InfGtkCertificateManagerClass* certificate_manager_class)
{
  GObjectClass* object_class;
  InfCertificateVerifyClass* certificate_verify_class;

  object_class = G_OBJECT_CLASS(certificate_manager_class);
  certificate_verify_class =
    INF_CERTIFICATE_VERIFY_CLASS(certificate_manager_class);

  object_class->dispose = inf_gtk_certificate_manager_dispose;
  object_class->set_property = inf_gtk_certificate_manager_set_property;
  object_class->get_property = inf_gtk_certificate_manager_get_property;
  
  certificate_verify_class->check_certificate =
    inf_gtk_certificate_manager_check_certificate;
  certificate_verify_class->check_cancelled =
    inf_gtk_certificate_manager_check_cancelled;

  g_object_class_install_property(
    object_class,
    PROP_PARENT_WINDOW,
    g_param_spec_object(
      "parent-window",
      "Parent window",
      "The parent window for certificate verification dialogs",
      GTK_TYPE_WINDOW,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );
}

/*
 * Public API.
 */

/**
 * inf_gtk_certificate_manager_new: (constructor)
 * @parent_window: The #GtkWindow to which to make certificate verification
 * dialogs transient to.
 * @xmpp_manager: The #InfXmppManager whose #InfXmppConnection<!-- -->s to
 * manage the certificates for.
 * @known_hosts_file: (type filename) (allow-none): Path pointing to a file
 * that contains pinned certificates, or %NULL.
 *
 * Creates a new #InfGtkCertificateManager. See #InfCertificateVerify for
 * details about the certificate verification process.
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
