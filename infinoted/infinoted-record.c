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

#include <infinoted/infinoted-record.h>

#include <libinfinity/adopted/inf-adopted-session.h>
#include <libinfinity/inf-i18n.h>
#include <libinfinity/inf-signals.h>

#include <string.h>
#include <errno.h>

static InfAdoptedSessionRecord*
infinoted_record_start_real(InfAdoptedSession* session,
                            const gchar* filename,
                            const gchar* title)
{
  GError* error;
  InfAdoptedSessionRecord* record;

  record = inf_adopted_session_record_new(session);
  error = NULL;

  inf_adopted_session_record_start_recording(record, filename, &error);

  if(error != NULL)
  {
    g_warning(_("Error while writing record for session "
                "\"%s\" into \"%s\": %s"),
              title, filename, error->message);
    g_error_free(error);
    g_object_unref(record);
    return NULL;
  }
  else
  {
    g_object_set_data(G_OBJECT(session), "infinoted-record", record);
    return record;
  }
}

static InfAdoptedSessionRecord*
infinoted_record_start(InfAdoptedSession* session,
                       const gchar* title)
{
  gchar* dirname;
  gchar* basename;
  gchar* filename;
  guint i;
  gsize pos;
  InfAdoptedSessionRecord* record;

  dirname = g_build_filename(g_get_home_dir(), ".infinoted-records", NULL);
  basename = g_build_filename(dirname, title, NULL);

  pos = strlen(basename) + 8;
  filename = g_strdup_printf("%s.record-00000.xml", basename);
  g_free(basename);

  i = 0;
  while(g_file_test(filename, G_FILE_TEST_EXISTS) && ++i < 100000)
  {
    g_snprintf(filename + pos, 10, "%05u.xml", i);
  }

  record = NULL;
  if(i >= 100000)
  {
    g_warning(
      _("Could not create record file for session \"%s\": Could not generate "
        "unused record file in directory \"%s\""),
      title,
      dirname
    );
  }
  else
  {
    /* TODO: Use GetLastError() on Win32 */
    if(g_mkdir_with_parents(dirname, 0700) == -1)
    {
      g_warning(
        _("Could not create record file directory \"%s\": %s"),
        strerror(errno)
      );
    }
    else
    {
      record = infinoted_record_start_real(session, filename, title);
    }
  }

  g_free(filename);
  g_free(dirname);

  return record;
}

static void
infinoted_record_directory_subscribe_session_cb(InfBrowser* browser,
                                                const InfBrowserIter* iter,
                                                InfSessionProxy* proxy,
                                                gpointer user_data)
{
  InfinotedRecord* record;
  InfSession* session;
  const gchar* title;
  InfAdoptedSessionRecord* rec;

  record = (InfinotedRecord*)user_data;
  g_object_get(G_OBJECT(proxy), "session", &session, NULL);

  if(INF_ADOPTED_IS_SESSION(session))
  {
    title = inf_browser_get_node_name(browser, iter);

    rec = infinoted_record_start(INF_ADOPTED_SESSION(session), title);
    if(rec)
      record->records = g_slist_prepend(record->records, rec);
  }

  g_object_unref(session);
}

static void
infinoted_record_directory_unsubscribe_session_cb(InfBrowser* browser,
                                                  const InfBrowserIter* iter,
                                                  InfSessionProxy* proxy,
                                                  gpointer user_data)
{
  InfinotedRecord* record;
  InfSession* session;
  GSList* item;

  InfAdoptedSessionRecord* rec;
  InfSession* cur_session;

  g_object_get(G_OBJECT(proxy), "session", &session, NULL);
  record = (InfinotedRecord*)user_data;

  for(item = record->records; item != NULL; item = item->next)
  {
    rec = INF_ADOPTED_SESSION_RECORD(item->data);
    g_object_get(G_OBJECT(rec), "session", &cur_session, NULL);
    if(session == cur_session)
    {
      record->records = g_slist_remove(record->records, rec);
      g_object_set_data(G_OBJECT(session), "infinoted-record", NULL);
      g_object_unref(cur_session);
      g_object_unref(rec);
      break;
    }
    else
    {
      g_object_unref(cur_session);
    }
  }

  g_object_unref(session);
}

/**
 * infinoted_record_new:
 * @directory: The directory for whose sessions to create records.
 *
 * Crates records for all sessions of @directory. They are stored into
 * .infinoted-records/ in the home directory of the executing user.
 *
 * Returns: A new #InfinotedRecord. Free with infinoted_record_free() to stop
 * recording.
 */
InfinotedRecord*
infinoted_record_new(InfdDirectory* directory)
{
  InfinotedRecord* record;

  record = g_slice_new(InfinotedRecord);
  record->directory = directory;
  record->records = NULL;
  g_object_ref(directory);

  g_signal_connect(
    G_OBJECT(record->directory),
    "subscribe-session",
    G_CALLBACK(infinoted_record_directory_subscribe_session_cb),
    record
  );

  g_signal_connect(
    G_OBJECT(record->directory),
    "unsubscribe-session",
    G_CALLBACK(infinoted_record_directory_unsubscribe_session_cb),
    record
  );

  return record;
}

/**
 * infinoted_record_free:
 * @record: A #InfinotedRecord.
 *
 * Frees the given #InfinotedRecord. This stops all recordings currently in
 * progress, if any.
 */
void
infinoted_record_free(InfinotedRecord* record)
{
  GSList* item;
  InfAdoptedSessionRecord* rec;

  inf_signal_handlers_disconnect_by_func(
    record->directory,
    G_CALLBACK(infinoted_record_directory_subscribe_session_cb),
    record
  );

  inf_signal_handlers_disconnect_by_func(
    record->directory,
    G_CALLBACK(infinoted_record_directory_unsubscribe_session_cb),
    record
  );

  for(item = record->records; item != NULL; item = item->next)
  {
    rec = INF_ADOPTED_SESSION_RECORD(item->data);
    g_object_unref(rec);
  }

  g_slist_free(record->records);
  g_object_unref(record->directory);
  g_slice_free(InfinotedRecord, record);
}

/**
 * infinoted_record_get_for_session:
 * @record: A #InfinotedRecord.
 * @session: A #InfAdoptedSession from @record's directory.
 *
 * Returns the #InfAdoptedSessionRecord with which @session is being recorded.
 *
 * Returns: The #InfAdoptedSessionRecord for @session.
 */
InfAdoptedSessionRecord*
infinoted_record_get_for_session(InfinotedRecord* record,
                                 InfAdoptedSession* session)
{
  GSList* item;
  InfAdoptedSessionRecord* rec;
  InfAdoptedSession* rec_session;

  for(item = record->records; item != NULL; item = item->next)
  {
    rec = INF_ADOPTED_SESSION_RECORD(item->data);
    g_object_get(G_OBJECT(rec), "session", &rec_session, NULL);
    if(rec_session == session)
    {
      g_object_unref(rec_session);
      return rec;
    }

    g_object_unref(rec_session);
  }
}

/* vim:set et sw=2 ts=2: */
