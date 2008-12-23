/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008 Armin Burgmeier <armin@arbur.net>
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

#include <libinfgtk/inf-gtk-certificate-dialog.h>
#include <libinfgtk/inf-gtk-certificate-view.h>
#include <libinfinity/common/inf-cert-util.h>
#include <libinfinity/inf-i18n.h>

#include <gnutls/x509.h>
#include <time.h>

typedef struct _InfGtkCertificateDialogPrivate InfGtkCertificateDialogPrivate;
struct _InfGtkCertificateDialogPrivate {
  InfCertificateChain* certificate_chain;
  InfGtkCertificateDialogFlags certificate_flags;
  gchar* hostname;

  GtkTreeStore* certificate_tree_store;

  GtkWidget* main_vbox;
  GtkWidget* upper_hbox;
  GtkWidget* info_vbox;
  GtkWidget* certificate_expander;
  GtkWidget* certificate_tree_view;
  GtkWidget* certificate_info_view;
};

enum {
  PROP_0,

  PROP_CERTIFICATE_CHAIN,
  PROP_CERTIFICATE_FLAGS,
  PROP_HOSTNAME
};

#define INF_GTK_CERTIFICATE_DIALOG_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_GTK_TYPE_CERTIFICATE_DIALOG, InfGtkCertificateDialogPrivate))

static GtkDialogClass* parent_class;

