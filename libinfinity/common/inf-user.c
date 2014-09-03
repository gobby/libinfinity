/* libinfinity - a GObject-based infinote implementation
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
 * SECTION:inf-user
 * @title: InfUser
 * @short_description: User in a #InfSession.
 * @include: libinfinity/common/inf-user.h
 * @see_also: #InfSession
 * @stability: Unstable
 *
 * #InfUser represents a user in an #InfSession. The #InfUser object stores
 * basic user information required in all kinds of sessions, that is the user
 * ID, user name, its status and auxiliary flags.
 */

#include <libinfinity/common/inf-user.h>
#include <libinfinity/common/inf-error.h>
#include <libinfinity/inf-i18n.h>
#include <libinfinity/inf-marshal.h>
#include <libinfinity/inf-define-enum.h>

#include <string.h>

static const GFlagsValue inf_user_flags_values[] = {
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

static const GEnumValue inf_user_status_values[] = {
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

enum {
  SET_STATUS,

  LAST_SIGNAL
};

#define INF_USER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TYPE_USER, InfUserPrivate))
#define INF_USER_PRIVATE(obj)     ((InfUserPrivate*)(obj)->priv)

static guint user_signals[LAST_SIGNAL];

INF_DEFINE_FLAGS_TYPE(InfUserFlags, inf_user_flags, inf_user_flags_values)
INF_DEFINE_ENUM_TYPE(InfUserStatus, inf_user_status, inf_user_status_values)
G_DEFINE_TYPE_WITH_CODE(InfUser, inf_user, G_TYPE_OBJECT,
  G_ADD_PRIVATE(InfUser))

static void
inf_user_init(InfUser* user)
{
  InfUserPrivate* priv;

  user->priv = INF_USER_GET_PRIVATE(user);
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
  InfUserPrivate* priv;

  user = INF_USER(object);
  priv = INF_USER_PRIVATE(user);

  if(priv->connection != NULL)
  {
    g_object_unref(priv->connection);
    priv->connection = NULL;
  }

  G_OBJECT_CLASS(inf_user_parent_class)->dispose(object);
}

static void
inf_user_finalize(GObject* object)
{
  InfUser* user;
  InfUserPrivate* priv;

  user = INF_USER(object);
  priv = INF_USER_PRIVATE(user);

  g_free(priv->name);

  G_OBJECT_CLASS(inf_user_parent_class)->finalize(object);
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
    g_signal_emit(
      object,
      user_signals[SET_STATUS],
      0,
      g_value_get_enum(value)
    );

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
inf_user_set_status_handler(InfUser* user,
                            InfUserStatus status)
{
  InfUserPrivate* priv;
  priv = INF_USER_PRIVATE(user);

  priv->status = status;
}

static void
inf_user_class_init(InfUserClass* user_class)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(user_class);

  object_class->dispose = inf_user_dispose;
  object_class->finalize = inf_user_finalize;
  object_class->set_property = inf_user_set_property;
  object_class->get_property = inf_user_get_property;

  user_class->set_status = inf_user_set_status_handler;

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

  /**
   * InfUser::set-status:
   * @user: The #InfUser that changes status.
   * @status: The new user status.
   *
   * This signal is emitted whenever the user's status changes. This is
   * basically the same as a notification for the #InfUser:status property,
   * but it allows to access the previous user status when connecting before
   * the default signal handler.
   */
  user_signals[SET_STATUS] = g_signal_new(
    "set-status",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfUserClass, set_status),
    NULL, NULL,
    inf_marshal_VOID__ENUM,
    G_TYPE_NONE,
    1,
    INF_TYPE_USER_STATUS
  );
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
inf_user_get_id(InfUser* user)
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
inf_user_get_name(InfUser* user)
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
inf_user_get_status(InfUser* user)
{
  g_return_val_if_fail(INF_IS_USER(user), INF_USER_UNAVAILABLE);
  return INF_USER_PRIVATE(user)->status;
}

/**
 * inf_user_get_flags:
 * @user: A #InfUser.
 *
 * Returns the flags for the given #InfUser.
 *
 * Return Value: The user's flags.
 **/
InfUserFlags
inf_user_get_flags(InfUser* user)
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
 * %INF_USER_UNAVAILABLE).
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
      _("Invalid user status: '%s'"),
      string
    );

    return FALSE;
  }

  if(status) *status = tmp_status;
  return TRUE;
}

/* vim:set et sw=2 ts=2: */
