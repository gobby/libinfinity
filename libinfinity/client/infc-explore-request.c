/* infcinote - Collaborative notetaking application
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
 * SECTION:infc-explore-request
 * @title: InfcExploreRequest
 * @short_description: Watch progress of node exploration
 * @include: libinfinity/client/infc-explore-request.h
 * @see_also: #InfExploreRequest, #InfcRequest, #InfcBrowser
 * @stability: Unstable
 *
 * When starting a node exploration using inf_browser_explore() on a
 * #InfcBrowser then it returns an #InfcExploreRequest. This object can be
 * used to get notified when there is progress in exploration (for example to
 * show a progress bar in the GUI) or when the operation finished, that is
 * all child nodes of the explored subdirectory are known to the browser.
 */

#include <libinfinity/client/infc-explore-request.h>
#include <libinfinity/client/infc-node-request.h>
#include <libinfinity/common/inf-explore-request.h>
#include <libinfinity/inf-marshal.h>

typedef struct _InfcExploreRequestPrivate InfcExploreRequestPrivate;
struct _InfcExploreRequestPrivate {
  guint current;
  guint total;

  gboolean initiated;
};

enum {
  PROP_0,

  PROP_CURRENT,
  PROP_TOTAL
};

#define INFC_EXPLORE_REQUEST_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFC_TYPE_EXPLORE_REQUEST, InfcExploreRequestPrivate))

static InfcNodeRequestClass* parent_class;

static void
infc_explore_request_init(GTypeInstance* instance,
                          gpointer g_class)
{
  InfcExploreRequest* request;
  InfcExploreRequestPrivate* priv;

  request = INFC_EXPLORE_REQUEST(instance);
  priv = INFC_EXPLORE_REQUEST_PRIVATE(request);

  priv->current = 0;
  priv->total = 0;

  priv->initiated = FALSE;
}

static void
infc_explore_request_finalize(GObject* object)
{
  InfcExploreRequest* request;
  InfcExploreRequestPrivate* priv;

  request = INFC_EXPLORE_REQUEST(object);
  priv = INFC_EXPLORE_REQUEST_PRIVATE(request);

  if(G_OBJECT_CLASS(parent_class)->finalize != NULL)
    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
infc_explore_request_set_property(GObject* object,
                                  guint prop_id,
                                  const GValue* value,
                                  GParamSpec* pspec)
{
  InfcExploreRequest* request;
  InfcExploreRequestPrivate* priv;

  request = INFC_EXPLORE_REQUEST(object);
  priv = INFC_EXPLORE_REQUEST_PRIVATE(request);

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
infc_explore_request_get_property(GObject* object,
                                  guint prop_id,
                                  GValue* value,
                                  GParamSpec* pspec)
{
  InfcExploreRequest* explore_request;
  InfcExploreRequestPrivate* priv;
  gboolean finished;

  explore_request = INFC_EXPLORE_REQUEST(object);
  priv = INFC_EXPLORE_REQUEST_PRIVATE(explore_request);

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
infc_explore_request_class_init(gpointer g_class,
                                gpointer class_data)
{
  GObjectClass* object_class;
  InfcExploreRequestClass* request_class;

  object_class = G_OBJECT_CLASS(g_class);
  request_class = INFC_EXPLORE_REQUEST_CLASS(g_class);

  parent_class = INFC_NODE_REQUEST_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfcExploreRequestPrivate));

  object_class->finalize = infc_explore_request_finalize;
  object_class->set_property = infc_explore_request_set_property;
  object_class->get_property = infc_explore_request_get_property;

  g_object_class_override_property(object_class, PROP_CURRENT, "current");
  g_object_class_override_property(object_class, PROP_TOTAL, "total");
}

static void
infc_explore_request_explore_request_init(gpointer g_iface,
                                          gpointer iface_data)
{
  InfExploreRequestIface* iface;
  iface = (InfExploreRequestIface*)g_iface;
}

GType
infc_explore_request_get_type(void)
{
  static GType explore_request_type = 0;

  if(!explore_request_type)
  {
    static const GTypeInfo explore_request_type_info = {
      sizeof(InfcExploreRequestClass),  /* class_size */
      NULL,                             /* base_init */
      NULL,                             /* base_finalize */
      infc_explore_request_class_init,  /* class_init */
      NULL,                             /* class_finalize */
      NULL,                             /* class_data */
      sizeof(InfcExploreRequest),       /* instance_size */
      0,                                /* n_preallocs */
      infc_explore_request_init,        /* instance_init */
      NULL                              /* value_table */
    };

    static const GInterfaceInfo explore_request_info = {
      infc_explore_request_explore_request_init,
      NULL,
      NULL
    };

    explore_request_type = g_type_register_static(
      INFC_TYPE_NODE_REQUEST,
      "InfcExploreRequest",
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
 * infc_explore_request_initiated:
 * @request: An #InfcExploreRequest.
 * @total: The total number of children of the node which is being explored.
 *
 * Initiates the request. An explore request is considered initiated as soon
 * as the total amount of child nodes is known.
 **/
void
infc_explore_request_initiated(InfcExploreRequest* request,
                               guint total)
{
  InfcExploreRequestPrivate* priv;
  g_return_if_fail(INFC_IS_EXPLORE_REQUEST(request));

  priv = INFC_EXPLORE_REQUEST_PRIVATE(request);
  g_return_if_fail(priv->initiated == FALSE);

  priv->total = total;
  priv->initiated = TRUE;

  g_object_notify(G_OBJECT(request), "total");
}

/**
 * infc_explore_request_get_initiated:
 * @request: A #InfcExploreRequest.
 *
 * Returns whether the exploration process was already initiated, i.e. the
 * total number of nodes to explore is known.
 *
 * Returns: Whether the exploration was initiated.
 **/
gboolean
infc_explore_request_get_initiated(InfcExploreRequest* request)
{
  g_return_val_if_fail(INFC_IS_EXPLORE_REQUEST(request), FALSE);
  return INFC_EXPLORE_REQUEST_PRIVATE(request)->initiated;
}

/**
 * infc_explore_request_progress:
 * @request: A #InfcExploreRequest.
 * @error: Location to store error information.
 *
 * Indicates that one more node has been explored and changes the
 * #InfcExploreRequest:current property accordingly. The request must be
 * initiated before this function can be called.
 **/
void
infc_explore_request_progress(InfcExploreRequest* request)
{
  InfcExploreRequestPrivate* priv;
  g_return_if_fail(INFC_IS_EXPLORE_REQUEST(request));

  priv = INFC_EXPLORE_REQUEST_PRIVATE(request);
  g_return_if_fail(priv->initiated == TRUE);  
  g_return_if_fail(priv->current < priv->total);

  ++priv->current;
  g_object_notify(G_OBJECT(request), "current");
}

/* vim:set et sw=2 ts=2: */
