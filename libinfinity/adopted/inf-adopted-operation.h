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

/**
 * InfAdoptedOperationFlags:
 * @INF_ADOPTED_OPERATION_AFFECTS_BUFFER: The operation changes the content of
 * the buffer.
 * @INF_ADOPTED_OPERATION_REVERSIBLE: The operation is reversible, which means
 * that inf_adopted_operation_revert() can be called to generate an operation
 * that undoes the effect of the operation.
 *
 * Various flags for #InfAdoptedOperation.
 */
typedef enum _InfAdoptedOperationFlags {
  INF_ADOPTED_OPERATION_AFFECTS_BUFFER = 1 << 0,
  INF_ADOPTED_OPERATION_REVERSIBLE = 1 << 1
} InfAdoptedOperationFlags;

/**
 * InfAdoptedOperationIface:
 * @transform: Virtual function that transform @operation against @against and
 * returns a new #InfAdoptedOperation as the result of the transformation.
 * @concurrency_id is either 1 or -1 and can be used to make a decision in
 * case there is no other criteria to decide how to do the transformation, for
 * example when both @operation and @against are inserting text at the same
 * position in the buffer.
 * @copy: Virtual function that returns a copy of the operation.
 * @get_flags: Virtual function that returns the flags of the operation,
 * see #InfAdoptedOperationFlags.
 * @apply: Virtual function that applies the operation to the buffer. @by is
 * the user that applies the operation.
 * @revert: Virtual function that creates a new operation that undoes the
 * effect of the operation. If @get_flags does never return the
 * %INF_ADOPTED_OPERATION_REVERSIBLE flag set, then this is allowed to be
 * %NULL.
 * @make_reversible: Virtual function that creates a reversible operation out
 * of the operation itself. If @get_flags does always return the
 * %INF_ADOPTED_OPERATION_REVERSIBLE flag set, then this is allowed to be
 * %NULL. Some operations may not be reversible, but can be made reversible
 * with some extra information such as another operation that collected
 * information while being transformed, or the current buffer. This function
 * should return either a new, reversible operation that has the same effect
 * on a buffer, or %NULL if the operation cannot be made reversible.
 *
 * The virtual methods that need to be implemented by an operation to be used
 * with #InfAdoptedAlgorithm.
 */
struct _InfAdoptedOperationIface {
  /*< private >*/
  GTypeInterface parent;

  /*< public >*/
  InfAdoptedOperation* (*transform)(InfAdoptedOperation* operation,
                                    InfAdoptedOperation* against,
                                    gint concurrency_id);

  InfAdoptedOperation* (*copy)(InfAdoptedOperation* operation);

  InfAdoptedOperationFlags (*get_flags)(InfAdoptedOperation* operation);

  void (*apply)(InfAdoptedOperation* operation,
                InfAdoptedUser* by,
                InfBuffer* buffer);

  InfAdoptedOperation* (*revert)(InfAdoptedOperation* operation);

  InfAdoptedOperation* (*make_reversible)(InfAdoptedOperation* operation,
                                          InfAdoptedOperation* with,
                                          InfBuffer* buffer);
};

/**
 * InfAdoptedOperation:
 *
 * #InfAdoptedOperation is an opaque data type. You should only access it
 * via the public API functions.
 */

GType
inf_adopted_operation_flags_get_type(void) G_GNUC_CONST;

GType
inf_adopted_operation_get_type(void) G_GNUC_CONST;

InfAdoptedOperation*
inf_adopted_operation_transform(InfAdoptedOperation* operation,
                                InfAdoptedOperation* against,
                                gint concurrency_id);

InfAdoptedOperation*
inf_adopted_operation_copy(InfAdoptedOperation* operation);

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
