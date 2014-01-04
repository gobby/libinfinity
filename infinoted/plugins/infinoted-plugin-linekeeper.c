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
  InfUserRequest* request;
  InfUser* user;
  InfTextBuffer* buffer;
  InfIoDispatch* dispatch;
};

static gboolean
infinoted_plugin_linekeeper_initialize(InfinotedPluginManager* manager,
                                       gpointer plugin_info,
                                       GError** error)
{
  InfinotedPluginLinekeeper* plugin;
  plugin = (InfinotedPluginLinekeeper*)plugin_info;

  plugin->manager = manager;
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
infinoted_plugin_linekeeper_user_join_cb(InfUserRequest* request,
                                         InfUser* user,
                                         const GError* error,
                                         gpointer user_data)
{
  InfinotedPluginLinekeeperSessionInfo* info;
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
  InfUserRequest* request;

  info = (InfinotedPluginLinekeeperSessionInfo*)session_info;

  info->plugin = (InfinotedPluginLinekeeper*)plugin_info;
  info->request = NULL;
  info->user = NULL;
  info->dispatch = NULL;

  g_object_get(G_OBJECT(proxy), "session", &session, NULL);
  g_assert(inf_session_get_status(session) == INF_SESSION_RUNNING);
  info->buffer = INF_TEXT_BUFFER(inf_session_get_buffer(session));
  g_object_ref(info->buffer);

  info->request = inf_text_session_join_user(
    proxy,
    "LineKeeper",
    INF_USER_ACTIVE,
    0.0,
    inf_text_buffer_get_length(info->buffer),
    0,
    infinoted_plugin_linekeeper_user_join_cb,
    info
  );

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

  info = (InfinotedPluginLinekeeperSessionInfo*)session_info;

  if(info->dispatch != NULL)
  {
    directory = infinoted_plugin_manager_get_directory(info->plugin->manager);
    inf_io_remove_dispatch(infd_directory_get_io(directory), info->dispatch);
    info->dispatch = NULL;
  }

  if(info->buffer != NULL)
  {
    if(info->user != NULL)
    {
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
    }

    g_object_unref(info->buffer);
    info->buffer = NULL;
  }

  if(info->user != NULL)
  {
    g_object_unref(info->user);
    info->user = NULL;
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
