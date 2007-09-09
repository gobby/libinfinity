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

#include <libinfinity/inf-text-buffer.h>
#include <libinfinity/inf-buffer.h>
#include <libinfinity/inf-marshal.h>

enum {
  INSERT_TEXT,
  ERASE_TEXT,

  LAST_SIGNAL
};

static guint text_buffer_signals[LAST_SIGNAL];

static void
inf_text_buffer_base_init(gpointer g_class)
{
  static gboolean initialized = FALSE;
  GObjectClass* object_class;

  object_class = G_OBJECT_CLASS(g_class);

  if(!initialized)
  {
    text_buffer_signals[INSERT_TEXT] = g_signal_new(
      "insert-text",
      G_OBJECT_CLASS_TYPE(object_class),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET(InfTextBufferIface, insert_text),
      NULL, NULL,
      inf_marshal_VOID__POINTER_UINT_UINT_OBJECT,
      G_TYPE_NONE,
      4,
      G_TYPE_POINTER,
      G_TYPE_UINT,
      G_TYPE_UINT,
      INF_TYPE_USER
    );

    text_buffer_signals[ERASE_TEXT] = g_signal_new(
      "erase-text",
      G_OBJECT_CLASS_TYPE(object_class),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET(InfTextBufferIface, erase_text),
      NULL, NULL,
      inf_marshal_VOID__UINT_UINT_OBJECT,
      G_TYPE_NONE,
      3,
      G_TYPE_UINT,
      G_TYPE_UINT,
      INF_TYPE_USER
    );
  }
}

GType
inf_text_buffer_get_type(void)
{
  static GType text_buffer_type = 0;

  if(!text_buffer_type)
  {
    static const GTypeInfo text_buffer_info = {
      sizeof(InfTextBufferIface),    /* class_size */
      inf_text_buffer_base_init,     /* base_init */
      NULL,                          /* base_finalize */
      NULL,                          /* class_init */
      NULL,                          /* class_finalize */
      NULL,                          /* class_data */
      0,                             /* instance_size */
      0,                             /* n_preallocs */
      NULL,                          /* instance_init */
      NULL                           /* value_table */
    };

    text_buffer_type = g_type_register_static(
      G_TYPE_INTERFACE,
      "InfTextBuffer",
      &text_buffer_info,
      0
    );

    g_type_interface_add_prerequisite(text_buffer_type, INF_TYPE_BUFFER);
  }

  return text_buffer_type;
}

/** inf_text_buffer_insert_text:
 *
 * @buffer: A #InfTextBuffer.
 * @text: A pointer to the text to insert.
 * @len: The length (in characters) of @text.
 * @bytes: The length (in bytes) of @text.
 * @author: A #InfUser that has written the new text, or %NULL.
 *
 * Inserts @text into @buffer. @text must be encoded in the character
 * encoding of the buffer, see inf_text_buffer_get_encoding().
 **/
void
inf_text_buffer_insert_text(InfTextBuffer* buffer,
                            gconstpointer text,
                            guint len,
                            guint bytes,
                            InfUser* author)
{
  g_return_if_fail(INF_IS_TEXT_BUFFER(buffer));
  g_return_if_fail(text != NULL);
  g_return_if_fail(author == NULL || INF_IS_USER(author));

  g_signal_emit(
    G_OBJECT(buffer),
    text_buffer_signals[INSERT_TEXT],
    0,
    text,
    len,
    bytes,
    author
  );
}

/** inf_text_buffer_erase_text:
 *
 * @buffer: A #InfTextBuffer.
 * @pos: The position to begin deleting characters from.
 * @len: The amount of characters to delete.
 * @author: A #InfUser that erases the text, or %NULL.
 *
 * Erases characters from the text buffer.
 **/
void
inf_text_buffer_erase_text(InfTextBuffer* buffer,
                           guint pos,
                           guint len,
                           InfUser* author)
{
  g_return_if_fail(INF_IS_TEXT_BUFFER(buffer));
  g_return_if_fail(author == NULL || INF_IS_USER(author));

  g_signal_emit(
    G_OBJECT(buffer),
    text_buffer_signals[ERASE_TEXT],
    0,
    pos,
    len,
    author
  );
}

/** inf_text_buffer_get_encoding:
 *
 * @buffer: A #InfTextBuffer.
 *
 * Returns the character encoding that the buffer uses. This means that all
 * return values are encoded in this encoding and all string parameters are
 * expected to be encoded in that encoding.
 *
 * Return Value: The character encoding for @buffer.
 **/
const gchar*
inf_text_buffer_get_encoding(InfTextBuffer* buffer)
{
  InfTextBufferIface* iface;

  g_return_val_if_fail(INF_IS_TEXT_BUFFER(buffer), NULL);

  iface = INF_TEXT_BUFFER_GET_IFACE(buffer);
  g_return_val_if_fail(iface->get_encoding != NULL, NULL);

  return iface->get_encoding(buffer);
}

/* vim:set et sw=2 ts=2: */
