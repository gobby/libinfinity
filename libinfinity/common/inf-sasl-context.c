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

/**
 * SECTION:inf-sasl-context
 * @title: InfSaslContext
 * @short_description: Asynchronous SASL processing
 * @include: libinfinity/common/inf-sasl-context.h
 * @stability: Unstable
 *
 * #InfSaslContext is a wrapper for the Gsasl library. It basically adds
 * functionality to provide properties asynchronously to Gsasl, so that for
 * example a dialog can be shown to the user before continuing with the
 * authentication process. With Gsasl, it is expected that the callback
 * function sets the requested property before returning, which makes it hard
 * to give control back to a main loop while waiting for user input.
 *
 * This wrapper makes sure the callback is called in another thread so that it
 * can block without affecting the rest of the program.
 * Use inf_sasl_context_session_feed() as a replacement for gsasl_step64().
 * Instead of returning the result data directly, the function calls a
 * callback once all properties requested have been provided.
 *
 * All threading internals are hidden by the wrapper, so all callbacks are
 * issued in the user thread. However, it requires an #InfIo object to
 * dispatch messages to it. Also, all #InfSaslContext functions are fully
 * thread-safe.
 **/

#include <libinfinity/common/inf-sasl-context.h>
#include <libinfinity/common/inf-standalone-io.h>
#include <libinfinity/common/inf-error.h>

#include <string.h>

typedef enum _InfSaslContextMessageType {
  /* main -> session */
  INF_SASL_CONTEXT_MESSAGE_TERMINATE,
  INF_SASL_CONTEXT_MESSAGE_CONTINUE,
  INF_SASL_CONTEXT_MESSAGE_STEP,

  /* session -> main */
  INF_SASL_CONTEXT_MESSAGE_QUERY, /* invoke callback to query a property */
  INF_SASL_CONTEXT_MESSAGE_STEPPED /* step finished */
} InfSaslContextMessageType;

typedef enum _InfSaslContextSessionStatus {
  INF_SASL_CONTEXT_SESSION_OUTER,
  INF_SASL_CONTEXT_SESSION_INNER,
  INF_SASL_CONTEXT_SESSION_TERMINATE,
} InfSaslContextSessionStatus;

struct _InfSaslContextSession {
  InfSaslContext* context;
  Gsasl_session* session;
  gpointer session_data;
  InfIo* main_io;
  GAsyncQueue* session_queue;
  /* query or stepped dispatch, protected by context mutex */
  /* TODO: This is required because Inf(StandaloneIo) can not guarantee to
   * return before the dispatch is executed in another thread. We would not
   * need the mutex for this if InfIo would allow to set the
   * InfIoDispatch pointer before executing the dispatch. */
  InfIoDispatch* dispatch;
  GThread* thread;
  /* This flag tells whether we are currently processing user data in the
   * helper thread. It is meant as a simple indicator in the main thread
   * whether more data can be given to the context or not. */
  gboolean stepping;

  /* used in session thread only */
  gchar* step64;
  InfSaslContextSessionFeedFunc feed_func;
  gpointer feed_user_data;
  int retval;

  InfSaslContextSessionStatus status;
};

typedef struct _InfSaslContextMessage InfSaslContextMessage;
struct _InfSaslContextMessage {
  InfSaslContextSession* session;
  InfSaslContextMessageType type;

  union {
    struct {
      int retval;
    } cont;

    struct {
      gchar* data;
      InfSaslContextSessionFeedFunc func;
      gpointer user_data;
    } step;

    struct {
      Gsasl_property prop;
    } query;

    struct {
      gchar *data;
      int retval;
      InfSaslContextSessionFeedFunc func;
      gpointer user_data;
    } stepped;
  } shared;
};

struct _InfSaslContext {
  Gsasl* gsasl;

  gint ref_count;

  GSList* sessions;

  InfSaslContextCallbackFunc callback;
  gpointer callback_user_data;

  /* protects the session list, the callback function and access to the
   * Gsasl object. */
  GMutex* mutex;
};

/*
 * Message handling
 */

