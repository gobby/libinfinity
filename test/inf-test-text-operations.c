/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2013 Armin Burgmeier <armin@arbur.net>
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

#include <libinftext/inf-text-default-insert-operation.h>
#include <libinftext/inf-text-default-delete-operation.h>
#include <libinftext/inf-text-insert-operation.h>
#include <libinftext/inf-text-delete-operation.h>
#include <libinftext/inf-text-default-buffer.h>
#include <libinftext/inf-text-buffer.h>
#include <libinftext/inf-text-chunk.h>
#include <libinftext/inf-text-user.h>
#include <libinfinity/adopted/inf-adopted-no-operation.h>
#include <libinfinity/adopted/inf-adopted-operation.h>
#include <libinfinity/adopted/inf-adopted-user.h>

#include <string.h>

typedef struct {
  guint total;
  guint passed;
} test_result;

typedef enum {
  OP_INS,
  OP_DEL
} operation_type;

typedef struct {
  operation_type type;
  guint offset;
  const gchar* text;
} operation_def;

static const operation_def OPERATIONS[] = {
  { OP_INS, 4, "a" },
  { OP_INS, 4, "b" },
  { OP_INS, 4, "c" },
  { OP_INS, 4, "a" },
  { OP_INS, 2, "ac" },
  { OP_INS, 3, "bc" },
  { OP_INS, 2, "gro" },
  { OP_DEL, 0, GUINT_TO_POINTER(1) },
  { OP_DEL, 0, GUINT_TO_POINTER(5) },
  { OP_DEL, 2, GUINT_TO_POINTER(7) },
  { OP_DEL, 1, GUINT_TO_POINTER(9) }
};

static const gchar EXAMPLE_DOCUMENT[] = "abcdefghijklmnopqrstuvwxyz";

static InfAdoptedOperation*
def_to_operation(const operation_def* def,
                 InfTextChunk* document,
                 guint user)
{
  InfTextChunk* chunk;
  InfAdoptedOperation* operation;

  switch(def->type)
  {
  case OP_INS:
    chunk = inf_text_chunk_new("UTF-8");
    inf_text_chunk_insert_text(
      chunk,
      0,
      def->text,
      strlen(def->text),
      strlen(def->text),
      user
    );

    operation = INF_ADOPTED_OPERATION(
      inf_text_default_insert_operation_new(def->offset, chunk)
    );

    inf_text_chunk_free(chunk);
    break;
  case OP_DEL:
    chunk = inf_text_chunk_substring(
      document,
      def->offset,
      GPOINTER_TO_UINT(def->text)
    );

    operation = INF_ADOPTED_OPERATION(
      inf_text_default_delete_operation_new(def->offset, chunk)
    );

    inf_text_chunk_free(chunk);
    break;
  default:
    g_assert_not_reached();
    break;
  }

  return operation;
}

static gboolean
insert_operation_equal(InfTextDefaultInsertOperation* op1,
                       InfAdoptedOperation* op2)
{
  guint pos1;
  guint pos2;
  int result;

  if(INF_TEXT_IS_DEFAULT_INSERT_OPERATION(op2))
  {
    pos1 = inf_text_insert_operation_get_position(
      INF_TEXT_INSERT_OPERATION(op1)
    );
    pos2 = inf_text_insert_operation_get_position(
      INF_TEXT_INSERT_OPERATION(op2)
    );

    if(pos1 != pos2)
      return FALSE;
    
    result = inf_text_chunk_equal(
      inf_text_default_insert_operation_get_chunk(op1),
      inf_text_default_insert_operation_get_chunk(
        INF_TEXT_DEFAULT_INSERT_OPERATION(op2)
      )
    );

    return result;
  }
  else
  {
    return FALSE;
  }
}

