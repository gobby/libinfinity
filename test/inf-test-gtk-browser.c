/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2013 Armin Burgmeier <armin@arbur.net>
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

#include <libinftextgtk/inf-text-gtk-buffer.h>
#include <libinftextgtk/inf-text-gtk-view.h>
#include <libinftextgtk/inf-text-gtk-viewport.h>
#include <libinfgtk/inf-gtk-browser-view.h>
#include <libinfgtk/inf-gtk-browser-store.h>
#include <libinfgtk/inf-gtk-chat.h>
#include <libinfgtk/inf-gtk-io.h>
#include <libinftext/inf-text-session.h>
#include <libinfinity/client/infc-session-proxy.h>
#include <libinfinity/common/inf-xmpp-manager.h>
#include <libinfinity/common/inf-discovery-avahi.h>
#include <libinfinity/common/inf-chat-session.h>
#include <libinfinity/common/inf-error.h>
#include <libinfinity/common/inf-protocol.h>
#include <libinfinity/inf-signals.h>
#include <libinfinity/common/inf-init.h>

#include <gtk/gtk.h>

typedef struct _InfTestGtkBrowserWindow InfTestGtkBrowserWindow;
struct _InfTestGtkBrowserWindow {
  GtkWidget* textview;
  GtkWidget* undo_button;
  GtkWidget* redo_button;

  InfTextGtkBuffer* buffer;
  InfTextGtkView* view;
  InfTextGtkViewport* viewport;
  InfSessionProxy* proxy;
  InfUser* user;
  InfUserRequest* request;
};

typedef struct _InfTestGtkBrowserChatWindow InfTestGtkBrowserChatWindow;
struct _InfTestGtkBrowserChatWindow {
  GtkWidget* chat;
  GtkWidget* status;

  InfChatBuffer* buffer;
  InfSessionProxy* proxy;
  InfUser* user;
  InfUserRequest* request;
};

static InfSession*
inf_test_gtk_browser_session_new(InfIo* io,
                                 InfCommunicationManager* manager,
                                 InfSessionStatus status,
                                 InfCommunicationJoinedGroup* sync_group,
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
    status,
    INF_COMMUNICATION_GROUP(sync_group),
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
  InfSession* session;

  test = (InfTestGtkBrowserWindow*)user_data;
  g_object_get(G_OBJECT(test->proxy), "session", &session, NULL);

  inf_adopted_session_undo(
    INF_ADOPTED_SESSION(session),
    INF_ADOPTED_USER(test->user),
    1
  );

  g_object_unref(session);
}

