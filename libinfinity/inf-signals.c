/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2014 Armin Burgmeier <armin@arbur.net>
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

#include <libinfinity/inf-signals.h>

/**
 * inf_signal_handlers_disconnect_by_func:
 * @instance: The instance to remove handlers from.
 * @func: The C closure callback of the handlers (useless for non-C closures).
 * @data: The closure data of the handlers' closures.
 *
 * Disconnects all handlers on an instance that match @func and @data. This is
 * basically g_signal_handlers_disconnect_by_func(), except that it does not
 * cause warnings for implicitly converting a function pointer to a void
 * pointer.
 *
 * Returns: The number of handlers that matched.
 */
guint
inf_signal_handlers_disconnect_by_func(gpointer instance,
                                       GCallback func,
                                       gpointer data)
{
  return g_signal_handlers_disconnect_by_func(instance,
                                              *(gpointer*)&func,
                                              data);
}

/**
 * inf_signal_handlers_block_by_func:
 * @instance: The instance to block handlers from.
 * @func: The C closure callback of the handlers (useless for non-C closures).
 * @data: The closure data of the handlers' closures.
 * 
 * Blocks all handlers on an instance that match @func and @data. This is
 * basically g_signal_handlers_block_by_func(), except that it does not
 * cause warnings for implicitly converting a function pointer to a void
 * pointer.
 * 
 * Returns: The number of handlers that matched.
 */
guint
inf_signal_handlers_block_by_func(gpointer instance,
                                  GCallback func,
                                  gpointer data)
{
  return g_signal_handlers_block_by_func(instance,
                                         *(gpointer*)&func,
                                         data);
}

/**
 * inf_signal_handlers_unblock_by_func:
 * @instance: The instance to unblock handlers from.
 * @func: The C closure callback of the handlers (useless for non-C closures).
 * @data: The closure data of the handlers' closures.
 * 
 * Unblocks all handlers on an instance that match @func and @data. This is
 * basically g_signal_handlers_unblock_by_func(), except that it does not
 * cause warnings for implicitly converting a function pointer to a void
 * pointer.
 * 
 * Returns: The number of handlers that matched.
 */
guint
inf_signal_handlers_unblock_by_func(gpointer instance,
                                    GCallback func,
                                    gpointer data)
{
  return g_signal_handlers_unblock_by_func(instance,
                                           *(gpointer*)&func,
                                           data);
}

/* vim:set et sw=2 ts=2: */
