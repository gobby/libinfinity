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

/**
 * SECTION:inf-ip-address
 * @title: InfIpAddress
 * @short_description: IPv4 and IPv6 addresses
 * @see_also: #InfTcpConnection
 * @include: libinfinity/common/inf-ip-address.h
 * @stability: Unstable
 *
 * A #InfIpAddress represents an IPv4 or an IPv6 address. Use
 * inf_ip_address_get_family() to find out the type of a specific address.
 **/

#include <libinfinity/common/inf-ip-address.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <string.h>

struct _InfIpAddress {
  InfIpAddressFamily family;

  union {
    struct in_addr addr4;
    struct in6_addr addr6;
  } shared;
};

static InfIpAddress*
inf_ip_address_new_common(InfIpAddressFamily family)
{
  InfIpAddress* address;
  address = g_slice_new(InfIpAddress);
  address->family = family;
  return address;
}

GType
inf_ip_address_family_get_type(void)
{
  static GType ip_address_family_type = 0;

  if(!ip_address_family_type)
  {
    static const GEnumValue ip_address_family_values[] = {
      {
        INF_IP_ADDRESS_IPV4,
        "INF_IP_ADDRESS_IPV4",
        "IPv4"
      }, {
        INF_IP_ADDRESS_IPV6,
        "INF_IP_ADDRESS_IPV6",
        "IPv6"
      }, {
        0,
        NULL,
        NULL
      }
    };

    ip_address_family_type = g_enum_register_static(
      "InfIpAddressFamily",
      ip_address_family_values
    );
  }

  return ip_address_family_type;
}

GType
inf_ip_address_get_type(void)
{
  static GType ip_address_type = 0;

  if(!ip_address_type)
  {
    ip_address_type = g_boxed_type_register_static(
      "InfIpAddress",
      (GBoxedCopyFunc)inf_ip_address_copy,
      (GBoxedFreeFunc)inf_ip_address_free
    );
  }

  return ip_address_type;
}

/**
 * inf_ip_address_new_raw4:
 * @address: An IPv4 address in network byte order.
 *
 * Creates a new IPv4 address.
 *
 * Return Value: A new #InfIpAddress.
 **/
InfIpAddress*
inf_ip_address_new_raw4(guint32 address)
{
  InfIpAddress* addr;
  addr = inf_ip_address_new_common(INF_IP_ADDRESS_IPV4);
  addr->shared.addr4.s_addr = address;
  return addr;
}

/**
 * inf_ip_address_new_loopback4:
 *
 * Creates a new IPv4 address that contains the local host's IP address
 * ("127.0.0.1").
 *
 * Return Value: A new #InfIpAddress.
 **/
InfIpAddress*
inf_ip_address_new_loopback4(void)
{
  return inf_ip_address_new_raw4(htonl(INADDR_LOOPBACK));
}

/**
 * inf_ip_address_new_raw6:
 * @address: An IPv6 address in network bype order.
 *
 * Creates a new IPv6 address.
 *
 * Return Value: A new #InfIpAddress.
 **/
InfIpAddress*
inf_ip_address_new_raw6(const guint8 address[16])
{
  InfIpAddress* addr;
  addr = inf_ip_address_new_common(INF_IP_ADDRESS_IPV6);
  memcpy(addr->shared.addr6.s6_addr, address, 16);
  return addr;
}

/**
 * inf_ip_address_new_loopback6:
 *
 * Creates a new IPv6 address that contains the local host's IP address
 * ("::1").
 *
 * Return Value: A new #InfIpAddress.
 **/
InfIpAddress*
inf_ip_address_new_loopback6(void)
{
  return inf_ip_address_new_raw6(in6addr_loopback.s6_addr);
}

/**
 * inf_ip_address_new_from_string:
 * @str: A string containing an IPv4 or IPv6 address in standard dots
 * notation.
 *
 * Creates a new IP address (either IPv4 or IPv6) from the given string.
 *
 * Return Value: A new #InfIpAddress, or %NULL.
 **/
