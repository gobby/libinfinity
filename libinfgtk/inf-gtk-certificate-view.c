/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2011 Armin Burgmeier <armin@arbur.net>
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

#include <libinfgtk/inf-gtk-certificate-view.h>
#include <libinfinity/common/inf-cert-util.h>
#include <libinfinity/inf-i18n.h>

#include <gnutls/x509.h>
#include <time.h>

typedef struct _InfGtkCertificateViewPrivate InfGtkCertificateViewPrivate;
struct _InfGtkCertificateViewPrivate {
  gnutls_x509_crt_t certificate;

  GtkWidget* general_vbox;
  GtkSizeGroup* general_size_group;

  GtkWidget* common_name;
  GtkWidget* organization;
  GtkWidget* organizational_unit;
  GtkWidget* serial_number;

  GtkWidget* issuer_common_name;
  GtkWidget* issuer_organization;
  GtkWidget* issuer_organizational_unit;

  GtkWidget* activation_time;
  GtkWidget* expiration_time;

  GtkWidget* sha1_fingerprint;
  GtkWidget* md5_fingerprint;
};

enum {
  PROP_0,

  PROP_CERTIFICATE
};

#define INF_GTK_CERTIFICATE_VIEW_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_GTK_TYPE_CERTIFICATE_VIEW, InfGtkCertificateViewPrivate))

static GtkNotebookClass* parent_class;

static void
inf_gtk_certificate_view_set_label(GtkLabel* label,
                                   const gchar* value)
{
  const gchar* text;
  gchar* markup;

  if(value != NULL)
  {
    gtk_label_set_text(label, value);
  }
  else
  {
    text = _("<Not part of certificate>");
    markup = g_markup_printf_escaped("<i>%s</i>", text);
    gtk_label_set_markup(label, markup);
    g_free(markup);
  }
}

static void
inf_gtk_certificate_view_set_label_dn_by_oid(gnutls_x509_crt_t cert,
                                             GtkLabel* label,
                                             const char* oid)
{
  gchar* value;

  value = inf_cert_util_get_dn_by_oid(cert, oid, 0);
  inf_gtk_certificate_view_set_label(label, value);
  g_free(value);
}

static void
inf_gtk_certificate_view_set_label_issuer_dn_by_oid(gnutls_x509_crt_t cert,
                                                    GtkLabel* label,
                                                    const gchar* oid)
{
  gchar* value;

  value = inf_cert_util_get_issuer_dn_by_oid(cert, oid, 0);
  inf_gtk_certificate_view_set_label(label, value);
  g_free(value);
}

static void
inf_gtk_certificate_view_add_section(GtkSizeGroup* size_group,
                                     GtkVBox* parent,
                                     const gchar* title,
                                     const gchar* first_caption,
                                     ...)
{
  GtkWidget* table;
  va_list valist;

  const gchar* caption;
  GtkWidget** location;
  GtkWidget* caption_label;
  unsigned int i;

  GtkWidget* alignment;
  GtkWidget* frame;
  GtkWidget* title_label;
  gchar* title_markup;

  table = gtk_table_new(1, 2, FALSE);
  gtk_table_set_col_spacings(GTK_TABLE(table), 12);
  gtk_table_set_row_spacings(GTK_TABLE(table), 6);
  va_start(valist, first_caption);

  caption = first_caption;
  for(i = 1; caption != NULL; ++ i)
  {
    location = va_arg(valist, GtkWidget**);

    gtk_table_resize(GTK_TABLE(table), i, 2);

    caption_label = gtk_label_new(caption);
    gtk_misc_set_alignment(GTK_MISC(caption_label), 0.0, 0.0);
    gtk_widget_show(caption_label);

    *location = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(*location), 0.0, 0.0);
    gtk_label_set_selectable(GTK_LABEL(*location), TRUE);
    gtk_widget_show(*location);

    gtk_size_group_add_widget(size_group, caption_label);

    gtk_table_attach(
      GTK_TABLE(table),
      caption_label,
      0,
      1,
      i-1,
      i,
      GTK_FILL,
      GTK_FILL,
      0,
      0
    );

    gtk_table_attach(
      GTK_TABLE(table),
      *location,
      1,
      2,
      i-1,
      i,
      GTK_EXPAND | GTK_FILL,
      GTK_FILL,
      0,
      0
    );

    caption = va_arg(valist, const gchar*);
  }

  va_end(valist);
  gtk_widget_show(table);

  alignment = gtk_alignment_new(0.0, 0.0, 1.0, 1.0);
  gtk_alignment_set_padding(GTK_ALIGNMENT(alignment), 6, 0, 12, 0);
  gtk_container_add(GTK_CONTAINER(alignment), table);
  gtk_widget_show(alignment);

  frame = gtk_frame_new(NULL);
  title_label = gtk_label_new(NULL);
  title_markup = g_markup_printf_escaped("<b>%s</b>", title);
  gtk_label_set_markup(GTK_LABEL(title_label), title_markup);
  g_free(title_markup);
  gtk_widget_show(title_label);

  gtk_frame_set_label_widget(GTK_FRAME(frame), title_label);
  gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_NONE);
  gtk_container_add(GTK_CONTAINER(frame), alignment);
  gtk_widget_show(frame);

  gtk_box_pack_start(GTK_BOX(parent), frame, FALSE, FALSE, 0);
}