static void
on_redo_button_clicked(GtkButton* button,
                       gpointer user_data)
{
  InfTestGtkBrowserWindow* test;
  InfSession* session;

  test = (InfTestGtkBrowserWindow*)user_data;
  g_object_get(G_OBJECT(test->proxy), "session", &session, NULL);

  inf_adopted_session_redo(
    INF_ADOPTED_SESSION(session),
    INF_ADOPTED_USER(test->user),
    1
  );

  g_object_unref(session);
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
request_chat_join(InfTestGtkBrowserChatWindow* test,
                  const gchar* user_name);

static void
on_chat_join_finished(InfUserRequest* request,
                      InfUser* user,
                      const GError* error,
                      gpointer user_data)
{
  InfTestGtkBrowserChatWindow* test;
  gchar* new_name;
  gchar* text;

  test = (InfTestGtkBrowserChatWindow*)user_data;

  if(test->request != NULL)
  {
    g_object_unref(test->request);
    test->request = NULL;
  }

  if(error == NULL)
  {
    inf_gtk_chat_set_active_user(INF_GTK_CHAT(test->chat), user);

    text = g_strdup_printf("Joined as %s", inf_user_get_name(user));
    gtk_label_set_text(GTK_LABEL(test->status), text);
    g_free(text);

    test->user = user;
    g_object_ref(user);

    /* Unfortunately, gtk_widget_grab_focus(test->chat) +
     * gtk_container_set_focus_child() in inf_gtk_chat_set_active_user() does
     * not do the job which is why I added this crappy API. */
    gtk_widget_grab_focus(inf_gtk_chat_get_entry(INF_GTK_CHAT(test->chat)));
  }
  else
  {
    if(error->domain == inf_user_error_quark() &&
       error->code == INF_USER_ERROR_NAME_IN_USE)
    {
      new_name = g_strdup_printf("%s%d", g_get_user_name(), rand());
      request_chat_join(test, new_name);
      g_free(new_name);
    }
    else
    {
      text = g_strdup_printf("User join failed: %s", error->message);
      gtk_label_set_text(GTK_LABEL(test->status), text);
      g_free(text);
    }
  }
}

static void
request_join(InfTestGtkBrowserWindow* test,
             const gchar* user_name);

static void
on_join_finished(InfUserRequest* request,
                 InfUser* user,
                 const GError* error,
                 gpointer user_data)
{
  InfTestGtkBrowserWindow* test;
  InfSession* session;
  InfAdoptedAlgorithm* algorithm;
  gboolean undo;
  gboolean redo;
  gchar* new_name;

  test = (InfTestGtkBrowserWindow*)user_data;

  if(test->request != NULL)
  {
    g_object_unref(test->request);
    test->request = NULL;
  }

  if(error == NULL)
  {
    inf_text_gtk_buffer_set_active_user(test->buffer, INF_TEXT_USER(user));
    inf_text_gtk_view_set_active_user(test->view, INF_TEXT_USER(user));
    inf_text_gtk_viewport_set_active_user(
      test->viewport,
      INF_TEXT_USER(user)
    );
    gtk_text_view_set_editable(GTK_TEXT_VIEW(test->textview), TRUE);

    test->user = user;
    g_object_ref(user);

    g_object_get(G_OBJECT(test->proxy), "session", &session, NULL);
    algorithm =
      inf_adopted_session_get_algorithm(INF_ADOPTED_SESSION(session));
    g_object_unref(session);

    undo = inf_adopted_algorithm_can_undo(algorithm, INF_ADOPTED_USER(user));
    redo = inf_adopted_algorithm_can_redo(algorithm, INF_ADOPTED_USER(user));

    gtk_widget_set_sensitive(test->undo_button, undo);
    gtk_widget_set_sensitive(test->redo_button, redo);
  }
  else
  {
    if(error->domain == inf_user_error_quark() &&
       error->code == INF_USER_ERROR_NAME_IN_USE)
    {
      new_name = g_strdup_printf("%s%d", g_get_user_name(), rand());
      request_join(test, new_name);
      g_free(new_name);
    }
    else
    {
      set_error(test, "User join failed", error->message);
    }
  }
}

static void
request_chat_join(InfTestGtkBrowserChatWindow* test,
                  const gchar* user_name)
{
  InfUserRequest* request;
  gchar* text;

  GParameter params[1] = { { "name", { 0 } } };
  g_value_init(&params[0].value, G_TYPE_STRING);
  g_value_set_static_string(&params[0].value, user_name);

  text = g_strdup_printf("Requesting user join for %s", user_name);
  gtk_label_set_text(GTK_LABEL(test->status), text);
  g_free(text);

  request = inf_session_proxy_join_user(
    test->proxy,
    1,
    params,
    on_chat_join_finished,
    test
  );

  g_value_unset(&params[0].value);

  if(request != NULL)
  {
    g_assert(test->request == NULL);
    test->request = request;
    g_object_ref(test->request);
  }
}

static void
request_join(InfTestGtkBrowserWindow* test,
             const gchar* user_name)
{
  InfUserRequest* request;
  InfSession* session;
  InfAdoptedStateVector* v;
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

  g_object_get(G_OBJECT(test->proxy), "session", &session, NULL);

  /* Use current state vector. Infinote should already do this. */
  v = inf_adopted_state_vector_copy(
    inf_adopted_algorithm_get_current(
      inf_adopted_session_get_algorithm(INF_ADOPTED_SESSION(session))
    )
  );

  g_value_take_boxed(&params[1].value, v);

  buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(test->textview));
  mark = gtk_text_buffer_get_insert(buffer);
  gtk_text_buffer_get_iter_at_mark(buffer, &iter, mark);
  g_value_set_uint(&params[2].value, gtk_text_iter_get_offset(&iter));

  request = inf_session_proxy_join_user(
    test->proxy,
    3,
    params,
    on_join_finished,
    test
  );

  g_object_unref(session);

  if(request != NULL)
  {
    g_assert(test->request == NULL);
    test->request = request;
    g_object_ref(request);
  }
}

