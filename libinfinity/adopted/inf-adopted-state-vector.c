/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008, 2009 Armin Burgmeier <armin@arbur.net>
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
 * SECTION:inf-adopted-state-vector
 * @title: InfAdoptedStateVector
 * @short_description: Represents a state in the interaction model
 * @see_also: #InfAdoptedAlgorithm
 * @include: libinfinity/adopted/inf-adopted-state-vector.h
 * @stability: Unstable
 *
 * The #InfAdoptedStateVector represents a state in the current state space.
 * It basically maps user IDs to operation counts and states how many
 * operations of the corresponding user have already been performed.
 **/

#include <libinfinity/adopted/inf-adopted-state-vector.h>
#include <libinfinity/inf-i18n.h>

#include <glib.h>
#include <stdlib.h>
#include <string.h>

/* NOTE: What the state vector actually counts is the amount of operations
 * performed by each user. This number is called a timestamp, although it has
 * nothing to do with actual time. */

typedef struct _InfAdoptedStateVectorComponent InfAdoptedStateVectorComponent;
struct _InfAdoptedStateVectorComponent {
  guint id;
  guint n; /* timestamp */
};

typedef struct _InfAdoptedStateVectorForeachData
  InfAdoptedStateVectorForeachData;

struct _InfAdoptedStateVectorForeachData {
  InfAdoptedStateVectorForeachFunc func;
  gpointer user_data;
};


struct _InfAdoptedStateVector {
  gsize size;
  gsize max_size;
  InfAdoptedStateVectorComponent* data;
};

static gsize
inf_adopted_state_vector_find_insert_pos(InfAdoptedStateVector* vec,
                                         guint id)
{
  gsize begin;
  gsize end;
  gsize middle;
  InfAdoptedStateVectorComponent* comp;

  if(vec->size == 0) return 0;

  begin = 0;
  end = vec->size;

  /* The vector is sorted, so we perform a binary search */
  while(begin != end)
  {
    middle = begin + (end - begin) / 2;
    comp = vec->data + middle;
    if (comp->id == id)
    {
      return middle;
    }

    if (comp->id < id)
    {
      begin = middle + 1;
    }
    else
    {
      end = middle;
    }
  }
  return begin;
}

static InfAdoptedStateVectorComponent*
inf_adopted_state_vector_lookup(InfAdoptedStateVector* vec,
                                      guint id)
{
  gsize pos;

  pos = inf_adopted_state_vector_find_insert_pos(vec, id);
  if(pos < vec->size && vec->data[pos].id == id)
    return vec->data + pos;
  return NULL;
}

