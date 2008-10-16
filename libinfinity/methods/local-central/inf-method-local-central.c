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

#include <libinfinity/common/inf-central-method.h>

const InfConnectionManagerMethodDesc INF_METHOD_PLUGIN = {
  "local",
  "central",
  inf_central_method_open,
  inf_central_method_join,
  inf_central_method_finalize,
  inf_central_method_receive_msg,
  inf_central_method_receive_ctrl,
  inf_central_method_add_connection,
  inf_central_method_remove_connection,
  inf_central_method_send_to_net
};

/* vim:set et sw=2 ts=2: */
