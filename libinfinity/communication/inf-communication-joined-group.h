/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2013 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INF_COMMUNICATION_JOINED_GROUP_H__
#define __INF_COMMUNICATION_JOINED_GROUP_H__

#include <libinfinity/communication/inf-communication-group.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_COMMUNICATION_TYPE_JOINED_GROUP                 (inf_communication_joined_group_get_type())
#define INF_COMMUNICATION_JOINED_GROUP(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_COMMUNICATION_TYPE_JOINED_GROUP, InfCommunicationJoinedGroup))
#define INF_COMMUNICATION_JOINED_GROUP_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_COMMUNICATION_TYPE_JOINED_GROUP, InfCommunicationJoinedGroupClass))
#define INF_COMMUNICATION_IS_JOINED_GROUP(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_COMMUNICATION_TYPE_JOINED_GROUP))
#define INF_COMMUNICATION_IS_JOINED_GROUP_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_COMMUNICATION_TYPE_JOINED_GROUP))
#define INF_COMMUNICATION_JOINED_GROUP_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_COMMUNICATION_TYPE_JOINED_GROUP, InfCommunicationJoinedGroupClass))

typedef struct _InfCommunicationJoinedGroup InfCommunicationJoinedGroup;
typedef struct _InfCommunicationJoinedGroupClass
  InfCommunicationJoinedGroupClass;

/**
 * InfCommunicationJoinedGroupClass:
 *
 * This structure does not contain any public fields.
 */
struct _InfCommunicationJoinedGroupClass {
  /*< private >*/
  InfCommunicationGroupClass parent;
};

/**
 * InfCommunicationJoinedGroup:
 *
 * #InfCommunicationJoinedGroup is an opaque data type. You should only
 * access it via the public API functions.
 */
struct _InfCommunicationJoinedGroup {
  /*< private >*/
  InfCommunicationGroup parent_instance;
};

GType
inf_communication_joined_group_get_type(void) G_GNUC_CONST;

void
inf_communication_joined_group_remove_member(InfCommunicationJoinedGroup* grp,
                                             InfXmlConnection* connection);

InfXmlConnection*
inf_communication_joined_group_get_publisher(InfCommunicationJoinedGroup* g);

G_END_DECLS

#endif /* __INF_COMMUNICATION_JOINED_GROUP_H__ */

/* vim:set et sw=2 ts=2: */