static InfSaslContextMessage*
inf_sasl_context_message_terminate(InfSaslContextSession* session)
{
  InfSaslContextMessage* message;
  message = g_slice_new(InfSaslContextMessage);
  message->type = INF_SASL_CONTEXT_MESSAGE_TERMINATE;
  message->session = session;
  return message;
}

static InfSaslContextMessage*
inf_sasl_context_message_continue(InfSaslContextSession* session,
                                  int retval)
{
  InfSaslContextMessage* message;
  message = g_slice_new(InfSaslContextMessage);
  message->type = INF_SASL_CONTEXT_MESSAGE_CONTINUE;
  message->session = session;
  message->shared.cont.retval = retval;
  return message;
}

static InfSaslContextMessage*
inf_sasl_context_message_step(InfSaslContextSession* session,
                              const char* data,
                              InfSaslContextSessionFeedFunc func,
                              gpointer user_data)
{
  InfSaslContextMessage* message;
  message = g_slice_new(InfSaslContextMessage);
  message->type = INF_SASL_CONTEXT_MESSAGE_STEP;
  message->session = session;
  message->shared.step.data = data ? g_strdup(data) : NULL;
  message->shared.step.func = func;
  message->shared.step.user_data = user_data;
  return message;
}

static InfSaslContextMessage*
inf_sasl_context_message_query(InfSaslContextSession* session,
                               Gsasl_property prop)
{
  InfSaslContextMessage* message;
  message = g_slice_new(InfSaslContextMessage);
  message->type = INF_SASL_CONTEXT_MESSAGE_QUERY;
  message->session = session;
  message->shared.query.prop = prop;
  return message;
}

static InfSaslContextMessage*
inf_sasl_context_message_stepped(InfSaslContextSession* session,
                                 char* data,
                                 int retval,
                                 InfSaslContextSessionFeedFunc func,
                                 gpointer user_data)
{
  InfSaslContextMessage* message;
  message = g_slice_new(InfSaslContextMessage);
  message->type = INF_SASL_CONTEXT_MESSAGE_STEPPED;
  message->session = session;
  message->shared.stepped.data = data;
  message->shared.stepped.retval = retval;
  message->shared.stepped.func = func;
  message->shared.stepped.user_data = user_data;
  return message;
}

static void
inf_sasl_context_message_free(gpointer message_)
{
  InfSaslContextMessage* message;
  message = (InfSaslContextMessage*)message_;

  switch(message->type)
  {
  case INF_SASL_CONTEXT_MESSAGE_TERMINATE:
  case INF_SASL_CONTEXT_MESSAGE_CONTINUE:
    /* nothing to do */
    break;
  case INF_SASL_CONTEXT_MESSAGE_STEP:
    g_free(message->shared.step.data);
    break;
  case INF_SASL_CONTEXT_MESSAGE_QUERY:
    /* nothing to do */
    break;
  case INF_SASL_CONTEXT_MESSAGE_STEPPED:
    if(message->shared.stepped.data != NULL)
      gsasl_free(message->shared.stepped.data);
    break;
  default:
    g_assert_not_reached();
    break;
  }

  g_slice_free(InfSaslContextMessage, message);
}

/*
 * Session thread and gsasl callback
 */

