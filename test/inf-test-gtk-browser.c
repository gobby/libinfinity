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

#include <libinftextgtk/inf-text-gtk-buffer.h>
#include <libinfgtk/inf-gtk-browser-view.h>
#include <libinfgtk/inf-gtk-browser-model.h>
#include <libinfgtk/inf-gtk-io.h>
#include <libinftext/inf-text-session.h>
#include <libinfinity/client/infc-session-proxy.h>
#include <libinfinity/common/inf-xmpp-manager.h>
#include <libinfinity/common/inf-discovery-avahi.h>
#include <libinfinity/common/inf-error.h>

#include <gtk/gtkmain.h>
#include <gtk/gtkscrolledwindow.h>
#include <gtk/gtktextview.h>
#include <gtk/gtkwindow.h>

typedef struct _InfTestGtkBrowserWindow InfTestGtkBrowserWindow;
struct _InfTestGtkBrowserWindow {
  GtkWidget* textview;
  InfTextGtkBuffer* buffer;
  InfcSessionProxy* proxy;
};

static InfSession*
inf_test_gtk_browser_session_new(InfIo* io,
                                 InfConnectionManager* manager,
                                 InfConnectionManagerGroup* sync_group,
                                 InfXmlConnection* sync_connection)
{
  InfTextGtkBuffer* buffer;
  GtkTextBuffer* textbuffer;
  InfTextSession* session;

  textbuffer = gtk_text_buffer_new(NULL);
  buffer = inf_text_gtk_buffer_new(textbuffer);

  session = inf_text_session_new(
    manager,
    INF_TEXT_BUFFER(buffer),
    io,
    sync_group,
    sync_connection
  );

  return INF_SESSION(session);
}

static const InfcNotePlugin INF_TEST_GTK_BROWSER_TEXT_PLUGIN = {
  "InfText",
  inf_test_gtk_browser_session_new
};

static void
set_error(InfTestGtkBrowserWindow* test,
          const gchar* prefix,
          const gchar* error_message)
{
  GtkTextBuffer* new_buffer;
  GtkTextIter iter;

  /* Note we cannot just write in the current buffer of the textview because
   * it is coupled with the InfTextGtkBuffer which would then try to send
   * the text insertion to the subscription group (and fail doing so,
   * probably). */
  new_buffer = gtk_text_buffer_new(NULL);
  gtk_text_buffer_get_start_iter(new_buffer, &iter);
  gtk_text_buffer_insert(new_buffer, &iter, prefix, -1);
  gtk_text_buffer_insert(new_buffer, &iter, ": ", 2);
  gtk_text_buffer_insert(new_buffer, &iter, error_message, -1);
  gtk_text_view_set_buffer(GTK_TEXT_VIEW(test->textview), new_buffer);
  g_object_unref(G_OBJECT(new_buffer));
}

static void
on_join_finished(InfcUserRequest* request,
                 InfUser* user,
                 gpointer user_data)
{
  InfTestGtkBrowserWindow* test;
  test = (InfTestGtkBrowserWindow*)user_data;

  inf_text_gtk_buffer_set_active_user(test->buffer, INF_TEXT_USER(user));
  gtk_text_view_set_editable(GTK_TEXT_VIEW(test->textview), TRUE);
}

static void
request_join(InfTestGtkBrowserWindow* test,
             const gchar* user_name);

static void
on_join_failed(InfcRequest* request,
               const GError* error,
               gpointer user_data)
{
  InfTestGtkBrowserWindow* test;
  gchar* new_name;

  test = (InfTestGtkBrowserWindow*)user_data;

  if(error->domain == inf_user_join_error_quark() &&
     error->code == INF_USER_JOIN_ERROR_NAME_IN_USE)
  {
    new_name = g_strdup_printf("%s%d", g_get_user_name(), 2);
    request_join(test, new_name);
    g_free(new_name);
  }
  else
  {
    set_error(test, "User join failed", error->message);
  }
}

static void
request_join(InfTestGtkBrowserWindow* test,
             const gchar* user_name)
{
  InfcUserRequest* request;
  InfAdoptedStateVector* v;
  GError* error;
  GParameter params[2] = { { "name", { 0 } }, { "vector", { 0 } } };

  g_value_init(&params[0].value, G_TYPE_STRING);
  g_value_init(&params[1].value, INF_ADOPTED_TYPE_STATE_VECTOR);

  g_value_set_static_string(&params[0].value, user_name);

  /* Use current state vector. Infinote should already do this. */
  v = inf_adopted_state_vector_copy(
    inf_adopted_algorithm_get_current(
      inf_adopted_session_get_algorithm(
        INF_ADOPTED_SESSION(infc_session_proxy_get_session(test->proxy))
      )
    )
  );

  g_value_take_boxed(&params[1].value, v);

  error = NULL;
  request = infc_session_proxy_join_user(test->proxy, params, 2, &error);

  if(request == NULL)
  {
    set_error(test, "Failed to request user join", error->message);
  }
  else
  {
    g_signal_connect_after(
      G_OBJECT(request),
      "failed",
      G_CALLBACK(on_join_failed),
      test
    );

    g_signal_connect_after(
      G_OBJECT(request),
      "finished",
      G_CALLBACK(on_join_finished),
      test
    );
  }
}

