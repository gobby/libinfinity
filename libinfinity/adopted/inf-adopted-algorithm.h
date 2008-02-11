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

#ifndef __INF_ADOPTED_ALGORITHM_H__
#define __INF_ADOPTED_ALGORITHM_H__

#include <libinfinity/adopted/inf-adopted-request-log.h>
#include <libinfinity/adopted/inf-adopted-user.h>
#include <libinfinity/common/inf-buffer.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_ADOPTED_TYPE_ALGORITHM                 (inf_adopted_algorithm_get_type())
#define INF_ADOPTED_ALGORITHM(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_ADOPTED_TYPE_ALGORITHM, InfAdoptedAlgorithm))
#define INF_ADOPTED_ALGORITHM_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_ADOPTED_TYPE_ALGORITHM, InfAdoptedAlgorithmClass))
#define INF_ADOPTED_IS_ALGORITHM(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_ADOPTED_TYPE_ALGORITHM))
#define INF_ADOPTED_IS_ALGORITHM_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_ADOPTED_TYPE_ALGORITHM))
#define INF_ADOPTED_ALGORITHM_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_ADOPTED_TYPE_ALGORITHM, InfAdoptedAlgorithmClass))

typedef struct _InfAdoptedAlgorithm InfAdoptedAlgorithm;
typedef struct _InfAdoptedAlgorithmClass InfAdoptedAlgorithmClass;

struct _InfAdoptedAlgorithmClass {
  GObjectClass parent_class;

  /* Signals */

  void(*can_undo_changed)(InfAdoptedAlgorithm* algorithm,
                          InfAdoptedUser* user,
                          gboolean can_undo);

  void(*can_redo_changed)(InfAdoptedAlgorithm* algorithm,
                          InfAdoptedUser* user,
                          gboolean can_redo);

  void(*apply_request)(InfAdoptedAlgorithm* algorithm,
                       InfAdoptedUser* user,
                       InfAdoptedRequest* request);
};

struct _InfAdoptedAlgorithm {
  GObject parent;
};

GType
inf_adopted_algorithm_get_type(void) G_GNUC_CONST;

InfAdoptedAlgorithm*
inf_adopted_algorithm_new(InfBuffer* buffer);

InfAdoptedAlgorithm*
inf_adopted_algorithm_new_full(InfBuffer* buffer,
                               guint max_total_log_size);

void
inf_adopted_algorithm_add_user(InfAdoptedAlgorithm* algorithm,
                               InfAdoptedUser* user);

InfAdoptedStateVector*
inf_adopted_algorithm_get_current(InfAdoptedAlgorithm* algorithm);

InfAdoptedRequest*
inf_adopted_algorithm_generate_request_noexec(InfAdoptedAlgorithm* algorithm,
                                              InfAdoptedUser* user,
                                              InfAdoptedOperation* operation);

InfAdoptedRequest*
inf_adopted_algorithm_generate_request(InfAdoptedAlgorithm* algorithm,
                                       InfAdoptedUser* user,
                                       InfAdoptedOperation* operation);

InfAdoptedRequest*
inf_adopted_algorithm_generate_undo(InfAdoptedAlgorithm* algorithm,
                                    InfAdoptedUser* user);

InfAdoptedRequest*
inf_adopted_algorithm_generate_redo(InfAdoptedAlgorithm* algorithm,
                                    InfAdoptedUser* user);

void
inf_adopted_algorithm_receive_request(InfAdoptedAlgorithm* algorithm,
                                      InfAdoptedRequest* request);

gboolean
inf_adopted_algorithm_can_undo(InfAdoptedAlgorithm* algorithm,
                               InfAdoptedUser* user);

gboolean
inf_adopted_algorithm_can_redo(InfAdoptedAlgorithm* algorithm,
                               InfAdoptedUser* user);

G_END_DECLS

#endif /* __INF_ADOPTED_ALGORITHM_H__ */

/* vim:set et sw=2 ts=2: */
