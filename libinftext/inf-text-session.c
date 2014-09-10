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

#include <libinftext/inf-text-session.h>
#include <libinftext/inf-text-default-insert-operation.h>
#include <libinftext/inf-text-default-delete-operation.h>
#include <libinftext/inf-text-remote-delete-operation.h>
#include <libinftext/inf-text-insert-operation.h>
#include <libinftext/inf-text-delete-operation.h>
#include <libinftext/inf-text-move-operation.h>
#include <libinftext/inf-text-chunk.h>
#include <libinftext/inf-text-user.h>
#include <libinfinity/adopted/inf-adopted-no-operation.h>
#include <libinfinity/common/inf-xml-util.h>
#include <libinfinity/common/inf-error.h>
#include <libinfinity/inf-i18n.h>
#include <libinfinity/inf-signals.h>

#include <libxml/tree.h>
#include <string.h>
#include <errno.h>

/* TODO: Optionally broadcast operations delayed to merge adjacent operations
 * and send as a single request. */

typedef struct _InfTextSessionLocalUser InfTextSessionLocalUser;
struct _InfTextSessionLocalUser {
  InfTextSession* session;
  InfTextUser* user;
  GTimeVal last_caret_update;
  InfIoTimeout* caret_timeout;
};

typedef struct _InfTextSessionPrivate InfTextSessionPrivate;
struct _InfTextSessionPrivate {
  guint caret_update_interval;
  GSList* local_users;
};

enum {
  PROP_0,

  PROP_CARET_UPDATE_INTERVAL
};

typedef struct _InfTextSessionInsertForeachData
  InfTextSessionInsertForeachData;
typedef struct _InfTextSessionEraseForeachData
  InfTextSessionEraseForeachData;

struct _InfTextSessionInsertForeachData {
  guint position;
  InfTextChunk* chunk;
  InfUser* user;
};

struct _InfTextSessionEraseForeachData {
  guint position;
  guint length;
  InfUser* user;
};

#define INF_TEXT_SESSION_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TEXT_TYPE_SESSION, InfTextSessionPrivate))

static GQuark inf_text_session_error_quark;

G_DEFINE_TYPE_WITH_CODE(InfTextSession, inf_text_session, INF_ADOPTED_TYPE_SESSION,
  G_ADD_PRIVATE(InfTextSession))

/*
 * Utility functions
 */

static gint64
inf_text_session_get_real_time()
{
  /* TODO: Replace by g_get_real_time() once we depend on glib >=2.28 */
  GTimeVal timeval;
  g_get_current_time(&timeval);
  return (gint64)timeval.tv_sec * 1000000 + timeval.tv_usec;
}

/* Returns the difference between two GTimeVal, in milliseconds */
static guint
inf_text_session_timeval_diff(GTimeVal* first,
                              GTimeVal* second)
{
  g_assert(first->tv_sec > second->tv_sec ||
           (first->tv_sec == second->tv_sec &&
            first->tv_usec >= second->tv_usec));

  /* Don't risk overflow, don't need to convert to signed int */
  return (first->tv_sec - second->tv_sec) * 1000 +
         (first->tv_usec+500)/1000 - (second->tv_usec+500)/1000;
}

/* Converts at most *bytes bytes with cd and writes the result, which are
 * at most 1024 bytes, into xml, setting the given author. *bytes will be
 * set to the number of bytes not yet processed. */
static void
inf_text_session_segment_to_xml(GIConv* cd,
                                xmlNodePtr xml,
                                gconstpointer text,
                                gsize* bytes, /* in/out */
                                guint author)
{
  gchar utf8_text[1024];
  gsize result;

  gsize bytes_left;

  gchar* inbuf;
  gchar* outbuf;

  bytes_left = 1024;

  inbuf = *(gchar**)(gpointer)&text; /* cast const away without warning */
  outbuf = utf8_text;

  result = g_iconv(
    *cd,
    &inbuf,
    bytes,
    &outbuf,
    &bytes_left
  );

  /* Conversion into UTF-8 should always succeed */
  g_assert(result == 0 || errno == E2BIG);

  inf_xml_util_add_child_text(xml, utf8_text, 1024 - bytes_left);
  inf_xml_util_set_attribute_uint(xml, "author", author);
}

static gpointer
inf_text_session_segment_from_xml(GIConv* cd,
                                  xmlNodePtr xml,
                                  guint* length,
                                  gsize* bytes,
                                  guint* author,
                                  GError** error)
{
  gsize bytes_read;
  gchar* utf8_text;
  gpointer text;

  if(!inf_xml_util_get_attribute_uint_required(xml, "author", author, error))
    return NULL;

  utf8_text = inf_xml_util_get_child_text(xml, &bytes_read, length, error);
  if(!utf8_text)
    return NULL;

  text = g_convert_with_iconv(
    utf8_text,
    bytes_read,
    *cd,
    NULL,
    bytes,
    error
  );

  g_free(utf8_text);
  return text;
}

/*
 * Caret/Selection handling
 */

static InfTextSessionLocalUser*
inf_text_session_find_local_user(InfTextSession* session,
                                 InfTextUser* user)
{
  InfTextSessionPrivate* priv;
  GSList* item;
  InfTextSessionLocalUser* local;

  priv = INF_TEXT_SESSION_PRIVATE(session);

  for(item = priv->local_users; item != NULL; item = g_slist_next(item))
  {
    local = (InfTextSessionLocalUser*)item->data;
    if(local->user == user)
      return local;
  }

  return NULL;
}

static void
inf_text_session_broadcast_caret_selection(InfTextSession* session,
                                           InfTextSessionLocalUser* local)
{
  InfAdoptedOperation* operation;
  InfAdoptedAlgorithm* algorithm;
  InfAdoptedRequest* request;
  guint buf_len;
  guint position;
  int sel;
  guint end;

  algorithm = inf_adopted_session_get_algorithm(INF_ADOPTED_SESSION(session));
  position = inf_text_user_get_caret_position(local->user);
  sel = inf_text_user_get_selection_length(local->user);
  end = position + sel;

  /* Clamp position and selection to buffer length. The only case when this is
   * needed is when a local user's position is beyond the end of the document
   * since there are some local document modifications. This can happen with
   * for example InfTextFixlineBuffer. */
  /* TODO: This should be handled more cleverly, by propagating the user
   * position and selection through the buffer, to make sure that at this
   * point it is always consistent with the infinote view of the buffer. */
  buf_len = inf_text_buffer_get_length(
    INF_TEXT_BUFFER(inf_session_get_buffer(INF_SESSION(session)))
  );

  if(position > buf_len)
    position = buf_len;
  if(end > buf_len)
    end = buf_len;

  if(end >= position)
    sel = (int)(end - position);
  else
    sel = -(int)(position - end);

  operation = INF_ADOPTED_OPERATION(
    inf_text_move_operation_new(position, sel)
  );

  request = inf_adopted_algorithm_generate_request(
    algorithm,
    INF_ADOPTED_REQUEST_DO,
    INF_ADOPTED_USER(local->user),
    operation
  );

  /* This cannot fail since operation is not applied */
  inf_adopted_algorithm_execute_request(algorithm, request, FALSE, NULL);

  g_object_unref(operation);

  inf_adopted_session_broadcast_request(
    INF_ADOPTED_SESSION(session),
    request
  );

  g_object_unref(request);

  g_get_current_time(&local->last_caret_update);

  if(local->caret_timeout != NULL)
  {
    inf_io_remove_timeout(
      inf_adopted_session_get_io(INF_ADOPTED_SESSION(session)),
      local->caret_timeout
    );

    local->caret_timeout = NULL;
  }
}