static InfAdoptedStateVectorComponent*
inf_adopted_state_vector_insert(InfAdoptedStateVector* vec,
                                guint id,
                                guint value,
                                gsize insert_pos)
{
  InfAdoptedStateVectorComponent* comp;

  if(vec->max_size <= vec->size)
  {
    vec->max_size += 5;
    vec->data = g_realloc(vec->data,
                vec->max_size * sizeof(InfAdoptedStateVectorComponent));
  }

  comp = vec->data + insert_pos;
  if(insert_pos < vec->size)
  {
    gsize move_count;

    g_assert(comp->id != id);

    move_count = (vec->size - insert_pos);
    g_memmove(comp + 1, comp,
              move_count * sizeof(InfAdoptedStateVectorComponent));
  }

  ++vec->size;
  comp->id = id;
  comp->n = value;

  return comp;
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

/**
 * inf_adopted_state_vector_error_quark:
 *
 * The domain for #InfAdoptedStateVectorError errors.
 *
 * Returns: A #GQuark for that domain.
 **/
GQuark
inf_adopted_state_vector_error_quark(void)
{
  return g_quark_from_static_string("INF_ADOPTED_STATE_VECTOR_ERROR");
}

/**
 * inf_adopted_state_vector_new:
 *
 * Returns a new state vector with all components set to zero.
 *
 * Return Value: A new #InfAdoptedStateVector.
 **/
InfAdoptedStateVector*
inf_adopted_state_vector_new(void)
{
  InfAdoptedStateVector* vec;

  vec = g_slice_new(InfAdoptedStateVector);
  vec->size = 0;
  vec->max_size = 0;
  vec->data = NULL;

  return vec;
}

/**
 * inf_adopted_state_vector_copy:
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

  new_vec = g_slice_new(InfAdoptedStateVector);
  new_vec->size = vec->size;
  new_vec->max_size = vec->max_size;

  if(new_vec->max_size == 0)
  {
    new_vec->data = NULL;
  }
  else
  {
    new_vec->data =
      g_malloc(new_vec->max_size * sizeof(InfAdoptedStateVectorComponent));
    memcpy(new_vec->data, vec->data,
           new_vec->size * sizeof(InfAdoptedStateVectorComponent));
  }

  return new_vec;
}

/**
 * inf_adopted_state_vector_free:
 * @vec: A #InfAdoptedStateVector.
 *
 * Frees a state vector allocated by inf_adopted_state_vector_new() or
 * inf_adopted_state_vector_copy().
 **/
void
inf_adopted_state_vector_free(InfAdoptedStateVector* vec)
{
  g_return_if_fail(vec != NULL);

  g_free(vec->data);
  g_slice_free(InfAdoptedStateVector, vec);
}

/**
 * inf_adopted_state_vector_get:
 * @vec: A #InfAdoptedStateVector.
 * @id: The component whose timestamp to look for.
 *
 * Returns the timestamp for the given component. Implicitely, all IDs
 * that the vector does not contain are assigned the timestamp 0.
 *
 * Return Value: The @component'th entry in the vector.
 */
guint
inf_adopted_state_vector_get(InfAdoptedStateVector* vec,
                             guint id)
{
  InfAdoptedStateVectorComponent* comp;

  g_return_val_if_fail(vec != NULL, 0);

  comp = inf_adopted_state_vector_lookup(vec, id);

  if(comp == NULL)
    return 0;

  return comp->n;
}

/**
 * inf_adopted_state_vector_set:
 * @vec: A #InfAdoptedStateVector.
 * @id: The component to change.
 * @value: The value to set the component to.
 *
 * Sets the given component of @vec to @value.
 **/
void
inf_adopted_state_vector_set(InfAdoptedStateVector* vec,
                             guint id,
                             guint value)
{
  gsize pos;

  g_return_if_fail(vec != NULL);

  pos = inf_adopted_state_vector_find_insert_pos(vec, id);
  if(pos < vec->size && vec->data[pos].id == id)
    vec->data[pos].n = value;
  else
    inf_adopted_state_vector_insert(vec, id, value, pos);
}

/**
 * inf_adopted_state_vector_add:
 * @vec: A #InfAdoptedStateVector.
 * @id: The component to change.
 * @value: The value by which to change the component.
 *
 * Adds @value to the current value of @component. @value may be negative in
 * which case the current value is actually decreased. Make sure to not drop
 * below zero this way.
 **/
void
inf_adopted_state_vector_add(InfAdoptedStateVector* vec,
                             guint id,
                             gint value)
{
  InfAdoptedStateVectorComponent* comp;
  gsize pos;

  g_return_if_fail(vec != NULL);

  pos = inf_adopted_state_vector_find_insert_pos(vec, id);
  comp = vec->data + pos;
  if(pos == vec->size || comp->id != id)
  {
    g_assert(value > 0);
    comp = inf_adopted_state_vector_insert(vec, id, value, pos);
  }
  else
  {
    g_assert(value > 0 || comp->n >= (guint)-value);

    comp->n += value;
  }
}

/**
 * inf_adopted_state_vector_foreach:
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
  gsize pos;

  g_return_if_fail(vec != NULL);
  g_return_if_fail(func != NULL);

  for(pos = 0; pos < vec->size; ++pos)
  {
    func(vec->data[pos].id, vec->data[pos].n, user_data);
  }
}

/**
 * inf_adopted_state_vector_compare:
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
  gsize first_pos;
  gsize second_pos;
  InfAdoptedStateVectorComponent* first_comp;
  InfAdoptedStateVectorComponent* second_comp;

  g_return_val_if_fail(first != NULL, 0);
  g_return_val_if_fail(second != NULL, 0);

  first_pos = 0;
  second_pos = 0;

  /* TODO: Some test that verifies that this function
   * provides strict weak ordering */

  for(;;)
  {
    /* Jump over components whose value is 0. This is necessary because
     * components that are not in the sequence are treated like having the
     * value zero and should be compared equal. */
    while(first_pos < first->size)
    {
      first_comp = first->data + first_pos;
      if(first_comp->n > 0)
        break;
      ++first_pos;
    }
    
    while(second_pos < second->size)
    {
      second_comp = second->data + second_pos;
      if(second_comp->n > 0)
        break;
      ++second_pos;
    }

    if(first_pos == first->size || second_pos == second->size)
    {
      break;
    }

    /* first_comp and second_comp are set here */

    if(first_comp->id < second_comp->id)
    {
      return -1;
    }
    else if(first_comp->id > second_comp->id)
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

    ++first_pos;
    ++second_pos;
  }

  if(first_pos == first->size && second_pos == second->size)
  {
    return 0; 
  }
  else if(first_pos == first->size)
  {
    return -1;
  }
  else
  {
    return 1;
  }
}

