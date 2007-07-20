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

#include <libinfinity/client/infc-browser-iter.h>

GType
infc_browser_iter_get_type(void)
{
  static GType browser_iter_type = 0;

  if(!browser_iter_type)
  {
    browser_iter_type = g_boxed_type_register_static(
      "InfcBrowserIter",
      (GBoxedCopyFunc)infc_browser_iter_copy,
      (GBoxedFreeFunc)infc_browser_iter_free
    );
  }

  return browser_iter_type;
}

/** infc_browser_iter_copy:
 *
 * @iter: A #InfcBrowserIter.
 *
 * Makes a dynamically allocated copy of @iter. This should not be used by
 * applications because you can copy the structs by value.
 *
 * Return Value: A newly-allocated copy of @iter.
 */
InfcBrowserIter*
infc_browser_iter_copy(InfcBrowserIter* iter)
{
  InfcBrowserIter* new_iter;

  g_return_val_if_fail(iter != NULL, NULL);

  new_iter = g_slice_new(InfcBrowserIter);
  *new_iter = *iter;

  return new_iter;
}

/** infc_browser_iter_free:
 *
 * @iter: A #InfcBrowserIter.
 *
 * Frees a #InfcBrowserIter allocated by infc_browser_iter_copy().
 **/
void
infc_browser_iter_free(InfcBrowserIter* iter)
{
  g_return_if_fail(iter != NULL);

  g_slice_free(InfcBrowserIter, iter);
}
