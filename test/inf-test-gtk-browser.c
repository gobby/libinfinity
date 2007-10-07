/* infinote - Collaborative notetaking application
 * Copyright (C) 2007 Armin Burgmeier
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

#include <libinfgtk/inf-gtk-browser-view.h>
#include <libinfgtk/inf-gtk-browser-model.h>
#include <libinfgtk/inf-gtk-io.h>
#include <libinfinity/common/inf-xmpp-manager.h>
#include <libinfinity/common/inf-discovery-avahi.h>

#include <gtk/gtkmain.h>
#include <gtk/gtkscrolledwindow.h>
#include <gtk/gtkwindow.h>

extern int _gnutls_log_level;

static void
on_destroy(GtkWindow* window)
{
  gtk_main_quit();
}

int
main(int argc,
     char* argv[])
{
  InfGtkIo* io;
  InfXmppManager* xmpp_manager;
  InfConnectionManager* connection_manager;
  InfDiscoveryAvahi* avahi;
  InfGtkBrowserModel* model;
  GtkWidget* view;
  GtkWidget* scroll;
  GtkWidget* window;

  _gnutls_log_level = 9;
  gtk_init(&argc, &argv);
  gnutls_global_init();

  io = inf_gtk_io_new();
  xmpp_manager = inf_xmpp_manager_new();
  avahi = inf_discovery_avahi_new(INF_IO(io), xmpp_manager, NULL, NULL);
  g_object_unref(G_OBJECT(xmpp_manager));
  g_object_unref(G_OBJECT(io));

  connection_manager = inf_connection_manager_new();
  model = inf_gtk_browser_model_new(connection_manager);
  g_object_unref(G_OBJECT(connection_manager));

  inf_gtk_browser_model_add_discovery(model, INF_DISCOVERY(avahi));
  g_object_unref(G_OBJECT(avahi));

  view = inf_gtk_browser_view_new_with_model(model);
  g_object_unref(G_OBJECT(model));
  gtk_widget_show(view);

  scroll = gtk_scrolled_window_new(NULL, NULL);

  gtk_scrolled_window_set_policy(
    GTK_SCROLLED_WINDOW(scroll),
    GTK_POLICY_AUTOMATIC,
    GTK_POLICY_AUTOMATIC
  );
  
  gtk_scrolled_window_set_shadow_type(
    GTK_SCROLLED_WINDOW(scroll),
    GTK_SHADOW_IN
  );

  gtk_container_add(GTK_CONTAINER(scroll), view);
  gtk_widget_show(scroll);

  window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(window),"Infinote Browser");
  gtk_window_set_default_size(GTK_WINDOW(window), 400, 400);
  gtk_window_set_icon_name(GTK_WINDOW(window), "infinote");
  gtk_container_set_border_width(GTK_CONTAINER(window), 6);
  gtk_container_add(GTK_CONTAINER(window), scroll);
  gtk_widget_show_all(window);
  
  g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(on_destroy), NULL);

  gtk_main();
  return 0;
}

/* vim:set et sw=2 ts=2: */