/**
 * inf_adopted_state_vector_causally_before:
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
  gsize first_pos;
  gsize second_pos;
  InfAdoptedStateVectorComponent* first_comp;
  InfAdoptedStateVectorComponent* second_comp;

  g_return_val_if_fail(first != NULL, FALSE);
  g_return_val_if_fail(second != NULL, FALSE);

  first_pos = 0;
  second_pos = 0;

  while(first_pos < first->size)
  {
    first_comp = first->data + first_pos++;

    if(second_pos == second->size)
    {
      /* That component is not contained in second (thus 0) */
      if(first_comp->n > 0)
        return FALSE;
    }
    else
    {
      second_comp = second->data + second_pos;
      while(second_comp != NULL && first_comp->id > second_comp->id)
      {
        ++second_pos;
        if(second_pos != second->size)
          second_comp = second->data + second_pos;
        else
          second_comp = NULL;
      }

      if(second_comp == NULL || first_comp->id < second_comp->id)
      {
        /* That component is not contained in second (thus 0) */
        if(first_comp->n > 0)
          return FALSE;
        else
          /* 0 <= 0 */
          continue;
      }

      g_assert(first_comp->id == second_comp->id);
      if(first_comp->n > second_comp->n)
        return FALSE;
    }
  }

  return TRUE;
}

/**
 * inf_adopted_state_vector_causally_before_inc:
 * @first: A #InfAdoptedStateVector.
 * @second: Another #InfAdoptedStateVector.
 * @inc_component: The component to increment before comparing.
 *
 * This function does the equivalent of
 *
 * |[
 * inf_adopted_state_vector_add(first, inc_component, 1);
 * gboolean result = inf_adopted_state_vector_causally_before(first, second);
 * inf_adopted_state_vector_add(first, inc_component, -1);
 * return result;
 * ]|
 *
 * But it is more efficient.
 *
 * Return Value: Whether @second depends on @first with the
 * @inc_component<!-- -->th component increased by one.
 **/
gboolean
inf_adopted_state_vector_causally_before_inc(InfAdoptedStateVector* first,
                                             InfAdoptedStateVector* second,
                                             guint inc_component)
{
  gsize first_pos;
  gsize second_pos;
  gboolean inc_comp_seen;

  InfAdoptedStateVectorComponent* first_comp;
  InfAdoptedStateVectorComponent* second_comp;
  InfAdoptedStateVectorComponent inc_comp;

  g_return_val_if_fail(first != NULL, FALSE);
  g_return_val_if_fail(second != NULL, FALSE);

  first_pos = 0;
  second_pos = 0;
  inc_comp.id = inc_component;
  inc_comp_seen = FALSE;

  while(first_pos < first->size || !inc_comp_seen)
  {
    /* Set first_comp as if there was inc_component increased by one */
    if(!inc_comp_seen)
    {
      if(first_pos < first->size)
      {
        first_comp = first->data + first_pos;
        if(first_comp->id < inc_component)
        {
          ++ first_pos;
        }
        else if(first_comp->id == inc_component)
        {
          inc_comp.n = first_comp->n + 1;
          first_comp = &inc_comp;
          inc_comp_seen = TRUE;
          ++ first_pos;
        }
        else
        {
          inc_comp.n = 1;
          first_comp = &inc_comp;
          inc_comp_seen = TRUE;
        }
      }
      else
      {
        /* inc_comp is the only component of first */
        inc_comp.n = 1;
        first_comp = &inc_comp;
        inc_comp_seen = TRUE;
      }
    }
    else
    {
      /* inc_comp already handled, business as usual */
      first_comp = first->data + first_pos;
      ++ first_pos;
    }

    if(second_pos == second->size)
    {
      /* That component is not contained in second (thus 0) */
      if(first_comp->n > 0)
        return FALSE;
    }
    else
    {
      second_comp = second->data + second_pos;
      while(second_comp != NULL && first_comp->id > second_comp->id)
      {
        ++second_pos;
        if(second_pos != second->size)
          second_comp = second->data + second_pos;
        else
          second_comp = NULL;
      }

      if(second_comp == NULL || first_comp->id < second_comp->id)
      {
        /* That component is not contained in second (thus 0) */
        if(first_comp->n > 0)
          return FALSE;
        else
          /* 0 <= 0 */
          continue;
      }

      g_assert(first_comp->id == second_comp->id);
      if(first_comp->n > second_comp->n)
        return FALSE;
    }
  }

  return TRUE;
}

