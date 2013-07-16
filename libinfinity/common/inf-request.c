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

/**
 * SECTION:inf-request
 * @title: InfRequest
 * @short_description: Asynchronous request
 * @include: libinfinity/common/inf-request.h
 * @see_also: #InfBrowserRequest
 * @stability: Unstable
 *
 * #InfRequest represents an asynchronous operation. This is a basic interface
 * which allows to query the type of the operation. More specific operations
 * are possible on more specialized interfaces or classes, such as
 * #InfBrowserRequest or #InfcUserRequest.
 */

#include <libinfinity/common/inf-request.h>

static void
inf_request_base_init(gpointer g_class)
{
  static gboolean initialized = FALSE;

  if(!initialized)
  {
    g_object_interface_install_property(
      g_class,
      g_param_spec_string(
        "type",
        "Type",
        "A string identifier for the type of the request",
        NULL,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
      )
    );

    initialized = TRUE;
  }
}

GType
inf_request_get_type(void)
{
  static GType request_type = 0;

  if(!request_type)
  {
    static const GTypeInfo request_info = {
      sizeof(InfRequestIface),    /* class_size */
      inf_request_base_init,      /* base_init */
      NULL,                       /* base_finalize */
      NULL,                       /* class_init */
      NULL,                       /* class_finalize */
      NULL,                       /* class_data */
      0,                          /* instance_size */
      0,                          /* n_preallocs */
      NULL,                       /* instance_init */
      NULL                        /* value_table */
    };

    request_type = g_type_register_static(
      G_TYPE_INTERFACE,
      "InfRequest",
      &request_info,
      0
    );

    g_type_interface_add_prerequisite(request_type, G_TYPE_OBJECT);
  }

  return request_type;
}

/*
 * Public API
 */

/**
 * inf_request_fail:
 * @request: A #InfRequest.
 * @error: A #GError describing the reason for why the request failed.
 *
 * Declares the request as failed. What this actually does is defined by the
 * actual class implementing this interface. Usually a signal is emitted which
 * lets higher level objects know that an operation has failed.
 */
void
inf_request_fail(InfRequest* request,
                 const GError* error)
{
  InfRequestIface* iface;

  g_return_if_fail(INF_IS_REQUEST(request));
  g_return_if_fail(error != NULL);

  iface = INF_REQUEST_GET_IFACE(request);
  g_return_if_fail(iface->fail != NULL);

  return iface->fail(request, error);
}

/* vim:set et sw=2 ts=2: */
