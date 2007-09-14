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

#include <libinftext/inf-text-pword.h>
#include <libinfinity/common/inf-xml-util.h>

#include <string.h>

/* TODO: Consider slice-allocating */

struct _InfTextPword {
  guint* data;
  guint size;
};

GType
inf_text_pword_get_type(void)
{
  static GType pword_type = 0;

  if(!pword_type)
  {
    pword_type = g_boxed_type_register_static(
      "InfTextPword",
      (GBoxedCopyFunc)inf_text_pword_copy,
      (GBoxedFreeFunc)inf_text_pword_free
    );
  }

  return pword_type;
}

/** inf_text_pword_new:
 *
 * @initial: Initial position of the pword.
 *
 * Creates a new #InfTextPword with @initial as initial position.
 *
 * Return Value: A new #InfTextPword.
 **/
InfTextPword*
inf_text_pword_new(guint initial)
{
  InfTextPword* pword = g_slice_new(InfTextPword);
  pword->data = GUINT_TO_POINTER(initial);
  pword->size = 1;

  return pword;
}

/** inf_text_pword_new_proceed:
 *
 * @pword: A #InfTextPword.
 * @next: Next position.
 *
 * Creates a new pword based on @pword with one more position being the new
 * current position and setting it to @next.
 *
 * Return Value: A new #InfTextPword.
 **/
InfTextPword*
inf_text_pword_new_proceed(InfTextPword* pword,
                           guint next)
{
  InfTextPword* new_pword;

  g_return_val_if_fail(pword != NULL, NULL);

  new_pword = g_slice_new(InfTextPword);

  if(pword->size > 1)
  {
    new_pword->data = g_malloc( (pword->size + 1) * sizeof(guint));
    new_pword->data[0] = next;
    memcpy(new_pword->data + 1, pword->data, pword->size * sizeof(guint));
    new_pword->size = pword->size + 1;
  }
  else
  {
    g_assert(pword->size == 1);
    new_pword->data = g_malloc(2 * sizeof(guint));
    new_pword->data[0] = next;
    new_pword->data[1] = GPOINTER_TO_UINT(pword->data);
    new_pword->size = 2;
  }

  return new_pword;
}

/** inf_text_pword_copy:
 *
 * @pword: A #InfTextPword.
 *
 * Creates a copy of @pword.
 *
 * Return Value: A new InfTextPword.
 **/
InfTextPword*
inf_text_pword_copy(InfTextPword* pword)
{
  InfTextPword* new_pword;

  g_return_val_if_fail(pword != NULL, NULL);

  new_pword = g_slice_new(InfTextPword);

  if(pword->size > 1)
  {
    new_pword->data = g_malloc(pword->size * sizeof(guint));
    memcpy(new_pword->data, pword->data, pword->size * sizeof(guint));
    new_pword->size = pword->size;
  }
  else
  {
    g_assert(pword->size == 1);
    new_pword->data = pword->data;
    new_pword->size = 1;
  }

  return new_pword;
}

/** inf_text_pword_free:
 *
 * @pword: A #InfTextPword.
 *
 * Frees a #InfTextPword.
 **/
void
inf_text_pword_free(InfTextPword* pword)
{
  g_return_if_fail(pword != NULL);

  if(pword->size > 1)
    g_free(pword->data);
  g_slice_free(InfTextPword, pword);
}

/** inf_text_pword_get_size:
 *
 * @pword: A #InfTextPword.
 *
 * Returns the number of positions @pword stores.
 *
 * Return Value: The size of @pword.
 **/
guint
inf_text_pword_get_size(InfTextPword* pword)
{
  g_return_val_if_fail(pword != NULL, 0);
  return pword->size;
}

/** inf_text_pword_get_current:
 *
 * @pword: A #InfTextPword.
 *
 * Returns the newest position in @pword.
 *
 * Return Value: The newest position in @pword.
 **/
guint
inf_text_pword_get_current(InfTextPword* pword)
{
  g_return_val_if_fail(pword != NULL, 0);
  if(pword->size > 1)
    return pword->data[0];
  else
    return GPOINTER_TO_UINT(pword->data);
}

/** inf_text_pword_get_origin:
 *
 * @pword: A #InfTextPword.
 *
 * Returns the oldest position in @pword.
 *
 * Return Value: The oldest position in @pword.
 **/
guint
inf_text_pword_get_origin(InfTextPword* pword)
{
  g_return_val_if_fail(pword != NULL, 0);
  if(pword->size > 1)
    return pword->data[pword->size - 1];
  else
    return GPOINTER_TO_UINT(pword->data);
}

/** inf_text_pword_compare:
 *
 * @first: A #InfTextPword.
 * @second: Another #InfTextPword.
 *
 * Returns -1 if @first compares before @second, 0 if they compare equal and
 * 1 if @first compares behind @second.
 *
 * Return Value: qsort()-style compare result.
 **/
int
inf_text_pword_compare(InfTextPword* first,
                       InfTextPword* second)
{
  guint i;

  g_return_val_if_fail(first != NULL, 0);
  g_return_val_if_fail(second != NULL, 0);

#define PWORD_CURRENT(pword) \
  ((pword->size > 1) ? (pword->data[0]) : (GPOINTER_TO_UINT(pword->data)))

  if(PWORD_CURRENT(first) < PWORD_CURRENT(second))
    return -1;
  if(PWORD_CURRENT(first) > PWORD_CURRENT(second))
    return 1;

  for(i = 1; i < first->size && i < second->size; ++ i)
  {
    if(first->data[i] < second->data[i])
      return -1;
    if(first->data[i] > second->data[i])
      return 1;
  }

  if(i == first->size && i < second->size)
    return 1;
  else if(i < first->size && i == second->size)
    return -1;
  else
    return 0;
}

/* vim:set et sw=2 ts=2: */
