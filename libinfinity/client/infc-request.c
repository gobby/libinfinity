/* infcinote - Collaborative notetaking application
 * Copyright (C) 2007 Armin Burgmeier
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
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <libinfinity/client/infc-request.h>
#include <libinfinity/inf-marshal.h>

typedef struct _InfcRequestPrivate InfcRequestPrivate;
struct _InfcRequestPrivate {
  guint seq;
  gchar* name;
};

enum {
  PROP_0,

  PROP_SEQ,
  PROP_NAME
};

enum {
  FAILED,

  LAST_SIGNAL
};

#define INFC_REQUEST_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFC_TYPE_REQUEST, InfcRequestPrivate))

static GObjectClass* parent_class;
static guint request_signals[LAST_SIGNAL];

static void
infc_request_init(GTypeInstance* instance,
                  gpointer g_class)
{
  InfcRequest* request;
  InfcRequestPrivate* priv;

  request = INFC_REQUEST(instance);
  priv = INFC_REQUEST_PRIVATE(request);

  priv->seq = 0;
  priv->name = NULL;
}

static void
infc_request_finalize(GObject* object)
{
  InfcRequest* request;
  InfcRequestPrivate* priv;

  request = INFC_REQUEST(object);
  priv = INFC_REQUEST_PRIVATE(request);

  g_free(priv->name);

  if(parent_class->finalize != NULL)
    parent_class->finalize(object);
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
  case PROP_SEQ:
    priv->seq = g_value_get_uint(value);
    break;
  case PROP_NAME:
    g_free(priv->name);
    priv->name = g_value_dup_string(value);
    break;
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
  case PROP_SEQ:
    g_value_set_uint(value, priv->seq);
    break;
  case PROP_NAME:
    g_value_set_string(value, priv->name);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infc_request_class_init(gpointer g_class,
                        gpointer class_data)
{
  GObjectClass* object_class;
  InfcRequestClass* request_class;

  object_class = G_OBJECT_CLASS(g_class);
  request_class = INFC_REQUEST_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfcRequestPrivate));

  object_class->finalize = infc_request_finalize;
  object_class->set_property = infc_request_set_property;
  object_class->get_property = infc_request_get_property;

  request_class->failed = NULL;

  g_object_class_install_property(
    object_class,
    PROP_SEQ,
    g_param_spec_uint(
      "seq",
      "Sequence identifier",
      "Identifier for this request",
      0,
      G_MAXUINT,
      0,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_NAME,
    g_param_spec_string(
      "name",
      "Request name",
      "Name of the request",
      NULL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  request_signals[FAILED] = g_signal_new(
    "failed",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfcRequestClass, failed),
    NULL, NULL,
    inf_marshal_VOID__POINTER,
    G_TYPE_NONE,
    1,
    G_TYPE_POINTER /* actually a GError* */
  );
}

GType
infc_request_get_type(void)
{
  static GType request_type = 0;

  if(!request_type)
  {
    static const GTypeInfo request_type_info = {
      sizeof(InfcRequestClass),  /* class_size */
      NULL,                      /* base_init */
      NULL,                      /* base_finalize */
      infc_request_class_init,   /* class_init */
      NULL,                      /* class_finalize */
      NULL,                      /* class_data */
      sizeof(InfcRequest),       /* instance_size */
      0,                         /* n_preallocs */
      infc_request_init,         /* instance_init */
      NULL                       /* value_table */
    };

    request_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfcRequest",
      &request_type_info,
      0
    );
  }

  return request_type;
}

/**
 * infc_request_get_seq:
 * @request: A #InfcRequest.
 *
 * Returns the sequence identifier for this request.
 **/
guint
infc_request_get_seq(InfcRequest* request)
{
  g_return_val_if_fail(INFC_IS_REQUEST(request), 0);
  return INFC_REQUEST_PRIVATE(request)->seq;
}

/**
 * infc_request_get_name:
 * @request: A #InfcRequest.
 *
 * Returns the name of the request.
 **/
const gchar*
infc_request_get_name(InfcRequest* request)
{
  g_return_val_if_fail(INFC_IS_REQUEST(request), NULL);
  return INFC_REQUEST_PRIVATE(request)->name;
}

/**
 * infc_request_failed:
 * @request: A #InfcRequest.
 * @error: A #GError.
 *
 * Emits the "failed" signal on @request.
 **/
void
infc_request_failed(InfcRequest* request,
                    GError* error)
{
  g_return_if_fail(INFC_IS_REQUEST(request));
  g_return_if_fail(error != NULL);

  g_signal_emit(G_OBJECT(request), request_signals[FAILED], 0, error);
}

/* vim:set et sw=2 ts=2: */
