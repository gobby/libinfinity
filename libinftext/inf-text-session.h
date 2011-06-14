/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2010 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INF_TEXT_SESSION_H__
#define __INF_TEXT_SESSION_H__

#include <libinftext/inf-text-session.h>
#include <libinftext/inf-text-buffer.h>
#include <libinftext/inf-text-user.h>
#include <libinfinity/adopted/inf-adopted-session.h>
#include <libinfinity/communication/inf-communication-manager.h>
#include <libinfinity/client/infc-user-request.h>
#include <libinfinity/client/infc-session-proxy.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TEXT_TYPE_SESSION                 (inf_text_session_get_type())
#define INF_TEXT_SESSION(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TEXT_TYPE_SESSION, InfTextSession))
#define INF_TEXT_SESSION_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TEXT_TYPE_SESSION, InfTextSessionClass))
#define INF_TEXT_IS_SESSION(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TEXT_TYPE_SESSION))
#define INF_TEXT_IS_SESSION_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TEXT_TYPE_SESSION))
#define INF_TEXT_SESSION_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TEXT_TYPE_SESSION, InfTextSessionClass))

typedef struct _InfTextSession InfTextSession;
typedef struct _InfTextSessionClass InfTextSessionClass;

typedef enum _InfTextSessionError {
  INF_TEXT_SESSION_ERROR_INVALID_HUE,

  INF_TEXT_SESSION_ERROR_FAILED
} InfTextSessionError;

struct _InfTextSessionClass {
  InfAdoptedSessionClass parent_class;
};

struct _InfTextSession {
  InfAdoptedSession parent;
};

GType
inf_text_session_get_type(void);

InfTextSession*
inf_text_session_new(InfCommunicationManager* manager,
                     InfTextBuffer* buffer,
                     InfIo* io,
                     InfSessionStatus status,
                     InfCommunicationGroup* sync_group,
                     InfXmlConnection* sync_connection);

InfTextSession*
inf_text_session_new_with_user_table(InfCommunicationManager* manager,
                                     InfTextBuffer* buffer,
                                     InfIo* io,
                                     InfUserTable* user_table,
                                     InfSessionStatus status,
                                     InfCommunicationGroup* sync_group,
                                     InfXmlConnection* sync_connection);

void
inf_text_session_set_user_color(InfTextSession* session,
                                InfTextUser* user,
                                gdouble hue);

void
inf_text_session_flush_requests_for_user(InfTextSession* session,
                                         InfTextUser* user);

InfcUserRequest*
inf_text_session_join_user(InfcSessionProxy* proxy,
                           const gchar* name,
                           InfUserStatus status,
                           gdouble hue,
                           guint caret_position,
                           int selection_length,
                           GError** error);

G_END_DECLS

#endif /* __INF_TEXT_SESSION_H__ */

/* vim:set et sw=2 ts=2: */
