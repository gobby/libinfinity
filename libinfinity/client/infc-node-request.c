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

#include <libinfinity/client/infc-browser.h>
#include <libinfinity/client/infc-node-request.h>
#include <libinfinity/inf-marshal.h>

enum {
  FINISHED,

  LAST_SIGNAL
};

#define INFC_NODE_REQUEST_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFC_TYPE_NODE_REQUEST, InfcNodeRequestPrivate))

static InfcRequestClass* parent_class;
static guint node_request_signals[LAST_SIGNAL];

static void
infc_node_request_init(GTypeInstance* instance,
                          gpointer g_class)
{
  InfcNodeRequest* node_request;
  node_request = INFC_NODE_REQUEST(instance);
}

static void
infc_node_request_finalize(GObject* object)
{
  InfcNodeRequest* request;
  request = INFC_NODE_REQUEST(object);

  if(G_OBJECT_CLASS(parent_class)->finalize != NULL)
    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
infc_node_request_class_init(gpointer g_class,
                             gpointer class_data)
{
  GObjectClass* object_class;
  InfcNodeRequestClass* request_class;

  object_class = G_OBJECT_CLASS(g_class);
  request_class = INFC_NODE_REQUEST_CLASS(g_class);

  parent_class = INFC_REQUEST_CLASS(g_type_class_peek_parent(g_class));

  object_class->finalize = infc_node_request_finalize;

  request_class->finished = NULL;

  node_request_signals[FINISHED] = g_signal_new(
    "finished",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfcNodeRequestClass, finished),
    NULL, NULL,
    inf_marshal_VOID__BOXED,
    G_TYPE_NONE,
    1,
    INFC_TYPE_BROWSER_ITER | G_SIGNAL_TYPE_STATIC_SCOPE
  );
}

GType
infc_node_request_get_type(void)
{
  static GType node_request_type = 0;

  if(!node_request_type)
  {
    static const GTypeInfo node_request_type_info = {
      sizeof(InfcNodeRequestClass),  /* class_size */
      NULL,                          /* base_init */
      NULL,                          /* base_finalize */
      infc_node_request_class_init,  /* class_init */
      NULL,                          /* class_finalize */
      NULL,                          /* class_data */
      sizeof(InfcNodeRequest),       /* instance_size */
      0,                             /* n_preallocs */
      infc_node_request_init,        /* instance_init */
      NULL                           /* value_table */
    };

    node_request_type = g_type_register_static(
      INFC_TYPE_REQUEST,
      "InfcNodeRequest",
      &node_request_type_info,
      0
    );
  }

  return node_request_type;
}

/** infc_node_request_finished:
 *
 * @request: A #InfcNodeRequest.
 * @iter: A #InfcBrowserIter pointing to a node affected by the request.
 *
 * Emits the "finished" signal on @request.
 **/
void
infc_node_request_finished(InfcNodeRequest* request,
                           const InfcBrowserIter* iter)
{
  g_signal_emit(G_OBJECT(request), node_request_signals[FINISHED], 0, iter);
}