InfIpAddress*
inf_ip_address_new_from_string(const gchar* str)
{
  InfIpAddress* addr;
  addr = g_slice_new(InfIpAddress);

  if(inet_pton(AF_INET, str, &addr->shared.addr4) > 0)
  {
    addr->family = INF_IP_ADDRESS_IPV4;
  }
  else if(inet_pton(AF_INET6, str, &addr->shared.addr6) > 0)
  {
    addr->family = INF_IP_ADDRESS_IPV6;
  }
  else
  {
    g_slice_free(InfIpAddress, addr);
    addr = NULL;
  }

  return addr;
}

/**
 * inf_ip_address_copy:
 * @address: A #InfIpAddress.
 *
 * Creates a new #InfIpAddress that contains the same address as @address.
 *
 * Return Value: A new #InfIpAddress.
 **/
InfIpAddress*
inf_ip_address_copy(const InfIpAddress* address)
{
  switch(address->family)
  {
  case INF_IP_ADDRESS_IPV4:
    return inf_ip_address_new_raw4(address->shared.addr4.s_addr);
  case INF_IP_ADDRESS_IPV6:
    return inf_ip_address_new_raw6(address->shared.addr6.s6_addr);
  default:
    g_assert_not_reached();
    return NULL;
  }
}

/**
 * inf_ip_address_free:
 * @address: A #InfIpAddress.
 *
 * Frees @address.
 **/
void
inf_ip_address_free(InfIpAddress* address)
{
  g_slice_free(InfIpAddress, address);
}

/**
 * inf_ip_address_get_family:
 * @address: A #InfIpAddress.
 *
 * Returns the address family of @address.
 *
 * Return Value: A #InfIpAddressFamily.
 **/
InfIpAddressFamily
inf_ip_address_get_family(const InfIpAddress* address)
{
  return address->family;
}

/**
 * inf_ip_address_get_raw:
 * @address: A #InfIpAddress.
 *
 * Returns either 32 bit (IPv4) or 128 bit (IPv6) raw address data in host
 * byte order of @address.
 *
 * Return Value: The raw address.
 **/
gconstpointer
inf_ip_address_get_raw(const InfIpAddress* address)
{
  switch(address->family)
  {
  case INF_IP_ADDRESS_IPV4:
    return &address->shared.addr4;
  case INF_IP_ADDRESS_IPV6:
    return address->shared.addr6.s6_addr;
  default:
    g_assert_not_reached();
    return NULL;
  }
}

/**
 * inf_ip_address_to_string:
 * @address: A #InfIpAddress.
 *
 * Returns a string representation of @address in standard dots format (like
 * "192.168.0.1" or "::1").
 *
 * Return Value: A newly-allocated string. Free with g_free().
 **/
gchar*
inf_ip_address_to_string(const InfIpAddress* address)
{
  gchar* retval;

  switch(address->family)
  {
  case INF_IP_ADDRESS_IPV4:
    retval = g_malloc(INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &address->shared.addr4, retval, INET_ADDRSTRLEN);
    return retval;
  case INF_IP_ADDRESS_IPV6:
    retval = g_malloc(INET6_ADDRSTRLEN);
    inet_ntop(AF_INET6, &address->shared.addr6, retval, INET6_ADDRSTRLEN);
    return retval;
  default:
    g_assert_not_reached();
    return NULL;
  }
}

/**
 * inf_ip_address_collate:
 * @address1: First address.
 * @address2: Second address.
 *
 * Compares the two addresses for sorting.
 *
 * Return Value: -1 if @address1 compares before, 0 if it compares equal or
 * 1 if it compares after @address.
 **/
int
inf_ip_address_collate(const InfIpAddress* address1,
                       const InfIpAddress* address2)
{
  if(address1->family != address2->family)
  {
    /* IPv4 compares before IPv6 */
    if(address1->family == INF_IP_ADDRESS_IPV4)
      return -1;
    else
      return 1;
  }
  else
  {

   if(address1->family == INF_IP_ADDRESS_IPV4)
   {
     return memcmp(
       &address1->shared.addr4,
       &address2->shared.addr4,
       sizeof(struct in_addr)
     );
   }
   else
   {
     return memcmp(
       &address1->shared.addr6,
       &address2->shared.addr6,
       sizeof(struct in6_addr)
     );
   }
  }
}

/* vim:set et sw=2 ts=2: */
