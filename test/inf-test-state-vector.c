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

#include <libinfinity/adopted/inf-adopted-state-vector.h>
#include <libinfinity/common/inf-user.h>
#include <string.h>

static void cmp(const char* should_be, InfAdoptedStateVector* vec) {
  char* is;
  InfAdoptedStateVector* should_be_vec;
  
  is = inf_adopted_state_vector_to_string(vec);
  if (strcmp(should_be, is) != 0) {
    printf("should be: %s\n"
           "is:        %s\n"
           "strcmp failed\n", should_be, is);
    g_assert_not_reached();
  }

  should_be_vec = inf_adopted_state_vector_from_string(should_be, NULL);
  if (!should_be_vec
      || inf_adopted_state_vector_compare(vec, should_be_vec) != 0
      || inf_adopted_state_vector_compare(should_be_vec, vec) != 0) {
    printf("should be: %s\n"
           "is:        %s\n"
           "compare failed\n", should_be, is);
    g_assert_not_reached();
  }
  g_free(is);
  inf_adopted_state_vector_free(should_be_vec);
  printf("ok!\n");
}

#define apply(op, args) inf_adopted_state_vector_##op args

static void l_test() {
  InfAdoptedStateVector* vec, * vec_;
  int i;
  char* str;

  vec = inf_adopted_state_vector_new();

  apply(set, (vec, 10, 14));
  cmp("10:14", vec);
  
  apply(set, (vec, 4, 5));
  cmp("4:5;10:14", vec);

  apply(set, (vec, 4, 8));
  cmp("4:8;10:14", vec);

  for (i = 0; i < 10; ++i)
    apply(set, (vec, i, i*10));

  cmp("1:10;2:20;3:30;4:40;5:50;6:60;7:70;8:80;9:90;10:14", vec);

  apply(free, (vec));

  vec  = apply(from_string, ("1:10;2:5",       NULL));
  vec_ = apply(from_string, ("1:10;2:10;4:10", NULL));

  g_assert(apply(causally_before, (vec,  vec)));
  g_assert(apply(causally_before, (vec,  vec_)));
  g_assert(apply(causally_before, (vec_, vec_)));

  apply(free, (vec));
  apply(free, (vec_));

  vec  = apply(from_string, ("1:10;2:15",       NULL));
  vec_ = apply(from_string, ("1:10;2:10;4:10", NULL));

  g_assert(!apply(causally_before, (vec, vec_)));
  g_assert( apply(causally_before, (vec, vec)));

  apply(free, (vec));
  vec  = apply(from_string, ("1:10;3:15",       NULL));

  g_assert(!apply(causally_before, (vec, vec_)));
  g_assert( apply(causally_before, (vec, vec)));

  apply(free, (vec_));
  apply(free, (vec));


  vec  = apply(from_string, ("1:10", NULL));
  vec_ = apply(from_string, ("1:7", NULL));

  str = apply(to_string_diff, (vec, vec_));
  g_assert(strcmp("1:3", str) == 0);

  apply(free, (vec_));
  vec_ = apply(from_string_diff, (str, vec, NULL));
  g_free(str);
  g_assert(vec_ != NULL);
  cmp("1:13", vec_);
  apply(free, (vec_));

  for (i = 0; i < 100; ++i) {
    apply(set, (vec, rand(), i));
  }
  str = apply(to_string, (vec));
  /* printf("%s\n", str); */
  g_free(str);
  apply(free, (vec));

  vec  = apply(from_string, ("1:0;5:0", NULL));
  vec_ = apply(new, ());
  g_assert(apply(compare, (vec, vec_)) == 0);

  apply(free, (vec));
  apply(free, (vec_));
}

int main(int argc, char* argv[])
{
  guint users[2];
  InfAdoptedStateVector* vec;
  InfAdoptedStateVector* vec2;

  g_type_init();

  users[0] = 1;
  users[1] = 2;

  /* Note we do not need to allocate users since the state vector does not
   * touch them. */

  vec = inf_adopted_state_vector_new();
  vec2 = inf_adopted_state_vector_new();
  g_assert(inf_adopted_state_vector_causally_before_inc(vec, vec2, 1) == FALSE);
  inf_adopted_state_vector_free(vec2);

  inf_adopted_state_vector_set(vec, users[0], 2);
  g_assert(inf_adopted_state_vector_get(vec, users[0]) == 2);

  inf_adopted_state_vector_add(vec, users[0], 4);
  g_assert(inf_adopted_state_vector_get(vec, users[0]) == 6);

  inf_adopted_state_vector_add(vec, users[1], 3);
  g_assert(inf_adopted_state_vector_get(vec, users[1]) == 3);

  inf_adopted_state_vector_set(vec, users[1], 5);
  g_assert(inf_adopted_state_vector_get(vec, users[1]) == 5);

  inf_adopted_state_vector_free(vec);
  l_test();
  return 0;
}

/* vim:set et sw=2 ts=2: */
