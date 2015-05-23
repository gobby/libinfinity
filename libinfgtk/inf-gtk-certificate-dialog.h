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
#include <libinfinity/common/inf-certificate-verify.h>

#include <gtk/gtk.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_GTK_TYPE_CERTIFICATE_DIALOG                 (inf_gtk_certificate_dialog_get_type())
#define INF_GTK_CERTIFICATE_DIALOG(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_GTK_TYPE_CERTIFICATE_DIALOG, InfGtkCertificateDialog))
#define INF_GTK_CERTIFICATE_DIALOG_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_GTK_TYPE_CERTIFICATE_DIALOG, InfGtkCertificateDialogClass))
#define INF_GTK_IS_CERTIFICATE_DIALOG(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_GTK_TYPE_CERTIFICATE_DIALOG))
#define INF_GTK_IS_CERTIFICATE_DIALOG_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_GTK_TYPE_CERTIFICATE_DIALOG))
#define INF_GTK_CERTIFICATE_DIALOG_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_GTK_TYPE_CERTIFICATE_DIALOG, InfGtkCertificateDialogClass))

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

GType
inf_gtk_certificate_dialog_get_type(void) G_GNUC_CONST;

InfGtkCertificateDialog*
inf_gtk_certificate_dialog_new(GtkWindow* parent,
                               GtkDialogFlags dialog_flags,
                               InfCertificateVerifyFlags verify_flags,
                               const gchar* hostname,
                               InfCertificateChain* certificate_chain,
                               gnutls_x509_crt_t pinned_certificate);

G_END_DECLS

#endif /* __INF_GTK_CERTIFICATE_DIALOG_H__ */

/* vim:set et sw=2 ts=2: */
