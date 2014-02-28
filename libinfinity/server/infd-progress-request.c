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
 * SECTION:infd-progress-request
 * @title: InfdProgressRequest
 * @short_description: Watch progress of node exploration
 * @include: libinfinity/server/infd-progress-request.h
 * @see_also: #InfProgressRequest, #InfdNodeRequest, #InfdDirectory
 * @stability: Unstable
 *
 * This class represents a request which consists of multiple steps and
 * for which progress on the overall operation is reported.
 */

#include <libinfinity/server/infd-progress-request.h>

typedef struct _InfdProgressRequestPrivate InfdProgressRequestPrivate;
struct _InfdProgressRequestPrivate {
  guint current;
  guint total;

  gboolean initiated;
};

enum {
  PROP_0,

  PROP_CURRENT,
  PROP_TOTAL,
  PROP_PROGRESS
};

#define INFD_PROGRESS_REQUEST_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFD_TYPE_PROGRESS_REQUEST, InfdProgressRequestPrivate))

static InfdRequestClass* parent_class;

static void
infd_progress_request_init(GTypeInstance* instance,
                           gpointer g_class)
{
  InfdProgressRequest* request;
  InfdProgressRequestPrivate* priv;

  request = INFD_PROGRESS_REQUEST(instance);
  priv = INFD_PROGRESS_REQUEST_PRIVATE(request);

  priv->current = 0;
  priv->total = 0;

  priv->initiated = FALSE;
}

static void
infd_progress_request_finalize(GObject* object)
{
  InfdProgressRequest* request;
  InfdProgressRequestPrivate* priv;

  request = INFD_PROGRESS_REQUEST(object);
  priv = INFD_PROGRESS_REQUEST_PRIVATE(request);

  if(G_OBJECT_CLASS(parent_class)->finalize != NULL)
    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
infd_progress_request_set_property(GObject* object,
                                   guint prop_id,
                                   const GValue* value,
                                   GParamSpec* pspec)
{
  InfdProgressRequest* request;
  InfdProgressRequestPrivate* priv;

  request = INFD_PROGRESS_REQUEST(object);
  priv = INFD_PROGRESS_REQUEST_PRIVATE(request);

  switch(prop_id)
  {
  case PROP_TOTAL:
  case PROP_CURRENT:
  case PROP_PROGRESS:
    /* readonly */
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infd_progress_request_get_property(GObject* object,
                                   guint prop_id,
                                   GValue* value,
                                   GParamSpec* pspec)
{
  InfdProgressRequest* progress_request;
  InfdProgressRequestPrivate* priv;
  gboolean finished;

  progress_request = INFD_PROGRESS_REQUEST(object);
  priv = INFD_PROGRESS_REQUEST_PRIVATE(progress_request);

  switch(prop_id)
  {
  case PROP_TOTAL:
    g_value_set_uint(value, priv->total);
    break;
  case PROP_CURRENT:
    g_value_set_uint(value, priv->current);
    break;
  case PROP_PROGRESS:
    if(priv->initiated == FALSE)
      g_value_set_double(value, 0.0);
    else if(priv->total == 0)
      g_value_set_double(value, 1.0);
    else
      g_value_set_double(value, (double)priv->current / (double)priv->total);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infd_progress_request_class_init(gpointer g_class,
                                gpointer class_data)
{
  GObjectClass* object_class;
  InfdProgressRequestClass* request_class;

  object_class = G_OBJECT_CLASS(g_class);
  request_class = INFD_PROGRESS_REQUEST_CLASS(g_class);

  parent_class = INFD_REQUEST_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfdProgressRequestPrivate));

  object_class->finalize = infd_progress_request_finalize;
  object_class->set_property = infd_progress_request_set_property;
  object_class->get_property = infd_progress_request_get_property;

  g_object_class_install_property(
    object_class,
    PROP_CURRENT,
    g_param_spec_uint(
      "current",
      "Current",
      "The current number of finished operations",
      0,
      G_MAXUINT,
      0,
      G_PARAM_READABLE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_TOTAL,
    g_param_spec_uint(
      "total",
      "Total",
      "The total number of operations",
      0,
      G_MAXUINT,
      0,
      G_PARAM_READABLE
    )
  );

  g_object_class_override_property(object_class, PROP_PROGRESS, "progress");
}

GType
infd_progress_request_get_type(void)
{
  static GType progress_request_type = 0;

  if(!progress_request_type)
  {
    static const GTypeInfo progress_request_type_info = {
      sizeof(InfdProgressRequestClass),  /* class_size */
      NULL,                              /* base_init */
      NULL,                              /* base_finalize */
      infd_progress_request_class_init,  /* class_init */
      NULL,                              /* class_finalize */
      NULL,                              /* class_data */
      sizeof(InfdProgressRequest),       /* instance_size */
      0,                                 /* n_preallocs */
      infd_progress_request_init,        /* instance_init */
      NULL                               /* value_table */
    };

    progress_request_type = g_type_register_static(
      INFD_TYPE_REQUEST,
      "InfdProgressRequest",
      &progress_request_type_info,
      0
    );
  }

  return progress_request_type;
}

/**
 * infd_progress_request_initiated:
 * @request: A #InfdProgressRequest.
 * @total: The total number of operations.
 *
 * Initiates the request. A progress request is considered initiated as soon
 * as the total number of operations is known.
 **/
void
infd_progress_request_initiated(InfdProgressRequest* request,
                                guint total)
{
  InfdProgressRequestPrivate* priv;

  g_return_if_fail(INFD_IS_PROGRESS_REQUEST(request));

  priv = INFD_PROGRESS_REQUEST_PRIVATE(request);

  g_return_if_fail(priv->initiated == FALSE);

  priv->total = total;
  priv->initiated = TRUE;

  g_object_notify(G_OBJECT(request), "total");
  if(priv->total == 0)
    g_object_notify(G_OBJECT(request), "progress");
}

/**
 * infd_progress_request_progress:
 * @request: A #InfdProgressRequest.
 *
 * Indicates that one more operation has been finished and changes the
 * #InfdProgressRequest:current property accordingly.
 **/
void
infd_progress_request_progress(InfdProgressRequest* request)
{
  InfdProgressRequestPrivate* priv;

  g_return_if_fail(INFD_IS_PROGRESS_REQUEST(request));

  priv = INFD_PROGRESS_REQUEST_PRIVATE(request);
  
  g_return_if_fail(priv->current < priv->total);

  ++priv->current;
  g_object_notify(G_OBJECT(request), "current");
  g_object_notify(G_OBJECT(request), "progress");
}

/* vim:set et sw=2 ts=2: */
