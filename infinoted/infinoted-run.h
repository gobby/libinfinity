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

#ifndef __INFINOTED_RUN_H__
#define __INFINOTED_RUN_H__

#include <libinfinity/inf-config.h>

#include <infinoted/infinoted-startup.h>
#include <infinoted/infinoted-plugin-manager.h>

#include <libinfinity/server/infd-server-pool.h>
#include <libinfinity/server/infd-directory.h>
#include <libinfinity/common/inf-standalone-io.h>
#include <libinfinity/common/inf-discovery-avahi.h>

#include <glib.h>

G_BEGIN_DECLS

typedef struct _InfinotedRun InfinotedRun;
struct _InfinotedRun {
  InfinotedStartup* startup;

  InfStandaloneIo* io;
  InfdDirectory* directory;
  InfdServerPool* pool;

  InfinotedPluginManager* plugin_manager;

  InfdXmppServer* xmpp4;
  InfdXmppServer* xmpp6;
  gnutls_dh_params_t dh_params;

#ifdef LIBINFINITY_HAVE_AVAHI
  InfDiscoveryAvahi* avahi;
#endif
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
