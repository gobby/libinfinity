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

#ifndef __INF_TEXT_PWORD_H__
#define __INF_TEXT_PWORD_H__

#include <libxml/tree.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TEXT_TYPE_PWORD            (inf_text_pword_get_type())

typedef struct _InfTextPword InfTextPword;

GType
inf_text_pword_get_type(void) G_GNUC_CONST;

InfTextPword*
inf_text_pword_new(guint initial);

InfTextPword*
inf_text_pword_new_proceed(InfTextPword* pword,
                           guint next);

InfTextPword*
inf_text_pword_copy(InfTextPword* pword);

void
inf_text_pword_free(InfTextPword* pword);

guint
inf_text_pword_get_size(InfTextPword* pword);

guint
inf_text_pword_get_current(InfTextPword* pword);

guint
inf_text_pword_get_origin(InfTextPword* pword);

int
inf_text_pword_compare(InfTextPword* first,
                       InfTextPword* second);

G_END_DECLS

#endif /* __INF_TEXT_PWORD_H__ */

/* vim:set et sw=2 ts=2: */
