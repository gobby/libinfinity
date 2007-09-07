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

#ifndef __INF_ADOPTED_STATE_VECTOR_H__
#define __INF_ADOPTED_STATE_VECTOR_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_ADOPTED_TYPE_STATE_VECTOR            (inf_adopted_state_vector_get_type())

/* TODO: Wrap in own struct for type safety? However, StateVectors are often
 * used and I think it is best if they are as fast as possible. */
/* TODO: I think GTree is better suited for what we are looking, but it does
 * not allow iteration over its elements, required for
 * inf_adopted_state_vector_compare. */
typedef GSequence InfAdoptedStateVector;

typedef enum _InfAdoptedStateVectorError {
  INF_ADOPTED_STATE_VECTOR_BAD_FORMAT,

  INF_ADOPTED_STATE_VECTOR_FAILED
} InfAdoptedStateVectorError;

typedef void(*InfAdoptedStateVectorForeachFunc)(guint, guint, gpointer);

GType
inf_adopted_state_vector_get_type(void) G_GNUC_CONST;

GQuark
inf_adopted_state_vector_get_quark(void);

InfAdoptedStateVector*
inf_adopted_state_vector_new(void);

InfAdoptedStateVector*
inf_adopted_state_vector_copy(InfAdoptedStateVector* vec);

void
inf_adopted_state_vector_free(InfAdoptedStateVector* vec);

guint
inf_adopted_state_vector_get(InfAdoptedStateVector* vec,
                             guint id);

void
inf_adopted_state_vector_set(InfAdoptedStateVector* vec,
                             guint id,
                             guint value);

void
inf_adopted_state_vector_add(InfAdoptedStateVector* vec,
                             guint id,
                             gint value);

void
inf_adopted_state_vector_foreach(InfAdoptedStateVector* vec,
                                 InfAdoptedStateVectorForeachFunc func,
                                 gpointer user_data);

int
inf_adopted_state_vector_compare(InfAdoptedStateVector* first,
                                 InfAdoptedStateVector* second);

gboolean
inf_adopted_state_vector_causally_before(InfAdoptedStateVector* first,
                                         InfAdoptedStateVector* second);

gchar*
inf_adopted_state_vector_to_string(InfAdoptedStateVector* vec);

InfAdoptedStateVector*
inf_adopted_state_vector_from_string(const gchar* str,
                                     GError** error);

gchar*
inf_adopted_state_vector_to_string_diff(InfAdoptedStateVector* vec,
                                        InfAdoptedStateVector* orig);

InfAdoptedStateVector*
inf_adopted_state_vector_from_string_diff(const gchar* str,
                                          InfAdoptedStateVector* orig,
                                          GError** error);

G_END_DECLS

#endif /* __INF_ADOPTED_STATE_VECTOR_H__ */

/* vim:set et sw=2 ts=2: */
