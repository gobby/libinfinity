/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2015 Armin Burgmeier <armin@arbur.net>
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

#include <libinfinity/adopted/inf-adopted-session-record.h>
#include <libinfinity/inf-i18n.h>

#include <string.h>

typedef struct _InfinotedPluginRecord InfinotedPluginRecord;
struct _InfinotedPluginRecord {
  InfinotedPluginManager* manager;
};

typedef struct _InfinotedPluginRecordSessionInfo
  InfinotedPluginRecordSessionInfo;
struct _InfinotedPluginRecordSessionInfo {
  InfinotedPluginRecord* plugin;
  InfAdoptedSessionRecord* record;
};

static InfAdoptedSessionRecord*
infinoted_plugin_record_start(InfinotedPluginRecord* plugin,
                              InfAdoptedSession* session,
                              const gchar* title)
{
  gchar* dirname;
  gchar* basename;
  gchar* filename;
  guint i;
  gsize pos;
  InfAdoptedSessionRecord* record;
  GError* error;

  basename = g_build_filename(g_get_home_dir(), ".infinoted-records", title, NULL);
  pos = strlen(basename) + 8;
  filename = g_strdup_printf("%s.record-00000.xml", basename);
  g_free(basename);

  i = 0;
  while(g_file_test(filename, G_FILE_TEST_EXISTS) && ++i < 100000)
    g_snprintf(filename + pos, 10, "%05u.xml", i);

  record = NULL;
  if(i >= 100000)
  {
    dirname = g_path_get_dirname(filename);

    infinoted_log_warning(
      infinoted_plugin_manager_get_log(plugin->manager),
      _("Could not create record file for session \"%s\": Could not generate "
        "unused record file in directory \"%s\""),
      title,
      dirname
    );

    g_free(dirname);
  }
  else
  {
    error = NULL;
    if(!infinoted_util_create_dirname(filename, &error))
    {
      dirname = g_path_get_dirname(filename);

      infinoted_log_warning(
        infinoted_plugin_manager_get_log(plugin->manager),
        _("Could not create directory \"%s\": %s"),
        filename,
        error->message
      );

      g_error_free(error);
      g_free(dirname);
    }
    else
    {
      record = inf_adopted_session_record_new(session);
      inf_adopted_session_record_start_recording(record, filename, &error);
      if(error != NULL)
      {
        infinoted_log_warning(
          infinoted_plugin_manager_get_log(plugin->manager),
          _("Error while writing record for session \"%s\" into \"%s\": %s"),
          title,
          filename,
          error->message
        );

        g_error_free(error);
        g_object_unref(record);
        record = NULL;
      }
    }
  }

  g_free(filename);
  return record;
}

static void
infinoted_plugin_record_info_initialize(gpointer plugin_info)
{
  InfinotedPluginRecord* plugin;
  plugin = (InfinotedPluginRecord*)plugin_info;

  plugin->manager = NULL;
}

static gboolean
infinoted_plugin_record_initialize(InfinotedPluginManager* manager,
                                   gpointer plugin_info,
                                   GError** error)
{
  InfinotedPluginRecord* plugin;
  plugin = (InfinotedPluginRecord*)plugin_info;

  plugin->manager = manager;

  return TRUE;
}

static void
infinoted_plugin_record_deinitialize(gpointer plugin_info)
{
  InfinotedPluginRecord* plugin;
  plugin = (InfinotedPluginRecord*)plugin_info;
}

static void
infinoted_plugin_record_session_added(const InfBrowserIter* iter,
                                      InfSessionProxy* proxy,
                                      gpointer plugin_info,
                                      gpointer session_info)
{
  InfinotedPluginRecord* plugin;
  InfinotedPluginRecordSessionInfo* info;
  InfSession* session;
  gchar* title;
  gchar* pos;
  InfAdoptedSessionRecord* record;

  plugin = (InfinotedPluginRecord*)plugin_info;
  info = (InfinotedPluginRecordSessionInfo*)session_info;

  g_object_get(G_OBJECT(proxy), "session", &session, NULL);
  g_assert(INF_ADOPTED_IS_SESSION(session));

  title = inf_browser_get_path(
    INF_BROWSER(infinoted_plugin_manager_get_directory(plugin->manager)),
    iter
  );

  for(pos = title + 1; *pos != '\0'; ++pos)
    if(*pos == '/')
      *pos = '_';

  info->plugin = plugin;
  info->record = infinoted_plugin_record_start(
    plugin,
    INF_ADOPTED_SESSION(session),
    title + 1
  );

  g_object_set_data(G_OBJECT(session), "infinoted-record", info->record);

  g_object_unref(session);
  g_free(title);
}

static void
infinoted_plugin_record_session_removed(const InfBrowserIter* iter,
                                        InfSessionProxy* proxy,
                                        gpointer plugin_info,
                                        gpointer session_info)
{
  InfinotedPluginRecordSessionInfo* info;
  InfSession* session;

  info = (InfinotedPluginRecordSessionInfo*)session_info;

  g_object_get(G_OBJECT(proxy), "session", &session, NULL);
  g_object_set_data(G_OBJECT(session), "infinoted-record", NULL);
  
  g_object_unref(info->record);
  g_object_unref(session);
}

static const InfinotedParameterInfo INFINOTED_PLUGIN_RECORD_OPTIONS[] = {
  {
    NULL,
    0,
    0,
    0,
    NULL
  }
};

const InfinotedPlugin INFINOTED_PLUGIN = {
  "record",
  N_("Creates a recording of each session that can be replayed later. The "
     "records are created in the ~/.infinoted-records directory."),
  INFINOTED_PLUGIN_RECORD_OPTIONS,
  sizeof(InfinotedPluginRecord),
  0,
  sizeof(InfinotedPluginRecordSessionInfo),
  "InfAdoptedSession",
  infinoted_plugin_record_info_initialize,
  infinoted_plugin_record_initialize,
  infinoted_plugin_record_deinitialize,
  NULL,
  NULL,
  infinoted_plugin_record_session_added,
  infinoted_plugin_record_session_removed
};

/* vim:set et sw=2 ts=2: */
