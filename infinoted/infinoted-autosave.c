/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2011 Armin Burgmeier <armin@arbur.net>
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

/* TODO: Make a GObject out of this, and move it into libinfinity */

#include <glib.h>
#include <infinoted/infinoted-autosave.h>
#include <libinfinity/inf-i18n.h>
#include <libinfinity/inf-signals.h>

typedef struct _InfinotedAutosaveSession InfinotedAutosaveSession;
struct _InfinotedAutosaveSession {
  InfinotedAutosave* autosave;
  InfBrowserIter iter;
  InfSessionProxy* proxy;
  InfIoTimeout* timeout;
};

static InfinotedAutosaveSession*
infinoted_autosave_find_session(InfinotedAutosave* autosave,
                                const InfBrowserIter* iter)
{
  GSList* item;
  InfinotedAutosaveSession* session;

  for(item = autosave->sessions; item != NULL; item = g_slist_next(item))
  {
    session = (InfinotedAutosaveSession*)item->data;

    /* TODO: Add a infd_directory_iter_compare() method in libinfinity */
    if(session->iter.node == iter->node &&
       session->iter.node_id == iter->node_id)
    {
      return session;
    }
  }

  return NULL;
}

/* Required by infinoted_autosave_session_start */
static void
infinoted_autosave_session_timeout_cb(gpointer user_data);

static void
infinoted_autosave_session_start(InfinotedAutosave* autosave,
                                 InfinotedAutosaveSession* session)
{
  InfIo* io;
  io = infd_directory_get_io(autosave->directory);

  g_assert(session->timeout == NULL);

  session->timeout = inf_io_add_timeout(
    io,
    autosave->autosave_interval * 1000,
    infinoted_autosave_session_timeout_cb,
    session,
    NULL
  );
}

static void
infinoted_autosave_session_stop(InfinotedAutosave* autosave,
                                InfinotedAutosaveSession* session)
{
  InfIo* io;
  io = infd_directory_get_io(autosave->directory);

  g_assert(session->timeout != NULL);

  inf_io_remove_timeout(io, session->timeout);
  session->timeout = NULL;
}

static void
infinoted_autosave_buffer_notify_modified_cb(GObject* object,
                                             GParamSpec* pspec,
                                             gpointer user_data)
{
  InfinotedAutosaveSession* autosave_session;
  InfSession* session;
  InfBuffer* buffer;

  autosave_session = (InfinotedAutosaveSession*)user_data;
  g_object_get(G_OBJECT(autosave_session->proxy), "session", &session, NULL);
  buffer = inf_session_get_buffer(session);

  if(inf_buffer_get_modified(buffer) == TRUE)
  {
    if(autosave_session->timeout == NULL)
    {
      infinoted_autosave_session_start(
        autosave_session->autosave,
        autosave_session
      );
    }
  }
  else
  {
    if(autosave_session->timeout != NULL)
    {
      infinoted_autosave_session_stop(
        autosave_session->autosave,
        autosave_session
      );
    }
  }

  g_object_unref(session);
}

static void
infinoted_autosave_session_save(InfinotedAutosave* autosave,
                                InfinotedAutosaveSession* autosave_session)
{
  InfdDirectory* directory;
  InfBrowserIter* iter;
  GError* error;
  gchar* path;
  InfSession* session;
  InfBuffer* buffer;
  gchar* root_directory;
  gchar* argv[4];

  directory = autosave->directory;
  iter = &autosave_session->iter;
  error = NULL;

  if(autosave_session->timeout != NULL)
  {
    inf_io_remove_timeout(
      infd_directory_get_io(directory),
      autosave_session->timeout
    );

    autosave_session->timeout = NULL;
  }

  g_object_get(G_OBJECT(autosave_session->proxy), "session", &session, NULL);
  buffer = inf_session_get_buffer(session);
  g_object_unref(session);

  inf_signal_handlers_block_by_func(
    G_OBJECT(buffer),
    G_CALLBACK(infinoted_autosave_buffer_notify_modified_cb),
    autosave_session
  );

  if(infd_directory_iter_save_session(directory, iter, &error) == FALSE)
  {
    path = inf_browser_get_path(INF_BROWSER(directory), iter);
    g_warning(
      _("Failed to auto-save session \"%s\": %s\n\n"
        "Will retry in %u seconds."),
      path,
      error->message,
      autosave_session->autosave->autosave_interval
    );

    g_free(path);
    g_error_free(error);
    error = NULL;

    infinoted_autosave_session_start(
      autosave_session->autosave,
      autosave_session
    );
  }
  else
  {
    /* TODO: Remove this as soon as directory itself unsets modified flag
     * on session_write */
    inf_buffer_set_modified(INF_BUFFER(buffer), FALSE);

    if(autosave->autosave_hook != NULL)
    {
      path = inf_browser_get_path(INF_BROWSER(directory), iter);

      g_object_get(
        G_OBJECT(infd_directory_get_storage(directory)),
        "root-directory",
        &root_directory,
        NULL
      );

      argv[0] = autosave->autosave_hook;
      argv[1] = root_directory;
      argv[2] = path;
      argv[3] = NULL;

      if(!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                        NULL, NULL, NULL, &error))
      {
        g_warning(
          _("Could not execute autosave hook: \"%s\""),
          error->message
        );

        g_error_free(error);
        error = NULL;
      }

      g_free(path);
      g_free(root_directory);
    }
  }
  
  inf_signal_handlers_unblock_by_func(
    G_OBJECT(buffer),
    G_CALLBACK(infinoted_autosave_buffer_notify_modified_cb),
    autosave_session
  );
}

