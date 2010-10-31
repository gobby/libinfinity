/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2010 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INFINOTED_SIGNAL_H__
#define __INFINOTED_SIGNAL_H__

#include <infinoted/infinoted-run.h>
#include <libinfinity/inf-config.h>

#include <glib.h>

#include <signal.h>

G_BEGIN_DECLS

/* sighandler_t seems not to be defined for some reason */
typedef void(*InfinotedSignalFunc)(int);

typedef struct _InfinotedSignal InfinotedSignal;
struct _InfinotedSignal {
#ifdef LIBINFINITY_HAVE_LIBDAEMON
  InfinotedRun* run;
  int signal_fd;
  InfIoWatch* watch;
#else
  InfinotedSignalFunc previous_sigint_handler;
  InfinotedSignalFunc previous_sigterm_handler;
  InfinotedSignalFunc previous_sigquit_handler;
  InfinotedSignalFunc previous_sighup_handler;
#endif
};

InfinotedSignal*
infinoted_signal_register(InfinotedRun* run);

void
infinoted_signal_unregister(InfinotedSignal* sig);

G_END_DECLS

#endif /* __INFINOTED_SIGNAL_H__ */

/* vim:set et sw=2 ts=2: */