static void
inf_text_session_caret_update_timeout_func(gpointer user_data)
{
  InfTextSessionLocalUser* local;
  local = (InfTextSessionLocalUser*)user_data;

  local->caret_timeout = NULL;
  inf_text_session_broadcast_caret_selection(local->session, local);
}

static void
inf_text_session_selection_changed_cb(InfTextUser* user,
                                      guint position,
                                      gint sel,
                                      gboolean by_request,
                                      gpointer user_data)
{
  InfTextSession* session;
  InfTextSessionPrivate* priv;
  InfAdoptedAlgorithm* algorithm;
  InfAdoptedRequest* execute_request;
  InfTextSessionLocalUser* local;
  GTimeVal current;
  guint diff;

  session = INF_TEXT_SESSION(user_data);
  priv = INF_TEXT_SESSION_PRIVATE(session);
  algorithm = inf_adopted_session_get_algorithm(INF_ADOPTED_SESSION(session));
  execute_request = inf_adopted_algorithm_get_execute_request(algorithm);

  /* We should block all changes that have by_request set to FALSE... breaks
   * if someone else does that... should maybe emit a warning instead. */
  g_assert( (execute_request != NULL && by_request == FALSE) ||
            (execute_request == NULL && by_request == TRUE));

  if(execute_request == NULL)
  {
    local = inf_text_session_find_local_user(session, user);
    g_assert(local != NULL);

    g_get_current_time(&current);
    diff = inf_text_session_timeval_diff(&current, &local->last_caret_update);

    if(diff < priv->caret_update_interval)
    {
      if(local->caret_timeout == NULL)
      {
        /* TODO: Interrupt timeout if a -caret request is sent from that
         * local user. */
        local->caret_timeout = inf_io_add_timeout(
          inf_adopted_session_get_io(INF_ADOPTED_SESSION(local->session)),
          priv->caret_update_interval - diff,
          inf_text_session_caret_update_timeout_func,
          local,
          NULL
        );
      }
    }
    else
    {
      inf_text_session_broadcast_caret_selection(session, local);
    }
  }
}

static void
inf_text_session_add_local_user(InfTextSession* session,
                                InfTextUser* user)
{
  InfTextSessionPrivate* priv;
  InfTextSessionLocalUser* local;

  priv = INF_TEXT_SESSION_PRIVATE(session);

  local = g_slice_new(InfTextSessionLocalUser);
  local->session = session;
  local->user = user;
  g_get_current_time(&local->last_caret_update);
  local->caret_timeout = NULL;

  priv->local_users = g_slist_prepend(priv->local_users, local);

  g_signal_connect_after(
    G_OBJECT(user),
    "selection-changed",
    G_CALLBACK(inf_text_session_selection_changed_cb),
    session
  );
}

static void
inf_text_session_remove_local_user(InfTextSession* session,
                                   InfTextSessionLocalUser* local)
{
  InfTextSessionPrivate* priv;

  priv = INF_TEXT_SESSION_PRIVATE(session);

  if(local->caret_timeout != NULL)
  {
    inf_io_remove_timeout(
      inf_adopted_session_get_io(INF_ADOPTED_SESSION(session)),
      local->caret_timeout
    );
  }

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(local->user),
    G_CALLBACK(inf_text_session_selection_changed_cb),
    session
  );

  g_slice_free(InfTextSessionLocalUser, local);
  priv->local_users = g_slist_remove(priv->local_users, local);
}

static void
inf_text_session_local_user_added_cb(InfUserTable* user_table,
                                     InfUser* user,
                                     gpointer user_data)
{
  g_assert(INF_TEXT_IS_USER(user));

  inf_text_session_add_local_user(
    INF_TEXT_SESSION(user_data),
    INF_TEXT_USER(user)
  );
}

static void
inf_text_session_local_user_removed_cb(InfUserTable* user_table,
                                       InfUser* user,
                                       gpointer user_data)
{
  InfTextSession* session;
  InfTextSessionLocalUser* local;

  g_assert(INF_TEXT_IS_USER(user));

  session = INF_TEXT_SESSION(user_data);
  local = inf_text_session_find_local_user(session, INF_TEXT_USER(user));
  g_assert(local != NULL);

  inf_text_session_remove_local_user(session, local);
}

static void
inf_text_session_block_local_users_selection_changed(InfTextSession* session)
{
  InfTextSessionPrivate* priv;
  GSList* item;
  InfTextSessionLocalUser* local;

  priv = INF_TEXT_SESSION_PRIVATE(session);

  for(item = priv->local_users; item != NULL; item = g_slist_next(item))
  {
    local = (InfTextSessionLocalUser*)item->data;

    inf_signal_handlers_block_by_func(
      G_OBJECT(local->user),
      G_CALLBACK(inf_text_session_selection_changed_cb),
      session
    );
  }
}

static void
inf_text_session_unblock_local_users_selection_changed(InfTextSession* sess)
{
  InfTextSessionPrivate* priv;
  GSList* item;
  InfTextSessionLocalUser* local;

  priv = INF_TEXT_SESSION_PRIVATE(sess);

  for(item = priv->local_users; item != NULL; item = g_slist_next(item))
  {
    local = (InfTextSessionLocalUser*)item->data;

    inf_signal_handlers_unblock_by_func(
      G_OBJECT(local->user),
      G_CALLBACK(inf_text_session_selection_changed_cb),
      sess
    );
  }
}

static void
inf_text_session_buffer_text_inserted_cb_foreach_func(InfUser* user,
                                                      gpointer user_data)
{
  InfTextSessionInsertForeachData* data;
  guint position;
  gint length;

  data = (InfTextSessionInsertForeachData*)user_data;
  if(inf_user_get_status(user) != INF_USER_UNAVAILABLE)
  {
    /* TODO: Handle separately if insert-caret */
    position = inf_text_user_get_caret_position(INF_TEXT_USER(user));
    length = inf_text_user_get_selection_length(INF_TEXT_USER(user));

    inf_text_move_operation_transform_insert(
      data->position,
      inf_text_chunk_get_length(data->chunk),
      &position,
      &length,
      /* Right gravity for local insertions, left gravity for remote ones */
      user == data->user ? FALSE : TRUE
    );

    inf_text_user_set_selection(
      INF_TEXT_USER(user),
      position,
      length,
      user == data->user ? TRUE : FALSE
    );
  }
}

static void
inf_text_session_buffer_text_erased_cb_foreach_func(InfUser* user,
                                                    gpointer user_data)
{
  InfTextSessionEraseForeachData* data;
  guint position;
  gint length;

  data = (InfTextSessionEraseForeachData*)user_data;
  if(inf_user_get_status(user) != INF_USER_UNAVAILABLE)
  {
    /* TODO: Handle separately if erase-caret */
    position = inf_text_user_get_caret_position(INF_TEXT_USER(user));
    length = inf_text_user_get_selection_length(INF_TEXT_USER(user));

    inf_text_move_operation_transform_delete(
      data->position,
      data->length,
      &position,
      &length
    );

    inf_text_user_set_selection(
      INF_TEXT_USER(user),
      position,
      length,
      user == data->user ? TRUE : FALSE
    );
  }
}

/* The after handlers readjust the caret and selection properties of the
 * users. Block handlers so we don't broadcast this. */