static void
inf_sasl_context_session_message_func(gpointer user_data)
{
  InfSaslContextMessage* message;
  InfSaslContextCallbackFunc func;
  gpointer func_user_data;
  gboolean needs_more;
  GError* error;

  message = (InfSaslContextMessage*)user_data;

  switch(message->type)
  {
  case INF_SASL_CONTEXT_MESSAGE_TERMINATE:
    /* session thread */
    message->session->status = INF_SASL_CONTEXT_SESSION_TERMINATE;
    break;
  case INF_SASL_CONTEXT_MESSAGE_CONTINUE:
    /* session thread */
    g_assert(message->session->status == INF_SASL_CONTEXT_SESSION_INNER);
    message->session->retval = message->shared.cont.retval;
    break;
  case INF_SASL_CONTEXT_MESSAGE_STEP:
    /* session thread */
    g_assert(message->session->status == INF_SASL_CONTEXT_SESSION_OUTER);
    g_assert(message->shared.step.func != NULL);

    message->session->step64 = message->shared.step.data;
    message->session->feed_func = message->shared.step.func;
    message->session->feed_user_data = message->shared.step.user_data;
    message->shared.step.data = NULL; /* prevent deletion */

    message->session->status = INF_SASL_CONTEXT_SESSION_INNER;
    break;
  case INF_SASL_CONTEXT_MESSAGE_QUERY:
    /* main thread */
    g_mutex_lock(message->session->context->mutex);
    g_assert(message->session->dispatch != NULL);
    message->session->dispatch = NULL;

    func = message->session->context->callback;
    func_user_data = message->session->context->callback_user_data;
    g_mutex_unlock(message->session->context->mutex);

    func(
      message->session,
      message->shared.query.prop,
      message->session->session_data,
      func_user_data
    );

    break;
  case INF_SASL_CONTEXT_MESSAGE_STEPPED:
    /* main thread */
    g_mutex_lock(message->session->context->mutex);
    g_assert(message->session->dispatch != NULL);
    message->session->dispatch = NULL;
    g_mutex_unlock(message->session->context->mutex);

    switch(message->shared.stepped.retval)
    {
    case GSASL_OK:
      error = NULL;
      needs_more = FALSE;
      break;
    case GSASL_NEEDS_MORE:
      error = NULL;
      needs_more = TRUE;
      break;
    default:
      inf_gsasl_set_error(&error, message->shared.stepped.retval);
      needs_more = FALSE;
      break;
    }

    g_assert(message->session->stepping == TRUE);
    message->session->stepping = FALSE;

    message->shared.stepped.func(
      message->session,
      message->shared.stepped.data,
      needs_more,
      error,
      message->shared.stepped.user_data
    );

    if(error != NULL) g_error_free(error);

    break;
  default:
    g_assert_not_reached();
    break;
  }
}

static int
inf_sasl_context_gsasl_callback(Gsasl* gsasl,
                                Gsasl_session* gsasl_session,
                                Gsasl_property prop)
{
  InfSaslContextSession* session;
  InfSaslContextMessage* message;

  session = (InfSaslContextSession*)gsasl_session_hook_get(gsasl_session);

  /* if the status is TERMINATE then get out of the inner loop by
   * returning from all SASL callbacks immediately. */
  if(session->status == INF_SASL_CONTEXT_SESSION_TERMINATE)
    return GSASL_NO_CALLBACK;

  g_assert(session->status == INF_SASL_CONTEXT_SESSION_INNER);
  
  /* query the property from the main thread */
  g_assert(session->dispatch == NULL);
  session->dispatch = inf_io_add_dispatch(
    INF_IO(session->main_io),
    inf_sasl_context_session_message_func,
    inf_sasl_context_message_query(session, prop),
    inf_sasl_context_message_free
  );

  g_mutex_unlock(session->context->mutex);

  session->retval = G_MAXINT;
  while(session->status == INF_SASL_CONTEXT_SESSION_INNER &&
        session->retval == G_MAXINT)
  {
    message = g_async_queue_pop(session->session_queue);
    inf_sasl_context_session_message_func(message);
    inf_sasl_context_message_free(message);
  }

  g_mutex_lock(session->context->mutex);

  /* return on terminate */
  if(session->status == INF_SASL_CONTEXT_SESSION_TERMINATE)
    return GSASL_NO_CALLBACK;

  g_assert(session->dispatch == NULL);
  return session->retval;
}