static void
on_synchronization_failed(InfSession* session,
                          InfXmlConnection* connection,
                          const GError* error,
                          gpointer user_data)
{
  InfTestGtkBrowserWindow* test;
  test = (InfTestGtkBrowserWindow*)user_data;
  set_error(test, "Synchronization failed", error->message);
}

static void
on_synchronization_complete(InfSession* session,
                            InfXmlConnection* connection,
                            gpointer user_data)
{
  InfTestGtkBrowserWindow* test;

  test = (InfTestGtkBrowserWindow*)user_data;
  request_join(test, g_get_user_name());
}

static void
on_text_window_destroy(GtkWindow* window,
                       gpointer user_data)
{
  /* TODO: Close session */
  g_slice_free(InfTestGtkBrowserWindow, user_data);
}

static void
on_subscribe_session(InfcBrowser* browser,
                     InfcBrowserIter* iter,
                     InfcSessionProxy* proxy,
                     gpointer user_data)
{
  GtkWidget* window;
  GtkWidget* scroll;
  GtkWidget* textview;
  InfSession* session;
  InfTextGtkBuffer* buffer;
  GtkTextBuffer* textbuffer;
  InfTestGtkBrowserWindow* test;

  session = infc_session_proxy_get_session(proxy);
  buffer = INF_TEXT_GTK_BUFFER(inf_session_get_buffer(session));
  textbuffer = inf_text_gtk_buffer_get_text_buffer(buffer);

  textview = gtk_text_view_new_with_buffer(textbuffer);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(textview), FALSE);
  gtk_widget_show(textview);

  scroll = gtk_scrolled_window_new(NULL, NULL);

  gtk_scrolled_window_set_policy(
    GTK_SCROLLED_WINDOW(scroll),
    GTK_POLICY_AUTOMATIC,
    GTK_POLICY_AUTOMATIC
  );

  gtk_container_add(GTK_CONTAINER(scroll), textview);
  gtk_widget_show(scroll);

  window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

  gtk_window_set_title(
    GTK_WINDOW(window),
    infc_browser_iter_get_name(browser, iter)
  );

  gtk_window_set_default_size(GTK_WINDOW(window), 400, 400);
  gtk_window_set_icon_name(GTK_WINDOW(window), "infinote");
  gtk_container_set_border_width(GTK_CONTAINER(window), 6);
  gtk_container_add(GTK_CONTAINER(window), scroll);
  gtk_widget_show(window);

  test = g_slice_new(InfTestGtkBrowserWindow);
  test->textview = textview;
  test->buffer = buffer;
  test->proxy = proxy;

  g_signal_connect_after(
    G_OBJECT(session),
    "synchronization-failed",
    G_CALLBACK(on_synchronization_failed),
    test
  );

  g_signal_connect_after(
    G_OBJECT(session),
    "synchronization-complete",
    G_CALLBACK(on_synchronization_complete),
    test
  );

  g_signal_connect(
    G_OBJECT(window),
    "destroy",
    G_CALLBACK(on_text_window_destroy),
    test
  );
}

static void
on_set_browser(InfGtkBrowserModel* model,
               GtkTreePath* path,
               GtkTreeIter* iter,
               InfcBrowser* browser,
               gpointer user_data)
{
  if(browser != NULL)
  {
    infc_browser_add_plugin(browser, &INF_TEST_GTK_BROWSER_TEXT_PLUGIN);

    g_signal_connect(
      G_OBJECT(browser),
      "subscribe-session",
      G_CALLBACK(on_subscribe_session),
      NULL
    );
  }
}

static void
on_destroy(GtkWindow* window,
           gpointer user_data)
{
  /* TODO: Destroy open text windows */
  gtk_main_quit();
}

int
main(int argc,
     char* argv[])
{
  InfGtkIo* io;
  InfConnectionManager* connection_manager;
#ifdef INFINOTE_HAVE_AVAHI
  InfXmppManager* xmpp_manager;
  InfDiscoveryAvahi* avahi;
#endif
  InfGtkBrowserModel* model;
  GtkWidget* view;
  GtkWidget* scroll;
  GtkWidget* window;

  gtk_init(&argc, &argv);
  gnutls_global_init();

  io = inf_gtk_io_new();
#ifdef INFINOTE_HAVE_AVAHI
  xmpp_manager = inf_xmpp_manager_new();
  avahi = inf_discovery_avahi_new(INF_IO(io), xmpp_manager, NULL, NULL);
  g_object_unref(G_OBJECT(xmpp_manager));
#endif

  connection_manager = inf_connection_manager_new();
  model = inf_gtk_browser_model_new(INF_IO(io), connection_manager);
  g_object_unref(G_OBJECT(connection_manager));
  g_object_unref(G_OBJECT(io));

  g_signal_connect(
    G_OBJECT(model),
    "set-browser",
    G_CALLBACK(on_set_browser),
    NULL
  );

#ifdef INFINOTE_HAVE_AVAHI
  inf_gtk_browser_model_add_discovery(model, INF_DISCOVERY(avahi));
  g_object_unref(G_OBJECT(avahi));
#endif

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
  gtk_widget_show(window);
  
  g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(on_destroy), NULL);

  gtk_main();
  return 0;
}

/* vim:set et sw=2 ts=2: */
