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

#include <libinfinity/inf-buffer.h>
#include <libinfinity/inf-marshal.h>

enum {
  READ_ONLY_CHANGED,

  LAST_SIGNAL
};

static guint buffer_signals[LAST_SIGNAL];

static void
inf_buffer_base_init(gpointer g_class)
{
  static gboolean initialized = FALSE;
  GObjectClass* object_class;

  object_class = G_OBJECT_CLASS(g_class);

  if(!initialized)
  {
    buffer_signals[READ_ONLY_CHANGED] = g_signal_new(
      "read-only-changed",
      G_OBJECT_CLASS_TYPE(object_class),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET(InfBufferIface, read_only_changed),
      NULL, NULL,
      inf_marshal_VOID__BOOLEAN,
      G_TYPE_NONE,
      1,
      G_TYPE_BOOLEAN,
    );
  }
}

GType
inf_buffer_get_type(void)
{
  static GType buffer_type = 0;

  if(!buffer_type)
  {
    static const GTypeInfo buffer_info = {
      sizeof(InfBufferIface),        /* class_size */
      inf_buffer_base_init,          /* base_init */
      NULL,                          /* base_finalize */
      NULL,                          /* class_init */
      NULL,                          /* class_finalize */
      NULL,                          /* class_data */
      0,                             /* instance_size */
      0,                             /* n_preallocs */
      NULL,                          /* instance_init */
      NULL                           /* value_table */
    };

    buffer_type = g_type_register_static(
      G_TYPE_INTERFACE,
      "InfBuffer",
      &buffer_info,
      0
    );

    g_type_interface_add_prerequisite(buffer_type, G_TYPE_OBJECT);
  }

  return buffer_type;
}

/** inf_buffer_set_read_only:
 *
 * @buffer: A #InfBuffer.
 *
 * Sets @buffer in read-only mode, so no modifications can be performed.
 **/
void
inf_buffer_set_read_only(InfBuffer* buffer)
{
  g_return_if_fail(INF_IS_BUFFER(buffer));
  g_signal_emit(G_OBJECT(buffer), buffer_signals[READ_ONLY_CHANGED], 0, true);
}

/** inf_buffer_set_read_write:
 *
 * @buffer A #InfBuffer.
 *
 * Sets @buffer in read-write mode, so modifications can be performed.
 **/
void inf_buffer_set_read_write(InfBuffer* buffer)
{
  g_return_if_fail(INF_IS_BUFFER(buffer));
  g_signal_emit(G_OBJECT(buffer), buffer_signals[READ_ONLY_CHANGED], 0, false);
}

/** inf_buffer_is_read_only:
 *
 * @buffer: A #InfBuffer.
 *
 * Returns whether @buffer is in read-only mode.
 *
 * Return Value: Whether @buffer is in read-only mode.
 **/
gboolean
inf_buffer_is_read_only(InfBuffer* buffer)
{
  InfBufferIface* iface;

  g_return_val_if_fail(INF_IS_BUFFER(buffer), FALSE);

  iface = INF_BUFFER_GET_IFACE(buffer);
  g_return_val_if_fail(iface->is_read_only != NULL, FALSE);

  return iface->is_read_only(buffer);
}
