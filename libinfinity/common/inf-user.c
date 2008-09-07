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

#include <libinfinity/common/inf-user.h>
#include <libinfinity/common/inf-error.h>

#include <string.h>

typedef struct _InfUserPrivate InfUserPrivate;
struct _InfUserPrivate {
  guint id;
  gchar* name;
  InfUserStatus status;
  InfUserFlags flags;
  InfXmlConnection* connection;
};

enum {
  PROP_0,

  PROP_ID,
  PROP_NAME,
  PROP_STATUS,
  PROP_FLAGS,
  PROP_CONNECTION
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
  priv->flags = 0;
  priv->connection = NULL;
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
  
  /* TODO: Check if properties are still valid.
   * There are several combinations possible:
   *
   * Status  | Flags | Connection | Desc
   * UNAVAIL |   0   |   unset    | User not available, was non-local last time
   *  AVAIL  |   0   |   unset    | INVALID
   * UNAVAIL | LOCAL |   unset    | User not available, was local last time
   *  AVAIL  | LOCAL |   unset    | User is available, and local
   * UNAVAIL |   0   |    set     | INVALID
   *  AVAIL  |   0   |    set     | User is available, non-local
   * UNAVAIL | LOCAL |    set     | INVALID
   *  AVAIL  | LOCAL |    set     | INVALID */

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
  case PROP_FLAGS:
    priv->flags = g_value_get_flags(value);
    break;
  case PROP_CONNECTION:
    if(priv->connection != NULL) g_object_unref(priv->connection);
    priv->connection = INF_XML_CONNECTION(g_value_dup_object(value));
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
  case PROP_FLAGS:
    g_value_set_flags(value, priv->flags);
    break;
  case PROP_CONNECTION:
    g_value_set_object(value, G_OBJECT(priv->connection));
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

  g_object_class_install_property(
    object_class,
    PROP_FLAGS,
    g_param_spec_flags(
      "flags",
      "Flags",
      "Flags the user currently has",
      INF_TYPE_USER_FLAGS,
      0,
      G_PARAM_READWRITE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_CONNECTION,
    g_param_spec_object(
      "connection",
      "Connection",
      "Connection to the user",
      INF_TYPE_XML_CONNECTION,
      G_PARAM_READWRITE
    )
  );
}

GType
inf_user_flags_get_type(void)
{
  static GType user_flags_type = 0;

  if(!user_flags_type)
  {
    static const GFlagsValue user_flags_type_values[] = {
      {
        INF_USER_LOCAL,
        "INF_USER_LOCAL",
        "local"
      }, {
        0,
        NULL,
        NULL
      }
    };

    user_flags_type = g_flags_register_static(
      "InfUserFlags",
      user_flags_type_values
    );
  }

  return user_flags_type;
}

GType
inf_user_status_get_type(void)
{
  static GType user_status_type = 0;

  if(!user_status_type)
  {
    static const GEnumValue user_status_type_values[] = {
      {
        INF_USER_ACTIVE,
        "INF_USER_ACTIVE",
        "active"
      }, {
        INF_USER_INACTIVE,
        "INF_USER_INACTIVE",
        "inactive"
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

/**
 * inf_user_get_id:
 * @user: A #InfUser.
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

/**
 * inf_user_get_name:
 * @user: A #InfUser.
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

/**
 * inf_user_get_status:
 * @user: A #InfUser.
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

/**
 * inf_user_get_flags:
 * @user: A #InfUser.
 *
 * Returns the flags for the given #INfUser.
 *
 * Return Value: The user's flags.
 **/
InfUserFlags
inf_user_get_flags(const InfUser* user)
{
  g_return_val_if_fail(INF_IS_USER(user), 0);
  return INF_USER_PRIVATE(user)->flags;
}

/**
 * inf_user_get_connection:
 * @user: A #InfUser.
 *
 * Returns a connection to the given #InfUser, or %NULL. If a non-%NULL
 * connection is returned, then this is the connection through which records
 * from that user come from. This means that, when this connection is closed,
 * then the user is no longer available. However, you cannot send something
 * to this connection expecting the user will receive it. For example,
 * in central messaging mode, this connection is always the publisher, because
 * all records from the user are relayed via the publisher.
 *
 * If this functions returns %NULL, this either means @user is a local user
 * (%INF_USER_LOCAL flag set) or it is not available (status is
 * %INF_USER_UNAVAILBALE).
 *
 * Return Value: A #InfXmlConnection, or %NULL.
 **/
InfXmlConnection*
inf_user_get_connection(InfUser* user)
{
  g_return_val_if_fail(INF_IS_USER(user), NULL);
  return INF_USER_PRIVATE(user)->connection;
}

/**
 * inf_user_status_to_string:
 * @status: A value from the #InfUserStatus enumeration.
 *
 * Returns a non-localized string identifying the given status. This is not
 * meant to be shown to a user, but rather to serialize a user status, for
 * example to store it in XML.
 *
 * Returns: A static string representation of @status.
 */
const gchar*
inf_user_status_to_string(InfUserStatus status)
{
  switch(status)
  {
  case INF_USER_ACTIVE: return "active";
  case INF_USER_INACTIVE: return "inactive";
  case INF_USER_UNAVAILABLE: return "unavailable";
  default: g_assert_not_reached();
  }
}

/**
 * inf_user_status_from_string:
 * @string: A string representation of a #InfUserStatus.
 * @status: A pointer to a #InfUserStatus value, or %NULL.
 * @error: Location to store error information, if any.
 *
 * This function does the opposite of inf_user_status_to_string(). It turns
 * the given string back to a #InfUserStatus, storing the result in @status
 * if @status is non-%NULL. If @string is invalid, then @status is left
 * untouched, @error is set and %FALSE is returned. Otherwise, the function
 * returns %TRUE.
 *
 * Returns: When an error occured during the conversion, %FALSE is returned,
 * and %TRUE otherwise.
 */
gboolean
inf_user_status_from_string(const gchar* string,
                            InfUserStatus* status,
                            GError** error)
{
  InfUserStatus tmp_status;

  if(strcmp(string, "active") == 0)
    tmp_status = INF_USER_ACTIVE;
  else if(strcmp(string, "inactive") == 0)
    tmp_status = INF_USER_INACTIVE;
  else if(strcmp(string, "unavailable") == 0)
    tmp_status = INF_USER_UNAVAILABLE;
  else
  {
    g_set_error(
      error,
      inf_user_error_quark(),
      INF_USER_ERROR_INVALID_STATUS,
      "Invalid user status: '%s'",
      string
    );

    return FALSE;
  }

  if(status) *status = tmp_status;
  return TRUE;
}

/* vim:set et sw=2 ts=2: */
