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
#include <libinfgtk/inf-gtk-browser-store.h>
#include <libinfgtk/inf-gtk-io.h>
#include <libinftext/inf-text-session.h>
#include <libinfinity/client/infc-session-proxy.h>
#include <libinfinity/common/inf-xmpp-manager.h>
#include <libinfinity/common/inf-discovery-avahi.h>
#include <libinfinity/common/inf-error.h>

#include <gtk/gtkmain.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtkhbbox.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtkscrolledwindow.h>
#include <gtk/gtktextview.h>
#include <gtk/gtkwindow.h>
#include <gtk/gtkstock.h>

typedef struct _InfTestGtkBrowserWindow InfTestGtkBrowserWindow;
struct _InfTestGtkBrowserWindow {
  GtkWidget* textview;
  GtkWidget* undo_button;
  GtkWidget* redo_button;

  InfTextGtkBuffer* buffer;
  InfcSessionProxy* proxy;
  InfUser* user;
};

static InfSession*
inf_test_gtk_browser_session_new(InfIo* io,
                                 InfConnectionManager* manager,
                                 InfConnectionManagerGroup* sync_group,
                                 InfXmlConnection* sync_connection,
                                 gpointer user_data)
{
  InfTextGtkBuffer* buffer;
  InfUserTable* user_table;
  GtkTextBuffer* textbuffer;
  InfTextSession* session;

  textbuffer = gtk_text_buffer_new(NULL);
  user_table = inf_user_table_new();
  buffer = inf_text_gtk_buffer_new(textbuffer, user_table);

  session = inf_text_session_new_with_user_table(
    manager,
    INF_TEXT_BUFFER(buffer),
    io,
    user_table,
    sync_group,
    sync_connection
  );

  return INF_SESSION(session);
}

static const InfcNotePlugin INF_TEST_GTK_BROWSER_TEXT_PLUGIN = {
  NULL,
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
on_undo_button_clicked(GtkButton* button,
                       gpointer user_data)
{
  InfTestGtkBrowserWindow* test;
  InfAdoptedSession* session;

  test = (InfTestGtkBrowserWindow*)user_data;
  session = INF_ADOPTED_SESSION(infc_session_proxy_get_session(test->proxy));
  inf_adopted_session_undo(session, INF_ADOPTED_USER(test->user));
}

static void
on_redo_button_clicked(GtkButton* button,
                       gpointer user_data)
{
  InfTestGtkBrowserWindow* test;
  InfAdoptedSession* session;

  test = (InfTestGtkBrowserWindow*)user_data;
  session = INF_ADOPTED_SESSION(infc_session_proxy_get_session(test->proxy));
  inf_adopted_session_redo(session, INF_ADOPTED_USER(test->user));
}

static void
on_can_undo_changed(InfAdoptedAlgorithm* algorithm,
                    InfAdoptedUser* user,
                    gboolean can_undo,
                    gpointer user_data)
{
  InfTestGtkBrowserWindow* test;
  test = (InfTestGtkBrowserWindow*)user_data;

  gtk_widget_set_sensitive(test->undo_button, can_undo);
}

static void
on_can_redo_changed(InfAdoptedAlgorithm* algorithm,
                    InfAdoptedUser* user,
                    gboolean can_redo,
                    gpointer user_data)
{
  InfTestGtkBrowserWindow* test;
  test = (InfTestGtkBrowserWindow*)user_data;

  gtk_widget_set_sensitive(test->redo_button, can_redo);
}

static void
on_join_finished(InfcUserRequest* request,
                 InfUser* user,
                 gpointer user_data)
{
  InfTestGtkBrowserWindow* test;
  InfAdoptedSession* session;
  InfAdoptedAlgorithm* algorithm;
  gboolean undo;
  gboolean redo;

  test = (InfTestGtkBrowserWindow*)user_data;

  inf_text_gtk_buffer_set_active_user(test->buffer, INF_TEXT_USER(user));
  gtk_text_view_set_editable(GTK_TEXT_VIEW(test->textview), TRUE);

  test->user = user;

  session = INF_ADOPTED_SESSION(infc_session_proxy_get_session(test->proxy));
  algorithm = inf_adopted_session_get_algorithm(session);

  undo = inf_adopted_algorithm_can_undo(algorithm, INF_ADOPTED_USER(user));
  redo = inf_adopted_algorithm_can_redo(algorithm, INF_ADOPTED_USER(user));

  gtk_widget_set_sensitive(test->undo_button, undo);
  gtk_widget_set_sensitive(test->redo_button, redo);
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

  if(error->domain == inf_user_error_quark() &&
     error->code == INF_USER_ERROR_NAME_IN_USE)
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
  GtkTextBuffer* buffer;
  GtkTextMark* mark;
  GtkTextIter iter;
  GParameter params[3] = {
    { "name", { 0 } },
    { "vector", { 0 } },
    { "caret-position", { 0 } }
  };

  g_value_init(&params[0].value, G_TYPE_STRING);
  g_value_init(&params[1].value, INF_ADOPTED_TYPE_STATE_VECTOR);
  g_value_init(&params[2].value, G_TYPE_UINT);

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

  buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(test->textview));
  mark = gtk_text_buffer_get_insert(buffer);
  gtk_text_buffer_get_iter_at_mark(buffer, &iter, mark);
  g_value_set_uint(&params[2].value, gtk_text_iter_get_offset(&iter));

  error = NULL;
  request = infc_session_proxy_join_user(test->proxy, params, 3, &error);

  /* TODO: Free GValues? */

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
  InfAdoptedAlgorithm* algorithm;

  test = (InfTestGtkBrowserWindow*)user_data;
  session = infc_session_proxy_get_session(test->proxy);
  algorithm = inf_adopted_session_get_algorithm(INF_ADOPTED_SESSION(session));

  g_signal_connect(
    G_OBJECT(algorithm),
    "can-undo-changed",
    G_CALLBACK(on_can_undo_changed),
    test
  );

  g_signal_connect(
    G_OBJECT(algorithm),
    "can-redo-changed",
    G_CALLBACK(on_can_redo_changed),
    test
  );

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
  GtkWidget* vbox;
  GtkWidget* hbox;
  GtkWidget* undo_button;
  GtkWidget* redo_button;
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
  gtk_scrolled_window_set_shadow_type(
    GTK_SCROLLED_WINDOW(scroll),
    GTK_SHADOW_IN
  );

  gtk_scrolled_window_set_policy(
    GTK_SCROLLED_WINDOW(scroll),
    GTK_POLICY_AUTOMATIC,
    GTK_POLICY_AUTOMATIC
  );

  gtk_container_add(GTK_CONTAINER(scroll), textview);
  gtk_widget_show(scroll);

  undo_button = gtk_button_new_from_stock(GTK_STOCK_UNDO);
  redo_button = gtk_button_new_from_stock(GTK_STOCK_REDO);
  gtk_widget_set_sensitive(undo_button, FALSE);
  gtk_widget_set_sensitive(redo_button, FALSE);
  gtk_widget_show(undo_button);
  gtk_widget_show(redo_button);

  hbox = gtk_hbutton_box_new();
  gtk_box_pack_start(GTK_BOX(hbox), undo_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(hbox), redo_button, FALSE, FALSE, 0);
  gtk_widget_show(hbox);

  vbox = gtk_vbox_new(FALSE, 6);
  gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show(vbox);

  window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

  gtk_window_set_title(
    GTK_WINDOW(window),
    infc_browser_iter_get_name(browser, iter)
  );

  gtk_window_set_default_size(GTK_WINDOW(window), 400, 400);
  gtk_window_set_icon_name(GTK_WINDOW(window), "infinote");
  gtk_container_set_border_width(GTK_CONTAINER(window), 6);
  gtk_container_add(GTK_CONTAINER(window), vbox);
  gtk_widget_show(window);

  test = g_slice_new(InfTestGtkBrowserWindow);
  test->textview = textview;
  test->undo_button = undo_button;
  test->redo_button = redo_button;
  test->buffer = buffer;
  test->proxy = proxy;
  test->user = NULL;

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

  g_signal_connect(
    G_OBJECT(undo_button),
    "clicked",
    G_CALLBACK(on_undo_button_clicked),
    test
  );

  g_signal_connect(
    G_OBJECT(redo_button),
    "clicked",
    G_CALLBACK(on_redo_button_clicked),
    test
  );
}

