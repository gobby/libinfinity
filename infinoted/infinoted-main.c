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

#include <infinoted/infinoted-signal.h>
#include <infinoted/infinoted-run.h>
#include <infinoted/infinoted-startup.h>

#include <libinfinity/inf-i18n.h>

#include <locale.h>

static gboolean
infinoted_main_run(InfinotedStartup* startup,
                   GError** error)
{
  InfinotedRun* run;
  InfinotedSignal* sig;
  gboolean result;

  run = infinoted_run_new(startup, error);
  if(run == NULL) return FALSE;

  sig = infinoted_signal_register(run);

  /* Now start the server. It can later be stopped by signals. */
  result = infinoted_run_start(run, error);

  infinoted_signal_unregister(sig);
  infinoted_run_free(run);

  return result;
}

static gboolean
infinoted_main(int argc,
               char* argv[],
               GError** error)
{
  InfinotedStartup* startup;
  gboolean result;

  startup = infinoted_startup_new(&argc, &argv, error);

  if(startup == NULL)
    return FALSE;

  result = infinoted_main_run(startup, error);
  infinoted_startup_free(startup);

  return result;
}

int
main(int argc,
     char* argv[])
{
  GError* error;

  setlocale(LC_ALL, "");

  error = NULL;
  if(infinoted_main(argc, argv, &error) == FALSE)
  {
    fprintf(stderr, "%s\n", error->message);
    g_error_free(error);
    return -1;
  }

  return 0;
}

/* vim:set et sw=2 ts=2: */