static gboolean
delete_operation_equal(InfTextDefaultDeleteOperation* op1,
                       InfAdoptedOperation* op2)
{
  guint pos1;
  guint pos2;
  InfTextChunk* chunk1;
  InfTextChunk* chunk2;

  if(INF_TEXT_IS_DEFAULT_DELETE_OPERATION(op2))
  {
    pos1 = inf_text_delete_operation_get_position(
      INF_TEXT_DELETE_OPERATION(op1)
    );

    pos2 = inf_text_delete_operation_get_position(
      INF_TEXT_DELETE_OPERATION(op2)
    );

    chunk1 = inf_text_default_delete_operation_get_chunk(
      INF_TEXT_DEFAULT_DELETE_OPERATION(op1)
    );

    chunk2 = inf_text_default_delete_operation_get_chunk(
      INF_TEXT_DEFAULT_DELETE_OPERATION(op2)
    );

    /* Both are no-op */
    if(inf_text_chunk_get_length(chunk1) == 0 &&
       inf_text_chunk_get_length(chunk2) == 0)
    {
      return TRUE;
    }

    if(pos1 != pos2) return FALSE;
    return inf_text_chunk_equal(chunk1, chunk2);
  }
  else
  {
    return FALSE;
  }
}

/* Required by split_operation_equal */
static gboolean
operation_equal(InfAdoptedOperation* op1,
                InfAdoptedOperation* op2);

static gboolean
split_operation_equal(InfAdoptedSplitOperation* op1,
                      InfAdoptedOperation* op2)
{
  GSList* first_list;
  GSList* second_list;
  GSList* first_item;
  GSList* second_item;
  InfAdoptedOperation* first_op;
  InfAdoptedOperation* second_op;

  first_list = inf_adopted_split_operation_unsplit(op1);
  if(INF_ADOPTED_IS_SPLIT_OPERATION(op2))
  {
    second_list = inf_adopted_split_operation_unsplit(
      INF_ADOPTED_SPLIT_OPERATION(op2)
    );
  }
  else
  {
    second_list = g_slist_prepend(NULL, op2);
  }

  /* Order in split operations does not matter. */
  for(first_item = first_list;
      first_item != NULL;
      first_item = g_slist_next(first_item))
  {
    first_op = first_item->data;

    /* Ignore noop operations */
    if(INF_TEXT_IS_DELETE_OPERATION(first_op))
      if(inf_text_delete_operation_get_length(INF_TEXT_DELETE_OPERATION(first_op)) == 0)
        continue;

    if(INF_ADOPTED_IS_NO_OPERATION(first_op))
      continue;

    for(second_item = second_list; second_item != NULL; second_item = g_slist_next(second_item))
    {
      second_op = second_item->data;
      if(operation_equal(first_op, second_op) == TRUE)
        break;
    }

    if(second_item == NULL)
    {
      g_slist_free(first_list);
      g_slist_free(second_list);
      return FALSE;
    }
  }

  for(second_item = second_list; second_item != NULL; second_item = g_slist_next(second_item))
  {
    second_op = second_item->data;

    /* Ignore noop operations */
    if(INF_TEXT_IS_DELETE_OPERATION(second_op))
      if(inf_text_delete_operation_get_length(INF_TEXT_DELETE_OPERATION(second_op)) == 0)
        continue;

    if(INF_ADOPTED_IS_NO_OPERATION(second_op))
      continue;

    for(first_item = first_list; first_item != NULL; first_item = g_slist_next(first_item))
    {
      first_op = first_item->data;
      if(operation_equal(first_op, second_op) == TRUE)
        break;
    }
    
    if(first_item == NULL)
    {
      g_slist_free(first_list);
      g_slist_free(second_list);
      return FALSE;
    }
  }
  
  g_slist_free(first_list);
  g_slist_free(second_list);
  return TRUE;
}

static gboolean
operation_equal(InfAdoptedOperation* op1,
                InfAdoptedOperation* op2)
{
  if(INF_ADOPTED_IS_SPLIT_OPERATION(op1))
    return split_operation_equal(INF_ADOPTED_SPLIT_OPERATION(op1), op2);
  else if(INF_ADOPTED_IS_SPLIT_OPERATION(op2))
    return split_operation_equal(INF_ADOPTED_SPLIT_OPERATION(op2), op1);
  else if(INF_TEXT_IS_DEFAULT_INSERT_OPERATION(op1))
    return insert_operation_equal(INF_TEXT_DEFAULT_INSERT_OPERATION(op1), op2);
  else if(INF_TEXT_IS_DEFAULT_DELETE_OPERATION(op1))
    return delete_operation_equal(INF_TEXT_DEFAULT_DELETE_OPERATION(op1), op2);
  else
    g_assert_not_reached();
}