static void
inf_text_session_buffer_text_inserted_cb(InfTextBuffer* buffer,
                                         guint pos,
                                         InfTextChunk* chunk,
                                         InfUser* user,
                                         gpointer user_data)
{
  InfTextSession* session;
  InfTextSessionPrivate* priv;
  InfUserTable* user_table;
  InfAdoptedAlgorithm* algorithm;
  InfAdoptedRequest* execute_request;

  InfAdoptedOperation* operation;
  InfAdoptedRequest* request;
  InfTextSessionInsertForeachData data;

  g_assert(INF_TEXT_IS_USER(user));

  session = INF_TEXT_SESSION(user_data);
  priv = INF_TEXT_SESSION_PRIVATE(session);
  user_table = inf_session_get_user_table(INF_SESSION(session));
  algorithm = inf_adopted_session_get_algorithm(INF_ADOPTED_SESSION(session));
  execute_request = inf_adopted_algorithm_get_execute_request(algorithm);

  if(execute_request == NULL)
  {
    operation = INF_ADOPTED_OPERATION(
      inf_text_default_insert_operation_new(pos, chunk)
    );

    request = inf_adopted_algorithm_generate_request(
      algorithm,
      INF_ADOPTED_REQUEST_DO,
      INF_ADOPTED_USER(user),
      operation
    );

    /* This cannot fail since operation is not applied */
    inf_adopted_algorithm_execute_request(algorithm, request, FALSE, NULL);

    inf_adopted_session_broadcast_request(
      INF_ADOPTED_SESSION(session),
      request
    );

    g_object_unref(request);
    g_object_unref(operation);
  }

  data.position = pos;
  data.chunk = chunk;
  data.user = user;

  inf_text_session_block_local_users_selection_changed(session);

  inf_user_table_foreach_user(
    user_table,
    inf_text_session_buffer_text_inserted_cb_foreach_func,
    &data
  );

#if 0
  /* TODO: If that was an insert-caret request, then do this: */
  if(user != NULL)
  {
    inf_text_user_set_selection(
      INF_TEXT_USER(user),
      pos + inf_text_chunk_get_length(chunk),
      0,
      TRUE
    );
  }
#endif

  inf_text_session_unblock_local_users_selection_changed(session);
}

static void
inf_text_session_buffer_text_erased_cb(InfTextBuffer* buffer,
                                       guint pos,
                                       InfTextChunk* chunk,
                                       InfUser* user,
                                       gpointer user_data)
{
  InfTextSession* session;
  InfTextSessionPrivate* priv;
  InfUserTable* user_table;
  InfAdoptedAlgorithm* algorithm;
  InfAdoptedRequest* execute_request;

  InfAdoptedOperation* operation;
  InfAdoptedRequest* request;
  InfTextSessionEraseForeachData data;

  g_assert(INF_TEXT_IS_USER(user));

  session = INF_TEXT_SESSION(user_data);
  priv = INF_TEXT_SESSION_PRIVATE(session);
  user_table = inf_session_get_user_table(INF_SESSION(session));
  algorithm = inf_adopted_session_get_algorithm(INF_ADOPTED_SESSION(session));
  execute_request = inf_adopted_algorithm_get_execute_request(algorithm);

  if(execute_request == NULL)
  {
    operation = INF_ADOPTED_OPERATION(
      inf_text_default_delete_operation_new(pos, chunk)
    );

    request = inf_adopted_algorithm_generate_request(
      algorithm,
      INF_ADOPTED_REQUEST_DO,
      INF_ADOPTED_USER(user),
      operation
    );

    /* This cannot fail since operation is not applied */
    inf_adopted_algorithm_execute_request(algorithm, request, FALSE, NULL);

    inf_adopted_session_broadcast_request(
      INF_ADOPTED_SESSION(session),
      request
    );

    g_object_unref(request);
    g_object_unref(operation);
  }

  data.position = pos;
  data.length = inf_text_chunk_get_length(chunk);
  data.user = user;

  inf_text_session_block_local_users_selection_changed(session);

  inf_user_table_foreach_user(
    user_table,
    inf_text_session_buffer_text_erased_cb_foreach_func,
    &data
  );

  /* TODO: If that was an erase-caret request, then do this: */
#if 0
  if(user != NULL)
    inf_text_user_set_selection(INF_TEXT_USER(user), pos, 0, TRUE);
#endif

  inf_text_session_unblock_local_users_selection_changed(session);
}

static void
inf_text_session_init_text_handlers_user_foreach_func(InfUser* user,
                                                      gpointer user_data)
{
  g_assert(INF_TEXT_IS_USER(user));

  inf_text_session_add_local_user(
    INF_TEXT_SESSION(user_data),
    INF_TEXT_USER(user)
  );
}

static void
inf_text_session_init_text_handlers(InfTextSession* session)
{
  InfTextBuffer* buffer;
  InfAdoptedAlgorithm* algorithm;
  InfUserTable* user_table;

  buffer = INF_TEXT_BUFFER(inf_session_get_buffer(INF_SESSION(session)));
  algorithm = inf_adopted_session_get_algorithm(INF_ADOPTED_SESSION(session));
  user_table = inf_session_get_user_table(INF_SESSION(session));

  g_signal_connect(
    G_OBJECT(buffer),
    "text-inserted",
    G_CALLBACK(inf_text_session_buffer_text_inserted_cb),
    session
  );

  g_signal_connect(
    G_OBJECT(buffer),
    "text-erased",
    G_CALLBACK(inf_text_session_buffer_text_erased_cb),
    session
  );

  g_signal_connect(
    G_OBJECT(user_table),
    "add-local-user",
    G_CALLBACK(inf_text_session_local_user_added_cb),
    session
  );

  g_signal_connect(
    G_OBJECT(user_table),
    "remove-local-user",
    G_CALLBACK(inf_text_session_local_user_removed_cb),
    session
  );

  inf_user_table_foreach_local_user(
    user_table,
    inf_text_session_init_text_handlers_user_foreach_func,
    session
  );
}

/*
 * GObject overrides.
 */

static void
inf_text_session_init(InfTextSession* session)
{
  InfTextSessionPrivate* priv;
  priv = INF_TEXT_SESSION_PRIVATE(session);

  priv->caret_update_interval = 500;
}

static GObject*
inf_text_session_constructor(GType type,
                             guint n_construct_properties,
                             GObjectConstructParam* construct_properties)
{
  GObject* object;
  InfTextSession* session;
  InfTextSessionPrivate* priv;
  InfTextBuffer* buffer;
  InfSessionStatus status;

  object = G_OBJECT_CLASS(inf_text_session_parent_class)->constructor(
    type,
    n_construct_properties,
    construct_properties
  );

  session = INF_TEXT_SESSION(object);
  priv = INF_TEXT_SESSION_PRIVATE(session);

  buffer = INF_TEXT_BUFFER(inf_session_get_buffer(INF_SESSION(session)));
  g_object_get(G_OBJECT(session), "status", &status, NULL);

  /* We can either be already synchronized in which case we use the given
   * buffer as initial buffer. This is used to initiate a new session with
   * predefined content. In that case, we can directly start through. In the
   * other case we are getting synchronized in which case the buffer must be
   * empty (we will fill it during synchronization). Text handlers are
   * connected when synchronization is complete. */
  g_assert(
    status == INF_SESSION_RUNNING ||
    inf_text_buffer_get_length(buffer) == 0
  );

  if(status == INF_SESSION_RUNNING)
    inf_text_session_init_text_handlers(session);

  return object;
}

