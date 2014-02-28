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

#include <infinoted/infinoted-plugin-manager.h>
#include <infinoted/infinoted-parameter.h>

#include <libinftext/inf-text-session.h>
#include <libinftext/inf-text-buffer.h>

#include <libinfinity/inf-i18n.h>

#include <string.h>

typedef struct _InfinotedPluginLinekeeper InfinotedPluginLinekeeper;
struct _InfinotedPluginLinekeeper {
  InfinotedPluginManager* manager;
  guint n_lines;
};

typedef struct _InfinotedPluginLinekeeperSessionInfo
  InfinotedPluginLinekeeperSessionInfo;
struct _InfinotedPluginLinekeeperSessionInfo {
  InfinotedPluginLinekeeper* plugin;
  InfSessionProxy* proxy;
  InfRequest* request;
  InfUser* user;
  InfTextBuffer* buffer;
  InfIoDispatch* dispatch;
};

typedef struct _InfinotedPluginLinekeeperHasAvailableUsersData
  InfinotedPluginLinekeeperHasAvailableUsersData;
struct _InfinotedPluginLinekeeperHasAvailableUsersData {
  InfUser* own_user;
  gboolean has_available_user;
};

static gboolean
infinoted_plugin_linekeeper_initialize(InfinotedPluginManager* manager,
                                       gpointer plugin_info,
                                       GError** error)
{
  InfinotedPluginLinekeeper* plugin;
  plugin = (InfinotedPluginLinekeeper*)plugin_info;

  plugin->manager = manager;

  return TRUE;
}

static void
infinoted_plugin_linekeeper_deinitialize(gpointer plugin_info)
{
  InfinotedPluginLinekeeper* plugin;
  plugin = (InfinotedPluginLinekeeper*)plugin_info;
}

static guint
infinoted_plugin_linekeeper_count_lines(InfTextBuffer* buffer)
{
  /* Count the number of lines at the end of the document. This assumes the
   * buffer content is in UTF-8, which is currently hardcoded in infinoted. */
  InfTextBufferIter* iter;
  guint n_lines;
  gboolean has_iter;

  guint length;
  gsize bytes;
  gchar* text;
  gchar* pos;
  gchar* new_pos;
  gunichar c;

  g_assert(strcmp(inf_text_buffer_get_encoding(buffer), "UTF-8") == 0);

  n_lines = 0;

  iter = inf_text_buffer_create_end_iter(buffer);
  if(iter == NULL) return 0;

  do
  {
    length = inf_text_buffer_iter_get_length(buffer, iter);
    bytes = inf_text_buffer_iter_get_bytes(buffer, iter);
    text = inf_text_buffer_iter_get_text(buffer, iter);
    pos = text + bytes;

    while(length > 0)
    {
      new_pos = g_utf8_prev_char(pos);
      g_assert(bytes >= (pos - new_pos));

      c = g_utf8_get_char(new_pos);
      if(c == '\n' || g_unichar_type(c) == G_UNICODE_LINE_SEPARATOR)
        ++n_lines;
      else
        break;

      --length;
      bytes -= (pos - new_pos);
      pos = new_pos;
    }

    g_free(text);
  } while(length == 0 && inf_text_buffer_iter_prev(buffer, iter));

  inf_text_buffer_destroy_iter(buffer, iter);
  return n_lines;
}

static void
infinoted_plugin_linekeeper_run(InfinotedPluginLinekeeperSessionInfo* info)
{
  guint cur_lines;
  guint n;
  gchar* text;

  cur_lines = infinoted_plugin_linekeeper_count_lines(info->buffer);

  if(cur_lines > info->plugin->n_lines)
  {
    n = cur_lines - info->plugin->n_lines;

    inf_text_buffer_erase_text(
      info->buffer,
      inf_text_buffer_get_length(info->buffer) - n,
      n,
      info->user
    );
  }
  else if(cur_lines < info->plugin->n_lines)
  {
    n = info->plugin->n_lines - cur_lines;
    text = g_malloc(n * sizeof(gchar));
    memset(text, '\n', n);

    inf_text_buffer_insert_text(
      info->buffer,
      inf_text_buffer_get_length(info->buffer),
      text,
      n,
      n,
      info->user
    );
  }
}

