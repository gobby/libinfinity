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

#include <libinftext/inf-text-default-buffer.h>
#include <libinftext/inf-text-fixline-buffer.h>
#include <libinfinity/common/inf-standalone-io.h>

#include <string.h>

typedef enum _InfTestTextFixlineTarget {
  TG_BASE,
  TG_BUF
} InfTestTextFixlineTarget;

typedef enum _InfTestTextFixlineOperation {
  OP_NONE,
  OP_INS,
  OP_DEL
} InfTestTextFixlineOperation;

typedef struct _InfTestTextFixlineTest {
  const gchar* initial_buffer;
  const gchar* initial_base;
  guint n_lines;
  InfTestTextFixlineOperation operation;
  InfTestTextFixlineTarget target;
  guint pos;
  gchar* text;
  const gchar* final_buffer;
  const gchar* final_base;
} InfTestTextFixlineTest;

#define MKNOOP OP_NONE,TG_BUF,0,NULL
#define MKBASEINOP(pos,text) OP_INS,TG_BASE,pos,text
#define MKBASEDLOP(pos,len) OP_DEL,TG_BASE,pos,GUINT_TO_POINTER(len)
#define MKBUFINOP(pos,text) OP_INS,TG_BUF,pos,text
#define MKBUFDLOP(pos,len) OP_DEL,TG_BUF,pos,GUINT_TO_POINTER(len)

static gboolean
check_buffer(InfTextBuffer* buffer,
             const gchar* check_text,
             const gchar* buffer_name)
{
  InfTextChunk* chunk;
  gpointer text;
  gsize len;
  gboolean result;

  chunk = inf_text_buffer_get_slice(
    buffer,
    0,
    inf_text_buffer_get_length(buffer)
  );

  text = inf_text_chunk_get_text(chunk, &len);
  inf_text_chunk_free(chunk);

  if(strlen(check_text) != len || strncmp(check_text, text, len) != 0)
  {
    printf(
      "%s Buffer has text \"%.*s\" but should have \"%s\"\n",
      buffer_name,
      (int)len, (gchar*)text,
      check_text
    );

    result = FALSE;
  }
  else
  {
    result = TRUE;
  }

  g_free(text);
  return result;
}

static gboolean
test_fixline(const gchar* initial_buffer_content,
             const gchar* initial_base_content,
             guint n_lines,
             InfTestTextFixlineOperation operation,
             InfTestTextFixlineTarget target,
             guint pos,
             gchar* text,
             const gchar* final_buffer_content,
             const gchar* final_base_content)
{
  InfStandaloneIo* io;
  InfTextBuffer* base;
  InfTextBuffer* buffer;

  /* Create the initial state */
  io = inf_standalone_io_new();
  base = INF_TEXT_BUFFER(inf_text_default_buffer_new("UTF-8"));

  inf_text_buffer_insert_text(
    base,
    0,
    initial_buffer_content,
    strlen(initial_buffer_content),
    strlen(initial_buffer_content),
    0
  );

  buffer = INF_TEXT_BUFFER(
    inf_text_fixline_buffer_new(INF_IO(io), base, n_lines)
  );

  /* Check the initial state */
  if(!check_buffer(base, initial_base_content, "Initial base"))
  {
    g_object_unref(io);
    g_object_unref(base);
    g_object_unref(buffer);
    return FALSE;
  }

  if(!check_buffer(buffer, initial_buffer_content, "Initial buf"))
  {
    g_object_unref(io);
    g_object_unref(base);
    g_object_unref(buffer);
    return FALSE;
  }

  /* Apply the operation */
  switch(operation)
  {
  case OP_NONE:
    break;
  case OP_INS:
    if(target == TG_BASE)
    {
      inf_text_buffer_insert_text(
        base,
        pos,
        text,
        strlen(text),
        strlen(text),
        0
      );
    }
    else if(target == TG_BUF)
    {
      inf_text_buffer_insert_text(
        buffer,
        pos,
        text,
        strlen(text),
        strlen(text),
        0
      );
    }
    break;
  case OP_DEL:
    if(target == TG_BASE)
      inf_text_buffer_erase_text(base, pos, GPOINTER_TO_UINT(text), 0);
    else if(target == TG_BUF)
      inf_text_buffer_erase_text(buffer, pos, GPOINTER_TO_UINT(text), 0);
    break;
  default:
    g_assert_not_reached();
    break;
  }

  /* Run any delayed action */
  inf_standalone_io_iteration_timeout(io, 0);

  /* Check the final state */
  if(!check_buffer(base, final_base_content, "Final base"))
  {
    g_object_unref(io);
    g_object_unref(base);
    g_object_unref(buffer);
    return FALSE;
  }

  if(!check_buffer(buffer, final_buffer_content, "Final buf"))
  {
    g_object_unref(io);
    g_object_unref(base);
    g_object_unref(buffer);
    return FALSE;
  }

  g_object_unref(io);
  g_object_unref(base);
  g_object_unref(buffer);

  return TRUE;
}

