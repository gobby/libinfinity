/* infinote - Collaborative notetaking application
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

#include <libinfinity/adopted/inf-adopted-user.h>
#include <libinfinity/adopted/inf-adopted-state-vector.h>

typedef struct _InfAdoptedUserPrivate InfAdoptedUserPrivate;
struct _InfAdoptedUserPrivate {
  InfAdoptedStateVector* vector;
};

enum {
  PROP_0,

  PROP_VECTOR
};

#define INF_ADOPTED_USER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_ADOPTED_TYPE_USER, InfAdoptedUserPrivate))

static InfUserClass* parent_class;

static void
inf_adopted_user_init(GTypeInstance* instance,
                      gpointer g_class)
{
  InfAdoptedUser* user;
  InfAdoptedUserPrivate* priv;

  user = INF_ADOPTED_USER(instance);
  priv = INF_ADOPTED_USER_PRIVATE(user);

  priv->vector = inf_adopted_state_vector_new();
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

  user = INF_ADOPTED_USER(object);
  priv = INF_ADOPTED_USER_PRIVATE(user);

  switch(prop_id)
  {
  case PROP_VECTOR:
    inf_adopted_state_vector_free(priv->vector);
    priv->vector = g_value_dup_boxed(value);
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

/** inf_adopted_user_get_component:
 *
 * @user: A #InfAdoptedUser.
 * @component: The user component to retrieve.
 *
 * Returns the amount of requests @user is guaranteed to have processed from
 * @component.
 **/
guint
inf_adopted_user_get_component(InfAdoptedUser* user,
                               InfUser* component)
{
  g_return_val_if_fail(INF_ADOPTED_IS_USER(user), 0);
  g_return_val_if_fail(INF_IS_USER(component), 0);

  return inf_adopted_state_vector_get(
    INF_ADOPTED_USER_PRIVATE(user)->vector,
    component
  );
}

/** inf_adopted_user_get_vector:
 *
 * @user: A #InfAdoptedUser.
 *
 * Returns the current vector time of @user.
 *
 * Return Value: The current vector time of @user. Free with
 * inf_adopted_state_vector_free() when done.
 **/
InfAdoptedStateVector*
inf_adopted_user_get_vector(InfAdoptedUser* user)
{
  g_return_val_if_fail(INF_ADOPTED_IS_USER(user), NULL);

  return inf_adopted_state_vector_copy(
    INF_ADOPTED_USER_PRIVATE(user)->vector
  );
}

/** inf_adopted_user_set_vector:
 *
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

/* vim:set et sw=2 ts=2: */
