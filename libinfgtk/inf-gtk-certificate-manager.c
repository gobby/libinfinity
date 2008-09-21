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

/* TODO: Put the non-GUI-relevant parts of this code into libinfinity */
/* TODO: Support CRLs */

#include <libinfgtk/inf-gtk-certificate-manager.h>
#include <libinfgtk/inf-gtk-certificate-dialog.h>

#include <libinfinity/common/inf-xml-connection.h>
#include <libinfinity/common/inf-cert-util.h>
#include <libinfinity/inf-i18n.h>

#include <gnutls/x509.h>

#include <string.h>
#include <errno.h>

typedef struct _InfGtkCertificateManagerQuery InfGtkCertificateManagerQuery;
struct _InfGtkCertificateManagerQuery {
  InfGtkCertificateManager* manager;
  InfXmppConnection* connection;
  InfGtkCertificateDialog* dialog;
  GtkWidget* checkbutton;
  gnutls_x509_crt_t old_certificate; /* points into known_hosts array */
  InfCertificateChain* certificate_chain;
};

typedef struct _InfGtkCertificateManagerPrivate InfGtkCertificateManagerPrivate;
struct _InfGtkCertificateManagerPrivate {
  GtkWindow* parent_window;
  InfXmppManager* xmpp_manager;

  gchar* trust_file;
  gchar* known_hosts_file;

  GPtrArray* ca_certs;
  GPtrArray* known_hosts;

  GSList* queries;
};

enum {
  PROP_0,

  PROP_PARENT_WINDOW,
  PROP_XMPP_MANAGER,

  PROP_TRUST_FILE,
  PROP_KNOWN_HOSTS_FILE
};

#define INF_GTK_CERTIFICATE_MANAGER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_GTK_TYPE_CERTIFICATE_MANAGER, InfGtkCertificateManagerPrivate))

static GObjectClass* parent_class;

#if 0
static InfGtkCertificateManagerQuery*
inf_gtk_certificate_manager_find_query(InfGtkCertificateManager* manager,
                                       InfXmppConnection* connection)
{
  InfGtkCertificateManagerPrivate* priv;
  GSList* item;
  InfGtkCertificateManagerQuery* query;

  priv = INF_GTK_CERTIFICATE_MANAGER_PRIVATE(manager);
  for(item = priv->queries; item != NULL; item == item->next)
  {
    query = (InfGtkCertificateManagerQuery*)item->data;
    if(query->connection == connection)
      return query;
  }

  return NULL;
}
#endif

static void
inf_gtk_certificate_manager_notify_status_cb(GObject* object,
                                             GParamSpec* pspec,
                                             gpointer user_data);

static void
inf_gtk_certificate_manager_query_free(InfGtkCertificateManagerQuery* query)
{
  g_signal_handlers_disconnect_by_func(
    G_OBJECT(query->connection),
    G_CALLBACK(inf_gtk_certificate_manager_notify_status_cb),
    query
  );

  g_object_unref(query->connection);
  inf_certificate_chain_unref(query->certificate_chain);
  gtk_widget_destroy(GTK_WIDGET(query->dialog));
  g_slice_free(InfGtkCertificateManagerQuery, query);
}

