/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2013 Armin Burgmeier <armin@arbur.net>
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

/**
 * SECTION:inf-adopted-split-operation
 * @short_description: Operation wrapping two operations
 * @include: libinfinity/adopted/inf-adopted-split-operation.h
 * @stability: Unstable
 *
 * #InfAdoptedSplitOperation is a wrapper around that two
 * #InfAdoptedOperation<!-- -->s. This is normally not required directly but
 * may be a result of some transformation.
 **/

#include <libinfinity/adopted/inf-adopted-split-operation.h>
#include <libinfinity/adopted/inf-adopted-operation.h>

#include <libinfinity/adopted/inf-adopted-concurrency-warning.h>

typedef struct _InfAdoptedSplitOperationPrivate InfAdoptedSplitOperationPrivate;
struct _InfAdoptedSplitOperationPrivate {
  InfAdoptedOperation* first;
  InfAdoptedOperation* second;
  /* TODO: Cache new_second? We should probably profile before */
};

enum {
  PROP_0,

  PROP_FIRST,
  PROP_SECOND
};

#define INF_ADOPTED_SPLIT_OPERATION_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_ADOPTED_TYPE_SPLIT_OPERATION, InfAdoptedSplitOperationPrivate))

static GObjectClass* parent_class;

static void
inf_adopted_split_operation_unsplit_impl(InfAdoptedSplitOperation* operation,
                                         GSList** list)
{
  InfAdoptedSplitOperationPrivate* priv;
  priv = INF_ADOPTED_SPLIT_OPERATION_PRIVATE(operation);

  /* Since we prepend the entries to the list, we begin with the second
   * operation so that the list actually contains the operations in order. */

  if(INF_ADOPTED_IS_SPLIT_OPERATION(priv->second))
  {
    inf_adopted_split_operation_unsplit_impl(
      INF_ADOPTED_SPLIT_OPERATION(priv->second),
      list
    );
  }
  else
  {
    *list = g_slist_prepend(*list, priv->second);
  }

  if(INF_ADOPTED_IS_SPLIT_OPERATION(priv->first))
  {
    inf_adopted_split_operation_unsplit_impl(
      INF_ADOPTED_SPLIT_OPERATION(priv->first),
      list
    );
  }
  else
  {
    *list = g_slist_prepend(*list, priv->first);
  }
}

static void
inf_adopted_split_operation_init(GTypeInstance* instance,
                                 gpointer g_class)
{
  InfAdoptedSplitOperation* operation;
  InfAdoptedSplitOperationPrivate* priv;

  operation = INF_ADOPTED_SPLIT_OPERATION(instance);
  priv = INF_ADOPTED_SPLIT_OPERATION_PRIVATE(operation);

  priv->first = NULL;
  priv->second = NULL;
}

