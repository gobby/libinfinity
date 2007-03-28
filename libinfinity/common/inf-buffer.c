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

#include <libinfinity/common/inf-buffer.h>

static void
inf_buffer_base_init(gpointer g_class)
{
  static gboolean initialized = FALSE;

  if(!initialized)
  {
    g_object_interface_install_property(
      g_class,
      g_param_spec_boolean(
        "read-only",
        "Read Only",
        "Whether write access on the buffer is permitted or not",
        TRUE,
        G_PARAM_READWRITE
      )
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