static void
inf_gtk_certificate_manager_response_cb(GtkDialog* dialog,
                                        int response_id,
                                        gpointer user_data)
{
  InfGtkCertificateManagerQuery* query;
  InfGtkCertificateManagerPrivate* priv;
  InfGtkCertificateDialogFlags flags;

  GSList* item;
  InfGtkCertificateManagerQuery* other_query;
  guint i;

  gnutls_x509_crt_t own;
  gnutls_x509_crt_t copied_own;
  InfXmppConnection* connection;

  query = (InfGtkCertificateManagerQuery*)user_data;
  priv = INF_GTK_CERTIFICATE_MANAGER_PRIVATE(query->manager);
  g_object_get(G_OBJECT(dialog), "certificate-flags", &flags, NULL);

  /* If the certificate changed, and we shall remember the answer, then we
   * remove the old certificate from the known hosts file. If the user
   * accepted the connection, then we will add the new certificate below. */
  if(flags & INF_GTK_CERTIFICATE_DIALOG_CERT_CHANGED)
  {
    /* TODO: Should we always do this, independant of the
     * checkbutton setting? */
    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(query->checkbutton)))
    {
      /* Make sure the certificate is not currently in use by
       * another dialog. */
      for(item = priv->queries; item != NULL; item = g_slist_next(item))
      {
        other_query = (InfGtkCertificateManagerQuery*)item->data;
        if(query != other_query)
          if(query->old_certificate == other_query->old_certificate)
            break;
      }

      if(item == NULL)
      {
        for(i = 0; i < priv->known_hosts->len; ++ i)
        {
          if(g_ptr_array_index(priv->known_hosts, i) ==
             query->old_certificate)
          {
            gnutls_x509_crt_deinit(query->old_certificate);
            g_ptr_array_remove_index_fast(priv->known_hosts, i);
            break;
          }
        }
      } 

      query->old_certificate = NULL;
    }
  }

  own = inf_certificate_chain_get_own_certificate(query->certificate_chain);
  connection = query->connection;
  g_object_ref(connection);

  switch(response_id)
  {
  case GTK_RESPONSE_ACCEPT:
    if( (flags & INF_GTK_CERTIFICATE_DIALOG_CERT_CHANGED) ||
        (flags & INF_GTK_CERTIFICATE_DIALOG_CERT_ISSUER_NOT_TRUSTED))
    {
      if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(query->checkbutton)))
      {
        /* We should not add the same certificate twice, meaning we don't
         * already have a certificate for this host. If we had an older one,
         * then it should have been removed above. */
        g_assert(query->old_certificate == NULL);

        if(inf_cert_util_copy(&copied_own, own) == GNUTLS_E_SUCCESS)
          g_ptr_array_add(priv->known_hosts, copied_own);
      }
    }

    /* We do this before calling verify_continue since this could cause a
     * status notify, in which our signal handler would already remove the
     * query. We would then try to free it again at the end of this call. */
    priv->queries = g_slist_remove(priv->queries, query);
    inf_gtk_certificate_manager_query_free(query);

    inf_xmpp_connection_certificate_verify_continue(connection);
    break;
  case GTK_RESPONSE_REJECT:
  case GTK_RESPONSE_DELETE_EVENT:
    if( (flags & INF_GTK_CERTIFICATE_DIALOG_CERT_CHANGED) ||
        (flags & INF_GTK_CERTIFICATE_DIALOG_CERT_ISSUER_NOT_TRUSTED))
    {
      /* TODO: Remember that the connection was rejected if the checkbutton
       * is active. */
    }

    /* We do this before calling verify_cancel since this could cause a
     * status notify, in which our signal handler would already remove the
     * query. We would then try to free it again at the end of this call. */
    priv->queries = g_slist_remove(priv->queries, query);
    inf_gtk_certificate_manager_query_free(query);

    inf_xmpp_connection_certificate_verify_cancel(connection);
    break;
  default:
    g_assert_not_reached();
    break;
  }

  g_object_unref(connection);
}

static void
inf_gtk_certificate_manager_notify_status_cb(GObject* object,
                                             GParamSpec* pspec,
                                             gpointer user_data)
{
  InfGtkCertificateManagerQuery* query;
  InfGtkCertificateManagerPrivate* priv;
  InfXmppConnection* connection;
  InfXmlConnectionStatus status;

  query = (InfGtkCertificateManagerQuery*)user_data;
  priv = INF_GTK_CERTIFICATE_MANAGER_PRIVATE(query->manager);
  connection = INF_XMPP_CONNECTION(object);

  g_object_get(G_OBJECT(connection), "status", &status, NULL);

  if(status == INF_XML_CONNECTION_CLOSING ||
     status == INF_XML_CONNECTION_CLOSED)
  {
    priv->queries = g_slist_remove(priv->queries, query);
    inf_gtk_certificate_manager_query_free(query);
  }
}

