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
 * Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#ifndef __INF_COMMUNICATION_CENTRAL_METHOD_H__
#define __INF_COMMUNICATION_CENTRAL_METHOD_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_COMMUNICATION_TYPE_CENTRAL_METHOD                 (inf_communication_central_method_get_type())
#define INF_COMMUNICATION_CENTRAL_METHOD(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_COMMUNICATION_TYPE_CENTRAL_METHOD, InfCommunicationCentralMethod))
#define INF_COMMUNICATION_CENTRAL_METHOD_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_COMMUNICATION_TYPE_CENTRAL_METHOD, InfCommunicationCentralMethodClass))
#define INF_COMMUNICATION_IS_CENTRAL_METHOD(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_COMMUNICATION_TYPE_CENTRAL_METHOD))
#define INF_COMMUNICATION_IS_CENTRAL_METHOD_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_COMMUNICATION_TYPE_CENTRAL_METHOD))
#define INF_COMMUNICATION_CENTRAL_METHOD_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_COMMUNICATION_TYPE_CENTRAL_METHOD, InfCommunicationCentralMethodClass))

typedef struct _InfCommunicationCentralMethod InfCommunicationCentralMethod;
typedef struct _InfCommunicationCentralMethodClass
  InfCommunicationCentralMethodClass;

/**
 * InfCommunicationCentralMethodClass:
 *
 * This structure does not contain any public fields.
 */
struct _InfCommunicationCentralMethodClass {
  /*< private >*/
  GObjectClass parent;
};

/**
 * InfCommunicationCentralMethod:
 *
 * #InfCommunicationCentralMethod is an opaque data type. You should only
 * access it * via the public API functions.
 */
struct _InfCommunicationCentralMethod {
  /*< private >*/
  GObject parent_instance;
};

GType
inf_communication_central_method_get_type(void) G_GNUC_CONST;

G_END_DECLS

#endif /* __INF_COMMUNICATION_CENTRAL_METHOD_H__ */

/* vim:set et sw=2 ts=2: */
