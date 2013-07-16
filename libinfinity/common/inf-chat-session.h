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
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __INF_CHAT_SESSION_H__
#define __INF_CHAT_SESSION_H__

#include <libinfinity/common/inf-chat-buffer.h>
#include <libinfinity/common/inf-session.h>
#include <libinfinity/common/inf-user.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_CHAT_SESSION                 (inf_chat_session_get_type())
#define INF_CHAT_SESSION(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_CHAT_SESSION, InfChatSession))
#define INF_CHAT_SESSION_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TYPE_CHAT_SESSION, InfChatSessionClass))
#define INF_IS_CHAT_SESSION(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_CHAT_SESSION))
#define INF_IS_CHAT_SESSION_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TYPE_CHAT_SESSION))
#define INF_CHAT_SESSION_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TYPE_CHAT_SESSION, InfChatSessionClass))

typedef struct _InfChatSession InfChatSession;
typedef struct _InfChatSessionClass InfChatSessionClass;

/**
 * InfChatSessionError:
 * @INF_CHAT_SESSION_ERROR_TYPE_INVALID: An invalid message type was sent.
 * @INF_CHAT_SESSION_ERROR_NO_SUCH_USER: A message referred to a nonexisting
 * user.
 * @INF_CHAT_SESSION_ERROR_FAILED: Generic error code when no further reason
 * of failure is known.
 *
 * Errors that can occur with a chat session, especially in the
 * process_xml_sync and process_xml_run vfunc implementations.
 */
typedef enum _InfChatSessionError {
  INF_CHAT_SESSION_ERROR_TYPE_INVALID,
  INF_CHAT_SESSION_ERROR_NO_SUCH_USER,

  INF_CHAT_SESSION_ERROR_FAILED
} InfChatSessionError;

/**
 * InfChatSessionClass:
 * @receive_message: Default signal handler for the
 * #InfChatSession::receive-message signal.
 * @send_message: Default signal handler for the
 * #InfChatSession::send-message signal.
 *
 * This structure contains default signal handlers for #InfChatSession.
 */
struct _InfChatSessionClass {
  /*< private >*/
  InfSessionClass parent_class;

  /*< public >*/
  void (*receive_message)(InfChatSession* session,
                          const InfChatBufferMessage* message);
  void (*send_message)(InfChatSession* session,
                       const InfChatBufferMessage* message);
};

/**
 * InfChatSession:
 *
 * #InfChatSession is an opaque data type. You should only access it via the
 * public API functions.
 */
struct _InfChatSession {
  /*< private >*/
  InfSession parent;
};

GType
inf_chat_session_get_type(void) G_GNUC_CONST;

InfChatSession*
inf_chat_session_new(InfCommunicationManager* manager,
                     guint backlog_size,
                     InfSessionStatus status,
                     InfCommunicationGroup* sync_group,
                     InfXmlConnection* sync_connection);

gboolean
inf_chat_session_set_log_file(InfChatSession* session,
                              const gchar* log_file,
                              GError** error);

G_END_DECLS

#endif /* __INF_CHAT_SESSION_H__ */

/* vim:set et sw=2 ts=2: */