static gboolean
test_c1(InfAdoptedOperation* op1,
        InfAdoptedOperation* op2,
        InfAdoptedUser* user1,
        InfAdoptedUser* user2,
        InfAdoptedConcurrencyId cid12)
{
  InfTextDefaultBuffer* first;
  InfTextDefaultBuffer* second;
  InfTextChunk* first_chunk;
  InfTextChunk* second_chunk;
  InfAdoptedOperation* transformed;
  int result;

  first = inf_text_default_buffer_new("UTF-8");

  inf_text_buffer_insert_text(
    INF_TEXT_BUFFER(first),
    0,
    EXAMPLE_DOCUMENT,
    strlen(EXAMPLE_DOCUMENT),
    strlen(EXAMPLE_DOCUMENT),
    NULL
  );

  second = inf_text_default_buffer_new("UTF-8");
  inf_text_buffer_insert_text(
    INF_TEXT_BUFFER(second),
    0,
    EXAMPLE_DOCUMENT,
    strlen(EXAMPLE_DOCUMENT),
    strlen(EXAMPLE_DOCUMENT),
    NULL
  );

  inf_adopted_operation_apply(op1, user1, INF_BUFFER(first));
  transformed = inf_adopted_operation_transform(op2, op1, -cid12);
  inf_adopted_operation_apply(transformed, user2, INF_BUFFER(first));
  g_object_unref(G_OBJECT(transformed));

  inf_adopted_operation_apply(op2, user2, INF_BUFFER(second));
  transformed = inf_adopted_operation_transform(op1, op2, cid12);
  inf_adopted_operation_apply(transformed, user1, INF_BUFFER(second));
  g_object_unref(G_OBJECT(transformed));

  first_chunk = inf_text_buffer_get_slice(
    INF_TEXT_BUFFER(first),
    0,
    inf_text_buffer_get_length(INF_TEXT_BUFFER(first))
  );
  second_chunk = inf_text_buffer_get_slice(
    INF_TEXT_BUFFER(second),
    0,
    inf_text_buffer_get_length(INF_TEXT_BUFFER(second))
  );

  result = inf_text_chunk_equal(first_chunk, second_chunk);

  inf_text_chunk_free(first_chunk);
  inf_text_chunk_free(second_chunk);
  g_object_unref(G_OBJECT(first));
  g_object_unref(G_OBJECT(second));

  return result;
}

static gboolean
test_c2(InfAdoptedOperation* op1,
        InfAdoptedOperation* op2,
        InfAdoptedOperation* op3,
        InfAdoptedConcurrencyId cid12,
        InfAdoptedConcurrencyId cid13,
        InfAdoptedConcurrencyId cid23)
{
  InfAdoptedOperation* temp1;
  InfAdoptedOperation* temp2;
  InfAdoptedOperation* result1;
  InfAdoptedOperation* result2;
  InfAdoptedConcurrencyId cid;
  gboolean retval;

  temp1 = inf_adopted_operation_transform(op2, op1, -cid12);
  temp2 = inf_adopted_operation_transform(op3, op1, -cid13);

  cid = INF_ADOPTED_CONCURRENCY_NONE;
  if(inf_adopted_operation_need_concurrency_id(temp2, temp1))
    cid = inf_adopted_operation_get_concurrency_id(op3, op2);
  if(cid == INF_ADOPTED_CONCURRENCY_NONE)
    cid = -cid23;

  result1 = inf_adopted_operation_transform(temp2, temp1, cid);
  g_object_unref(G_OBJECT(temp1));
  g_object_unref(G_OBJECT(temp2));

  temp1 = inf_adopted_operation_transform(op1, op2, cid12);
  temp2 = inf_adopted_operation_transform(op3, op2, -cid23);

  cid = INF_ADOPTED_CONCURRENCY_NONE;
  if(inf_adopted_operation_need_concurrency_id(temp2, temp1))
    cid = inf_adopted_operation_get_concurrency_id(op3, op1);
  if(cid == INF_ADOPTED_CONCURRENCY_NONE)
    cid = -cid13;

  result2 = inf_adopted_operation_transform(temp2, temp1, cid);
  g_object_unref(G_OBJECT(temp1));
  g_object_unref(G_OBJECT(temp2));

  retval = operation_equal(result1, result2);

  g_object_unref(G_OBJECT(result1));
  g_object_unref(G_OBJECT(result2));
  return retval;
}

