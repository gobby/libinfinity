/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INF_TEST_UTIL_H__
#define __INF_TEST_UTIL_H__

#include <libinftext/inf-text-buffer.h>
#include <libinftext/inf-text-chunk.h>
#include <libinfinity/adopted/inf-adopted-operation.h>
#include <libinfinity/adopted/inf-adopted-request.h>

G_BEGIN_DECLS

typedef enum {
  INF_TEST_UTIL_PARSE_ERROR_UNEXPECTED_NODE,
  INF_TEST_UTIL_PARSE_ERROR_USER_ALREADY_EXISTS
} InfTestUtilParseError;

GQuark
inf_test_util_parse_error_quark(void);

void
inf_test_util_print_operation(InfAdoptedOperation* op);

void
inf_test_util_print_request(InfAdoptedRequest* request);

void
inf_test_util_print_buffer(InfTextBuffer* buffer);

gboolean
inf_test_util_dir_foreach(const char* dirname,
                          void(*callback)(const char*, gpointer),
                          gpointer user_data,
                          GError** error);

InfTextChunk*
inf_test_util_parse_buffer(xmlNodePtr xml,
                           GError** error);

gboolean
inf_test_util_parse_user(xmlNodePtr xml,
                         GSList** users,
                         GError** error);

G_END_DECLS

#endif /* __INF_TEST_UTIL_H__ */
