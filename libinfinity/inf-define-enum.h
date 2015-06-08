/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2015 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INF_DEFINE_ENUM_H__
#define __INF_DEFINE_ENUM_H__

#include <glib-object.h>

#define INF_DEFINE_ENUM_TYPE(TypeName, type_name, values) \
GType \
type_name##_get_type(void) \
{ \
  static volatile gsize inf_define_type_id__volatile = 0; \
  if(g_once_init_enter(&inf_define_type_id__volatile)) \
  { \
    GType inf_define_type_id = g_enum_register_static( \
      #TypeName, \
      values \
    ); \
    g_once_init_leave(&inf_define_type_id__volatile, inf_define_type_id); \
  } \
\
  return inf_define_type_id__volatile; \
}

#define INF_DEFINE_FLAGS_TYPE(TypeName, type_name, values) \
GType \
type_name##_get_type(void) \
{ \
  static volatile gsize inf_define_type_id__volatile = 0; \
  if(g_once_init_enter(&inf_define_type_id__volatile)) \
  { \
    GType inf_define_type_id = g_flags_register_static( \
      #TypeName, \
      values \
    ); \
    g_once_init_leave(&inf_define_type_id__volatile, inf_define_type_id); \
  } \
\
  return inf_define_type_id__volatile; \
}

#endif /* __INF_DEFINE_ENUM_H__ */

/* vim:set et sw=2 ts=2: */