static void
inf_gtk_certificate_view_init(GTypeInstance* instance,
                              gpointer g_class)
{
  InfGtkCertificateView* view;
  InfGtkCertificateViewPrivate* priv;
  PangoFontDescription* monospace_desc;
  gint size;

  view = INF_GTK_CERTIFICATE_VIEW(instance);
  priv = INF_GTK_CERTIFICATE_VIEW_PRIVATE(view);

  priv->certificate = NULL;
  gtk_notebook_set_show_tabs(GTK_NOTEBOOK(view), FALSE);
  gtk_notebook_set_show_border(GTK_NOTEBOOK(view), FALSE);

  priv->general_vbox = gtk_vbox_new(FALSE, 12);
  priv->general_size_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);

  inf_gtk_certificate_view_add_section(
    priv->general_size_group,
    GTK_VBOX(priv->general_vbox),
    _("Issued To"),
    _("Common Name:"), &priv->common_name,
    _("Organization:"), &priv->organization,
    _("Organizational Unit:"), &priv->organizational_unit,
    _("Serial Number:"), &priv->serial_number,
    NULL
  );

  inf_gtk_certificate_view_add_section(
    priv->general_size_group,
    GTK_VBOX(priv->general_vbox),
    _("Issued By"),
    _("Common Name:"), &priv->issuer_common_name,
    _("Organization:"), &priv->issuer_organization,
    _("Organizational Unit:"), &priv->issuer_organizational_unit,
    NULL
  );

  inf_gtk_certificate_view_add_section(
    priv->general_size_group,
    GTK_VBOX(priv->general_vbox),
    _("Validity"),
    _("Issued On:"), &priv->activation_time,
    _("Expires On:"), &priv->expiration_time,
    NULL
  );

  inf_gtk_certificate_view_add_section(
    priv->general_size_group,
    GTK_VBOX(priv->general_vbox),
    _("Fingerprints"),
    _("SHA1 Fingerprint:"), &priv->sha1_fingerprint,
    _("MD5 Fingerprint:"), &priv->md5_fingerprint,
    NULL
  );

  size = pango_font_description_get_size(
    gtk_widget_get_style(priv->serial_number)->font_desc);
  monospace_desc = pango_font_description_new();
  pango_font_description_set_family(monospace_desc, "Monospace");
  pango_font_description_set_size(monospace_desc, size * PANGO_SCALE_SMALL);

  gtk_widget_modify_font(priv->serial_number, monospace_desc);
  gtk_widget_modify_font(priv->sha1_fingerprint, monospace_desc);
  gtk_widget_modify_font(priv->md5_fingerprint, monospace_desc);

  pango_font_description_free(monospace_desc);

  gtk_notebook_append_page(
    GTK_NOTEBOOK(view),
    priv->general_vbox,
    gtk_label_new(_("General"))
  );

  gtk_widget_show(priv->general_vbox);
}

