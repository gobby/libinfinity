/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2011 Armin Burgmeier <armin@arbur.net>
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

/**
 * SECTION:inf-browser-request
 * @title: InfBrowserRequest
 * @short_description: Asynchronous browser request
 * @include: libinfinity/common/inf-browser-request.h
 * @see_also: #InfBrowser, #InfRequest, #InfcNodeRequest
 * @stability: Unstable
 *
 * #InfBrowserRequest represents a request that has been made via the
 * #InfBrowser API. Usually such a request is asynchronous, for example
 * because it waits for a response from an infinote server or because it
 * performs I/O. The #InfBrowserRequest class is used to monitor progress and
 * being notified when the request finishes.
 */

#include <libinfinity/common/inf-browser-request.h>
#include <libinfinity/common/inf-request.h>
#include <libinfinity/inf-marshal.h>

enum {
  FINISHED,

  LAST_SIGNAL
};

static guint browser_request_signals[LAST_SIGNAL];

static void
inf_browser_request_base_init(gpointer g_class)
{
  static gboolean initialized = FALSE;

  if(!initialized)
  {
    /**
     * InfBrowserRequest::finished:
     * @browser: The #InfBrowserRequest which finished.
     * @iter: A #InfBrowserIter corresponding to the node associated to the
     * request.
     * @error: Error information in case the request failed, or %NULL
     * otherwise.
     *
     * This signal is emitted when the request finishes. If @error is
     * non-%NULL the request failed, otherwise it finished successfully.
     */
    browser_request_signals[FINISHED] = g_signal_new(
      "finished",
      INF_TYPE_BROWSER_REQUEST,
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET(InfBrowserRequestIface, finished),
      NULL, NULL,
      inf_marshal_VOID__BOXED_POINTER,
      G_TYPE_NONE,
      1,
      INF_TYPE_BROWSER_ITER | G_SIGNAL_TYPE_STATIC_SCOPE,
      G_TYPE_POINTER /* GError* */
    );

    g_object_interface_install_property(
      g_class,
      g_param_spec_double(
        "progress",
        "Progress",
        "The progress of the operation represented as a floating point "
        "number between 0.0 and 1.0",
        0.0,
        1.0,
        0.0,
        G_PARAM_READABLE
      )
    );

    initialized = TRUE;
  }
}

GType
inf_browser_request_get_type(void)
{
  static GType browser_request_type = 0;

  if(!browser_request_type)
  {
    static const GTypeInfo browser_request_info = {
      sizeof(InfBrowserRequestIface),     /* class_size */
      inf_browser_request_base_init,      /* base_init */
      NULL,                               /* base_finalize */
      NULL,                               /* class_init */
      NULL,                               /* class_finalize */
      NULL,                               /* class_data */
      0,                                  /* instance_size */
      0,                                  /* n_preallocs */
      NULL,                               /* instance_init */
      NULL                                /* value_table */
    };

    browser_request_type = g_type_register_static(
      G_TYPE_INTERFACE,
      "InfBrowserRequest",
      &browser_request_info,
      0
    );

    g_type_interface_add_prerequisite(browser_request_type, INF_TYPE_REQUEST);
  }

  return browser_request_type;
}

/**
 * inf_browser_request_finished:
 * @request: A #InfBrowserRequest.
 * @iter: An iterator pointing to the node affected by the request.
 * @error: A #GError containing error information in case the request failed,
 * or %NULL otherwise.
 *
 * This function emits the #InfBrowserRequest::finished signal on @request.
 * It is meant to be used by interface implementations only.
 */
void
inf_browser_request_finished(InfBrowserRequest* request,
                             InfBrowserIter* iter,
                             const GError* error)
{
  g_return_if_fail(INF_IS_BROWSER_REQUEST(request));

  g_signal_emit(
    request,
    browser_request_signals[FINISHED],
    0,
    iter,
    error
  );
}

/* vim:set et sw=2 ts=2: */
