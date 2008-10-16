/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INFINOTED_RUN_H__
#define __INFINOTED_RUN_H__

#include <infinoted/infinoted-startup.h>

#include <libinfinity/server/infd-server-pool.h>
#include <libinfinity/server/infd-directory.h>
#include <libinfinity/common/inf-standalone-io.h>

#include <glib.h>

G_BEGIN_DECLS

typedef struct _InfinotedRun InfinotedRun;
struct _InfinotedRun {
  InfStandaloneIo* io;
  InfdDirectory* directory;
  InfdServerPool* pool;
};

InfinotedRun*
infinoted_run_new(InfinotedStartup* startup,
                  GError** error);

void
infinoted_run_free(InfinotedRun* run);

void
infinoted_run_start(InfinotedRun* run);

void
infinoted_run_stop(InfinotedRun* run);

G_END_DECLS

#endif /* __INFINOTED_RUN_H__ */

/* vim:set et sw=2 ts=2: */