static void
inf_gtk_certificate_manager_free_certificate_array(GPtrArray* array)
{
  guint i;
  for(i = 0; i < array->len; ++ i)
    gnutls_x509_crt_deinit((gnutls_x509_crt_t)g_ptr_array_index(array, i));
  g_ptr_array_free(array, TRUE);
}

static void
inf_gtk_certificate_manager_set_known_hosts(InfGtkCertificateManager* manager,
                                            const gchar* known_hosts_file)
{
  InfGtkCertificateManagerPrivate* priv;
  gchar* path;
  int ret;
  GError* error;
  int save_errno;

  priv = INF_GTK_CERTIFICATE_MANAGER_PRIVATE(manager);

  if(priv->known_hosts_file != NULL)
  {
    if(priv->known_hosts != NULL)
    {
      path = g_path_get_dirname(priv->known_hosts_file);
      ret = g_mkdir_with_parents(path, 0700);
      save_errno = errno;
      g_free(path);

      if(ret != 0)
      {
        /* TODO_Win32: Is the error code also in errno on Windows? */
        g_warning(
          _("Failed to save known hosts file: %s\n"),
          strerror(save_errno)
        );
      }
      else
      {
        error = NULL;
        ret = inf_cert_util_save_file(
          (gnutls_x509_crt_t*)priv->known_hosts->pdata,
          priv->known_hosts->len,
          priv->known_hosts_file,
          &error
        );

        if(ret == FALSE)
        {
          g_warning(
            _("Failed to save known hosts file: %s\n"),
            error->message
          );
          g_error_free(error);
        }
      }

      inf_gtk_certificate_manager_free_certificate_array(priv->known_hosts);
      priv->known_hosts = NULL;
    }

    g_free(priv->known_hosts_file);
  }

  priv->known_hosts_file = g_strdup(known_hosts_file);
}

