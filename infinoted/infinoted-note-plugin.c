/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008, 2009 Armin Burgmeier <armin@arbur.net>
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

#include <infinoted/infinoted-note-plugin.h>
#include <infinoted/infinoted-util.h>
#include <libinfinity/inf-i18n.h>

#include <gmodule.h>

#include <string.h>

/**
 * infinoted_note_plugin_load:
 * @plugin_path: Path to a note plugin.
 * @error: Location to store error information, if any.
 *
 * Tries to load the plugin at @plugin_path. Such a plugin must be a shared
 * object that exports a symbol called "INFD_NOTE_PLUGIN" of type
 * #InfdNotePlugin. If the plugin could not be load, the function returns
 * %NULL and @error is set, otherwise it returns the loaded #InfdNotePlugin.
 *
 * Return Value: A #InfdNotePlugin, or %NULL.
 **/
const InfdNotePlugin*
infinoted_note_plugin_load(const gchar* plugin_path,
                           GError** error)
{
  GModule* module;
  InfdNotePlugin* plugin;

  module = g_module_open(plugin_path, G_MODULE_BIND_LOCAL);

  if(module == NULL)
  {
    g_set_error(
      error,
      g_quark_from_static_string("INFINOTED_NOTE_PLUGIN_ERROR"),
      INFINOTED_NOTE_PLUGIN_ERROR_OPEN_FAILED,
      "%s",
      g_module_error()
    );

    return NULL;
  }

  if(g_module_symbol(module, "INFD_NOTE_PLUGIN", (gpointer*)&plugin) == FALSE)
  {
    g_set_error(
      error,
      g_quark_from_static_string("INFINOTED_NOTE_PLUGIN_ERROR"),
      INFINOTED_NOTE_PLUGIN_ERROR_NO_ENTRY_POINT,
      "%s",
      g_module_error()
    );
    
    g_module_close(module);
    return NULL;
  }

  g_module_make_resident(module);
  g_module_close(module); /* TODO: Don't necessary? */
  return plugin;
}

/**
 * infinoted_note_plugin_load_directory:
 * Loads a directory that contains note plugins and adds them to the given
 * @directory. The directory should only contain valid plugins. A warning for
 * each plugin that could not be load is issued.
 **/
gboolean
infinoted_note_plugin_load_directory(const gchar* path,
                                     InfdDirectory* directory)
{
  GDir* dir;
  GError* error;
  InfdStorage* storage;
  const gchar* storage_type;
  const gchar* filename;
  const InfdNotePlugin* plugin;
  gchar* plugin_path;
  gboolean has_plugins;

  error = NULL;
  dir = g_dir_open(path, 0, &error);
  if(dir == NULL)
  {
    g_warning("%s", error->message);
    g_error_free(error);
    return FALSE;
  }
  else
  {
    storage = infd_directory_get_storage(directory);
    storage_type = g_type_name(G_TYPE_FROM_INSTANCE(storage));
    has_plugins = FALSE;

    while((filename = g_dir_read_name(dir)) != NULL)
    {
      /* Ignore libtool ".la" files and other uninteresting stuff */
      if(!g_str_has_suffix(filename, G_MODULE_SUFFIX))
        continue;

      plugin_path = g_build_filename(path, filename, NULL);
      plugin = infinoted_note_plugin_load(plugin_path, &error);

      if(plugin == NULL)
      {
        g_warning(
          "%s",
          error->message
        );

        g_error_free(error);
      }
      else
      {
        if(infd_directory_lookup_plugin(directory, plugin->note_type) != NULL)
        {
          g_warning(
            _("Failed to load plugin \"%s\": Note type \"%s\" is "
              "already handled by another plugin"),
            plugin_path,
            plugin->note_type
          );
        }
        else
        {
          if(strcmp(storage_type, plugin->storage_type) != 0)
          {
            g_warning(
              _("Failed to load plugin \"%s\": "
                "Storage type \"%s\" does not match"),
              plugin_path,
              plugin->storage_type
            );
          }
          else
          {
            infinoted_util_log_info(
              _("Loaded plugin \"%s\" (%s)"),
              plugin_path,
              plugin->note_type
            );

            infd_directory_add_plugin(directory, plugin);
            has_plugins = TRUE;
          }
        }

        /* TODO: We should unload the plugin here since we don't need it */
      }

      g_free(plugin_path);
    }

    g_dir_close(dir);

    if(has_plugins == FALSE)
    {
      g_warning(_("Path \"%s\" does not contain any note plugins"), path);
      return FALSE;
    }
    else
    {
      return TRUE;
    }
  }
}

/* vim:set et sw=2 ts=2: */