static void
on_chat_synchronization_failed(InfSession* session,
                               InfXmlConnection* connection,
                               const GError* error,
                               gpointer user_data)
{
  InfTestGtkBrowserChatWindow* test;
  gchar* text;

  test = (InfTestGtkBrowserChatWindow*)user_data;
  text = g_strdup_printf("Synchronization failed: %s\n", error->message);

  gtk_label_set_text(GTK_LABEL(test->status), text);
  g_free(text);
}

static void
on_chat_synchronization_complete(InfSession* session,
                                 InfXmlConnection* connection,
                                 gpointer user_data)
{
  InfTestGtkBrowserChatWindow* test;
  test = (InfTestGtkBrowserChatWindow*)user_data;

  request_chat_join(test, g_get_user_name());
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
on_chat_window_destroy(GtkWindow* window,
                       gpointer user_data)
{
  InfTestGtkBrowserChatWindow* test;
  InfSession* session;

  test = (InfTestGtkBrowserChatWindow*)user_data;
  g_object_get(G_OBJECT(test->proxy), "session", &session, NULL);

  inf_signal_handlers_disconnect_by_func(
    session,
    G_CALLBACK(on_chat_synchronization_complete),
    test
  );

  inf_signal_handlers_disconnect_by_func(
    session,
    G_CALLBACK(on_chat_synchronization_failed),
    test
  );

  g_object_unref(session);

  if(test->request != NULL)
  {
    inf_signal_handlers_disconnect_by_func(
      test->request,
      G_CALLBACK(on_chat_join_finished),
      test
    );

    g_object_unref(test->request);
  }

  if(test->proxy != NULL) g_object_unref(test->proxy);
  if(test->user != NULL) g_object_unref(test->user);

  g_slice_free(InfTestGtkBrowserChatWindow, test);
}

static void
on_text_window_destroy(GtkWindow* window,
                       gpointer user_data)
{
  InfTestGtkBrowserWindow* test;
  InfSession* session;

  test = (InfTestGtkBrowserWindow*)user_data;
  g_object_get(G_OBJECT(test->proxy), "session", &session, NULL);

  inf_signal_handlers_disconnect_by_func(
    session,
    G_CALLBACK(on_synchronization_complete),
    test
  );

  inf_signal_handlers_disconnect_by_func(
    session,
    G_CALLBACK(on_synchronization_failed),
    test
  );

  g_object_unref(session);

  if(test->request != NULL)
  {
    inf_signal_handlers_disconnect_by_func(
      test->request,
      G_CALLBACK(on_join_finished),
      test
    );

    g_object_unref(test->request);
  }

  if(test->proxy != NULL) g_object_unref(test->proxy);
  /* TODO: Do we ever ref buffer? */
  if(test->buffer != NULL) g_object_unref(test->buffer);
  if(test->user !=NULL) g_object_unref(test->user);

  g_slice_free(InfTestGtkBrowserWindow, test);
}

static void
on_subscribe_chat_session(InfcBrowser* browser,
                          InfcSessionProxy* proxy,
                          gpointer user_data)
{
  InfSession* session;
  InfChatBuffer* buffer;
  GtkWidget* chat;
  GtkWidget* status;
  GtkWidget* vbox;
  GtkWidget* window;
  InfTestGtkBrowserChatWindow* test;

  g_object_get(G_OBJECT(proxy), "session", &session, NULL);
  buffer = INF_CHAT_BUFFER(inf_session_get_buffer(session));

  chat = inf_gtk_chat_new();
  inf_gtk_chat_set_session(INF_GTK_CHAT(chat), INF_CHAT_SESSION(session));
  gtk_widget_show(chat);

  status = gtk_label_new("Synchronizing chat...");
  gtk_widget_show(status);

  vbox = gtk_vbox_new(FALSE, 6);
  gtk_box_pack_start(GTK_BOX(vbox), chat, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), status, FALSE, TRUE, 0);
  gtk_widget_show(vbox);

  window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

  gtk_window_set_title(GTK_WINDOW(window), "Chat");
  gtk_window_set_default_size(GTK_WINDOW(window), 400, 400);
  gtk_window_set_icon_name(GTK_WINDOW(window), "infinote");
  gtk_container_set_border_width(GTK_CONTAINER(window), 6);
  gtk_container_add(GTK_CONTAINER(window), vbox);
  gtk_widget_show(window);

  test = g_slice_new(InfTestGtkBrowserChatWindow);
  test->chat = chat;
  test->status = status;
  test->buffer = buffer;
  test->proxy = INF_SESSION_PROXY(proxy);
  test->user = NULL;
  test->request = NULL;
  g_object_ref(test->proxy);

  g_signal_connect_after(
    G_OBJECT(session),
    "synchronization-failed",
    G_CALLBACK(on_chat_synchronization_failed),
    test
  );

  g_signal_connect_after(
    G_OBJECT(session),
    "synchronization-complete",
    G_CALLBACK(on_chat_synchronization_complete),
    test
  );

  g_signal_connect(
    G_OBJECT(window),
    "destroy",
    G_CALLBACK(on_chat_window_destroy),
    test
  );

  g_object_unref(session);
}

