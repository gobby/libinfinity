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

#include <infinoted/infinoted-plugin-manager.h>
#include <infinoted/infinoted-parameter.h>
#include <infinoted/infinoted-log.h>

#include <libinftext/inf-text-session.h>
#include <libinftext/inf-text-buffer.h>

#include <libinfinity/inf-i18n.h>

#include <string.h>

typedef struct _InfinotedPluginAutosave InfinotedPluginAutosave;
struct _InfinotedPluginAutosave {
  InfinotedPluginManager* manager;
  guint interval;
  gchar* hook;
};

typedef struct _InfinotedPluginAutosaveSessionInfo
  InfinotedPluginAutosaveSessionInfo;
struct _InfinotedPluginAutosaveSessionInfo {
  InfinotedPluginAutosave* plugin;
  InfBrowserIter iter;
  InfSessionProxy* proxy;
  InfIoTimeout* timeout;
};

static void
infinoted_plugin_autosave_timeout_cb(gpointer user_data);

static void
infinoted_plugin_autosave_start(InfinotedPluginAutosaveSessionInfo* info)
{
  InfIo* io;

  io = infd_directory_get_io(
    infinoted_plugin_manager_get_directory(info->plugin->manager)
  );

  g_assert(info->timeout == NULL);

  info->timeout = inf_io_add_timeout(
    io,
    info->plugin->interval * 1000,
    infinoted_plugin_autosave_timeout_cb,
    info,
    NULL
  );
}

static void
infinoted_plugin_autosave_stop(InfinotedPluginAutosaveSessionInfo* info)
{
  InfIo* io;

  io = infd_directory_get_io(
    infinoted_plugin_manager_get_directory(info->plugin->manager)
  );

  g_assert(info->timeout != NULL);

  inf_io_remove_timeout(io, info->timeout);
  info->timeout = NULL;
}


static void
infinoted_plugin_autosave_buffer_notify_modified_cb(GObject* object,
                                                    GParamSpec* pspec,
                                                    gpointer user_data)
{
  InfinotedPluginAutosaveSessionInfo* info;
  InfSession* session;
  InfBuffer* buffer;

  info = (InfinotedPluginAutosaveSessionInfo*)user_data;
  g_object_get(G_OBJECT(info->proxy), "session", &session, NULL);
  buffer = inf_session_get_buffer(session);

  if(inf_buffer_get_modified(buffer) == TRUE)
  {
    if(info->timeout == NULL)
      infinoted_plugin_autosave_start(info);
  }
  else
  {
    if(info->timeout != NULL)
      infinoted_plugin_autosave_stop(info);
  }

  g_object_unref(session);
}

static void
infinoted_plugin_autosave_save(InfinotedPluginAutosaveSessionInfo* info)
{
  InfdDirectory* directory;
  InfBrowserIter* iter;
  GError* error;
  gchar* path;
  InfSession* session;
  InfBuffer* buffer;
  gchar* root_directory;
  gchar* argv[4];

  directory = infinoted_plugin_manager_get_directory(info->plugin->manager);
  iter = &info->iter;
  error = NULL;

  if(info->timeout != NULL)
  {
    inf_io_remove_timeout(infd_directory_get_io(directory), info->timeout);
    info->timeout = NULL;
  }

  g_object_get(G_OBJECT(info->proxy), "session", &session, NULL);
  buffer = inf_session_get_buffer(session);

  inf_signal_handlers_block_by_func(
    G_OBJECT(buffer),
    G_CALLBACK(infinoted_plugin_autosave_buffer_notify_modified_cb),
    info
  );

  if(infd_directory_iter_save_session(directory, iter, &error) == FALSE)
  {
    path = inf_browser_get_path(INF_BROWSER(directory), iter);

    infinoted_log_warning(
      infinoted_plugin_manager_get_log(info->plugin->manager),
      _("Failed to auto-save session \"%s\": %s\n\n"
        "Will retry in %u seconds."),
      path,
      error->message,
      info->plugin->interval
    );

    g_free(path);
    g_error_free(error);
    error = NULL;

    infinoted_plugin_autosave_start(info);
  }
  else
  {
    /* TODO: Remove this as soon as directory itself unsets modified flag
     * on session_write */
    inf_buffer_set_modified(INF_BUFFER(buffer), FALSE);

    if(info->plugin->hook != NULL)
    {
      path = inf_browser_get_path(INF_BROWSER(directory), iter);

      g_object_get(
        G_OBJECT(infd_directory_get_storage(directory)),
        "root-directory",
        &root_directory,
        NULL
      );

      argv[0] = info->plugin->hook;
      argv[1] = root_directory;
      argv[2] = path;
      argv[3] = NULL;

      if(!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                        NULL, NULL, NULL, &error))
      {
        infinoted_log_warning(
          infinoted_plugin_manager_get_log(info->plugin->manager),
          _("Could not execute autosave hook: \"%s\""),
          error->message
        );

        g_error_free(error);
        error = NULL;
      }

      g_free(path);
      g_free(root_directory);
    }
  }
  
  inf_signal_handlers_unblock_by_func(
    G_OBJECT(buffer),
    G_CALLBACK(infinoted_plugin_autosave_buffer_notify_modified_cb),
    info
  );

  g_object_unref(session);
}

