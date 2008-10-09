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

#include <infinoted/infinoted-signal.h>

static InfinotedRun* _infinoted_signal_server = NULL;

static void
infinoted_signal_sigint_handler(int sig)
{
  printf("\n");
  if(_infinoted_signal_server != NULL)
    infinoted_run_stop(_infinoted_signal_server);
}

static void
infinoted_signal_sigterm_handler(int sig)
{
  printf("\n");
  if(_infinoted_signal_server != NULL)
    infinoted_run_stop(_infinoted_signal_server);
}

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
  signal(SIGINT, sig->previous_sigint_handler);
  signal(SIGTERM, sig->previous_sigterm_handler);
  _infinoted_signal_server = NULL;
  g_slice_free(InfinotedSignal, sig);
}