static void*
inf_sasl_context_thread_func(gpointer data)
{
  InfSaslContextSession* session;
  InfSaslContextMessage* message;

  session = (InfSaslContextSession*)data;

  int retval;
  char* output;
  InfSaslContextSessionFeedFunc feed_func;
  gpointer feed_user_data;

  /* Wait for something to do */
  while(session->status != INF_SASL_CONTEXT_SESSION_TERMINATE)
  {
    switch(session->status)
    {
    case INF_SASL_CONTEXT_SESSION_OUTER:
      message = g_async_queue_pop(session->session_queue);
      inf_sasl_context_session_message_func(message);
      inf_sasl_context_message_free(message);
      break;
    case INF_SASL_CONTEXT_SESSION_INNER:
      g_mutex_lock(session->context->mutex);

      g_assert(session->dispatch == NULL);

      /* This might call the gsasl callback once or more in which we wait
       * for input from the main thread. */
      retval = gsasl_step64(
        session->session,
        session->step64,
        &output
      );

      g_mutex_unlock(session->context->mutex);

      g_free(session->step64);
      session->step64 = NULL;

      if(retval != GSASL_OK && retval != GSASL_NEEDS_MORE)
        output = NULL;

      /* Only process the result when we were not requested to terminate
       * within the gsasl callback. */
      if(session->status != INF_SASL_CONTEXT_SESSION_TERMINATE)
      {
        feed_func = session->feed_func;
        feed_user_data = session->feed_user_data;
        session->feed_func = NULL; /* clear, so that feed can be called again */

        session->status = INF_SASL_CONTEXT_SESSION_OUTER;

        g_mutex_lock(session->context->mutex);

        g_assert(session->dispatch == NULL);

        session->dispatch = inf_io_add_dispatch(
          INF_IO(session->main_io),
          inf_sasl_context_session_message_func,
          inf_sasl_context_message_stepped(
            session,
            output,
            retval,
            feed_func,
            feed_user_data
          ),
          inf_sasl_context_message_free
        );

        g_mutex_unlock(session->context->mutex);
      }
      else
      {
        session->feed_func = NULL;
        if(output) gsasl_free(output);
      }

      break;
    case INF_SASL_CONTEXT_SESSION_TERMINATE:
    default:
      g_assert_not_reached();
      break;
    }
  }

  return NULL;
}

/*
 * Helper functions
 */

static InfSaslContextSession*
inf_sasl_context_start_session(InfSaslContext* context,
                               InfIo* io,
                               Gsasl_session* gsasl_session,
                               gpointer session_data,
                               GError** error)
{
  InfSaslContextSession* session;
  session = g_slice_new(InfSaslContextSession);

  session->context = context;
  session->session = gsasl_session;
  session->session_data = session_data;
  session->main_io = io;
  g_object_ref(session->main_io);
  session->session_queue =
    g_async_queue_new_full(inf_sasl_context_message_free);
  session->dispatch = NULL;
  session->thread = NULL;
  session->stepping = FALSE;

  session->status = INF_SASL_CONTEXT_SESSION_OUTER;
  session->step64 = NULL;
  session->retval = GSASL_OK;
  session->feed_func = NULL;
  /*session->feed_user_data = NULL;*/

  context->sessions = g_slist_prepend(context->sessions, session);
  gsasl_session_hook_set(gsasl_session, session);

  session->thread = g_thread_create(
    inf_sasl_context_thread_func,
    session,
    TRUE,
    error
  );

  if(session->thread == NULL)
  {
    context->sessions = g_slist_remove(context->sessions, session);
    g_async_queue_unref(session->session_queue);
    g_object_unref(session->main_io);
    g_slice_free(InfSaslContextSession, session);
    return NULL;
  }

  return session;
}

/*
 * Public API
 */

GType
inf_sasl_context_get_type(void)
{
  static GType sasl_context_type = 0;

  if(!sasl_context_type)
  {
    sasl_context_type = g_boxed_type_register_static(
      "InfSaslContext",
      (GBoxedCopyFunc)inf_sasl_context_ref,
      (GBoxedFreeFunc)inf_sasl_context_unref
    );
  }

  return sasl_context_type;
}

/**
 * inf_sasl_context_new:
 * @error: Location to store error information, if any.
 *
 * Creates a new #InfSaslContext with a reference count of 1. If the function
 * fails it returns %NULL and @error is set.
 *
 * Returns: A new #InfSaslContext, or %NULL on error. Free with
 * inf_sasl_context_unref() when no longer needed.
 */
