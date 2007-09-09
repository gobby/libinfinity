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

#include <libinfinity/inf-user.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_TEXT_BUFFER                 (inf_text_buffer_get_type())
#define INF_TEXT_BUFFER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_TEXT_BUFFER, InfTextBuffer))
#define INF_IS_TEXT_BUFFER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_TEXT_BUFFER))
#define INF_TEXT_BUFFER_GET_IFACE(inst)      (G_TYPE_INSTANCE_GET_INTERFACE((inst), INF_TYPE_TEXT_BUFFER, InfTextBufferIface))

typedef struct _InfTextBuffer InfTextBuffer;
typedef struct _InfTextBufferIface InfTextBufferIface;

struct _InfTextBufferIface {
  GTypeInterface parent;

  /* Virtual table */
  const gchar* (*get_encoding)(InfTextBuffer* buffer);

  /* Signals */
  void(*insert_text)(InfTextBuffer* buffer,
                     gconstpointer text,
                     guint len,
                     guint bytes,
                     InfUser* author);

  void(*erase_text)(InfTextBuffer* buffer,
                    guint pos,
                    guint len,
                    InfUser* author);
};

GType
inf_text_buffer_get_type(void) G_GNUC_CONST;

void
inf_text_buffer_insert_text(InfTextBuffer* buffer,
                            gconstpointer text,
                            guint len,
                            gsize bytes,
                            InfUser* author);

void
inf_text_buffer_erase_text(InfTextBuffer* buffer,
                           guint pos,
                           guint len,
                           InfUser* author);

const gchar*
inf_text_buffer_get_encoding(InfTextBuffer* buffer);

G_END_DECLS

#endif /* __INF_TEXT_BUFFER_H__ */

/* vim:set et sw=2 ts=2: */
