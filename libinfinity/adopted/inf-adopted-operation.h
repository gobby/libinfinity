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

#ifndef __INF_ADOPTED_OPERATION_H__
#define __INF_ADOPTED_OPERATION_H__

/* We cannot include inf-adopted-user.h because inf-adopted-user.h includes
 * us via inf-adopted-request-log.h via inf-adopted-request.h */
/*#include <libinfinity/adopted/inf-adopted-user.h>*/
typedef struct _InfAdoptedUser InfAdoptedUser;

#include <libinfinity/common/inf-buffer.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_ADOPTED_TYPE_OPERATION                 (inf_adopted_operation_get_type())
#define INF_ADOPTED_OPERATION(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_ADOPTED_TYPE_OPERATION, InfAdoptedOperation))
#define INF_ADOPTED_IS_OPERATION(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_ADOPTED_TYPE_OPERATION))
#define INF_ADOPTED_OPERATION_GET_IFACE(inst)      (G_TYPE_INSTANCE_GET_INTERFACE((inst), INF_ADOPTED_TYPE_OPERATION, InfAdoptedOperationIface))

#define INF_ADOPTED_TYPE_OPERATION_FLAGS           (inf_adopted_operation_flags_get_type())

typedef struct _InfAdoptedOperation InfAdoptedOperation;
typedef struct _InfAdoptedOperationIface InfAdoptedOperationIface;

/* TODO: Make a flag out of reversible */
typedef enum _InfAdoptedOperationFlags {
  INF_ADOPTED_OPERATION_AFFECTS_BUFFER = 1 << 0
} InfAdoptedOperationFlags;

struct _InfAdoptedOperationIface {
  GTypeInterface parent;

  InfAdoptedOperation* (*transform)(InfAdoptedOperation* operation,
                                    InfAdoptedOperation* against);

#if 0
  /* TODO: I am not sure whether we need this */
  InfAdoptedOperation* (*copy)(InfAdoptedOperation* operation);
#endif

  InfAdoptedOperationFlags (*get_flags)(InfAdoptedOperation* operation);

  void (*apply)(InfAdoptedOperation* operation,
                InfAdoptedUser* by,
                InfBuffer* buffer);

  gboolean (*is_reversible)(InfAdoptedOperation* operation);

  InfAdoptedOperation* (*revert)(InfAdoptedOperation* operation);

  /* Some operations may not be reversible, but can be made reversible with
   * some extra information such as another operation that collected
   * information while being transformed and the current buffer.
   *
   * This function should is only called when the opertaion itself is not yet
   * reversible and should return either a reversible operation or NULL if
   [* the operation cannot be made reversible. */
  InfAdoptedOperation* (*make_reversible)(InfAdoptedOperation* operation,
                                          InfAdoptedOperation* with,
                                          InfBuffer* buffer);
};

GType
inf_adopted_operation_flags_get_type(void) G_GNUC_CONST;

GType
inf_adopted_operation_get_type(void) G_GNUC_CONST;

InfAdoptedOperation*
inf_adopted_operation_transform(InfAdoptedOperation* operation,
                                InfAdoptedOperation* against);

#if 0
InfAdoptedOperation*
inf_adopted_operation_copy(InfAdoptedOperation* operation);
#endif

InfAdoptedOperationFlags
inf_adopted_operation_get_flags(InfAdoptedOperation* operation);

void
inf_adopted_operation_apply(InfAdoptedOperation* operation,
                            InfAdoptedUser* by,
                            InfBuffer* buffer);

gboolean
inf_adopted_operation_is_reversible(InfAdoptedOperation* operation);

InfAdoptedOperation*
inf_adopted_operation_revert(InfAdoptedOperation* operation);

InfAdoptedOperation*
inf_adopted_operation_make_reversible(InfAdoptedOperation* operation,
                                      InfAdoptedOperation* with,
				      InfBuffer* buffer);

G_END_DECLS

#endif /* __INF_ADOPTED_OPERATION_H__ */

/* vim:set et sw=2 ts=2: */
