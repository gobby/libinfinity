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

/**
 * SECTION:inf-browser-iter
 * @title: InfBrowserIter
 * @short_description: Iterating through browsed nodes
 * @see_also: #InfBrowser
 * @include: libinfinity/common/inf-browser-iter.h
 * @stability: Unstable
 *
 * #InfBrowserIter is used to iterate through the nodes of a #InfBrowser.
 * Normally, #InfBrowserIter is allocated on the stack and
 * initialized by a #InfBrowser API call, such as inf_browser_get_root(). You
 * can also safely copy the struct by value to create a copy. It is not
 * necessary to free it.
 *
 * Most operations are done via the #InfBrowser API. The methods
 * described here could be useful to language bindings, however.
 *
 * An initialized #InfBrowserIter always points to a node within the
 * #InfBrowser. It stays valid as long as the node it points to is not
 * removed from the browser (if it is, the #InfBrowser::node-removed) signal
 * is emitted.
 **/

#include <libinfinity/common/inf-browser-iter.h>

G_DEFINE_BOXED_TYPE(InfBrowserIter, inf_browser_iter, inf_browser_iter_copy, inf_browser_iter_free)

/**
 * inf_browser_iter_copy:
 * @iter: A #InfBrowserIter.
 *
 * Makes a dynamically allocated copy of @iter. This should not be used by
 * applications because you can copy the structs by value.
 *
 * Return Value: (transfer full): A newly-allocated copy of @iter.
 */
InfBrowserIter*
inf_browser_iter_copy(const InfBrowserIter* iter)
{
  InfBrowserIter* new_iter;

  g_return_val_if_fail(iter != NULL, NULL);

  new_iter = g_slice_new(InfBrowserIter);
  *new_iter = *iter;

  return new_iter;
}

/**
 * inf_browser_iter_free:
 * @iter: A #InfBrowserIter.
 *
 * Frees a #InfBrowserIter allocated by inf_browser_iter_copy().
 **/
void
inf_browser_iter_free(InfBrowserIter* iter)
{
  g_return_if_fail(iter != NULL);

  g_slice_free(InfBrowserIter, iter);
}

/* vim:set et sw=2 ts=2: */
