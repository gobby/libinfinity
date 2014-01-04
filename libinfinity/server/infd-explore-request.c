/* infdinote - Collaborative notetaking application
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
 * SECTION:infd-explore-request
 * @title: InfdExploreRequest
 * @short_description: Watch progress of node exploration
 * @include: libinfinity/server/infd-explore-request.h
 * @see_also: #InfExploreRequest, #InfdNodeRequest, #InfdDirectory
 * @stability: Unstable
 *
 * When starting a node exploration using inf_browser_explore() on a
 * #InfdDirectory then it returns an #InfdExploreRequest. This object can be
 * used to get notified when there is progress in exploration (for example to
 * show a progress bar in the GUI) or when the operation finished, that is
 * all child nodes of the explored subdirectory are known to the directory.
 */

#include <libinfinity/server/infd-explore-request.h>
#include <libinfinity/server/infd-node-request.h>
#include <libinfinity/common/inf-explore-request.h>

typedef struct _InfdExploreRequestPrivate InfdExploreRequestPrivate;
struct _InfdExploreRequestPrivate {
  guint current;
  guint total;

  gboolean initiated;
};

enum {
  PROP_0,

  PROP_CURRENT,
  PROP_TOTAL
};

#define INFD_EXPLORE_REQUEST_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFD_TYPE_EXPLORE_REQUEST, InfdExploreRequestPrivate))

static InfdNodeRequestClass* parent_class;

static void
infd_explore_request_init(GTypeInstance* instance,
                          gpointer g_class)
{
  InfdExploreRequest* request;
  InfdExploreRequestPrivate* priv;

  request = INFD_EXPLORE_REQUEST(instance);
  priv = INFD_EXPLORE_REQUEST_PRIVATE(request);

  priv->current = 0;
  priv->total = 0;

  priv->initiated = FALSE;
}

static void
infd_explore_request_finalize(GObject* object)
{
  InfdExploreRequest* request;
  InfdExploreRequestPrivate* priv;

  request = INFD_EXPLORE_REQUEST(object);
  priv = INFD_EXPLORE_REQUEST_PRIVATE(request);

  if(G_OBJECT_CLASS(parent_class)->finalize != NULL)
    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
infd_explore_request_set_property(GObject* object,
                                  guint prop_id,
                                  const GValue* value,
                                  GParamSpec* pspec)
{
  InfdExploreRequest* request;
  InfdExploreRequestPrivate* priv;

  request = INFD_EXPLORE_REQUEST(object);
  priv = INFD_EXPLORE_REQUEST_PRIVATE(request);

  switch(prop_id)
  {
  case PROP_TOTAL:
  case PROP_CURRENT:
    /* readonly */
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infd_explore_request_get_property(GObject* object,
                                  guint prop_id,
                                  GValue* value,
                                  GParamSpec* pspec)
{
  InfdExploreRequest* explore_request;
  InfdExploreRequestPrivate* priv;
  gboolean finished;

  explore_request = INFD_EXPLORE_REQUEST(object);
  priv = INFD_EXPLORE_REQUEST_PRIVATE(explore_request);

  switch(prop_id)
  {
  case PROP_TOTAL:
    g_value_set_uint(value, priv->total);
    break;
  case PROP_CURRENT:
    g_value_set_uint(value, priv->current);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infd_explore_request_class_init(gpointer g_class,
                                gpointer class_data)
{
  GObjectClass* object_class;
  InfdExploreRequestClass* request_class;

  object_class = G_OBJECT_CLASS(g_class);
  request_class = INFD_EXPLORE_REQUEST_CLASS(g_class);

  parent_class = INFD_NODE_REQUEST_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfdExploreRequestPrivate));

  object_class->finalize = infd_explore_request_finalize;
  object_class->set_property = infd_explore_request_set_property;
  object_class->get_property = infd_explore_request_get_property;

  g_object_class_override_property(object_class, PROP_CURRENT, "current");
  g_object_class_override_property(object_class, PROP_TOTAL, "total");
}

static void
infd_explore_request_explore_request_init(gpointer g_iface,
                                          gpointer iface_data)
{
  InfExploreRequestIface* iface;
  iface = (InfExploreRequestIface*)g_iface;
}

GType
infd_explore_request_get_type(void)
{
  static GType explore_request_type = 0;

  if(!explore_request_type)
  {
    static const GTypeInfo explore_request_type_info = {
      sizeof(InfdExploreRequestClass),  /* class_size */
      NULL,                             /* base_init */
      NULL,                             /* base_finalize */
      infd_explore_request_class_init,  /* class_init */
      NULL,                             /* class_finalize */
      NULL,                             /* class_data */
      sizeof(InfdExploreRequest),       /* instance_size */
      0,                                /* n_preallocs */
      infd_explore_request_init,        /* instance_init */
      NULL                              /* value_table */
    };

    static const GInterfaceInfo explore_request_info = {
      infd_explore_request_explore_request_init,
      NULL,
      NULL
    };

    explore_request_type = g_type_register_static(
      INFD_TYPE_NODE_REQUEST,
      "InfdExploreRequest",
      &explore_request_type_info,
      0
    );

    g_type_add_interface_static(
      explore_request_type,
      INF_TYPE_EXPLORE_REQUEST,
      &explore_request_info
    );
  }

  return explore_request_type;
}

/**
 * infd_explore_request_initiated:
 * @request: An #InfdExploreRequest.
 * @total: The total number of children of the node which is being explored.
 *
 * Initiates the request. An explore request is considered initiated as soon
 * as the total amount of child nodes is known.
 **/
void
infd_explore_request_initiated(InfdExploreRequest* request,
                               guint total)
{
  InfdExploreRequestPrivate* priv;

  g_return_if_fail(INFD_IS_EXPLORE_REQUEST(request));

  priv = INFD_EXPLORE_REQUEST_PRIVATE(request);

  g_return_if_fail(priv->initiated == FALSE);

  priv->total = total;
  priv->initiated = TRUE;

  g_object_notify(G_OBJECT(request), "total");
}

/**
 * infd_explore_request_progress:
 * @request: A #InfdExploreRequest.
 * @error: Location to store error information.
 *
 * Indicates that one more node has been explored and changes the
 * #InfdExploreRequest:current property accordingly.
 **/
void
infd_explore_request_progress(InfdExploreRequest* request)
{
  InfdExploreRequestPrivate* priv;

  g_return_if_fail(INFD_IS_EXPLORE_REQUEST(request));

  priv = INFD_EXPLORE_REQUEST_PRIVATE(request);
  
  g_return_if_fail(priv->current < priv->total);

  ++priv->current;
  g_object_notify(G_OBJECT(request), "current");
}

/* vim:set et sw=2 ts=2: */
