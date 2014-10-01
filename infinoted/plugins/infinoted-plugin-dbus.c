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

#include "util/infinoted-plugin-util-navigate-browser.h"

#include <infinoted/infinoted-plugin-manager.h>
#include <libinfinity/inf-i18n.h>

#include <gio/gio.h>

#include <string.h>

static const gchar infinoted_plugin_dbus_introspection[] =
  "<node>"
  "  <interface name='org.infinote.server'>"
  "    <method name='query_acl'>"
  "      <arg type='s' name='node' direction='in'/>"
  "      <arg type='s' name='account' direction='in'/>"
  "      <arg type='a{sa{sb}}' name='sheet-set' direction='out'/>"
  "    </method>"
  "    <method name='set_acl'>"
  "      <arg type='s' name='node' direction='in'/>"
  "      <arg type='a{sa{sb}}' name='sheet_set' direction='in'/>"
  "    </method>"
  "    <method name='check_acl'>"
  "      <arg type='s' name='node' direction='in'/>"
  "      <arg type='s' name='account' direction='in'/>"
  "      <arg type='as' name='permissions' direction='in'/>"
  "      <arg type='a{sb}' name='sheet' direction='out'/>"
  "    </method>"
  "  </interface>"
  "</node>";

typedef struct _InfinotedPluginDbus InfinotedPluginDbus;
struct _InfinotedPluginDbus {
  GBusType bus_type;
  gchar* bus_name;

  InfinotedPluginManager* manager;
  GMutex mutex;
  GThread* thread;

  GMainContext* context;
  GMainLoop* loop;
  guint id;

  GSList* invocations; /* invocations currently being processed */
};

typedef struct _InfinotedPluginDbusInvocation InfinotedPluginDbusInvocation;
struct _InfinotedPluginDbusInvocation {
  InfinotedPluginDbus* plugin;
  int ref_count;

  gchar* method_name;
  GVariant* parameters;
  GDBusMethodInvocation* invocation;

  InfinotedPluginUtilNavigateData* navigate;
  InfRequest* request;
  InfRequestFunc request_func;
};

static void
infinoted_plugin_dbus_invocation_unref(gpointer data)
{
  InfinotedPluginDbusInvocation* invocation;
  invocation = (InfinotedPluginDbusInvocation*)data;

  if(g_atomic_int_dec_and_test(&invocation->ref_count) == TRUE)
  {
    if(invocation->navigate != NULL)
      infinoted_plugin_util_navigate_cancel(invocation->navigate);

    if(invocation->request != NULL)
    {
      g_signal_handlers_disconnect_by_func(
        G_OBJECT(invocation->request),
        G_CALLBACK(invocation->request_func),
        invocation
      );
    }

    g_free(invocation->method_name);
    g_variant_unref(invocation->parameters);
    g_object_unref(invocation->invocation);

    g_slice_free(InfinotedPluginDbusInvocation, invocation);
  }
}

infinoted_plugin_dbus_invocation_free(InfinotedPluginDbus* plugin,
                                      InfinotedPluginDbusInvocation* inv)
{
  plugin->invocations = g_slist_remove(plugin->invocations, inv);
  infinoted_plugin_dbus_invocation_unref(inv);
}

static GVariant*
infinoted_plugin_dbus_perms_to_variant(const InfAclMask* mask,
                                       const InfAclMask* perms)
{
  GVariantBuilder builder;
  GEnumClass* enum_class;
  guint i;

  enum_class = G_ENUM_CLASS(g_type_class_ref(INF_TYPE_ACL_SETTING));
  g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sb}"));
  for(i = 0; i < enum_class->n_values; ++i)
  {
    if(inf_acl_mask_has(mask, enum_class->values[i].value))
    {
      g_variant_builder_add(
        &builder,
        "{sb}",
        enum_class->values[i].value_nick,
        inf_acl_mask_has(perms, enum_class->values[i].value)
      );
    }
  }

  g_type_class_unref(enum_class);
  return g_variant_builder_end(&builder);
}