static void
infinoted_plugin_linekeeper_run_dispatch_func(gpointer user_data)
{
  InfinotedPluginLinekeeperSessionInfo* info;
  info = (InfinotedPluginLinekeeperSessionInfo*)user_data;

  info->dispatch = NULL;

  infinoted_plugin_linekeeper_run(info);
}

static void
infinoted_plugin_linekeeper_text_inserted_cb(InfTextBuffer* buffer,
                                             guint pos,
                                             InfTextChunk* chunk,
                                             InfUser* user,
                                             gpointer user_data)
{
  InfinotedPluginLinekeeperSessionInfo* info;
  InfdDirectory* directory;

  info = (InfinotedPluginLinekeeperSessionInfo*)user_data;

  if(info->dispatch == NULL)
  {
    directory = infinoted_plugin_manager_get_directory(info->plugin->manager);

    info->dispatch = inf_io_add_dispatch(
      infd_directory_get_io(directory),
      infinoted_plugin_linekeeper_run_dispatch_func,
      info,
      NULL
    );
  }
}

static void
infinoted_plugin_linekeeper_text_erased_cb(InfTextBuffer* buffer,
                                           guint pos,
                                           InfTextChunk* chunk,
                                           InfUser* user,
                                           gpointer user_data)
{
  InfinotedPluginLinekeeperSessionInfo* info;
  InfdDirectory* directory;

  info = (InfinotedPluginLinekeeperSessionInfo*)user_data;

  if(info->dispatch == NULL)
  {
    directory = infinoted_plugin_manager_get_directory(info->plugin->manager);

    info->dispatch = inf_io_add_dispatch(
      infd_directory_get_io(directory),
      infinoted_plugin_linekeeper_run_dispatch_func,
      info,
      NULL
    );
  }
}

static void
infinoted_plugin_linekeeper_remove_user(
  InfinotedPluginLinekeeperSessionInfo* info)
{
  InfSession* session;
  InfUser* user;

  g_assert(info->user != NULL);
  g_assert(info->request == NULL);

  user = info->user;
  info->user = NULL;

  g_object_get(G_OBJECT(info->proxy), "session", &session, NULL); 

  inf_session_set_user_status(session, user, INF_USER_UNAVAILABLE);
  g_object_unref(user);

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(info->buffer),
    G_CALLBACK(infinoted_plugin_linekeeper_text_inserted_cb),
    info
  );

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(info->buffer),
    G_CALLBACK(infinoted_plugin_linekeeper_text_erased_cb),
    info
  );

  g_object_unref(session);
}

static void
infinoted_plugin_linekeeper_has_available_users_foreach_func(InfUser* user,
                                                             gpointer udata)
{
  InfinotedPluginLinekeeperHasAvailableUsersData* data;
  data = (InfinotedPluginLinekeeperHasAvailableUsersData*)udata;

  /* Return TRUE if there are non-local users connected */
  if(user != data->own_user &&
     inf_user_get_status(user) != INF_USER_UNAVAILABLE &&
     (inf_user_get_flags(user) & INF_USER_LOCAL) == 0)
  {
    data->has_available_user = TRUE;
  }
}

static gboolean
infinoted_plugin_linekeeper_has_available_users(
  InfinotedPluginLinekeeperSessionInfo* info)
{
  InfinotedPluginLinekeeperHasAvailableUsersData data;
  InfSession* session;
  InfUserTable* user_table;

  g_object_get(G_OBJECT(info->proxy), "session", &session, NULL); 
  user_table = inf_session_get_user_table(session);

  data.has_available_user = FALSE;
  data.own_user = info->user;

  inf_user_table_foreach_user(
    user_table,
    infinoted_plugin_linekeeper_has_available_users_foreach_func,
    &data
  );

  g_object_unref(session);
  return data.has_available_user;
}