static void
inf_adopted_split_operation_dispose(GObject* object)
{
  InfAdoptedSplitOperation* operation;
  InfAdoptedSplitOperationPrivate* priv;

  operation = INF_ADOPTED_SPLIT_OPERATION(object);
  priv = INF_ADOPTED_SPLIT_OPERATION_PRIVATE(operation);

  if(priv->first != NULL)
    g_object_unref(G_OBJECT(priv->first));

  if(priv->second != NULL)
    g_object_unref(G_OBJECT(priv->second));

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_adopted_split_operation_set_property(GObject* object,
                                         guint prop_id,
                                         const GValue* value,
                                         GParamSpec* pspec)
{
  InfAdoptedSplitOperation* operation;
  InfAdoptedSplitOperationPrivate* priv;

  operation = INF_ADOPTED_SPLIT_OPERATION(object);
  priv = INF_ADOPTED_SPLIT_OPERATION_PRIVATE(operation);

  switch(prop_id)
  {
  case PROP_FIRST:
    if(priv->first != NULL) g_object_unref(G_OBJECT(priv->first));
    priv->first = INF_ADOPTED_OPERATION(g_value_dup_object(value));
    g_assert(priv->first != INF_ADOPTED_OPERATION(operation));
    break;
  case PROP_SECOND:
    if(priv->second != NULL) g_object_unref(G_OBJECT(priv->second));
    priv->second = INF_ADOPTED_OPERATION(g_value_dup_object(value));
    g_assert(priv->second != INF_ADOPTED_OPERATION(operation));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_adopted_split_operation_get_property(GObject* object,
                                         guint prop_id,
                                         GValue* value,
                                         GParamSpec* pspec)
{
  InfAdoptedSplitOperation* operation;
  InfAdoptedSplitOperationPrivate* priv;

  operation = INF_ADOPTED_SPLIT_OPERATION(object);
  priv = INF_ADOPTED_SPLIT_OPERATION_PRIVATE(operation);

  switch(prop_id)
  {
  case PROP_FIRST:
    g_value_set_object(value, G_OBJECT(priv->first));
    break;
  case PROP_SECOND:
    g_value_set_object(value, G_OBJECT(priv->second));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_adopted_split_operation_class_init(gpointer g_class,
                                       gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfAdoptedSplitOperationPrivate));

  object_class->dispose = inf_adopted_split_operation_dispose;
  object_class->set_property = inf_adopted_split_operation_set_property;
  object_class->get_property = inf_adopted_split_operation_get_property;

  g_object_class_install_property(
    object_class,
    PROP_FIRST,
    g_param_spec_object(
      "first",
      "First operation",
      "The first operation of the split operation",
      INF_ADOPTED_TYPE_OPERATION,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_SECOND,
    g_param_spec_object(
      "second",
      "Second operation",
      "The second operation of the split operation",
      INF_ADOPTED_TYPE_OPERATION,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );
}

static gboolean
inf_adopted_split_operation_need_concurrency_id(InfAdoptedOperation* op,
                                                InfAdoptedOperation* against)
{
  InfAdoptedSplitOperation* split;
  InfAdoptedSplitOperationPrivate* priv;

  split = INF_ADOPTED_SPLIT_OPERATION(op);
  priv = INF_ADOPTED_SPLIT_OPERATION_PRIVATE(split);

  return inf_adopted_operation_need_concurrency_id(priv->first, against) ||
         inf_adopted_operation_need_concurrency_id(priv->second, against);
}

static InfAdoptedConcurrencyId
inf_adopted_split_operation_get_concurrency_id(InfAdoptedOperation* operation,
                                               InfAdoptedOperation* against)
{
  InfAdoptedSplitOperation* split;
  InfAdoptedSplitOperationPrivate* priv;
  InfAdoptedConcurrencyId first_id;
  InfAdoptedConcurrencyId second_id;

  split = INF_ADOPTED_SPLIT_OPERATION(operation);
  priv = INF_ADOPTED_SPLIT_OPERATION_PRIVATE(split);

  first_id = inf_adopted_operation_get_concurrency_id(priv->first, against);
  second_id = inf_adopted_operation_get_concurrency_id(priv->second, against);

  /* everything is fine if both split parts agree, or if only one can
   * make a decision. Problem if they are contradictory. */

  if(first_id == second_id)
    return first_id;
  else if(first_id == INF_ADOPTED_CONCURRENCY_NONE)
    return second_id;
  else if(second_id == INF_ADOPTED_CONCURRENCY_NONE)
    return first_id;

  _inf_adopted_concurrency_warning(INF_ADOPTED_TYPE_SPLIT_OPERATION);
  return INF_ADOPTED_CONCURRENCY_NONE;
}

static InfAdoptedOperation*
inf_adopted_split_operation_transform(InfAdoptedOperation* operation,
                                      InfAdoptedOperation* against,
                                      InfAdoptedConcurrencyId concurrency_id)
{
  InfAdoptedSplitOperation* split;
  InfAdoptedSplitOperationPrivate* priv;

  InfAdoptedOperation* new_first;
  InfAdoptedOperation* new_second;
  InfAdoptedOperation* result;

  split = INF_ADOPTED_SPLIT_OPERATION(operation);
  priv = INF_ADOPTED_SPLIT_OPERATION_PRIVATE(split);

  new_first = inf_adopted_operation_transform(
    priv->first,
    against,
    concurrency_id
  );

  new_second = inf_adopted_operation_transform(
    priv->second,
    against,
    concurrency_id
  );

  /* TODO: Check whether one of these is a noop and return only the other
   * one in that case. */

  result = INF_ADOPTED_OPERATION(
    inf_adopted_split_operation_new(new_first, new_second)
  );

  g_object_unref(G_OBJECT(new_first));
  g_object_unref(G_OBJECT(new_second));

  return result;
}

static InfAdoptedOperation*
inf_adopted_split_operation_copy(InfAdoptedOperation* operation)
{
  InfAdoptedSplitOperation* split;
  InfAdoptedSplitOperationPrivate* priv;

  split = INF_ADOPTED_SPLIT_OPERATION(operation);
  priv = INF_ADOPTED_SPLIT_OPERATION_PRIVATE(split);

  return INF_ADOPTED_OPERATION(
    inf_adopted_split_operation_new(
      inf_adopted_operation_copy(priv->first),
      inf_adopted_operation_copy(priv->second)
    )
  );
}

static InfAdoptedOperationFlags
inf_adopted_split_operation_get_flags(InfAdoptedOperation* operation)
{
  InfAdoptedSplitOperation* split;
  InfAdoptedSplitOperationPrivate* priv;
  InfAdoptedOperationFlags flags1;
  InfAdoptedOperationFlags flags2;
  InfAdoptedOperationFlags result;

  split = INF_ADOPTED_SPLIT_OPERATION(operation);
  priv = INF_ADOPTED_SPLIT_OPERATION_PRIVATE(split);

  flags1 = inf_adopted_operation_get_flags(priv->first);
  flags2 = inf_adopted_operation_get_flags(priv->second);
  result = 0;

  if( (flags1 & INF_ADOPTED_OPERATION_AFFECTS_BUFFER) != 0 ||
      (flags2 & INF_ADOPTED_OPERATION_AFFECTS_BUFFER) != 0)
  {
    result |= INF_ADOPTED_OPERATION_AFFECTS_BUFFER;
  }

  if( (flags1 & INF_ADOPTED_OPERATION_REVERSIBLE) != 0 &&
      (flags2 & INF_ADOPTED_OPERATION_REVERSIBLE) != 0)
  {
    result |= INF_ADOPTED_OPERATION_REVERSIBLE;
  }

  return result;
}

static void
inf_adopted_split_operation_apply(InfAdoptedOperation* operation,
                                  InfAdoptedUser* by,
                                  InfBuffer* buffer)
{
  InfAdoptedSplitOperation* split;
  InfAdoptedSplitOperationPrivate* priv;
  InfAdoptedOperation* new_second;

  split = INF_ADOPTED_SPLIT_OPERATION(operation);
  priv = INF_ADOPTED_SPLIT_OPERATION_PRIVATE(split);

  new_second = inf_adopted_operation_transform(priv->second, priv->first, 0);

  inf_adopted_operation_apply(priv->first, by, buffer);
  inf_adopted_operation_apply(new_second, by, buffer);

  g_object_unref(G_OBJECT(new_second));
}

static InfAdoptedOperation*
inf_adopted_split_operation_revert(InfAdoptedOperation* operation)
{
  InfAdoptedSplitOperation* split;
  InfAdoptedSplitOperationPrivate* priv;
  InfAdoptedOperation* new_second;

  InfAdoptedOperation* revert_first;
  InfAdoptedOperation* revert_second;
  InfAdoptedSplitOperation* result;

  split = INF_ADOPTED_SPLIT_OPERATION(operation);
  priv = INF_ADOPTED_SPLIT_OPERATION_PRIVATE(split);

  new_second = inf_adopted_operation_transform(priv->second, priv->first, 0);

  revert_first = inf_adopted_operation_revert(priv->first);
  revert_second = inf_adopted_operation_revert(new_second);
  g_object_unref(new_second);

  result = inf_adopted_split_operation_new(revert_first, revert_second);

  g_object_unref(revert_first);
  g_object_unref(revert_second);

  return INF_ADOPTED_OPERATION(result);
}

static InfAdoptedOperation*
inf_adopted_split_operation_make_reversible(InfAdoptedOperation* operation,
                                            InfAdoptedOperation* with,
                                            InfBuffer* buffer)
{
  InfAdoptedSplitOperation* split;
  InfAdoptedSplitOperationPrivate* priv;
  InfAdoptedOperation* first;
  InfAdoptedOperation* second;

  split = INF_ADOPTED_SPLIT_OPERATION(operation);
  priv = INF_ADOPTED_SPLIT_OPERATION_PRIVATE(split);

  first = inf_adopted_operation_make_reversible(priv->first, with, buffer);
  if(first == NULL) return NULL;

  second = inf_adopted_operation_make_reversible(priv->second, with, buffer);
  if(second == NULL)
  {
    g_object_unref(G_OBJECT(first));
    return NULL;
  }

  return INF_ADOPTED_OPERATION(
    inf_adopted_split_operation_new(first, second)
  );
}

static void
inf_adopted_split_operation_operation_init(gpointer g_iface,
                                           gpointer iface_data)
{
  InfAdoptedOperationIface* iface;
  iface = (InfAdoptedOperationIface*)g_iface;

  iface->need_concurrency_id =
    inf_adopted_split_operation_need_concurrency_id;
  iface->get_concurrency_id = inf_adopted_split_operation_get_concurrency_id;
  iface->transform = inf_adopted_split_operation_transform;
  iface->copy = inf_adopted_split_operation_copy;
  iface->get_flags = inf_adopted_split_operation_get_flags;
  iface->apply = inf_adopted_split_operation_apply;
  iface->revert = inf_adopted_split_operation_revert;
  iface->make_reversible = inf_adopted_split_operation_make_reversible;
}

GType
inf_adopted_split_operation_get_type(void)
{
  static GType split_operation_type = 0;

  if(!split_operation_type)
  {
    static const GTypeInfo split_operation_type_info = {
      sizeof(InfAdoptedSplitOperationClass),    /* class_size */
      NULL,                                     /* base_init */
      NULL,                                     /* base_finalize */
      inf_adopted_split_operation_class_init,   /* class_init */
      NULL,                                     /* class_finalize */
      NULL,                                     /* class_data */
      sizeof(InfAdoptedSplitOperation),         /* instance_size */
      0,                                        /* n_preallocs */
      inf_adopted_split_operation_init,         /* instance_init */
      NULL                                      /* value_table */
    };

    static const GInterfaceInfo operation_info = {
      inf_adopted_split_operation_operation_init,
      NULL,
      NULL
    };

    split_operation_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfAdoptedSplitOperation",
      &split_operation_type_info,
      0
    );

    g_type_add_interface_static(
      split_operation_type,
      INF_ADOPTED_TYPE_OPERATION,
      &operation_info
    );
  }

  return split_operation_type;
}

/**
 * inf_adopted_split_operation_new:
 * @first: one of the #InfAdoptedOperation<!-- -->s to be wrapped
 * @second: the other #InfAdoptedOperation to be wrapped
 *
 * Creates a new #InfAdoptedSplitOperation. A split operation is simply a
 * wrapper around two operations (which may in turn be split operations).
 *
 * Return Value: A new #InfAdoptedSplitOperation.
 **/
InfAdoptedSplitOperation*
inf_adopted_split_operation_new(InfAdoptedOperation* first,
                                InfAdoptedOperation* second)
{
  GObject* object;

  g_return_val_if_fail(INF_ADOPTED_IS_OPERATION(first), NULL);
  g_return_val_if_fail(INF_ADOPTED_IS_OPERATION(second), NULL);

  object = g_object_new(
    INF_ADOPTED_TYPE_SPLIT_OPERATION,
    "first", first,
    "second", second,
    NULL
  );

  return INF_ADOPTED_SPLIT_OPERATION(object);
}

/**
 * inf_adopted_split_operation_unsplit:
 * @operation: A #InfAdoptedSplitOperation.
 *
 * Returns a list of the operations contained by the split operation. If the
 * splitted operation are in turn split operations, they will also be
 * unsplitted. The returned list is guarenteed to not contain a
 * #InfAdoptedSplitOperation.
 *
 * Return Value: A list of operations. Free with g_slist_free() when done.
 **/
GSList*
inf_adopted_split_operation_unsplit(InfAdoptedSplitOperation* operation)
{
  GSList* result;

  result = NULL;
  inf_adopted_split_operation_unsplit_impl(operation, &result);
  return result;
}

/**
 * inf_adopted_split_operation_transform_other:
 * @op: A #InfAdoptedSplitOperation.
 * @other: An arbitrary #InfAdoptedOperation.
 * @concurrency_id: The concurrency id for the transformation of
 * @other against @op.
 *
 * Transforms @other against @op.
 *
 * Return Value: The transformed operation.
 **/
InfAdoptedOperation*
inf_adopted_split_operation_transform_other(InfAdoptedSplitOperation* op,
                                            InfAdoptedOperation* other,
                                            gint concurrency_id)
{
  InfAdoptedSplitOperationPrivate* priv;
  InfAdoptedOperation* tmp;
  InfAdoptedOperation* new_second;
  InfAdoptedOperation* result;

  g_return_val_if_fail(INF_ADOPTED_IS_SPLIT_OPERATION(op), NULL);
  g_return_val_if_fail(INF_ADOPTED_IS_OPERATION(other), NULL);

  priv = INF_ADOPTED_SPLIT_OPERATION_PRIVATE(op);

  tmp = inf_adopted_operation_transform(other, priv->first, concurrency_id);
  new_second = inf_adopted_operation_transform(priv->second, priv->first, 0);
  result = inf_adopted_operation_transform(tmp, new_second, concurrency_id);

  g_object_unref(G_OBJECT(tmp));
  g_object_unref(G_OBJECT(new_second));
  return result;
}

/* vim:set et sw=2 ts=2: */