static gboolean
infinoted_plugin_dbus_mask_from_variant(InfAclMask* mask,
                                        GVariant* variant,
                                        GError** error)
{
  GEnumClass* enum_class;
  GEnumValue* val;
  GVariantIter iter;
  const gchar* perm;

  inf_acl_mask_clear(mask);

  enum_class = G_ENUM_CLASS(g_type_class_ref(INF_TYPE_ACL_SETTING));
  g_variant_iter_init(&iter, variant);
  while(g_variant_iter_next(&iter, "&s", &perm))
  {
    val = g_enum_get_value_by_nick(enum_class, perm);
    if(val == NULL)
    {
      g_set_error(
        error,
        G_DBUS_ERROR,
        G_DBUS_ERROR_INVALID_ARGS,
        "No such permission: \"%s\"",
        perm
      );

      g_type_class_unref(enum_class);
      return FALSE;
    }

    inf_acl_mask_or1(mask, val->value);
  }

  g_type_class_unref(enum_class);
  return TRUE;
}

static gboolean
infinoted_plugin_dbus_perms_from_variant(InfAclMask* mask,
                                         InfAclMask* perms,
                                         GVariant* variant,
                                         GError** error)
{
  GEnumClass* enum_class;
  GEnumValue* val;
  GVariantIter iter;
  const gchar* perm;
  gboolean set;

  inf_acl_mask_clear(mask);
  inf_acl_mask_clear(perms);

  enum_class = G_ENUM_CLASS(g_type_class_ref(INF_TYPE_ACL_SETTING));
  g_variant_iter_init(&iter, variant);
  while(g_variant_iter_next(&iter, "{&sb}", &perm, &set))
  {
    val = g_enum_get_value_by_nick(enum_class, perm);
    if(val == NULL)
    {
      g_set_error(
        error,
        G_DBUS_ERROR,
        G_DBUS_ERROR_INVALID_ARGS,
        "No such permission: \"%s\"",
        perm
      );

      g_type_class_unref(enum_class);
      return FALSE;
    }

    inf_acl_mask_or1(mask, val->value);
    if(set == TRUE)
      inf_acl_mask_or1(perms, val->value);
  }

  g_type_class_unref(enum_class);
  return TRUE;
}

static GVariant*
infinoted_builder_dbus_sheet_set_to_variant(const InfAclSheetSet* sheet_set)
{
  GVariantBuilder builder;
  const InfAclSheet* sheet;
  guint i;

  g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sa{sb}}"));

  if(sheet_set != NULL)
  {
    for(i = 0; i < sheet_set->n_sheets; ++i)
    {
      sheet = &sheet_set->sheets[i];
      g_variant_builder_add(
        &builder,
        "{s@a{sb}}",
        inf_acl_account_id_to_string(sheet->account),
        infinoted_plugin_dbus_perms_to_variant(&sheet->mask, &sheet->perms)
      );
    }
  }

  return g_variant_builder_end(&builder);
}

static InfAclSheetSet*
infinoted_plugin_dbus_sheet_set_from_variant(GVariant* variant,
                                             GError** error)
{
  InfAclSheetSet* sheet_set;
  GVariantIter iter;
  const gchar* account;
  GVariant* sub_variant;
  InfAclSheet* sheet;
  gboolean success;

  sheet_set = inf_acl_sheet_set_new();
  g_variant_iter_init(&iter, variant);
  while(g_variant_iter_loop(&iter, "{&s@a{sb}}", &account, &sub_variant))
  {
    sheet = inf_acl_sheet_set_add_sheet(
      sheet_set,
      inf_acl_account_id_from_string(account)
    );

    success = infinoted_plugin_dbus_perms_from_variant(
      &sheet->mask,
      &sheet->perms,
      sub_variant,
      error
    );

    if(success != TRUE)
    {
      inf_acl_sheet_set_free(sheet_set);
      g_variant_unref(sub_variant);
      return NULL;
    }
  }

  return sheet_set;
}