static gboolean
test_fixline_struct(const InfTestTextFixlineTest* test)
{
  return test_fixline(
    test->initial_buffer,
    test->initial_base,
    test->n_lines,
    test->operation,
    test->target,
    test->pos,
    test->text,
    test->final_buffer,
    test->final_base
  );
}

int main()
{
  const InfTestTextFixlineTest TESTS[] = {
    { "", "", 0, MKNOOP, "", "" },
    { "", "\n", 1, MKNOOP, "", "\n" },
    { "", "\n\n", 2, MKNOOP, "", "\n\n" },

    { "\n\n\n\n", "\n\n", 2, MKNOOP, "\n\n\n\n", "\n\n" },

    { "", "\n\n", 2, MKBASEINOP(0, "\n"), "", "\n\n" },
    { "", "\n\n", 2, MKBASEINOP(1, "\n"), "", "\n\n" },
    { "", "\n\n", 2, MKBASEINOP(2, "\n"), "", "\n\n" },

    { "", "\n\n", 2, MKBASEINOP(0, "A"), "A", "A\n\n" },
    { "", "\n\n", 2, MKBASEINOP(1, "A"), "\nA", "\nA\n\n" },
    { "", "\n\n", 2, MKBASEINOP(2, "A"), "\n\nA", "\n\nA\n\n" },

    /* 10: */
    { "", "\n\n", 2, MKBUFINOP(0, "\n"), "\n", "\n\n" },
    { "", "\n\n", 2, MKBUFINOP(0, "\n\n\n"), "\n\n\n", "\n\n" },
    { "\n\n\n\n", "\n\n", 2, MKBUFINOP(0, "\n"), "\n\n\n\n\n", "\n\n" },

    { "\n\n\n\n", "\n\n", 2, MKBUFINOP(0, "A"), "A\n\n\n\n", "A\n\n" },
    { "\n\n\n\n", "\n\n", 2, MKBUFINOP(1, "A"), "\nA\n\n\n", "\nA\n\n" },
    { "\n\n\n\n", "\n\n", 2, MKBUFINOP(2, "A"), "\n\nA\n\n", "\n\nA\n\n" },
    { "\n\n\n\n", "\n\n", 2, MKBUFINOP(3, "A"), "\n\n\nA\n", "\n\n\nA\n\n" },
    { "\n\n\n\n", "\n\n", 2, MKBUFINOP(4, "A"), "\n\n\n\nA", "\n\n\n\nA\n\n" },

    /* 18: */
    { "", "\n\n", 2, MKBASEDLOP(0, 1), "", "\n\n" },
    { "", "\n\n", 2, MKBASEDLOP(1, 1), "", "\n\n" },
    { "", "\n\n", 2, MKBASEDLOP(0, 2), "", "\n\n" },

    { "A", "A\n\n", 2, MKBASEDLOP(0, 1), "", "\n\n" },
    { "A", "A\n\n", 2, MKBASEDLOP(1, 1), "A", "A\n\n" },
    { "A", "A\n\n", 2, MKBASEDLOP(2, 1), "A", "A\n\n" },
    { "A", "A\n\n", 2, MKBASEDLOP(0, 2), "", "\n\n" },
    { "A", "A\n\n", 2, MKBASEDLOP(0, 3), "", "\n\n" },

    { "\nA", "\nA\n\n", 2, MKBASEDLOP(0, 1), "A", "A\n\n" },
    { "\nA", "\nA\n\n", 2, MKBASEDLOP(1, 1), "\n", "\n\n" },

    { "\nA\n", "\nA\n\n", 2, MKBASEDLOP(0, 1), "A\n", "A\n\n" },
    { "\nA\n", "\nA\n\n", 2, MKBASEDLOP(1, 1), "\n\n", "\n\n" },
    { "\nA\n", "\nA\n\n", 2, MKBASEDLOP(2, 1), "\nA\n" /* \nA would be reasonable, too... */, "\nA\n\n" },
    { "\nA\n", "\nA\n\n", 2, MKBASEDLOP(3, 1), "\nA\n", "\nA\n\n" },

    { "\nA\n", "\nA\n\n", 2, MKBASEDLOP(0, 2), "\n", "\n\n" },
    { "\nA\n", "\nA\n\n", 2, MKBASEDLOP(1, 2), "\n", "\n\n" },
    { "\nA\n", "\nA\n\n", 2, MKBASEDLOP(2, 2), "\nA\n" /* \nA would be reasonable, too... */, "\nA\n\n" },

    { "\nA\n", "\nA\n\n", 2, MKBUFDLOP(0, 1), "A\n", "A\n\n" },
    { "\nA\n", "\nA\n\n", 2, MKBUFDLOP(1, 1), "\n\n", "\n\n" },
    { "\nA\n", "\nA\n\n", 2, MKBUFDLOP(2, 1), "\nA", "\nA\n\n" },

    { "\n\n\n\nA", "\n\n\n\nA\n\n", 2, MKBUFDLOP(0, 1), "\n\n\nA", "\n\n\nA\n\n" },
    { "\n\n\n\nA", "\n\n\n\nA\n\n", 2, MKBUFDLOP(0, 2), "\n\nA", "\n\nA\n\n" },
    { "\n\n\n\nA", "\n\n\n\nA\n\n", 2, MKBUFDLOP(0, 3), "\nA", "\nA\n\n" },
    { "\n\n\n\nA", "\n\n\n\nA\n\n", 2, MKBUFDLOP(0, 4), "A", "A\n\n" },
    { "\n\n\n\nA", "\n\n\n\nA\n\n", 2, MKBUFDLOP(0, 5), "", "\n\n" },

    { "\n\n\n\nA", "\n\n\n\nA\n\n", 2, MKBUFDLOP(1, 1), "\n\n\nA", "\n\n\nA\n\n" },
    { "\n\n\n\nA", "\n\n\n\nA\n\n", 2, MKBUFDLOP(2, 1), "\n\n\nA", "\n\n\nA\n\n" },
    { "\n\n\n\nA", "\n\n\n\nA\n\n", 2, MKBUFDLOP(3, 1), "\n\n\nA", "\n\n\nA\n\n" },
    { "\n\n\n\nA", "\n\n\n\nA\n\n", 2, MKBUFDLOP(4, 1), "\n\n\n\n", "\n\n" },

    { "\n\n\n\nA", "\n\n\n\nA\n\n", 2, MKBUFDLOP(2, 2), "\n\nA", "\n\nA\n\n" },
    { "\n\n\n\nA", "\n\n\n\nA\n\n", 2, MKBUFDLOP(2, 3), "\n\n", "\n\n" },
    { "\n\n\n\nA", "\n\n\n\nA\n\n", 2, MKBUFDLOP(3, 2), "\n\n\n", "\n\n" },
  };

  guint i;

  for(i = 0; i < sizeof(TESTS)/sizeof(TESTS[0]); ++i)
  {
    printf("Test %u... ", i);
    if(!test_fixline_struct(&TESTS[i]))
      return 1;
    printf("OK\n");
  }

  return 0;
}

/* vim:set et sw=2 ts=2: */
