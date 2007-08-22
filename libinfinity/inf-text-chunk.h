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

#ifndef __INF_TEXT_CHUNK_H__
#define __INF_TEXT_CHUNK_H__

#include <libinfinity/common/inf-user.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TEXT_TYPE_CHUNK            (inf_text_chunk_get_type())

typedef struct _InfTextChunk InfTextChunk;

GType
inf_text_chunk_get_type(void) G_GNUC_CONST;

InfTextChunk*
inf_text_chunk_new(const gchar* encoding);

InfTextChunk*
inf_text_chunk_copy(InfTextChunk* self);

void
inf_text_chunk_free(InfTextChunk* self);

const gchar*
inf_text_chunk_get_encoding(InfTextChunk* self);

guint
inf_text_chunk_get_length(InfTextChunk* self);

InfTextChunk*
inf_text_chunk_substring(InfTextChunk* self,
                         guint begin,
                         guint length);

void
inf_text_chunk_insert(InfTextChunk* self,
                      guint offset,
                      const gchar* text,
                      guint author);

void
inf_text_chunk_insert(InfTextChunk* self,
                      guint offset,
                      InfTextChunk* text);

void
inf_text_chunk_erase(InfTextChunk* self,
                     guint begin,
                     guint length);

G_END_DECLS

#endif /* __INF_TEXT_CHUNK_H__ */

/* vim:set et sw=2 ts=2: */