/**
 * inf_adopted_state_vector_vdiff:
 * @first: A #InfAdoptedStateVector.
 * @second: Another #InfAdoptedStateVector.
 *
 * This function returns the sum of the differences between each component
 * of @first and @second. This function can only be called if
 * inf_adopted_state_vector_causally_before() returns %TRUE.
 *
 * Returns: The sum of the differences between each component of @first and
 * @second.
 */
guint
inf_adopted_state_vector_vdiff(InfAdoptedStateVector* first,
                               InfAdoptedStateVector* second)
{
  gsize n;
  guint first_sum;
  guint second_sum;

  g_return_val_if_fail(
    inf_adopted_state_vector_causally_before(first, second) == TRUE,
    0
  );

  first_sum = 0;
  second_sum = 0;

  for(n = 0; n < first->size; ++ n)
    first_sum += first->data[n].n;
  for(n = 0; n < second->size; ++ n)
    second_sum += second->data[n].n;

  g_assert(second_sum >= first_sum);
  return second_sum - first_sum;
}

/**
 * inf_adopted_state_vector_to_string:
 * @vec: A #InfAdoptedStateVector.
 *
 * Returns a string representation of @vec.
 *
 * Return Value: A newly-allocated string to be freed by the caller.
 **/
gchar*
inf_adopted_state_vector_to_string(InfAdoptedStateVector* vec)
{
  GString* str;
  gsize pos;
  InfAdoptedStateVectorComponent* component;

  g_return_val_if_fail(vec != NULL, NULL);

  str = g_string_sized_new(vec->size * 12);

  for(pos = 0; pos < vec->size; ++pos)
  {
    component = vec->data + pos;

    if(component->n > 0)
    {
      if(str->len > 0)
        g_string_append_c(str, ';');

      g_string_append_printf(str, "%u:%u", component->id, component->n);
    }
  }

  return g_string_free(str, FALSE);
}

/**
 * inf_adopted_state_vector_from_string.
 * @str: A string representation of a #InfAdoptedStateVector.
 * @error: Location to place an error, if any.
 *
 * Recreates the #InfAdoptedStateVector from its string representation. If
 * an error occurs, the function returns %NULL and @error is set.
 *
 * Return Value: A new #InfAdoptedStateVector, or %NULL.
 **/
InfAdoptedStateVector*
inf_adopted_state_vector_from_string(const gchar* str,
                                     GError** error)
{
  InfAdoptedStateVector* vec;
  const char* strpos;
  gsize pos;
  guint id;
  guint n;

  g_return_val_if_fail(str != NULL, NULL);

  vec = inf_adopted_state_vector_new();
  strpos = str;

  while(*strpos)
  {
    id = strtoul(strpos, (char**)&strpos, 10);
    if(*strpos != ':')
    {
      g_set_error(
        error,
        inf_adopted_state_vector_error_quark(),
        INF_ADOPTED_STATE_VECTOR_BAD_FORMAT,
        _("Expected ':' after ID")
      );

      inf_adopted_state_vector_free(vec);
      return NULL;
    }

    pos = inf_adopted_state_vector_find_insert_pos(vec, id);
    if(pos < vec->size && vec->data[pos].id == id)
    {
      g_set_error(
        error,
        inf_adopted_state_vector_error_quark(),
        INF_ADOPTED_STATE_VECTOR_BAD_FORMAT,
        _("ID '%u' already occured before"),
        id
      );

      inf_adopted_state_vector_free(vec);
      return NULL;
    }

    ++ strpos; /* step over ':' */
    n = strtoul(strpos, (char**)&strpos, 10);

    if(*strpos != ';' && *strpos != '\0')
    {
      g_set_error(
        error,
        inf_adopted_state_vector_error_quark(),
        INF_ADOPTED_STATE_VECTOR_BAD_FORMAT,
        _("Expected ';' or end of string after component of ID '%u'"),
        id
      );

      inf_adopted_state_vector_free(vec);
      return NULL;
    }

    inf_adopted_state_vector_insert(vec, id, n, pos);
    if(*strpos != '\0') ++ strpos; /* step over ';' */
  }

  return vec;
}

/**
 * inf_adopted_state_vector_to_string_diff:
 * @vec: A #InfAdoptedStateVector.
 * @orig: Another #InfAdoptedStateVector.
 *
 * Returns the string representation of a diff between @orig and @vec. This
 * is possibly smaller than the representation created by
 * inf_adopted_state_vector_to_string(), but the same @orig vector is needed
 * to recreate @vec from the string representation. Additionally,
 * inf_adopted_state_vector_causally_before(@orig, @vec) must hold.
 *
 * Return Value: A newly allocated string to be freed by the caller.
 **/
