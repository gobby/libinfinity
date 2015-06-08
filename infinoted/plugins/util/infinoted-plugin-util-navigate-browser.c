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

#include <infinoted/plugins/util/infinoted-plugin-util-navigate-browser.h>

#include <libinfinity/common/inf-request-result.h>
#include <libinfinity/inf-signals.h>
#include <libinfinity/inf-i18n.h>

#include <string.h>

struct _InfinotedPluginUtilNavigateData {
  gboolean initial;
  gchar* path;
  gsize len;
  gsize offset;
  gboolean explore_last;
  InfinotedPluginUtilNavigateCallback cb;
  gpointer user_data;
  InfRequest* request;
};

static void
infinoted_plugin_util_navigate_explore_cb(InfRequest* request,
                                          const InfRequestResult* result,
                                          const GError* error,
                                          gpointer user_data);

static void
infinoted_plugin_util_navigate_one(InfBrowser* browser,
                                   const InfBrowserIter* iter,
                                   InfinotedPluginUtilNavigateData* data);

static void
infinoted_plugin_util_navigate_data_done(InfinotedPluginUtilNavigateData* dat,
                                         InfBrowser* browser,
                                         const InfBrowserIter* iter,
                                         const GError* error)
{
  if(dat->request != NULL)
  {
    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(dat->request),
      G_CALLBACK(infinoted_plugin_util_navigate_explore_cb),
      dat
    );

    dat->request = NULL;
  }

  if(dat->cb != NULL)
  {
    dat->cb(browser, iter, error, dat->user_data);
    dat->cb = NULL;
  }

  g_free(dat->path);
  dat->path = NULL;

  if(!dat->initial)
    g_slice_free(InfinotedPluginUtilNavigateData, dat);
}

static void
infinoted_plugin_util_navigate_explored(InfBrowser* browser,
                                        const InfBrowserIter* iter,
                                        InfinotedPluginUtilNavigateData* data)
{
  InfBrowserIter child_iter;
  const gchar* name;
  GError* error;

  g_assert(inf_browser_is_subdirectory(browser, iter));
  g_assert(inf_browser_get_explored(browser, iter));

  /* In case we explored the last element */
  if(data->offset == data->len)
  {
    infinoted_plugin_util_navigate_data_done(data, browser, iter, NULL);
    return;
  }

  /* Find the name of the next element */
  gsize sep;
  for(sep = data->offset; sep < data->len; ++sep)
    if(data->path[sep] == '/')
      break;

  /* Find the node */
  child_iter = *iter;
  if(inf_browser_get_child(browser, &child_iter))
  {
    do
    {
      name = inf_browser_get_node_name(browser, &child_iter);
      if(strncmp(&data->path[data->offset], name, sep - data->offset) == 0 &&
         name[sep - data->offset] == '\0')
      {
        /* Found the child node, now proceed with next iteration */
        if(sep < data->len)
        {
          g_assert(data->path[sep] == '/');
          data->offset = sep + 1;
        }
        else
        {
          data->offset = sep;
        }

        infinoted_plugin_util_navigate_one(browser, &child_iter, data);
        return;
      }
    } while(inf_browser_get_next(browser, &child_iter));
  }

  error = NULL;
  g_set_error(
    &error,
    infinoted_plugin_util_navigate_error_quark(),
    INFINOTED_PLUGIN_UTIL_NAVIGATE_ERROR_NOT_EXIST,
    _("The path \"%.*s\" does not exist"),
    (int)sep,
    data->path
  );

  infinoted_plugin_util_navigate_data_done(data, NULL, NULL, error);
  g_error_free(error);
}

static void
infinoted_plugin_util_navigate_explore_cb(InfRequest* request,
                                          const InfRequestResult* result,
                                          const GError* error,
                                          gpointer user_data)
{
  InfinotedPluginUtilNavigateData* data;
  InfBrowser* browser;
  const InfBrowserIter* iter;
  GError* prefixed;

  data = (InfinotedPluginUtilNavigateData*)user_data;

  g_assert(data->request == NULL || data->request == request);
  data->request = NULL;

  if(error != NULL)
  {
    /* Failed to explore */
    prefixed = NULL;

    g_propagate_prefixed_error(
      &prefixed,
      (GError*)error, /* should be const... */
      _("Failed to explore path \"%.*s\": "),
      (int)data->offset,
      data->path
    );

    infinoted_plugin_util_navigate_data_done(data, NULL, NULL, prefixed);
    g_error_free(prefixed);
  }
  else
  {
    inf_request_result_get_explore_node(result, &browser, &iter);
    infinoted_plugin_util_navigate_explored(browser, iter, data);
  }
}

