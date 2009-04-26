/* infcinote - Collaborative notetaking application
 * Copyright (C) 2007, 2008, 2009 Armin Burgmeier <armin@arbur.net>
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

#include <libinfinity/client/infc-browser.h>
#include <libinfinity/client/infc-node-request.h>
#include <libinfinity/inf-marshal.h>

typedef struct _InfcNodeRequestPrivate InfcNodeRequestPrivate;
struct _InfcNodeRequestPrivate {
  guint node_id;
};

enum {
  PROP_0,

  PROP_NODE_ID
};

enum {
  FINISHED,

  LAST_SIGNAL
};

#define INFC_NODE_REQUEST_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFC_TYPE_NODE_REQUEST, InfcNodeRequestPrivate))

static InfcRequestClass* parent_class;
static guint node_request_signals[LAST_SIGNAL];

static void
infc_node_request_init(GTypeInstance* instance,
                       gpointer g_class)
{
  InfcNodeRequest* node_request;
  InfcNodeRequestPrivate* priv;

  node_request = INFC_NODE_REQUEST(instance);
  priv = INFC_NODE_REQUEST_PRIVATE(node_request);

  priv->node_id = 0;
}

static void
infc_node_request_finalize(GObject* object)
{
  InfcNodeRequest* request;
  request = INFC_NODE_REQUEST(object);

  if(G_OBJECT_CLASS(parent_class)->finalize != NULL)
    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
infc_node_request_set_property(GObject* object,
                               guint prop_id,
                               const GValue* value,
                               GParamSpec* pspec)
{
  InfcNodeRequest* request;
  InfcNodeRequestPrivate* priv;

  request = INFC_NODE_REQUEST(object);
  priv = INFC_NODE_REQUEST_PRIVATE(request);

  switch(prop_id)
  {
  case PROP_NODE_ID:
    priv->node_id = g_value_get_uint(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infc_node_request_get_property(GObject* object,
                               guint prop_id,
                               GValue* value,
                               GParamSpec* pspec)
{
  InfcNodeRequest* request;
  InfcNodeRequestPrivate* priv;

  request = INFC_NODE_REQUEST(object);
  priv = INFC_NODE_REQUEST_PRIVATE(request);

  switch(prop_id)
  {
  case PROP_NODE_ID:
    g_value_set_uint(value, priv->node_id);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infc_node_request_class_init(gpointer g_class,
                             gpointer class_data)
{
  GObjectClass* object_class;
  InfcNodeRequestClass* request_class;

  object_class = G_OBJECT_CLASS(g_class);
  request_class = INFC_NODE_REQUEST_CLASS(g_class);

  parent_class = INFC_REQUEST_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfcNodeRequestPrivate));

  object_class->finalize = infc_node_request_finalize;
  object_class->set_property = infc_node_request_set_property;
  object_class->get_property = infc_node_request_get_property;

  request_class->finished = NULL;

  g_object_class_install_property(
    object_class,
    PROP_NODE_ID,
    g_param_spec_uint(
      "node-id",
      "Node ID",
      "The ID of the node affected by the request",
      0,
      G_MAXUINT,
      0,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  node_request_signals[FINISHED] = g_signal_new(
    "finished",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfcNodeRequestClass, finished),
    NULL, NULL,
    inf_marshal_VOID__BOXED,
    G_TYPE_NONE,
    1,
    INFC_TYPE_BROWSER_ITER | G_SIGNAL_TYPE_STATIC_SCOPE
  );
}

GType
infc_node_request_get_type(void)
{
  static GType node_request_type = 0;

  if(!node_request_type)
  {
    static const GTypeInfo node_request_type_info = {
      sizeof(InfcNodeRequestClass),  /* class_size */
      NULL,                          /* base_init */
      NULL,                          /* base_finalize */
      infc_node_request_class_init,  /* class_init */
      NULL,                          /* class_finalize */
      NULL,                          /* class_data */
      sizeof(InfcNodeRequest),       /* instance_size */
      0,                             /* n_preallocs */
      infc_node_request_init,        /* instance_init */
      NULL                           /* value_table */
    };

    node_request_type = g_type_register_static(
      INFC_TYPE_REQUEST,
      "InfcNodeRequest",
      &node_request_type_info,
      0
    );
  }

  return node_request_type;
}

/**
 * infc_node_request_finished:
 * @request: A #InfcNodeRequest.
 * @iter: A #InfcBrowserIter pointing to a node affected by the request.
 *
 * Emits the "finished" signal on @request.
 **/
void
infc_node_request_finished(InfcNodeRequest* request,
                           const InfcBrowserIter* iter)
{
  g_signal_emit(G_OBJECT(request), node_request_signals[FINISHED], 0, iter);
}

/* vim:set et sw=2 ts=2: */
