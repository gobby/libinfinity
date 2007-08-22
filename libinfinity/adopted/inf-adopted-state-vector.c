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

#include <libinfinity/adopted/inf-adopted-state-vector.h>

/* NOTE: We do not reference the user for performance reasons. The user has to
 * make sure to keep the user alive while operating with the state vector.
 * This is ensured automatically if you use InfAdoptedAlgorithm becasue this
 * keeps a reference on all users it will use as state vector component. */

/* NOTE: What the state vector actually counts is the amount of operations
 * performed by each user. This number is called a timestamp, although it has
 * nothing to do with actual time. */

typedef struct _InfAdoptedStateVectorForeachData
  InfAdoptedStateVectorForeachData;

struct _InfAdoptedStateVectorForeachData {
  InfAdoptedStateVectorForeachFunc func;
  gpointer user_data;
};

typedef struct _InfAdoptedStateVectorComponent InfAdoptedStateVectorComponent;
struct _InfAdoptedStateVectorComponent {
  InfUser* user;
  guint n; /* timestamp */
};

static gint
inf_adopted_state_vector_component_cmp(InfAdoptedStateVectorComponent* comp1,
                                       InfAdoptedStateVectorComponent* comp2)
{
  if(comp1->user < comp2->user)
    return -1;
  if(comp1->user > comp2->user)
    return 1;
  return 0;
}

static void
inf_adopted_state_vector_component_free(InfAdoptedStateVectorComponent* comp)
{
  g_slice_free(InfAdoptedStateVectorComponent, comp);
}

static GSequenceIter*
inf_adopted_state_vector_lookup(InfAdoptedStateVector* vec,
                                InfUser* component)
{
  GSequenceIter* iter;
  InfAdoptedStateVectorComponent comp;

  comp.user = component;
  /* n is irrelevant for lookup */

  iter = g_sequence_search(
    vec,
    &comp,
    (GCompareDataFunc)inf_adopted_state_vector_component_cmp,
    NULL
  );

  /* Why the heck doesn't g_sequence_search return an iterator pointing to
   * the queried element if there is one, but behind? */
  if(iter == g_sequence_get_begin_iter(vec))
    return NULL;

  iter = g_sequence_iter_prev(iter);

  if(((InfAdoptedStateVectorComponent*)g_sequence_get(iter))->user == component)
    return iter;

  return NULL;
}

static GSequenceIter*
inf_adopted_state_vector_push(InfAdoptedStateVector* vec,
                              InfUser* component,
                              guint value)
{
  InfAdoptedStateVectorComponent* comp;
  comp = g_slice_new(InfAdoptedStateVectorComponent);

  comp->user = component;
  comp->n = value;

  return g_sequence_insert_sorted(
    vec,
    comp,
    (GCompareDataFunc)inf_adopted_state_vector_component_cmp,
    NULL
  );
}

static void
inf_adopted_state_vector_copy_func(gpointer data,
                                   gpointer user_data)
{
  InfAdoptedStateVectorComponent* comp;
  InfAdoptedStateVectorComponent* new_comp;

  comp = data;
  new_comp = g_slice_new(InfAdoptedStateVectorComponent);

  new_comp->user = comp->user;
  new_comp->n = comp->n;

  /* Since g_sequence_foreach traverses the original sequence in-order we
   * keep the sorting even though we only use append here. */
  g_sequence_append((GSequence*)user_data, new_comp);
}

static void
inf_adopted_state_vector_foreach_func(gpointer data,
                                      gpointer user_data)
{
  InfAdoptedStateVectorComponent* comp;
  InfAdoptedStateVectorForeachData* foreach_data;

  comp = (InfAdoptedStateVectorComponent*)data;
  foreach_data = (InfAdoptedStateVectorForeachData*)user_data;

  foreach_data->func(comp->user, comp->n, foreach_data->user_data);
}

GType
inf_adopted_state_vector_get_type(void)
{
  static GType state_vector_type = 0;

  if(!state_vector_type)
  {
    state_vector_type = g_boxed_type_register_static(
      "InfAdoptedStateVector",
      (GBoxedCopyFunc)inf_adopted_state_vector_copy,
      (GBoxedFreeFunc)inf_adopted_state_vector_free
    );
  }

  return state_vector_type;
}

/** inf_adopted_state_vector_new:
 *
 * Returns a new state vector with all components set to zero.
 *
 * Return Value: A new #InfAdoptedStateVector.
 **/
InfAdoptedStateVector*
inf_adopted_state_vector_new(void)
{
  return g_sequence_new(
    (GDestroyNotify)inf_adopted_state_vector_component_free
  );
}

