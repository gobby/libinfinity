/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2010 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INF_GTK_BROWSER_STORE_H__
#define __INF_GTK_BROWSER_STORE_H__

#include <libinfinity/client/infc-browser.h>
#include <libinfinity/common/inf-discovery.h>
#include <libinfinity/common/inf-xml-connection.h>
#include <libinfinity/communication/inf-communication-manager.h>

#include <gtk/gtk.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define INF_GTK_TYPE_BROWSER_STORE                 (inf_gtk_browser_store_get_type())
#define INF_GTK_BROWSER_STORE(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_GTK_TYPE_BROWSER_STORE, InfGtkBrowserStore))
#define INF_GTK_BROWSER_STORE_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_GTK_TYPE_BROWSER_STORE, InfGtkBrowserStoreClass))
#define INF_GTK_IS_BROWSER_STORE(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_GTK_TYPE_BROWSER_STORE))
#define INF_GTK_IS_BROWSER_STORE_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_GTK_TYPE_BROWSER_STORE))
#define INF_GTK_BROWSER_STORE_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_GTK_TYPE_BROWSER_STORE, InfGtkBrowserStoreClass))

typedef struct _InfGtkBrowserStore InfGtkBrowserStore;
typedef struct _InfGtkBrowserStoreClass InfGtkBrowserStoreClass;

struct _InfGtkBrowserStoreClass {
  GObjectClass parent_class;
};

struct _InfGtkBrowserStore {
  GObject parent;
};

GType
inf_gtk_browser_store_get_type(void) G_GNUC_CONST;

InfGtkBrowserStore*
inf_gtk_browser_store_new(InfIo* io,
                          InfCommunicationManager* comm_manager);

void
inf_gtk_browser_store_add_discovery(InfGtkBrowserStore* store,
                                    InfDiscovery* discovery);

void
inf_gtk_browser_store_add_connection(InfGtkBrowserStore* store,
                                     InfXmlConnection* connection,
                                     const gchar* name);

void
inf_gtk_browser_store_remove_connection(InfGtkBrowserStore* store,
                                        InfXmlConnection* connection);

void
inf_gtk_browser_store_clear_connection_error(InfGtkBrowserStore* store,
                                             InfXmlConnection* connection);

void
inf_gtk_browser_store_set_connection_name(InfGtkBrowserStore* store,
                                          InfXmlConnection* connection,
                                          const gchar* name);

G_END_DECLS

#endif /* __INF_GTK_BROWSER_STORE_H__ */

/* vim:set et sw=2 ts=2: */