static void
on_activate(InfGtkBrowserView* view,
            GtkTreeIter* iter,
            gpointer user_data)
{
  InfcBrowser* browser;
  InfcBrowserIter* browser_iter;

  gtk_tree_model_get(
    GTK_TREE_MODEL(inf_gtk_browser_view_get_model(view)),
    iter,
    INF_GTK_BROWSER_MODEL_COL_BROWSER, &browser,
    INF_GTK_BROWSER_MODEL_COL_NODE, &browser_iter,
    -1
  );

  /* Subscribe, if possible and not already */
  if(!infc_browser_iter_get_session(browser, browser_iter) &&
     !infc_browser_iter_get_subscribe_request(browser, browser_iter) &&
     infc_browser_iter_get_plugin(browser, browser_iter) != NULL)
  {
    infc_browser_iter_subscribe_session(browser, browser_iter);
  }

  infc_browser_iter_free(browser_iter);
  g_object_unref(browser);
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

    g_signal_connect_after(
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
#ifdef LIBINFINITY_HAVE_AVAHI
  InfXmppManager* xmpp_manager;
  InfDiscoveryAvahi* avahi;
#endif
  InfGtkBrowserStore* store;
  GtkWidget* view;
  GtkWidget* scroll;
  GtkWidget* window;

  gtk_init(&argc, &argv);
  gnutls_global_init();

  io = inf_gtk_io_new();
#ifdef LIBINFINITY_HAVE_AVAHI
  xmpp_manager = inf_xmpp_manager_new();
  avahi = inf_discovery_avahi_new(INF_IO(io), xmpp_manager, NULL, NULL, NULL);
  g_object_unref(G_OBJECT(xmpp_manager));
#endif

  connection_manager = inf_connection_manager_new();
  store = inf_gtk_browser_store_new(INF_IO(io), connection_manager, NULL);
  g_object_unref(G_OBJECT(connection_manager));
  g_object_unref(G_OBJECT(io));

  g_signal_connect(
    G_OBJECT(store),
    "set-browser",
    G_CALLBACK(on_set_browser),
    NULL
  );

#ifdef LIBINFINITY_HAVE_AVAHI
  inf_gtk_browser_store_add_discovery(store, INF_DISCOVERY(avahi));
  g_object_unref(G_OBJECT(avahi));
#endif

  view = inf_gtk_browser_view_new_with_model(INF_GTK_BROWSER_MODEL(store));
  g_object_unref(G_OBJECT(store));
  gtk_widget_show(view);

  g_signal_connect(
    G_OBJECT(view),
    "activate",
    G_CALLBACK(on_activate),
    NULL
  );

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