static void
infinoted_plugin_dbus_query_acl(InfinotedPluginDbus* plugin,
                                InfinotedPluginDbusInvocation* invocation,
                                InfBrowser* browser,
                                const InfBrowserIter* iter)
{
  const InfAclSheetSet* sheet_set;
  const InfAclSheet* sheet;
  const gchar* account;
  InfAclAccountId id;
  GVariantBuilder builder;

  /* TODO: Actually query the ACL if not available */
  sheet_set = inf_browser_get_acl(browser, iter);
  g_variant_get_child(invocation->parameters, 1, "&s", &account);

  if(*account == '\0')
  {
    g_dbus_method_invocation_return_value(
      invocation->invocation,
      g_variant_new(
        "(@a{sa{sb}})",
        infinoted_builder_dbus_sheet_set_to_variant(sheet_set)
      )
    );
  }
  else
  {
    id = inf_acl_account_id_from_string(account);
    if(sheet_set != NULL)
      sheet = inf_acl_sheet_set_find_const_sheet(sheet_set, id);
    else
      sheet = NULL;

    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sa{sb}}"));
    if(sheet != NULL)
    {
      g_variant_builder_add(
        &builder,
        "{s@a{sb}}",
        account,
        infinoted_plugin_dbus_perms_to_variant(&sheet->mask, &sheet->perms)
      );
    }

    g_dbus_method_invocation_return_value(
      invocation->invocation,
      g_variant_new("(@a{sa{sb}})", g_variant_builder_end(&builder))
    );
  }

  infinoted_plugin_dbus_invocation_free(plugin, invocation);
}

static void
infinoted_plugin_dbus_set_acl_finished_cb(InfRequest* request,
                                          const InfRequestResult* result,
                                          const GError* error,
                                          gpointer user_data)
{
  InfinotedPluginDbusInvocation* invocation;
  invocation = (InfinotedPluginDbusInvocation*)user_data;

  invocation->request = NULL;

  if(error != NULL)
  {
    g_dbus_method_invocation_return_error_literal(
      invocation->invocation,
      G_DBUS_ERROR,
      G_DBUS_ERROR_INVALID_ARGS,
      error->message
    );
  }
  else
  {
    g_dbus_method_invocation_return_value(
      invocation->invocation,
      g_variant_new_tuple(NULL, 0)
    );
  }

  infinoted_plugin_dbus_invocation_free(invocation->plugin, invocation);
}

static void
infinoted_plugin_dbus_set_acl(InfinotedPluginDbus* plugin,
                              InfinotedPluginDbusInvocation* invocation,
                              InfBrowser* browser,
                              const InfBrowserIter* iter)
{
  GVariant* sheet_set_variant;
  InfAclSheetSet* sheet_set;
  GError* error;
  InfRequest* request;

  g_variant_get_child(
    invocation->parameters,
    1,
    "@a{sa{sb}}",
    &sheet_set_variant
  );

  error = NULL;
  sheet_set = infinoted_plugin_dbus_sheet_set_from_variant(
    sheet_set_variant,
    &error
  );

  g_variant_unref(sheet_set_variant);

  if(error != NULL)
  {
    g_dbus_method_invocation_return_gerror(invocation->invocation, error);
    g_error_free(error);
    infinoted_plugin_dbus_invocation_free(plugin, invocation);
  }
  else
  {
    request = inf_browser_set_acl(
      browser,
      iter,
      sheet_set,
      infinoted_plugin_dbus_set_acl_finished_cb,
      invocation
    );

    inf_acl_sheet_set_free(sheet_set);

    if(request != NULL)
    {
      invocation->request = request;
      invocation->request_func = infinoted_plugin_dbus_set_acl_finished_cb;
    }
  }
}

static void
infinoted_plugin_dbus_check_acl(InfinotedPluginDbus* plugin,
                                InfinotedPluginDbusInvocation* invocation,
                                InfBrowser* browser,
                                const InfBrowserIter* iter)
{
  const gchar* account;
  GVariant* mask_variant;
  InfAclMask mask;
  InfAclMask out;
  GError* error;

  g_variant_get_child(invocation->parameters, 1, "&s", &account);
  g_variant_get_child(invocation->parameters, 2, "@as", &mask_variant);

  error = NULL;
  infinoted_plugin_dbus_mask_from_variant(&mask, mask_variant, &error);
  g_variant_unref(mask_variant);

  if(error != NULL)
  {
    g_dbus_method_invocation_return_gerror(invocation->invocation, error);
    g_error_free(error);
  }
  else
  {
    inf_browser_check_acl(
      browser,
      iter,
      inf_acl_account_id_from_string(account),
      &mask,
      &out
    );

    g_dbus_method_invocation_return_value(
      invocation->invocation,
      g_variant_new(
        "(@a{sb})",
        infinoted_plugin_dbus_perms_to_variant(&mask, &out)
      )
    );
  }

  infinoted_plugin_dbus_invocation_free(plugin, invocation);
}

