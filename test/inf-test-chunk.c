/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2010 Armin Burgmeier <armin@arbur.net>
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

#include <libinftext/inf-text-chunk.h>

int main()
{
  InfTextChunk* chunk;
  InfTextChunk* chunk2;

  chunk2 = inf_text_chunk_new("UTF-8");

  inf_text_chunk_insert_text(chunk2, 0, "a", 1, 1, 500);
  inf_text_chunk_insert_text(chunk2, 0, "b", 1, 1, 501);
  inf_text_chunk_insert_text(chunk2, 0, "c", 1, 1, 502);
  inf_text_chunk_insert_text(chunk2, 3, "ü", 2, 1, 503);
  chunk = inf_text_chunk_substring(chunk2, 0, 3);

  inf_text_chunk_free(chunk);
  inf_text_chunk_free(chunk2);

  return 0;
}
