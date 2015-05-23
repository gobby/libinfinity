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

#ifndef __INF_GTK_CONNECTION_VIEW_H__
#define __INF_GTK_CONNECTION_VIEW_H__

#include <libinfinity/common/inf-xmpp-connection.h>

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define INF_GTK_TYPE_CONNECTION_VIEW                 (inf_gtk_connection_view_get_type())
#define INF_GTK_CONNECTION_VIEW(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_GTK_TYPE_CONNECTION_VIEW, InfGtkConnectionView))
#define INF_GTK_CONNECTION_VIEW_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_GTK_TYPE_CONNECTION_VIEW, InfGtkConnectionViewClass))
#define INF_GTK_IS_CONNECTION_VIEW(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_GTK_TYPE_CONNECTION_VIEW))
#define INF_GTK_IS_CONNECTION_VIEW_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_GTK_TYPE_CONNECTION_VIEW))
#define INF_GTK_CONNECTION_VIEW_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_GTK_TYPE_CONNECTION_VIEW, InfGtkConnectionViewClass))

#define INF_GTK_TYPE_CONNECTION_VIEW_FLAGS           (inf_gtk_connection_view_flags_get_type())

typedef struct _InfGtkConnectionView InfGtkConnectionView;
typedef struct _InfGtkConnectionViewClass InfGtkConnectionViewClass;

/**
 * InfGtkConnectionViewClass:
 *
 * This structure does not contain any public fields.
 */
struct _InfGtkConnectionViewClass {
  /*< private >*/
  GtkGridClass parent_class;
};

/**
 * InfGtkConnectionView:
 *
 * #InfGtkConnectionView is an opaque data type. You should only access
 * it via the public API functions.
 */
struct _InfGtkConnectionView {
  /*< private >*/
  GtkGrid parent;
};

GType
inf_gtk_connection_view_get_type(void) G_GNUC_CONST;

GtkWidget*
inf_gtk_connection_view_new(void);

GtkWidget*
inf_gtk_connection_view_new_with_connection(InfXmppConnection* connection);

void
inf_gtk_connection_view_set_connection(InfGtkConnectionView* view,
                                       InfXmppConnection* connection);

G_END_DECLS

#endif /* __INF_GTK_CONNECTION_VIEW_H__ */

/* vim:set et sw=2 ts=2: */
