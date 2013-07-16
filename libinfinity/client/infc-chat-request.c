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
 * SECTION:infc-chat-request
 * @title: InfcChatRequest
 * @short_description: Asynchronous request related to the chat.
 * @include: libinfinity/client/infc-chat-request.h
 * @see_also: #InfcBrowser, #InfcRequest
 * @stability: Unstable
 *
 * #InfcChatRequest represents an asynchronous operation which is related to
 * subscribing to the chat session of a #InfcBrowser. The request finishes
 * when the server has sent a reply and will emit the
 * #InfcChatRequest::finished signal.
 */

#include <libinfinity/client/infc-chat-request.h>
#include <libinfinity/client/infc-request.h>
#include <libinfinity/common/inf-request.h>
#include <libinfinity/inf-marshal.h>

typedef struct _InfcChatRequestPrivate InfcChatRequestPrivate;
struct _InfcChatRequestPrivate {
  guint seq;
};

enum {
  FINISHED,

  LAST_SIGNAL
};

enum {
  PROP_0,

  PROP_TYPE,
  PROP_SEQ
};

#define INFC_CHAT_REQUEST_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFC_TYPE_CHAT_REQUEST, InfcChatRequestPrivate))

static GObjectClass* parent_class;
static guint chat_request_signals[LAST_SIGNAL];

static void
infc_chat_request_init(GTypeInstance* instance,
                       gpointer g_class)
{
  InfcChatRequest* request;
  InfcChatRequestPrivate* priv;

  request = INFC_CHAT_REQUEST(instance);
  priv = INFC_CHAT_REQUEST_PRIVATE(request);

  priv->seq = 0;
}

static void
infc_chat_request_finalize(GObject* object)
{
  InfcChatRequest* request;
  InfcChatRequestPrivate* priv;

  request = INFC_CHAT_REQUEST(object);
  priv = INFC_CHAT_REQUEST_PRIVATE(request);

  if(G_OBJECT_CLASS(parent_class)->finalize != NULL)
    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
infc_chat_request_set_property(GObject* object,
                               guint prop_id,
                               const GValue* value,
                               GParamSpec* pspec)
{
  InfcChatRequest* request;
  InfcChatRequestPrivate* priv;

  request = INFC_CHAT_REQUEST(object);
  priv = INFC_CHAT_REQUEST_PRIVATE(request);

  switch(prop_id)
  {
  case PROP_TYPE:
    /* this can only have subscribe-chat type, otherwise it would not be a
     * InfcChatRequest. */
    g_assert(strcmp(g_value_get_string(value), "subscribe-chat") == 0);
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
infc_chat_request_get_property(GObject* object,
                               guint prop_id,
                               GValue* value,
                               GParamSpec* pspec)
{
  InfcChatRequest* request;
  InfcChatRequestPrivate* priv;

  request = INFC_CHAT_REQUEST(object);
  priv = INFC_CHAT_REQUEST_PRIVATE(request);

  switch(prop_id)
  {
  case PROP_TYPE:
    g_value_set_static_string(value, "subscribe-chat");
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
infc_chat_request_request_fail(InfRequest* request,
                               const GError* error)
{
  g_signal_emit(
    request,
    chat_request_signals[FINISHED],
    0,
    error
  );
}

static void
infc_chat_request_class_init(gpointer g_class,
                             gpointer class_data)
{
  GObjectClass* object_class;
  InfcChatRequestClass* request_class;

  object_class = G_OBJECT_CLASS(g_class);
  request_class = INFC_CHAT_REQUEST_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfcChatRequestPrivate));

  object_class->finalize = infc_chat_request_finalize;
  object_class->set_property = infc_chat_request_set_property;
  object_class->get_property = infc_chat_request_get_property;

  request_class->finished = NULL;

  /**
   * InfcChatRequest::finished:
   * @request: The #InfcUserRequest object emitting the signal.
   * @error: Reason of request failure in the case of an error.
   *
   * This signal is emitted when the chat request finishes. If it finishes
   * successfully, @error will be %NULL, othewise it will be non-%NULL and
   * contains error information.
   */
  chat_request_signals[FINISHED] = g_signal_new(
    "finished",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfcChatRequestClass, finished),
    NULL, NULL,
    inf_marshal_VOID__POINTER,
    G_TYPE_NONE,
    1,
    G_TYPE_POINTER /* GError* */
  );

  g_object_class_override_property(object_class, PROP_TYPE, "type");
  g_object_class_override_property(object_class, PROP_SEQ, "seq");
}

static void
infc_chat_request_request_init(gpointer g_iface,
                               gpointer iface_data)
{
  InfRequestIface* iface;
  iface = (InfRequestIface*)g_iface;

  iface->fail = infc_chat_request_request_fail;
}

static void
infc_chat_request_infc_request_init(gpointer g_iface,
                                    gpointer iface_data)
{
  InfcRequestIface* iface;
  iface = (InfcRequestIface*)g_iface;
}

GType
infc_chat_request_get_type(void)
{
  static GType chat_request_type = 0;

  if(!chat_request_type)
  {
    static const GTypeInfo chat_request_type_info = {
      sizeof(InfcChatRequestClass),  /* class_size */
      NULL,                          /* base_init */
      NULL,                          /* base_finalize */
      infc_chat_request_class_init,  /* class_init */
      NULL,                          /* class_finalize */
      NULL,                          /* class_data */
      sizeof(InfcChatRequest),       /* instance_size */
      0,                             /* n_preallocs */
      infc_chat_request_init,        /* instance_init */
      NULL                           /* value_table */
    };

    static const GInterfaceInfo request_info = {
      infc_chat_request_request_init,
      NULL,
      NULL
    };

    static const GInterfaceInfo infc_request_info = {
      infc_chat_request_infc_request_init,
      NULL,
      NULL
    };

    chat_request_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfcChatRequest",
      &chat_request_type_info,
      0
    );

    g_type_add_interface_static(
      chat_request_type,
      INF_TYPE_REQUEST,
      &request_info
    );

    g_type_add_interface_static(
      chat_request_type,
      INFC_TYPE_REQUEST,
      &infc_request_info
    );
  }

  return chat_request_type;
}

/**
 * infc_chat_request_finished:
 * @request: A #InfcChatRequest.
 * @error: A #GError explaining the reason for request failure, or %NULL.
 *
 * Emits the #InfcChatRequest::finished signal on @request. @error should be
 * %NULL if the request finished successfully, otherwise it should contain
 * appropriate error information.
 **/
void
infc_chat_request_finished(InfcChatRequest* request,
                           const GError* error)
{
  g_signal_emit(
    G_OBJECT(request),
    chat_request_signals[FINISHED],
    0,
    error
  );
}

/* vim:set et sw=2 ts=2: */