static InfAdoptedConcurrencyId
cid(InfAdoptedOperation** first,
    InfAdoptedOperation** second)
{
  if(first > second)
    return INF_ADOPTED_CONCURRENCY_SELF;
  else if(first < second)
    return INF_ADOPTED_CONCURRENCY_OTHER;
  else
    g_assert_not_reached();
}

static void
perform_c1(InfAdoptedOperation** begin,
           InfAdoptedOperation** end,
           InfAdoptedUser** users,
           test_result* result)
{
  InfAdoptedOperation** _1;
  InfAdoptedOperation** _2;

  for(_1 = begin; _1 != end; ++ _1)
  {
    for(_2 = begin; _2 != end; ++ _2)
    {
      if(_1 != _2)
      {
        ++ result->total;
        if(test_c1(*_1, *_2, users[_1 - begin], users[_2 - begin], cid(_1, _2)))
          ++ result->passed;
      }
    }
  }
}

static void
perform_c2(InfAdoptedOperation** begin,
           InfAdoptedOperation** end,
           test_result* result)
{
  InfAdoptedOperation** _1;
  InfAdoptedOperation** _2;
  InfAdoptedOperation** _3;

  for(_1 = begin; _1 != end; ++ _1)
  {
    for(_2 = begin; _2 != end; ++ _2)
    {
      for(_3 = begin; _3 != end; ++ _3)
      {
        if(_1 != _2 && _1 != _3 && _2 != _3)
        {
          ++ result->total;
          if(test_c2(*_1, *_2, *_3, cid(_1, _2), cid(_1, _3), cid(_2, _3)))
            ++ result->passed;
        }
      }
    }
  }
}

int main()
{
  InfAdoptedOperation** operations;
  InfAdoptedUser** users;
  InfTextChunk* document;
  test_result result;
  guint i;
  int retval;

  g_type_init();

  retval = 0;

  operations = g_malloc(
    sizeof(InfAdoptedOperation*) * G_N_ELEMENTS(OPERATIONS)
  );
  
  users = g_malloc(sizeof(InfAdoptedUser*) * G_N_ELEMENTS(OPERATIONS));

  document = inf_text_chunk_new("UTF-8");
  inf_text_chunk_insert_text(
    document,
    0,
    EXAMPLE_DOCUMENT,
    strlen(EXAMPLE_DOCUMENT),
    strlen(EXAMPLE_DOCUMENT),
    0
  );

  for(i = 0; i < G_N_ELEMENTS(OPERATIONS); ++ i)
  {
    operations[i] = def_to_operation(&OPERATIONS[i], document, i + 1);
    users[i] = INF_ADOPTED_USER(
      g_object_new(INF_TEXT_TYPE_USER, "id", i + 1, NULL)
    );
  }

  inf_text_chunk_free(document);

  result.passed = 0;
  result.total = 0;
  perform_c1(operations, operations + G_N_ELEMENTS(OPERATIONS), users, &result);

  printf("C1: %u out of %u passed\n", result.passed, result.total);
  if(result.passed < result.total)
    retval = -1;

  result.passed = 0;
  result.total = 0;
  perform_c2(operations, operations + G_N_ELEMENTS(OPERATIONS), &result);

  printf("C2: %u out of %u passed\n", result.passed, result.total);
  if(result.passed < result.total)
    retval = -1;

  for(i = 0; i < G_N_ELEMENTS(OPERATIONS); ++ i)
  {
    g_object_unref(G_OBJECT(operations[i]));
    g_object_unref(G_OBJECT(users[i]));
  }

  g_free(operations);
  g_free(users);

  return retval;
}
