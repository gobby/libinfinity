/* infdinote - Collaborative notetaking application
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
 * SECTION:infd-node-request
 * @title: InfdNodeRequest
 * @short_description: Asynchronous request related to a node in a directory
 * @include: libinfinity/server/infd-node-request.h
 * @see_also: #InfdDirectory, #InfNodeRequest
 * @stability: Unstable
 *
 * #InfdNodeRequest represents an asynchronous operation which is related to
 * a node in a #InfdDirectory. This could be the request to add a node or
 * explore a subdirectory. This is a potentially asynchronous operation since
 * it involves I/O.
 */

#include <libinfinity/server/infd-node-request.h>
#include <libinfinity/common/inf-node-request.h>
#include <libinfinity/common/inf-request.h>
#include <libinfinity/common/inf-xml-connection.h>

typedef struct _InfdNodeRequestPrivate InfdNodeRequestPrivate;
struct _InfdNodeRequestPrivate {
  gchar* type;
  guint node_id;
  InfXmlConnection* requestor;
};

enum {
  PROP_0,

  PROP_TYPE,
  PROP_NODE_ID,
  PROP_REQUESTOR
};

#define INFD_NODE_REQUEST_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFD_TYPE_NODE_REQUEST, InfdNodeRequestPrivate))

static GObjectClass* parent_class;

static void
infd_node_request_init(GTypeInstance* instance,
                       gpointer g_class)
{
  InfdNodeRequest* request;
  InfdNodeRequestPrivate* priv;

  request = INFD_NODE_REQUEST(instance);
  priv = INFD_NODE_REQUEST_PRIVATE(request);

  priv->type = NULL;
  priv->node_id = 0;
  priv->requestor = NULL;
}

static void
infd_node_request_dispose(GObject* object)
{
  InfdNodeRequest* request;
  InfdNodeRequestPrivate* priv;

  request = INFD_NODE_REQUEST(object);
  priv = INFD_NODE_REQUEST_PRIVATE(request);

  if(priv->requestor != NULL)
  {
    g_object_unref(priv->requestor);
    priv->requestor = NULL;
  }

  if(G_OBJECT_CLASS(parent_class)->finalize != NULL)
    G_OBJECT_CLASS(parent_class)->finalize(object);

}

static void
infd_node_request_finalize(GObject* object)
{
  InfdNodeRequest* request;
  InfdNodeRequestPrivate* priv;

  request = INFD_NODE_REQUEST(object);
  priv = INFD_NODE_REQUEST_PRIVATE(request);

  g_free(priv->type);

  if(G_OBJECT_CLASS(parent_class)->finalize != NULL)
    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
infd_node_request_set_property(GObject* object,
                               guint prop_id,
                               const GValue* value,
                               GParamSpec* pspec)
{
  InfdNodeRequest* request;
  InfdNodeRequestPrivate* priv;

  request = INFD_NODE_REQUEST(object);
  priv = INFD_NODE_REQUEST_PRIVATE(request);

  switch(prop_id)
  {
  case PROP_TYPE:
    g_assert(priv->type == NULL); /* construct only */
    priv->type = g_value_dup_string(value);
    break;
  case PROP_NODE_ID:
    g_assert(priv->node_id == 0); /* construct only */
    priv->node_id = g_value_get_uint(value);
    break;
  case PROP_REQUESTOR:
    g_assert(priv->requestor == NULL); /* construct only */
    priv->requestor = g_value_dup_object(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infd_node_request_get_property(GObject* object,
                               guint prop_id,
                               GValue* value,
                               GParamSpec* pspec)
{
  InfdNodeRequest* request;
  InfdNodeRequestPrivate* priv;

  request = INFD_NODE_REQUEST(object);
  priv = INFD_NODE_REQUEST_PRIVATE(request);

  switch(prop_id)
  {
  case PROP_TYPE:
    g_value_set_string(value, priv->type);
    break;
  case PROP_NODE_ID:
    g_value_set_uint(value, priv->node_id);
    break;
  case PROP_REQUESTOR:
    g_value_set_object(value, priv->requestor);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infd_node_request_request_fail(InfRequest* request,
                               const GError* error)
{
  inf_node_request_finished(INF_NODE_REQUEST(request), NULL, error);
}

static void
infd_node_request_class_init(gpointer g_class,
                             gpointer class_data)
{
  GObjectClass* object_class;
  InfdNodeRequestClass* request_class;

  object_class = G_OBJECT_CLASS(g_class);
  request_class = INFD_NODE_REQUEST_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfdNodeRequestPrivate));

  object_class->dispose = infd_node_request_dispose;
  object_class->finalize = infd_node_request_finalize;
  object_class->set_property = infd_node_request_set_property;
  object_class->get_property = infd_node_request_get_property;

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

  g_object_class_install_property(
    object_class,
    PROP_REQUESTOR,
    g_param_spec_object(
      "requestor",
      "Requestor",
      "The connection making the request",
      INF_TYPE_XML_CONNECTION,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_override_property(object_class, PROP_TYPE, "type");
}

static void
infd_node_request_request_init(gpointer g_iface,
                               gpointer iface_data)
{
  InfRequestIface* iface;
  iface = (InfRequestIface*)g_iface;

  iface->fail = infd_node_request_request_fail;
}

static void
infd_node_request_node_request_init(gpointer g_iface,
                                    gpointer iface_data)
{
  InfNodeRequestIface* iface;
  iface = (InfNodeRequestIface*)g_iface;

  iface->finished = NULL;
}

GType
infd_node_request_get_type(void)
{
  static GType node_request_type = 0;

  if(!node_request_type)
  {
    static const GTypeInfo node_request_type_info = {
      sizeof(InfdNodeRequestClass),  /* class_size */
      NULL,                          /* base_init */
      NULL,                          /* base_finalize */
      infd_node_request_class_init,  /* class_init */
      NULL,                          /* class_finalize */
      NULL,                          /* class_data */
      sizeof(InfdNodeRequest),       /* instance_size */
      0,                             /* n_preallocs */
      infd_node_request_init,        /* instance_init */
      NULL                           /* value_table */
    };

    static const GInterfaceInfo request_info = {
      infd_node_request_request_init,
      NULL,
      NULL
    };

    static const GInterfaceInfo node_request_info = {
      infd_node_request_node_request_init,
      NULL,
      NULL
    };

    node_request_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfdNodeRequest",
      &node_request_type_info,
      0
    );

    g_type_add_interface_static(
      node_request_type,
      INF_TYPE_REQUEST,
      &request_info
    );

    g_type_add_interface_static(
      node_request_type,
      INF_TYPE_NODE_REQUEST,
      &node_request_info
    );
  }

  return node_request_type;
}

/* vim:set et sw=2 ts=2: */