gchar*
inf_adopted_state_vector_to_string_diff(InfAdoptedStateVector* vec,
                                        InfAdoptedStateVector* orig)
{
  gsize vec_pos;
  gsize orig_pos;
  InfAdoptedStateVectorComponent* vec_comp;
  InfAdoptedStateVectorComponent* orig_comp;
  GString* str;

  g_return_val_if_fail(vec != NULL, NULL);
  g_return_val_if_fail(orig != NULL, NULL);

  g_return_val_if_fail(
    inf_adopted_state_vector_causally_before(orig, vec) == TRUE,
    NULL
  );

  str = g_string_sized_new(vec->size * 12);
  vec_pos = 0;

  for(orig_pos = 0; orig_pos < orig->size; ++orig_pos)
  {
    orig_comp = orig->data + orig_pos;
    if(orig_comp->n == 0) continue;

    g_assert(vec_pos != vec->size);
    vec_comp = vec->data + vec_pos;
    while(vec_comp->id < orig_comp->id)
    {
      /* There does not seem to be a corresponding entry in orig_comp, so
       * it is implicitely zero. */
      if(str->len > 0) g_string_append_c(str, ';');
      g_string_append_printf(str, "%u:%u", vec_comp->id, vec_comp->n);

      ++vec_pos;

      g_assert(vec_pos != vec->size);
      vec_comp = vec->data + vec_pos;
    }

    /* Otherwise the inf_adopted_state_vector_causally_before test above
     * should not have passed since orig_comp->n is not 0. */
    g_assert(vec_comp->id == orig_comp->id);
    g_assert(vec_comp->n >= orig_comp->n);

    if(vec_comp->n > orig_comp->n)
    {
      if(str->len > 0) g_string_append_c(str, ';');
      g_string_append_printf(
        str,
        "%u:%u",
        vec_comp->id,
        vec_comp->n - orig_comp->n
      );
    }

    ++vec_pos;
  }

  /* All remaining components in vec have no counterpart in orig, meaning
   * their values in orig are implicitely zero. */
  while(vec_pos != vec->size)
  {
    vec_comp = vec->data + vec_pos;
    if (vec_comp->n > 0)
    {
      if(str->len > 0) g_string_append_c(str, ';');
      g_string_append_printf(str, "%u:%u", vec_comp->id, vec_comp->n);
    }

    ++vec_pos;
  }

  return g_string_free(str, FALSE);
}

/**
 * inf_adopted_state_vector_from_string_diff:
 * @str: A string representation of a diff between state vectors.
 * @orig: The state vector used to create @str in
 * inf_adopted_state_vector_to_string_diff().
 * @error: Location to place an error, if any.
 *
 * Recreates a vector from its string representation diff and the original
 * vector. If an error returns, the function returns %NULL and @error is set.
 *
 * Return Value:
 **/
InfAdoptedStateVector*
inf_adopted_state_vector_from_string_diff(const gchar* str,
                                          InfAdoptedStateVector* orig,
                                          GError** error)
{
  InfAdoptedStateVector* vec;
  gsize vec_pos;
  gsize orig_pos;
  InfAdoptedStateVectorComponent* vec_comp;
  InfAdoptedStateVectorComponent* orig_comp;

  g_return_val_if_fail(str != NULL, NULL);
  g_return_val_if_fail(orig != NULL, NULL);

  vec = inf_adopted_state_vector_from_string(str, error);
  if(vec == NULL) return NULL;

  vec_pos = 0;

  for(orig_pos = 0; orig_pos < orig->size; ++orig_pos)
  {
    orig_comp = orig->data + orig_pos;
    if(orig_comp->n == 0) continue;

    if(vec_pos == vec->size)
    {
      inf_adopted_state_vector_insert(
        vec,
        orig_comp->id,
        orig_comp->n,
        vec_pos
      );
    }
    else
    {
      vec_comp = vec->data + vec_pos;
      while(vec_comp->id < orig_comp->id)
      {
        ++vec_pos;
        if(vec_pos < vec->size)
        {
          vec_comp = vec->data + vec_pos;
        }
        else
        {
          break;
        }
      }

      if(vec_comp->id == orig_comp->id)
      {
        vec_comp->n += orig_comp->n;
        ++vec_pos;
      }
      else
      {
        inf_adopted_state_vector_insert(
          vec,
          orig_comp->id,
          orig_comp->n,
          vec_pos
        );
      }
    }
  }

  return vec;
}

/* vim:set et sw=2 ts=2: */