/*static void
inf_text_session_dispose_foreach_local_user_func(InfUser* user,
                                                 gpointer user_data)
{
  g_assert(INF_TEXT_IS_USER(user));

  g_signal_handlers_disconnect_matched(
    G_OBJECT(user),
    G_SIGNAL_MATCH_FUNC,
    0,
    0,
    NULL,
    G_CALLBACK(inf_text_session_selection_changed_cb),
    NULL
  );
}*/

static void
inf_text_session_dispose(GObject* object)
{
  InfTextSession* session;
  InfTextSessionPrivate* priv;
  InfTextBuffer* buffer;
  InfUserTable* user_table;
  InfAdoptedAlgorithm* algorithm;

  session = INF_TEXT_SESSION(object);
  priv = INF_TEXT_SESSION_PRIVATE(session);

  buffer = INF_TEXT_BUFFER(inf_session_get_buffer(INF_SESSION(session)));
  user_table = inf_session_get_user_table(INF_SESSION(session));
  algorithm = inf_adopted_session_get_algorithm(INF_ADOPTED_SESSION(session));

  while(priv->local_users != NULL)
  {
    inf_text_session_remove_local_user(
      session,
      (InfTextSessionLocalUser*)priv->local_users->data
    );
  }

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(buffer),
    G_CALLBACK(inf_text_session_buffer_text_inserted_cb),
    session
  );

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(buffer),
    G_CALLBACK(inf_text_session_buffer_text_erased_cb),
    session
  );

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(user_table),
    G_CALLBACK(inf_text_session_local_user_added_cb),
    session
  );

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(user_table),
    G_CALLBACK(inf_text_session_local_user_removed_cb),
    session
  );

  G_OBJECT_CLASS(inf_text_session_parent_class)->dispose(object);
}

static void
inf_text_session_finalize(GObject* object)
{
  InfTextSession* session;
  InfTextSessionPrivate* priv;

  session = INF_TEXT_SESSION(object);
  priv = INF_TEXT_SESSION_PRIVATE(session);

  G_OBJECT_CLASS(inf_text_session_parent_class)->finalize(object);
}

