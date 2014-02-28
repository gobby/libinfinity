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
 * SECTION:infd-request
 * @title: InfdRequest
 * @short_description: Asynchronous request on the server side
 * @include: libinfinity/server/infd-request.h
 * @see_also: #InfdDirectory, #InfRequest
 * @stability: Unstable
 *
 * #InfdRequest represents an asynchronous operation carried out on the
 * server side. It has the #InfdRequest:requestor property which specifies
 * which connection triggered the request, if any.
 */

#include <libinfinity/server/infd-request.h>
#include <libinfinity/common/inf-request.h>
#include <libinfinity/common/inf-xml-connection.h>

typedef struct _InfdRequestPrivate InfdRequestPrivate;
struct _InfdRequestPrivate {
  gchar* type;
  guint node_id;
  InfXmlConnection* requestor;
  gboolean finished;
};

enum {
  PROP_0,

  PROP_TYPE,
  PROP_NODE_ID,
  PROP_REQUESTOR,
  PROP_PROGRESS
};

#define INFD_REQUEST_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFD_TYPE_REQUEST, InfdRequestPrivate))

static GObjectClass* parent_class;

static void
infd_request_init(GTypeInstance* instance,
                  gpointer g_class)
{
  InfdRequest* request;
  InfdRequestPrivate* priv;

  request = INFD_REQUEST(instance);
  priv = INFD_REQUEST_PRIVATE(request);

  priv->type = NULL;
  priv->node_id = G_MAXUINT;
  priv->requestor = NULL;
  priv->finished = FALSE;
}

static void
infd_request_dispose(GObject* object)
{
  InfdRequest* request;
  InfdRequestPrivate* priv;

  request = INFD_REQUEST(object);
  priv = INFD_REQUEST_PRIVATE(request);

  if(priv->requestor != NULL)
  {
    g_object_unref(priv->requestor);
    priv->requestor = NULL;
  }

  if(G_OBJECT_CLASS(parent_class)->dispose != NULL)
    G_OBJECT_CLASS(parent_class)->dispose(object);

}

static void
infd_request_finalize(GObject* object)
{
  InfdRequest* request;
  InfdRequestPrivate* priv;

  request = INFD_REQUEST(object);
  priv = INFD_REQUEST_PRIVATE(request);

  g_free(priv->type);

  if(G_OBJECT_CLASS(parent_class)->finalize != NULL)
    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
infd_request_set_property(GObject* object,
                          guint prop_id,
                          const GValue* value,
                          GParamSpec* pspec)
{
  InfdRequest* request;
  InfdRequestPrivate* priv;

  request = INFD_REQUEST(object);
  priv = INFD_REQUEST_PRIVATE(request);

  switch(prop_id)
  {
  case PROP_TYPE:
    g_assert(priv->type == NULL); /* construct only */
    priv->type = g_value_dup_string(value);
    break;
  case PROP_NODE_ID:
    g_assert(priv->node_id == G_MAXUINT); /* construct only */
    priv->node_id = g_value_get_uint(value);
    break;
  case PROP_REQUESTOR:
    g_assert(priv->requestor == NULL); /* construct only */
    priv->requestor = g_value_dup_object(value);
    break;
  case PROP_PROGRESS:
    /* read only */
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infd_request_get_property(GObject* object,
                          guint prop_id,
                          GValue* value,
                          GParamSpec* pspec)
{
  InfdRequest* request;
  InfdRequestPrivate* priv;

  request = INFD_REQUEST(object);
  priv = INFD_REQUEST_PRIVATE(request);

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
  case PROP_PROGRESS:
    if(priv->finished == TRUE)
      g_value_set_double(value, 1.0);
    else
      g_value_set_double(value, 0.0);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infd_request_request_finished(InfRequest* request,
                              const InfRequestResult* result,
                              const GError* error)
{
  InfdRequestPrivate* priv;
  priv = INFD_REQUEST_PRIVATE(request);

  priv->finished = TRUE;
  g_object_notify(G_OBJECT(request), "progress");
}

static gboolean
infd_request_request_is_local(InfRequest* request)
{

  InfdRequestPrivate* priv;
  priv = INFD_REQUEST_PRIVATE(request);

  if(priv->requestor != NULL)
    return FALSE;

  return TRUE;
}

static void
infd_request_class_init(gpointer g_class,
                        gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfdRequestPrivate));

  object_class->dispose = infd_request_dispose;
  object_class->finalize = infd_request_finalize;
  object_class->set_property = infd_request_set_property;
  object_class->get_property = infd_request_get_property;

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

  g_object_class_install_property(
    object_class,
    PROP_NODE_ID,
    g_param_spec_uint(
      "node-id",
      "Node ID",
      "The ID of the node affected by the request",
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
infd_request_request_init(gpointer g_iface,
                          gpointer iface_data)
{
  InfRequestIface* iface;
  iface = (InfRequestIface*)g_iface;

  iface->finished = infd_request_request_finished;
  iface->is_local = infd_request_request_is_local;
}

GType
infd_request_get_type(void)
{
  static GType request_type = 0;

  if(!request_type)
  {
    static const GTypeInfo request_type_info = {
      sizeof(InfdRequestClass),  /* class_size */
      NULL,                      /* base_init */
      NULL,                      /* base_finalize */
      infd_request_class_init,   /* class_init */
      NULL,                      /* class_finalize */
      NULL,                      /* class_data */
      sizeof(InfdRequest),       /* instance_size */
      0,                         /* n_preallocs */
      infd_request_init,         /* instance_init */
      NULL                       /* value_table */
    };

    static const GInterfaceInfo request_info = {
      infd_request_request_init,
      NULL,
      NULL
    };

    request_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfdRequest",
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
