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

#ifndef __INF_ADOPTED_SESSION_REPLAY_H__
#define __INF_ADOPTED_SESSION_REPLAY_H__

#include <libinfinity/client/infc-note-plugin.h>
#include <libinfinity/adopted/inf-adopted-session.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_ADOPTED_TYPE_SESSION_REPLAY                 (inf_adopted_session_replay_get_type())
#define INF_ADOPTED_SESSION_REPLAY(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_ADOPTED_TYPE_SESSION_REPLAY, InfAdoptedSessionReplay))
#define INF_ADOPTED_SESSION_REPLAY_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_ADOPTED_TYPE_SESSION_REPLAY, InfAdoptedSessionReplayClass))
#define INF_ADOPTED_IS_SESSION_REPLAY(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_ADOPTED_TYPE_SESSION_REPLAY))
#define INF_ADOPTED_IS_SESSION_REPLAY_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_ADOPTED_TYPE_SESSION_REPLAY))
#define INF_ADOPTED_SESSION_REPLAY_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_ADOPTED_TYPE_SESSION_REPLAY, InfAdoptedSessionReplayClass))

typedef struct _InfAdoptedSessionReplay InfAdoptedSessionReplay;
typedef struct _InfAdoptedSessionReplayClass InfAdoptedSessionReplayClass;

/**
 * InfAdoptedSessionReplayError:
 * @INF_ADOPTED_SESSION_REPLAY_ERROR_BAD_FILE: The record file could not be
 * opened for reading.
 * @INF_ADOPTED_SESSION_REPLAY_ERROR_BAD_XML: The record file contains
 * invalid XML.
 * @INF_ADOPTED_SESSION_REPLAY_ERROR_BAD_DOCUMENT: The record file is not a
 * session recording.
 * @INF_ADOPTED_SESSION_REPLAY_ERROR_BAD_SESSION_TYPE: The record file is a
 * session recording for a different type of session than the one provided.
 * @INF_ADOPTED_SESSION_REPLAY_ERROR_BAD_FORMAT: The record file is invalid.
 * @INF_ADOPTED_SESSION_REPLAY_ERROR_UNEXPECTED_EOF: More data was expected
 * to be read from the record file, but the end of file was reached.
 *
 * Error codes for the <literal>INF_ADOPTED_SESSION_REPLAY_ERROR</literal>
 * error domain. These can occur while loading or replaying a session
 * with #InfAdoptedSessionReplay.
 */
typedef enum _InfAdoptedSessionReplayError {
  INF_ADOPTED_SESSION_REPLAY_ERROR_BAD_FILE,
  INF_ADOPTED_SESSION_REPLAY_ERROR_BAD_XML,
  INF_ADOPTED_SESSION_REPLAY_ERROR_BAD_DOCUMENT,
  INF_ADOPTED_SESSION_REPLAY_ERROR_BAD_SESSION_TYPE,
  INF_ADOPTED_SESSION_REPLAY_ERROR_BAD_FORMAT,
  INF_ADOPTED_SESSION_REPLAY_ERROR_UNEXPECTED_EOF
} InfAdoptedSessionReplayError;

/**
 * InfAdoptedSessionReplayClass:
 *
 * This structure does not contain any public fields.
 */
struct _InfAdoptedSessionReplayClass {
  /*< private >*/
  GObjectClass parent_class;
};

/**
 * InfAdoptedSessionReplay:
 *
 * #InfAdoptedSessionReplay is an opaque data type. You should only access it
 * via the public API functions.
 */
struct _InfAdoptedSessionReplay {
  /*< private >*/
  GObject parent;
};

GType
inf_adopted_session_replay_get_type(void);

InfAdoptedSessionReplay*
inf_adopted_session_replay_new(void);

/* TODO: Move InfcNotePlugin to common, as InfNotePlugin, and "derive"
 * InfdNotePlugin from it. */
gboolean
inf_adopted_session_replay_set_record(InfAdoptedSessionReplay* replay,
                                      const gchar* filename,
                                      const InfcNotePlugin* plugin,
                                      GError** error);

InfAdoptedSession*
inf_adopted_session_replay_get_session(InfAdoptedSessionReplay* replay);

gboolean
inf_adopted_session_replay_play_next(InfAdoptedSessionReplay* replay,
                                     GError** error);

gboolean
inf_adopted_session_replay_play_to_end(InfAdoptedSessionReplay* replay,
                                       GError** error);

G_END_DECLS

#endif /* __INF_ADOPTED_SESSION_REPLAY_H__ */

/* vim:set et sw=2 ts=2: */
