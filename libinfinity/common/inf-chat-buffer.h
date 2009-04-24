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

#ifndef __INF_CHAT_BUFFER_H__
#define __INF_CHAT_BUFFER_H__

#include <libinfinity/common/inf-buffer.h>
#include <libinfinity/common/inf-user.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_CHAT_BUFFER                 (inf_chat_buffer_get_type())
#define INF_CHAT_BUFFER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_CHAT_BUFFER, InfChatBuffer))
#define INF_CHAT_BUFFER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TYPE_CHAT_BUFFER, InfChatBufferClass))
#define INF_IS_CHAT_BUFFER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_CHAT_BUFFER))
#define INF_IS_CHAT_BUFFER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TYPE_CHAT_BUFFER))
#define INF_CHAT_BUFFER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TYPE_CHAT_BUFFER, InfChatBufferClass))

#define INF_TYPE_CHAT_BUFFER_MESSAGE_TYPE    (inf_chat_buffer_message_type_get_type())
#define INF_TYPE_CHAT_BUFFER_MESSAGE         (inf_chat_buffer_message_get_type())

typedef struct _InfChatBuffer InfChatBuffer;
typedef struct _InfChatBufferClass InfChatBufferClass;

typedef struct _InfChatBufferMessage InfChatBufferMessage;

/**
 * InfChatBufferMessageType:
 * @INF_CHAT_BUFFER_MESSAGE_NORMAL: A normal chat message.
 * @INF_CHAT_BUFFER_MESSAGE_EMOTE: An emote chat message (/me is doing
 * something).
 * @INF_CHAT_BUFFER_MESSAGE_USERJOIN: A user join notification.
 * @INF_CHAT_BUFFER_MESSAGE_USERPART: A user part notification.
 *
 * Possible chat message types.
 */
typedef enum _InfChatBufferMessageType {
  INF_CHAT_BUFFER_MESSAGE_NORMAL,
  INF_CHAT_BUFFER_MESSAGE_EMOTE,
  INF_CHAT_BUFFER_MESSAGE_USERJOIN,
  INF_CHAT_BUFFER_MESSAGE_USERPART
} InfChatBufferMessageType;

/**
 * InfChatBufferMessage:
 * @type: The #InfChatBufferMessageType of the message.
 * @user: The #InfUser that issued the message.
 * @text: The UTF-8 encoded text of the message.
 * @length: The length of the message, in bytes.
 * @time: The time at which the message was received.
 *
 * Represents a chat message.
 */
struct _InfChatBufferMessage {
  InfChatBufferMessageType type;
  InfUser* user;
  gchar* text;
  gsize length;
  time_t time;
};

/**
 * InfChatBufferClass:
 * @add_message: Default signal handler for the #InfChatBuffer::add-message
 * signal.
 *
 * This structure contains default signal handlers for #InfChatBuffer.
 */
struct _InfChatBufferClass {
  /*< private >*/
  GObjectClass parent_class;

  /*< public >*/
  void (*add_message)(InfChatBuffer* buffer,
                      const InfChatBufferMessage* message);
};

/**
 * InfChatBuffer:
 *
 * #InfChatBuffer is an opaque data type. You should only access it via the
 * public API functions.
 */
struct _InfChatBuffer {
  /*< private >*/
  GObject parent;
};

GType
inf_chat_buffer_message_get_type(void) G_GNUC_CONST;

GType
inf_chat_buffer_message_type_get_type(void) G_GNUC_CONST;

GType
inf_chat_buffer_get_type(void) G_GNUC_CONST;

InfChatBufferMessage*
inf_chat_buffer_message_copy(const InfChatBufferMessage* message);

void
inf_chat_buffer_message_free(InfChatBufferMessage* message);

InfChatBuffer*
inf_chat_buffer_new(guint size);

void
inf_chat_buffer_add_message(InfChatBuffer* buffer,
                            InfUser* by,
                            const gchar* message,
                            gsize length,
                            time_t time);

void
inf_chat_buffer_add_emote_message(InfChatBuffer* buffer,
                                  InfUser* by,
                                  const gchar* message,
                                  gsize length,
                                  time_t time);

void
inf_chat_buffer_add_userjoin_message(InfChatBuffer* buffer,
                                     InfUser* user,
                                     time_t time);

void
inf_chat_buffer_add_userpart_message(InfChatBuffer* buffer,
                                     InfUser* user,
                                     time_t time);

const InfChatBufferMessage*
inf_chat_buffer_get_message(InfChatBuffer* buffer,
                            guint n);

guint
inf_chat_buffer_get_n_messages(InfChatBuffer* buffer);

guint
inf_chat_buffer_get_size(InfChatBuffer* buffer);

G_END_DECLS

#endif /* __INF_CHAT_BUFFER_H__ */

/* vim:set et sw=2 ts=2: */
