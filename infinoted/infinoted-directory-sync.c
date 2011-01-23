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

#include <infinoted/infinoted-directory-sync.h>
#include <infinoted/infinoted-util.h>

#include <libinftext/inf-text-buffer.h>
#include <libinftext/inf-text-session.h>

#include <libinfinity/server/infd-session-proxy.h>

#include <libinfinity/inf-i18n.h>
#include <libinfinity/inf-signals.h>

typedef struct _InfinotedDirectorySyncSession InfinotedDirectorySyncSession;
struct _InfinotedDirectorySyncSession {
  InfinotedDirectorySync* dsync;
  InfdDirectoryIter iter;
  InfdSessionProxy* proxy;
  InfIoTimeout* timeout;

  gchar* path;
};

#ifdef G_OS_WIN32
/* Currently only used on Windows, thus ifdef-out on Unix to avoid
 * a compiler warning. */
static GQuark
infinoted_directory_sync_error_quark(void)
{
  return g_quark_from_static_string("INFINOTED_DIRECTORY_SYNC_ERROR");
}
#endif

static InfinotedDirectorySyncSession*
infinoted_directory_sync_find_session(InfinotedDirectorySync* dsync,
                                      InfdDirectoryIter* iter)
{
  GSList* item;
  InfinotedDirectorySyncSession* session;

  for(item = dsync->sessions; item != NULL; item = g_slist_next(item))
  {
    session = (InfinotedDirectorySyncSession*)item->data;

    /* TODO: Add a infd_directory_iter_compare() method in libinfinity */
    if(session->iter.node == iter->node &&
       session->iter.node_id == iter->node_id)
    {
      return session;
    }
  }

  return NULL;
}

/* Required by infinoted_directory_sync_session_start */
static void
infinoted_directory_sync_session_timeout_cb(gpointer user_data);

static void
infinoted_directory_sync_session_start(InfinotedDirectorySync* dsync,
                                       InfinotedDirectorySyncSession* session)
{
  InfIo* io;
  io = infd_directory_get_io(dsync->directory);

  g_assert(session->timeout == NULL);

  session->timeout = inf_io_add_timeout(
    io,
    dsync->sync_interval * 1000,
    infinoted_directory_sync_session_timeout_cb,
    session,
    NULL
  );
}

static void
infinoted_directory_sync_session_stop(InfinotedDirectorySync* dsync,
                                      InfinotedDirectorySyncSession* session)
{
  InfIo* io;
  io = infd_directory_get_io(dsync->directory);

  g_assert(session->timeout != NULL);

  inf_io_remove_timeout(io, session->timeout);
  session->timeout = NULL;
}

static void
infinoted_directory_sync_buffer_text_inserted_cb(InfTextBuffer* buffer,
                                                 guint position,
                                                 InfTextChunk* text,
                                                 InfUser* user,
                                                 gpointer user_data)
{
  InfinotedDirectorySyncSession* session;
  session = (InfinotedDirectorySyncSession*)user_data;

  if(session->timeout == NULL)
    infinoted_directory_sync_session_start(session->dsync, session);
}

static void
infinoted_directory_sync_buffer_text_erased_cb(InfTextBuffer* buffer,
                                               guint position,
                                               InfTextChunk* text,
                                               InfUser* user,
                                               gpointer user_data)
{
  InfinotedDirectorySyncSession* session;
  session = (InfinotedDirectorySyncSession*)user_data;

  if(session->timeout == NULL)
    infinoted_directory_sync_session_start(session->dsync, session);
}

static void
infinoted_directory_sync_session_save(InfinotedDirectorySync* dsync,
                                      InfinotedDirectorySyncSession* session)
{
  InfdDirectoryIter* iter;
  GError* error;
  InfBuffer* buffer;
  InfTextChunk* chunk;
  gchar* content;
  gsize bytes;

  iter = &session->iter;
  error = NULL;

  if(session->timeout != NULL)
  {
    inf_io_remove_timeout(
      infd_directory_get_io(dsync->directory),
      session->timeout
    );

    session->timeout = NULL;
  }

  buffer = inf_session_get_buffer(
    infd_session_proxy_get_session(session->proxy)
  );

  error = NULL;
  if(!infinoted_util_create_dirname(session->path, &error))
  {
    g_warning(_("Failed to create directory for path \"%s\": %s\n\n"),
              session->path, error->message);
    g_error_free(error);
  }
  else
  {
    /* TODO: Use the iterator API here, which should be less expensive */
    chunk = inf_text_buffer_get_slice(
      INF_TEXT_BUFFER(buffer),
      0,
      inf_text_buffer_get_length(INF_TEXT_BUFFER(buffer))
    );

    content = inf_text_chunk_get_text(chunk, &bytes);
    inf_text_chunk_free(chunk);

    if(!g_file_set_contents(session->path, content, bytes, &error))
    {
      g_warning(
        _("Failed to write session for path \"%s\": %s\n\n"
          "Will retry in %u seconds."),
        session->path, error->message, dsync->sync_interval
      );

      g_error_free(error);

      infinoted_directory_sync_session_start(session->dsync, session);
    }

    g_free(content);
  }
}

