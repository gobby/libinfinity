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

#ifndef __INF_KEEPALIVE_H__
#define __INF_KEEPALIVE_H__

#include <libinfinity/common/inf-native-socket.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_KEEPALIVE_MASK  (inf_keepalive_mask_get_type())
#define INF_TYPE_KEEPALIVE       (inf_keepalive_get_type())

/**
 * InfKeepaliveMask:
 * @INF_KEEPALIVE_ENABLED: Whether the keepalive mechanism is
 * explicitly enabled or disabled.
 * @INF_KEEPALIVE_TIME: Whether the keepalive time is
 * overriding the system default.
 * @INF_KEEPALIVE_INTERVAL: Whether the keepalive interval is
 * overriding the system default.
 * @INF_KEEPALIVE_ALL: All previous values combined.
 *
 * This bitmask specifies which of the fields in #InfKeepalive
 * override the system defaults. For fields that are not enabled in the
 * bitmask, the system default value is taken, and the corresponding field in
 * #InfKeepalive is ignored.
 */
typedef enum _InfKeepaliveMask {
  INF_KEEPALIVE_ENABLED = 1 << 0,
  INF_KEEPALIVE_TIME = 1 << 1,
  INF_KEEPALIVE_INTERVAL = 1 << 2,

  INF_KEEPALIVE_ALL = (1 << 3) - 1
} InfKeepaliveMask;

/**
 * InfKeepalive:
 * @mask: Which of the following settings are enabled. If a setting is
 * disabled, then the system default is taken.
 * @enabled: Whether sending keep-alive probes is enabled or not.
 * @time: Time in seconds after which to send keep-alive probes.
 * @interval: Time in seconds between keep-alive probes.
 *
 * This structure contains the settings to configure keep-alive on TCP
 * connections.
 */
typedef struct _InfKeepalive InfKeepalive;
struct _InfKeepalive {
  InfKeepaliveMask mask;
  gboolean enabled;
  guint time;
  guint interval;
};

/* TODO: Implement the stuff; make sure it works on linux, windows and mac: */
/* http://www.starquest.com/Supportdocs/techStarLicense/SL002_TCPKeepAlive.shtml */
/* MSDN... */
/* SOL_TCP, TCP_KEEPCNT (tcp_keepalive_probes), TCP_KEEPIDLE (tcp_keepalive_time), TCP_KEEPINTVL (tcp_keepalive_intvl) */
/* SOL_SOCKET, SO_KEEPALIVE */

GType
inf_keepalive_mask_get_type(void) G_GNUC_CONST;

GType
inf_keepalive_get_type(void) G_GNUC_CONST;

InfKeepalive*
inf_keepalive_copy(const InfKeepalive* keepalive);

void
inf_keepalive_free(InfKeepalive* keepalive);

gboolean
inf_keepalive_apply(const InfKeepalive* keepalive,
                    InfNativeSocket* socket,
                    InfKeepaliveMask current_mask,
                    GError** error);

void
inf_keepalive_load_default(InfKeepalive* keepalive,
                           InfKeepaliveMask mask);

G_END_DECLS

#endif /* __INF_KEEPALIVE_H__ */

/* vim:set et sw=2 ts=2: */