InfSaslContext*
inf_sasl_context_new(GError** error)
{
  Gsasl* gsasl;
  int status;

  InfSaslContext* sasl;

  g_return_val_if_fail(error == NULL || *error == NULL, NULL);

  status = gsasl_init(&gsasl);
  if(status != GSASL_OK)
  {
    inf_gsasl_set_error(error, status);
    return NULL;
  }

  sasl = g_slice_new(InfSaslContext);
  sasl->gsasl = gsasl;
  sasl->ref_count = 1;
  sasl->sessions = NULL;

  sasl->callback = NULL;
  sasl->callback_user_data = NULL;

  gsasl_callback_set(gsasl, inf_sasl_context_gsasl_callback);
  gsasl_callback_hook_set(gsasl, sasl);
  
  sasl->mutex = g_mutex_new();

  return sasl;
}

/**
 * inf_sasl_context_ref:
 * @context: A #InfSaslContext.
 *
 * Increases the reference count of @context by one.
 *
 * Returns: The passed in pointer, @context.
 */
InfSaslContext*
inf_sasl_context_ref(InfSaslContext* context)
{
  g_return_val_if_fail(context != NULL, NULL);

  g_atomic_int_inc(&context->ref_count);
  return context;
}

/**
 * inf_sasl_context_unref:
 * @context: A #InfSaslContext.
 *
 * Decreases the reference count of @sasl by one. When the reference count
 * reaches zero then the object is freed and may no longer be used. If that
 * happens then also all sessions created with
 * inf_sasl_context_client_start_session() or
 * inf_sasl_context_server_start_session() are stopped automatically.
 */
void
inf_sasl_context_unref(InfSaslContext* context)
{
  g_return_if_fail(context != NULL);

  if(g_atomic_int_dec_and_test(&context->ref_count))
  {
    /* Note that we don't need to lock the mutex here since if nobody has a
     * reference anymore then they cannot access the session list concurrently
     * anyway. Also, the session threads do not access the list at all. */
    while(context->sessions != NULL)
    {
      inf_sasl_context_stop_session(
        context,
        (InfSaslContextSession*)context->sessions->data
      );
    }

    /* Again we don't need to lock the mutex for this since all session
     * threads have been stopped at this point. */
    gsasl_done(context->gsasl);
    g_mutex_free(context->mutex);

    g_slice_free(InfSaslContext, context);
  }
}

/**
 * inf_sasl_context_set_callback:
 * @context: A #InfSaslContext.
 * @callback: A function to call to query properties for authentication.
 * @user_data: Additional context to pass to @callback.
 *
 * Sets the callback to call when, during authentication, a certain properties
 * needs to be provided, such as a username or a password. The callback
 * function is expected to set the requested property using
 * inf_sasl_context_session_set_property() and then call
 * inf_sasl_context_session_continue() with retval being GSASL_OK. If it
 * cannot provide the property then it should only call
 * inf_sasl_context_session_continue() with retval indicating the problem.
 *
 * The callback function does not need to provide the property immediately.
 * It is also allowed return and call inf_sasl_context_session_continue()
 * later.
 */
void
inf_sasl_context_set_callback(InfSaslContext* context,
                              InfSaslContextCallbackFunc callback,
                              gpointer user_data)
{
  g_return_if_fail(context != NULL);

  g_mutex_lock(context->mutex);
  context->callback = callback;
  context->callback_user_data = user_data;
  g_mutex_unlock(context->mutex);
}

/**
 * inf_sasl_context_client_start_session:
 * @context: A #InfSaslContext.
 * @io: The #InfIo main loop to which to dispatch callbacks.
 * @mech: The mechanism to use for the session.
 * @session_data: Session-specific data to provide to the
 * #InfSaslContextCallbackFunc.
 * @error: Location to store error information, if any.
 *
 * Starts a new client-side SASL session using @mech for authentication. When
 * the session finished, that is either when an error occurred or the
 * authentication finished successfully, use inf_sasl_context_stop_session().
 *
 * The callback function will be called in the thread that @io runs in.
 *
 * Returns: A #InfSaslContextSession.
 */
