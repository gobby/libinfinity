/* infcinote - Collaborative notetaking application
 * Copyright (C) 2007-2015 Armin Burgmeier <armin@arbur.net>
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
 * SECTION:infc-progress-request
 * @title: InfcProgressRequest
 * @short_description: Asynchronous request with dynamic progress.
 * @include: libinfinity/client/infc-progress-request.h
 * @see_also: #InfcRequest, #InfcBrowser
 * @stability: Unstable
 *
 * This class represents a request which consists of multiple steps and
 * for which progress on the overall operation is reported.
 */

#include <libinfinity/client/infc-progress-request.h>

typedef struct _InfcProgressRequestPrivate InfcProgressRequestPrivate;
struct _InfcProgressRequestPrivate {
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

#define INFC_PROGRESS_REQUEST_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFC_TYPE_PROGRESS_REQUEST, InfcProgressRequestPrivate))

G_DEFINE_TYPE_WITH_CODE(InfcProgressRequest, infc_progress_request, INFC_TYPE_REQUEST,
  G_ADD_PRIVATE(InfcProgressRequest))

static void
infc_progress_request_init(InfcProgressRequest* request)
{
  InfcProgressRequestPrivate* priv;
  priv = INFC_PROGRESS_REQUEST_PRIVATE(request);

  priv->current = 0;
  priv->total = 0;

  priv->initiated = FALSE;
}

static void
infc_progress_request_finalize(GObject* object)
{
  InfcProgressRequest* request;
  InfcProgressRequestPrivate* priv;

  request = INFC_PROGRESS_REQUEST(object);
  priv = INFC_PROGRESS_REQUEST_PRIVATE(request);

  G_OBJECT_CLASS(infc_progress_request_parent_class)->finalize(object);
}

static void
infc_progress_request_set_property(GObject* object,
                                   guint prop_id,
                                   const GValue* value,
                                   GParamSpec* pspec)
{
  InfcProgressRequest* request;
  InfcProgressRequestPrivate* priv;

  request = INFC_PROGRESS_REQUEST(object);
  priv = INFC_PROGRESS_REQUEST_PRIVATE(request);

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
infc_progress_request_get_property(GObject* object,
                                   guint prop_id,
                                   GValue* value,
                                   GParamSpec* pspec)
{
  InfcProgressRequest* progress_request;
  InfcProgressRequestPrivate* priv;
  gboolean finished;

  progress_request = INFC_PROGRESS_REQUEST(object);
  priv = INFC_PROGRESS_REQUEST_PRIVATE(progress_request);

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
infc_progress_request_class_init(InfcProgressRequestClass* request_class)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(request_class);

  object_class->finalize = infc_progress_request_finalize;
  object_class->set_property = infc_progress_request_set_property;
  object_class->get_property = infc_progress_request_get_property;

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

/**
 * infc_progress_request_initiated:
 * @request: An #InfcProgressRequest.
 * @total: The total number of items.
 *
 * Initiates the request. A progress request is considered initiated as soon
 * as the total number of items is known.
 **/
void
infc_progress_request_initiated(InfcProgressRequest* request,
                                guint total)
{
  InfcProgressRequestPrivate* priv;
  g_return_if_fail(INFC_IS_PROGRESS_REQUEST(request));

  priv = INFC_PROGRESS_REQUEST_PRIVATE(request);
  g_return_if_fail(priv->initiated == FALSE);

  priv->total = total;
  priv->initiated = TRUE;

  g_object_notify(G_OBJECT(request), "total");
  if(priv->total == 0)
    g_object_notify(G_OBJECT(request), "progress");
}

/**
 * infc_progress_request_get_initiated:
 * @request: A #InfcProgressRequest.
 *
 * Returns whether the request was initiated, i.e. the total number of items
 * is known.
 *
 * Returns: Whether the request was initiated.
 **/
gboolean
infc_progress_request_get_initiated(InfcProgressRequest* request)
{
  g_return_val_if_fail(INFC_IS_PROGRESS_REQUEST(request), FALSE);
  return INFC_PROGRESS_REQUEST_PRIVATE(request)->initiated;
}

/**
 * infc_progress_request_progress:
 * @request: A #InfcProgressRequest.
 *
 * Indicates that one more operation has been performed and changes the
 * #InfcProgressRequest:current property accordingly. The request must be
 * initiated before this function can be called.
 **/
void
infc_progress_request_progress(InfcProgressRequest* request)
{
  InfcProgressRequestPrivate* priv;
  g_return_if_fail(INFC_IS_PROGRESS_REQUEST(request));

  priv = INFC_PROGRESS_REQUEST_PRIVATE(request);
  g_return_if_fail(priv->initiated == TRUE);  
  g_return_if_fail(priv->current < priv->total);

  ++priv->current;
  g_object_notify(G_OBJECT(request), "current");
  g_object_notify(G_OBJECT(request), "progress");
}

/* vim:set et sw=2 ts=2: */