static void
infinoted_plugin_dbus_navigate_done(InfBrowser* browser,
                                    const InfBrowserIter* iter,
                                    const GError* error,
                                    gpointer user_data)
{
  InfinotedPluginDbusInvocation* invocation;
  invocation = (InfinotedPluginDbusInvocation*)user_data;

  if(error != NULL)
  {
    g_dbus_method_invocation_return_error_literal(
      invocation->invocation,
      G_DBUS_ERROR,
      G_DBUS_ERROR_FILE_NOT_FOUND,
      error->message
    );

    infinoted_plugin_dbus_invocation_free(invocation->plugin, invocation);
  }
  else if(strcmp(invocation->method_name, "query_acl") == 0)
  {
    infinoted_plugin_dbus_query_acl(
      invocation->plugin,
      invocation,
      browser,
      iter
    );
  }
  else if(strcmp(invocation->method_name, "set_acl") == 0)
  {
    infinoted_plugin_dbus_set_acl(
      invocation->plugin,
      invocation,
      browser,
      iter
    );
  }
  else if(strcmp(invocation->method_name, "check_acl") == 0)
  {
    infinoted_plugin_dbus_check_acl(
      invocation->plugin,
      invocation,
      browser,
      iter
    );
  }
  else
  {
    g_assert_not_reached();
  }
}

static void
infinoted_plugin_dbus_main_invocation(gpointer user_data)
{
  /* Main thread invocation handler */
  InfinotedPluginDbusInvocation* invocation;
  const gchar* path;
  gsize len;
  InfinotedPluginUtilNavigateData* navigate;

  invocation = (InfinotedPluginDbusInvocation*)user_data;
  invocation->plugin->invocations =
    g_slist_prepend(invocation->plugin->invocations, invocation);
  g_atomic_int_inc(&invocation->ref_count);

  /* These commands take a path as the first parameter */
  if(strcmp(invocation->method_name, "query_acl") == 0 ||
     strcmp(invocation->method_name, "set_acl") == 0 ||
     strcmp(invocation->method_name, "check_acl") == 0)
  {
    path = g_variant_get_string(
      g_variant_get_child_value(invocation->parameters, 0),
      &len
    );

    navigate = infinoted_plugin_util_navigate_to(
      INF_BROWSER(infinoted_plugin_manager_get_directory(invocation->plugin->manager)),
      path,
      len,
      infinoted_plugin_dbus_navigate_done,
      invocation
    );
    
    if(navigate != NULL)
      invocation->navigate = navigate;
  }
  else
  {
    g_dbus_method_invocation_return_error_literal(
      invocation->invocation,
      G_DBUS_ERROR,
      G_DBUS_ERROR_UNKNOWN_METHOD,
      "Not implemented"
    );

    infinoted_plugin_dbus_invocation_free(invocation->plugin, invocation);
  }
}

static void
infinoted_plugin_dbus_method_call_func(GDBusConnection* connection,
                                       const gchar* sender,
                                       const gchar* object_path,
                                       const gchar* interface_name,
                                       const gchar* method_name,
                                       GVariant* parameters,
                                       GDBusMethodInvocation* invocation,
                                       gpointer user_data)
{
  /* Dispatch to the main thread */
  InfinotedPluginDbus* plugin;
  InfinotedPluginDbusInvocation* thread_invocation;
  InfIo* io;

  plugin = (InfinotedPluginDbus*)user_data;
  thread_invocation = g_slice_new(InfinotedPluginDbusInvocation);

  thread_invocation->plugin = plugin;
  thread_invocation->ref_count = 1;
  thread_invocation->method_name = g_strdup(method_name);
  thread_invocation->parameters = g_variant_ref(parameters);
  thread_invocation->invocation = g_object_ref(invocation);
  thread_invocation->navigate = NULL;
  thread_invocation->request = NULL;
  thread_invocation->request_func = NULL;

  io = infinoted_plugin_manager_get_io(plugin->manager);

  inf_io_add_dispatch(
    io,
    infinoted_plugin_dbus_main_invocation,
    thread_invocation,
    infinoted_plugin_dbus_invocation_unref
  );
}

