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

#ifndef __INF_TEXT_BUFFER_H__
#define __INF_TEXT_BUFFER_H__

#include <libinftext/inf-text-chunk.h>
#include <libinfinity/common/inf-buffer.h>
#include <libinfinity/common/inf-user.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TEXT_TYPE_BUFFER                 (inf_text_buffer_get_type())
#define INF_TEXT_BUFFER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TEXT_TYPE_BUFFER, InfTextBuffer))
#define INF_TEXT_IS_BUFFER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TEXT_TYPE_BUFFER))
#define INF_TEXT_BUFFER_GET_IFACE(inst)      (G_TYPE_INSTANCE_GET_INTERFACE((inst), INF_TEXT_TYPE_BUFFER, InfTextBufferIface))

typedef struct _InfTextBuffer InfTextBuffer;
typedef struct _InfTextBufferIface InfTextBufferIface;

typedef struct _InfTextBufferIter InfTextBufferIter;

struct _InfTextBufferIface {
  GTypeInterface parent;

  /* Virtual table */
  const gchar* (*get_encoding)(InfTextBuffer* buffer);

  guint(*get_length)(InfTextBuffer* buffer);

  InfTextChunk*(*get_slice)(InfTextBuffer* buffer,
                            guint pos,
                            guint len);

  InfTextBufferIter*(*create_iter)(InfTextBuffer* buffer);

  void(*destroy_iter)(InfTextBuffer* buffer,
                      InfTextBufferIter* iter);

  gboolean(*iter_next)(InfTextBuffer* buffer,
                       InfTextBufferIter* iter);
  
  gboolean(*iter_prev)(InfTextBuffer* buffer,
                       InfTextBufferIter* iter);

  gpointer(*iter_get_text)(InfTextBuffer* buffer,
                           InfTextBufferIter* iter);

  guint(*iter_get_length)(InfTextBuffer* buffer,
                          InfTextBufferIter* iter);

  gsize(*iter_get_bytes)(InfTextBuffer* buffer,
                         InfTextBufferIter* iter);

  guint(*iter_get_author)(InfTextBuffer* buffer,
                          InfTextBufferIter* iter);

  /* Signals */
  void(*insert_text)(InfTextBuffer* buffer,
                     guint pos,
                     InfTextChunk* chunk,
                     InfUser* user);

  void(*erase_text)(InfTextBuffer* buffer,
                    guint pos,
                    guint len,
                    InfUser* user);
};

GType
inf_text_buffer_get_type(void) G_GNUC_CONST;

const gchar*
inf_text_buffer_get_encoding(InfTextBuffer* buffer);

guint
inf_text_buffer_get_length(InfTextBuffer* buffer);

InfTextChunk*
inf_text_buffer_get_slice(InfTextBuffer* buffer,
                          guint pos,
                          guint len);

void
inf_text_buffer_insert_text(InfTextBuffer* buffer,
                            guint pos,
                            gconstpointer text,
                            guint len,
                            gsize bytes,
                            InfUser* user);

void
inf_text_buffer_insert_chunk(InfTextBuffer* buffer,
                             guint pos,
                             InfTextChunk* chunk,
                             InfUser* user);

void
inf_text_buffer_erase_text(InfTextBuffer* buffer,
                           guint pos,
                           guint len,
                           InfUser* user);

InfTextBufferIter*
inf_text_buffer_create_iter(InfTextBuffer* buffer);

void
inf_text_buffer_destroy_iter(InfTextBuffer* buffer,
                             InfTextBufferIter* iter);

gboolean
inf_text_buffer_iter_next(InfTextBuffer* buffer,
                          InfTextBufferIter* iter);

gboolean
inf_text_buffer_iter_prev(InfTextBuffer* buffer,
                          InfTextBufferIter* iter);

gpointer
inf_text_buffer_iter_get_text(InfTextBuffer* buffer,
                              InfTextBufferIter* iter);

guint
inf_text_buffer_iter_get_length(InfTextBuffer* buffer,
                                InfTextBufferIter* iter);

gsize
inf_text_buffer_iter_get_bytes(InfTextBuffer* buffer,
                               InfTextBufferIter* iter);

guint
inf_text_buffer_iter_get_author(InfTextBuffer* buffer,
                                InfTextBufferIter* iter);

G_END_DECLS

#endif /* __INF_TEXT_BUFFER_H__ */

/* vim:set et sw=2 ts=2: */
