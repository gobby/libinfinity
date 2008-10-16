/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008 Armin Burgmeier <armin@arbur.net>
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

/**
 * InfAdoptedStateVector:
 *
 * #InfAdoptedStateVector is an opaque data type. You should only access it
 * via the public API functions.
 */
typedef struct _InfAdoptedStateVector InfAdoptedStateVector;

/**
 * InfAdoptedStateVectorError:
 * @INF_ADOPTED_STATE_VECTOR_BAD_FORMAT: A string representation of an
 * #InfAdoptedStateVector as required by
 * inf_adopted_state_vector_from_string() or
 * inf_adopted_state_vector_from_string_diff() is invalid.
 * @INF_ADOPTED_STATE_VECTOR_FAILED: No further specified error code.
 *
 * Error codes for #InfAdoptedStateVector.
 */
typedef enum _InfAdoptedStateVectorError {
  INF_ADOPTED_STATE_VECTOR_BAD_FORMAT,

  INF_ADOPTED_STATE_VECTOR_FAILED
} InfAdoptedStateVectorError;

/**
 * InfAdoptedStateVectorForeachFunc:
 * @id: The ID of the entry.
 * @value: The value of the entry.
 * @user_data: The user data passed to inf_adopted_state_vector_foreach().
 *
 * This function is called for every component in the state vector during
 * the invocation of inf_adopted_state_vector_foreach().
 */
typedef void(*InfAdoptedStateVectorForeachFunc)(guint id,
                                                guint value,
                                                gpointer user_data);

GType
inf_adopted_state_vector_get_type(void) G_GNUC_CONST;

GQuark
inf_adopted_state_vector_error_quark(void);

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

gboolean
inf_adopted_state_vector_causally_before_inc(InfAdoptedStateVector* first,
                                             InfAdoptedStateVector* second,
                                             guint inc_component);

guint
inf_adopted_state_vector_vdiff(InfAdoptedStateVector* first,
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