static void
inf_gtk_certificate_dialog_renew_info(InfGtkCertificateDialog* dialog)
{
  InfGtkCertificateDialogPrivate* priv;
  gnutls_x509_crt_t own_cert;

  gint normal_width_chars;
  gint size;
  PangoFontDescription* font_desc;

  const gchar* ctext;
  gchar* text;
  gchar* markup;
  GString* info_text;
  GtkWidget* caption;
  GtkWidget* info;

  priv = INF_GTK_CERTIFICATE_DIALOG_PRIVATE(dialog);

  if(priv->info_vbox != NULL)
  {
    gtk_container_remove(GTK_CONTAINER(priv->upper_hbox), priv->info_vbox);
    priv->info_vbox = NULL;
  }

  if(priv->certificate_flags != 0 && priv->hostname != NULL)
  {
    own_cert =
      inf_certificate_chain_get_own_certificate(priv->certificate_chain);

    text = g_strdup_printf(
      _("The connection to host \"%s\" is not considered secure"),
      priv->hostname
    );

    caption = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(caption), 0.0, 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(caption), TRUE);
    size = pango_font_description_get_size(caption->style->font_desc);
    font_desc = pango_font_description_new();
    pango_font_description_set_weight(font_desc, PANGO_WEIGHT_BOLD);
    pango_font_description_set_size(font_desc, size * PANGO_SCALE_LARGE);
    gtk_widget_modify_font(caption, font_desc);
    pango_font_description_free(font_desc);

    normal_width_chars = gtk_label_get_max_width_chars(GTK_LABEL(caption));
    gtk_label_set_max_width_chars(
      GTK_LABEL(caption),
      (gint)(normal_width_chars / PANGO_SCALE_LARGE)
    );

    gtk_label_set_text(GTK_LABEL(caption), text);
    g_free(text);
    gtk_widget_show(caption);

    info_text = g_string_sized_new(256);

    if(priv->certificate_flags &
       INF_GTK_CERTIFICATE_DIALOG_CERT_CHANGED)
    {
      ctext = _("The certificate of the host has changed!");
      markup = g_markup_printf_escaped("<b>%s</b>", ctext);

      g_string_append(info_text, markup);
      g_free(markup);
      g_string_append_c(info_text, ' ');

      if(priv->certificate_flags &
         INF_GTK_CERTIFICATE_DIALOG_CERT_OLD_EXPIRED)
      {
        g_string_append(
          info_text,
          _("The previous certificate of the server has expired.")
        );
      }
      else
      {
        g_string_append(
          info_text,
          _("It is possible that the connection to the server is being "
            "hijacked. It is also possible that the host just has got a new "
            "certificate. However, please only continue the connection if "
            "you expected this warning.")
        );
      }
    }

    if(priv->certificate_flags &
       INF_GTK_CERTIFICATE_DIALOG_CERT_ISSUER_NOT_TRUSTED)
    {
      if(info_text->len > 0)
        g_string_append(info_text, "\n\n");

      g_string_append(
        info_text,
        _("The certificate issuer is not trusted.")
      );

      if(gnutls_x509_crt_check_issuer(own_cert, own_cert))
      {
        g_string_append_c(info_text, ' ');
        g_string_append(info_text, _("The certificate is self-signed."));
      }
    }

    if(priv->certificate_flags &
       INF_GTK_CERTIFICATE_DIALOG_CERT_INVALID)
    {
      if(info_text->len > 0)
        g_string_append(info_text, "\n\n");

      ctext = _("The certificate is invalid!");
      markup = g_markup_printf_escaped("<b>%s</b>", ctext);
      g_string_append(info_text, markup);
      g_free(markup);
    }

    if(priv->certificate_flags &
       INF_GTK_CERTIFICATE_DIALOG_CERT_HOSTNAME_MISMATCH)
    {
      if(info_text->len > 0)
        g_string_append(info_text, "\n\n");

      text = inf_cert_util_get_hostname(own_cert);

      g_string_append_printf(
        info_text,
        _("The hostname of the server, \"%s\", does not match the hostname "
          "the certificate is issued to, \"%s\"."),
        priv->hostname,
        text
      );

      g_free(text);
    }

    if(priv->certificate_flags &
       INF_GTK_CERTIFICATE_DIALOG_CERT_EXPIRED)
    {
      if(info_text->len > 0)
        g_string_append(info_text, "\n\n");

      text = inf_cert_util_get_expiration_time(own_cert);

      g_string_append_printf(
        info_text,
        _("The certificate has expired. The expiration date was %s"),
        text
      );

      g_free(text);
    }

    if(priv->certificate_flags &
       INF_GTK_CERTIFICATE_DIALOG_CERT_NOT_ACTIVATED)
    {
      if(info_text->len > 0)
        g_string_append(info_text, "\n\n");

      text = inf_cert_util_get_activation_time(own_cert);

      g_string_append_printf(
        info_text,
        _("The certificate has not yet been activated. "
          "Activation date is %s"),
        text
      );

      g_free(text);
    }

    info = gtk_label_new(NULL);
    markup = g_string_free(info_text, FALSE);
    gtk_label_set_markup(GTK_LABEL(info), markup);
    g_free(markup);

    gtk_label_set_selectable(GTK_LABEL(info), TRUE);
    gtk_label_set_line_wrap(GTK_LABEL(info), TRUE);
    gtk_misc_set_alignment(GTK_MISC(info), 0.0, 0.0);
    gtk_widget_show(info);

    priv->info_vbox = gtk_vbox_new(FALSE, 12);
    gtk_box_pack_start(GTK_BOX(priv->info_vbox), caption, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(priv->info_vbox), info, FALSE, TRUE, 0);
    gtk_widget_show(priv->info_vbox);

    gtk_box_pack_start(
      GTK_BOX(priv->upper_hbox),
      priv->info_vbox,
      TRUE,
      TRUE,
      0
    );
  }
}