static void
infinoted_plugin_linekeeper_user_join_cb(InfRequest* request,
                                         const InfRequestResult* result,
                                         const GError* error,
                                         gpointer user_data)
{
  InfinotedPluginLinekeeperSessionInfo* info;
  InfUser* user;

  info = (InfinotedPluginLinekeeperSessionInfo*)user_data;

  info->request = NULL;

  if(error != NULL)
  {
    infinoted_log_warning(
      infinoted_plugin_manager_get_log(info->plugin->manager),
      _("Could not join LineKeeper user for document: %s\n"),
      error->message
    );
  }
  else
  {
    inf_request_result_get_join_user(result, NULL, &user);

    info->user = user;
    g_object_ref(info->user);

    /* Initial run */
    infinoted_plugin_linekeeper_run(info);

    g_signal_connect(
      G_OBJECT(info->buffer),
      "text-inserted",
      G_CALLBACK(infinoted_plugin_linekeeper_text_inserted_cb),
      info
    );

    g_signal_connect(
      G_OBJECT(info->buffer),
      "text-erased",
      G_CALLBACK(infinoted_plugin_linekeeper_text_erased_cb),
      info
    );

    /* It can happen that while the request is being processed, the situation
     * changes again. */
    if(infinoted_plugin_linekeeper_has_available_users(info) == FALSE)
    {
      infinoted_plugin_linekeeper_remove_user(info);
    }
  }
}

static void
infinoted_plugin_linekeeper_add_available_user_cb(InfUserTable* user_table,
                                                  InfUser* user,
                                                  gpointer user_data);

static void
infinoted_plugin_linekeeper_join_user(
  InfinotedPluginLinekeeperSessionInfo* info)
{
  InfSession* session;
  InfUserTable* user_table;

  g_assert(info->user == NULL);
  g_assert(info->request == NULL);

  g_object_get(G_OBJECT(info->proxy), "session", &session, NULL);
  user_table = inf_session_get_user_table(session);

  /* Prevent double user join attempt by blocking the callback for
   * joining our local user. */
  g_signal_handlers_block_by_func(
    user_table,
    G_CALLBACK(infinoted_plugin_linekeeper_add_available_user_cb),
    info
  );

  info->request = inf_text_session_join_user(
    info->proxy,
    "LineKeeper",
    INF_USER_ACTIVE,
    0.0,
    inf_text_buffer_get_length(info->buffer),
    0,
    infinoted_plugin_linekeeper_user_join_cb,
    info
  );

  g_signal_handlers_unblock_by_func(
    user_table,
    G_CALLBACK(infinoted_plugin_linekeeper_add_available_user_cb),
    info
  );

  g_object_unref(session);
}

static void
infinoted_plugin_linekeeper_add_available_user_cb(InfUserTable* user_table,
                                                  InfUser* user,
                                                  gpointer user_data)
{
  InfinotedPluginLinekeeperSessionInfo* info;
  info = (InfinotedPluginLinekeeperSessionInfo*)user_data;

  if(info->user == NULL && info->request == NULL &&
     infinoted_plugin_linekeeper_has_available_users(info))
  {
    infinoted_plugin_linekeeper_join_user(info);
  }
}

static void
infinoted_plugin_linekeeper_remove_available_user_cb(InfUserTable* user_table,
                                                     InfUser* user,
                                                     gpointer user_data)
{
  InfinotedPluginLinekeeperSessionInfo* info;
  info = (InfinotedPluginLinekeeperSessionInfo*)user_data;

  if(info->user != NULL &&
     !infinoted_plugin_linekeeper_has_available_users(info))
  {
    infinoted_plugin_linekeeper_remove_user(info);
  }
}