static void
infinoted_plugin_autosave_timeout_cb(gpointer user_data)
{
  InfinotedPluginAutosaveSessionInfo* info;

  info = (InfinotedPluginAutosaveSessionInfo*)user_data;
  info->timeout = NULL;

  infinoted_plugin_autosave_save(info);
}

static void
infinoted_plugin_autosave_info_initialize(gpointer plugin_info)
{
  InfinotedPluginAutosave* plugin;
  plugin = (InfinotedPluginAutosave*)plugin_info;

  plugin->manager = NULL;
  plugin->interval = 0;
  plugin->hook = NULL;
}

static gboolean
infinoted_plugin_autosave_initialize(InfinotedPluginManager* manager,
                                     gpointer plugin_info,
                                     GError** error)
{
  InfinotedPluginAutosave* plugin;
  plugin = (InfinotedPluginAutosave*)plugin_info;

  plugin->manager = manager;
}

static void
infinoted_plugin_autosave_deinitialize(gpointer plugin_info)
{
  InfinotedPluginAutosave* plugin;
  plugin = (InfinotedPluginAutosave*)plugin_info;

  g_free(plugin->hook);
}

static void
infinoted_plugin_autosave_session_added(const InfBrowserIter* iter,
                                        InfSessionProxy* proxy,
                                        gpointer plugin_info,
                                        gpointer session_info)
{
  InfinotedPluginAutosaveSessionInfo* info;
  InfSession* session;
  InfBuffer* buffer;

  info = (InfinotedPluginAutosaveSessionInfo*)session_info;
  info->plugin = (InfinotedPluginAutosave*)plugin_info;
  info->iter = *iter;
  info->proxy = proxy;
  info->timeout = NULL;
  g_object_ref(proxy);

  g_object_get(G_OBJECT(proxy), "session", &session, NULL);
  buffer = inf_session_get_buffer(session);

  g_signal_connect(
    G_OBJECT(buffer),
    "notify::modified",
    G_CALLBACK(infinoted_plugin_autosave_buffer_notify_modified_cb),
    info
  );

  if(inf_buffer_get_modified(buffer) == TRUE)
    infinoted_plugin_autosave_start(info);

  g_object_unref(session);
}

static void
infinoted_plugin_autosave_session_removed(const InfBrowserIter* iter,
                                          InfSessionProxy* proxy,
                                          gpointer plugin_info,
                                          gpointer session_info)
{
  InfinotedPluginAutosaveSessionInfo* info;
  InfSession* session;
  InfBuffer* buffer;

  info = (InfinotedPluginAutosaveSessionInfo*)session_info;

  /* Cancel autosave timeout even if session is modified. If the directory
   * removed the session, then it has already saved it anyway. */
  if(info->timeout != NULL)
    infinoted_plugin_autosave_stop(info);

  g_object_get(G_OBJECT(info->proxy), "session", &session, NULL);
  buffer = inf_session_get_buffer(session);

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(buffer),
    G_CALLBACK(infinoted_plugin_autosave_buffer_notify_modified_cb),
    info
  );

  g_object_unref(session);
  g_object_unref(info->proxy);
}

static const InfinotedParameterInfo INFINOTED_PLUGIN_AUTOSAVE_OPTIONS[] = {
  {
    "interval",
    INFINOTED_PARAMETER_INT,
    INFINOTED_PARAMETER_REQUIRED,
    offsetof(InfinotedPluginAutosave, interval),
    infinoted_parameter_convert_positive,
    0,
    N_("Interval, in seconds, after which to save documents into the root "
       "directory. Documents are also stored to disk when there has been "
       "no user logged into them for 60 seconds."),
    N_("SECONDS")
  }, {
    "autosave-hook",
    INFINOTED_PARAMETER_STRING,
    0,
    offsetof(InfinotedPluginAutosave, hook),
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
  "autosave",
  N_("Periodically saves the content of all documents to disk. If this "
     "plugin is not enabled, infinoted only moves a document to permanent "
     "storage 60 seconds after the last user left the document."),
  INFINOTED_PLUGIN_AUTOSAVE_OPTIONS,
  sizeof(InfinotedPluginAutosave),
  0,
  sizeof(InfinotedPluginAutosaveSessionInfo),
  NULL,
  infinoted_plugin_autosave_info_initialize,
  infinoted_plugin_autosave_initialize,
  infinoted_plugin_autosave_deinitialize,
  NULL,
  NULL,
  infinoted_plugin_autosave_session_added,
  infinoted_plugin_autosave_session_removed
};

/* vim:set et sw=2 ts=2: */