static void
infinoted_plugin_dbus_bus_acquired_func(GDBusConnection* connection,
                                        const gchar* name,
                                        gpointer user_data)
{
  GDBusNodeInfo* node_info;
  GDBusInterfaceInfo* interface_info;
  GDBusInterfaceVTable vtable;
  GError* error;

  node_info = g_dbus_node_info_new_for_xml(
    infinoted_plugin_dbus_introspection,
    NULL
  );

  g_assert(node_info != NULL);

  interface_info = g_dbus_node_info_lookup_interface(
    node_info,
    "org.infinote.server"
  );

  g_assert(interface_info != NULL);

  vtable.method_call = infinoted_plugin_dbus_method_call_func;
  vtable.get_property = NULL;
  vtable.set_property = NULL;

  error = NULL;
  g_dbus_connection_register_object(
    connection,
    "/org/infinote/infinoted",
    interface_info,
    &vtable,
    user_data,
    NULL,
    &error
  );

  if(error != NULL)
  {
    g_warning("Failed to register D-Bus object: %s\n", error->message);
    g_error_free(error);
    error = NULL;
  }

  g_dbus_node_info_unref(node_info);
}

static void
infinoted_plugin_dbus_name_acquired_func(GDBusConnection* connection,
                                         const gchar* name,
                                         gpointer user_data)
{
  /* nothing to do */
}

static void
infinoted_plugin_dbus_name_lost_func(GDBusConnection* connection,
                                     const gchar* name,
                                     gpointer user_data)
{
  InfinotedPluginDbus* plugin;
  plugin = (InfinotedPluginDbus*)user_data;

  infinoted_log_warning(
    infinoted_plugin_manager_get_log(plugin->manager),
    "The name \"%s\" could not be acquired on the bus: "
    "d-bus functionality is not available",
    name
  );
}

static gpointer
infinoted_plugin_dbus_thread_func(gpointer plugin_info)
{
  InfinotedPluginDbus* plugin;
  plugin = (InfinotedPluginDbus*)plugin_info;

  g_mutex_lock(&plugin->mutex);
  if(plugin->thread == NULL)
  {
    g_mutex_unlock(&plugin->mutex);
    return NULL;
  }

  plugin->context = g_main_context_new();
  g_main_context_push_thread_default(plugin->context);

  plugin->loop = g_main_loop_new(plugin->context, TRUE);
  g_mutex_unlock(&plugin->mutex);

  plugin->id = g_bus_own_name(
    plugin->bus_type,
    plugin->bus_name,
    G_BUS_NAME_OWNER_FLAGS_NONE,
    infinoted_plugin_dbus_bus_acquired_func,
    infinoted_plugin_dbus_name_acquired_func,
    infinoted_plugin_dbus_name_lost_func,
    plugin,
    NULL
  );

  g_main_loop_run(plugin->loop);

  g_bus_unown_name(plugin->id);
  plugin->id = 0;

  g_mutex_lock(&plugin->mutex);
  g_main_loop_unref(plugin->loop);
  plugin->loop = NULL;

  g_main_context_unref(plugin->context);
  plugin->context = NULL;
  g_mutex_unlock(&plugin->mutex);

  return NULL;
}

static void
infinoted_plugin_dbus_info_initialize(gpointer plugin_info)
{
  InfinotedPluginDbus* plugin;
  plugin = (InfinotedPluginDbus*)plugin_info;

  plugin->bus_type = G_BUS_TYPE_SESSION; /* good default? */
  plugin->bus_name = g_strdup("org.infinote.infinoted");

  plugin->manager = NULL;
  plugin->thread = NULL;
  plugin->context = NULL;
  plugin->loop = NULL;
  plugin->id = 0;
  plugin->invocations = NULL;
}

