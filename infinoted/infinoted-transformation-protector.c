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

#include <infinoted/infinoted-transformation-protector.h>
#include <libinfinity/server/infd-session-proxy.h>
#include <libinfinity/adopted/inf-adopted-session.h>
#include <libinfinity/inf-signals.h>

typedef struct _InfinotedTransformationProtectorSession
  InfinotedTransformationProtectorSession;
struct _InfinotedTransformationProtectorSession {
  InfinotedTransformationProtector* protector;
  InfdSessionProxy* proxy;
};

static InfinotedTransformationProtectorSession*
infinoted_transformation_protector_find_session(
  InfinotedTransformationProtector* protector,
  InfSessionProxy* proxy)
{
  GSList* item;
  InfinotedTransformationProtectorSession* sess;

  for(item = protector->sessions; item != NULL; item = g_slist_next(item))
  {
    sess = (InfinotedTransformationProtectorSession*)item->data;
    if(INF_SESSION_PROXY(sess->proxy) == proxy)
      return sess;
  }

  return NULL;
}

static void
infinoted_transformation_protector_execute_request_cb(InfAdoptedAlgorithm* algorithm,
                                                      InfAdoptedUser* user,
                                                      InfAdoptedRequest* request,
                                                      gboolean apply,
                                                      gpointer user_data)
{
  InfinotedTransformationProtectorSession* sess;
  InfXmlConnection* connection;
  guint vdiff;

  sess = (InfinotedTransformationProtectorSession*)user_data;

  vdiff = inf_adopted_state_vector_vdiff(
    inf_adopted_request_get_vector(request),
    inf_adopted_algorithm_get_current(algorithm)
  );

  if(vdiff > sess->protector->max_vdiff)
  {
    connection = inf_user_get_connection(INF_USER(user));

    /* Local requests do not need to be transformed, so always have a
     * zero vdiff. */
    g_assert(connection != NULL);

    /* Prevent the request from being transformed */
    g_signal_stop_emission_by_name(algorithm, "execute-request");

    /* TODO: Take care of this in some other way, by rearranging the way
     * execute-request and apply-request are called. */
#if 0
    sess->protector->log->current_session = NULL;
    sess->protector->log->current_request = NULL;
    sess->protector->log->current_user = NULL;
#endif

    /* Kill the connection (if any) */
    infd_session_proxy_unsubscribe(sess->proxy, connection);
  }
}

static void
infinoted_transformation_protector_add_session(
  InfinotedTransformationProtector* protector,
  InfdSessionProxy* proxy)
{
  InfinotedTransformationProtectorSession* sess;
  InfSession* session;
  InfAdoptedAlgorithm* algorithm;

  sess = g_slice_new(InfinotedTransformationProtectorSession);
  sess->protector = protector;
  sess->proxy = proxy;
  g_object_ref(proxy);

  /* TODO: Make sure the central method is used */

  protector->sessions = g_slist_prepend(protector->sessions, sess);

  g_object_get(G_OBJECT(proxy), "session", &session, NULL);
  g_assert(INF_ADOPTED_IS_SESSION(session));
  algorithm = inf_adopted_session_get_algorithm(INF_ADOPTED_SESSION(session));

  g_signal_connect(
    G_OBJECT(algorithm),
    "execute-request",
    G_CALLBACK(infinoted_transformation_protector_execute_request_cb),
    sess
  );

  g_object_unref(session);
}

static void
infinoted_transformation_protector_remove_session(
  InfinotedTransformationProtector* protector,
  InfinotedTransformationProtectorSession* sess)
{
  InfSession* session;
  g_object_get(G_OBJECT(sess->proxy), "session", &session, NULL);

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(inf_adopted_session_get_algorithm(INF_ADOPTED_SESSION(session))),
    G_CALLBACK(infinoted_transformation_protector_execute_request_cb),
    sess
  );

  g_object_unref(session);
  g_object_unref(sess->proxy);
  protector->sessions = g_slist_remove(protector->sessions, sess);

  g_slice_free(InfinotedTransformationProtectorSession, sess);
}

static void
infinoted_transformation_protector_subscribe_session_cb(
  InfBrowser* browser,
  const InfBrowserIter* iter,
  InfSessionProxy* proxy,
  gpointer user_data)
{
  InfinotedTransformationProtector* protector;
  InfSession* session;

  protector = (InfinotedTransformationProtector*)user_data;
  g_object_get(G_OBJECT(proxy), "session", &session, NULL);

  if(INF_ADOPTED_IS_SESSION(session))
  {
    infinoted_transformation_protector_add_session(
      protector,
      INFD_SESSION_PROXY(proxy)
    );
  }

  g_object_unref(session);
}

