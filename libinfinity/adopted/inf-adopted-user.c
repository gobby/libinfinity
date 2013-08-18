/* libinfinity - a GObject-based infinote implementation
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
 * SECTION:inf-adopted-user
 * @title: InfAdoptedUser
 * @short_description: User in a #InfAdoptedUser.
 * @include: libinfinity/adopted/inf-adopted-user.h
 * @see_also: #InfAdoptedSession, #InfAdoptedAlgorithm
 * @stability: Unstable
 *
 * #InfAdoptedUser is a #InfUser-derived class that is used in
 * #InfAdoptedSession. It holds all user-specific information that is needed
 * by #InfAdoptedAlgorithm to handle the concurrency control. This includes
 * the user's request log which stores all the requests made by the user and
 * a state vector which specifies the document state that the user has, as
 * known to the local host. This information is extracted from the most recent
 * request received from the user.
 */

#include <libinfinity/adopted/inf-adopted-user.h>
#include <libinfinity/adopted/inf-adopted-state-vector.h>

typedef struct _InfAdoptedUserPrivate InfAdoptedUserPrivate;
struct _InfAdoptedUserPrivate {
  InfAdoptedStateVector* vector;
  InfAdoptedRequestLog* log;
};

enum {
  PROP_0,

  PROP_VECTOR,
  PROP_REQUEST_LOG
};

#define INF_ADOPTED_USER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_ADOPTED_TYPE_USER, InfAdoptedUserPrivate))
#define INF_ADOPTED_USER_PRIVATE(obj)     ((InfAdoptedUserPrivate*)(obj)->priv)

static InfUserClass* parent_class;

static void
inf_adopted_user_init(GTypeInstance* instance,
                      gpointer g_class)
{
  InfAdoptedUser* user;
  InfAdoptedUserPrivate* priv;

  user = INF_ADOPTED_USER(instance);
  user->priv = INF_ADOPTED_USER_GET_PRIVATE(user);
  priv = INF_ADOPTED_USER_PRIVATE(user);

  priv->vector = inf_adopted_state_vector_new();
  priv->log = NULL;
}

static GObject*
inf_adopted_user_constructor(GType type,
                             guint n_construct_properties,
                             GObjectConstructParam* construct_properties)
{
  GObject* object;
  InfAdoptedUser* user;
  InfAdoptedUserPrivate* priv;

  object = G_OBJECT_CLASS(parent_class)->constructor(
    type,
    n_construct_properties,
    construct_properties
  );

  user = INF_ADOPTED_USER(object);
  priv = INF_ADOPTED_USER_PRIVATE(user);

  /* Create empty request log if none was set during construction */
  priv->log = inf_adopted_request_log_new(inf_user_get_id(INF_USER(user)));
  return object;
}