static void
infinoted_plugin_linekeeper_session_added(const InfBrowserIter* iter,
                                          InfSessionProxy* proxy,
                                          gpointer plugin_info,
                                          gpointer session_info)
{
  InfinotedPluginLinekeeperSessionInfo* info;
  InfSession* session;
  InfUserTable* user_table;

  info = (InfinotedPluginLinekeeperSessionInfo*)session_info;

  info->plugin = (InfinotedPluginLinekeeper*)plugin_info;
  info->proxy = proxy;
  info->request = NULL;
  info->user = NULL;
  info->dispatch = NULL;
  g_object_ref(proxy);

  g_object_get(G_OBJECT(proxy), "session", &session, NULL);
  g_assert(inf_session_get_status(session) == INF_SESSION_RUNNING);
  info->buffer = INF_TEXT_BUFFER(inf_session_get_buffer(session));
  g_object_ref(info->buffer);

  user_table = inf_session_get_user_table(session);

  g_signal_connect(
    G_OBJECT(user_table),
    "add-available-user",
    G_CALLBACK(infinoted_plugin_linekeeper_add_available_user_cb),
    info
  );

  g_signal_connect(
    G_OBJECT(user_table),
    "remove-available-user",
    G_CALLBACK(infinoted_plugin_linekeeper_remove_available_user_cb),
    info
  );

  /* Only join a user when there are other nonlocal users available, so that
   * we don't keep the session from going idle. */
  if(infinoted_plugin_linekeeper_has_available_users(info) == TRUE)
    infinoted_plugin_linekeeper_join_user(info);

  g_object_unref(session);
}

static void
infinoted_plugin_linekeeper_session_removed(const InfBrowserIter* iter,
                                            InfSessionProxy* proxy,
                                            gpointer plugin_info,
                                            gpointer session_info)
{
  InfinotedPluginLinekeeperSessionInfo* info;
  InfdDirectory* directory;
  InfSession* session;
  InfUserTable* user_table;

  info = (InfinotedPluginLinekeeperSessionInfo*)session_info;

  g_object_get(G_OBJECT(info->proxy), "session", &session, NULL);
  user_table = inf_session_get_user_table(session);

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(user_table),
    G_CALLBACK(infinoted_plugin_linekeeper_add_available_user_cb),
    info
  );

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(user_table),
    G_CALLBACK(infinoted_plugin_linekeeper_remove_available_user_cb),
    info
  );

  if(info->dispatch != NULL)
  {
    directory = infinoted_plugin_manager_get_directory(info->plugin->manager);
    inf_io_remove_dispatch(infd_directory_get_io(directory), info->dispatch);
    info->dispatch = NULL;
  }

  if(info->user != NULL)
  {
    infinoted_plugin_linekeeper_remove_user(info);
  }

  if(info->buffer != NULL)
  {
    g_object_unref(info->buffer);
    info->buffer = NULL;
  }

  if(info->request != NULL)
  {
    inf_signal_handlers_disconnect_by_func(
      info->request,
      G_CALLBACK(infinoted_plugin_linekeeper_user_join_cb),
      info
    );

    info->request = NULL;
  }

  g_assert(info->proxy != NULL);
  g_object_unref(info->proxy);

  g_object_unref(session);
}

static const InfinotedParameterInfo INFINOTED_PLUGIN_LINEKEEPER_OPTIONS[] = {
  {
    "n-lines",
    INFINOTED_PARAMETER_INT,
    INFINOTED_PARAMETER_REQUIRED,
    offsetof(InfinotedPluginLinekeeper, n_lines),
    infinoted_parameter_convert_nonnegative,
    0,
    N_("The number of empty lines to keep at the end of the document."),
    N_("LINES")
  }, {
    NULL,
    0,
    0,
    0,
    NULL
  }
};

const InfinotedPlugin INFINOTED_PLUGIN = {
  "linekeeper",
  N_("This plugin makes sure that at the end of every document there is "
     "always a fixed number of empty lines."),
  INFINOTED_PLUGIN_LINEKEEPER_OPTIONS,
  sizeof(InfinotedPluginLinekeeper),
  0,
  sizeof(InfinotedPluginLinekeeperSessionInfo),
  "InfTextSession",
  NULL,
  infinoted_plugin_linekeeper_initialize,
  infinoted_plugin_linekeeper_deinitialize,
  NULL,
  NULL,
  infinoted_plugin_linekeeper_session_added,
  infinoted_plugin_linekeeper_session_removed
};

/* vim:set et sw=2 ts=2: */