static void
infinoted_transformation_protector_unsubscribe_session_cb(
  InfBrowser* browser,
  const InfBrowserIter* iter,
  InfSessionProxy* proxy,
  gpointer user_data)
{
  InfinotedTransformationProtector* protector;
  InfinotedTransformationProtectorSession* sess;
  InfSession* session;

  protector = (InfinotedTransformationProtector*)user_data;
  g_object_get(G_OBJECT(proxy), "session", &session, NULL);

  if(INF_ADOPTED_IS_SESSION(session))
  {
    sess = infinoted_transformation_protector_find_session(protector, proxy);

    g_assert(sess != NULL);
    infinoted_transformation_protector_remove_session(protector, sess);
  }

  g_object_unref(session);
}

static void
infinoted_transformation_protector_walk_directory(
  InfinotedTransformationProtector* protector,
  const InfBrowserIter* iter)
{
  InfBrowser* browser;
  InfBrowserIter child;
  InfSessionProxy* proxy;
  GError* error;
  gchar* path;

  browser = INF_BROWSER(protector->directory);
  if(inf_browser_is_subdirectory(browser, iter) == TRUE)
  {
    if(inf_browser_get_explored(browser, iter) == TRUE)
    {
      child = *iter;
      inf_browser_get_child(browser, &child);
      do
      {
        infinoted_transformation_protector_walk_directory(
          protector,
          &child
        );
      } while(inf_browser_get_next(browser, &child));
    }
  }
  else
  {
    proxy = inf_browser_get_session(browser, iter);
    if(proxy != NULL)
    {
      infinoted_transformation_protector_add_session(
        protector,
        INFD_SESSION_PROXY(proxy)
      );
    }
  }
}

/**
 * infinoted_transformation_protector_new:
 * @directory: A #InfdDirectory.
 * @max_vdiff: Maximum vdiff to allow for transformations.
 *
 * Creates a new #InfinotedTransformationPretctor objects which will watch all
 * sessions in @directory. What a transformation protector does is it will
 * check for each incoming request that its vdiff to the current document
 * state (see inf_adopted_state_vector_vdiff()) is below a certain threshold,
 * @max_vdiff.
 *
 * If the vdiff is above the threshold, the request will be ignored and the
 * connection is unsubscribed from the session. The reason for this is that
 * with the current version of the protocol the client cannot be told to do
 * a rollback of his request, and so the session would otherwise become
 * inconsistent for it.
 *
 * This tool works only if the communication method for the session is the
 * "central" one. Otherwise it cannot be guaranteed that other participants of
 * the session have already received and executed the request.
 *
 * Returns: A new #InfinotedTransformationProtector.
 */
InfinotedTransformationProtector*
infinoted_transformation_protector_new(InfdDirectory* directory,
                                       InfinotedLog* log,
                                       guint max_vdiff)
{
  InfinotedTransformationProtector* protector;
  InfBrowserIter iter;

  protector = g_slice_new(InfinotedTransformationProtector);
  protector->directory = directory;
  protector->log = log;
  protector->max_vdiff = max_vdiff;
  protector->sessions = NULL;

  g_object_ref(directory);

  g_signal_connect_after(
    G_OBJECT(directory),
    "subscribe-session",
    G_CALLBACK(infinoted_transformation_protector_subscribe_session_cb),
    protector
  );

  g_signal_connect_after(
    G_OBJECT(directory),
    "unsubscribe-session",
    G_CALLBACK(infinoted_transformation_protector_unsubscribe_session_cb),
    protector
  );

  inf_browser_get_root(INF_BROWSER(directory), &iter);
  infinoted_transformation_protector_walk_directory(protector, &iter);
  return protector;
}

/**
 * infinoted_transformation_protector_free:
 * @pt: A #InfinotedTransformationProtector.
 *
 * Frees the given #InfinotedTransformationProtector.
 */
void
infinoted_transformation_protector_free(InfinotedTransformationProtector* pt)
{
  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(pt->directory),
    G_CALLBACK(infinoted_transformation_protector_subscribe_session_cb),
    pt
  );

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(pt->directory),
    G_CALLBACK(infinoted_transformation_protector_unsubscribe_session_cb),
    pt
  );

  while(pt->sessions != NULL)
  {
    infinoted_transformation_protector_remove_session(
      pt,
      (InfinotedTransformationProtectorSession*)pt->sessions->data
    );
  }

  g_object_unref(pt->directory);

  g_slice_free(InfinotedTransformationProtector, pt);
}

/**
 * infinoted_transformation_protector_set_max_vdiff:
 * @pt: A #InfinotedTransformationProtector.
 * @max_vdiff: Maximum vdiff to allow for transformations.
 *
 * Changes the maximum allowed vdiff of @pt to @max_vdiff.
 */
void
infinoted_transformation_protector_set_max_vdiff(
  InfinotedTransformationProtector* pt,
  guint max_vdiff)
{
  pt->max_vdiff = max_vdiff;
}

/* vim:set et sw=2 ts=2: */
