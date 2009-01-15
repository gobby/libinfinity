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

#ifndef __INF_COMMUNICATION_HOSTED_GROUP_H__
#define __INF_COMMUNICATION_HOSTED_GROUP_H__

#include <libinfinity/communication/inf-communication-group.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_COMMUNICATION_TYPE_HOSTED_GROUP                 (inf_communication_hosted_group_get_type())
#define INF_COMMUNICATION_HOSTED_GROUP(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_COMMUNICATION_TYPE_HOSTED_GROUP, InfCommunicationHostedGroup))
#define INF_COMMUNICATION_HOSTED_GROUP_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_COMMUNICATION_TYPE_HOSTED_GROUP, InfCommunicationHostedGroupClass))
#define INF_COMMUNICATION_IS_HOSTED_GROUP(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_COMMUNICATION_TYPE_HOSTED_GROUP))
#define INF_COMMUNICATION_IS_HOSTED_GROUP_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_COMMUNICATION_TYPE_HOSTED_GROUP))
#define INF_COMMUNICATION_HOSTED_GROUP_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_COMMUNICATION_TYPE_HOSTED_GROUP, InfCommunicationHostedGroupClass))

typedef struct _InfCommunicationHostedGroup InfCommunicationHostedGroup;
typedef struct _InfCommunicationHostedGroupClass
  InfCommunicationHostedGroupClass;

/**
 * InfCommunicationHostedGroupClass:
 *
 * This structure does not contain any public fields.
 */
struct _InfCommunicationHostedGroupClass {
  /*< private >*/
  InfCommunicationGroupClass parent;
};

/**
 * InfCommunicationHostedGroup:
 *
 * #InfCommunicationHostedGroup is an opaque data type. You should only
 * access it via the public API functions.
 */
struct _InfCommunicationHostedGroup {
  /*< private >*/
  InfCommunicationGroup parent_instance;
};

GType
inf_communication_hosted_group_get_type(void) G_GNUC_CONST;

void
inf_communication_hosted_group_add_method(InfCommunicationHostedGroup* group,
                                          const gchar* method);

void
inf_communication_hosted_group_add_member(InfCommunicationHostedGroup* group,
                                          InfXmlConnection* connection);

void
inf_communication_hosted_group_remove_member(InfCommunicationHostedGroup* grp,
                                             InfXmlConnection* connection);

G_END_DECLS

#endif /* __INF_COMMUNICATION_HOSTED_GROUP_H__ */

/* vim:set et sw=2 ts=2: */