InfSaslContextSession*
inf_sasl_context_client_start_session(InfSaslContext* context,
                                      InfIo* io,
                                      const char* mech,
                                      gpointer session_data,
                                      GError** error)
{
  Gsasl_session* gsasl_session;
  int status;
  InfSaslContextSession* session;

  g_return_val_if_fail(context != NULL, NULL);
  g_return_val_if_fail(INF_IS_IO(io), NULL);
  g_return_val_if_fail(mech != NULL, NULL);
  g_return_val_if_fail(error == NULL || *error == NULL, NULL);

  g_mutex_lock(context->mutex);

  status = gsasl_client_start(context->gsasl, mech, &gsasl_session);
  if(status != GSASL_OK)
  {
    g_mutex_unlock(context->mutex);
    inf_gsasl_set_error(error, status);
    return NULL;
  }

  session = inf_sasl_context_start_session(
    context,
    io,
    gsasl_session,
    session_data,
    error
  );

  if(session == NULL)
  {
    gsasl_finish(gsasl_session);
    g_mutex_unlock(context->mutex);
    return NULL;
  }

  g_mutex_unlock(context->mutex);
  return session;
}

/**
 * inf_sasl_context_client_list_mechanisms:
 * @context: A #InfSaslContext.
 * @error: Location to store error information, if any.
 *
 * Returns a newly allocated space-separated string containing SASL mechanisms
 * that @context supports for client sessions.
 *
 * Returns: A newly allocated string. Free with gsasl_free() when no longer
 * in use.
 */
char*
inf_sasl_context_client_list_mechanisms(InfSaslContext* context,
                                        GError** error)
{
  int ret;
  char* out;

  g_return_val_if_fail(context != NULL, NULL);
  g_return_val_if_fail(error == NULL || *error == NULL, NULL);

  g_mutex_lock(context->mutex);
  ret = gsasl_client_mechlist(context->gsasl, &out);
  g_mutex_unlock(context->mutex);

  if(ret != GSASL_OK)
  {
    inf_gsasl_set_error(error, ret);
    if(out) gsasl_free(out);
    return NULL;
  }

  return out;
}

/**
 * inf_sasl_context_client_supports_mechanism:
 * @context: A #InfSaslContext.
 * @mech: The name of the mechanism to be tested.
 *
 * Checks whether @context supports the mechanism with name @mech for
 * client sessions.
 *
 * Returns: %TRUE if @mech is supported or %FALSE otherwise.
 */
gboolean
inf_sasl_context_client_supports_mechanism(InfSaslContext* context,
                                           const char* mech)
{
  int result;

  g_return_val_if_fail(context != NULL, FALSE);
  g_return_val_if_fail(mech != NULL, FALSE);

  g_mutex_lock(context->mutex);
  result = gsasl_client_support_p(context->gsasl, mech);
  g_mutex_unlock(context->mutex);

  return result != 0;
}

/**
 * inf_sasl_context_client_suggest_mechanism:
 * @context: A #InfSaslContext.
 * @mechanisms: Space-separated list of SASL mechanism names.
 *
 * Given a list of SASL mechanisms this function suggests the which is the
 * "best" one to be used.
 *
 * Returns: The name of the suggested mechanism.
 */
const char*
inf_sasl_context_client_suggest_mechanism(InfSaslContext* context,
                                          const char* mechanisms)
{
  const char* suggestion;
  g_return_val_if_fail(context != NULL, NULL);
  g_return_val_if_fail(mechanisms != NULL, NULL);

  g_mutex_lock(context->mutex);
  suggestion = gsasl_client_suggest_mechanism(context->gsasl, mechanisms);
  g_mutex_unlock(context->mutex);

  return suggestion;
}

/**
 * inf_sasl_context_server_start_session:
 * @context: A #InfSaslContext.
 * @io: The #InfIo main loop to which to dispatch callbacks.
 * @mech: The mechanism to use for the session.
 * @session_data: Session-specific data to provide to the
 * #InfSaslContextCallbackFunc.
 * @error: Location to store error information, if any.
 *
 * Starts a new server-side SASL session using @mech for authentication. When
 * the session finished, that is either when an error occurred or the
 * authentication finished successfully, use inf_sasl_context_stop_session().
 *
 * The callback function will be called in the thread that @io runs in.
 *
 * Returns: A #InfSaslContextSession.
 */
