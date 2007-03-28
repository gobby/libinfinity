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

#ifndef __INF_IP_ADDRESS_H__
#define __INF_IP_ADDRESS_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_IP_ADDRESS                 (inf_ip_address_get_type())
#define INF_TYPE_IP_ADDRESS_FAMILY          (inf_ip_address_family_get_type())

typedef struct _InfIpAddress InfIpAddress;

typedef enum _InfIpAddressFamily {
  INF_IP_ADDRESS_IPV4,
  INF_IP_ADDRESS_IPV6
} InfIpAddressFamily;

GType
inf_ip_address_family_get_type(void) G_GNUC_CONST;

GType
inf_ip_address_get_type(void) G_GNUC_CONST;

InfIpAddress*
inf_ip_address_new_raw4(guint32 address);

InfIpAddress*
inf_ip_address_new_loopback4(void);

InfIpAddress*
inf_ip_address_new_raw6(const guint8 address[16]);

InfIpAddress*
inf_ip_address_new_loopback6(void);

InfIpAddress*
inf_ip_address_new_from_string(const gchar* str);

InfIpAddress*
inf_ip_address_copy(const InfIpAddress* address);

void
inf_ip_address_free(InfIpAddress* address);

InfIpAddressFamily
inf_ip_address_get_family(const InfIpAddress* address);

gconstpointer
inf_ip_address_get_raw(const InfIpAddress* address);

gchar*
inf_ip_address_to_string(const InfIpAddress* address);

int
inf_ip_address_collate(const InfIpAddress* address1,
                       const InfIpAddress* address2);

G_END_DECLS

#endif /* __INF_IP_ADDRESS_H__ */