static void
inf_gtk_certificate_view_dispose(GObject* object)
{
  InfGtkCertificateView* view;
  InfGtkCertificateViewPrivate* priv;

  view = INF_GTK_CERTIFICATE_VIEW(object);
  priv = INF_GTK_CERTIFICATE_VIEW_PRIVATE(view);

  if(priv->general_size_group != NULL)
  {
    g_object_unref(priv->general_size_group);
    priv->general_size_group = NULL;
  }

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_gtk_certificate_view_set_property(GObject* object,
                                      guint prop_id,
                                      const GValue* value,
                                      GParamSpec* pspec)
{
  InfGtkCertificateView* view;
  InfGtkCertificateViewPrivate* priv;

  view = INF_GTK_CERTIFICATE_VIEW(object);
  priv = INF_GTK_CERTIFICATE_VIEW_PRIVATE(view);

  switch(prop_id)
  {
  case PROP_CERTIFICATE:
    inf_gtk_certificate_view_set_certificate(
      view,
      (gnutls_x509_crt_t)g_value_get_pointer(value)
    );

    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_gtk_certificate_view_get_property(GObject* object,
                                      guint prop_id,
                                      GValue* value,
                                      GParamSpec* pspec)
{
  InfGtkCertificateView* view;
  InfGtkCertificateViewPrivate* priv;

  view = INF_GTK_CERTIFICATE_VIEW(object);
  priv = INF_GTK_CERTIFICATE_VIEW_PRIVATE(view);

  switch(prop_id)
  {
  case PROP_CERTIFICATE:
    g_value_set_pointer(value, priv->certificate);
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
inf_gtk_certificate_view_class_init(gpointer g_class,
                                    gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = GTK_NOTEBOOK_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfGtkCertificateViewPrivate));

  object_class->dispose = inf_gtk_certificate_view_dispose;
  object_class->set_property = inf_gtk_certificate_view_set_property;
  object_class->get_property = inf_gtk_certificate_view_get_property;

  g_object_class_install_property(
    object_class,
    PROP_CERTIFICATE,
    g_param_spec_pointer(
      "certificate",
      "Certificate",
      "The certificate to show",
      G_PARAM_READWRITE
    )
  );
}

GType
inf_gtk_certificate_view_get_type(void)
{
  static GType certificate_view_type = 0;

  if(!certificate_view_type)
  {
    static const GTypeInfo certificate_view_type_info = {
      sizeof(InfGtkCertificateViewClass),    /* class_size */
      NULL,                                  /* base_init */
      NULL,                                  /* base_finalize */
      inf_gtk_certificate_view_class_init,   /* class_init */
      NULL,                                  /* class_finalize */
      NULL,                                  /* class_data */
      sizeof(InfGtkCertificateView),         /* instance_size */
      0,                                     /* n_preallocs */
      inf_gtk_certificate_view_init,         /* instance_init */
      NULL                                   /* value_table */
    };

    certificate_view_type = g_type_register_static(
      GTK_TYPE_NOTEBOOK,
      "InfGtkCertificateView",
      &certificate_view_type_info,
      0
    );
  }

  return certificate_view_type;
}

/*
 * Public API.
 */

/**
 * inf_gtk_certificate_view_new:
 *
 * Creates a new #InfGtkCertificateView. To show a certificate, use
 * inf_gtk_certificate_view_set_certificate() on the returned widget.
 *
 * Returns: A new #InfGtkCertificateView.
 */
GtkWidget*
inf_gtk_certificate_view_new(void)
{
  GObject* object;
  object = g_object_new(INF_GTK_TYPE_CERTIFICATE_VIEW, NULL);
  return GTK_WIDGET(object);
}

/**
 * inf_gtk_certificate_view_new_with_certificate:
 * @cert: The certificate to show.
 *
 * Creates a new #InfGtkCertificateView showing the given certificate. This
 * is the same as creating a new certificate view and calling
 * inf_gtk_certificate_view_set_certificate() afterwards.
 *
 * @cert must not be freed as long as the certificate view is showing it. You
 * can make the view not showing it anymore by calling
 * inf_gtk_certificate_view_set_certificate() with %NULL as certificate.
 *
 * Returns: A new #InfGtkCertificateView.
 */

GtkWidget*
inf_gtk_certificate_view_new_with_certificate(gnutls_x509_crt_t cert)
{
  GObject* object;

  object = g_object_new(
    INF_GTK_TYPE_CERTIFICATE_VIEW,
    "certificate", cert,
    NULL
  );

  return GTK_WIDGET(object);
}

/**
 * inf_gtk_certificate_view_set_certificate:
 * @view: A #InfGtkCertificateView.
 * @cert: The certificate to show.
 *
 * Shows the given certificate in @view.
 *
 * @cert must not be freed as long as the certificate view is showing it. You
 * can make the view not showing it anymore by calling
 * inf_gtk_certificate_view_set_certificate() with %NULL as certificate.
 */
void
inf_gtk_certificate_view_set_certificate(InfGtkCertificateView* view,
                                         gnutls_x509_crt_t cert)
{
  InfGtkCertificateViewPrivate* priv;
  gchar* value;

  g_return_if_fail(INF_GTK_IS_CERTIFICATE_VIEW(view));

  priv = INF_GTK_CERTIFICATE_VIEW_PRIVATE(view);
  priv->certificate = cert;

  if(cert == NULL)
  {
    gtk_label_set_text(GTK_LABEL(priv->common_name), NULL);
    gtk_label_set_text(GTK_LABEL(priv->organization), NULL);
    gtk_label_set_text(GTK_LABEL(priv->organizational_unit), NULL);
    gtk_label_set_text(GTK_LABEL(priv->serial_number), NULL);

    gtk_label_set_text(GTK_LABEL(priv->issuer_common_name), NULL);
    gtk_label_set_text(GTK_LABEL(priv->issuer_organization), NULL);
    gtk_label_set_text(GTK_LABEL(priv->issuer_organizational_unit), NULL);

    gtk_label_set_text(GTK_LABEL(priv->activation_time), NULL);
    gtk_label_set_text(GTK_LABEL(priv->expiration_time), NULL);

    gtk_label_set_text(GTK_LABEL(priv->sha1_fingerprint), NULL);
    gtk_label_set_text(GTK_LABEL(priv->md5_fingerprint), NULL);
  }
  else
  {
    inf_gtk_certificate_view_set_label_dn_by_oid(
      cert,
      GTK_LABEL(priv->common_name),
      GNUTLS_OID_X520_COMMON_NAME
    );

    inf_gtk_certificate_view_set_label_dn_by_oid(
      cert,
      GTK_LABEL(priv->organization),
      GNUTLS_OID_X520_ORGANIZATION_NAME
    );

    inf_gtk_certificate_view_set_label_dn_by_oid(
      cert,
      GTK_LABEL(priv->organizational_unit),
      GNUTLS_OID_X520_ORGANIZATIONAL_UNIT_NAME
    );

    value = inf_cert_util_get_serial_number(cert);
    inf_gtk_certificate_view_set_label(
      GTK_LABEL(priv->serial_number),
      value
    );
    g_free(value);

    inf_gtk_certificate_view_set_label_issuer_dn_by_oid(
      cert,
      GTK_LABEL(priv->issuer_common_name),
      GNUTLS_OID_X520_COMMON_NAME
    );

    inf_gtk_certificate_view_set_label_issuer_dn_by_oid(
      cert,
      GTK_LABEL(priv->issuer_organization),
      GNUTLS_OID_X520_ORGANIZATION_NAME
    );

    inf_gtk_certificate_view_set_label_issuer_dn_by_oid(
      cert,
      GTK_LABEL(priv->issuer_organizational_unit),
      GNUTLS_OID_X520_ORGANIZATIONAL_UNIT_NAME
    );

    value = inf_cert_util_get_activation_time(cert);
    inf_gtk_certificate_view_set_label(
      GTK_LABEL(priv->activation_time),
      value
    );
    g_free(value);

    value = inf_cert_util_get_expiration_time(cert);
    inf_gtk_certificate_view_set_label(
      GTK_LABEL(priv->expiration_time),
      value
    );
    g_free(value);

    value = inf_cert_util_get_fingerprint(cert, GNUTLS_DIG_SHA1);
    inf_gtk_certificate_view_set_label(
      GTK_LABEL(priv->sha1_fingerprint),
      value
    );
    g_free(value);

    value = inf_cert_util_get_fingerprint(cert, GNUTLS_DIG_MD5);
    inf_gtk_certificate_view_set_label(
      GTK_LABEL(priv->md5_fingerprint),
      value
    );
    g_free(value);
  }

  g_object_notify(G_OBJECT(view), "certificate");
}

/* vim:set et sw=2 ts=2: */
