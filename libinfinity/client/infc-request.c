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
 * SECTION:infc-request
 * @title: InfcRequest
 * @short_description: Asynchronous request
 * @include: libinfinity/client/infc-request.h
 * @see_also: #InfcBrowser
 * @stability: Unstable
 *
 * #InfcRequest represents an asynchronous operation on the client side,
 * waiting for a reply from the server.
 */

#include <libinfinity/client/infc-request.h>
#include <libinfinity/common/inf-request.h>

typedef struct _InfcRequestPrivate InfcRequestPrivate;
struct _InfcRequestPrivate {
  gchar* type;
  guint seq;
  guint node_id;
  gboolean finished;
};

enum {
  PROP_0,

  PROP_TYPE,
  PROP_SEQ,
  PROP_PROGRESS,
  PROP_NODE_ID
};

#define INFC_REQUEST_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFC_TYPE_REQUEST, InfcRequestPrivate))

static GObjectClass* parent_class;

static void
infc_request_init(GTypeInstance* instance,
                  gpointer g_class)
{
  InfcRequest* request;
  InfcRequestPrivate* priv;

  request = INFC_REQUEST(instance);
  priv = INFC_REQUEST_PRIVATE(request);

  priv->type = NULL;
  priv->seq = G_MAXUINT;
  priv->node_id = G_MAXUINT;
  priv->finished = FALSE;
}

static void
infc_request_finalize(GObject* object)
{
  InfcRequest* request;
  InfcRequestPrivate* priv;

  request = INFC_REQUEST(object);
  priv = INFC_REQUEST_PRIVATE(request);

  g_free(priv->type);

  if(G_OBJECT_CLASS(parent_class)->finalize != NULL)
    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
infc_request_set_property(GObject* object,
                          guint prop_id,
                          const GValue* value,
                          GParamSpec* pspec)
{
  InfcRequest* request;
  InfcRequestPrivate* priv;

  request = INFC_REQUEST(object);
  priv = INFC_REQUEST_PRIVATE(request);

  switch(prop_id)
  {
  case PROP_TYPE:
    g_assert(priv->type == NULL); /* construct only */
    priv->type = g_value_dup_string(value);
    break;
  case PROP_SEQ:
    g_assert(priv->seq == G_MAXUINT); /* construct only */
    priv->seq = g_value_get_uint(value);
    break;
  case PROP_NODE_ID:
    g_assert(priv->node_id == G_MAXUINT); /* construct only */
    priv->node_id = g_value_get_uint(value);
    break;
  case PROP_PROGRESS:
    /* read only */
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infc_request_get_property(GObject* object,
                          guint prop_id,
                          GValue* value,
                          GParamSpec* pspec)
{
  InfcRequest* request;
  InfcRequestPrivate* priv;

  request = INFC_REQUEST(object);
  priv = INFC_REQUEST_PRIVATE(request);

  switch(prop_id)
  {
  case PROP_TYPE:
    g_value_set_string(value, priv->type);
    break;
  case PROP_SEQ:
    g_value_set_uint(value, priv->seq);
    break;
  case PROP_NODE_ID:
    g_value_set_uint(value, priv->node_id);
    break;
  case PROP_PROGRESS:
    if(priv->finished)
      g_value_set_double(value, 1.0);
    else
      g_value_set_double(value, 0.0);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static gboolean
infc_request_is_local(InfRequest* request)
{
  InfcRequestPrivate* priv;
  priv = INFC_REQUEST_PRIVATE(request);

  if(priv->seq == G_MAXUINT)
    return FALSE;

  return TRUE;
}

static void
infc_request_finished(InfRequest* request,
                      const InfRequestResult* result,
                      const GError* error)
{
  InfcRequestPrivate* priv;
  priv = INFC_REQUEST_PRIVATE(request);

  priv->finished = TRUE;
  g_object_notify(G_OBJECT(request), "progress");
}

static void
infc_request_class_init(gpointer g_class,
                        gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfcRequestPrivate));

  object_class->finalize = infc_request_finalize;
  object_class->set_property = infc_request_set_property;
  object_class->get_property = infc_request_get_property;

  g_object_class_install_property(
    object_class,
    PROP_SEQ,
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

  g_object_class_install_property(
    object_class,
    PROP_NODE_ID,
    g_param_spec_uint(
      "node-id",
      "Node ID",
      "The ID of the node affected by this request",
      0,
      G_MAXUINT,
      G_MAXUINT,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_override_property(object_class, PROP_TYPE, "type");
  g_object_class_override_property(object_class, PROP_PROGRESS, "progress");
}

static void
infc_request_request_init(gpointer g_iface,
                          gpointer iface_data)
{
  InfRequestIface* iface;
  iface = (InfRequestIface*)g_iface;

  iface->finished = infc_request_finished;
  iface->is_local = infc_request_is_local;
}

GType
infc_request_get_type(void)
{
  static GType request_type = 0;

  if(!request_type)
  {
    static const GTypeInfo request_type_info = {
      sizeof(InfcRequestClass),   /* class_size */
      NULL,                       /* base_init */
      NULL,                       /* base_finalize */
      infc_request_class_init,    /* class_init */
      NULL,                       /* class_finalize */
      NULL,                       /* class_data */
      sizeof(InfcRequest),        /* instance_size */
      0,                          /* n_preallocs */
      infc_request_init,          /* instance_init */
      NULL                        /* value_table */
    };

    static const GInterfaceInfo request_info = {
      infc_request_request_init,
      NULL,
      NULL
    };

    request_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfcRequest",
      &request_type_info,
      0
    );

    g_type_add_interface_static(
      request_type,
      INF_TYPE_REQUEST,
      &request_info
    );
  }

  return request_type;
}

/* vim:set et sw=2 ts=2: */
