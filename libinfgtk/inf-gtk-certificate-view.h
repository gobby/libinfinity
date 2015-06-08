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

#ifndef __INF_GTK_CERTIFICATE_VIEW_H__
#define __INF_GTK_CERTIFICATE_VIEW_H__

#include <gtk/gtk.h>

#include <glib-object.h>

#include <unistd.h> /* Get ssize_t on MSVC, required by gnutls.h */
#include <gnutls/gnutls.h>

G_BEGIN_DECLS

#define INF_GTK_TYPE_CERTIFICATE_VIEW                 (inf_gtk_certificate_view_get_type())
#define INF_GTK_CERTIFICATE_VIEW(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_GTK_TYPE_CERTIFICATE_VIEW, InfGtkCertificateView))
#define INF_GTK_CERTIFICATE_VIEW_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_GTK_TYPE_CERTIFICATE_VIEW, InfGtkCertificateViewClass))
#define INF_GTK_IS_CERTIFICATE_VIEW(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_GTK_TYPE_CERTIFICATE_VIEW))
#define INF_GTK_IS_CERTIFICATE_VIEW_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_GTK_TYPE_CERTIFICATE_VIEW))
#define INF_GTK_CERTIFICATE_VIEW_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_GTK_TYPE_CERTIFICATE_VIEW, InfGtkCertificateViewClass))

#define INF_GTK_TYPE_CERTIFICATE_VIEW_FLAGS           (inf_gtk_certificate_view_flags_get_type())

typedef struct _InfGtkCertificateView InfGtkCertificateView;
typedef struct _InfGtkCertificateViewClass InfGtkCertificateViewClass;

struct _InfGtkCertificateViewClass {
  GtkGridClass parent_class;
};

struct _InfGtkCertificateView {
  GtkGrid parent;
};

GType
inf_gtk_certificate_view_get_type(void) G_GNUC_CONST;

GtkWidget*
inf_gtk_certificate_view_new(void);

GtkWidget*
inf_gtk_certificate_view_new_with_certificate(gnutls_x509_crt_t cert);

void
inf_gtk_certificate_view_set_certificate(InfGtkCertificateView* view,
                                         gnutls_x509_crt_t cert);

G_END_DECLS

#endif /* __INF_GTK_CERTIFICATE_VIEW_H__ */

/* vim:set et sw=2 ts=2: */
