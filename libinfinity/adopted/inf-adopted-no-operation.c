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

#include <libinfinity/adopted/inf-adopted-no-operation.h>
#include <libinfinity/adopted/inf-adopted-operation.h>

/**
 * SECTION:inf-adopted-no-operation
 * @title: InfAdoptedNoOperation
 * @short_description: An operation not doing anything.
 * @include: libinfinity/adopted/inf-adopted-no-operation.h
 * @stability: Unstable
 * @see_also: #InfAdoptedOperation
 *
 * #InfAdoptedNoOperation is an operation that does nothing when applied to
 * the buffer. This might be the result of an operation transformation, for
 * example if a request is received that is supposed to delete text that was
 * already deleted by the local site. It is also used by #InfAdoptedSession to
 * send the current state to other users in case the user being idle, so that
 * others keep knowing the current state of that user (this is especially
 * required for cleanup of request logs and caches).
 **/

static GObjectClass* parent_class;

static void inf_adopted_no_operation_operation_iface_init(InfAdoptedOperationInterface* iface);
G_DEFINE_TYPE_WITH_CODE(InfAdoptedNoOperation, inf_adopted_no_operation, G_TYPE_OBJECT,
  G_IMPLEMENT_INTERFACE(INF_ADOPTED_TYPE_OPERATION, inf_adopted_no_operation_operation_iface_init))

static void
inf_adopted_no_operation_init(InfAdoptedNoOperation* operation)
{
}

static void
inf_adopted_no_operation_class_init(
  InfAdoptedNoOperationClass* operation_class)
{
}

static gboolean
inf_adopted_no_operation_need_concurrency_id(InfAdoptedOperation* operation,
                                             InfAdoptedOperation* against)
{
  return FALSE;
}

static InfAdoptedOperation*
inf_adopted_no_operation_transform(InfAdoptedOperation* operation,
                                   InfAdoptedOperation* against,
                                   InfAdoptedOperation* operation_lcs,
                                   InfAdoptedOperation* against_lcs,
                                   gint concurrency_id)
{
  return INF_ADOPTED_OPERATION(inf_adopted_no_operation_new());
}

static InfAdoptedOperation*
inf_adopted_no_operation_copy(InfAdoptedOperation* operation)
{
  return INF_ADOPTED_OPERATION(inf_adopted_no_operation_new());
}

static InfAdoptedOperationFlags
inf_adopted_no_operation_get_flags(InfAdoptedOperation* operation)
{
  return INF_ADOPTED_OPERATION_REVERSIBLE;
}

static gboolean
inf_adopted_no_operation_apply(InfAdoptedOperation* operation,
                               InfAdoptedUser* by,
                               InfBuffer* buffer,
                               GError** error)
{
  /* Does nothing */
  return TRUE;
}

static InfAdoptedOperation*
inf_adopted_no_operation_revert(InfAdoptedOperation* operation)
{
  return INF_ADOPTED_OPERATION(inf_adopted_no_operation_new());
}

static void
inf_adopted_no_operation_operation_iface_init(
  InfAdoptedOperationInterface* iface)
{
  iface->need_concurrency_id = inf_adopted_no_operation_need_concurrency_id;
  iface->transform = inf_adopted_no_operation_transform;
  iface->copy = inf_adopted_no_operation_copy;
  iface->get_flags = inf_adopted_no_operation_get_flags;
  iface->apply = inf_adopted_no_operation_apply;
  iface->apply_transformed = NULL;
  iface->revert = inf_adopted_no_operation_revert;
}

/**
 * inf_adopted_no_operation_new:
 *
 * Creates a new #InfAdoptedNoOperation. A no operation is an operation
 * that does nothing, but might be the result of a transformation.
 *
 * Return Value: A new #InfAdoptedNoOperation.
 **/
InfAdoptedNoOperation*
inf_adopted_no_operation_new(void)
{
  GObject* object;
  object = g_object_new(INF_ADOPTED_TYPE_NO_OPERATION, NULL);
  return INF_ADOPTED_NO_OPERATION(object);
}

/* vim:set et sw=2 ts=2: */