InfSaslContextSession*
inf_sasl_context_server_start_session(InfSaslContext* context,
                                      InfIo* io,
                                      const char* mech,
                                      gpointer session_data,
                                      GError** error)
{
  Gsasl_session* gsasl_session;
  int status;
  InfSaslContextSession* session;

  g_return_val_if_fail(context != NULL, NULL);
  g_return_val_if_fail(INF_IS_IO(io), NULL);
  g_return_val_if_fail(mech != NULL, NULL);
  g_return_val_if_fail(error == NULL || *error == NULL, NULL);

  g_mutex_lock(context->mutex);

  status = gsasl_server_start(context->gsasl, mech, &gsasl_session);
  if(status != GSASL_OK)
  {
    g_mutex_unlock(context->mutex);
    inf_gsasl_set_error(error, status);
    return NULL;
  }

  session = inf_sasl_context_start_session(
    context,
    io,
    gsasl_session,
    session_data,
    error
  );

  if(session == NULL)
  {
    gsasl_finish(gsasl_session);
    g_mutex_unlock(context->mutex);
    return NULL;
  }

  g_mutex_unlock(context->mutex);
  return session;
}

/**
 * inf_sasl_context_server_list_mechanisms:
 * @context: A #InfSaslContext.
 * @error: Location to store error information, if any.
 *
 * Returns a newly allocated space-separated string containing SASL mechanisms
 * that @context supports for server sessions.
 *
 * Returns: A newly allocated string. Free with gsasl_free() when no longer
 * in use.
 */
char*
inf_sasl_context_server_list_mechanisms(InfSaslContext* context,
                                        GError** error)
{
  int ret;
  char* out;

  g_return_val_if_fail(context != NULL, NULL);
  g_return_val_if_fail(error == NULL || *error == NULL, NULL);

  g_mutex_lock(context->mutex);
  ret = gsasl_server_mechlist(context->gsasl, &out);
  g_mutex_unlock(context->mutex);

  if(ret != GSASL_OK)
  {
    inf_gsasl_set_error(error, ret);
    if(out) gsasl_free(out);
    return NULL;
  }

  return out;
}

/**
 * inf_sasl_context_server_supports_mechanism:
 * @context: A #InfSaslContext.
 * @mech: The name of the mechanism to be tested.
 *
 * Checks whether @context supports the mechanism with name @mech for
 * server sessions.
 *
 * Returns: %TRUE if @mech is supported or %FALSE otherwise.
 */
gboolean
inf_sasl_context_server_supports_mechanism(InfSaslContext* context,
                                           const char* mech)
{
  int ret;

  g_return_val_if_fail(context != NULL, FALSE);
  g_return_val_if_fail(mech != NULL, FALSE);

  g_mutex_lock(context->mutex);
  ret = gsasl_server_support_p(context->gsasl, mech);
  g_mutex_unlock(context->mutex);

  return ret != 0;
}

/**
 * inf_sasl_context_stop_session:
 * @context: A #InfSaslContext.
 * @session: A #InfSaslContextSession created with @context.
 *
 * Finishes @session and frees all resources allocated to it. This can be used
 * to cancel an authentication session, or to free it after it finished
 * (either successfully or not).
 *
 * @session should no longer be used after this function was called.
 */
void
inf_sasl_context_stop_session(InfSaslContext* context,
                              InfSaslContextSession* session)
{
  g_return_if_fail(context != NULL);
  g_return_if_fail(session != NULL);

  g_mutex_lock(context->mutex);
  g_return_if_fail(g_slist_find(context->sessions, session) != NULL);
  g_return_if_fail(session->context == context);
  g_mutex_unlock(context->mutex);

  /* Tell client thread to terminate */
  g_async_queue_push(
    session->session_queue,
    inf_sasl_context_message_terminate(session)
  );

  g_thread_join(session->thread);

  g_mutex_lock(context->mutex);
  if(session->dispatch != NULL)
    inf_io_remove_dispatch(session->main_io, session->dispatch);

  context->sessions = g_slist_remove(context->sessions, session);
  gsasl_finish(session->session);
  g_mutex_unlock(context->mutex);

  /* Note that this assertion should hold because us pushing the terminate
   * message into the end of the queue, and all other queued messages will
   * have been processed before. */
  g_async_queue_unref(session->session_queue);

  g_object_unref(session->main_io);

  g_free(session->step64);
  g_slice_free(InfSaslContextSession, session);
}

