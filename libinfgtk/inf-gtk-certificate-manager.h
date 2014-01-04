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

#ifndef __INF_GTK_CERTIFICATE_MANAGER_H__
#define __INF_GTK_CERTIFICATE_MANAGER_H__

#include <libinfinity/common/inf-xmpp-manager.h>

#include <gtk/gtk.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_GTK_TYPE_CERTIFICATE_MANAGER                 (inf_gtk_certificate_manager_get_type())
#define INF_GTK_CERTIFICATE_MANAGER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_GTK_TYPE_CERTIFICATE_MANAGER, InfGtkCertificateManager))
#define INF_GTK_CERTIFICATE_MANAGER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_GTK_TYPE_CERTIFICATE_MANAGER, InfGtkCertificateManagerClass))
#define INF_GTK_IS_CERTIFICATE_MANAGER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_GTK_TYPE_CERTIFICATE_MANAGER))
#define INF_GTK_IS_CERTIFICATE_MANAGER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_GTK_TYPE_CERTIFICATE_MANAGER))
#define INF_GTK_CERTIFICATE_MANAGER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_GTK_TYPE_CERTIFICATE_MANAGER, InfGtkCertificateManagerClass))

typedef struct _InfGtkCertificateManager InfGtkCertificateManager;
typedef struct _InfGtkCertificateManagerClass InfGtkCertificateManagerClass;

struct _InfGtkCertificateManagerClass {
  GObjectClass parent_class;
};

struct _InfGtkCertificateManager {
  GObject parent;
};

GType
inf_gtk_certificate_manager_get_type(void) G_GNUC_CONST;

InfGtkCertificateManager*
inf_gtk_certificate_manager_new(GtkWindow* parent_window,
                                InfXmppManager* xmpp_manager,
                                const gchar* known_hosts_file);

G_END_DECLS

#endif /* __INF_GTK_CERTIFICATE_MANAGER_H__ */

/* vim:set et sw=2 ts=2: */
