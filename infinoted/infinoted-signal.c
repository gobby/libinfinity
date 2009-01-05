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

#include <infinoted/infinoted-signal.h>

#ifdef G_OS_WIN32
# include <windows.h>
#endif

static InfinotedRun* _infinoted_signal_server = NULL;

static void
infinoted_signal_terminate(void)
{
	InfinotedRun* run;

	if(_infinoted_signal_server != NULL)
	{
		run = _infinoted_signal_server;
		_infinoted_signal_server = NULL;

		infinoted_run_free(run);

		exit(0);
	}
}

static void
infinoted_signal_sigint_handler(int sig)
{
  printf("\n");
  infinoted_signal_terminate();
}

static void
infinoted_signal_sigterm_handler(int sig)
{
  printf("\n");
  infinoted_signal_terminate();
}

#ifdef G_OS_WIN32
BOOL WINAPI infinoted_signal_console_handler(DWORD fdwCtrlType)
{
  /* TODO: Don't terminate for CTRL_LOGOFF_EVENT? */
  infinoted_signal_terminate();
  /* Doesn't matter, we exit() anyway */
  return TRUE;
}
#endif

/**
 * infinoted_signal_register:
 * @run: A #InfinotedRun.
 *
 * Registers signal handlers for SIGINT and SIGTERM that terminate the given
 * infinote server. When you don't need the signal handlers anymore, you
 * must unregister them again using infinoted_signal_unregister().
 *
 * Returns: A #InfinotedSignal to unregister the signal handlers again later.
 */
InfinotedSignal*
infinoted_signal_register(InfinotedRun* run)
{
  InfinotedSignal* sig;
  sig = g_slice_new(InfinotedSignal);

  sig->previous_sigint_handler =
    signal(SIGINT, &infinoted_signal_sigint_handler);
  sig->previous_sigterm_handler =
    signal(SIGTERM, &infinoted_signal_sigterm_handler);

#ifdef G_OS_WIN32
  SetConsoleCtrlHandler(infinoted_signal_console_handler, TRUE);
#endif

  _infinoted_signal_server = run;
  return sig;
}

/**
 * infinoted_signal_unregister:
 * @sig: A #InfinotedSignal.
 *
 * Unregisters signal handlers registered with infinoted_signal_register().
 */
void
infinoted_signal_unregister(InfinotedSignal* sig)
{
#ifdef G_OS_WIN32
  SetConsoleCtrlHandler(infinoted_signal_console_handler, FALSE);
#endif

  signal(SIGINT, sig->previous_sigint_handler);
  signal(SIGTERM, sig->previous_sigterm_handler);
  _infinoted_signal_server = NULL;
  g_slice_free(InfinotedSignal, sig);
}

/* vim:set et sw=2 ts=2: */
