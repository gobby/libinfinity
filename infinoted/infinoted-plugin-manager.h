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

#ifndef __INFINOTED_PLUGIN_MANAGER_H__
#define __INFINOTED_PLUGIN_MANAGER_H__

/* Note that this class is compiled into an own shared library. Therefore, it
 * must not use any other infinoted API! The reason for this is to allow
 * loaded plugins to call plugin manager functions. Calling symbols from the
 * application itself would not be portable, so this needs to reside in a
 * shared library.
 *
 * The only API allowed to be used is what is declared in
 * infinoted-parameter.h and infinoted-log. The reason for this is that this
 * code is also included in the shared library. This allows parameter parsing
 * and central logging for plugins. */

#include <infinoted/infinoted-parameter.h>
#include <infinoted/infinoted-log.h>

#include <libinfinity/server/infd-directory.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INFINOTED_TYPE_PLUGIN_MANAGER                 (infinoted_plugin_manager_get_type())
#define INFINOTED_PLUGIN_MANAGER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INFINOTED_TYPE_PLUGIN_MANAGER, InfinotedPluginManager))
#define INFINOTED_PLUGIN_MANAGER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INFINOTED_TYPE_PLUGIN_MANAGER, InfinotedPluginManagerClass))
#define INFINOTED_IS_PLUGIN_MANAGER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INFINOTED_TYPE_PLUGIN_MANAGER))
#define INFINOTED_IS_PLUGIN_MANAGER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INFINOTED_TYPE_PLUGIN_MANAGER))
#define INFINOTED_PLUGIN_MANAGER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INFINOTED_TYPE_PLUGIN_MANAGER, InfinotedPluginManagerClass))

typedef struct _InfinotedPluginManager InfinotedPluginManager;
typedef struct _InfinotedPluginManagerClass InfinotedPluginManagerClass;

typedef struct _InfinotedPlugin InfinotedPlugin;
struct _InfinotedPlugin {
  const gchar* name;
  const gchar* description;
  const InfinotedParameterInfo* options;

  gsize info_size;
  gsize connection_info_size;
  gsize session_info_size;
  const gchar* session_type;

  void(*on_info_initialize)(gpointer plugin_info);

  gboolean(*on_initialize)(InfinotedPluginManager* manager,
                           gpointer plugin_info,
                           GError** error);

  void(*on_deinitialize)(gpointer plugin_info);

  void(*on_connection_added)(InfXmlConnection* connection,
                             gpointer plugin_info,
                             gpointer connection_info);

  void(*on_connection_removed)(InfXmlConnection* connection,
                               gpointer plugin_info,
                               gpointer connection_info);

  void(*on_session_added)(const InfBrowserIter* iter,
                          InfSessionProxy* proxy,
                          gpointer plugin_info,
                          gpointer session_info);

  void(*on_session_removed)(const InfBrowserIter* iter,
                            InfSessionProxy* proxy,
                            gpointer plugin_info,
                            gpointer session_info);
};

/**
 * InfinotedPluginManagerClass:
 *
 * This structure does not contain any public fields.
 */
struct _InfinotedPluginManagerClass {
  /*< private >*/
  GObjectClass parent_class;
};

/**
 * InfinotedPluginManager:
 *
 * #InfinotedPluginManager is an opaque data type. You should only access it
 * via the public API functions.
 */
struct _InfinotedPluginManager {
  /*< private >*/
  GObject parent;
};

typedef void(*InfinotedPluginManagerQueryUserFunc)(gpointer plugin_info,
                                                   InfUser* user,
                                                   const GError* error);

typedef enum _InfinotedPluginManagerError {
  INFINOTED_PLUGIN_MANAGER_ERROR_OPEN_FAILED,
  INFINOTED_PLUGIN_MANAGER_ERROR_NO_ENTRY_POINT
} InfinotedPluginManagerError;

GType
infinoted_plugin_manager_get_type(void) G_GNUC_CONST;

InfinotedPluginManager*
infinoted_plugin_manager_new(InfdDirectory* directory,
                             InfinotedLog* log,
                             InfCertificateCredentials* creds);

gboolean
infinoted_plugin_manager_load(InfinotedPluginManager* manager,
                              const gchar* plugin_path,
                              const gchar* const* plugins,
                              GKeyFile* options,
                              GError** error);

InfdDirectory*
infinoted_plugin_manager_get_directory(InfinotedPluginManager* manager);

InfIo*
infinoted_plugin_manager_get_io(InfinotedPluginManager* manager);

InfinotedLog*
infinoted_plugin_manager_get_log(InfinotedPluginManager* manager);

InfCertificateCredentials*
infinoted_plugin_manager_get_credentials(InfinotedPluginManager* manager);

GQuark
infinoted_plugin_manager_error_quark(void);

gpointer
infinoted_plugin_manager_get_connection_info(InfinotedPluginManager* mgr,
                                             gpointer plugin_info,
                                             InfXmlConnection* connection);

gpointer
infinoted_plugin_manager_get_session_info(InfinotedPluginManager* mgr,
                                          gpointer plugin_info,
                                          InfSessionProxy* proxy);

G_END_DECLS

#endif /* __INFINOTED_PLUGIN_MANAGER_H__ */

/* vim:set et sw=2 ts=2: */