static void
on_subscribe_session(InfcBrowser* browser,
                     const InfBrowserIter* iter,
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
  InfIo* io;
  InfUserTable* user_table;
  InfTextGtkBuffer* buffer;
  InfTextGtkView* view;
  InfTextGtkViewport* viewport;
  GtkTextBuffer* textbuffer;
  InfTestGtkBrowserWindow* test;

  if(iter == NULL)
  {
    on_subscribe_chat_session(browser, proxy, user_data);
    return;
  }

  g_object_get(G_OBJECT(proxy), "session", &session, NULL);
  io = inf_adopted_session_get_io(INF_ADOPTED_SESSION(session));
  user_table = inf_session_get_user_table(session);
  buffer = INF_TEXT_GTK_BUFFER(inf_session_get_buffer(session));
  textbuffer = inf_text_gtk_buffer_get_text_buffer(buffer);

  textview = gtk_text_view_new_with_buffer(textbuffer);
  view = inf_text_gtk_view_new(io, GTK_TEXT_VIEW(textview), user_table);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(textview), FALSE);
  gtk_widget_show(textview);

  scroll = gtk_scrolled_window_new(NULL, NULL);
  viewport = inf_text_gtk_viewport_new(GTK_SCROLLED_WINDOW(scroll), user_table);
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
    inf_browser_get_node_name(INF_BROWSER(browser), iter)
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
  test->view = view;
  test->viewport = viewport;
  test->proxy = INF_SESSION_PROXY(proxy);
  test->user = NULL;
  test->request = NULL;
  g_object_ref(proxy);

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

  g_object_unref(session);
}

static void
on_activate(InfGtkBrowserView* view,
            GtkTreeIter* iter,
            gpointer user_data)
{
  InfBrowser* browser;
  InfBrowserIter* browser_iter;
  const char* note_type;
  const InfcNotePlugin* plugin;
  InfRequest* request;

  gtk_tree_model_get(
    GTK_TREE_MODEL(gtk_tree_view_get_model(GTK_TREE_VIEW(view))),
    iter,
    INF_GTK_BROWSER_MODEL_COL_BROWSER, &browser,
    INF_GTK_BROWSER_MODEL_COL_NODE, &browser_iter,
    -1
  );

  note_type = inf_browser_get_node_type(browser, browser_iter);
  plugin = infc_browser_lookup_plugin(INFC_BROWSER(browser), note_type);

  /* Subscribe, if possible and not already */
  request = inf_browser_get_pending_request(
    browser,
    browser_iter,
    "subscribe-session"
  );

  if(!inf_browser_get_session(browser, browser_iter) &&
     !request && plugin != NULL)
  {
    inf_browser_subscribe(browser, browser_iter, NULL, NULL);
  }

  inf_browser_iter_free(browser_iter);
  g_object_unref(browser);
}