static gboolean
infinoted_plugin_dbus_initialize(InfinotedPluginManager* manager,
                                 gpointer plugin_info,
                                 GError** error)
{
  InfinotedPluginDbus* plugin;
  plugin = (InfinotedPluginDbus*)plugin_info;

  plugin->manager = manager;
  g_mutex_init(&plugin->mutex);

  g_mutex_lock(&plugin->mutex);

  /* We run the DBus activity in its own thread, so that we can iterate
   * a glib main loop there. */
  plugin->thread = g_thread_try_new(
    "InfinotedPluginDbus",
    infinoted_plugin_dbus_thread_func,
    plugin_info,
    error
  );

  g_mutex_unlock(&plugin->mutex);

  if(plugin->thread == NULL)
  {
    g_mutex_clear(&plugin->mutex);
    return FALSE;
  }

  return TRUE;
}

static void
infinoted_plugin_dbus_deinitialize(gpointer plugin_info)
{
  InfinotedPluginDbus* plugin;
  GThread* thread;

  plugin = (InfinotedPluginDbus*)plugin_info;

  if(plugin->thread != NULL)
  {
    g_mutex_lock(&plugin->mutex);
    thread = plugin->thread;
    plugin->thread = NULL;

    if(plugin->loop != NULL)
      g_main_loop_quit(plugin->loop);
    g_mutex_unlock(&plugin->mutex);

    g_thread_join(thread);
    thread = NULL;

    g_mutex_clear(&plugin->mutex);
  }

  while(plugin->invocations != NULL)
  {
    infinoted_plugin_dbus_invocation_unref(plugin->invocations->data);
    plugin->invocations =
      g_slist_delete_link(plugin->invocations, plugin->invocations);
  }
}

static gboolean
infinoted_plugin_dbus_parameter_convert_bus_type(gpointer out,
                                                 gpointer in,
                                                 GError** error)
{
  gchar** in_str;
  GBusType* out_val;

  in_str = (gchar**)in;
  out_val = (GBusType*)out;

  if(strcmp(*in_str, "system") == 0)
  {
    *out_val = G_BUS_TYPE_SYSTEM;
  }
  else if(strcmp(*in_str, "session") == 0)
  {
    *out_val = G_BUS_TYPE_SESSION;
  }
  else
  {
    g_set_error(
      error,
      infinoted_parameter_error_quark(),
      INFINOTED_PARAMETER_ERROR_INVALID_FLAG,
      _("\"%s\" is not a valid bus type. Allowed values are "
        "\"system\" or \"session\""),
      *in_str
    );

    return FALSE;
  }

  return TRUE;
}

static const InfinotedParameterInfo INFINOTED_PLUGIN_DBUS_OPTIONS[] = {
  {
    "type",
    INFINOTED_PARAMETER_STRING,
    0,
    offsetof(InfinotedPluginDbus, bus_type),
    infinoted_plugin_dbus_parameter_convert_bus_type,
    0,
    N_("The bus type to use, either \"session\" or \"system\". "
       "[default=session]"),
    N_("TYPE")
  }, {
    "name",
    INFINOTED_PARAMETER_STRING,
    0,
    offsetof(InfinotedPluginDbus, bus_name),
    infinoted_parameter_convert_string,
    0,
    N_("The name to own on the bus. [default=org.infinote.infinoted]"),
    N_("NAME")
  }, {
    NULL,
    0,
    0,
    0,
    NULL
  }
};

const InfinotedPlugin INFINOTED_PLUGIN = {
  "dbus",
  N_("Exports infinoted functionality on D-Bus"),
  INFINOTED_PLUGIN_DBUS_OPTIONS,
  sizeof(InfinotedPluginDbus),
  0,
  0,
  NULL,
  infinoted_plugin_dbus_info_initialize,
  infinoted_plugin_dbus_initialize,
  infinoted_plugin_dbus_deinitialize,
  NULL,
  NULL,
  NULL,
  NULL
};

/* vim:set et sw=2 ts=2: */