static void
inf_adopted_user_dispose(GObject* object)
{
  InfAdoptedUser* user;
  InfAdoptedUserPrivate* priv;

  user = INF_ADOPTED_USER(object);
  priv = INF_ADOPTED_USER_PRIVATE(user);

  if(priv->log != NULL)
  {
    g_object_unref(G_OBJECT(priv->log));
    priv->log = NULL;
  }

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_adopted_user_finalize(GObject* object)
{
  InfAdoptedUser* user;
  InfAdoptedUserPrivate* priv;

  user = INF_ADOPTED_USER(object);
  priv = INF_ADOPTED_USER_PRIVATE(user);

  inf_adopted_state_vector_free(priv->vector);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_adopted_user_set_property(GObject* object,
                              guint prop_id,
                              const GValue* value,
                              GParamSpec* pspec)
{
  InfAdoptedUser* user;
  InfAdoptedUserPrivate* priv;
  InfAdoptedRequestLog* log;

  user = INF_ADOPTED_USER(object);
  priv = INF_ADOPTED_USER_PRIVATE(user);

  switch(prop_id)
  {
  case PROP_VECTOR:
    inf_adopted_state_vector_free(priv->vector);
    priv->vector = g_value_dup_boxed(value);
    break;
  case PROP_REQUEST_LOG:
    g_assert(priv->log == NULL); /* construct only */

    if(g_value_get_object(value) != NULL)
    {
      log = INF_ADOPTED_REQUEST_LOG(g_value_get_object(value));

      g_assert(
        inf_adopted_request_log_get_user_id(log) ==
        inf_user_get_id(INF_USER(user))
      );

      priv->log = log;
      g_object_ref(G_OBJECT(log));
    }

    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_adopted_user_get_property(GObject* object,
                              guint prop_id,
                              GValue* value,
                              GParamSpec* pspec)
{
  InfAdoptedUser* user;
  InfAdoptedUserPrivate* priv;

  user = INF_ADOPTED_USER(object);
  priv = INF_ADOPTED_USER_PRIVATE(user);

  switch(prop_id)
  {
  case PROP_VECTOR:
    g_value_set_boxed(value, priv->vector);
    break;
  case PROP_REQUEST_LOG:
    g_value_set_object(value, G_OBJECT(priv->log));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_adopted_user_class_init(gpointer g_class,
                            gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = INF_USER_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfAdoptedUserPrivate));

  object_class->constructor = inf_adopted_user_constructor;
  object_class->dispose = inf_adopted_user_dispose;
  object_class->finalize = inf_adopted_user_finalize;
  object_class->set_property = inf_adopted_user_set_property;
  object_class->get_property = inf_adopted_user_get_property;

  g_object_class_install_property(
    object_class,
    PROP_VECTOR,
    g_param_spec_boxed(
      "vector",
      "State vector",
      "The state this user is currently at",
      INF_ADOPTED_TYPE_STATE_VECTOR,
      G_PARAM_READWRITE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_REQUEST_LOG,
    g_param_spec_object(
      "request-log",
      "Request log",
      "Request log of this user",
      INF_ADOPTED_TYPE_REQUEST_LOG,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );
}

GType
inf_adopted_user_get_type(void)
{
  static GType user_type = 0;

  if(!user_type)
  {
    static const GTypeInfo user_type_info = {
      sizeof(InfAdoptedUserClass),  /* class_size */
      NULL,                         /* base_init */
      NULL,                         /* base_finalize */
      inf_adopted_user_class_init,  /* class_init */
      NULL,                         /* class_finalize */
      NULL,                         /* class_data */
      sizeof(InfAdoptedUser),       /* instance_size */
      0,                            /* n_preallocs */
      inf_adopted_user_init,        /* instance_init */
      NULL                          /* value_table */
    };

    user_type = g_type_register_static(
      INF_TYPE_USER,
      "InfAdoptedUser",
      &user_type_info,
      0
    );
  }

  return user_type;
}

/**
 * inf_adopted_user_get_component:
 * @user: A #InfAdoptedUser.
 * @id: The component to retrieve.
 *
 * Returns the amount of requests @user is guaranteed to have processed from
 * the user with ID @id.
 *
 * Returns: The number of requests @user has processed from @id.
 **/
guint
inf_adopted_user_get_component(InfAdoptedUser* user,
                               guint id)
{
  g_return_val_if_fail(INF_ADOPTED_IS_USER(user), 0);
  g_return_val_if_fail(id != 0, 0);

  return inf_adopted_state_vector_get(
    INF_ADOPTED_USER_PRIVATE(user)->vector,
    id
  );
}

/**
 * inf_adopted_user_get_vector:
 * @user: A #InfAdoptedUser.
 *
 * Returns the current vector time of @user.
 *
 * Return Value: The current vector time of @user.
 **/
InfAdoptedStateVector*
inf_adopted_user_get_vector(InfAdoptedUser* user)
{
  g_return_val_if_fail(INF_ADOPTED_IS_USER(user), NULL);
  return INF_ADOPTED_USER_PRIVATE(user)->vector;
}

/**
 * inf_adopted_user_set_vector:
 * @user: A #InfAdoptedUser.
 * @vec: A #InfAdoptedStateVector.
 *
 * Updates the state vector of @user. This function takes ownership of @vec.
 **/
void
inf_adopted_user_set_vector(InfAdoptedUser* user,
                            InfAdoptedStateVector* vec)
{
  InfAdoptedUserPrivate* priv;

  g_return_if_fail(INF_ADOPTED_IS_USER(user));
  g_return_if_fail(vec != NULL);

  priv = INF_ADOPTED_USER_PRIVATE(user);

  inf_adopted_state_vector_free(priv->vector);
  priv->vector = vec;

  g_object_notify(G_OBJECT(user), "vector");
}

/**
 * inf_adopted_user_get_request_log:
 * @user: A #InfAdoptedUser.
 *
 * Returns the request log of @user.
 *
 * Return Value: User's #InfAdoptedRequestLog.
 **/
InfAdoptedRequestLog*
inf_adopted_user_get_request_log(InfAdoptedUser* user)
{
  g_return_val_if_fail(INF_ADOPTED_IS_USER(user), NULL);
  return INF_ADOPTED_USER_PRIVATE(user)->log;
}

/* vim:set et sw=2 ts=2: */
