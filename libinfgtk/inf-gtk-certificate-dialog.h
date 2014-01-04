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

#ifndef __INF_GTK_CERTIFICATE_DIALOG_H__
#define __INF_GTK_CERTIFICATE_DIALOG_H__

#include <libinfinity/common/inf-certificate-chain.h>

#include <gtk/gtk.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_GTK_TYPE_CERTIFICATE_DIALOG                 (inf_gtk_certificate_dialog_get_type())
#define INF_GTK_CERTIFICATE_DIALOG(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_GTK_TYPE_CERTIFICATE_DIALOG, InfGtkCertificateDialog))
#define INF_GTK_CERTIFICATE_DIALOG_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_GTK_TYPE_CERTIFICATE_DIALOG, InfGtkCertificateDialogClass))
#define INF_GTK_IS_CERTIFICATE_DIALOG(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_GTK_TYPE_CERTIFICATE_DIALOG))
#define INF_GTK_IS_CERTIFICATE_DIALOG_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_GTK_TYPE_CERTIFICATE_DIALOG))
#define INF_GTK_CERTIFICATE_DIALOG_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_GTK_TYPE_CERTIFICATE_DIALOG, InfGtkCertificateDialogClass))

#define INF_GTK_TYPE_CERTIFICATE_DIALOG_FLAGS           (inf_gtk_certificate_dialog_flags_get_type())

typedef struct _InfGtkCertificateDialog InfGtkCertificateDialog;
typedef struct _InfGtkCertificateDialogClass InfGtkCertificateDialogClass;

/**
 * InfGtkCertificateDialogClass:
 *
 * This structure does not contain any public fields.
 */
struct _InfGtkCertificateDialogClass {
  /*< private >*/
  GtkDialogClass parent_class;
};

/**
 * InfGtkCertificateDialog:
 *
 * #InfGtkCertificateDialog is an opaque data type. You should only access
 * it via the public API functions.
 */
struct _InfGtkCertificateDialog {
  /*< private >*/
  GtkDialog parent;
};

/**
 * InfGtkCertificateDialogFlags:
 * @INF_GTK_CERTIFICATE_DIALOG_CERT_NOT_ACTIVATED: The certificate is not
 * valid yet, i.e. the certificate start date lies in the future.
 * @INF_GTK_CERTIFICATE_DIALOG_CERT_EXPIRED: The certificate is expired,
 * i.e. its end date lies in the past.
 * @INF_GTK_CERTIFICATE_DIALOG_CERT_HOSTNAME_MISMATCH: The hostname of the
 * machine connected to does not match the one from the certificate.
 * @INF_GTK_CERTIFICATE_DIALOG_CERT_INVALID: The certificate is invalid, i.e.
 * has an invalid signature.
 * @INF_GTK_CERTIFICATE_DIALOG_CERT_ISSUER_NOT_TRUSTED: The issuer of the
 * certificate is not trusted, i.e. is not in the list of trusted CAs.
 * @INF_GTK_CERTIFICATE_DIALOG_CERT_CHANGED: The certificate has changed
 * since the last connection to the same host.
 * @INF_GTK_CERTIFICATE_DIALOG_CERT_OLD_EXPIRED: This can only be set when
 * #INF_GTK_CERTIFICATE_DIALOG_CERT_CHANGED is set as well. In this case it
 * means that the previous certificate of the host is expired, which is a
 * good reason why we see a new certificate.
 *
 * Various flags for why a certificate is not trusted.
 * #InfGtkCertificateDialog uses this information to show a corresponding
 * warning message to the user.
 */
typedef enum _InfGtkCertificateDialogFlags {
  INF_GTK_CERTIFICATE_DIALOG_CERT_NOT_ACTIVATED      = 1 << 0,
  INF_GTK_CERTIFICATE_DIALOG_CERT_EXPIRED            = 1 << 1,
  INF_GTK_CERTIFICATE_DIALOG_CERT_HOSTNAME_MISMATCH  = 1 << 2,
  INF_GTK_CERTIFICATE_DIALOG_CERT_INVALID            = 1 << 3,
  INF_GTK_CERTIFICATE_DIALOG_CERT_ISSUER_NOT_TRUSTED = 1 << 4,
  INF_GTK_CERTIFICATE_DIALOG_CERT_CHANGED            = 1 << 5,
  /* only valid when CERT_CHANGED is also set */
  INF_GTK_CERTIFICATE_DIALOG_CERT_OLD_EXPIRED        = 1 << 6
} InfGtkCertificateDialogFlags;

GType
inf_gtk_certificate_dialog_flags_get_type(void) G_GNUC_CONST;

GType
inf_gtk_certificate_dialog_get_type(void) G_GNUC_CONST;

InfGtkCertificateDialog*
inf_gtk_certificate_dialog_new(GtkWindow* parent,
                               GtkDialogFlags dialog_flags,
                               InfGtkCertificateDialogFlags certificate_flags,
                               const gchar* hostname,
                               InfCertificateChain* certificate_chain);

G_END_DECLS

#endif /* __INF_GTK_CERTIFICATE_DIALOG_H__ */

/* vim:set et sw=2 ts=2: */
