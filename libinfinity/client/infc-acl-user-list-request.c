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
 * SECTION:infc-acl-user-list-request
 * @title: InfcAclUserListRequest
 * @short_description: Asynchronous request for the ACL user list.
 * @include: libinfinity/client/infc-user-request.h
 * @see_also: #InfcSessionProxy
 * @stability: Unstable
 *
 * #InfcAclUserListRequest represents an asynchronous operation to query the
 * list of ACL users from the server, using the
 * inf_browser_query_acl_user_list() function. The request object can be used
 * to be notified when the operation finishes.
 */

#include <libinfinity/client/infc-acl-user-list-request.h>
#include <libinfinity/client/infc-request.h>
#include <libinfinity/common/inf-acl-user-list-request.h>
#include <libinfinity/common/inf-request.h>

typedef struct _InfcAclUserListRequestPrivate InfcAclUserListRequestPrivate;
struct _InfcAclUserListRequestPrivate {
  guint seq;

  guint current;
  guint total;
  gboolean initiated;
};

enum {
  PROP_0,

  PROP_TYPE,
  PROP_SEQ,

  PROP_CURRENT,
  PROP_TOTAL
};

#define INFC_ACL_USER_LIST_REQUEST_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFC_TYPE_ACL_USER_LIST_REQUEST, InfcAclUserListRequestPrivate))

static GObjectClass* parent_class;

static void
infc_acl_user_list_request_init(GTypeInstance* instance,
                                gpointer g_class)
{
  InfcAclUserListRequest* request;
  InfcAclUserListRequestPrivate* priv;

  request = INFC_ACL_USER_LIST_REQUEST(instance);
  priv = INFC_ACL_USER_LIST_REQUEST_PRIVATE(request);

  priv->seq = 0;

  priv->current = 0;
  priv->total = 0;
  priv->initiated = FALSE;
}

