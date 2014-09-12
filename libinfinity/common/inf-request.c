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

/**
 * SECTION:inf-request
 * @title: InfRequest
 * @short_description: Asynchronous request
 * @include: libinfinity/common/inf-request.h
 * @stability: Unstable
 *
 * #InfRequest represents a potentially asynchronous operation. This is a
 * basic interface which allows to query the type of the operation and
 * to be notified when the request finishes.
 */

#include <libinfinity/common/inf-request.h>
#include <libinfinity/common/inf-request-result.h>

G_DEFINE_INTERFACE(InfRequest, inf_request, G_TYPE_OBJECT)

enum {
  FINISHED,

  LAST_SIGNAL
};

static guint request_signals[LAST_SIGNAL];

static void
inf_request_default_init(InfRequestInterface* iface)
{
  g_object_interface_install_property(
    iface,
    g_param_spec_string(
      "type",
      "Type",
      "A string identifier for the type of the request",
      NULL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_interface_install_property(
    iface,
    g_param_spec_double(
      "progress",
      "Progress",
      "Percentage of completion of the request",
      0.0,
      1.0,
      0.0,
      G_PARAM_READABLE
    )
  );

  /**
   * InfRequest::finished:
   * @request: The #InfRequest which finished.
   * @result: A #InfRequestResult which contains the result of the request.
   * @error: Error information in case the request failed, or %NULL
   * otherwise.
   *
   * This signal is emitted when the request finishes. If @error is
   * non-%NULL the request failed, otherwise it finished successfully.
   */
  request_signals[FINISHED] = g_signal_new(
    "finished",
    INF_TYPE_REQUEST,
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfRequestInterface, finished),
    NULL, NULL,
    NULL,
    G_TYPE_NONE,
    2,
    INF_TYPE_REQUEST_RESULT | G_SIGNAL_TYPE_STATIC_SCOPE,
    G_TYPE_POINTER /* GError* */
  );
}

/*
 * Public API
 */

/**
 * inf_request_fail:
 * @request: A #InfRequest.
 * @error: A #GError describing the reason for why the request failed.
 *
 * Declares the request as failed by emitting the #InfRequest::finished
 * signal with the given error.
 */
void
inf_request_fail(InfRequest* request,
                 const GError* error)
{
  g_return_if_fail(INF_IS_REQUEST(request));
  g_return_if_fail(error != NULL);

  g_signal_emit(
    request,
    request_signals[FINISHED],
    0,
    NULL,
    error
  );
}

/**
 * inf_request_finish:
 * @request: A #InfRequest.
 * @result: (transfer full): A #InfRequestResult containing the result of
 * the request.
 *
 * Declares the request as succeeded by emitting the #InfRequest::finished
 * signal with the given result. The function takes ownership of @result.
 */
void
inf_request_finish(InfRequest* request,
                   InfRequestResult* result)
{
  g_return_if_fail(INF_IS_REQUEST(request));
  g_return_if_fail(result != NULL);

  g_signal_emit(
    request,
    request_signals[FINISHED],
    0,
    result,
    NULL
  );

  inf_request_result_free(result);
}

/**
 * inf_request_is_local:
 * @request: A #InfRequest.
 *
 * Returns whether @request is local or remote. A local request was triggered
 * by a local API call, whereas a remote request was caused by a remote
 * participant from the network.
 *
 * Returns: %TRUE if the request is local and %FALSE if it is remote.
 */
gboolean
inf_request_is_local(InfRequest* request)
{
  InfRequestInterface* iface;

  g_return_val_if_fail(INF_IS_REQUEST(request), FALSE);

  iface = INF_REQUEST_GET_IFACE(request);
  g_return_val_if_fail(iface->is_local != NULL, FALSE);

  return iface->is_local(request);
}

/* vim:set et sw=2 ts=2: */
