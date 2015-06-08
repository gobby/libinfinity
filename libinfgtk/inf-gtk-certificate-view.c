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

#include <libinfgtk/inf-gtk-certificate-view.h>
#include <libinfinity/common/inf-cert-util.h>
#include <libinfinity/inf-i18n.h>

#include <gnutls/x509.h>
#include <time.h>

typedef struct _InfGtkCertificateViewPrivate InfGtkCertificateViewPrivate;
struct _InfGtkCertificateViewPrivate {
  gnutls_x509_crt_t certificate;

  GtkWidget* subject_common_name;
  GtkWidget* subject_organization;
  GtkWidget* subject_organizational_unit;
  GtkWidget* subject_serial_number;

  GtkWidget* issuer_common_name;
  GtkWidget* issuer_organization;
  GtkWidget* issuer_organizational_unit;

  GtkWidget* activation_time;
  GtkWidget* expiration_time;

  GtkWidget* sha1_fingerprint;
  GtkWidget* sha256_fingerprint;
  GtkWidget* signature_algorithm;
};

enum {
  PROP_0,

  PROP_CERTIFICATE
};

#define INF_GTK_CERTIFICATE_VIEW_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_GTK_TYPE_CERTIFICATE_VIEW, InfGtkCertificateViewPrivate))

G_DEFINE_TYPE_WITH_CODE(InfGtkCertificateView, inf_gtk_certificate_view, GTK_TYPE_GRID,
  G_ADD_PRIVATE(InfGtkCertificateView))

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
inf_gtk_certificate_view_init(InfGtkCertificateView* view)
{
  InfGtkCertificateViewPrivate* priv;
  PangoFontDescription* monospace_desc;
  gint size;

  priv = INF_GTK_CERTIFICATE_VIEW_PRIVATE(view);

  priv->certificate = NULL;

  gtk_widget_init_template(GTK_WIDGET(view));
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
inf_gtk_certificate_view_class_init(
  InfGtkCertificateViewClass* certificate_view_class)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(certificate_view_class);

  object_class->set_property = inf_gtk_certificate_view_set_property;
  object_class->get_property = inf_gtk_certificate_view_get_property;

  gtk_widget_class_set_template_from_resource(
    GTK_WIDGET_CLASS(certificate_view_class),
    "/de/0x539/libinfgtk/ui/infgtkcertificateview.ui"
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(certificate_view_class),
    InfGtkCertificateView,
    subject_common_name
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(certificate_view_class),
    InfGtkCertificateView,
    subject_organization
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(certificate_view_class),
    InfGtkCertificateView,
    subject_organizational_unit
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(certificate_view_class),
    InfGtkCertificateView,
    subject_serial_number
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(certificate_view_class),
    InfGtkCertificateView,
    issuer_common_name
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(certificate_view_class),
    InfGtkCertificateView,
    issuer_organization
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(certificate_view_class),
    InfGtkCertificateView,
    issuer_organizational_unit
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(certificate_view_class),
    InfGtkCertificateView,
    activation_time
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(certificate_view_class),
    InfGtkCertificateView,
    expiration_time
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(certificate_view_class),
    InfGtkCertificateView,
    sha1_fingerprint
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(certificate_view_class),
    InfGtkCertificateView,
    sha256_fingerprint
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(certificate_view_class),
    InfGtkCertificateView,
    signature_algorithm
  );

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

/*
 * Public API.
 */

/**
 * inf_gtk_certificate_view_new: (constructor)
 *
 * Creates a new #InfGtkCertificateView. To show a certificate, use
 * inf_gtk_certificate_view_set_certificate() on the returned widget.
 *
 * Returns: (transfer floating): A new #InfGtkCertificateView.
 */
GtkWidget*
inf_gtk_certificate_view_new(void)
{
  GObject* object;
  object = g_object_new(INF_GTK_TYPE_CERTIFICATE_VIEW, NULL);
  return GTK_WIDGET(object);
}

/**
 * inf_gtk_certificate_view_new_with_certificate: (constructor)
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
 * Returns: (transfer floating): A new #InfGtkCertificateView.
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
  int algo;

  g_return_if_fail(INF_GTK_IS_CERTIFICATE_VIEW(view));

  priv = INF_GTK_CERTIFICATE_VIEW_PRIVATE(view);
  priv->certificate = cert;

  if(cert == NULL)
  {
    gtk_label_set_text(GTK_LABEL(priv->subject_common_name), NULL);
    gtk_label_set_text(GTK_LABEL(priv->subject_organization), NULL);
    gtk_label_set_text(GTK_LABEL(priv->subject_organizational_unit), NULL);
    gtk_label_set_text(GTK_LABEL(priv->subject_serial_number), NULL);

    gtk_label_set_text(GTK_LABEL(priv->issuer_common_name), NULL);
    gtk_label_set_text(GTK_LABEL(priv->issuer_organization), NULL);
    gtk_label_set_text(GTK_LABEL(priv->issuer_organizational_unit), NULL);

    gtk_label_set_text(GTK_LABEL(priv->activation_time), NULL);
    gtk_label_set_text(GTK_LABEL(priv->expiration_time), NULL);

    gtk_label_set_text(GTK_LABEL(priv->sha1_fingerprint), NULL);
    gtk_label_set_text(GTK_LABEL(priv->sha256_fingerprint), NULL);

    gtk_label_set_text(GTK_LABEL(priv->signature_algorithm), NULL);
  }
  else
  {
    inf_gtk_certificate_view_set_label_dn_by_oid(
      cert,
      GTK_LABEL(priv->subject_common_name),
      GNUTLS_OID_X520_COMMON_NAME
    );

    inf_gtk_certificate_view_set_label_dn_by_oid(
      cert,
      GTK_LABEL(priv->subject_organization),
      GNUTLS_OID_X520_ORGANIZATION_NAME
    );

    inf_gtk_certificate_view_set_label_dn_by_oid(
      cert,
      GTK_LABEL(priv->subject_organizational_unit),
      GNUTLS_OID_X520_ORGANIZATIONAL_UNIT_NAME
    );

    value = inf_cert_util_get_serial_number(cert);
    inf_gtk_certificate_view_set_label(
      GTK_LABEL(priv->subject_serial_number),
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

    value = inf_cert_util_get_fingerprint(cert, GNUTLS_DIG_SHA256);
    inf_gtk_certificate_view_set_label(
      GTK_LABEL(priv->sha256_fingerprint),
      value
    );
    g_free(value);

    algo = gnutls_x509_crt_get_signature_algorithm(cert);
    if(algo < 0)
    {
      inf_gtk_certificate_view_set_label(
        GTK_LABEL(priv->signature_algorithm),
        gnutls_strerror(algo)
      );
    }
    else
    {
      inf_gtk_certificate_view_set_label(
        GTK_LABEL(priv->signature_algorithm),
        gnutls_sign_get_name(algo)
      );
    }
  }

  g_object_notify(G_OBJECT(view), "certificate");
}

/* vim:set et sw=2 ts=2: */