/**
 * inf_sasl_context_session_get_property:
 * @session: A #InfSaslContextSession.
 * @prop: A SASL property.
 *
 * Returns the value of the property @prop in @session. If the value does not
 * yet exist then this function returns %NULL. It does not invoke the
 * #InfSaslContextCallbackFunc to query it.
 *
 * Returns: The value of the property, or %NULL. The value is owned by the
 * session and must not be freed.
 */
const char*
inf_sasl_context_session_get_property(InfSaslContextSession* session,
                                      Gsasl_property prop)
{
  const char* property;

  g_return_val_if_fail(session != NULL, NULL);

  /* TODO: We should g_strdup the return value for thread safety reasons */

  g_mutex_lock(session->context->mutex);
  property = gsasl_property_fast(session->session, prop);
  g_mutex_unlock(session->context->mutex);

  return property;
}

/**
 * inf_sasl_context_session_set_property:
 * @session: A #InfSaslContextSession.
 * @prop: A SASL property.
 * @value: The value to set the property to.
 *
 * Sets the property @prop in @session to @value.
 */
void
inf_sasl_context_session_set_property(InfSaslContextSession* session,
                                      Gsasl_property prop,
                                      const char* value)
{
  g_return_if_fail(session != NULL);

  g_mutex_lock(session->context->mutex);
  gsasl_property_set(session->session, prop, value);
  g_mutex_unlock(session->context->mutex);
}

/**
 * inf_sasl_context_session_continue:
 * @session: A #InfSaslContextSession.
 * @retval: Error code of the operation requested.
 *
 * When the callback function specified in inf_sasl_context_set_callback()
 * is called then @session waits for the user to call
 * inf_sasl_context_session_continue(). It should do so once it provided the
 * requested property using inf_sasl_context_session_set_property() with
 * @retval being %GSASL_OK. If it decides that the property cannot be provided
 * then it should still call this function with @retval being a SASL error
 * code specifying the problem.
 */
void
inf_sasl_context_session_continue(InfSaslContextSession* session,
                                  int retval)
{
  g_return_if_fail(session != NULL);

  g_async_queue_push(
    session->session_queue,
    inf_sasl_context_message_continue(session, retval)
  );
}

/**
 * inf_sasl_context_session_feed:
 * @session: A #InfSaslContextSession.
 * @data: The data to feed to the SASL session.
 * @func: The function to call when the data has been processed.
 * @user_data: Additional user data to pass to @func.
 *
 * This function feeds data from the session's remote counterpart to @session.
 * It should be base64 encoded. This function will, asynchronously, process
 * the data and query for properties it requires to do so. Once it has
 * finished, @func is called with output data to send to the remote side to
 * be fed to its session counterpart.
 *
 * This function must not be called again before @func was called.
 */
void
inf_sasl_context_session_feed(InfSaslContextSession* session,
                              const char* data,
                              InfSaslContextSessionFeedFunc func,
                              gpointer user_data)
{
  g_return_if_fail(session != NULL);
  g_return_if_fail(func != NULL);
  g_return_if_fail(session->stepping == FALSE);
  /*g_return_if_fail(session->context->callback != NULL); not threadsafe */

  session->stepping = TRUE;

  g_async_queue_push(
    session->session_queue,
    inf_sasl_context_message_step(session, data, func, user_data)
  );
}

/**
 * inf_sasl_context_session_is_processing:
 * @session: A #InfSaslContextSession.
 *
 * Returns whether the session is currently asynchronously processing data
 * fed to it with inf_sasl_context_session_feed(). In this case the first
 * call needs to finish before another one is allowed to be made.
 *
 * Returns: Whether @session is currently processing data asynchronously.
 */
gboolean
inf_sasl_context_session_is_processing(InfSaslContextSession* session)
{
  g_return_val_if_fail(session != NULL, FALSE);
  return session->stepping;
}

/* vim:set et sw=2 ts=2: */