static void
inf_gtk_certificate_manager_certificate_func(InfXmppConnection* connection,
                                             InfCertificateChain* chain,
                                             gpointer user_data)
{
  InfGtkCertificateManager* manager;
  InfGtkCertificateManagerPrivate* priv;

  InfGtkCertificateDialogFlags flags;
  gnutls_x509_crt_t own;
  gchar* hostname;
  time_t t;

  int ret;
  unsigned int verify;

  gchar* own_hostname;
  gnutls_x509_crt_t known;
  guint i;
  gchar* own_fingerprint;
  gchar* known_fingerprint;

  InfGtkCertificateManagerQuery* query;
  gchar* text;
  GtkWidget* button;
  GtkWidget* image;
  GtkWidget* label;

  GError* error;

  manager = INF_GTK_CERTIFICATE_MANAGER(user_data);
  priv = INF_GTK_CERTIFICATE_MANAGER_PRIVATE(manager);

  /* TODO: We don't want to load these at the beginning to improve startup
   * time... maybe we should do this in an idle handler, or even
   * asynchronously via Gio. */
  if(priv->ca_certs == NULL && priv->trust_file != NULL)
  {
    error = NULL;

    priv->ca_certs = inf_cert_util_load_file(priv->trust_file, &error);
    if(priv->ca_certs == NULL)
    {
      g_warning(_("Could not load trust file: %s"), error->message);
      g_error_free(error);

      g_free(priv->trust_file);
      priv->trust_file = NULL;
      g_object_notify(G_OBJECT(manager), "trust-file");
    }
  }

  if(priv->ca_certs == NULL)
    priv->ca_certs = g_ptr_array_new();

  if(priv->known_hosts == NULL && priv->known_hosts_file != NULL)
  {
    error = NULL;

    priv->known_hosts = inf_cert_util_load_file(
      priv->known_hosts_file,
      &error
    );

    if(priv->known_hosts == NULL)
    {
      /* If the known hosts file was nonexistant, then this is not an error
       * since we will write to end when being finalized, with hosts we
       * met (and trusted) during the session. */
      if(error->domain != g_file_error_quark() ||
         error->code != G_FILE_ERROR_NOENT)
      {
        g_warning(_("Could not load known hosts file: %s"), error->message);

        g_free(priv->known_hosts_file);
        priv->known_hosts_file = NULL;
        g_object_notify(G_OBJECT(manager), "known-hosts-file");
      }
      else
      {
        priv->known_hosts = g_ptr_array_new();
      }

      g_error_free(error);
    }
  }

  g_object_get(G_OBJECT(connection), "remote-hostname", &hostname, NULL);
  own = inf_certificate_chain_get_own_certificate(chain);

  flags = 0;
  if(!gnutls_x509_crt_check_hostname(own, hostname))
    flags |= INF_GTK_CERTIFICATE_DIALOG_CERT_HOSTNAME_MISMATCH;

  t = gnutls_x509_crt_get_activation_time(own);
  if(t == (time_t)(-1) || t > time(NULL))
    flags |= INF_GTK_CERTIFICATE_DIALOG_CERT_NOT_ACTIVATED;

  t = gnutls_x509_crt_get_expiration_time(own);
  if(t == (time_t)(-1) || t < time(NULL))
    flags |= INF_GTK_CERTIFICATE_DIALOG_CERT_EXPIRED;

  ret = gnutls_x509_crt_list_verify(
    inf_certificate_chain_get_raw(chain),
    inf_certificate_chain_get_n_certificates(chain),
    (gnutls_x509_crt_t*)priv->ca_certs->pdata,
    priv->ca_certs->len,
    NULL,
    0,
    0,
    &verify
  );

  if(ret < 0)
  {
    g_warning(_("Could not verify certificate: %s"), gnutls_strerror(ret));
    inf_xmpp_connection_certificate_verify_cancel(connection);
  }
  else
  {
    if((verify & GNUTLS_CERT_INVALID) != 0)
      flags |= INF_GTK_CERTIFICATE_DIALOG_CERT_INVALID;

#define GNUTLS_CERT_ISSUER_NOT_TRUSTED \
  (GNUTLS_CERT_SIGNER_NOT_FOUND | GNUTLS_CERT_SIGNER_NOT_CA)

    if((verify & GNUTLS_CERT_ISSUER_NOT_TRUSTED) != 0)
    {
      flags |= INF_GTK_CERTIFICATE_DIALOG_CERT_ISSUER_NOT_TRUSTED;
      /* If the certificate is invalid because of this, then unset the
       * invalid flag again. We handle the two cases separately. */
      if((verify & ~GNUTLS_CERT_ISSUER_NOT_TRUSTED) == GNUTLS_CERT_INVALID)
        flags &= ~INF_GTK_CERTIFICATE_DIALOG_CERT_INVALID;
    }

    own_hostname = inf_cert_util_get_hostname(own);
    for(i = 0; i < priv->known_hosts->len; ++ i)
    {
      known = (gnutls_x509_crt_t)g_ptr_array_index(priv->known_hosts, i);
      if(gnutls_x509_crt_check_hostname(known, own_hostname))
      {
        /* TODO: Compare this as binary, not as string */
        own_fingerprint =
          inf_cert_util_get_fingerprint(own, GNUTLS_DIG_SHA1);
        known_fingerprint =
          inf_cert_util_get_fingerprint(known, GNUTLS_DIG_SHA1);

        if(strcmp(own_fingerprint, known_fingerprint) == 0)
        {
          /* We know this host, so we trust it, even if the issuer is 
           * not a CA. */
          flags &= ~INF_GTK_CERTIFICATE_DIALOG_CERT_ISSUER_NOT_TRUSTED;
        }
        else
        {
          /* The fingerprint does not match, so the certificate for this host
           * has changed. */
          flags |= INF_GTK_CERTIFICATE_DIALOG_CERT_CHANGED;

          /* Check whether it has changed because the old one expired
           * (then we have expected the certificate change, otherwise
           * something strange is going on). */
          t = gnutls_x509_crt_get_expiration_time(known);
          if(t == (time_t)(-1) || t < time(NULL))
            flags |= INF_GTK_CERTIFICATE_DIALOG_CERT_OLD_EXPIRED;
        }

        g_free(own_fingerprint);
        g_free(known_fingerprint);
        break;
      }
    }

    g_free(own_hostname);

    /* Host not found in known hosts list */
    if(i == priv->known_hosts->len)
      known = NULL;

    query = g_slice_new(InfGtkCertificateManagerQuery);
    query->manager = manager;
    query->connection = connection;
    query->dialog = inf_gtk_certificate_dialog_new(
      priv->parent_window,
      GTK_DIALOG_NO_SEPARATOR,
      flags,
      hostname,
      chain
    );
    query->certificate_chain = chain;
    query->old_certificate = known;

    g_object_ref(query->connection);
    inf_certificate_chain_ref(chain);

    g_signal_connect(
      G_OBJECT(connection),
      "notify::status",
      G_CALLBACK(inf_gtk_certificate_manager_notify_status_cb),
      query
    );

    g_signal_connect(
      G_OBJECT(query->dialog),
      "response",
      G_CALLBACK(inf_gtk_certificate_manager_response_cb),
      query
    );

    image = gtk_image_new_from_stock(GTK_STOCK_CANCEL, GTK_ICON_SIZE_BUTTON);
    gtk_widget_show(image);

    button = gtk_dialog_add_button(
      GTK_DIALOG(query->dialog),
      _("_Cancel connection"),
      GTK_RESPONSE_REJECT
    );

    gtk_button_set_image(GTK_BUTTON(button), image);

    image = gtk_image_new_from_stock(GTK_STOCK_CONNECT, GTK_ICON_SIZE_BUTTON);
    gtk_widget_show(image);

    button = gtk_dialog_add_button(
      GTK_DIALOG(query->dialog),
      _("C_ontinue connection"),
      GTK_RESPONSE_ACCEPT
    );

    gtk_button_set_image(GTK_BUTTON(button), image);

    /* TODO: Do we want a default response here? Which one? */

    text = g_strdup_printf(
      _("Do you want to continue the connection to host %s?"),
      hostname
    );

    label = gtk_label_new(text);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.0);
    gtk_widget_show(label);
    g_free(text);

    gtk_box_pack_start(
      GTK_BOX(GTK_DIALOG(query->dialog)->vbox),
      label,
      FALSE,
      FALSE,
      0
    );

    text = g_strdup_printf(
      _("Remember the answer for future connections to host %s"),
      hostname
    );

    query->checkbutton = gtk_check_button_new_with_label(text);

    /* TODO: Be able remember any answer, not only the one to
     * "issuer not trusted" */
    if(flags & INF_GTK_CERTIFICATE_DIALOG_CERT_ISSUER_NOT_TRUSTED ||
       flags & INF_GTK_CERTIFICATE_DIALOG_CERT_CHANGED)
    {
      gtk_widget_show(query->checkbutton);
    }

    g_free(text);

    gtk_box_pack_start(
      GTK_BOX(GTK_DIALOG(query->dialog)->vbox),
      query->checkbutton,
      FALSE,
      FALSE,
      0
    );

    /* TODO: In which cases should the checkbutton be checked by default? */

    priv->queries = g_slist_prepend(priv->queries, query);
    gtk_window_present(GTK_WINDOW(query->dialog));
  }

  g_free(hostname);
}

