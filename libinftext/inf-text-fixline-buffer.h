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

#ifndef __INF_TEXT_FIXLINE_BUFFER_H__
#define __INF_TEXT_FIXLINE_BUFFER_H__

#include <libinftext/inf-text-buffer.h>

#include <libinfinity/common/inf-io.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TEXT_TYPE_FIXLINE_BUFFER                 (inf_text_fixline_buffer_get_type())
#define INF_TEXT_FIXLINE_BUFFER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TEXT_TYPE_FIXLINE_BUFFER, InfTextFixlineBuffer))
#define INF_TEXT_FIXLINE_BUFFER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TEXT_TYPE_FIXLINE_BUFFER, InfTextFixlineBufferClass))
#define INF_TEXT_IS_FIXLINE_BUFFER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TEXT_TYPE_FIXLINE_BUFFER))
#define INF_TEXT_IS_FIXLINE_BUFFER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TEXT_TYPE_FIXLINE_BUFFER))
#define INF_TEXT_FIXLINE_BUFFER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TEXT_TYPE_FIXLINE_BUFFER, InfTextFixlineBufferClass))

typedef struct _InfTextFixlineBuffer InfTextFixlineBuffer;
typedef struct _InfTextFixlineBufferClass InfTextFixlineBufferClass;

/**
 * InfTextFixlineBufferClass:
 *
 * This structure does not contain any public fields.
 */
struct _InfTextFixlineBufferClass {
  GObjectClass parent_class;
};

/**
 * InfTextFixlineBuffer:
 *
 * #InfTextFixlineBuffer is an opaque data type. You should only access it
 * via the public API functions.
 */
struct _InfTextFixlineBuffer {
  GObject parent;
};

GType
inf_text_fixline_buffer_get_type(void) G_GNUC_CONST;

InfTextFixlineBuffer*
inf_text_fixline_buffer_new(InfIo* io,
                            InfTextBuffer* buffer,
                            guint n_lines);

G_END_DECLS

#endif /* __INF_TEXT_FIXLINE_BUFFER_H__ */

/* vim:set et sw=2 ts=2: */
