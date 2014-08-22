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
#include <infinoted/infinoted-log.h>
#include <infinoted/infinoted-util.h>

#include <libinftext/inf-text-session.h>
#include <libinftext/inf-text-buffer.h>

#include <libinfinity/common/inf-file-util.h>
#include <libinfinity/inf-signals.h>
#include <libinfinity/inf-i18n.h>

#include <string.h>

typedef struct _InfinotedPluginDirectorySync InfinotedPluginDirectorySync;
struct _InfinotedPluginDirectorySync {
  InfinotedPluginManager* manager;
  gchar* directory;
  guint interval;
  gchar* hook;
};

typedef struct _InfinotedPluginDirectorySyncSessionInfo
  InfinotedPluginDirectorySyncSessionInfo;
struct _InfinotedPluginDirectorySyncSessionInfo {
  InfinotedPluginDirectorySync* plugin;
  InfBrowserIter iter;
  InfSessionProxy* proxy;
  InfIoTimeout* timeout;
};

static const gchar*
infinoted_plugin_directory_sync_get_filename_encoding(void)
{
  const gchar* encoding;
  encoding = g_getenv("G_FILENAME_ENCODING");

  if(encoding != NULL && *encoding != '\0')
    return encoding;
  return "UTF-8";
}

static gchar*
infinoted_plugin_directory_sync_get_filename(
  InfinotedPluginDirectorySync* plugin,
  const InfBrowserIter* iter,
  GError** error)
{
  gchar* iter_path;
#ifdef G_OS_WIN32
  gchar* pos;
#endif
  gchar* converted;
  gchar* full_path;

  iter_path = inf_browser_get_path(
    INF_BROWSER(infinoted_plugin_manager_get_directory(plugin->manager)),
    iter
  );

#ifdef G_OS_WIN32
  for(pos = iter_path; *pos != '\0'; ++pos)
    if(*pos == '/')
      *pos = '\\';
#endif

  converted = g_filename_from_utf8(iter_path, -1, NULL, NULL, error);
  g_free(iter_path);

  if(converted == NULL)
  {
    g_prefix_error(
      error,
      _("Failed to convert path \"%s\" from UTF-8 to %s: "),
      iter_path,
      infinoted_plugin_directory_sync_get_filename_encoding()
    );

    return NULL;
  }

  full_path = g_build_filename(plugin->directory, converted + 1, NULL);
  g_free(converted);

  return full_path;
}

static gchar*
infinoted_plugin_directory_sync_filename_to_utf8(const gchar* filename)
{
  gchar* utf8;
  utf8 = g_filename_to_utf8(filename, -1, NULL, NULL, NULL);
  /* This cannot fail really, since the passed filename was created with
   * g_utf8_to_filename, so we have a correctly encoded string. */
  g_assert(utf8 != NULL);
  return utf8;
}

static void
infinoted_plugin_directory_sync_timeout_cb(gpointer user_data);

static void
infinoted_plugin_directory_sync_start(
  InfinotedPluginDirectorySyncSessionInfo* info)
{
  InfIo* io;

  io = infd_directory_get_io(
    infinoted_plugin_manager_get_directory(info->plugin->manager)
  );

  g_assert(info->timeout == NULL);

  info->timeout = inf_io_add_timeout(
    io,
    info->plugin->interval * 1000,
    infinoted_plugin_directory_sync_timeout_cb,
    info,
    NULL
  );
}

static void
infinoted_plugin_directory_sync_stop(
  InfinotedPluginDirectorySyncSessionInfo* info)
{
  InfIo* io;

  io = infd_directory_get_io(
    infinoted_plugin_manager_get_directory(info->plugin->manager)
  );

  g_assert(info->timeout != NULL);

  inf_io_remove_timeout(io, info->timeout);
  info->timeout = NULL;
}

