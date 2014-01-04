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

#ifndef __INF_DISCOVERY_AVAHI_H__
#define __INF_DISCOVERY_AVAHI_H__

#include <libinfinity/common/inf-xmpp-manager.h>
#include <libinfinity/common/inf-certificate-credentials.h>
#include <libinfinity/common/inf-io.h>
#include <libinfinity/inf-config.h> /* For LIBINFINITY_HAVE_AVAHI */

#include <libxml/tree.h>

#include <glib-object.h>

#ifdef LIBINFINITY_HAVE_AVAHI

G_BEGIN_DECLS

#define INF_TYPE_DISCOVERY_AVAHI                 (inf_discovery_avahi_get_type())
#define INF_DISCOVERY_AVAHI(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_DISCOVERY_AVAHI, InfDiscoveryAvahi))
#define INF_DISCOVERY_AVAHI_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TYPE_DISCOVERY_AVAHI, InfDiscoveryAvahiClass))
#define INF_IS_DISCOVERY_AVAHI(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_DISCOVERY_AVAHI))
#define INF_IS_DISCOVERY_AVAHI_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TYPE_DISCOVERY_AVAHI))
#define INF_DISCOVERY_AVAHI_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TYPE_DISCOVERY_AVAHI, InfDiscoveryAvahiClass))

typedef struct _InfDiscoveryAvahi InfDiscoveryAvahi;
typedef struct _InfDiscoveryAvahiClass InfDiscoveryAvahiClass;

/**
 * InfDiscoveryAvahiClass:
 *
 * This structure does not contain any public fields.
 */
struct _InfDiscoveryAvahiClass {
  /*< private >*/
  GObjectClass parent_class;
};

/**
 * InfDiscoveryAvahi:
 *
 * #InfDiscoveryAvahi is an opaque data type. You should only access it
 * via the public API functions.
 */
struct _InfDiscoveryAvahi {
  /*< private >*/
  GObject parent;
};

GType
inf_discovery_avahi_get_type(void) G_GNUC_CONST;

InfDiscoveryAvahi*
inf_discovery_avahi_new(InfIo* io,
                        InfXmppManager* manager,
                        InfCertificateCredentials* creds,
                        InfSaslContext* sasl_context,
                        const gchar* sasl_mechanisms);

void
inf_discovery_avahi_set_security_policy(InfDiscoveryAvahi* discovery,
                                        InfXmppConnectionSecurityPolicy plcy);

InfXmppConnectionSecurityPolicy
inf_discovery_avahi_get_security_policy(InfDiscoveryAvahi* discovery);

G_END_DECLS

#endif /* LIBINFINITY_HAVE_AVAHI */

#endif /* __INF_DISCOVERY_AVAHI_H__ */

/* vim:set et sw=2 ts=2: */
