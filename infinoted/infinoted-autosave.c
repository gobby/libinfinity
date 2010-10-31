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

/* TODO: Make a GObject out of this, and move it into libinfinity */

#include <infinoted/infinoted-autosave.h>
#include <libinfinity/inf-i18n.h>
#include <libinfinity/inf-signals.h>

typedef struct _InfinotedAutosaveSession InfinotedAutosaveSession;
struct _InfinotedAutosaveSession {
  InfinotedAutosave* autosave;
  InfdDirectoryIter iter;
  InfdSessionProxy* proxy;
  InfIoTimeout* timeout;
};

static InfinotedAutosaveSession*
infinoted_autosave_find_session(InfinotedAutosave* autosave,
                                InfdDirectoryIter* iter)
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
  InfinotedAutosaveSession* session;
  InfBuffer* buffer;

  session = (InfinotedAutosaveSession*)user_data;
  buffer = inf_session_get_buffer(
    infd_session_proxy_get_session(session->proxy)
  );

  if(inf_buffer_get_modified(buffer) == TRUE)
  {
    if(session->timeout == NULL)
      infinoted_autosave_session_start(session->autosave, session);
  }
  else
  {
    if(session->timeout != NULL)
      infinoted_autosave_session_stop(session->autosave, session);
  }
}

static void
infinoted_autosave_session_save(InfinotedAutosave* autosave,
                                InfinotedAutosaveSession* session)
{
  InfdDirectory* directory;
  InfdDirectoryIter* iter;
  GError* error;
  gchar* path;
  InfBuffer* buffer;

  directory = autosave->directory;
  iter = &session->iter;
  error = NULL;

  if(session->timeout != NULL)
  {
    inf_io_remove_timeout(
      infd_directory_get_io(directory),
      session->timeout
    );

    session->timeout = NULL;
  }

  buffer = inf_session_get_buffer(
    infd_session_proxy_get_session(session->proxy)
  );

  inf_signal_handlers_block_by_func(
    G_OBJECT(buffer),
    G_CALLBACK(infinoted_autosave_buffer_notify_modified_cb),
    session
  );

  if(infd_directory_iter_save_session(directory, iter, &error) == FALSE)
  {
    path = infd_directory_iter_get_path(directory, iter);
    g_warning(
      _("Failed to auto-save session \"%s\": %s\n\n"
        "Will retry in %u seconds."),
      path,
      error->message,
      session->autosave->autosave_interval
    );

    g_free(path);
    g_error_free(error);

    infinoted_autosave_session_start(session->autosave, session);
  }
  else
  {
    /* TODO: Remove this as soon as directory itself unsets modified flag
     * on session_write */
    inf_buffer_set_modified(INF_BUFFER(buffer), FALSE);
  }

  inf_signal_handlers_unblock_by_func(
    G_OBJECT(buffer),
    G_CALLBACK(infinoted_autosave_buffer_notify_modified_cb),
    session
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
                               InfdDirectoryIter* iter)
{
  InfinotedAutosaveSession* session;
  InfdSessionProxy* proxy;
  InfBuffer* buffer;

  g_assert(infinoted_autosave_find_session(autosave, iter) == NULL);

  session = g_slice_new(InfinotedAutosaveSession);
  session->autosave = autosave;
  session->iter = *iter;

  proxy = infd_directory_iter_peek_session(autosave->directory, iter);
  g_assert(proxy != NULL);
  session->proxy = proxy;
  session->timeout = NULL;

  autosave->sessions = g_slist_prepend(autosave->sessions, session);

  buffer = inf_session_get_buffer(infd_session_proxy_get_session(proxy));

  g_signal_connect(
    G_OBJECT(buffer),
    "notify::modified",
    G_CALLBACK(infinoted_autosave_buffer_notify_modified_cb),
    session
  );

  if(inf_buffer_get_modified(buffer) == TRUE)
  {
    infinoted_autosave_session_start(autosave, session);
  }
}

static void
infinoted_autosave_remove_session(InfinotedAutosave* autosave,
                                  InfinotedAutosaveSession* session)
{
  InfBuffer* buffer;

  /* Cancel autosave timeout even if session is modified. If the directory
   * removed the session, then it has already saved it anyway. */
  if(session->timeout != NULL)
    infinoted_autosave_session_stop(autosave, session);

  buffer =
    inf_session_get_buffer(infd_session_proxy_get_session(session->proxy));

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(buffer),
    G_CALLBACK(infinoted_autosave_buffer_notify_modified_cb),
    session
  );

  autosave->sessions = g_slist_remove(autosave->sessions, session);
  g_slice_free(InfinotedAutosaveSession, session);
}

static void
infinoted_autosave_directory_add_session_cb(InfdDirectory* directory,
                                            InfdDirectoryIter* iter,
                                            InfdSessionProxy* proxy,
                                            gpointer user_data)
{
  InfinotedAutosave* autosave;
  autosave = (InfinotedAutosave*)user_data;

  infinoted_autosave_add_session(autosave, iter);
}

static void
infinoted_autosave_directory_remove_session_cb(InfdDirectory* directory,
                                               InfdDirectoryIter* iter,
                                               InfdSessionProxy* proxy,
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
                                  InfdDirectoryIter* iter)
{
  InfdDirectoryIter child;
  InfdSessionProxy* session;

  if(infd_directory_iter_get_node_type(autosave->directory, iter) ==
     INFD_STORAGE_NODE_SUBDIRECTORY)
  {
    if(infd_directory_iter_get_explored(autosave->directory, iter) == TRUE)
    {
      /* Errors can't happen as the directory is already explored */
      child = *iter;
      if(infd_directory_iter_get_child(autosave->directory, &child, NULL))
      {
        do {
          infinoted_autosave_walk_directory(autosave, &child);
        } while(infd_directory_iter_get_next(autosave->directory, &child));
      }
    }
  }
  else
  {
    session = infd_directory_iter_peek_session(autosave->directory, iter);
    if(session != NULL)
      infinoted_autosave_add_session(autosave, iter);
  }
}

/**
 * infinoted_autosave_new:
 * @directory: A #InfdDirectory.
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
                       unsigned int autosave_interval)
{
  InfinotedAutosave* autosave;
  InfdDirectoryIter iter;

  autosave = g_slice_new(InfinotedAutosave);

  autosave->directory = directory;
  autosave->autosave_interval = autosave_interval;
  autosave->sessions = NULL;
  g_object_ref(directory);

  g_signal_connect_after(
    G_OBJECT(directory),
    "add-session",
    G_CALLBACK(infinoted_autosave_directory_add_session_cb),
    autosave
  );

  g_signal_connect_after(
    G_OBJECT(directory),
    "remove-session",
    G_CALLBACK(infinoted_autosave_directory_remove_session_cb),
    autosave
  );

  infd_directory_iter_get_root(directory, &iter);
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
    G_CALLBACK(infinoted_autosave_directory_add_session_cb),
    autosave
  );

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(autosave->directory),
    G_CALLBACK(infinoted_autosave_directory_remove_session_cb),
    autosave
  );

  while(autosave->sessions != NULL)
  {
    infinoted_autosave_remove_session(
      autosave,
      (InfinotedAutosaveSession*)autosave->sessions->data
    );
  }

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
