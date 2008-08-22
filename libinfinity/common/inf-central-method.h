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

#ifndef __INF_CENTRAL_METHOD_H__
#define __INF_CENTRAL_METHOD_H__

#include <libinfinity/common/inf-connection-manager.h>

G_BEGIN_DECLS

InfConnectionManagerMethod*
inf_central_method_open(const InfConnectionManagerMethodDesc* dc,
                        InfConnectionManagerGroup* group);

InfConnectionManagerMethod*
inf_central_method_join(const InfConnectionManagerMethodDesc* dc,
                        InfConnectionManagerGroup* group,
                        InfXmlConnection* publisher_conn);

void
inf_central_method_finalize(InfConnectionManagerMethod* instance);

void
inf_central_method_receive_msg(InfConnectionManagerMethod* instance,
                               InfConnectionManagerScope scope,
                               gboolean can_forward,
                               InfXmlConnection* connection,
                               xmlNodePtr xml);

void
inf_central_method_receive_ctrl(InfConnectionManagerMethod* instance,
                                InfXmlConnection* connection,
                                xmlNodePtr xml);

void
inf_central_method_add_connection(InfConnectionManagerMethod* instance,
                                  InfXmlConnection* connection);

void
inf_central_method_remove_connection(InfConnectionManagerMethod* inst,
                                     InfXmlConnection* connection);

void
inf_central_method_send_to_net(InfConnectionManagerMethod* instance,
                               InfXmlConnection* except,
                               xmlNodePtr xml);

G_END_DECLS

#endif /* __INF_CENTRAL_METHOD_H__ */

/* vim:set et sw=2 ts=2: */
