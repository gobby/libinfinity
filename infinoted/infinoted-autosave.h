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
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __INFINOTED_AUTOSAVE_H__
#define __INFINOTED_AUTOSAVE_H__

#include <libinfinity/server/infd-directory.h>

#include <glib.h>

G_BEGIN_DECLS

typedef struct _InfinotedAutosave InfinotedAutosave;
struct _InfinotedAutosave {
  InfdDirectory* directory;
  unsigned int autosave_interval;
  GSList* sessions;
};

InfinotedAutosave*
infinoted_autosave_new(InfdDirectory* directory,
                       unsigned int autosave_interval);

void
infinoted_autosave_free(InfinotedAutosave* autosave);

void
infinoted_autosave_save_immediately(InfinotedAutosave* autosave);

G_END_DECLS

#endif /* __INFINOTED_AUTOSAVE_H__ */

/* vim:set et sw=2 ts=2: */