static gboolean
infinoted_plugin_directory_sync_remove(
  InfinotedPluginDirectorySync* plugin,
  const InfBrowserIter* iter,
  GError** error)
{
  gchar* filename;
  gboolean result;
  gchar* utf8;
  GError* local_error;

  filename = infinoted_plugin_directory_sync_get_filename(
    plugin,
    iter,
    error
  );

  if(filename == NULL) return FALSE;

  local_error = NULL;
  result = inf_file_util_delete(filename, &local_error);

  if(result == FALSE)
  {
    /* Accept when the directory did not exist in the first place */
    if(local_error->domain == G_FILE_ERROR &&
       local_error->code == G_FILE_ERROR_NOENT)
    {
      g_error_free(local_error);
      local_error = NULL;

      result = TRUE;
    }
    else
    {
      utf8 = infinoted_plugin_directory_sync_filename_to_utf8(filename);
      g_free(filename);

      g_propagate_prefixed_error(
        error,
        local_error,
        _("Failed to remove directory \"%s\": "),
        utf8
      );

      g_free(utf8);
      return FALSE;
    }
  }

  g_free(filename);
  return result;
}

static gboolean
infinoted_plugin_directory_sync_save(
  InfinotedPluginDirectorySyncSessionInfo* info,
  GError** error)
{
  gchar* filename;
  gchar* utf8;

  InfSession* session;
  InfTextBuffer* buffer;
  InfTextChunk* chunk;
  gchar* content;
  gsize bytes;
  gchar* path;
  gchar* argv[4];

  if(info->timeout != NULL)
  {
    inf_io_remove_timeout(
      infd_directory_get_io(
        infinoted_plugin_manager_get_directory(info->plugin->manager)
      ),
      info->timeout
    );

    info->timeout = NULL;
  }

  filename = infinoted_plugin_directory_sync_get_filename(
    info->plugin,
    &info->iter,
    error
  );

  if(filename == NULL) return FALSE;

  if(infinoted_util_create_dirname(filename, error) == FALSE)
  {
    utf8 = infinoted_plugin_directory_sync_filename_to_utf8(filename);
    g_free(filename);

    g_prefix_error(
      error,
      _("Failed to create directory for path \"%s\": "),
      utf8
    );

    g_free(utf8);
    return FALSE;
  }

  g_object_get(G_OBJECT(info->proxy), "session", &session, NULL);
  buffer = INF_TEXT_BUFFER(inf_session_get_buffer(session));

  /* TODO: Use the iterator API here, which should be less expensive */
  chunk = inf_text_buffer_get_slice(
    buffer,
    0,
    inf_text_buffer_get_length(buffer)
  );

  content = inf_text_chunk_get_text(chunk, &bytes);
  inf_text_chunk_free(chunk);

  if(!g_file_set_contents(filename, content, bytes, error))
  {
    utf8 = infinoted_plugin_directory_sync_filename_to_utf8(filename);
    g_free(filename);

    g_prefix_error(
      error,
      _("Failed to write session for path \"%s\": "),
      utf8
    );

    g_free(content);
    g_object_unref(session);
    g_free(utf8);
    return FALSE;
  }

  g_free(content);
  g_object_unref(session);

  if(info->plugin->hook != NULL)
  {
    path = inf_browser_get_path(
      INF_BROWSER(
        infinoted_plugin_manager_get_directory(info->plugin->manager)
      ),
      &info->iter
    );

    argv[0] = info->plugin->hook;
    argv[1] = path;
    argv[2] = filename;
    argv[3] = NULL;

    if(!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                      NULL, NULL, NULL, error))
    {
      g_prefix_error(
        error,
        _("Failed to execute hook \"%s\": "),
        info->plugin->hook
      );

      g_free(path);
      g_free(filename);
      return FALSE;
    }

    g_free(path);
  }

  g_free(filename);
  return TRUE;
}

static void
infinoted_plugin_directory_sync_save_with_error(
  InfinotedPluginDirectorySyncSessionInfo* info,
  gboolean retry)
{
  GError* error;

  error = NULL;
  if(!infinoted_plugin_directory_sync_save(info, &error))
  {
    if(retry)
    {
      /* TODO: Provide a simple error to write a secondary log message... we
       * could also make use of such an API in the logging plugin. */
      infinoted_log_error(
        infinoted_plugin_manager_get_log(info->plugin->manager),
        _("%s\n\tWill retry in %u seconds"),
        error->message,
        info->plugin->interval
      );

      infinoted_plugin_directory_sync_start(info);
    }
    else
    {
      infinoted_log_error(
        infinoted_plugin_manager_get_log(info->plugin->manager),
        _("%s"),
        error->message
      );
    }

    g_error_free(error);
  }
}

