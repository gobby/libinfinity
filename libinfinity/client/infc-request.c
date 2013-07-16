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
 * SECTION:infc-request
 * @title: InfcRequest
 * @short_description: Asynchronous request on the client side
 * @include: libinfinity/client/infc-request.h
 * @see_also: #InfRequest, #InfcNodeRequest, #InfcUserRequest
 * @stability: Unstable
 *
 * #InfcRequest is an interface which represents an asynchronous operation on
 * the client side. This usually means that a request has sent to the server
 * and the client is waiting for a reply. For this purpose, classes
 * implementing this interface need to rememeber a so-called sequence number
 * ("seq" number) that uniquely identifies the particular request in the
 * server reply.
 *
 * This interface is implemented by specific requests such as #InfcNodeRequest
 * or #InfcUserRequest.
 */

#include <libinfinity/client/infc-request.h>
#include <libinfinity/common/inf-request.h>

static void
infc_request_base_init(gpointer g_class)
{
  static gboolean initialized = FALSE;

  if(!initialized)
  {
    g_object_interface_install_property(
      g_class,
      g_param_spec_uint(
        "seq",
        "Seq",
        "The sequence number of the request",
        0,
        G_MAXUINT,
        0,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
      )
    );

    initialized = TRUE;
  }
}

GType
infc_request_get_type(void)
{
  static GType request_type = 0;

  if(!request_type)
  {
    static const GTypeInfo request_info = {
      sizeof(InfcRequestIface),   /* class_size */
      infc_request_base_init,     /* base_init */
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
      "InfcRequest",
      &request_info,
      0
    );

    g_type_interface_add_prerequisite(request_type, INF_TYPE_REQUEST);
  }

  return request_type;
}

/* vim:set et sw=2 ts=2: */
