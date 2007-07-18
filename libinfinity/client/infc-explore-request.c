/* infcinote - Collaborative notetaking application
 * Copyright (C) 2007 Armin Burgmeier
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
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <libinfinity/client/infc-explore-request.h>
#include <libinfinity/common/inf-error.h>
#include <libinfinity/inf-marshal.h>

typedef struct _InfcExploreRequestPrivate InfcExploreRequestPrivate;
struct _InfcExploreRequestPrivate {
  guint node_id;
  guint current;
  guint total;
};

enum {
  PROP_0,

  PROP_NODE_ID,
  PROP_CURRENT,
  PROP_TOTAL
};

enum {
  INITIATED,
  PROGRESS,
  FINISHED,

  LAST_SIGNAL
};

#define INFC_EXPLORE_REQUEST_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFC_TYPE_EXPLORE_REQUEST, InfcExploreRequestPrivate))

static InfcRequestClass* parent_class;
static guint explore_request_signals[LAST_SIGNAL];

static void
infc_explore_request_init(GTypeInstance* instance,
                          gpointer g_class)
{
  InfcExploreRequest* request;
  InfcExploreRequestPrivate* priv;

  request = INFC_EXPLORE_REQUEST(instance);
  priv = INFC_EXPLORE_REQUEST_PRIVATE(request);

  priv->node_id = 0;
  priv->current = 0;
  priv->total = 0;
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
  case PROP_NODE_ID:
    priv->node_id = g_value_get_uint(value);
    break;
  case PROP_TOTAL:
    priv->total = g_value_get_uint(value);
    break;
  case PROP_CURRENT:
    priv->total = g_value_get_uint(value);
    break;
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

  explore_request = INFC_EXPLORE_REQUEST(object);
  priv = INFC_EXPLORE_REQUEST_PRIVATE(explore_request);

  switch(prop_id)
  {
  case PROP_NODE_ID:
    g_value_set_uint(value, priv->node_id);
    break;
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

  parent_class = INFC_REQUEST_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfcExploreRequestPrivate));

  object_class->finalize = infc_explore_request_finalize;
  object_class->set_property = infc_explore_request_set_property;
  object_class->get_property = infc_explore_request_get_property;

  request_class->initiated = NULL;
  request_class->progress = NULL;
  request_class->finished = NULL;

  g_object_class_install_property(
    object_class,
    PROP_NODE_ID,
    g_param_spec_uint(
      "node_id",
      "Node ID",
      "ID of the node to explore",
      0,
      G_MAXUINT,
      0,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

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
      G_PARAM_READWRITE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_CURRENT,
    g_param_spec_uint(
      "current",
      "Current",
      "Node that has just been explored",
      0,
      G_MAXUINT,
      0,
      G_PARAM_READWRITE
    )
  );

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

  explore_request_signals[PROGRESS] = g_signal_new(
    "progress",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfcExploreRequestClass, progress),
    NULL, NULL,
    inf_marshal_VOID__UINT_UINT,
    G_TYPE_NONE,
    2,
    G_TYPE_UINT,
    G_TYPE_UINT
  );

  explore_request_signals[FINISHED] = g_signal_new(
    "finished",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfcExploreRequestClass, finished),
    NULL, NULL,
    inf_marshal_VOID__VOID,
    G_TYPE_NONE,
    0
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
      INFC_TYPE_REQUEST,
      "InfcExploreRequest",
      &explore_request_type_info,
      0
    );
  }

  return explore_request_type;
}

/** infc_explore_request_get_node_id:
 *
 * @request: An #InfcExploreRequest.
 *
 * Returns the ID of the node to be explored.
 *
 * Return Value: ID of the node to be explored.
 **/
guint
infc_explore_request_get_node_id(InfcExploreRequest* request)
{
  return INFC_EXPLORE_REQUEST_PRIVATE(request)->node_id;
}

/** infc_explore_request_initiated:
 *
 * @request: An #InfcExploreRequest.
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

/** infc_explore_request_progress:
 *
 * @request: A #InfcExploreRequest.
 * @error: Location to store error information.
 *
 * Emits the "progress" signal on @request.
 *
 * Return Value: %TRUE when the signal was emitted, %FALSE on error.
 **/
gboolean
infc_explore_request_progress(InfcExploreRequest* request,
                              GError** error)
{
  InfcExploreRequestPrivate* priv;
  priv = INFC_EXPLORE_REQUEST_PRIVATE(request);

  if(priv->current == priv->total)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_TOO_MUCH_CHILDREN,
      "%s",
      inf_directory_strerror(INF_DIRECTORY_ERROR_TOO_MUCH_CHILDREN)
    );

    return FALSE;
  }
  else
  {
    g_signal_emit(
      G_OBJECT(request),
      explore_request_signals[PROGRESS],
      0,
      ++ priv->current,
      priv->total
    );

    return TRUE;
  }
}

/** infc_explore_request_finished:
 *
 * @request: A #InfcExploreRequest.
 * @error: Location to store error information.
 *
 * Emits the "finished" signal on @request.
 *
 * Return Value: %TRUE when the signal was emitted, %FALSE on error.
 **/
gboolean
infc_explore_request_finished(InfcExploreRequest* request,
                              GError** error)
{
  InfcExploreRequestPrivate* priv;
  priv = INFC_EXPLORE_REQUEST_PRIVATE(request);

  if(priv->current < priv->total)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_TOO_FEW_CHILDREN,
      "%s",
      inf_directory_strerror(INF_DIRECTORY_ERROR_TOO_FEW_CHILDREN)
    );

    return FALSE;
  }
  else
  {
    g_signal_emit(
      G_OBJECT(request),
      explore_request_signals[FINISHED],
      0
    );

    return TRUE;
  }
}
