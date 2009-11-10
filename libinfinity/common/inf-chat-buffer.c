/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008, 2009 Armin Burgmeier <armin@arbur.net>
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

/**
 * SECTION:inf-chat-buffer
 * @title: InfChatBuffer
 * @short_description: A ring buffer for chat messages
 * @include: libinfinity/common/inf-chat-buffer.h
 * @stability: Unstable
 *
 * #InfChatBuffer contains the chat messages for a #InfChatSession.
 **/

#include <libinfinity/common/inf-chat-buffer.h>

#include <libinfinity/inf-i18n.h>
#include <libinfinity/inf-marshal.h>

#include <string.h>

typedef struct _InfChatBufferPrivate InfChatBufferPrivate;
struct _InfChatBufferPrivate {
  InfChatBufferMessage* messages;
  guint alloc_messages;
  guint num_messages;
  guint first_message;

  /* Total size of buffer */
  guint size;

  gboolean modified;
};

enum {
  PROP_0,

  /* construct only */
  PROP_SIZE,

  /* Read/write */
  PROP_MODIFIED
};

enum {
  ADD_MESSAGE,

  LAST_SIGNAL
};

#define INF_CHAT_BUFFER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TYPE_CHAT_BUFFER, InfChatBufferPrivate))

static GObjectClass* parent_class;
static guint chat_buffer_signals[LAST_SIGNAL];

/*
 * Message handling functions
 */

/* Allocate a new InfChatMessage for the given time, possibly removing an
 * old one if the buffer is full. Messages are ordered according to their time,
 * if there are multiple messages with the time, the new message is inserted
 * after the messages with the same time. */
/* The function returns NULL if the new message is older than all other
 * messages in the buffer. Make sure to initalize all fields of the new
 * message to sensible values after having called this function. */
static InfChatBufferMessage*
inf_chat_buffer_reserve_message(InfChatBuffer* buffer,
                                time_t time)
{
  InfChatBufferPrivate* priv;
  InfChatBufferMessage* message;
  guint begin;
  guint end;
  guint n;

  priv = INF_CHAT_BUFFER_PRIVATE(buffer);

  begin = 0;
  end = priv->num_messages;

  /* Find the place at which to insert the new message */
  while(begin != end)
  {
    n = (begin + end) / 2;
    message = &priv->messages[(priv->first_message + n) % priv->size];
    if(message->time <= time)
      begin = (begin + end + 1)/2;
    else
      end = (begin + end)/2;
  } while(begin != end);

  n = begin;

  /* Can't insert at the beginning if there is no more space in the buffer */
  if(n == 0 && priv->num_messages == priv->size)
    return NULL;

  if(priv->num_messages < priv->size)
  {
    /* We have buffer space available, so we don't need to drop an existing
     * message. */
 
    /* We rely on the messages not wrapping around yet when the buffer is
     * not yet full. */
    g_assert(priv->first_message + priv->num_messages <= priv->alloc_messages);

    if(priv->num_messages == priv->alloc_messages)
    {
      /* We need to allocate more space */
      priv->alloc_messages = MAX(priv->alloc_messages * 2, 16);
      priv->alloc_messages = MIN(priv->alloc_messages, priv->size);

      g_assert(priv->alloc_messages > priv->num_messages);

      priv->messages = g_realloc(
        priv->messages,
        priv->alloc_messages * sizeof(InfChatBufferMessage)
      );
    }

    if(n == 0 && priv->first_message == 0)
    {
      /* The new message is the first one, but there is no space at the
       * beginning of the array, so we need to shift the other messages */
      end = (priv->alloc_messages - priv->num_messages + 1) / 2;
      g_assert(end > 0);

      memmove(
        priv->messages + end,
        priv->messages /* + priv->first_message */,
        priv->num_messages * sizeof(InfChatBufferMessage)
      );

      priv->first_message = end;
    }
    else if(n == priv->num_messages &&
            priv->first_message + priv->num_messages == priv->alloc_messages)
    {
      /* The new message is the last one, but there is no space at the end of
       * the array, so we need to shift the other messages */
      end = (priv->alloc_messages - priv->num_messages) / 2;
      g_assert(end + priv->num_messages < priv->alloc_messages);

      memmove(
        priv->messages + end,
        priv->messages + priv->first_message,
        priv->num_messages * sizeof(InfChatBufferMessage)
      );

      priv->first_message = end;
    }
    else if(n > 0 && n < priv->num_messages)
    {
      /* The new message is inserted in the middle, so we need to shift either
       * the messages before or the ones after, depending on where we have
       * space available. */
      if((n < priv->num_messages / 2 &&
          priv->first_message > 0) ||
         (n > priv->num_messages / 2 &&
          priv->first_message + priv->num_messages == priv->alloc_messages))
      {
        begin = priv->first_message;

        memmove(
          priv->messages + begin - 1,
          priv->messages + begin,
          (n + 1) * sizeof(InfChatBufferMessage)
        );

        --priv->first_message;
      }
      else
      {
        memmove(
          priv->messages + n + 1,
          priv->messages + n,
          (priv->num_messages - n) * sizeof(InfChatBufferMessage)
        );
      }
    }

    ++ priv->num_messages;
  }
  else
  {
    /* The buffer is full. This means we need to remove the oldest message */
    g_assert(n > 0); /* we have catched this before */

    begin = priv->first_message;
    end = (priv->first_message + n) % priv->size;

    if(n == priv->num_messages)
    {
      priv->first_message = (priv->first_message + 1) % priv->size;
      g_free(message->text);
    }
    else
    {
      /* Clear the oldest message */
      g_free(priv->messages[end].text);

      if(begin < end)
      {
        memmove(
          priv->messages + begin + 1,
          priv->messages + begin,
          (end - begin) * sizeof(InfChatBufferMessage)
        );
      }
      else
      {
        memmove(
          priv->messages + end + 1,
          priv->messages + end,
          (begin - end) * sizeof(InfChatBufferMessage)
        );

        priv->first_message = (priv->first_message + 1) % priv->size;
      }
    }
  }

  return &priv->messages[(priv->first_message + n) % priv->size];
}

