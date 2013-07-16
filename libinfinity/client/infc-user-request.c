/* infcinote - Collaborative notetaking application
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
 * SECTION:infc-user-request
 * @title: InfcUserRequest
 * @short_description: Asynchronous request related to a user
 * @include: libinfinity/client/infc-user-request.h
 * @see_also: #InfcSessionProxy
 * @stability: Unstable
 *
 * #InfcUserRequest represents an asynchronous operation which is related to
 * a user in a session. This is typically a user join request.
 */

#include <libinfinity/client/infc-user-request.h>
#include <libinfinity/client/infc-request.h>
#include <libinfinity/common/inf-user-request.h>
#include <libinfinity/common/inf-request.h>

typedef struct _InfcUserRequestPrivate InfcUserRequestPrivate;
struct _InfcUserRequestPrivate {
  gchar* type;
  guint seq;
};

enum {
  PROP_0,

  PROP_TYPE,
  PROP_SEQ
};

#define INFC_USER_REQUEST_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFC_TYPE_USER_REQUEST, InfcUserRequestPrivate))

static GObjectClass* parent_class;

static void
infc_user_request_init(GTypeInstance* instance,
                       gpointer g_class)
{
  InfcUserRequest* request;
  InfcUserRequestPrivate* priv;

  request = INFC_USER_REQUEST(instance);
  priv = INFC_USER_REQUEST_PRIVATE(request);

  priv->type = NULL;
  priv->seq = 0;
}

static void
infc_user_request_finalize(GObject* object)
{
  InfcUserRequest* request;
  InfcUserRequestPrivate* priv;

  request = INFC_USER_REQUEST(object);
  priv = INFC_USER_REQUEST_PRIVATE(request);

  g_free(priv->type);

  if(G_OBJECT_CLASS(parent_class)->finalize != NULL)
    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
infc_user_request_set_property(GObject* object,
                               guint prop_id,
                               const GValue* value,
                               GParamSpec* pspec)
{
  InfcUserRequest* request;
  InfcUserRequestPrivate* priv;

  request = INFC_USER_REQUEST(object);
  priv = INFC_USER_REQUEST_PRIVATE(request);

  switch(prop_id)
  {
  case PROP_TYPE:
    g_assert(priv->type == NULL); /* construct only */
    priv->type = g_value_dup_string(value);
    break;
  case PROP_SEQ:
    g_assert(priv->seq == 0); /* construct only */
    priv->seq = g_value_get_uint(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infc_user_request_get_property(GObject* object,
                               guint prop_id,
                               GValue* value,
                               GParamSpec* pspec)
{
  InfcUserRequest* request;
  InfcUserRequestPrivate* priv;

  request = INFC_USER_REQUEST(object);
  priv = INFC_USER_REQUEST_PRIVATE(request);

  switch(prop_id)
  {
  case PROP_TYPE:
    g_value_set_string(value, priv->type);
    break;
  case PROP_SEQ:
    g_value_set_uint(value, priv->seq);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infc_user_request_request_fail(InfRequest* request,
                               const GError* error)
{
  inf_user_request_finished(INF_USER_REQUEST(request), NULL, error);
}

static void
infc_user_request_class_init(gpointer g_class,
                             gpointer class_data)
{
  GObjectClass* object_class;
  InfcUserRequestClass* request_class;

  object_class = G_OBJECT_CLASS(g_class);
  request_class = INFC_USER_REQUEST_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfcUserRequestPrivate));

  object_class->finalize = infc_user_request_finalize;
  object_class->set_property = infc_user_request_set_property;
  object_class->get_property = infc_user_request_get_property;

  g_object_class_override_property(object_class, PROP_TYPE, "type");
  g_object_class_override_property(object_class, PROP_SEQ, "seq");
}

static void
infc_user_request_request_init(gpointer g_iface,
                               gpointer iface_data)
{
  InfRequestIface* iface;
  iface = (InfRequestIface*)g_iface;

  iface->fail = infc_user_request_request_fail;
}

static void
infc_user_request_user_request_init(gpointer g_iface,
                                    gpointer iface_data)
{
  InfUserRequestIface* iface;
  iface = (InfUserRequestIface*)g_iface;

  iface->finished = NULL;
}

static void
infc_user_request_infc_request_init(gpointer g_iface,
                                    gpointer iface_data)
{
  InfcRequestIface* iface;
  iface = (InfcRequestIface*)g_iface;
}

GType
infc_user_request_get_type(void)
{
  static GType user_request_type = 0;

  if(!user_request_type)
  {
    static const GTypeInfo user_request_type_info = {
      sizeof(InfcUserRequestClass),  /* class_size */
      NULL,                          /* base_init */
      NULL,                          /* base_finalize */
      infc_user_request_class_init,  /* class_init */
      NULL,                          /* class_finalize */
      NULL,                          /* class_data */
      sizeof(InfcUserRequest),       /* instance_size */
      0,                             /* n_preallocs */
      infc_user_request_init,        /* instance_init */
      NULL                           /* value_table */
    };

    static const GInterfaceInfo request_info = {
      infc_user_request_request_init,
      NULL,
      NULL
    };
    
    static const GInterfaceInfo user_request_info = {
      infc_user_request_user_request_init,
      NULL,
      NULL
    };

    static const GInterfaceInfo infc_request_info = {
      infc_user_request_infc_request_init,
      NULL,
      NULL
    };

    user_request_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfcUserRequest",
      &user_request_type_info,
      0
    );

    g_type_add_interface_static(
      user_request_type,
      INF_TYPE_REQUEST,
      &request_info
    );

    g_type_add_interface_static(
      user_request_type,
      INF_TYPE_USER_REQUEST,
      &user_request_info
    );

    g_type_add_interface_static(
      user_request_type,
      INFC_TYPE_REQUEST,
      &infc_request_info
    );
  }

  return user_request_type;
}

/* vim:set et sw=2 ts=2: */