static void
infinoted_plugin_directory_sync_timeout_cb(gpointer user_data)
{
  InfinotedPluginDirectorySyncSessionInfo* info;
  info = (InfinotedPluginDirectorySyncSessionInfo*)user_data;

  info->timeout = NULL;

  infinoted_plugin_directory_sync_save_with_error(info, TRUE);
}

static void
infinoted_plugin_directory_sync_buffer_text_inserted_cb(InfTextBuffer* buffer,
                                                        guint pos,
                                                        InfTextChunk* chunk,
                                                        InfUser* user,
                                                        gpointer user_data)
{
  InfinotedPluginDirectorySyncSessionInfo* info;
  info = (InfinotedPluginDirectorySyncSessionInfo*)user_data;

  if(info->timeout == NULL)
    infinoted_plugin_directory_sync_start(info);
}

static void
infinoted_plugin_directory_sync_buffer_text_erased_cb(InfTextBuffer* buffer,
                                                      guint pos,
                                                      InfTextChunk* chunk,
                                                      InfUser* user,
                                                      gpointer user_data)
{
  InfinotedPluginDirectorySyncSessionInfo* info;
  info = (InfinotedPluginDirectorySyncSessionInfo*)user_data;

  if(info->timeout == NULL)
    infinoted_plugin_directory_sync_start(info);
}

static void
infinoted_plugin_directory_sync_node_removed_cb(InfBrowser* browser,
                                                const InfBrowserIter* iter,
                                                InfRequest* request,
                                                gpointer user_data)
{
  InfinotedPluginDirectorySync* plugin;
  GError* error;

  plugin = (InfinotedPluginDirectorySync*)user_data;

  error = NULL;
  if(!infinoted_plugin_directory_sync_remove(plugin, iter, &error))
  {
    infinoted_log_error(
      infinoted_plugin_manager_get_log(plugin->manager),
      "%s",
      error->message
    );

    g_error_free(error);
  }
}

static void
infinoted_plugin_directory_sync_info_initialize(gpointer plugin_info)
{
  InfinotedPluginDirectorySync* plugin;
  plugin = (InfinotedPluginDirectorySync*)plugin_info;

  plugin->manager = NULL;
  plugin->directory = NULL;
  plugin->interval = 0;
  plugin->hook = NULL;
}

static gboolean
infinoted_plugin_directory_sync_initialize(InfinotedPluginManager* manager,
                                           gpointer plugin_info,
                                           GError** error)
{
  InfinotedPluginDirectorySync* plugin;
  plugin = (InfinotedPluginDirectorySync*)plugin_info;

  plugin->manager = manager;

  if(inf_file_util_create_directory(plugin->directory, 0700, error) == FALSE)
    return FALSE;

  g_signal_connect(
    G_OBJECT(infinoted_plugin_manager_get_directory(manager)),
    "node-removed",
    G_CALLBACK(infinoted_plugin_directory_sync_node_removed_cb),
    plugin
  );

  return TRUE;
}

static void
infinoted_plugin_directory_sync_deinitialize(gpointer plugin_info)
{
  InfinotedPluginDirectorySync* plugin;
  plugin = (InfinotedPluginDirectorySync*)plugin_info;

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(infinoted_plugin_manager_get_directory(plugin->manager)),
    G_CALLBACK(infinoted_plugin_directory_sync_node_removed_cb),
    plugin
  );

  g_free(plugin->directory);
  g_free(plugin->hook);
}