/*
 * GObject overrides
 */

static void
inf_chat_buffer_init(GTypeInstance* instance,
                     gpointer g_class)
{
  InfChatBuffer* buffer;
  InfChatBufferPrivate* priv;

  buffer = INF_CHAT_BUFFER(instance);
  priv = INF_CHAT_BUFFER_PRIVATE(buffer);

  priv->messages = NULL;
  priv->alloc_messages = 0;
  priv->num_messages = 0;
  priv->first_message = 0;
  priv->size = 256;
  priv->modified = FALSE;
}

static void
inf_chat_buffer_finalize(GObject* object)
{
  InfChatBuffer* buffer;
  InfChatBufferPrivate* priv;
  guint i;

  buffer = INF_CHAT_BUFFER(object);
  priv = INF_CHAT_BUFFER_PRIVATE(buffer);

  /* Note that the messages array is not necessarily filled from its
   * beginning - we might have preallocated some space for prepending
   * entries. */
  for(i = 0; i < priv->num_messages; ++i)
    g_free(priv->messages[(priv->first_message + i) % priv->size].text);
  g_free(priv->messages);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_chat_buffer_set_property(GObject* object,
                             guint prop_id,
                             const GValue* value,
                             GParamSpec* pspec)
{
  InfChatBuffer* session;
  InfChatBufferPrivate* priv;

  session = INF_CHAT_BUFFER(object);
  priv = INF_CHAT_BUFFER_PRIVATE(session);

  switch(prop_id)
  {
  case PROP_SIZE:
    g_assert(priv->num_messages == 0); /* construct only */
    priv->size = g_value_get_uint(value);
    break;
  case PROP_MODIFIED:
    priv->modified = g_value_get_boolean(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(value, prop_id, pspec);
    break;
  }
}

static void
inf_chat_buffer_get_property(GObject* object,
                             guint prop_id,
                             GValue* value,
                             GParamSpec* pspec)
{
  InfChatBuffer* session;
  InfChatBufferPrivate* priv;

  session = INF_CHAT_BUFFER(object);
  priv = INF_CHAT_BUFFER_PRIVATE(session);

  switch(prop_id)
  {
  case PROP_SIZE:
    g_value_set_uint(value, priv->size);
    break;
  case PROP_MODIFIED:
    g_value_set_boolean(value, priv->modified);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

/*
 * Default signal handlers
 */

static void
inf_chat_buffer_add_message_handler(InfChatBuffer* buffer,
                                    const InfChatBufferMessage* message)
{
  InfChatBufferPrivate* priv;
  InfChatBufferMessage* new_message;

  priv = INF_CHAT_BUFFER_PRIVATE(buffer);

  new_message = inf_chat_buffer_reserve_message(buffer, message->time);

  /* new_message can be NULL if the buffer is already full, and the new
   * message is older than all existing messages. */
  if(new_message != NULL)
  {
    new_message->type = message->type;
    new_message->user = message->user;
    new_message->text = g_strndup(message->text, message->length);
    new_message->length = message->length;
    new_message->time = message->time;
  }
}

/*
 * InfBuffer implementation
 */

static gboolean
inf_chat_buffer_buffer_get_modified(InfBuffer* buffer)
{
  return INF_CHAT_BUFFER_PRIVATE(buffer)->modified;
}

static void
inf_chat_buffer_buffer_set_modified(InfBuffer* buffer,
                                    gboolean modified)
{
  InfChatBuffer* chat_buffer;
  InfChatBufferPrivate* priv;

  chat_buffer = INF_CHAT_BUFFER(buffer);
  priv = INF_CHAT_BUFFER_PRIVATE(chat_buffer);

  if(priv->modified != modified)
  {
    priv->modified = modified;
    g_object_notify(G_OBJECT(buffer), "modified");
  }
}

/*
 * GType registration
 */

static void
inf_chat_buffer_class_init(gpointer g_class,
                            gpointer class_data)
{
  GObjectClass* object_class;
  InfChatBufferClass* buffer_class;

  object_class = G_OBJECT_CLASS(g_class);
  buffer_class = INF_CHAT_BUFFER_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfChatBufferPrivate));

  object_class->finalize = inf_chat_buffer_finalize;
  object_class->set_property = inf_chat_buffer_set_property;
  object_class->get_property = inf_chat_buffer_get_property;
  
  buffer_class->add_message = inf_chat_buffer_add_message_handler;

  g_object_class_install_property(
    object_class,
    PROP_SIZE,
    g_param_spec_uint(
      "size",
      "size",
      "The maxmimum number of messages saved",
      0,
      G_MAXUINT,
      256,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_override_property(object_class, PROP_MODIFIED, "modified");

  /**
   * InfChatBuffer::add-message:
   * @buffer: The #InfChatBuffer that is receiving a message.
   * @message: The #InfChatBufferMessage that was received.
   *
   * This signal is emitted whenever a message has been added to @buffer.
   */
  chat_buffer_signals[ADD_MESSAGE] = g_signal_new(
    "add-message",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfChatBufferClass, add_message),
    NULL, NULL,
    inf_marshal_VOID__BOXED,
    G_TYPE_NONE,
    1,
    INF_TYPE_CHAT_BUFFER_MESSAGE | G_SIGNAL_TYPE_STATIC_SCOPE
  );
}

GType
inf_chat_buffer_message_type_get_type(void)
{
  static GType chat_buffer_message_type_type = 0;

  if(!chat_buffer_message_type_type)
  {
    static const GEnumValue chat_buffer_message_type_type_values[] = {
      {
        INF_CHAT_BUFFER_MESSAGE_NORMAL,
        "INF_CHAT_BUFFER_MESSAGE_NORMAL",
        "normal"
      }, {
        INF_CHAT_BUFFER_MESSAGE_EMOTE,
        "INF_CHAT_BUFFER_MESSAGE_EMOTE",
        "emote"
      }, {
        INF_CHAT_BUFFER_MESSAGE_USERJOIN,
        "INF_CHAT_BUFFER_MESSAGE_USERJOIN",
        "userjoin"
      }, {
        INF_CHAT_BUFFER_MESSAGE_USERPART,
        "INF_CHAT_BUFFER_MESSAGE_USERPART",
        "userpart"
      }, {
        0,
        NULL,
        NULL
      }
    };

    chat_buffer_message_type_type = g_enum_register_static(
      "InfChatBufferMessageType",
      chat_buffer_message_type_type_values
    );
  }

  return chat_buffer_message_type_type;
}

GType
inf_chat_buffer_message_get_type(void)
{
  static GType chat_buffer_message_type = 0;

  if(!chat_buffer_message_type)
  {
    chat_buffer_message_type = g_boxed_type_register_static(
      "InfChatBufferMessage",
      (GBoxedCopyFunc)inf_chat_buffer_message_copy,
      (GBoxedFreeFunc)inf_chat_buffer_message_free
    );
  }

  return chat_buffer_message_type;
}

static void
inf_chat_buffer_buffer_init(gpointer g_iface,
                            gpointer iface_data)
{
  InfBufferIface* iface;
  iface = (InfBufferIface*)g_iface;

  iface->get_modified = inf_chat_buffer_buffer_get_modified;
  iface->set_modified = inf_chat_buffer_buffer_set_modified;
}

GType
inf_chat_buffer_get_type(void)
{
  static GType chat_buffer_type = 0;

  if(!chat_buffer_type)
  {
    static const GTypeInfo chat_buffer_type_info = {
      sizeof(InfChatBufferClass),  /* class_size */
      NULL,                        /* base_init */
      NULL,                        /* base_finalize */
      inf_chat_buffer_class_init,  /* class_init */
      NULL,                        /* class_finalize */
      NULL,                        /* class_data */
      sizeof(InfChatBuffer),       /* instance_size */
      0,                           /* n_preallocs */
      inf_chat_buffer_init,        /* instance_init */
      NULL                         /* value_table */
    };

    static const GInterfaceInfo buffer_info = {
      inf_chat_buffer_buffer_init,
      NULL,
      NULL
    };

    chat_buffer_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfChatBuffer",
      &chat_buffer_type_info,
      0
    );

    g_type_add_interface_static(
      chat_buffer_type,
      INF_TYPE_BUFFER,
      &buffer_info
    );
  }

  return chat_buffer_type;
}

/*
 * Public API
 */

/**
 * inf_chat_buffer_message_copy:
 * @message: The #InfChatBufferMessage to copy.
 *
 * Creates a copy of the given message.
 *
 * Returns: A new #InfChatBufferMessage. Free with
 * inf_chat_buffer_message_free() when no longer needed.
 */
InfChatBufferMessage*
inf_chat_buffer_message_copy(const InfChatBufferMessage* message)
{
  InfChatBufferMessage* new_message;

  g_return_val_if_fail(message != NULL, NULL);

  new_message = g_slice_new(InfChatBufferMessage);

  new_message->type = message->type;
  new_message->user = message->user;
  new_message->text = g_strndup(message->text, message->length);
  new_message->length = message->length;
  new_message->time = message->time;

  return new_message;
}

/**
 * inf_chat_buffer_message_free:
 * @message: A #InfChatBufferMessage.
 *
 * Frees the given #InfChatBufferMessage which must have been created with
 * inf_chat_buffer_message_copy().
 */
void
inf_chat_buffer_message_free(InfChatBufferMessage* message)
{
  g_return_if_fail(message != NULL);

  g_free(message->text);
  g_slice_free(InfChatBufferMessage, message);
}

/**
 * inf_chat_buffer_new:
 * @size: The number of messages to store.
 *
 * Creates a new #InfChatBuffer which contains no initial messages. @size
 * specifies how many messages to store before dropping old messages.
 *
 * Returns: A new #InfChatBuffer.
 */
InfChatBuffer*
inf_chat_buffer_new(guint size)
{
  return g_object_new(
    INF_TYPE_CHAT_BUFFER,
    "size", size,
    NULL
  );
}

/**
 * inf_chat_buffer_add_message:
 * @buffer: A #InfChatBuffer.
 * @by: A #InfUser who wrote the message.
 * @message: The message text.
 * @length: The length of @message, in bytes.
 * @time: The time at which the user has written the message.
 *
 * Adds a new message to the chat buffer. If the buffer is full (meaning the
 * number of messages in the buffer equals its size), then an old message will
 * get discarded. If the message to be added is older than all other messages
 * in the buffer, then it will not be added at all.
 */
void
inf_chat_buffer_add_message(InfChatBuffer* buffer,
                            InfUser* by,
                            const gchar* message,
                            gsize length,
                            time_t time)
{
  InfChatBufferMessage msg;

  g_return_if_fail(INF_IS_CHAT_BUFFER(buffer));
  g_return_if_fail(INF_IS_USER(by));
  g_return_if_fail(message != NULL);

  msg.type = INF_CHAT_BUFFER_MESSAGE_NORMAL;
  msg.user = by;
  msg.text = (gchar*)message;
  msg.length = length;
  msg.time = time;

  g_signal_emit(buffer, chat_buffer_signals[ADD_MESSAGE], 0, &msg);
}

/**
 * inf_chat_buffer_add_emote_message:
 * @buffer: A #InfChatBuffer.
 * @by: A #InfUser who wrote the message.
 * @message: The message text.
 * @length: The length of @message, in bytes.
 * @time: The time at which the user has written the message.
 *
 * Adds a new emote message to the chat buffer. If the buffer is full
 * (meaning the number of messages in the buffer equals its size), then an
 * old message will get discarded. If the message to be added is older than
 * all other messages in the buffer, then it will not be added at all.
 */
void
inf_chat_buffer_add_emote_message(InfChatBuffer* buffer,
                                  InfUser* by,
                                  const gchar* message,
                                  gsize length,
                                  time_t time)
{
  InfChatBufferMessage msg;

  g_return_if_fail(INF_IS_CHAT_BUFFER(buffer));
  g_return_if_fail(INF_IS_USER(by));
  g_return_if_fail(message != NULL);

  msg.type = INF_CHAT_BUFFER_MESSAGE_EMOTE;
  msg.user = by;
  msg.text = (gchar*)message;
  msg.length = length;
  msg.time = time;

  g_signal_emit(buffer, chat_buffer_signals[ADD_MESSAGE], 0, &msg);
}

/**
 * inf_chat_buffer_add_userjoin_message:
 * @buffer: A #InfChatBuffer.
 * @user: A #InfUser who wrote the message.
 * @time: The time at which the user has written the message.
 *
 * Adds a new userjoin message to the chat buffer. If the buffer is full
 * (meaning the number of messages in the buffer equals its size), then an
 * old message will get discarded. If the message to be added is older than
 * all other messages in the buffer, then it will not be added at all.
 */
void
inf_chat_buffer_add_userjoin_message(InfChatBuffer* buffer,
                                     InfUser* user,
                                     time_t time)
{
  InfChatBufferMessage msg;

  g_return_if_fail(INF_IS_CHAT_BUFFER(buffer));
  g_return_if_fail(INF_IS_USER(user));

  msg.type = INF_CHAT_BUFFER_MESSAGE_USERJOIN;
  msg.user = user;
  msg.text = NULL;
  msg.length = 0;
  msg.time = time;

  g_signal_emit(buffer, chat_buffer_signals[ADD_MESSAGE], 0, &msg);
}

/**
 * inf_chat_buffer_add_userpart_message:
 * @buffer: A #InfChatBuffer.
 * @user: A #InfUser who wrote the message.
 * @time: The time at which the user has written the message.
 *
 * Adds a new userpart message to the chat buffer. If the buffer is full
 * (meaning the number of messages in the buffer equals its size), then an
 * old message will get discarded. If the message to be added is older than
 * all other messages in the buffer, then it will not be added at all.
 */
void
inf_chat_buffer_add_userpart_message(InfChatBuffer* buffer,
                                     InfUser* user,
                                     time_t time)
{
  InfChatBufferMessage msg;

  g_return_if_fail(INF_IS_CHAT_BUFFER(buffer));
  g_return_if_fail(INF_IS_USER(user));

  msg.type = INF_CHAT_BUFFER_MESSAGE_USERPART;
  msg.user = user;
  msg.text = NULL;
  msg.length = 0;
  msg.time = time;

  g_signal_emit(buffer, chat_buffer_signals[ADD_MESSAGE], 0, &msg);
}

/**
 * inf_chat_buffer_get_message:
 * @buffer: A #InfChatBuffer.
 * @n: The index of the message to obtain.
 *
 * Returns the message with the given index from the buffer. The oldest
 * message in the buffer has index 0, and the most recent one has index
 * inf_chat_buffer_get_n_messages() - 1.
 *
 * Returns: The #InfChatBufferMessage with the given index.
 */
const InfChatBufferMessage*
inf_chat_buffer_get_message(InfChatBuffer* buffer,
                            guint n)
{
  InfChatBufferPrivate* priv;

  g_return_val_if_fail(INF_IS_CHAT_BUFFER(buffer), NULL);
  g_return_val_if_fail(n < inf_chat_buffer_get_n_messages(buffer), NULL);

  priv = INF_CHAT_BUFFER_PRIVATE(buffer);
  return &priv->messages[
    (priv->first_message + priv->num_messages - 1 - n) % priv->size
  ];
}

/**
 * inf_chat_buffer_get_n_messages:
 * @buffer: A #InfChatBuffer.
 *
 * Returns the number of messages in the buffer.
 *
 * Returns: The number of messages in the buffer.
 */
guint
inf_chat_buffer_get_n_messages(InfChatBuffer* buffer)
{
  g_return_val_if_fail(INF_IS_CHAT_BUFFER(buffer), 0);
  return INF_CHAT_BUFFER_PRIVATE(buffer)->num_messages;
}

/**
 * inf_chat_buffer_get_size:
 * @buffer: A #InfChatBuffer.
 *
 * Returns the size of the chat buffer, which is the maximum number of
 * messages that can be stored in the buffer.
 *
 * Returns: The number of messages in the chat buffer.
 */
guint
inf_chat_buffer_get_size(InfChatBuffer* buffer)
{
  g_return_val_if_fail(INF_IS_CHAT_BUFFER(buffer), 0);
  return INF_CHAT_BUFFER_PRIVATE(buffer)->size;
}

/* vim:set et sw=2 ts=2: */