static void
inf_text_session_set_property(GObject* object,
                              guint prop_id,
                              const GValue* value,
                              GParamSpec* pspec)
{
  InfTextSession* session;
  InfTextSessionPrivate* priv;

  session = INF_TEXT_SESSION(object);
  priv = INF_TEXT_SESSION_PRIVATE(session);

  switch(prop_id)
  {
  case PROP_CARET_UPDATE_INTERVAL:
    priv->caret_update_interval = g_value_get_uint(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_text_session_get_property(GObject* object,
                              guint prop_id,
                              GValue* value,
                              GParamSpec* pspec)
{
  InfTextSession* session;
  InfTextSessionPrivate* priv;

  session = INF_TEXT_SESSION(object);
  priv = INF_TEXT_SESSION_PRIVATE(session);

  switch(prop_id)
  {
  case PROP_CARET_UPDATE_INTERVAL:
    g_value_set_uint(value, priv->caret_update_interval);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

/*
 * Network command handlers
 */

static InfCommunicationScope
inf_text_session_handle_user_color_change(InfTextSession* session,
                                          InfXmlConnection* connection,
                                          xmlNodePtr xml,
                                          GError** error)
{
  InfUserTable* user_table;
  guint user_id;
  InfUser* user;
  gdouble hue;

  user_table = inf_session_get_user_table(INF_SESSION(session));

  if(!inf_xml_util_get_attribute_uint_required(xml, "id", &user_id, error))
    return INF_COMMUNICATION_SCOPE_PTP;
  if(!inf_xml_util_get_attribute_double_required(xml, "hue", &hue, error))
    return INF_COMMUNICATION_SCOPE_PTP;

  /* TODO: A public function in InfSession that does the following two checks
   * (and returns the user). This can also be used in
   * inf_session_handle_user_status_change */
  user = inf_user_table_lookup_user_by_id(user_table, user_id);
  if(user == NULL)
  {
    g_set_error(
      error,
      inf_user_error_quark(),
      INF_USER_ERROR_NO_SUCH_USER,
      _("No such user with ID '%u'"),
      user_id
    );

    return INF_COMMUNICATION_SCOPE_PTP;
  }

  if(inf_user_get_status(user) == INF_USER_UNAVAILABLE ||
     inf_user_get_connection(user) != connection)
  {
    g_set_error(
      error,
      inf_user_error_quark(),
      INF_USER_ERROR_NOT_JOINED,
      "%s",
      _("User did not join from this connection")
    );

    return INF_COMMUNICATION_SCOPE_PTP;
  }

  g_assert(INF_TEXT_IS_USER(user));

  if(hue < 0.0 || hue > 1.0)
  {
    g_set_error(
      error,
      inf_text_session_error_quark,
      INF_TEXT_SESSION_ERROR_INVALID_HUE,
      _("Invalid hue value: '%g'"),
      hue
    );

    return INF_COMMUNICATION_SCOPE_PTP;
  }

  g_object_set(G_OBJECT(user), "hue", hue, NULL);
  return INF_COMMUNICATION_SCOPE_GROUP;
}

/*
 * InfSession overrides
 */

static void
inf_text_session_to_xml_sync(InfSession* session,
                             xmlNodePtr parent)
{
  InfTextBuffer* buffer;
  InfTextBufferIter* iter;
  xmlNodePtr xml;
  gboolean result;

  gchar* text;
  gsize total_bytes;
  gsize bytes_left;
  GIConv cd;

  INF_SESSION_CLASS(inf_text_session_parent_class)->to_xml_sync(
    session,
    parent
  );

  buffer = INF_TEXT_BUFFER(inf_session_get_buffer(session));
  cd = g_iconv_open("UTF-8", inf_text_buffer_get_encoding(buffer));

  iter = inf_text_buffer_create_begin_iter(buffer);
  if(iter != NULL)
  {
    result = TRUE;
    while(result == TRUE)
    {
      /* Write segment in 1024 byte chunks */
      text = inf_text_buffer_iter_get_text(buffer, iter);
      total_bytes = inf_text_buffer_iter_get_bytes(buffer, iter);
      bytes_left = total_bytes;

      while(bytes_left > 0)
      {
        xml = xmlNewChild(parent, NULL, (const xmlChar*)"sync-segment", NULL);
        inf_text_session_segment_to_xml(
          &cd,
          xml,
          text + total_bytes - bytes_left,
          &bytes_left,
          inf_text_buffer_iter_get_author(buffer, iter)
        );
      }

      g_free(text);
      result = inf_text_buffer_iter_next(buffer, iter);
    }

    inf_text_buffer_destroy_iter(buffer, iter);
  }

  g_iconv_close(cd);
}

static gboolean
inf_text_session_process_xml_sync(InfSession* session,
                                  InfXmlConnection* connection,
                                  const xmlNodePtr xml,
                                  GError** error)
{
  InfTextBuffer* buffer;
  GIConv cd;

  gpointer text;
  gsize bytes;
  guint length;
  guint author;
  InfUser* user;

  if(strcmp((const char*)xml->name, "sync-segment") == 0)
  {
    buffer = INF_TEXT_BUFFER(inf_session_get_buffer(session));
    cd = g_iconv_open(inf_text_buffer_get_encoding(buffer), "UTF-8");

    text = inf_text_session_segment_from_xml(
      &cd,
      xml,
      &length,
      &bytes,
      &author,
      error
    );

    g_iconv_close(cd);
    if(text == NULL) return FALSE;

    if(author != 0)
    {
      /* TODO: inf_user_table_lookup_user_by_id_required, with error. */
      user = inf_user_table_lookup_user_by_id(
        inf_session_get_user_table(session),
        author
      );

      if(user == NULL)
      {
        g_free(text);

        g_set_error(
          error,
          inf_user_error_quark(),
          INF_USER_ERROR_NO_SUCH_USER,
          _("No such user with ID '%u'"),
          author
        );

        return FALSE;
      }
    }
    else
    {
      user = NULL;
    }

    inf_text_buffer_insert_text(
      buffer,
      inf_text_buffer_get_length(buffer),
      text,
      bytes,
      length,
      user
    );

    g_free(text);
    return TRUE;
  }
  else
  {
    return INF_SESSION_CLASS(inf_text_session_parent_class)->process_xml_sync(
      session,
      connection,
      xml,
      error
    );
  }
}

static InfCommunicationScope
inf_text_session_process_xml_run(InfSession* session,
                                 InfXmlConnection* connection,
                                 const xmlNodePtr xml,
                                 GError** error)
{
  if(strcmp((const char*)xml->name, "user-color-change") == 0)
  {
    return inf_text_session_handle_user_color_change(
      INF_TEXT_SESSION(session),
      connection,
      xml,
      error
    );
  }
  else
  {
    return INF_SESSION_CLASS(inf_text_session_parent_class)->process_xml_run(
      session,
      connection,
      xml,
      error
    );
  }
}

static GArray*
inf_text_session_get_xml_user_props(InfSession* session,
                                    InfXmlConnection* connection,
                                    const xmlNodePtr xml)
{
  InfSessionClass* parent_class;
  GArray* array;
  GParameter* parameter;
  guint caret;
  gint selection;
  gdouble hue;

  parent_class = INF_SESSION_CLASS(inf_text_session_parent_class);
  array = parent_class->get_xml_user_props(session, connection, xml);

  /* TODO: Error reporting for get_xml_user_props */
  if(inf_xml_util_get_attribute_uint(xml, "caret", &caret, NULL))
  {
    parameter = inf_session_get_user_property(array, "caret-position");
    g_value_init(&parameter->value, G_TYPE_UINT);
    g_value_set_uint(&parameter->value, caret);
  }

  parameter = inf_session_get_user_property(array, "selection-length");
  g_value_init(&parameter->value, G_TYPE_INT);
  if(inf_xml_util_get_attribute_int(xml, "selection", &selection, NULL))
    g_value_set_int(&parameter->value, selection);
  else
    g_value_set_int(&parameter->value, 0);

  parameter = inf_session_get_user_property(array, "hue");
  g_value_init(&parameter->value, G_TYPE_DOUBLE);

  /* Use a random hue if none set */
  if(inf_xml_util_get_attribute_double(xml, "hue", &hue, NULL))
    g_value_set_double(&parameter->value, hue);
  else
    g_value_set_double(&parameter->value, g_random_double());

  return array;
}

static void
inf_text_session_set_xml_user_props(InfSession* session,
                                    const GParameter* params,
                                    guint n_params,
                                    xmlNodePtr xml)
{
  InfSessionClass* parent_class;
  const GParameter* param;

  parent_class = INF_SESSION_CLASS(inf_text_session_parent_class);
  parent_class->set_xml_user_props(session, params, n_params, xml);

  param = inf_session_lookup_user_property(
    params,
    n_params,
    "caret-position"
  );

  if(param != NULL)
  {
    inf_xml_util_set_attribute_uint(
      xml,
      "caret",
      g_value_get_uint(&param->value)
    );
  }

  param = inf_session_lookup_user_property(
    params,
    n_params,
    "selection-length"
  );

  if(param != NULL)
  {
    inf_xml_util_set_attribute_int(
      xml,
      "selection",
      g_value_get_int(&param->value)
    );
  }

  param = inf_session_lookup_user_property(
    params,
    n_params,
    "hue"
  );

  if(param != NULL)
  {
    inf_xml_util_set_attribute_double(
      xml,
      "hue",
      g_value_get_double(&param->value)
    );
  }
}

static gboolean
inf_text_session_validate_user_props(InfSession* session,
                                     const GParameter* params,
                                     guint n_params,
                                     InfUser* exclude,
                                     GError** error)
{
  InfSessionClass* parent_class;
  const GParameter* caret;
  gboolean result;

  parent_class = INF_SESSION_CLASS(inf_text_session_parent_class);
  result = parent_class->validate_user_props(
    session,
    params,
    n_params,
    exclude,
    error
  );

  if(result == FALSE) return FALSE;

  caret = inf_session_lookup_user_property(
    params,
    n_params,
    "caret-position"
  );

  if(caret == NULL)
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_NO_SUCH_ATTRIBUTE,
      "%s",
      _("'caret' attribute in user message is missing")
    );

    return FALSE;
  }

  /* Selection is optional and 0 if not given */

  return result;
}

static InfUser*
inf_text_session_user_new(InfSession* session,
                          GParameter* params,
                          guint n_params)
{
  GObject* object;
  object = g_object_newv(INF_TEXT_TYPE_USER, n_params, params);
  return INF_USER(object);
}

static void
inf_text_session_synchronization_complete(InfSession* session,
                                          InfXmlConnection* connection)
{
  InfSessionClass* parent_class;
  InfSessionStatus status;

  parent_class = INF_SESSION_CLASS(inf_text_session_parent_class);
  status = inf_session_get_status(session);

  parent_class->synchronization_complete(session, connection);

  /* init_text_handlers needs to access the algorithm which is created in the
   * parent class default signal handler which is why we call this afterwards.
   * Note that we need to query status before, so we know whether the session
   * itself was synchronized (status == SYNCHRONIZING) or whether we just
   * synchronized the session to someone else (status == RUNNING). */
  if(status == INF_SESSION_SYNCHRONIZING)
    inf_text_session_init_text_handlers(INF_TEXT_SESSION(session));
}

/*
 * InfAdoptedSession overrides
 */

static void
inf_text_session_request_to_xml(InfAdoptedSession* session,
                                xmlNodePtr xml,
                                InfAdoptedRequest* request,
                                InfAdoptedStateVector* diff_vec,
                                gboolean for_sync)
{
  InfTextChunk* chunk;
  InfTextChunkIter iter;
  gboolean result;
  xmlNodePtr op_xml;

  gchar* utf8_text;
  gsize bytes_read;
  gsize bytes_written;

  GIConv cd;
  xmlNodePtr child;
  const gchar* text;
  gsize total_bytes;
  gsize bytes_left;

  InfAdoptedOperation* operation;

  switch(inf_adopted_request_get_request_type(request))
  {
  case INF_ADOPTED_REQUEST_DO:
    operation = inf_adopted_request_get_operation(request);
    if(INF_TEXT_IS_INSERT_OPERATION(operation))
    {
      op_xml = xmlNewNode(NULL, (const xmlChar*)"insert-caret");

      inf_xml_util_set_attribute_uint(
        op_xml,
        "pos",
        inf_text_insert_operation_get_position(
          INF_TEXT_INSERT_OPERATION(operation)
        )
      );

      /* Must be default insert operation so we get the inserted text */
      g_assert(INF_TEXT_IS_DEFAULT_INSERT_OPERATION(operation));

      chunk = inf_text_default_insert_operation_get_chunk(
        INF_TEXT_DEFAULT_INSERT_OPERATION(operation)
      );

      result = inf_text_chunk_iter_init_begin(chunk, &iter);
      g_assert(result == TRUE);

      utf8_text = g_convert(
        inf_text_chunk_iter_get_text(&iter),
        inf_text_chunk_iter_get_bytes(&iter),
        "UTF-8",
        inf_text_chunk_get_encoding(chunk),
        &bytes_read,
        &bytes_written,
        NULL
      );

      /* Conversion to UTF-8 should always succeed */
      g_assert(utf8_text != NULL);
      g_assert(bytes_read == inf_text_chunk_iter_get_bytes(&iter));

      inf_xml_util_add_child_text(op_xml, utf8_text, bytes_written);
      g_free(utf8_text);

      /* We only allow a single segment because the whole inserted text must
       * be written by a single user. */
      g_assert(inf_text_chunk_iter_next(&iter) == FALSE);
    }
    else if(INF_TEXT_IS_DELETE_OPERATION(operation))
    {
      op_xml = xmlNewNode(NULL, (const xmlChar*)"delete-caret");

      inf_xml_util_set_attribute_uint(
        op_xml,
        "pos",
        inf_text_delete_operation_get_position(
          INF_TEXT_DELETE_OPERATION(operation)
        )
      );

      if(for_sync == TRUE)
      {
        /* Must be default delete operation so we get chunk */
        g_assert(INF_TEXT_IS_DEFAULT_DELETE_OPERATION(operation));

        chunk = inf_text_default_delete_operation_get_chunk(
          INF_TEXT_DEFAULT_DELETE_OPERATION(operation)
        );

        /* Need to transmit all deleted data */
        cd = g_iconv_open("UTF-8", inf_text_chunk_get_encoding(chunk));
        result = inf_text_chunk_iter_init_begin(chunk, &iter);

        while(result == TRUE)
        {
          text = inf_text_chunk_iter_get_text(&iter);
          total_bytes = inf_text_chunk_iter_get_bytes(&iter);
          bytes_left = total_bytes;
          child = xmlNewChild(op_xml, NULL, (const xmlChar*)"segment", NULL);

          while(bytes_left > 0)
          {
            inf_text_session_segment_to_xml(
              &cd,
              child,
              text + total_bytes - bytes_left,
              &bytes_left,
              inf_text_chunk_iter_get_author(&iter)
            );
          }

          result = inf_text_chunk_iter_next(&iter);
        }

        g_iconv_close(cd);
      }
      else
      {
        /* Just transmit position and length, the other site generates a
         * InfTextRemoteDeleteOperation from that and is able to restore the
         * deleted text for potential Undo. */
        inf_xml_util_set_attribute_uint(
          op_xml,
          "len",
          inf_text_delete_operation_get_length(
            INF_TEXT_DELETE_OPERATION(operation)
          )
        );
      }
    }
    else if(for_sync == FALSE && INF_TEXT_IS_MOVE_OPERATION(operation))
    {
      op_xml = xmlNewNode(NULL, (const xmlChar*)"move");

      inf_xml_util_set_attribute_uint(
        op_xml,
        "caret",
        inf_text_move_operation_get_position(
          INF_TEXT_MOVE_OPERATION(operation)
        )
      );

      inf_xml_util_set_attribute_int(
        op_xml,
        "selection",
        inf_text_move_operation_get_length(INF_TEXT_MOVE_OPERATION(operation))
      );
    }
    else if(for_sync == FALSE && INF_ADOPTED_IS_NO_OPERATION(operation))
    {
      op_xml = xmlNewNode(NULL, (const xmlChar*)"no-op");
    }
    else
    {
      g_assert_not_reached();
    }

    break;
  case INF_ADOPTED_REQUEST_UNDO:
    op_xml = xmlNewNode(NULL, (const xmlChar*)"undo-caret");
    break;
  case INF_ADOPTED_REQUEST_REDO:
    op_xml = xmlNewNode(NULL, (const xmlChar*)"redo-caret");
    break;
  default:
    g_assert_not_reached();
    break;
  }

  g_assert(op_xml != NULL);

  inf_adopted_session_write_request_info(
    session,
    request,
    diff_vec,
    xml,
    op_xml
  );
}

static InfAdoptedRequest*
inf_text_session_xml_to_request(InfAdoptedSession* session,
                                xmlNodePtr xml,
                                InfAdoptedStateVector* diff_vec,
                                gboolean for_sync,
                                GError** error)
{
  InfTextBuffer* buffer;
  InfAdoptedUser* user;
  guint user_id;
  InfAdoptedStateVector* vector;
  xmlNodePtr op_xml;
  InfAdoptedOperation* operation;
  InfAdoptedRequestType type;
  InfAdoptedRequest* request;

  guint pos;
  gchar* text;
  gsize bytes;
  InfTextChunk* chunk;

  gchar* utf8_text;
  gsize in_bytes;
  guint length;

  xmlNodePtr child;
  GIConv cd;
  guint author;
  gboolean cmp;

  gint selection;

  buffer = INF_TEXT_BUFFER(inf_session_get_buffer(INF_SESSION(session)));

  cmp = inf_adopted_session_read_request_info(
    session,
    xml,
    diff_vec,
    &user,
    &vector,
    &op_xml,
    error
  );

  if(cmp == FALSE) return FALSE;
  user_id = (user == NULL) ? 0 : inf_user_get_id(INF_USER(user));

  if(strcmp((const char*)op_xml->name, "insert") == 0 ||
     strcmp((const char*)op_xml->name, "insert-caret") == 0)
  {
    type = INF_ADOPTED_REQUEST_DO;

    if(!inf_xml_util_get_attribute_uint_required(op_xml, "pos", &pos, error))
      goto fail;

    utf8_text = inf_xml_util_get_child_text(op_xml, &in_bytes, &length, error);
    if(!utf8_text)
      goto fail;

    text = g_convert(
      utf8_text,
      in_bytes,
      inf_text_buffer_get_encoding(buffer),
      "UTF-8",
      NULL,
      &bytes,
      error
    );

    g_free(utf8_text);
    if(text == NULL) goto fail;

    chunk = inf_text_chunk_new(inf_text_buffer_get_encoding(buffer));
    inf_text_chunk_insert_text(chunk, 0, text, bytes, length, user_id);
    g_free(text);

    operation = INF_ADOPTED_OPERATION(
      inf_text_default_insert_operation_new(pos, chunk)
    );

    inf_text_chunk_free(chunk);
  }
  else if(strcmp((const char*)op_xml->name, "delete") == 0 ||
          strcmp((const char*)op_xml->name, "delete-caret") == 0)
  {
    type = INF_ADOPTED_REQUEST_DO;

    if(!inf_xml_util_get_attribute_uint_required(op_xml, "pos", &pos, error))
      goto fail;

    if(for_sync == TRUE)
    {
      chunk = inf_text_chunk_new(inf_text_buffer_get_encoding(buffer));
      cd = g_iconv_open(inf_text_buffer_get_encoding(buffer), "UTF-8");
      g_assert(cd != (GIConv)(-1));

      for(child = op_xml->children; child != NULL; child = child->next)
      {
        if(strcmp((const char*)child->name, "segment") == 0)
        {
          text = inf_text_session_segment_from_xml(
            &cd,
            child,
            &length,
            &bytes,
            &author,
            error
          );

          if(text == NULL)
          {
            inf_text_chunk_free(chunk);
            g_iconv_close(cd);
            goto fail;
          }
          else
          {
            inf_text_chunk_insert_text(
              chunk,
              inf_text_chunk_get_length(chunk),
              text,
              bytes,
              length,
              author
            );

            g_free(text);
          }
        }
        else
        {
          /* TODO: Error */
        }
      }

      g_iconv_close(cd);

      operation = INF_ADOPTED_OPERATION(
        inf_text_default_delete_operation_new(pos, chunk)
      );

      inf_text_chunk_free(chunk);
    }
    else
    {
      cmp = inf_xml_util_get_attribute_uint_required(
        op_xml,
        "len",
        &length,
        error
      );

      if(cmp == FALSE) goto fail;

      operation = INF_ADOPTED_OPERATION(
        inf_text_remote_delete_operation_new(pos, length)
      );
    }
  }
  else if(strcmp((const char*)op_xml->name, "move") == 0)
  {
    type = INF_ADOPTED_REQUEST_DO;

    cmp = inf_xml_util_get_attribute_uint_required(
      op_xml,
      "caret",
      &pos,
      error
    );

    if(cmp == FALSE) goto fail;

    cmp = inf_xml_util_get_attribute_int_required(
      op_xml,
      "selection",
      &selection,
      error
    );

    if(cmp == FALSE) goto fail;

    operation = INF_ADOPTED_OPERATION(
      inf_text_move_operation_new(pos, selection)
    );
  }
  else if(strcmp((const char*)op_xml->name, "no-op") == 0)
  {
    type = INF_ADOPTED_REQUEST_DO;
    operation = INF_ADOPTED_OPERATION(inf_adopted_no_operation_new());
  }
  else if(strcmp((const char*)op_xml->name, "undo") == 0 ||
          strcmp((const char*)op_xml->name, "undo-caret") == 0)
  {
    type = INF_ADOPTED_REQUEST_UNDO;
  }
  else if(strcmp((const char*)op_xml->name, "redo") == 0 ||
          strcmp((const char*)op_xml->name, "redo-caret") == 0)
  {
    type = INF_ADOPTED_REQUEST_REDO;
  }
  else
  {
    /* TODO: Error */
    goto fail;
  }

  switch(type)
  {
  case INF_ADOPTED_REQUEST_DO:
    g_assert(operation != NULL);
    request = inf_adopted_request_new_do(
      vector,
      user_id,
      operation,
      inf_text_session_get_real_time()
    );
    g_object_unref(operation);
    break;
  case INF_ADOPTED_REQUEST_UNDO:
    request = inf_adopted_request_new_undo(
      vector,
      user_id,
      inf_text_session_get_real_time()
    );
    break;
  case INF_ADOPTED_REQUEST_REDO:
    request = inf_adopted_request_new_redo(
      vector,
      user_id,
      inf_text_session_get_real_time()
    );
    break;
  default:
    g_assert_not_reached();
    break;
  }

  inf_adopted_state_vector_free(vector);
  return request;

fail:
  inf_adopted_state_vector_free(vector);
  return NULL;
}

/*
 * Gype registration.
 */

static void
inf_text_session_class_init(InfTextSessionClass* text_session_class)
{
  GObjectClass* object_class;
  InfSessionClass* session_class;
  InfAdoptedSessionClass* adopted_session_class;

  object_class = G_OBJECT_CLASS(text_session_class);
  session_class = INF_SESSION_CLASS(text_session_class);
  adopted_session_class = INF_ADOPTED_SESSION_CLASS(text_session_class);

  object_class->constructor = inf_text_session_constructor;
  object_class->dispose = inf_text_session_dispose;
  object_class->finalize = inf_text_session_finalize;
  object_class->set_property = inf_text_session_set_property;
  object_class->get_property = inf_text_session_get_property;

  session_class->to_xml_sync = inf_text_session_to_xml_sync;
  session_class->process_xml_sync = inf_text_session_process_xml_sync;
  session_class->process_xml_run = inf_text_session_process_xml_run;
  session_class->get_xml_user_props = inf_text_session_get_xml_user_props;
  session_class->set_xml_user_props = inf_text_session_set_xml_user_props;
  session_class->validate_user_props = inf_text_session_validate_user_props;
  session_class->user_new = inf_text_session_user_new;
  session_class->synchronization_complete =
    inf_text_session_synchronization_complete;

  adopted_session_class->xml_to_request = inf_text_session_xml_to_request;
  adopted_session_class->request_to_xml = inf_text_session_request_to_xml;

  inf_text_session_error_quark = g_quark_from_static_string(
    "INF_TEXT_SESSION_ERROR"
  );

  g_object_class_install_property(
    object_class,
    PROP_CARET_UPDATE_INTERVAL,
    g_param_spec_uint(
      "caret-update-interval",
      "Caret update interval",
      "Minimum number of milliseconds between caret update broadcasts",
      0,
      G_MAXUINT,
      500,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT
    )
  );
}

/*
 * Public API.
 */

/**
 * inf_text_session_new: (constructor)
 * @manager: A #InfCommunicationManager.
 * @buffer: An initial #InfTextBuffer.
 * @io: A #InfIo object.
 * @status: The initial status of the session.
 * @sync_group: (allow-none): A group in which the session is synchronized.
 * Ignored if @status is %INF_SESSION_RUNNING.
 * @sync_connection: (allow-none): A connection to synchronize the session
 * from. Ignored if @status is %INF_SESSION_RUNNING.
 *
 * Creates a new #InfTextSession. The communication manager is used to send
 * and receive requests from subscription and synchronization. @buffer will be
 * set to be initially empty if the session is initially synchronized
 * (see below). @io is required to trigger timeouts.
 *
 * If @status is %INF_SESSION_PRESYNC or %INF_SESSION_SYNCHRONIZING, then the
 * session will initially be sychronized, meaning the initial content is
 * retrieved from @sync_connection. If you are subscribed to the session, set
 * the subscription group via inf_session_set_subscription_group().
 *
 * Returns: (transfer full): A new #InfTextSession.
 **/
InfTextSession*
inf_text_session_new(InfCommunicationManager* manager,
                     InfTextBuffer* buffer,
                     InfIo* io,
                     InfSessionStatus status,
                     InfCommunicationGroup* sync_group,
                     InfXmlConnection* sync_connection)
{
  GObject* object;

  g_return_val_if_fail(INF_COMMUNICATION_IS_MANAGER(manager), NULL);
  g_return_val_if_fail(INF_TEXT_IS_BUFFER(buffer), NULL);
  g_return_val_if_fail(INF_IS_IO(io), NULL);

  g_return_val_if_fail(
    (status == INF_SESSION_RUNNING &&
     sync_group == NULL && sync_connection == NULL) ||
    (status != INF_SESSION_RUNNING &&
     INF_COMMUNICATION_IS_GROUP(sync_group) &&
     INF_IS_XML_CONNECTION(sync_connection)),
    NULL
  );

  object = g_object_new(
    INF_TEXT_TYPE_SESSION,
    "communication-manager", manager,
    "buffer", buffer,
    "status", status,
    "sync-group", sync_group,
    "sync-connection", sync_connection,
    "io", io,
    NULL
  );

  return INF_TEXT_SESSION(object);
}

/**
 * inf_text_session_new_with_user_table: (constructor)
 * @manager: A #InfCommunicationManager.
 * @buffer: An initial #InfTextBuffer.
 * @io: A #InfIo object.
 * @user_table: A #InfUserTable.
 * @status: The initial status for the session.
 * @sync_group: (allow-none): A group in which the session is synchronized.
 * Ignored if @status is %INF_SESSION_RUNNING.
 * @sync_connection: (allow-none): A connection to synchronize the session
 * from. Ignored if @status is %INF_SESSION_RUNNING.
 *
 * Creates a new #InfTextSession. The connection manager is used to send and
 * receive requests from subscription and synchronization. @buffer will be
 * set to be initially empty if the session is initially synchronized
 * (see below). @io is required to trigger timeouts.
 *
 * If @status is %INF_SESSION_PRESYNC or %INF_SESSION_SYNCHRONIZING, then the
 * session will initially be sychronized, meaning the initial content is
 * retrieved from @sync_connection. If you are subscribed to the session, set
 * the subscription group via inf_session_set_subscription_group().
 *
 * @user_table is used as an initial user table. The user table should only
 * contain unavailable users, if any, that may rejoin during the session. If
 * there was an available user in the user table, it would probably belong
 * to another session, but different sessions cannot share the same user
 * object.
 *
 * Returns: (transfer full): A new #InfTextSession.
 **/
InfTextSession*
inf_text_session_new_with_user_table(InfCommunicationManager* manager,
                                     InfTextBuffer* buffer,
                                     InfIo* io,
                                     InfUserTable* user_table,
                                     InfSessionStatus status,
                                     InfCommunicationGroup* sync_group,
                                     InfXmlConnection* sync_connection)
{
  /* TODO: Can it happen that the user_table is set explicitely PLUS the
   * session is synchronized? If not then this function can be simplified */

  GObject* object;

  g_return_val_if_fail(INF_COMMUNICATION_IS_MANAGER(manager), NULL);
  g_return_val_if_fail(INF_TEXT_IS_BUFFER(buffer), NULL);
  g_return_val_if_fail(INF_IS_IO(io), NULL);
  g_return_val_if_fail(INF_IS_USER_TABLE(user_table), NULL);

  g_return_val_if_fail(
    (status == INF_SESSION_RUNNING &&
     sync_group == NULL && sync_connection == NULL) ||
    (status != INF_SESSION_RUNNING &&
     INF_COMMUNICATION_IS_GROUP(sync_group) &&
     INF_IS_XML_CONNECTION(sync_connection)),
    NULL
  );

  object = g_object_new(
    INF_TEXT_TYPE_SESSION,
    "communication-manager", manager,
    "buffer", buffer,
    "user-table", user_table,
    "status", status,
    "sync-group", sync_group,
    "sync-connection", sync_connection,
    "io", io,
    NULL
  );

  return INF_TEXT_SESSION(object);
}

/**
 * inf_text_session_set_user_color:
 * @session: A #InfTextSession.
 * @user: A local #InfTextUser from @session's user table.
 * @hue: New hue value for @user's color. Ranges from 0.0 (red) to 1.0 (red).
 *
 * Changes the user color of @user. @user must have the %INF_USER_LOCAL flag
 * set.
 */
void
inf_text_session_set_user_color(InfTextSession* session,
                                InfTextUser* user,
                                gdouble hue)
{
  xmlNodePtr xml;

  g_return_if_fail(INF_TEXT_IS_SESSION(session));
  g_return_if_fail(INF_TEXT_IS_USER(user));
  g_return_if_fail(hue >= 0.0 && hue <= 1.0);

  g_return_if_fail(
    inf_user_get_status(INF_USER(user)) != INF_USER_UNAVAILABLE
  );
  g_return_if_fail(
    (inf_user_get_flags(INF_USER(user)) & INF_USER_LOCAL) != 0
  );

  xml = xmlNewNode(NULL, (const xmlChar*)"user-color-change");
  inf_xml_util_set_attribute_uint(xml, "id", inf_user_get_id(INF_USER(user)));
  inf_xml_util_set_attribute_double(xml, "hue", hue);

  inf_session_send_to_subscriptions(INF_SESSION(session), xml);
  g_object_set(G_OBJECT(user), "hue", hue, NULL);
}

/**
 * inf_text_session_flush_requests_for_user:
 * @session: A #InfTextSession.
 * @user: The #InfTextUser for which to flush messages.
 *
 * This function sends all pending requests for @user immediately. Requests
 * that modify the buffer are not queued normally, but cursor movement
 * requests are delayed in case are issued frequently, to save bandwidth.
 *
 * The main purpose of this function is to send all pending requests before
 * changing a user's status to inactive or unavailable since inactive users
 * are automatically activated as soon as they issue a request.
 *
 * TODO: We should probably detect this automatically, without requiring
 * people to call this function, i.e. flush requests for local users just
 * before they become inactive.
 *
 * @user must have the %INF_USER_LOCAL flag set.
 */
void
inf_text_session_flush_requests_for_user(InfTextSession* session,
                                         InfTextUser* user)
{
  InfTextSessionLocalUser* local;

  g_return_if_fail(INF_TEXT_IS_SESSION(session));
  g_return_if_fail(INF_TEXT_IS_USER(user));

  local = inf_text_session_find_local_user(session, user);
  g_assert(local != NULL);

  if(local->caret_timeout != NULL)
  {
    inf_text_session_broadcast_caret_selection(session, local);
  }
}

/**
 * inf_text_session_join_user:
 * @proxy: A #InfSessionProxy with a #InfTextSession session.
 * @name: The name of the user to join.
 * @status: The initial status of the user to join. Must not be
 * @INF_USER_UNAVAILABLE.
 * @hue: The user color of the user to join.
 * @caret_position: The initial position of the new user's cursor.
 * @selection_length: The initial length of the new user's selection.
 * @func: (scope async): Function to call after completion of the request,
 * or %NULL.
 * @user_data: Additional data to pass to @func.
 *
 * This functions creates a user join request for an #InfTextSession. This is
 * a shortcut for inf_session_proxy_join_user().
 *
 * Returns: (transfer full): A #InfRequest, or %NULL.
 */
InfRequest*
inf_text_session_join_user(InfSessionProxy* proxy,
                           const gchar* name,
                           InfUserStatus status,
                           gdouble hue,
                           guint caret_position,
                           int selection_length,
                           InfRequestFunc func,
                           gpointer user_data)
{
#define N_PARAMS 6u
  GParameter params[N_PARAMS] = {
    { "hue", { 0 } },
    { "vector", { 0 } },
    { "caret-position", { 0 } },
    { "selection-length", { 0 } },
    { "name", { 0 } },
    { "status", { 0 } }
  };

  InfSession* session;
  InfRequest* request;
  guint i;

  g_return_val_if_fail(INF_IS_SESSION_PROXY(proxy), NULL);
  
  g_object_get(G_OBJECT(proxy), "session", &session, NULL);
  g_return_val_if_fail(INF_TEXT_IS_SESSION(session), NULL);

  g_value_init(&params[0].value, G_TYPE_DOUBLE);
  g_value_set_double(&params[0].value, hue);

  g_value_init(&params[1].value, INF_ADOPTED_TYPE_STATE_VECTOR);
  g_value_set_boxed(
    &params[1].value,
    inf_adopted_algorithm_get_current(
      inf_adopted_session_get_algorithm(INF_ADOPTED_SESSION(session))
    )
  );

  g_value_init(&params[2].value, G_TYPE_UINT);
  g_value_set_uint(&params[2].value, caret_position);

  g_value_init(&params[3].value, G_TYPE_INT);
  g_value_set_int(&params[3].value, selection_length);

  g_value_init(&params[4].value, G_TYPE_STRING);
  g_value_set_string(&params[4].value, name); /* TODO: set_static_string? */

  g_value_init(&params[5].value, INF_TYPE_USER_STATUS);
  g_value_set_enum(&params[5].value, status);

  request = inf_session_proxy_join_user(
    proxy,
    N_PARAMS,
    params,
    func,
    user_data
  );

  for(i = 0; i < N_PARAMS; ++i)
    g_value_unset(&params[i].value);
#undef N_PARAMS

  g_object_unref(session);
  return request;
}

/* vim:set et sw=2 ts=2: */