/** inf_adopted_state_vector_copy:
 *
 * @vec: The #InfAdoptedStateVector to copy
 *
 * Returns a copy of @vec.
 *
 * Return Value: A copy of @vec.
 **/
InfAdoptedStateVector*
inf_adopted_state_vector_copy(InfAdoptedStateVector* vec)
{
  InfAdoptedStateVector* new_vec;

  g_return_val_if_fail(vec != NULL, NULL);

  new_vec = g_sequence_new(
    (GDestroyNotify)inf_adopted_state_vector_component_free
  );

  g_sequence_foreach(vec, inf_adopted_state_vector_copy_func, new_vec);
  return new_vec;
}

/** inf_adopted_state_vector_free:
 *
 * @vec: A #InfAdoptedStateVector.
 *
 * Frees a state vector allocated by inf_adopted_state_vector_new() or
 * inf_adopted_state_vector_copy().
 **/
void
inf_adopted_state_vector_free(InfAdoptedStateVector* vec)
{
  g_return_if_fail(vec != NULL);
  g_sequence_free(vec);
}

/** inf_adopted_state_vector_get:
 *
 * @vec: A #InfAdoptedStateVector.
 * @component: The component whose timestamp to look for.
 *
 * Returns the timestamp for the given component. Implicitely, all users
 * that the vector does not contain are assigned the timestamp 0.
 *
 * Return Value: The @component'th entry in the vector.
 */
guint
inf_adopted_state_vector_get(InfAdoptedStateVector* vec,
                             InfUser* component)
{
  GSequenceIter* iter;

  g_return_val_if_fail(vec != NULL, 0);
  g_return_val_if_fail(INF_IS_USER(component), 0);

  iter = inf_adopted_state_vector_lookup(vec, component);

  if(iter == NULL)
    return 0;

  return ((InfAdoptedStateVectorComponent*)g_sequence_get(iter))->n;
}

/** inf_adopted_state_vector_set:
 *
 * @vec: A #InfAdoptedStateVector.
 * @component: The component to change.
 * @value: The value to set the component to.
 *
 * Sets the given component of @vec to @value.
 **/
void
inf_adopted_state_vector_set(InfAdoptedStateVector* vec,
                             InfUser* component,
                             guint value)
{
  GSequenceIter* iter;

  g_return_if_fail(vec != NULL);
  g_return_if_fail(INF_IS_USER(component));

  iter = inf_adopted_state_vector_lookup(vec, component);
  if(iter == NULL)
    iter = inf_adopted_state_vector_push(vec, component, value);
  else
    ((InfAdoptedStateVectorComponent*)g_sequence_get(iter))->n = value;
}

/** inf_adopted_state_vector_add:
 *
 * @vec: A #InfAdoptedStateVector.
 * @component: The component to change.
 * @value: The value by which to change the component.
 *
 * Adds @value to the current value of @component. @value may be negative in
 * which case the current value is actually decreased. Make sure to not drop
 * below zero this way.
 **/
void
inf_adopted_state_vector_add(InfAdoptedStateVector* vec,
                             InfUser* component,
                             gint value)
{
  GSequenceIter* iter;
  InfAdoptedStateVectorComponent* comp;

  g_return_if_fail(vec != NULL);
  g_return_if_fail(INF_IS_USER(component));

  iter = inf_adopted_state_vector_lookup(vec, component);
  if(iter == NULL)
  {
    g_assert(value > 0);
    iter = inf_adopted_state_vector_push(vec, component, value);
  }
  else
  {
    comp = ((InfAdoptedStateVectorComponent*)g_sequence_get(iter));
    g_assert(value > 0 || comp->n > (guint)-value);

    comp->n += value;
  }
}

/** inf_adopted_state_vector_foreach:
 *
 * @vec: A #InfAdoptedStateVector.
 * @func: The function to call.
 * @user_data: Additional data to pass to @func.
 *
 * Calls @func for each component in @vec. Note that there may be users for
 * which @func will not be called if their timestamp is 0.
 **/
void
inf_adopted_state_vector_foreach(InfAdoptedStateVector* vec,
                                 InfAdoptedStateVectorForeachFunc func,
                                 gpointer user_data)
{
  InfAdoptedStateVectorForeachData data;

  g_return_if_fail(vec != NULL);
  g_return_if_fail(func != NULL);

  data.func = func;
  data.user_data = user_data;

  g_sequence_foreach(vec, inf_adopted_state_vector_foreach_func, &data);
}

