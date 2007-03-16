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

#include <libinfinity/inf-user.h>

typedef struct _InfUserPrivate InfUserPrivate;
struct _InfUserPrivate {
  guint id;
  gchar* name;
  InfUserStatus status;
};

enum {
  PROP_0,

  PROP_ID,
  PROP_NAME,
  PROP_STATUS
};

#define INF_USER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TYPE_USER, InfUserPrivate))

static GObjectClass* parent_class;

static void
inf_user_init(GTypeInstance* instance,
              gpointer g_class)
{
  InfUser* user;
  InfUserPrivate* priv;

  user = INF_USER(instance);
  priv = INF_USER_PRIVATE(user);

  priv->id = 0;
  priv->name = NULL;
  priv->status = INF_USER_UNAVAILABLE;
}

static void
inf_user_dispose(GObject* object)
{
  InfUser* user;
  user = INF_USER(object);

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_user_finalize(GObject* object)
{
  InfUser* user;
  InfUserPrivate* priv;

  user = INF_USER(object);
  priv = INF_USER_PRIVATE(user);

  g_free(priv->name);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_user_set_property(GObject* object,
                      guint prop_id,
                      const GValue* value,
                      GParamSpec* pspec)
{
  InfUser* user;
  InfUserPrivate* priv;

  user = INF_USER(object);
  priv = INF_USER_PRIVATE(user);

  switch(prop_id)
  {
  case PROP_ID:
    priv->id = g_value_get_uint(value);
    break;
  case PROP_NAME:
    g_free(priv->name);
    priv->name = g_value_dup_string(value);
    break;
  case PROP_STATUS:
    priv->status = g_value_get_enum(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_user_get_property(GObject* object,
                      guint prop_id,
                      GValue* value,
                      GParamSpec* pspec)
{
  InfUser* user;
  InfUserPrivate* priv;

  user = INF_USER(object);
  priv = INF_USER_PRIVATE(user);

  switch(prop_id)
  {
  case PROP_ID:
    g_value_set_uint(value, priv->id);
    break;
  case PROP_NAME:
    g_value_set_string(value, priv->name);
    break;
  case PROP_STATUS:
    g_value_set_enum(value, priv->status);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_user_class_init(gpointer g_class,
                    gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfUserPrivate));

  object_class->dispose = inf_user_dispose;
  object_class->finalize = inf_user_finalize;
  object_class->set_property = inf_user_set_property;
  object_class->get_property = inf_user_get_property;

  g_object_class_install_property(
    object_class,
    PROP_ID,
    g_param_spec_uint(
      "id",
      "User ID",
      "A Unique User ID",
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
      "User Name",
      "The Name with which a user joined a session. Most servers "
      "ensure that it is unique.",
      "",
      G_PARAM_READWRITE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_STATUS,
    g_param_spec_enum(
      "status",
      "User Status",
      "Whether the user is currently available or not.",
      INF_TYPE_USER_STATUS,
      INF_USER_UNAVAILABLE,
      G_PARAM_READWRITE
    )
  );
}

GType
inf_user_status_get_type(void)
{
  static GType user_status_type = 0;

  if(!user_status_type)
  {
    static const GEnumValue user_status_type_values[] = {
      {
        INF_USER_AVAILABLE,
        "INF_USER_AVAILABLE",
        "available"
      }, {
        INF_USER_UNAVAILABLE,
        "INF_USER_UNAVAILABLE",
        "unavailable"
      }, {
        0,
        NULL,
        NULL
      }
    };

    user_status_type = g_enum_register_static(
      "InfUserStatus",
      user_status_type_values
    );
  }

  return user_status_type;
}

GType
inf_user_get_type(void)
{
  static GType user_type = 0;

  if(!user_type)
  {
    static const GTypeInfo user_type_info = {
      sizeof(InfUserClass),  /* class_size */
      NULL,                  /* base_init */
      NULL,                  /* base_finalize */
      inf_user_class_init,   /* class_init */
      NULL,                  /* class_finalize */
      NULL,                  /* class_data */
      sizeof(InfUser),       /* instance_size */
      0,                     /* n_preallocs */
      inf_user_init,         /* instance_init */
      NULL                   /* value_table */
    };

    user_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfUser",
      &user_type_info,
      0
    );
  }

  return user_type;
}

/** inf_user_get_id:
 *
 * @user A #InfUser.
 *
 * Returns the ID of the given #InfUser.
 *
 * Return Value: A numerical User ID.
 **/
guint
inf_user_get_id(const InfUser* user)
{
  g_return_val_if_fail(INF_IS_USER(user), 0);
  return INF_USER_PRIVATE(user)->id;
}

/** inf_user_get_name:
 *
 * @user A #InfUser.
 *
 * Returns the name of the given #InfUser.
 *
 * Return Value: The user's name.
 **/
const gchar*
inf_user_get_name(const InfUser* user)
{
  g_return_val_if_fail(INF_IS_USER(user), NULL);
  return INF_USER_PRIVATE(user)->name;
}

/** inf_user_get_status:
 *
 * @user A #InfUser.
 *
 * Returns the status of the given #InfUser.
 *
 * Return Value: The user's status.
 **/
InfUserStatus
inf_user_get_status(const InfUser* user)
{
  g_return_val_if_fail(INF_IS_USER(user), INF_USER_UNAVAILABLE);
  return INF_USER_PRIVATE(user)->status;
}