static void
infinoted_plugin_directory_sync_session_added(const InfBrowserIter* iter,
                                              InfSessionProxy* proxy,
                                              gpointer plugin_info,
                                              gpointer session_info)
{
  InfinotedPluginDirectorySyncSessionInfo* info;
  InfSession* session;
  InfBuffer* buffer;

#ifdef G_OS_WIN32
  gchar* path;
  gchar* pos;
#endif
  gboolean name_okay;

  info = (InfinotedPluginDirectorySyncSessionInfo*)session_info;
  info->plugin = (InfinotedPluginDirectorySync*)plugin_info;
  info->iter = *iter;
  info->proxy = proxy;
  info->timeout = NULL;
  g_object_ref(proxy);

  name_okay = TRUE;

  /* Check that there are no '\' characters somewhere on the path, this would
   * not be allowed on Windows. */
#ifdef G_OS_WIN32
  path = inf_browser_get_path(
    INF_BROWSER(
      infinoted_plugin_manager_get_directory(info->plugin->manager)
    ),
    iter
  );

  for(pos = path; *pos != '\0'; ++pos)
  {
    if(*pos == '\\')
    {
      infinoted_log_error(
        infinoted_plugin_manager_get_log(info->plugin->manager),
        _("Node \"%s\" contains invalid characters"),
        path
      );

      name_okay = FALSE;
      break;
    }
  }

  g_free(path);
#endif

  if(name_okay == TRUE)
  {
    g_object_get(G_OBJECT(proxy), "session", &session, NULL);
    buffer = inf_session_get_buffer(session);

    g_signal_connect(
      G_OBJECT(buffer),
      "text-inserted",
      G_CALLBACK(infinoted_plugin_directory_sync_buffer_text_inserted_cb),
      info
    );

    g_signal_connect(
      G_OBJECT(buffer),
      "text-erased",
      G_CALLBACK(infinoted_plugin_directory_sync_buffer_text_erased_cb),
      info
    );

    infinoted_plugin_directory_sync_save_with_error(info, TRUE);

    g_object_unref(session);
  }
}

static void
infinoted_plugin_directory_sync_session_removed(const InfBrowserIter* iter,
                                                InfSessionProxy* proxy,
                                                gpointer plugin_info,
                                                gpointer session_info)
{
  InfinotedPluginDirectorySyncSessionInfo* info;
  InfSession* session;
  InfBuffer* buffer;

  info = (InfinotedPluginDirectorySyncSessionInfo*)session_info;

  /* If a directory sync was scheduled for this session, then do it now */
  if(info->timeout != NULL)
    infinoted_plugin_directory_sync_save_with_error(info, FALSE);

  g_object_get(G_OBJECT(info->proxy), "session", &session, NULL);
  buffer = inf_session_get_buffer(session);

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(buffer),
    G_CALLBACK(infinoted_plugin_directory_sync_buffer_text_inserted_cb),
    info
  );

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(buffer),
    G_CALLBACK(infinoted_plugin_directory_sync_buffer_text_erased_cb),
    info
  );

  g_object_unref(session);
  g_object_unref(info->proxy);
}

static const InfinotedParameterInfo
INFINOTED_PLUGIN_DIRECTORY_SYNC_OPTIONS[] = {
  { "directory",
    INFINOTED_PARAMETER_STRING,
    INFINOTED_PARAMETER_REQUIRED,
    offsetof(InfinotedPluginDirectorySync, directory),
    infinoted_parameter_convert_filename,
    0,
    N_("The directory into which to store the directory tree in text form."),
    N_("DIRECTORY")
  }, {
    "interval",
    INFINOTED_PARAMETER_INT,
    INFINOTED_PARAMETER_REQUIRED,
    offsetof(InfinotedPluginDirectorySync, interval),
    infinoted_parameter_convert_positive,
    0,
    N_("Interval, in seconds, after which to save documents into the given "
       "directory."),
    N_("SECONDS")
  }, {
    "hook",
    INFINOTED_PARAMETER_STRING,
    0,
    offsetof(InfinotedPluginDirectorySync, hook),
    infinoted_parameter_convert_filename,
    0,
    N_("Command to run after having saved a document."),
    N_("PROGRAM")
  }, {
    NULL,
    0,
    0,
    0,
    NULL
  }
};

const InfinotedPlugin INFINOTED_PLUGIN = {
  "directory-sync",
  N_("Periodically saves the content of all documents into a custom "
     "directory, without any infinote metadata such as which user wrote what "
     "text. This option can be used to (automatically) process the files on "
     "the server by standard tools that operate on normal UTF-8 encoded text "
     "files"),
  INFINOTED_PLUGIN_DIRECTORY_SYNC_OPTIONS,
  sizeof(InfinotedPluginDirectorySync),
  0,
  sizeof(InfinotedPluginDirectorySyncSessionInfo),
  "InfTextSession",
  infinoted_plugin_directory_sync_info_initialize,
  infinoted_plugin_directory_sync_initialize,
  infinoted_plugin_directory_sync_deinitialize,
  NULL,
  NULL,
  infinoted_plugin_directory_sync_session_added,
  infinoted_plugin_directory_sync_session_removed
};

/* vim:set et sw=2 ts=2: */