static void
infinoted_directory_sync_session_timeout_cb(gpointer user_data)
{
  InfinotedDirectorySyncSession* session;

  session = (InfinotedDirectorySyncSession*)user_data;
  session->timeout = NULL;

  infinoted_directory_sync_session_save(session->dsync, session);
}

static gboolean
infinoted_directory_sync_add_session(InfinotedDirectorySync* dsync,
                                     InfdDirectoryIter* iter,
                                     GError** error)
{
  InfinotedDirectorySyncSession* session;
  InfdSessionProxy* proxy;
  InfBuffer* buffer;
  gchar* iter_path;
#ifdef G_OS_WIN32
  gchar* pos;
#endif
  gchar* full_path;
  gchar* converted;

  g_assert(infinoted_directory_sync_find_session(dsync, iter) == NULL);

  proxy = infd_directory_iter_peek_session(dsync->directory, iter);
  g_assert(proxy != NULL);

  /* Ignore if this is not a text session */
  if(!INF_TEXT_IS_SESSION(infd_session_proxy_get_session(proxy)))
    return TRUE;

  iter_path = infd_directory_iter_get_path(dsync->directory, iter);
#ifdef G_OS_WIN32
  for(pos = iter_path; *pos != '\0'; ++pos)
  {
    if(*pos == '\\')
    {
      g_set_error(
        error,
        infinoted_directory_sync_error_quark(),
        INFINOTED_DIRECTORY_SYNC_ERROR_INVALID_PATH,
        _("Node \"%s\" contains invalid characters"),
        iter_path
      );

      g_free(iter_path);
      return FALSE;
    }
    else if(*pos == '/')
    {
      *pos = '\\';
    }
  }
#endif

  full_path = g_build_filename(dsync->sync_directory, iter_path+1, NULL);
  g_free(iter_path);

  converted = g_filename_from_utf8(full_path, -1, NULL, NULL, error);
  g_free(full_path);
  if(!converted) return FALSE;

  session = g_slice_new(InfinotedDirectorySyncSession);
  session->dsync = dsync;
  session->iter = *iter;

  session->proxy = proxy;
  session->timeout = NULL;
  session->path = converted;

  dsync->sessions = g_slist_prepend(dsync->sessions, session);

  buffer = inf_session_get_buffer(infd_session_proxy_get_session(proxy));

  g_signal_connect(
    G_OBJECT(buffer),
    "text-inserted",
    G_CALLBACK(infinoted_directory_sync_buffer_text_inserted_cb),
    session
  );

  g_signal_connect(
    G_OBJECT(buffer),
    "text-erased",
    G_CALLBACK(infinoted_directory_sync_buffer_text_erased_cb),
    session
  );

  infinoted_directory_sync_session_save(dsync, session);
  return TRUE;
}

static void
infinoted_directory_sync_remove_session(InfinotedDirectorySync* dsync,
                                        InfinotedDirectorySyncSession* sess)
{
  InfTextBuffer* buffer;

  if(sess->timeout != NULL)
  {
    infinoted_directory_sync_session_save(dsync, sess);
    infinoted_directory_sync_session_stop(dsync, sess);
  }

  buffer = INF_TEXT_BUFFER(
    inf_session_get_buffer(infd_session_proxy_get_session(sess->proxy))
  );

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(buffer),
    G_CALLBACK(infinoted_directory_sync_buffer_text_inserted_cb),
    sess
  );

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(buffer),
    G_CALLBACK(infinoted_directory_sync_buffer_text_erased_cb),
    sess
  );

  dsync->sessions = g_slist_remove(dsync->sessions, sess);

  g_free(sess->path);
  g_slice_free(InfinotedDirectorySyncSession, sess);
}

static void
infinoted_directory_sync_directory_add_session_cb(InfdDirectory* directory,
                                                  InfdDirectoryIter* iter,
                                                  InfdSessionProxy* proxy,
                                                  gpointer user_data)
{
  InfinotedDirectorySync* dsync;
  GError* error;
  gchar* path;

  dsync = (InfinotedDirectorySync*)user_data;
  error = NULL;
  if(!infinoted_directory_sync_add_session(dsync, iter, &error))
  {
    path = infd_directory_iter_get_path(directory, iter);

    infinoted_util_log_warning(
      _("Failed to synchronize session \"%s\" to disk: %s"),
      path,
      error->message
    );

    g_free(path);
    g_error_free(error);
  }
}

