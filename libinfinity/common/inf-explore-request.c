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
 * SECTION:inf-explore-request
 * @title: InfExploreRequest
 * @short_description: Asynchronous exploration request
 * @include: libinfinity/common/inf-explore-request.h
 * @see_also: #InfBrowser, #InfRequest, #InfcExploreRequest
 * @stability: Unstable
 *
 * #InfExploreRequest represents a request that has been made via the
 * inf_browser_explore() function. Such a request is asynchronous, for example
 * because it waits for a response from an infinote server or because it
 * performs I/O. The #InfExploreRequest class is used to monitor progress and
 * being notified when the request finishes.
 *
 * When the exploration starts the #InfExploreRequest:total property will be
 * 0, since it is not yet known. After the request has been initiated the
 * total number of nodes is set and the #InfExploreRequest:current property
 * will count until the total number of nodes has been explored. Then the
 * request is finished by the #InfNodeRequest::finished signal being emitted.
 */

#include <libinfinity/common/inf-explore-request.h>
#include <libinfinity/common/inf-node-request.h>

static void
inf_explore_request_base_init(gpointer g_class)
{
  static gboolean initialized = FALSE;

  if(!initialized)
  {
    g_object_interface_install_property(
      g_class,
      g_param_spec_uint(
        "current",
        "Current",
        "The number of nodes that have so far been explored",
        0,
        G_MAXUINT,
        0,
        G_PARAM_READABLE
      )
    );

    g_object_interface_install_property(
      g_class,
      g_param_spec_uint(
        "total",
        "Total",
        "The total number of nodes to be explored",
        0,
        G_MAXUINT,
        0,
        G_PARAM_READABLE
      )
    );

    initialized = TRUE;
  }
}

GType
inf_explore_request_get_type(void)
{
  static GType explore_request_type = 0;

  if(!explore_request_type)
  {
    static const GTypeInfo explore_request_info = {
      sizeof(InfExploreRequestIface),     /* class_size */
      inf_explore_request_base_init,      /* base_init */
      NULL,                               /* base_finalize */
      NULL,                               /* class_init */
      NULL,                               /* class_finalize */
      NULL,                               /* class_data */
      0,                                  /* instance_size */
      0,                                  /* n_preallocs */
      NULL,                               /* instance_init */
      NULL                                /* value_table */
    };

    explore_request_type = g_type_register_static(
      G_TYPE_INTERFACE,
      "InfExploreRequest",
      &explore_request_info,
      0
    );

    g_type_interface_add_prerequisite(
      explore_request_type,
      INF_TYPE_NODE_REQUEST
    );
  }

  return explore_request_type;
}

/* vim:set et sw=2 ts=2: */