/** inf_adopted_state_vector_compare:
 *
 * @first: A #InfAdoptedStateVector.
 * @second: Another #InfAdoptedStateVector.
 *
 * Performs a comparison suited for strict-weak ordering so that state vectors
 * can be sorted. This function returns -1 if @first compares before @second,
 * 0 if they compare equal and 1 if @first compares after @second.
 *
 * Return Value: -1, 0 or 1.
 **/
int
inf_adopted_state_vector_compare(InfAdoptedStateVector* first,
                                 InfAdoptedStateVector* second)
{
  GSequenceIter* first_iter;
  GSequenceIter* second_iter;
  InfAdoptedStateVectorComponent* first_comp;
  InfAdoptedStateVectorComponent* second_comp;

  g_return_val_if_fail(first != NULL, 0);
  g_return_val_if_fail(second != NULL, 0);

  /* TODO: Some test that verifies that this function
   * provides strict weak ordering */

  first_iter = g_sequence_get_begin_iter(first);
  second_iter = g_sequence_get_begin_iter(second);

  for(;;)
  {
    /* Jump over components whose value is 0. This is necessary because
     * components that are not in the sequence are treated like having the
     * value zero and should be compared equal. */
    while(first_iter != g_sequence_get_end_iter(first))
    {
      first_comp = g_sequence_get(first_iter);
      if(first_comp->n > 0)
        break;

      first_iter = g_sequence_iter_next(first_iter);
    }

    while(second_iter != g_sequence_get_end_iter(second))
    {
      second_comp = g_sequence_get(second_iter);
      if(second_comp->n > 0)
        break;

      second_iter = g_sequence_iter_next(second_iter);
    }

    if(first_iter == g_sequence_get_end_iter(first) ||
       second_iter == g_sequence_get_end_iter(second))
    {
      break;
    }

    /* first_comp and second_comp are set here */

    if(first_comp->user < second_comp->user)
    {
      return -1;
    }
    else if(first_comp->user > second_comp->user)
    {
      return 1;
    }
    else if(first_comp->n < second_comp->n)
    {
      return -1;
    }
    else if(first_comp->n > second_comp->n)
    {
      return 1;
    }

    /* Component matches, check next */

    first_iter = g_sequence_iter_next(first_iter);
    second_iter = g_sequence_iter_next(second_iter);
  }

  if(first_iter == g_sequence_get_end_iter(first) &&
     second_iter == g_sequence_get_end_iter(second))
  {
    return 0; 
  }
  else if(first_iter == g_sequence_get_end_iter(first))
  {
    return -1;
  }
  else
  {
    return 1;
  }
}

/** inf_adopted_state_vector_causally_before:
 *
 * @first: A #InfAdoptedStateVector.
 * @second: Another #InfAdoptedStateVector.
 *
 * Checks whether an event that occured at time @second is causally
 * dependant on an event that occured at time @first, that is all
 * components of @first are less or equal to the corresponding component in
 * @second.
 *
 * Return Value: Whether @second depends on @first.
 **/
gboolean
inf_adopted_state_vector_causally_before(InfAdoptedStateVector* first,
                                         InfAdoptedStateVector* second)
{
  GSequenceIter* first_iter;
  GSequenceIter* second_iter;
  InfAdoptedStateVectorComponent* first_comp;
  InfAdoptedStateVectorComponent* second_comp;

  g_return_val_if_fail(first != NULL, FALSE);
  g_return_val_if_fail(second != NULL, FALSE);

  first_iter = g_sequence_get_begin_iter(first);
  second_iter = g_sequence_get_begin_iter(second);

  while(first_iter != g_sequence_get_end_iter(first))
  {
    first_comp = g_sequence_get(first_iter);
    first_iter = g_sequence_iter_next(first_iter);

    if(second_iter == g_sequence_get_end_iter(second))
    {
      /* That component is not contained in second (thus 0) */
      if(first_comp->n > 0)
        return FALSE;
    }
    else
    {
      second_comp = g_sequence_get(second_iter);
      while(second_comp != NULL && first_comp->user > second_comp->user)
      {
        second_iter = g_sequence_iter_next(second_iter);
        if(second_iter != g_sequence_get_end_iter(second))
          second_comp = g_sequence_get(second_iter);
        else
          second_comp = NULL;
      }

      if(second_comp != NULL && first_comp->user < second_comp->user)
      {
        /* That component is not contained in second (thus 0) */
        if(first_comp->n > 0)
          return FALSE;
      }

      g_assert(first_comp->user == second_comp->user);
      if(first_comp->n > second_comp->n)
        return FALSE;
    }
  }

  return TRUE;
}

/* vim:set et sw=2 ts=2: */
