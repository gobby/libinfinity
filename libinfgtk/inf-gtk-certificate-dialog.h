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
 * @INF_GTK_CERTIFICATE_DIALOG_CERT_HOSTNAME_MISMATCH: The hostname of the
 * machine connected to does not match the one from the certificate.
 * @INF_GTK_CERTIFICATE_DIALOG_CERT_ISSUER_NOT_KNOWN: The issuer of the
 * certificate is not trusted, i.e. is not in the list of trusted CAs.
 * @INF_GTK_CERTIFICATE_DIALOG_CERT_UNEXPECTED: For this host we expected a
 * different certificate. Maybe someone is eavesdropping on the connection.
 * @INF_GTK_CERTIFICATE_DIALOG_CERT_OLD_EXPIRED: If set, the previously pinned
 * certificate has expired.
 *
 * Various flags for why a certificate is not trusted.
 * #InfGtkCertificateDialog uses this information to show a corresponding
 * warning message to the user.
 */
typedef enum _InfGtkCertificateDialogFlags {
  INF_GTK_CERTIFICATE_DIALOG_CERT_HOSTNAME_MISMATCH  = 1 << 0,
  INF_GTK_CERTIFICATE_DIALOG_CERT_ISSUER_NOT_KNOWN   = 1 << 1,
  INF_GTK_CERTIFICATE_DIALOG_CERT_UNEXPECTED         = 1 << 2,
  INF_GTK_CERTIFICATE_DIALOG_CERT_OLD_EXPIRED        = 1 << 3
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
