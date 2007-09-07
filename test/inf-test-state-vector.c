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

#include <libinfinity/adopted/inf-adopted-state-vector.h>
#include <libinfinity/common/inf-user.h>

int main(int argc, char* argv[])
{
  guint users[2];
  InfAdoptedStateVector* vec;

  g_type_init();

  users[0] = 1;
  users[1] = 2;

  /* Note we do not need to allocate users since the state vector does not
   * touch them. */

  vec = inf_adopted_state_vector_new();
  inf_adopted_state_vector_set(vec, users[0], 2);
  g_assert(inf_adopted_state_vector_get(vec, users[0]) == 2);

  inf_adopted_state_vector_add(vec, users[0], 4);
  g_assert(inf_adopted_state_vector_get(vec, users[0]) == 6);

  inf_adopted_state_vector_add(vec, users[1], 3);
  g_assert(inf_adopted_state_vector_get(vec, users[1]) == 3);

  inf_adopted_state_vector_set(vec, users[1], 5);
  g_assert(inf_adopted_state_vector_get(vec, users[1]) == 5);

  inf_adopted_state_vector_free(vec);
  return 0;
}

/* vim:set et sw=2 ts=2: */
