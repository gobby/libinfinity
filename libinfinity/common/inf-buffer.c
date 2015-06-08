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

/**
 * SECTION:inf-buffer
 * @title: InfBuffer
 * @short_description: Abstract document interface
 * @include: libinfinity/common/inf-buffer.h
 *
 * #InfBuffer represents a document containing a session's content. It does
 * not cope with keeping its content in-sync with other participants but just
 * offers an interface to modify the document.
 *
 * The #InfBuffer interface itself is probably not too useful, but actual
 * documents implementing functionality (such as text editing or graphics
 * editing) need to implement this interface to be passed to #InfSession.
 **/

#include <libinfinity/common/inf-buffer.h>

G_DEFINE_INTERFACE(InfBuffer, inf_buffer, G_TYPE_OBJECT)

static void
inf_buffer_default_init(InfBufferInterface* iface)
{
  g_object_interface_install_property(
    iface,
    g_param_spec_boolean(
      "modified",
      "Modified",
      "Whether the buffer was modified since it has been saved",
      FALSE,
      G_PARAM_READWRITE
    )
  );
}

/**
 * inf_buffer_get_modified:
 * @buffer: A #InfBuffer.
 *
 * Indicates whether the buffer has been modified since the last call to
 * inf_buffer_set_modified() set the modification flag to %FALSE.
 *
 * Returns: Whether the buffer has been modified.
 */
gboolean
inf_buffer_get_modified(InfBuffer* buffer)
{
  InfBufferInterface* iface;
  gboolean modified;

  g_return_val_if_fail(INF_IS_BUFFER(buffer), FALSE);

  iface = INF_BUFFER_GET_IFACE(buffer);
  if(iface->get_modified != NULL)
  {
    return iface->get_modified(buffer);
  }
  else
  {
    g_object_get(G_OBJECT(buffer), "modified", &modified, NULL);
    return modified;
  }
}

/**
 * inf_buffer_set_modified:
 * @buffer: A #InfBuffer.
 * @modified: Whether the buffer is considered modified or not.
 *
 * Sets the modification flag of @buffer to @modified. You should normally set
 * the flag to %FALSE every time the document is saved onto disk. The buffer
 * itself will set it to %TRUE when it has been changed.
 *
 * To get notified when the modification flag changes, connect to
 * GObject::notify for the InfBuffer:modified property.
 */
void
inf_buffer_set_modified(InfBuffer* buffer,
                        gboolean modified)
{
  InfBufferInterface* iface;

  g_return_if_fail(INF_IS_BUFFER(buffer));

  iface = INF_BUFFER_GET_IFACE(buffer);
  if(iface->set_modified != NULL)
  {
    iface->set_modified(buffer, modified);
  }
  else
  {
    g_object_set(G_OBJECT(buffer), "modified", modified, NULL);
  }
}

/* vim:set et sw=2 ts=2: */
