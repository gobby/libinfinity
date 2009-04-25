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

#include <libinfinity/adopted/inf-adopted-concurrency-warning.h>
#include <libinfinity/inf-i18n.h>

/* This function is only used internally. Please don't wrap it for language
 * bindings. */
void
_inf_adopted_concurrency_warning(GType for_type)
{
  g_warning(
    _("%s was called for %s. This means that you hit an unforseen "
      "situation in libinfinity. The session could become inconsistent "
      "because of this. If you were using Gobby, please notify me (Armin "
      "Burgmeier, armin@arbur.net) about this, and attach the contents of "
      "the ~/.infinote-records folder, or just the record of the session "
      "that triggered this error if you know which one. I hope I can fix "
      "this problem with that information in future versions."),
    "get_concurrency_id",
    g_type_name(for_type)
  );
}

/* vim:set et sw=2 ts=2: */
