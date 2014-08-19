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

#ifndef __INF_NAME_RESOLVER_H__
#define __INF_NAME_RESOLVER_H__

#include <libinfinity/common/inf-ip-address.h>
#include <libinfinity/common/inf-io.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_NAME_RESOLVER                 (inf_name_resolver_get_type())
#define INF_NAME_RESOLVER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_NAME_RESOLVER, InfNameResolver))
#define INF_NAME_RESOLVER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TYPE_NAME_RESOLVER, InfNameResolverClass))
#define INF_IS_NAME_RESOLVER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_NAME_RESOLVER))
#define INF_IS_NAME_RESOLVER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TYPE_NAME_RESOLVER))
#define INF_NAME_RESOLVER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TYPE_NAME_RESOLVER, InfNameResolverClass))

typedef struct _InfNameResolver InfNameResolver;
typedef struct _InfNameResolverClass InfNameResolverClass;

/**
 * InfNameResolverClass:
 * @resolved: Default signal handler for the #InfNameResolver::resolved
 * signal.
 *
 * This structure contains the default signal handlers of #InfNameResolver.
 */
struct _InfNameResolverClass {
  /*< private >*/
  GObjectClass parent_class;

  /*< public >*/

  /* Signals */
  void (*resolved)(InfNameResolver* connection,
                   const GError* error);
};

/**
 * InfNameResolver:
 *
 * #InfNameResolver is an opaque data type. You should only access it via
 * the public API functions.
 */
struct _InfNameResolver {
  /*< private >*/
  GObject parent;
};

GType
inf_name_resolver_get_type(void) G_GNUC_CONST;

InfNameResolver*
inf_name_resolver_new(InfIo* io,
                      const gchar* hostname,
                      const gchar* service,
                      const gchar* srv);

gboolean
inf_name_resolver_start(InfNameResolver* resolver,
                        GError** error);

gboolean
inf_name_resolver_lookup_backup(InfNameResolver* resolver,
                                GError** error);

gboolean
inf_name_resolver_finished(InfNameResolver* resolver);

guint
inf_name_resolver_get_n_addresses(InfNameResolver* resolver);

const InfIpAddress*
inf_name_resolver_get_address(InfNameResolver* resolver,
                              guint index);

guint
inf_name_resolver_get_port(InfNameResolver* resolver,
                           guint index);

G_END_DECLS

#endif /* __INF_NAME_RESOLVER_H__ */

/* vim:set et sw=2 ts=2: */