static void
on_browser_notify_status(GObject* object,
                         const GParamSpec* pspec,
                         gpointer user_data)
{
  InfcBrowser* browser;
  InfBrowserStatus status;
  browser = INFC_BROWSER(object);

  g_object_get(G_OBJECT(browser), "status", &status, NULL);
  if(status == INF_BROWSER_OPEN)
    infc_browser_subscribe_chat(browser, NULL, NULL);
}

static void
on_set_browser(InfGtkBrowserModel* model,
               GtkTreePath* path,
               GtkTreeIter* iter,
               InfcBrowser* browser,
               gpointer user_data)
{
  InfBrowserStatus status;

  if(browser != NULL)
  {
    infc_browser_add_plugin(browser, &INF_TEST_GTK_BROWSER_TEXT_PLUGIN);

    g_signal_connect_after(
      G_OBJECT(browser),
      "subscribe-session",
      G_CALLBACK(on_subscribe_session),
      NULL
    );

    g_object_get(G_OBJECT(browser), "status", &status, NULL);
    if(status == INF_BROWSER_OPEN)
    {
      infc_browser_subscribe_chat(browser, NULL, NULL);
    }
    else
    {
      g_signal_connect(
        G_OBJECT(browser),
        "notify::status",
        G_CALLBACK(on_browser_notify_status),
        browser
      );
    }
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
  InfCommunicationManager* communication_manager;
#ifdef LIBINFINITY_HAVE_AVAHI
  InfXmppManager* xmpp_manager;
  InfDiscoveryAvahi* avahi;
#endif
  InfGtkBrowserStore* store;
  GtkWidget* view;
  GtkWidget* scroll;
  GtkWidget* window;
  GError* error;

  int i;
  InfTcpConnection* tcp;
  InfXmppConnection* xmpp;
  InfIpAddress* addr;

  gtk_init(&argc, &argv);

  error = NULL;
  if(!inf_init(&error))
  {
    fprintf(stderr, "%s\n", error->message);
    return -1;
  }

  io = inf_gtk_io_new();
#ifdef LIBINFINITY_HAVE_AVAHI
  xmpp_manager = inf_xmpp_manager_new();
  avahi = inf_discovery_avahi_new(INF_IO(io), xmpp_manager, NULL, NULL, NULL);
  g_object_unref(G_OBJECT(xmpp_manager));
#endif

  communication_manager = inf_communication_manager_new();
  store = inf_gtk_browser_store_new(INF_IO(io), communication_manager);
  g_object_unref(communication_manager);

  g_signal_connect_after(
    G_OBJECT(store),
    "set-browser",
    G_CALLBACK(on_set_browser),
    NULL
  );

  for(i = 1; i < argc; ++i)
  {
    addr = inf_ip_address_new_from_string(argv[i]);

    tcp = inf_tcp_connection_new(
      INF_IO(io),
      addr,
      inf_protocol_get_default_port()
    );

    xmpp = inf_xmpp_connection_new(
      tcp,
      INF_XMPP_CONNECTION_CLIENT,
      g_get_host_name(),
      argv[i],
      INF_XMPP_CONNECTION_SECURITY_BOTH_PREFER_TLS,
      NULL,
      NULL,
      NULL
    );

    inf_ip_address_free(addr);
    g_object_unref(tcp);

#ifdef LIBINFINITY_HAVE_AVAHI
    inf_xmpp_manager_add_connection(xmpp_manager, xmpp);
#endif

    inf_gtk_browser_store_add_connection(
      INF_GTK_BROWSER_STORE(store),
      INF_XML_CONNECTION(xmpp),
      argv[i]
    );
  }

  g_object_unref(G_OBJECT(io));

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
