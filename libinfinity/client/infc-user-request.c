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

#include <libinfinity/client/infc-user-request.h>
#include <libinfinity/inf-marshal.h>

enum {
  FINISHED,

  LAST_SIGNAL
};

#define INFC_USER_REQUEST_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFC_TYPE_USER_REQUEST, InfcUserRequestPrivate))

static InfcRequestClass* parent_class;
static guint user_request_signals[LAST_SIGNAL];

static void
infc_user_request_init(GTypeInstance* instance,
                          gpointer g_class)
{
  InfcUserRequest* user_request;
  user_request = INFC_USER_REQUEST(instance);
}

static void
infc_user_request_finalize(GObject* object)
{
  InfcUserRequest* request;
  request = INFC_USER_REQUEST(object);

  if(G_OBJECT_CLASS(parent_class)->finalize != NULL)
    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
infc_user_request_class_init(gpointer g_class,
                                gpointer class_data)
{
  GObjectClass* object_class;
  InfcUserRequestClass* request_class;

  object_class = G_OBJECT_CLASS(g_class);
  request_class = INFC_USER_REQUEST_CLASS(g_class);

  parent_class = INFC_REQUEST_CLASS(g_type_class_peek_parent(g_class));

  object_class->finalize = infc_user_request_finalize;

  request_class->finished = NULL;

  user_request_signals[FINISHED] = g_signal_new(
    "finished",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfcUserRequestClass, finished),
    NULL, NULL,
    inf_marshal_VOID__OBJECT,
    G_TYPE_NONE,
    1,
    INF_TYPE_USER
  );
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

    user_request_type = g_type_register_static(
      INFC_TYPE_REQUEST,
      "InfcUserRequest",
      &user_request_type_info,
      0
    );
  }

  return user_request_type;
}

/** infc_user_request_finished:
 *
 * @request: A #InfcUserRequest.
 * @user: The #InfUser affected by the request.
 *
 * Emits the "finished" signal on @request.
 **/
void
infc_user_request_finished(InfcUserRequest* request,
                           InfUser* user)
{
  g_signal_emit(G_OBJECT(request), user_request_signals[FINISHED], 0, user);
}

/* vim:set et sw=2 ts=2: */
