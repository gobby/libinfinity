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

#include <libinftext/inf-text-user.h>
#include <libinfinity/inf-marshal.h>

typedef struct _InfTextUserPrivate InfTextUserPrivate;
struct _InfTextUserPrivate {
  guint caret;
  gint selection;
};

enum {
  PROP_0,

  PROP_CARET,
  PROP_SELECTION
};

enum {
  SELECTION_CHANGED,

  LAST_SIGNAL
};

#define INF_TEXT_USER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TEXT_TYPE_USER, InfTextUserPrivate))

static InfUserClass* parent_class;
static guint user_signals[LAST_SIGNAL] = { 0 };

static void
inf_text_user_init(GTypeInstance* instance,
                   gpointer g_class)
{
  InfTextUser* user;
  InfTextUserPrivate* priv;

  user = INF_TEXT_USER(instance);
  priv = INF_TEXT_USER_PRIVATE(user);

  priv->caret = 0;
  priv->selection = 0;
}

static void
inf_text_user_set_property(GObject* object,
                           guint prop_id,
                           const GValue* value,
                           GParamSpec* pspec)
{
  InfTextUser* user;
  InfTextUserPrivate* priv;

  user = INF_TEXT_USER(object);
  priv = INF_TEXT_USER_PRIVATE(user);

  switch(prop_id)
  {
  case PROP_CARET:
    priv->caret = g_value_get_uint(value);
    break;
  case PROP_SELECTION:
    priv->selection = g_value_get_int(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_text_user_get_property(GObject* object,
                           guint prop_id,
                           GValue* value,
                           GParamSpec* pspec)
{
  InfTextUser* user;
  InfTextUserPrivate* priv;

  user = INF_TEXT_USER(object);
  priv = INF_TEXT_USER_PRIVATE(user);

  switch(prop_id)
  {
  case PROP_CARET:
    g_value_set_uint(value, priv->caret);
    break;
  case PROP_SELECTION:
    g_value_set_int(value, priv->selection);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_text_user_selection_changed(InfTextUser* user,
                                guint position,
                                guint length)
{
  InfTextUserPrivate* priv;
  priv = INF_TEXT_USER_PRIVATE(user);

  priv->caret = position;
  priv->selection = length;

  g_object_notify(G_OBJECT(user), "caret-position");
  g_object_notify(G_OBJECT(user), "selection-length");
}

static void
inf_text_user_class_init(gpointer g_class,
                         gpointer class_data)
{
  GObjectClass* object_class;
  InfTextUserClass* user_class;

  object_class = G_OBJECT_CLASS(g_class);
  user_class = INF_TEXT_USER_CLASS(g_class);

  parent_class = INF_USER_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfTextUserPrivate));

  object_class->set_property = inf_text_user_set_property;
  object_class->get_property = inf_text_user_get_property;

  user_class->selection_changed = inf_text_user_selection_changed;

  g_object_class_install_property(
    object_class,
    PROP_CARET,
    g_param_spec_uint(
      "caret-position",
      "Caret position",
      "The position of this user's caret",
      0,
      G_MAXUINT,
      0,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_SELECTION,
    g_param_spec_uint(
      "selection-length",
      "Selection length",
      "The number of characters of this user's selection",
      G_MININT,
      G_MAXINT,
      0,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  user_signals[SELECTION_CHANGED] = g_signal_new(
    "selection-changed",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfTextUserClass, selection_changed),
    NULL, NULL,
    inf_marshal_VOID__UINT_INT,
    G_TYPE_NONE,
    2,
    G_TYPE_UINT,
    G_TYPE_INT
  );
}

GType
inf_text_user_get_type(void)
{
  static GType user_type = 0;

  if(!user_type)
  {
    static const GTypeInfo user_type_info = {
      sizeof(InfTextUserClass),  /* class_size */
      NULL,                      /* base_init */
      NULL,                      /* base_finalize */
      inf_text_user_class_init,  /* class_init */
      NULL,                      /* class_finalize */
      NULL,                      /* class_data */
      sizeof(InfTextUser),       /* instance_size */
      0,                         /* n_preallocs */
      inf_text_user_init,        /* instance_init */
      NULL                       /* value_table */
    };

    user_type = g_type_register_static(
      INF_TYPE_USER,
      "InfTextUser",
      &user_type_info,
      0
    );
  }

  return user_type;
}

/** inf_text_user_get_caret_position:
 *
 * @user: A #InfTextUser.
 *
 * Returns the position of @user's caret.
 *
 * Return Value: @user's caret position.
 **/
guint
inf_text_user_get_caret_position(InfTextUser* user)
{
  g_return_val_if_fail(INF_TEXT_IS_USER(user), 0);
  return INF_TEXT_USER_PRIVATE(user)->caret;
}

/** inf_text_user_get_selection_length:
 *
 * @user: A #InfTextUser.
 *
 * Returns the number of characters this user has selected, starting from
 * the caret position. Negative number mean selection towards the beginning
 * of the buffer.
 *
 * Return Value: @user's selection length in characters.
 **/
gint
inf_text_user_get_selection_length(InfTextUser* user)
{
  g_return_val_if_fail(INF_TEXT_IS_USER(user), 0);
  return INF_TEXT_USER_PRIVATE(user)->selection;
}

/** inf_text_user_set_selection:
 *
 * @user: A #InfTextUser.
 * @position: The new position for the user's caret.
 * @length: The number of characters to select. Negative numbers mean
 * selection towards the beginning.
 *
 * Changes @user's selection (i.e. caret position and selection length).
 **/
void
inf_text_user_set_selection(InfTextUser* user,
                            guint position,
                            gint length)
{
  g_return_if_fail(INF_TEXT_IS_USER(user));
  g_signal_emit(
    G_OBJECT(user),
    user_signals[SELECTION_CHANGED],
    0,
    position,
    length
  );
}

/* vim:set et sw=2 ts=2: */
