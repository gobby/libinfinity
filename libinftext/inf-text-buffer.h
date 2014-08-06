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

/**
 * InfTextBuffer:
 *
 * #InfTextBuffer is an opaque data type. You should only access it
 * via the public API functions.
 */
typedef struct _InfTextBuffer InfTextBuffer;
typedef struct _InfTextBufferIface InfTextBufferIface;

/**
 * InfTextBufferIter:
 *
 * #InfTextBufferIter is an opaque data type. You should only access it
 * via the public API functions.
 */
typedef struct _InfTextBufferIter InfTextBufferIter;

/**
 * InfTextBufferIface:
 * @get_encoding: Virtual function which returns the character coding of the
 * buffer.
 * @get_length: Virtual function to return the total length of the text in
 * the buffer, in characters.
 * @get_slice: Virtual function to extract a slice of text from the buffer.
 * @insert_text: Virtual function to insert text into the buffer.
 * @erase_text: Virtual function to remove text from the buffer.
 * @create_begin_iter: Virtual function to create a #InfTextBufferIter at the
 * beginning of the buffer, used for traversing through buffer segments.
 * @create_end_iter: Virtual function to create a #InfTextBufferIter at the
 * end of the buffer, used for traversing through buffer segments.
 * @destroy_iter: Virtual function to destroy an iterator created with
 * the @create_begin_iter and @create_end_iter functions.
 * @iter_next: Virtual function to advance a #InfTextBufferIter to the next
 * segment.
 * @iter_prev: Virtual function to retreat a #InfTextBufferIter to the
 * previous segment.
 * @iter_get_text: Virtual function to obtain the text of a segment a
 * #InfTextBufferIter points to.
 * @iter_get_offset: Virtual function to obtain the offset of the first
 * character in the segment a #InfTextBufferIter points to.
 * @iter_get_length: Virtual function to obtain the length of a segment a
 * #InfTextBufferIter points to.
 * @iter_get_bytes: Virtual function to obtain the number of bytes in a
 * segment a #InfTextBufferIter points to.
 * @iter_get_author: Virtual function to obtain the author of the segment a
 * #InfTextBufferIter points to.
 * @text_inserted: Default signal handler of the #InfTextBuffer::text-inserted
 * signal.
 * @text_erased: Default signal handler of the #InfTextBuffer::text-erased
 * signal.
 *
 * This structure contains virtual functions and signal handlers of the
 * #InfTextBuffer interface.
 */
struct _InfTextBufferIface {
  /*< private >*/
  GTypeInterface parent;

  /*< public >*/

  /* Virtual table */
  const gchar* (*get_encoding)(InfTextBuffer* buffer);

  guint(*get_length)(InfTextBuffer* buffer);

  InfTextChunk*(*get_slice)(InfTextBuffer* buffer,
                            guint pos,
                            guint len);

  void(*insert_text)(InfTextBuffer* buffer,
                     guint pos,
                     InfTextChunk* chunk,
                     InfUser* user);

  void(*erase_text)(InfTextBuffer* buffer,
                    guint pos,
                    guint len,
                    InfUser* user);

  InfTextBufferIter*(*create_begin_iter)(InfTextBuffer* buffer);

  InfTextBufferIter*(*create_end_iter)(InfTextBuffer* buffer);

  void(*destroy_iter)(InfTextBuffer* buffer,
                      InfTextBufferIter* iter);

  gboolean(*iter_next)(InfTextBuffer* buffer,
                       InfTextBufferIter* iter);
  
  gboolean(*iter_prev)(InfTextBuffer* buffer,
                       InfTextBufferIter* iter);

  gpointer(*iter_get_text)(InfTextBuffer* buffer,
                           InfTextBufferIter* iter);

  guint(*iter_get_offset)(InfTextBuffer* buffer,
                          InfTextBufferIter* iter);

  guint(*iter_get_length)(InfTextBuffer* buffer,
                          InfTextBufferIter* iter);

  gsize(*iter_get_bytes)(InfTextBuffer* buffer,
                         InfTextBufferIter* iter);

  guint(*iter_get_author)(InfTextBuffer* buffer,
                          InfTextBufferIter* iter);

  /* Signals */
  void(*text_inserted)(InfTextBuffer* buffer,
                       guint pos,
                       InfTextChunk* chunk,
                       InfUser* user);

  void(*text_erased)(InfTextBuffer* buffer,
                     guint pos,
                     InfTextChunk* chunk,
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
                            gsize bytes,
                            guint len,
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
inf_text_buffer_create_begin_iter(InfTextBuffer* buffer);

InfTextBufferIter*
inf_text_buffer_create_end_iter(InfTextBuffer* buffer);

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
inf_text_buffer_iter_get_offset(InfTextBuffer* buffer,
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

void
inf_text_buffer_text_inserted(InfTextBuffer* buffer,
                              guint pos,
                              InfTextChunk* chunk,
                              InfUser* user);

void
inf_text_buffer_text_erased(InfTextBuffer* buffer,
                            guint pos,
                            InfTextChunk* chunk,
                            InfUser* user);

G_END_DECLS

#endif /* __INF_TEXT_BUFFER_H__ */

/* vim:set et sw=2 ts=2: */
