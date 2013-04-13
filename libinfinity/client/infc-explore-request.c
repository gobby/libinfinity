/* infcinote - Collaborative notetaking application
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
 * SECTION:infc-explore-request
 * @title: InfcExploreRequest
 * @short_description: Watch progess of node exploration
 * @include: libinfinity/client/infc-explore-request.h
 * @stability: Unstable
 *
 * When starting a node exploration using inf_browser_explore() on a
 * #InfcBrowser then it returns an #InfcExploreRequest. This object can be
 * used to get notified when there is progress in exploration (for example to
 * show a progress bar in the GUI) or when the operation finished, that is
 * all child nodes of the explored subdirectory are known to the browser.
 *
 * When the exploration starts the #InfcExploreRequest::initiated signal is
 * emitted. Then, for each node being explored the
 * #InfcExploreRequest:progress property changes.
 * Eventually, #InfBrowserRequest::finished is emitted when the
 * exploration has finished. Before each step the request can also fail, in
 * which case #InfBrowserRequest::finished is emitted with non-zero error.
 * When this happens then none of the other signals will be emitted anymore.
 */

#include <libinfinity/client/infc-explore-request.h>
#include <libinfinity/client/infc-node-request.h>
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
  PROP_TOTAL,
  PROP_PROGRESS,

  PROP_INITIATED
};

enum {
  INITIATED,

  LAST_SIGNAL
};

#define INFC_EXPLORE_REQUEST_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFC_TYPE_EXPLORE_REQUEST, InfcExploreRequestPrivate))

static InfcNodeRequestClass* parent_class;
static guint explore_request_signals[LAST_SIGNAL];

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
  case PROP_PROGRESS:
  case PROP_INITIATED:
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
  case PROP_PROGRESS:
    if(priv->total == 0)
    {
      g_object_get(object, "finished", &finished, NULL);
      if(finished == TRUE)
        g_value_set_double(value, 1.0);
      else
        g_value_set_double(value, 0.0);
    }
    else
    {
      g_value_set_double(value, (double)priv->current / (double)priv->total);
    }
    break;
  case PROP_INITIATED:
    g_value_set_boolean(value, priv->initiated);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infc_explore_request_initiated_impl(InfcExploreRequest* request,
                                    guint total)
{
  InfcExploreRequestPrivate* priv;
  priv = INFC_EXPLORE_REQUEST_PRIVATE(request);

  g_assert(priv->initiated == FALSE);

  priv->total = total;
  priv->initiated = TRUE;

  g_object_notify(G_OBJECT(request), "total");
  g_object_notify(G_OBJECT(request), "initiated");
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

  request_class->initiated = infc_explore_request_initiated_impl;

  g_object_class_install_property(
    object_class,
    PROP_TOTAL,
    g_param_spec_uint(
      "total",
      "Total",
      "Total number of nodes that are explored",
      0,
      G_MAXUINT,
      0,
      G_PARAM_READABLE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_CURRENT,
    g_param_spec_uint(
      "current",
      "Current",
      "Number of nodes that have been explored",
      0,
      G_MAXUINT,
      0,
      G_PARAM_READABLE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_INITIATED,
    g_param_spec_boolean(
      "initiated",
      "Initiated",
      "Whether the exploration process was already initiated",
      FALSE,
      G_PARAM_READABLE
    )
  );

  g_object_class_override_property(object_class, PROP_PROGRESS, "progress");

  /**
   * InfcExploreRequest::initiated:
   * @request: The #InfcExploreRequest that is being initiated.
   * @total: Total number of the directory's child nodes.
   *
   * This signal is emitted once the exploration has been initiated, that is
   * when it is known how many child nodes are going to be explored. The
   * number of nodes is provided in the @total parameter.
   */
  explore_request_signals[INITIATED] = g_signal_new(
    "initiated",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfcExploreRequestClass, initiated),
    NULL, NULL,
    inf_marshal_VOID__UINT,
    G_TYPE_NONE,
    1,
    G_TYPE_UINT
  );
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

    explore_request_type = g_type_register_static(
      INFC_TYPE_NODE_REQUEST,
      "InfcExploreRequest",
      &explore_request_type_info,
      0
    );
  }

  return explore_request_type;
}

/**
 * infc_explore_request_initiated:
 * @request: An #InfcExploreRequest.
 * @total: The total number of children of the node which is being explored.
 *
 * Emits the "initiated" signal on @request. An explore request is considered
 * initiated as soon as the total amount of child nodes is known.
 **/
void
infc_explore_request_initiated(InfcExploreRequest* request,
                               guint total)
{
  g_signal_emit(
    G_OBJECT(request),
    explore_request_signals[INITIATED],
    0,
    total
  );
}

/**
 * infc_explore_request_get_initiated:
 * @request: A #InfcExploreRequest.
 *
 * Returns whether the exploration process was already initiated, i.e. the
 * total number of nodes to explore is known.
 *
 * Return Value: Whether the exploration was initiated.
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
 * #InfcExploreRequest:current property accordingly.
 **/
void
infc_explore_request_progress(InfcExploreRequest* request)
{
  InfcExploreRequestPrivate* priv;
  priv = INFC_EXPLORE_REQUEST_PRIVATE(request);
  
  g_return_if_fail(priv->current < priv->total);

  ++priv->current;
  g_object_notify(G_OBJECT(request), "current");
  g_object_notify(G_OBJECT(request), "progress");
}

/* vim:set et sw=2 ts=2: */