static void
infinoted_plugin_util_navigate_one(InfBrowser* browser,
                                   const InfBrowserIter* iter,
                                   InfinotedPluginUtilNavigateData* data)
{
  GError* error;
  InfRequest* request;

  g_assert(data->request == NULL);
  g_assert(data->offset <= data->len);

  if(data->offset == data->len && data->explore_last == FALSE)
  {
    infinoted_plugin_util_navigate_data_done(data, browser, iter, NULL);
  }
  else
  {
    /* We have to proceed further, but can only do this if the current node
     * is a directory. This case happens when /foo/bar is requested, but
     * /foo is a leaf node. */
    if(!inf_browser_is_subdirectory(browser, iter))
    {
      error = NULL;

      g_set_error(
        &error,
        infinoted_plugin_util_navigate_error_quark(),
        INFINOTED_PLUGIN_UTIL_NAVIGATE_ERROR_NOT_EXIST,
        _("The path \"%.*s\" does not exist or is not a directory"),
        (int)data->len,
        data->path
      );

      infinoted_plugin_util_navigate_data_done(data, browser, iter, error);
    }
    else
    {
      if(inf_browser_get_explored(browser, iter))
      {
        infinoted_plugin_util_navigate_explored(browser, iter, data);
      }
      else
      {
        request =
          inf_browser_get_pending_request(browser, iter, "explore-node");

        if(request == NULL)
        {
          request = inf_browser_explore(
            browser,
            iter,
            infinoted_plugin_util_navigate_explore_cb,
            data
          );

          if(request != NULL)
            data->request = request;
        }
        else
        {
          data->request = request;

          g_signal_connect(
            G_OBJECT(data->request),
            "finished",
            G_CALLBACK(infinoted_plugin_util_navigate_explore_cb),
            data
          );
        }
      }
    }
  }
}

GQuark
infinoted_plugin_util_navigate_error_quark(void)
{
  return g_quark_from_static_string("INFINOTED_PLUGIN_UTIL_NAVIGATE_ERROR");
}

InfinotedPluginUtilNavigateData*
infinoted_plugin_util_navigate_to(InfBrowser* browser,
                                  const gchar* path,
                                  gsize len,
                                  gboolean explore_last,
                                  InfinotedPluginUtilNavigateCallback cb,
                                  gpointer user_data)
{
  GError* error;
  InfBrowserIter iter;
  InfinotedPluginUtilNavigateData* data;

  if(len == 0 || path[0] != '/')
  {
    error = NULL;
    g_set_error(
      &error,
      infinoted_plugin_util_navigate_error_quark(),
      INFINOTED_PLUGIN_UTIL_NAVIGATE_ERROR_PATH_NOT_ABSOLUTE,
      _("The path \"%.*s\" is not an absolute path"),
      (int)len,
      path
    );

    cb(browser, NULL, error, user_data);
    g_error_free(error);

    return NULL;
  }

  data = g_slice_new(InfinotedPluginUtilNavigateData);
  data->initial = TRUE;
  data->path = g_memdup(path, len);
  data->len = len;
  data->offset = 1;
  data->explore_last = explore_last;
  data->cb = cb;
  data->user_data = user_data;
  data->request = NULL;

  inf_browser_get_root(browser, &iter);
  infinoted_plugin_util_navigate_one(browser, &iter, data);

  data->initial = FALSE;
  if(data->path == NULL)
  {
    infinoted_plugin_util_navigate_data_done(data, NULL, NULL, NULL);
    data = NULL;
  }

  return data;
}

void
infinoted_plugin_util_navigate_cancel(InfinotedPluginUtilNavigateData* data)
{
  data->cb = NULL;
  infinoted_plugin_util_navigate_data_done(data, NULL, NULL, NULL);
}

/* vim:set et sw=2 ts=2: */
