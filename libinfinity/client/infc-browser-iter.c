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
 * SECTION:infc-browser-iter
 * @title: InfcBrowserIter
 * @short_description: Iterating through browsed nodes
 * @see_also: #InfcBrowser
 * @include: libinfinity/client/infc-browser-iter.h
 * @stability: Unstable
 *
 * The #InfcBrowserIter is used to iterate through the nodes of a
 * #InfcBrowser. Normally, #InfcBrowserIter is allocated on the stack and
 * initialized by a #InfcBrowser API call, such as
 * infc_browser_iter_get_root(). You can also safely copy the struct by
 * value to create a copy. It is not necessary to free it.
 *
 * Most operations are done via the #InfcBrowser API. These methods could be
 * useful to language bindings.
 *
 * An initialized #InfcBrowserIter always points to a node within the
 * #InfcBrowser. It stays valid as long as the node it points to is not
 * removed from the browser (if it is, the #InfcBrowser::node-removed) signal
 * is emitted.
 **/

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

/**
 * infc_browser_iter_copy:
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

/**
 * infc_browser_iter_free:
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

/* vim:set et sw=2 ts=2: */