static void
infinoted_autosave_session_timeout_cb(gpointer user_data)
{
  InfinotedAutosaveSession* session;

  session = (InfinotedAutosaveSession*)user_data;
  session->timeout = NULL;

  infinoted_autosave_session_save(session->autosave, session);
}

static void
infinoted_autosave_add_session(InfinotedAutosave* autosave,
                               const InfBrowserIter* iter)
{
  InfinotedAutosaveSession* autosave_session;
  InfSessionProxy* proxy;
  InfSession* session;
  InfBuffer* buffer;

  g_assert(infinoted_autosave_find_session(autosave, iter) == NULL);

  autosave_session = g_slice_new(InfinotedAutosaveSession);
  autosave_session->autosave = autosave;
  autosave_session->iter = *iter;

  proxy = inf_browser_get_session(INF_BROWSER(autosave->directory), iter);
  g_assert(proxy != NULL);
  autosave_session->proxy = proxy;
  autosave_session->timeout = NULL;

  autosave->sessions = g_slist_prepend(autosave->sessions, autosave_session);

  g_object_get(G_OBJECT(proxy), "session", &session, NULL);
  buffer = inf_session_get_buffer(session);
  g_object_unref(session);

  g_signal_connect(
    G_OBJECT(buffer),
    "notify::modified",
    G_CALLBACK(infinoted_autosave_buffer_notify_modified_cb),
    autosave_session
  );

  if(inf_buffer_get_modified(buffer) == TRUE)
  {
    infinoted_autosave_session_start(autosave, autosave_session);
  }
}

static void
infinoted_autosave_remove_session(InfinotedAutosave* autosave,
                                  InfinotedAutosaveSession* autosave_session)
{
  InfSession* session;
  InfBuffer* buffer;

  /* Cancel autosave timeout even if session is modified. If the directory
   * removed the session, then it has already saved it anyway. */
  if(autosave_session->timeout != NULL)
    infinoted_autosave_session_stop(autosave, autosave_session);

  g_object_get(G_OBJECT(autosave_session->proxy), "session", &session, NULL);
  buffer = inf_session_get_buffer(session);
  g_object_unref(session);

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(buffer),
    G_CALLBACK(infinoted_autosave_buffer_notify_modified_cb),
    autosave_session
  );

  autosave->sessions = g_slist_remove(autosave->sessions, autosave_session);
  g_slice_free(InfinotedAutosaveSession, autosave_session);
}

static void
infinoted_autosave_directory_subscribe_session_cb(
  InfBrowser* browser,
  const InfBrowserIter* iter,
  InfSessionProxy* proxy,
  gpointer user_data)
{
  InfinotedAutosave* autosave;
  autosave = (InfinotedAutosave*)user_data;

  infinoted_autosave_add_session(autosave, iter);
}

