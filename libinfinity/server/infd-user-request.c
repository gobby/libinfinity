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
 * SECTION:infd-user-request
 * @title: InfdUserRequest
 * @short_description: Asynchronous request related to a user
 * @include: libinfinity/server/infd-user-request.h
 * @see_also: #InfdSessionProxy
 * @stability: Unstable
 *
 * #InfdUserRequest represents an asynchronous operation which is related to
 * a user in a session. This is usually a user join request.
 */

#include <libinfinity/server/infd-user-request.h>
#include <libinfinity/common/inf-user-request.h>
#include <libinfinity/common/inf-request.h>
#include <libinfinity/inf-marshal.h>

typedef struct _InfdUserRequestPrivate InfdUserRequestPrivate;
struct _InfdUserRequestPrivate {
  gchar* type;
  guint seq;
};

enum {
  PROP_0,

  PROP_TYPE
};

#define INFD_USER_REQUEST_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFD_TYPE_USER_REQUEST, InfdUserRequestPrivate))

static GObjectClass* parent_class;

static void
infd_user_request_init(GTypeInstance* instance,
                       gpointer g_class)
{
  InfdUserRequest* request;
  InfdUserRequestPrivate* priv;

  request = INFD_USER_REQUEST(instance);
  priv = INFD_USER_REQUEST_PRIVATE(request);

  priv->type = NULL;
}

static void
infd_user_request_finalize(GObject* object)
{
  InfdUserRequest* request;
  InfdUserRequestPrivate* priv;

  request = INFD_USER_REQUEST(object);
  priv = INFD_USER_REQUEST_PRIVATE(request);

  g_free(priv->type);

  if(G_OBJECT_CLASS(parent_class)->finalize != NULL)
    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
infd_user_request_set_property(GObject* object,
                               guint prop_id,
                               const GValue* value,
                               GParamSpec* pspec)
{
  InfdUserRequest* request;
  InfdUserRequestPrivate* priv;

  request = INFD_USER_REQUEST(object);
  priv = INFD_USER_REQUEST_PRIVATE(request);

  switch(prop_id)
  {
  case PROP_TYPE:
    g_assert(priv->type == NULL); /* construct only */
    priv->type = g_value_dup_string(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infd_user_request_get_property(GObject* object,
                               guint prop_id,
                               GValue* value,
                               GParamSpec* pspec)
{
  InfdUserRequest* request;
  InfdUserRequestPrivate* priv;

  request = INFD_USER_REQUEST(object);
  priv = INFD_USER_REQUEST_PRIVATE(request);

  switch(prop_id)
  {
  case PROP_TYPE:
    g_value_set_string(value, priv->type);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infd_user_request_request_fail(InfRequest* request,
                               const GError* error)
{
  inf_user_request_finished(INF_USER_REQUEST(request), NULL, error);
}

static gboolean
infd_user_request_request_is_local(InfRequest* request)
{
  /* TODO: At the moment, user requests are always local, because no requests
   * are generated for remote user joins. We should change this, and create
   * such requests also for remote user joins, and set the "requestor"
   * property for InfdUserRequest just the same way we do it for
   * InfdNodeRequest. */
  return TRUE;
}

static void
infd_user_request_class_init(gpointer g_class,
                             gpointer class_data)
{
  GObjectClass* object_class;
  InfdUserRequestClass* request_class;

  object_class = G_OBJECT_CLASS(g_class);
  request_class = INFD_USER_REQUEST_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfdUserRequestPrivate));

  object_class->finalize = infd_user_request_finalize;
  object_class->set_property = infd_user_request_set_property;
  object_class->get_property = infd_user_request_get_property;

  g_object_class_override_property(object_class, PROP_TYPE, "type");
}

static void
infd_user_request_request_init(gpointer g_iface,
                               gpointer iface_data)
{
  InfRequestIface* iface;
  iface = (InfRequestIface*)g_iface;

  iface->fail = infd_user_request_request_fail;
  iface->is_local = infd_user_request_request_is_local;
}

static void
infd_user_request_user_request_init(gpointer g_iface,
                                    gpointer iface_data)
{
  InfUserRequestIface* iface;
  iface = (InfUserRequestIface*)g_iface;

  iface->finished = NULL;
}

GType
infd_user_request_get_type(void)
{
  static GType user_request_type = 0;

  if(!user_request_type)
  {
    static const GTypeInfo user_request_type_info = {
      sizeof(InfdUserRequestClass),  /* class_size */
      NULL,                          /* base_init */
      NULL,                          /* base_finalize */
      infd_user_request_class_init,  /* class_init */
      NULL,                          /* class_finalize */
      NULL,                          /* class_data */
      sizeof(InfdUserRequest),       /* instance_size */
      0,                             /* n_preallocs */
      infd_user_request_init,        /* instance_init */
      NULL                           /* value_table */
    };

    static const GInterfaceInfo request_info = {
      infd_user_request_request_init,
      NULL,
      NULL
    };
    
    static const GInterfaceInfo user_request_info = {
      infd_user_request_user_request_init,
      NULL,
      NULL
    };

    user_request_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfdUserRequest",
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
  }

  return user_request_type;
}

/* vim:set et sw=2 ts=2: */