static void
inf_gtk_certificate_manager_add_connection_cb(InfXmppManager* manager,
                                              InfXmppConnection* connection,
                                              gpointer user_data)
{
  InfXmppConnectionSite site;
  g_object_get(G_OBJECT(connection), "site", &site, NULL);

  if(site == INF_XMPP_CONNECTION_CLIENT)
  {
    inf_xmpp_connection_set_certificate_callback(
      connection,
      inf_gtk_certificate_manager_certificate_func,
      user_data
    );
  }
}

static void
inf_gtk_certificate_manager_init(GTypeInstance* instance,
                                 gpointer g_class)
{
  InfGtkCertificateManager* manager;
  InfGtkCertificateManagerPrivate* priv;

  manager = INF_GTK_CERTIFICATE_MANAGER(instance);
  priv = INF_GTK_CERTIFICATE_MANAGER_PRIVATE(manager);

  priv->parent_window = NULL;
  priv->xmpp_manager = NULL;

  priv->trust_file = NULL;
  priv->known_hosts_file = NULL;

  priv->ca_certs = NULL;
  priv->known_hosts = NULL;
}

static void
inf_gtk_certificate_manager_dispose(GObject* object)
{
  InfGtkCertificateManager* manager;
  InfGtkCertificateManagerPrivate* priv;
  GSList* item;

  manager = INF_GTK_CERTIFICATE_MANAGER(object);
  priv = INF_GTK_CERTIFICATE_MANAGER_PRIVATE(manager);

  if(priv->parent_window != NULL)
  {
    g_object_unref(priv->parent_window);
    priv->parent_window = NULL;
  }

  if(priv->xmpp_manager != NULL)
  {
    g_object_unref(priv->xmpp_manager);
    priv->xmpp_manager = NULL;
  }

  for(item = priv->queries; item != NULL; item = g_slist_next(item))
  {
    inf_gtk_certificate_manager_query_free(
      (InfGtkCertificateManagerQuery*)item->data
    );
  }

  g_slist_free(priv->queries);
  priv->queries = NULL;

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_gtk_certificate_manager_finalize(GObject* object)
{
  InfGtkCertificateManager* manager;
  InfGtkCertificateManagerPrivate* priv;

  manager = INF_GTK_CERTIFICATE_MANAGER(object);
  priv = INF_GTK_CERTIFICATE_MANAGER_PRIVATE(manager);

  inf_gtk_certificate_manager_set_known_hosts(manager, NULL);

  if(priv->ca_certs != NULL)
    inf_gtk_certificate_manager_free_certificate_array(priv->ca_certs);
  g_free(priv->known_hosts_file);

  G_OBJECT_CLASS(parent_class)->finalize(object);
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
  case PROP_XMPP_MANAGER:
    g_assert(priv->xmpp_manager == NULL); /* construct/only */
    priv->xmpp_manager = INF_XMPP_MANAGER(g_value_dup_object(value));

    g_signal_connect(
      G_OBJECT(priv->xmpp_manager),
      "add-connection",
      G_CALLBACK(inf_gtk_certificate_manager_add_connection_cb),
      manager
    );

    break;
  case PROP_TRUST_FILE:
    g_free(priv->trust_file);
    priv->trust_file = g_value_dup_string(value);
    if(priv->ca_certs != NULL)
    {
      inf_gtk_certificate_manager_free_certificate_array(priv->ca_certs);
      priv->ca_certs = NULL;
    }
    break;
  case PROP_KNOWN_HOSTS_FILE:
    inf_gtk_certificate_manager_set_known_hosts(
      manager,
      g_value_get_string(value)
    );

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
  case PROP_XMPP_MANAGER:
    g_value_set_object(value, G_OBJECT(priv->xmpp_manager));
    break;
  case PROP_TRUST_FILE:
    g_value_set_string(value, priv->trust_file);
    break;
  case PROP_KNOWN_HOSTS_FILE:
    g_value_set_string(value, priv->known_hosts_file);
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
inf_gtk_certificate_manager_class_init(gpointer g_class,
                                       gpointer class_data)
{
  GObjectClass* object_class;
  InfGtkCertificateManagerClass* certificate_manager_class;

  object_class = G_OBJECT_CLASS(g_class);
  certificate_manager_class = INF_GTK_CERTIFICATE_MANAGER_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfGtkCertificateManagerPrivate));

  object_class->dispose = inf_gtk_certificate_manager_dispose;
  object_class->finalize = inf_gtk_certificate_manager_finalize;
  object_class->set_property = inf_gtk_certificate_manager_set_property;
  object_class->get_property = inf_gtk_certificate_manager_get_property;

  g_object_class_install_property(
    object_class,
    PROP_PARENT_WINDOW,
    g_param_spec_object(
      "parent-window",
      "Parent window",
      "The parent window for certificate approval dialogs",
      GTK_TYPE_WINDOW,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
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
    PROP_TRUST_FILE,
    g_param_spec_string(
      "trust-file",
      "Trust file",
      "File containing trusted root CAs",
      NULL,
      G_PARAM_READWRITE
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

GType
inf_gtk_certificate_manager_get_type(void)
{
  static GType certificate_manager_type = 0;

  if(!certificate_manager_type)
  {
    static const GTypeInfo certificate_manager_type_info = {
      sizeof(InfGtkCertificateManagerClass),    /* class_size */
      NULL,                                     /* base_init */
      NULL,                                     /* base_finalize */
      inf_gtk_certificate_manager_class_init,   /* class_init */
      NULL,                                     /* class_finalize */
      NULL,                                     /* class_data */
      sizeof(InfGtkCertificateManager),         /* instance_size */
      0,                                        /* n_preallocs */
      inf_gtk_certificate_manager_init,         /* instance_init */
      NULL                                      /* value_table */
    };

    certificate_manager_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfGtkCertificateManager",
      &certificate_manager_type_info,
      0
    );
  }

  return certificate_manager_type;
}

/*
 * Public API.
 */

/**
 * inf_gtk_certificate_manager_new:
 * @parent_window: The #GtkWindow to which to make certificate approval
 * dialogs transient to.
 * @xmpp_manager: The #InfXmppManager whose #InfXmppConnection<!-- -->s to
 * manage the certificates for.
 * @trust_file: Path pointing to a file that contains trusted root CAs, or
 * %NULL.
 * @known_hosts_file: Path pointing to a file that contains certificates of
 * known hosts, or %NULL.
 *
 * Creates a new #InfGtkCertificateManager. For each new client-side
 * #InfXmppConnection in @xmpp_manager, the certificate manager will verify
 * the server's certificate.
 *
 * If the root CA of that certificate is contained in @trust_file, or the
 * server certificate itself is known already (meaning it is contained in
 * @known_hosts_file), then the certificate is accepted automatically.
 * Otherwise, the user is asked for approval. If the user approves the
 * certificate, then it is inserted into the @known_hosts_file.
 *
 * Returns: A new #InfGtkCertificateManager.
 **/
InfGtkCertificateManager*
inf_gtk_certificate_manager_new(GtkWindow* parent_window,
                                InfXmppManager* xmpp_manager,
                                const gchar* trust_file,
                                const gchar* known_hosts_file)
{
  GObject* object;

  object = g_object_new(
    INF_GTK_TYPE_CERTIFICATE_MANAGER,
    "parent-window", parent_window,
    "xmpp-manager", xmpp_manager,
    "trust-file", trust_file,
    "known-hosts-file", known_hosts_file,
    NULL
  );

  return INF_GTK_CERTIFICATE_MANAGER(object);
}

/* vim:set et sw=2 ts=2: */
