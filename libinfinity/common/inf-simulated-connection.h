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

#ifndef __INF_SIMULATED_CONNECTION_H__
#define __INF_SIMULATED_CONNECTION_H__

#include <libinfinity/common/inf-tcp-connection.h>
#include <libxml/tree.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_SIMULATED_CONNECTION                 (inf_simulated_connection_get_type())
#define INF_SIMULATED_CONNECTION(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_SIMULATED_CONNECTION, InfSimulatedConnection))
#define INF_SIMULATED_CONNECTION_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TYPE_SIMULATED_CONNECTION, InfSimulatedConnectionClass))
#define INF_IS_SIMULATED_CONNECTION(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_SIMULATED_CONNECTION))
#define INF_IS_SIMULATED_CONNECTION_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TYPE_SIMULATED_CONNECTION))
#define INF_SIMULATED_CONNECTION_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TYPE_SIMULATED_CONNECTION, InfSimulatedConnectionClass))

#define INF_TYPE_SIMULATED_CONNECTION_MODE            (inf_simulated_connection_mode_get_type())

typedef struct _InfSimulatedConnection InfSimulatedConnection;
typedef struct _InfSimulatedConnectionClass InfSimulatedConnectionClass;

/**
 * InfSimulatedConnectionMode:
 * @INF_SIMULATED_CONNECTION_IMMEDIATE: Messages are received directly by the
 * target site when calling inf_xml_connection_send().
 * @INF_SIMULATED_CONNECTION_DELAYED: Messages are queued, and delivered to
 * the target site when inf_simulated_connection_flush() is called.
 *
 * The mode of a simulated connection defines when sent messages arrive at
 * the target connection.
 */
typedef enum _InfSimulatedConnectionMode {
  INF_SIMULATED_CONNECTION_IMMEDIATE,
  INF_SIMULATED_CONNECTION_DELAYED
} InfSimulatedConnectionMode;

/**
 * InfSimulatedConnectionClass:
 *
 * This structure does not contain any public fields.
 */
struct _InfSimulatedConnectionClass {
  /*< private >*/
  GObjectClass parent_class;
};

/**
 * InfSimulatedConnection:
 *
 * #InfSimulatedConnection is an opaque data type. You should only access it
 * via the public API functions.
 */
struct _InfSimulatedConnection {
  /*< private >*/
  GObject parent;
};

GType
inf_simulated_connection_mode_get_type(void) G_GNUC_CONST;

GType
inf_simulated_connection_get_type(void) G_GNUC_CONST;

InfSimulatedConnection*
inf_simulated_connection_new(void);

void
inf_simulated_connection_connect(InfSimulatedConnection* connection,
                                 InfSimulatedConnection* to);

void
inf_simulated_connection_set_mode(InfSimulatedConnection* connection,
                                  InfSimulatedConnectionMode mode);

void
inf_simulated_connection_flush(InfSimulatedConnection* connection);

G_END_DECLS

#endif /* __INF_SIMULATED_CONNECTION_H__ */

/* vim:set et sw=2 ts=2: */