static void
infinoted_directory_sync_directory_remove_session_cb(InfdDirectory* directory,
                                                     InfdDirectoryIter* iter,
                                                     InfdSessionProxy* proxy,
                                                     gpointer user_data)
{
  InfinotedDirectorySync* dsync;
  InfinotedDirectorySyncSession* session;

  /* Ignore if this is not a text session */
  if(INF_TEXT_IS_SESSION(infd_session_proxy_get_session(proxy)))
  {
    dsync = (InfinotedDirectorySync*)user_data;
    session = infinoted_directory_sync_find_session(dsync, iter);
    g_assert(session != NULL && session->proxy == proxy);

    infinoted_directory_sync_remove_session(dsync, session);
  }
}

static void
infinoted_directory_sync_walk_directory(InfinotedDirectorySync* dsync,
                                        InfdDirectoryIter* iter)
{
  InfdDirectoryIter child;
  InfdSessionProxy* session;
  GError* error;
  gchar* path;

  if(infd_directory_iter_get_node_type(dsync->directory, iter) ==
     INFD_STORAGE_NODE_SUBDIRECTORY)
  {
    if(infd_directory_iter_get_explored(dsync->directory, iter) == TRUE)
    {
      /* Errors can't happen as the directory is already explored */
      child = *iter;
      if(infd_directory_iter_get_child(dsync->directory, &child, NULL))
      {
        do
        {
          infinoted_directory_sync_walk_directory(dsync, &child);
        } while(infd_directory_iter_get_next(dsync->directory, &child));
      }
    }
  }
  else
  {
    session = infd_directory_iter_peek_session(dsync->directory, iter);
    if(session != NULL)
    {
      error = NULL;
      if(!infinoted_directory_sync_add_session(dsync, iter, &error))
      {
        path = infd_directory_iter_get_path(dsync->directory, iter);

        infinoted_util_log_warning(
          _("Failed to synchronize session \"%s\" to disk: %s"),
          path,
          error->message
        );

        g_free(path);
        g_error_free(error);
      }
    }
  }
}

/**
 * infinoted_directory_sync_new:
 * @directory: A #InfdDirectory.
 * @sync_directory: The directory on the file system to sync documents to.
 * @sync_interval: The interval in which to save documents to @sync_directory,
 * in seconds.
 *
 * Creates a new #InfinotedDirectorySync object which will save all documents
 * in @directory every @sync_interval into the file system at @sync_directory.
 * If @sync_directory does not exist it will be created.
 *
 * Returns: A new #InfinotedDirectorySync.
 */
InfinotedDirectorySync*
infinoted_directory_sync_new(InfdDirectory* directory,
                             const gchar* sync_directory,
                             unsigned int sync_interval)
{
  InfinotedDirectorySync* dsync;
  InfdDirectoryIter iter;

  dsync = g_slice_new(InfinotedDirectorySync);

  dsync->directory = directory;
  dsync->sync_directory = g_strdup(sync_directory);
  dsync->sync_interval = sync_interval;
  dsync->sessions = NULL;
  g_object_ref(directory);

  g_signal_connect_after(
    G_OBJECT(directory),
    "add-session",
    G_CALLBACK(infinoted_directory_sync_directory_add_session_cb),
    dsync
  );

  g_signal_connect_after(
    G_OBJECT(directory),
    "remove-session",
    G_CALLBACK(infinoted_directory_sync_directory_remove_session_cb),
    dsync
  );

  infd_directory_iter_get_root(directory, &iter);
  infinoted_directory_sync_walk_directory(dsync, &iter);
  return dsync;
}

/**
 * infinoted_directory_sync_free:
 * @directory_sync: A #InfinotedDirectorySync.
 *
 * Frees the given #InfinotedDirectorySync. 
 */
void
infinoted_directory_sync_free(InfinotedDirectorySync* dsync)
{
  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(dsync->directory),
    G_CALLBACK(infinoted_directory_sync_directory_add_session_cb),
    dsync
  );

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(dsync->directory),
    G_CALLBACK(infinoted_directory_sync_directory_remove_session_cb),
    dsync
  );

  while(dsync->sessions != NULL)
  {
    infinoted_directory_sync_remove_session(
      dsync,
      (InfinotedDirectorySyncSession*)dsync->sessions->data
    );
  }

  g_object_unref(dsync->directory);
  g_slice_free(InfinotedDirectorySync, dsync);
}

/* vim:set et sw=2 ts=2: */