static void
infinoted_autosave_directory_unsubscribe_session_cb(
  InfBrowser* browser,
  const InfBrowserIter* iter,
  InfSessionProxy* proxy,
  gpointer user_data)
{
  InfinotedAutosave* autosave;
  InfinotedAutosaveSession* session;

  autosave = (InfinotedAutosave*)user_data;
  session = infinoted_autosave_find_session(autosave, iter);
  g_assert(session != NULL && session->proxy == proxy);

  infinoted_autosave_remove_session(autosave, session);
}

static void
infinoted_autosave_walk_directory(InfinotedAutosave* autosave,
                                  InfBrowserIter* iter)
{
  InfBrowser* browser;
  InfBrowserIter child;
  InfSessionProxy* proxy;

  browser = INF_BROWSER(autosave->directory);

  if(inf_browser_is_subdirectory(browser, iter) == TRUE)
  {
    if(inf_browser_get_explored(browser, iter) == TRUE)
    {
      child = *iter;
      if(inf_browser_get_child(browser, &child) == TRUE)
      {
        do {
          infinoted_autosave_walk_directory(autosave, &child);
        } while(inf_browser_get_next(browser, &child) == TRUE);
      }
    }
  }
  else
  {
    proxy = inf_browser_get_session(browser, iter);
    if(proxy != NULL)
      infinoted_autosave_add_session(autosave, iter);
  }
}

/**
 * infinoted_autosave_new:
 * @directory: A #InfdDirectory.
 * @autosave_hook: Command to run after saving the documents in @directory.
 * @autosave_interval: The interval in which to save documents in @directory,
 * in seconds.
 *
 * Creates a new #InfinotedAutosave object which will save all documents
 * in @directory every @autosave_interval seconds into the directory's
 * background storage.
 *
 * Returns: A new #InfinotedAutosave.
 */
InfinotedAutosave*
infinoted_autosave_new(InfdDirectory* directory,
                       unsigned int autosave_interval,
                       gchar* autosave_hook)
{
  InfinotedAutosave* autosave;
  InfBrowserIter iter;

  autosave = g_slice_new(InfinotedAutosave);

  autosave->directory = directory;
  autosave->autosave_interval = autosave_interval;
  autosave->autosave_hook = g_strdup(autosave_hook);
  autosave->sessions = NULL;
  g_object_ref(directory);

  g_signal_connect_after(
    G_OBJECT(directory),
    "subscribe-session",
    G_CALLBACK(infinoted_autosave_directory_subscribe_session_cb),
    autosave
  );

  g_signal_connect_after(
    G_OBJECT(directory),
    "unsubscribe-session",
    G_CALLBACK(infinoted_autosave_directory_unsubscribe_session_cb),
    autosave
  );

  inf_browser_get_root(INF_BROWSER(directory), &iter);
  infinoted_autosave_walk_directory(autosave, &iter);
  return autosave;
}

/**
 * infinoted_autosave_free:
 * @autosave: A #InfinotedAutosave.
 *
 * Frees the given #InfinotedAutosave. This function does not save changes
 * that don't have been saved yet (the InfdDirectory will do so when it is
 * freed). Use infinoted_autosave_save_immediately() before freeing the
 * #InfinotedAutosave if you want it to save everything.
 */
void
infinoted_autosave_free(InfinotedAutosave* autosave)
{
  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(autosave->directory),
    G_CALLBACK(infinoted_autosave_directory_subscribe_session_cb),
    autosave
  );

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(autosave->directory),
    G_CALLBACK(infinoted_autosave_directory_unsubscribe_session_cb),
    autosave
  );

  while(autosave->sessions != NULL)
  {
    infinoted_autosave_remove_session(
      autosave,
      (InfinotedAutosaveSession*)autosave->sessions->data
    );
  }

  g_free(autosave->autosave_hook);
  g_object_unref(autosave->directory);
  g_slice_free(InfinotedAutosave, autosave);
}

/**
 * infinoted_autosave_save_immediately:
 * @autosave: A #InfinotedAutosave.
 *
 * Saves all changes in all documents immediately, instead of waiting until
 * the autosave interval has elapsed.
 */
void
infinoted_autosave_save_immediately(InfinotedAutosave* autosave)
{
  GSList* item;
  InfinotedAutosaveSession* session;

  for(item = autosave->sessions; item != NULL; item = g_slist_next(item))
  {
    session = (InfinotedAutosaveSession*)item->data;
    if(session->timeout != NULL)
      infinoted_autosave_session_save(autosave, session);
  }
}

/* vim:set et sw=2 ts=2: */