static void
inf_gtk_certificate_dialog_set_chain(InfGtkCertificateDialog* dialog,
                                     InfCertificateChain* chain)
{
  InfGtkCertificateDialogPrivate* priv;
  guint i;
  gnutls_x509_crt_t crt;
  GtkTreeIter prev_row;
  GtkTreeIter new_row;
  GtkTreeIter* parent;
  GtkTreePath* path;

  priv = INF_GTK_CERTIFICATE_DIALOG_PRIVATE(dialog);

  if(priv->certificate_chain != NULL)
    inf_certificate_chain_unref(priv->certificate_chain);

  priv->certificate_chain = chain;

  gtk_tree_store_clear(priv->certificate_tree_store);
  inf_gtk_certificate_view_set_certificate(
    INF_GTK_CERTIFICATE_VIEW(priv->certificate_info_view),
    NULL
  );

  parent = NULL;
  if(chain != NULL)
  {
    inf_certificate_chain_ref(chain);

    for(i = inf_certificate_chain_get_n_certificates(chain); i > 0; -- i)
    {
      crt = inf_certificate_chain_get_nth_certificate(chain, i - 1);
      gtk_tree_store_append(priv->certificate_tree_store, &new_row, parent);
      gtk_tree_store_set(priv->certificate_tree_store, &new_row, 0, crt, -1);

      prev_row = new_row;
      parent = &prev_row;
    }

    path = gtk_tree_model_get_path(
      GTK_TREE_MODEL(priv->certificate_tree_store),
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

  g_object_notify(G_OBJECT(dialog), "certificate-chain");
}

static void
inf_gtk_certificate_dialog_selection_changed_cb(GtkTreeSelection* selection,
                                                gpointer user_data)
{
  InfGtkCertificateDialog* dialog;
  InfGtkCertificateDialogPrivate* priv;
  GtkTreeIter iter;
  gnutls_x509_crt_t cert;

  dialog = INF_GTK_CERTIFICATE_DIALOG(user_data);
  priv = INF_GTK_CERTIFICATE_DIALOG_PRIVATE(dialog);

  if(gtk_tree_selection_get_selected(selection, NULL, &iter))
  {
    gtk_tree_model_get(
      GTK_TREE_MODEL(priv->certificate_tree_store),
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
inf_gtk_certificate_dialog_chain_data_func(GtkTreeViewColumn* column,
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
inf_gtk_certificate_dialog_init(GTypeInstance* instance,
                                gpointer g_class)
{
  GtkWidget* image;
  GtkWidget* hbox;
  GtkWidget* scroll;
  GtkTreeViewColumn* column;
  GtkCellRenderer* renderer;
  GtkTreeSelection* selection;
  GtkIconTheme* theme;
  GtkIconInfo* icon_info;

  InfGtkCertificateDialog* dialog;
  InfGtkCertificateDialogPrivate* priv;

  dialog = INF_GTK_CERTIFICATE_DIALOG(instance);
  priv = INF_GTK_CERTIFICATE_DIALOG_PRIVATE(dialog);

  priv->certificate_chain = NULL;
  priv->certificate_flags = 0;
  priv->hostname = NULL;

  priv->certificate_tree_store = gtk_tree_store_new(1, G_TYPE_POINTER);

  /* Warning */
  priv->info_vbox = NULL;
  priv->upper_hbox = gtk_hbox_new(FALSE, 12);

  image = gtk_image_new_from_stock(
    GTK_STOCK_DIALOG_AUTHENTICATION,
    GTK_ICON_SIZE_DIALOG
  );

  gtk_misc_set_alignment(GTK_MISC(image), 0.0, 0.0);
  gtk_widget_show(image);

  gtk_box_pack_start(
    GTK_BOX(priv->upper_hbox),
    image,
    FALSE,
    TRUE,
    0
  );

  gtk_widget_show(priv->upper_hbox);

  /* Certificate info */
  column = gtk_tree_view_column_new();
  gtk_tree_view_column_set_title(column, _("Certificate Chain"));
  gtk_tree_view_column_set_spacing(column, 6);

  renderer = gtk_cell_renderer_pixbuf_new();
  theme = gtk_icon_theme_get_default();
  icon_info = gtk_icon_theme_lookup_icon(
    theme,
    "application-certificate",
    GTK_ICON_SIZE_MENU,
    GTK_ICON_LOOKUP_USE_BUILTIN
  );

  if(icon_info != NULL)
  {
    g_object_set(
      G_OBJECT(renderer),
      "icon-name", "application-certificate",
      NULL
    );

    gtk_icon_info_free(icon_info);
  }
  else
  {
    g_object_set(
      G_OBJECT(renderer),
      "visible", FALSE,
      NULL
    );
  }

  gtk_tree_view_column_pack_start(column, renderer, FALSE);

  renderer = gtk_cell_renderer_text_new();
  gtk_tree_view_column_pack_start(column, renderer, TRUE);

  gtk_tree_view_column_set_cell_data_func(
    column,
    renderer,
    inf_gtk_certificate_dialog_chain_data_func,
    NULL,
    NULL
  );

  priv->certificate_tree_view = gtk_tree_view_new_with_model(
    GTK_TREE_MODEL(priv->certificate_tree_store)
  );

  gtk_tree_view_append_column(
    GTK_TREE_VIEW(priv->certificate_tree_view),
    column
  );

  gtk_tree_view_set_show_expanders(
    GTK_TREE_VIEW(priv->certificate_tree_view),
    FALSE
  );

  gtk_tree_view_set_level_indentation(
    GTK_TREE_VIEW(priv->certificate_tree_view),
    12
  );

  selection =
    gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->certificate_tree_view));
  gtk_tree_selection_set_mode(selection, GTK_SELECTION_BROWSE);

  g_signal_connect(
    G_OBJECT(selection),
    "changed",
    G_CALLBACK(inf_gtk_certificate_dialog_selection_changed_cb),
    dialog
  );

  gtk_widget_show(priv->certificate_tree_view);

  scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_widget_set_size_request(scroll, 200, -1);

  gtk_scrolled_window_set_shadow_type(
    GTK_SCROLLED_WINDOW(scroll),
    GTK_SHADOW_IN
  );

  gtk_scrolled_window_set_policy(
    GTK_SCROLLED_WINDOW(scroll),
    GTK_POLICY_AUTOMATIC,
    GTK_POLICY_AUTOMATIC
  );

  gtk_container_add(GTK_CONTAINER(scroll), priv->certificate_tree_view);
  gtk_widget_show(scroll);

  priv->certificate_info_view = inf_gtk_certificate_view_new();
  gtk_widget_show(priv->certificate_info_view);

  hbox = gtk_hbox_new(FALSE, 12);
  gtk_box_pack_start(GTK_BOX(hbox), scroll, FALSE, FALSE, 0);

  gtk_box_pack_start(
    GTK_BOX(hbox),
    priv->certificate_info_view,
    TRUE,
    TRUE,
    0
  );

  gtk_widget_show(hbox);

  priv->certificate_expander =
    gtk_expander_new_with_mnemonic(_("_View Certificate"));
  gtk_expander_set_spacing(GTK_EXPANDER(priv->certificate_expander), 6);
  gtk_container_add(GTK_CONTAINER(priv->certificate_expander), hbox);

  priv->main_vbox = gtk_vbox_new(FALSE, 12);

  /* Main */
  gtk_box_pack_start(
    GTK_BOX(priv->main_vbox),
    priv->upper_hbox,
    FALSE,
    TRUE,
    0
  );

  gtk_box_pack_start(
    GTK_BOX(priv->main_vbox),
    priv->certificate_expander,
    TRUE,
    TRUE,
    0
  );

  gtk_widget_show(priv->main_vbox);

  gtk_box_pack_start(
    GTK_BOX(GTK_DIALOG(dialog)->vbox),
    priv->main_vbox,
    TRUE,
    TRUE,
    0
  );

  gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(dialog)->vbox), 12);

  gtk_container_set_border_width(GTK_CONTAINER(dialog), 12);
  gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
  gtk_window_set_title(GTK_WINDOW(dialog), _("Connection not secure"));
}

static void
inf_gtk_certificate_dialog_finalize(GObject* object)
{
  InfGtkCertificateDialog* dialog;
  InfGtkCertificateDialogPrivate* priv;

  dialog = INF_GTK_CERTIFICATE_DIALOG(object);
  priv = INF_GTK_CERTIFICATE_DIALOG_PRIVATE(dialog);

  inf_certificate_chain_unref(priv->certificate_chain);
  g_free(priv->hostname);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_gtk_certificate_dialog_set_property(GObject* object,
                                        guint prop_id,
                                        const GValue* value,
                                        GParamSpec* pspec)
{
  InfGtkCertificateDialog* dialog;
  InfGtkCertificateDialogPrivate* priv;

  dialog = INF_GTK_CERTIFICATE_DIALOG(object);
  priv = INF_GTK_CERTIFICATE_DIALOG_PRIVATE(dialog);

  switch(prop_id)
  {
  case PROP_CERTIFICATE_CHAIN:
    inf_gtk_certificate_dialog_set_chain(
      dialog,
      (InfCertificateChain*)g_value_get_boxed(value)
    );

    break;
  case PROP_CERTIFICATE_FLAGS:
    priv->certificate_flags =
      (InfGtkCertificateDialogFlags)g_value_get_flags(value);

    if(priv->certificate_flags != 0 && priv->hostname != NULL)
      inf_gtk_certificate_dialog_renew_info(dialog);

    break;
  case PROP_HOSTNAME:
    if(priv->hostname != NULL) g_free(priv->hostname);
    priv->hostname = g_value_dup_string(value);
    if(priv->certificate_flags != 0 && priv->hostname != NULL)
      inf_gtk_certificate_dialog_renew_info(dialog);

    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_gtk_certificate_dialog_get_property(GObject* object,
                                        guint prop_id,
                                        GValue* value,
                                        GParamSpec* pspec)
{
  InfGtkCertificateDialog* dialog;
  InfGtkCertificateDialogPrivate* priv;

  dialog = INF_GTK_CERTIFICATE_DIALOG(object);
  priv = INF_GTK_CERTIFICATE_DIALOG_PRIVATE(dialog);

  switch(prop_id)
  {
  case PROP_CERTIFICATE_CHAIN:
    g_value_set_boxed(value, priv->certificate_chain);
    break;
  case PROP_CERTIFICATE_FLAGS:
    g_value_set_flags(value, priv->certificate_flags);
    break;
  case PROP_HOSTNAME:
    g_value_set_string(value, priv->hostname);
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
inf_gtk_certificate_dialog_class_init(gpointer g_class,
                                       gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = GTK_DIALOG_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfGtkCertificateDialogPrivate));

  object_class->finalize = inf_gtk_certificate_dialog_finalize;
  object_class->set_property = inf_gtk_certificate_dialog_set_property;
  object_class->get_property = inf_gtk_certificate_dialog_get_property;

  g_object_class_install_property(
    object_class,
    PROP_CERTIFICATE_CHAIN,
    g_param_spec_boxed(
      "certificate-chain",
      "Certificate chain",
      "The certificate chain to show in the dialog",
      INF_TYPE_CERTIFICATE_CHAIN,
      G_PARAM_READWRITE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_CERTIFICATE_FLAGS,
    g_param_spec_flags(
      "certificate-flags",
      "Certificate flags",
      "What warnings about the certificate to display",
      INF_GTK_TYPE_CERTIFICATE_DIALOG_FLAGS,
      0,
      G_PARAM_READWRITE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_HOSTNAME,
    g_param_spec_string(
      "hostname",
      "Host name",
      "Host name of the server from which the certificate is",
      NULL,
      G_PARAM_READWRITE
    )
  );
}

GType
inf_gtk_certificate_dialog_flags_get_type(void)
{
  static GType certificate_dialog_flags_type = 0;

  if(!certificate_dialog_flags_type)
  {
    static const GFlagsValue certificate_dialog_flags_type_values[] = {
      {
        INF_GTK_CERTIFICATE_DIALOG_CERT_NOT_ACTIVATED,
        "INF_GTK_CERTIFICATE_DIALOG_CERT_NOT_ACTIVATED",
        "cert-not-activated"
      }, {
        INF_GTK_CERTIFICATE_DIALOG_CERT_EXPIRED,
        "INF_GTK_CERTIFICATE_DIALOG_CERT_EXPIRED",
        "cert-expired"
      }, {
        INF_GTK_CERTIFICATE_DIALOG_CERT_HOSTNAME_MISMATCH,
        "INF_GTK_CERTIFICATE_DIALOG_CERT_HOSTNAME_MISMATCH",
        "cert-hostname-mismatch"
      }, {
        INF_GTK_CERTIFICATE_DIALOG_CERT_ISSUER_NOT_TRUSTED,
        "INF_GTK_CERTIFICATE_DIALOG_CERT_ISSUER_NOT_TRUSTED",
        "cert-not-trusted"
      }, {
        INF_GTK_CERTIFICATE_DIALOG_CERT_INVALID,
        "INF_GTK_CERTIFICATE_DIALOG_CERT_INVALID",
        "cert-invalid"
      }, {
        INF_GTK_CERTIFICATE_DIALOG_CERT_CHANGED,
        "INF_GTK_CERTIFICATE_DIALOG_CERT_CHANGED",
        "cert-changed"
      }, {
        0,
        NULL,
        NULL
      }
    };

    certificate_dialog_flags_type = g_flags_register_static(
      "InfCertificateDialogFlags",
      certificate_dialog_flags_type_values
    );
  }

  return certificate_dialog_flags_type;
}

GType
inf_gtk_certificate_dialog_get_type(void)
{
  static GType certificate_dialog_type = 0;

  if(!certificate_dialog_type)
  {
    static const GTypeInfo certificate_dialog_type_info = {
      sizeof(InfGtkCertificateDialogClass),    /* class_size */
      NULL,                                    /* base_init */
      NULL,                                    /* base_finalize */
      inf_gtk_certificate_dialog_class_init,   /* class_init */
      NULL,                                    /* class_finalize */
      NULL,                                    /* class_data */
      sizeof(InfGtkCertificateDialog),         /* instance_size */
      0,                                       /* n_preallocs */
      inf_gtk_certificate_dialog_init,         /* instance_init */
      NULL                                     /* value_table */
    };

    certificate_dialog_type = g_type_register_static(
      GTK_TYPE_DIALOG,
      "InfGtkCertificateDialog",
      &certificate_dialog_type_info,
      0
    );
  }

  return certificate_dialog_type;
}

/*
 * Public API.
 */

/**
 * inf_gtk_certificate_dialog_new:
 * @parent: Parent #GtkWindow of the dialog.
 * @dialog_flags: Flags for the dialog, see #GtkDialogFlags.
 * @certificate_flags: What certificate warnings to show, see
 * #InfGtkCertificateDialogFlags.
 * @hostname: The host name of the server that provides the certificate.
 * @certificate_chain: The certificate chain provided by the server.
 *
 * Creates a new #InfGtkCertificateDialog. A #InfGtkCertificateDialog shows
 * warnings about a server's certificate to a user, for example when the
 * issuer is not trusted or the certificate is expired.
 *
 * Returns: A New #InfGtkCertificateDialog.
 */
InfGtkCertificateDialog*
inf_gtk_certificate_dialog_new(GtkWindow* parent,
                               GtkDialogFlags dialog_flags,
                               InfGtkCertificateDialogFlags certificate_flags,
                               const gchar* hostname,
                               InfCertificateChain* certificate_chain)
{
  GObject* object;

  g_return_val_if_fail(parent == NULL || GTK_IS_WINDOW(parent), NULL);
  g_return_val_if_fail(certificate_flags != 0, NULL);
  g_return_val_if_fail(hostname != NULL, NULL);
  g_return_val_if_fail(certificate_chain != NULL, NULL);

  object = g_object_new(
    INF_GTK_TYPE_CERTIFICATE_DIALOG,
    "certificate-chain", certificate_chain,
    "certificate-flags", certificate_flags,
    "hostname", hostname,
    NULL
  );

  if(dialog_flags & GTK_DIALOG_MODAL)
    gtk_window_set_modal(GTK_WINDOW(object), TRUE);

  if(dialog_flags & GTK_DIALOG_DESTROY_WITH_PARENT)
    gtk_window_set_destroy_with_parent(GTK_WINDOW(object), TRUE);

  if(dialog_flags & GTK_DIALOG_NO_SEPARATOR)
    gtk_dialog_set_has_separator(GTK_DIALOG(object), FALSE);
 
  gtk_window_set_transient_for(GTK_WINDOW(object), parent);
  return INF_GTK_CERTIFICATE_DIALOG(object);
}

/* vim:set et sw=2 ts=2: */
