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

typedef struct _InfinotedPluginTransformationProtection
  InfinotedPluginTransformationProtection;
struct _InfinotedPluginTransformationProtection {
  InfinotedPluginManager* manager;
  guint max_vdiff;
};

typedef struct _InfinotedPluginTransformationProtectionSessionInfo
  InfinotedPluginTransformationProtectionSessionInfo;
struct _InfinotedPluginTransformationProtectionSessionInfo {
  InfinotedPluginTransformationProtection* plugin;
  InfSessionProxy* proxy;
  InfBrowserIter iter;
};

static gboolean
infinoted_plugin_transformation_protection_check_request_cb(InfAdoptedSession* session,
                                                            InfAdoptedRequest* request,
                                                            InfAdoptedUser* user,
                                                            gpointer user_data)
{
  InfinotedPluginTransformationProtectionSessionInfo* info;
  guint vdiff;
  InfXmlConnection* connection;
  gchar* request_str;
  gchar* current_str;
  gchar* remote_id;
  gchar* path;

  info = (InfinotedPluginTransformationProtectionSessionInfo*)user_data;

  vdiff = inf_adopted_state_vector_vdiff(
    inf_adopted_request_get_vector(request),
    inf_adopted_algorithm_get_current(
      inf_adopted_session_get_algorithm(session)
    )
  );

  if(vdiff > info->plugin->max_vdiff)
  {
    connection = inf_user_get_connection(INF_USER(user));

    /* Local requests do not need to be transformed, so always have a
     * zero vdiff. */
    g_assert(connection != NULL);

    /* Kill the connection */
    infd_session_proxy_unsubscribe(
      INFD_SESSION_PROXY(info->proxy),
      connection
    );

    /* Write a log message */
    path = inf_browser_get_path(
      INF_BROWSER(
        infinoted_plugin_manager_get_directory(info->plugin->manager)
      ),
      &info->iter
    );

    request_str = inf_adopted_state_vector_to_string(
      inf_adopted_request_get_vector(request)
    );

    current_str = inf_adopted_state_vector_to_string(
      inf_adopted_algorithm_get_current(
        inf_adopted_session_get_algorithm(session)
      )
    );

    g_object_get(G_OBJECT(connection), "remote-id", &remote_id, NULL);

    infinoted_log_warning(
      infinoted_plugin_manager_get_log(info->plugin->manager),
      _("In document \"%s\": Attempt to transform request \"%s\" to current state \"%s\" "
        "(vdiff=%u) by user \"%s\" (id=%u, conn=%s). Maximum allowed is %u; the "
        "connection has been unsubscribed."),
      path,
      request_str,
      current_str,
      vdiff,
      inf_user_get_name(INF_USER(user)),
      inf_user_get_id(INF_USER(user)),
      remote_id,
      info->plugin->max_vdiff
    );

    g_free(path);
    g_free(request_str);
    g_free(current_str);
    g_free(remote_id);

    /* Prevent the request from being transformed */
    return TRUE;
  }

  return FALSE;
}

static gboolean
infinoted_plugin_transformation_protection_initialize(
  InfinotedPluginManager* manager,
  gpointer plugin_info,
  GError** error)
{
  InfinotedPluginTransformationProtection* plugin;
  plugin = (InfinotedPluginTransformationProtection*)plugin_info;

  plugin->manager = manager;
}

static void
infinoted_plugin_transformation_protection_deinitialize(gpointer plugin_info)
{
  InfinotedPluginTransformationProtection* plugin;
  plugin = (InfinotedPluginTransformationProtection*)plugin_info;
}

static void
infinoted_plugin_transformation_protection_session_added(
  const InfBrowserIter* iter,
  InfSessionProxy* proxy,
  gpointer plugin_info,
  gpointer session_info)
{
  InfinotedPluginTransformationProtectionSessionInfo* info;
  InfSession* session;
  
  info = (InfinotedPluginTransformationProtectionSessionInfo*)session_info;
  info->plugin = (InfinotedPluginTransformationProtection*)plugin_info;
  info->proxy = proxy;
  info->iter = *iter;
  g_object_ref(proxy);
  
  g_object_get(G_OBJECT(proxy), "session", &session, NULL);
  g_assert(INF_ADOPTED_IS_SESSION(session));

  /* TODO: Check that the subscription group of 
     session uses the central method */

  g_signal_connect(
    G_OBJECT(session),
    "check-request",
    G_CALLBACK(infinoted_plugin_transformation_protection_check_request_cb),
    info
  );

  g_object_unref(session);
}

static void
infinoted_plugin_transformation_protection_session_removed(
  const InfBrowserIter* iter,
  InfSessionProxy* proxy,
  gpointer plugin_info,
  gpointer session_info)
{
  InfinotedPluginTransformationProtectionSessionInfo* info;
  InfSession* session;

  info = (InfinotedPluginTransformationProtectionSessionInfo*)session_info;
  
  g_object_get(G_OBJECT(proxy), "session", &session, NULL);

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(session),
    G_CALLBACK(infinoted_plugin_transformation_protection_check_request_cb),
    info
  );

  g_object_unref(info->proxy);
  g_object_unref(session);
}

static const InfinotedParameterInfo
INFINOTED_PLUGIN_TRANSFORMATION_PROTECTION_OPTIONS[] = {
  {
    "max-vdiff",
    INFINOTED_PARAMETER_INT,
    INFINOTED_PARAMETER_REQUIRED,
    offsetof(InfinotedPluginTransformationProtection, max_vdiff),
    infinoted_parameter_convert_nonnegative,
    0,
    N_("The maximum number of individual transformations to allow. If a "
       "client makes a request that would require more than this number of "
       "transformations, the request is rejected and the client is "
       "unsubscribed from the session."),
    N_("DIFF")
  }, {
    NULL,
    0,
    0,
    0,
    NULL
  }
};

const InfinotedPlugin INFINOTED_PLUGIN = {
  "transformation-protection",
  N_("This plugin tries to protect the server from malicious clients that "
     "send formally valid requests but would take the server a long time "
     "to process, making in unresponsive to other requests. This is only "
     "possible if sessions use the \"central\" communication method. At the "
     "moment this is the only method available, so the plugin can always be "
     "used. Currently the plugin rejects requests that were made in a state "
     "too far behind the current state. However, additional criteria might "
     "be implemented in future versions."),
  INFINOTED_PLUGIN_TRANSFORMATION_PROTECTION_OPTIONS,
  sizeof(InfinotedPluginTransformationProtection),
  0,
  sizeof(InfinotedPluginTransformationProtectionSessionInfo),
  "InfAdoptedSession",
  NULL,
  infinoted_plugin_transformation_protection_initialize,
  infinoted_plugin_transformation_protection_deinitialize,
  NULL,
  NULL,
  infinoted_plugin_transformation_protection_session_added,
  infinoted_plugin_transformation_protection_session_removed
};

/* vim:set et sw=2 ts=2: */
