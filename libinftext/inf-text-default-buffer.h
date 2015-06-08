/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2015 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INF_TEXT_DEFAULT_BUFFER_H__
#define __INF_TEXT_DEFAULT_BUFFER_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TEXT_TYPE_DEFAULT_BUFFER                 (inf_text_default_buffer_get_type())
#define INF_TEXT_DEFAULT_BUFFER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TEXT_TYPE_DEFAULT_BUFFER, InfTextDefaultBuffer))
#define INF_TEXT_DEFAULT_BUFFER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TEXT_TYPE_DEFAULT_BUFFER, InfTextDefaultBufferClass))
#define INF_TEXT_IS_DEFAULT_BUFFER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TEXT_TYPE_DEFAULT_BUFFER))
#define INF_TEXT_IS_DEFAULT_BUFFER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TEXT_TYPE_DEFAULT_BUFFER))
#define INF_TEXT_DEFAULT_BUFFER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TEXT_TYPE_DEFAULT_BUFFER, InfTextDefaultBufferClass))

typedef struct _InfTextDefaultBuffer InfTextDefaultBuffer;
typedef struct _InfTextDefaultBufferClass InfTextDefaultBufferClass;

struct _InfTextDefaultBufferClass {
  GObjectClass parent_class;
};

struct _InfTextDefaultBuffer {
  GObject parent;
};

GType
inf_text_default_buffer_get_type(void) G_GNUC_CONST;

InfTextDefaultBuffer*
inf_text_default_buffer_new(const gchar* encoding);

G_END_DECLS

#endif /* __INF_TEXT_DEFAULT_BUFFER_H__ */

/* vim:set et sw=2 ts=2: */