static void
infc_acl_user_list_request_finalize(GObject* object)
{
  InfcAclUserListRequest* request;
  InfcAclUserListRequestPrivate* priv;

  request = INFC_ACL_USER_LIST_REQUEST(object);
  priv = INFC_ACL_USER_LIST_REQUEST_PRIVATE(request);

  if(G_OBJECT_CLASS(parent_class)->finalize != NULL)
    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
infc_acl_user_list_request_set_property(GObject* object,
                                        guint prop_id,
                                        const GValue* value,
                                        GParamSpec* pspec)
{
  InfcAclUserListRequest* request;
  InfcAclUserListRequestPrivate* priv;

  request = INFC_ACL_USER_LIST_REQUEST(object);
  priv = INFC_ACL_USER_LIST_REQUEST_PRIVATE(request);

  switch(prop_id)
  {
  case PROP_TYPE:
    g_assert(strcmp(g_value_get_string(value), "query-user-list") == 0);
    break;
  case PROP_SEQ:
    g_assert(priv->seq == 0); /* construct only */
    priv->seq = g_value_get_uint(value);
    break;
  case PROP_CURRENT:
  case PROP_TOTAL:
    /* readonly */
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infc_acl_user_list_request_get_property(GObject* object,
                                        guint prop_id,
                                        GValue* value,
                                        GParamSpec* pspec)
{
  InfcAclUserListRequest* request;
  InfcAclUserListRequestPrivate* priv;

  request = INFC_ACL_USER_LIST_REQUEST(object);
  priv = INFC_ACL_USER_LIST_REQUEST_PRIVATE(request);

  switch(prop_id)
  {
  case PROP_TYPE:
    g_value_set_static_string(value, "query-user-list");
    break;
  case PROP_SEQ:
    g_value_set_uint(value, priv->seq);
    break;
  case PROP_CURRENT:
    g_value_set_uint(value, priv->current);
    break;
  case PROP_TOTAL:
    g_value_set_uint(value, priv->total);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infc_acl_user_list_request_request_fail(InfRequest* request,
                                        const GError* error)
{
  inf_acl_user_list_request_finished(
    INF_ACL_USER_LIST_REQUEST(request),
    error
  );
}

static void
infc_acl_user_list_request_class_init(gpointer g_class,
                                      gpointer class_data)
{
  GObjectClass* object_class;
  InfcAclUserListRequestClass* request_class;

  object_class = G_OBJECT_CLASS(g_class);
  request_class = INFC_ACL_USER_LIST_REQUEST_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfcAclUserListRequestPrivate));

  object_class->finalize = infc_acl_user_list_request_finalize;
  object_class->set_property = infc_acl_user_list_request_set_property;
  object_class->get_property = infc_acl_user_list_request_get_property;

  g_object_class_override_property(object_class, PROP_TYPE, "type");
  g_object_class_override_property(object_class, PROP_SEQ, "seq");
  g_object_class_override_property(object_class, PROP_CURRENT, "current");
  g_object_class_override_property(object_class, PROP_TOTAL, "total");
}

static void
infc_acl_user_list_request_request_init(gpointer g_iface,
                                        gpointer iface_data)
{
  InfRequestIface* iface;
  iface = (InfRequestIface*)g_iface;

  iface->fail = infc_acl_user_list_request_request_fail;
}

static void
infc_acl_user_list_request_acl_user_list_request_init(gpointer g_iface,
                                                      gpointer iface_data)
{
  InfAclUserListRequestIface* iface;
  iface = (InfAclUserListRequestIface*)g_iface;

  iface->finished = NULL;
}

static void
infc_acl_user_list_request_infc_request_init(gpointer g_iface,
                                             gpointer iface_data)
{
  InfcRequestIface* iface;
  iface = (InfcRequestIface*)g_iface;
}

GType
infc_acl_user_list_request_get_type(void)
{
  static GType acl_user_list_request_type = 0;

  if(!acl_user_list_request_type)
  {
    static const GTypeInfo acl_user_list_request_type_info = {
      sizeof(InfcAclUserListRequestClass),    /* class_size */
      NULL,                                   /* base_init */
      NULL,                                   /* base_finalize */
      infc_acl_user_list_request_class_init,  /* class_init */
      NULL,                                   /* class_finalize */
      NULL,                                   /* class_data */
      sizeof(InfcAclUserListRequest),         /* instance_size */
      0,                                      /* n_preallocs */
      infc_acl_user_list_request_init,        /* instance_init */
      NULL                                    /* value_table */
    };

    static const GInterfaceInfo request_info = {
      infc_acl_user_list_request_request_init,
      NULL,
      NULL
    };
    
    static const GInterfaceInfo acl_user_list_request_info = {
      infc_acl_user_list_request_acl_user_list_request_init,
      NULL,
      NULL
    };

    static const GInterfaceInfo infc_request_info = {
      infc_acl_user_list_request_infc_request_init,
      NULL,
      NULL
    };

    acl_user_list_request_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfcAclUserListRequest",
      &acl_user_list_request_type_info,
      0
    );

    g_type_add_interface_static(
      acl_user_list_request_type,
      INF_TYPE_REQUEST,
      &request_info
    );

    g_type_add_interface_static(
      acl_user_list_request_type,
      INF_TYPE_ACL_USER_LIST_REQUEST,
      &acl_user_list_request_info
    );

    g_type_add_interface_static(
      acl_user_list_request_type,
      INFC_TYPE_REQUEST,
      &infc_request_info
    );
  }

  return acl_user_list_request_type;
}

/**
 * infc_acl_user_list_request_initiated:
 * @request: A #InfAclUserListRequest.
 * @total: The total number of users.
 *
 * Initiates the request. A user list request is considered initiated as soon
 * as the total number of users is known.
 */
void
infc_acl_user_list_request_initiated(InfcAclUserListRequest* request,
                                     guint total)
{
  InfcAclUserListRequestPrivate* priv;
  g_return_if_fail(INFC_IS_ACL_USER_LIST_REQUEST(request));

  priv = INFC_ACL_USER_LIST_REQUEST_PRIVATE(request);
  g_return_if_fail(priv->initiated == FALSE);

  priv->total = total;
  priv->initiated = TRUE;

  g_object_notify(G_OBJECT(request), "total");
}

/**
 * infc_acl_user_list_request_get_initiated:
 * @request: A #InfcAclUserListRequest.
 *
 * Returns whether the request was already initiated, i.e. the total number
 * of users is known at this point.
 *
 * Returns: Whether the request was initiated.
 */
gboolean
infc_acl_user_list_request_get_initiated(InfcAclUserListRequest* request)
{
  g_return_val_if_fail(INFC_IS_ACL_USER_LIST_REQUEST(request), FALSE);
  return INFC_ACL_USER_LIST_REQUEST_PRIVATE(request)->initiated;
}

/**
 * infc_acl_user_list_request_progress:
 * @request: A #InfcAclUserListRequest.
 *
 * Indicates that one more user has been transmitted and changes the
 * #InfcAclUserListRequest:current property accordingly. The request must be
 * initiated before this function can be called.
 */
void
infc_acl_user_list_request_progress(InfcAclUserListRequest* request)
{
  InfcAclUserListRequestPrivate* priv;
  g_return_if_fail(INFC_IS_ACL_USER_LIST_REQUEST(request));

  priv = INFC_ACL_USER_LIST_REQUEST_PRIVATE(request);
  g_return_if_fail(priv->initiated == TRUE);  
  g_return_if_fail(priv->current < priv->total);

  ++priv->current;
  g_object_notify(G_OBJECT(request), "current");
}

/* vim:set et sw=2 ts=2: */
