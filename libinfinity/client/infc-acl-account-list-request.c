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
 * SECTION:infc-acl-account-list-request
 * @title: InfcAclAccountListRequest
 * @short_description: Asynchronous request for the ACL account list.
 * @include: libinfinity/client/infc-account-request.h
 * @see_also: #InfcSessionProxy
 * @stability: Unstable
 *
 * #InfcAclAccountListRequest represents an asynchronous operation to query the
 * list of ACL accounts from the server, using the
 * inf_browser_query_acl_account_list() function. The request object can be used
 * to be notified when the operation finishes.
 */

#include <libinfinity/client/infc-acl-account-list-request.h>
#include <libinfinity/client/infc-request.h>
#include <libinfinity/common/inf-acl-account-list-request.h>
#include <libinfinity/common/inf-request.h>

typedef struct _InfcAclAccountListRequestPrivate
  InfcAclAccountListRequestPrivate;
struct _InfcAclAccountListRequestPrivate {
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

#define INFC_ACL_ACCOUNT_LIST_REQUEST_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFC_TYPE_ACL_ACCOUNT_LIST_REQUEST, InfcAclAccountListRequestPrivate))

static GObjectClass* parent_class;

static void
infc_acl_account_list_request_init(GTypeInstance* instance,
                                   gpointer g_class)
{
  InfcAclAccountListRequest* request;
  InfcAclAccountListRequestPrivate* priv;

  request = INFC_ACL_ACCOUNT_LIST_REQUEST(instance);
  priv = INFC_ACL_ACCOUNT_LIST_REQUEST_PRIVATE(request);

  priv->seq = 0;

  priv->current = 0;
  priv->total = 0;
  priv->initiated = FALSE;
}

static void
infc_acl_account_list_request_finalize(GObject* object)
{
  InfcAclAccountListRequest* request;
  InfcAclAccountListRequestPrivate* priv;

  request = INFC_ACL_ACCOUNT_LIST_REQUEST(object);
  priv = INFC_ACL_ACCOUNT_LIST_REQUEST_PRIVATE(request);

  if(G_OBJECT_CLASS(parent_class)->finalize != NULL)
    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
infc_acl_account_list_request_set_property(GObject* object,
                                           guint prop_id,
                                           const GValue* value,
                                           GParamSpec* pspec)
{
  InfcAclAccountListRequest* request;
  InfcAclAccountListRequestPrivate* priv;

  request = INFC_ACL_ACCOUNT_LIST_REQUEST(object);
  priv = INFC_ACL_ACCOUNT_LIST_REQUEST_PRIVATE(request);

  switch(prop_id)
  {
  case PROP_TYPE:
    g_assert(strcmp(g_value_get_string(value), "query-acl-account-list") == 0);
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
infc_acl_account_list_request_get_property(GObject* object,
                                           guint prop_id,
                                           GValue* value,
                                           GParamSpec* pspec)
{
  InfcAclAccountListRequest* request;
  InfcAclAccountListRequestPrivate* priv;

  request = INFC_ACL_ACCOUNT_LIST_REQUEST(object);
  priv = INFC_ACL_ACCOUNT_LIST_REQUEST_PRIVATE(request);

  switch(prop_id)
  {
  case PROP_TYPE:
    g_value_set_static_string(value, "query-acl-account-list");
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
infc_acl_account_list_request_request_fail(InfRequest* request,
                                           const GError* error)
{
  inf_acl_account_list_request_finished(
    INF_ACL_ACCOUNT_LIST_REQUEST(request),
    error
  );
}

static void
infc_acl_account_list_request_class_init(gpointer g_class,
                                         gpointer class_data)
{
  GObjectClass* object_class;
  InfcAclAccountListRequestClass* request_class;

  object_class = G_OBJECT_CLASS(g_class);
  request_class = INFC_ACL_ACCOUNT_LIST_REQUEST_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfcAclAccountListRequestPrivate));

  object_class->finalize = infc_acl_account_list_request_finalize;
  object_class->set_property = infc_acl_account_list_request_set_property;
  object_class->get_property = infc_acl_account_list_request_get_property;

  g_object_class_override_property(object_class, PROP_TYPE, "type");
  g_object_class_override_property(object_class, PROP_SEQ, "seq");
  g_object_class_override_property(object_class, PROP_CURRENT, "current");
  g_object_class_override_property(object_class, PROP_TOTAL, "total");
}

static void
infc_acl_account_list_request_request_init(gpointer g_iface,
                                           gpointer iface_data)
{
  InfRequestIface* iface;
  iface = (InfRequestIface*)g_iface;

  iface->fail = infc_acl_account_list_request_request_fail;
}

static void
infc_acl_account_list_request_acl_account_list_request_init(gpointer g_iface,
                                                            gpointer d_iface)
{
  InfAclAccountListRequestIface* iface;
  iface = (InfAclAccountListRequestIface*)g_iface;

  iface->finished = NULL;
}

static void
infc_acl_account_list_request_infc_request_init(gpointer g_iface,
                                                gpointer iface_data)
{
  InfcRequestIface* iface;
  iface = (InfcRequestIface*)g_iface;
}

GType
infc_acl_account_list_request_get_type(void)
{
  static GType acl_account_list_request_type = 0;

  if(!acl_account_list_request_type)
  {
    static const GTypeInfo acl_account_list_request_type_info = {
      sizeof(InfcAclAccountListRequestClass),    /* class_size */
      NULL,                                      /* base_init */
      NULL,                                      /* base_finalize */
      infc_acl_account_list_request_class_init,  /* class_init */
      NULL,                                      /* class_finalize */
      NULL,                                      /* class_data */
      sizeof(InfcAclAccountListRequest),         /* instance_size */
      0,                                         /* n_preallocs */
      infc_acl_account_list_request_init,        /* instance_init */
      NULL                                       /* value_table */
    };

    static const GInterfaceInfo request_info = {
      infc_acl_account_list_request_request_init,
      NULL,
      NULL
    };
    
    static const GInterfaceInfo acl_account_list_request_info = {
      infc_acl_account_list_request_acl_account_list_request_init,
      NULL,
      NULL
    };

    static const GInterfaceInfo infc_request_info = {
      infc_acl_account_list_request_infc_request_init,
      NULL,
      NULL
    };

    acl_account_list_request_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfcAclAccountListRequest",
      &acl_account_list_request_type_info,
      0
    );

    g_type_add_interface_static(
      acl_account_list_request_type,
      INF_TYPE_REQUEST,
      &request_info
    );

    g_type_add_interface_static(
      acl_account_list_request_type,
      INF_TYPE_ACL_ACCOUNT_LIST_REQUEST,
      &acl_account_list_request_info
    );

    g_type_add_interface_static(
      acl_account_list_request_type,
      INFC_TYPE_REQUEST,
      &infc_request_info
    );
  }

  return acl_account_list_request_type;
}

/**
 * infc_acl_account_list_request_initiated:
 * @request: A #InfAclAccountListRequest.
 * @total: The total number of accounts.
 *
 * Initiates the request. An account list request is considered initiated as
 * soon as the total number of accounts is known.
 */
void
infc_acl_account_list_request_initiated(InfcAclAccountListRequest* request,
                                        guint total)
{
  InfcAclAccountListRequestPrivate* priv;
  g_return_if_fail(INFC_IS_ACL_ACCOUNT_LIST_REQUEST(request));

  priv = INFC_ACL_ACCOUNT_LIST_REQUEST_PRIVATE(request);
  g_return_if_fail(priv->initiated == FALSE);

  priv->total = total;
  priv->initiated = TRUE;

  g_object_notify(G_OBJECT(request), "total");
}

/**
 * infc_acl_account_list_request_get_initiated:
 * @req: A #InfcAclAccountListRequest.
 *
 * Returns whether the request was already initiated, i.e. the total number
 * of accounts is known at this point.
 *
 * Returns: Whether the request was initiated.
 */
gboolean
infc_acl_account_list_request_get_initiated(InfcAclAccountListRequest* req)
{
  g_return_val_if_fail(INFC_IS_ACL_ACCOUNT_LIST_REQUEST(req), FALSE);
  return INFC_ACL_ACCOUNT_LIST_REQUEST_PRIVATE(req)->initiated;
}

/**
 * infc_acl_account_list_request_progress:
 * @request: A #InfcAclAccountListRequest.
 *
 * Indicates that one more account has been transmitted and changes the
 * #InfcAclAccountListRequest:current property accordingly. The request must
 * be initiated before this function can be called.
 */
void
infc_acl_account_list_request_progress(InfcAclAccountListRequest* request)
{
  InfcAclAccountListRequestPrivate* priv;
  g_return_if_fail(INFC_IS_ACL_ACCOUNT_LIST_REQUEST(request));

  priv = INFC_ACL_ACCOUNT_LIST_REQUEST_PRIVATE(request);
  g_return_if_fail(priv->initiated == TRUE);  
  g_return_if_fail(priv->current < priv->total);

  ++priv->current;
  g_object_notify(G_OBJECT(request), "current");
}

/* vim:set et sw=2 ts=2: */
