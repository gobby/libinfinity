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

#include <infinoted/infinoted-traffic-logger.h>

#include <libxml/xmlsave.h>

#include <string.h>
#include <errno.h>

typedef struct _InfinotedTrafficLoggerConnection
  InfinotedTrafficLoggerConnection;
struct _InfinotedTrafficLoggerConnection {
  InfXmlConnection* connection;
  gchar* filename;
  FILE* file;
};

static void
infinoted_traffic_logger_write(InfinotedTrafficLoggerConnection* tconn,
                               const gchar* fmt,
                               ...)
{
  time_t cur_time;
  struct tm* cur_tm;
  char time_msg[128];
  va_list arglist;

  if(tconn->file != NULL)
  {
    cur_time = time(NULL);
    cur_tm = localtime(&cur_time);
    strftime(time_msg, 128, "[%c] ", cur_tm);
    fputs(time_msg, tconn->file);

    va_start(arglist, fmt);
    vfprintf(tconn->file, fmt, arglist);
    va_end(arglist);
    fputc('\n', tconn->file);

    fflush(tconn->file);
  }
}

static void
infinoted_traffic_logger_received_cb(InfXmlConnection* conn,
                                     xmlNodePtr xml,
                                     gpointer user_data)
{
  InfinotedTrafficLoggerConnection* tconn;
  xmlBufferPtr buffer;
  xmlSaveCtxtPtr ctx;

  tconn = (InfinotedTrafficLoggerConnection*)user_data;
  buffer = xmlBufferCreate();
  ctx = xmlSaveToBuffer(buffer, "UTF-8", 0);
  xmlSaveTree(ctx, xml);
  xmlSaveClose(ctx);

  infinoted_traffic_logger_write(
    tconn,
    "<<< %s",
    (const gchar*)xmlBufferContent(buffer)
  );

  xmlBufferFree(buffer);
}

static void
infinoted_traffic_logger_sent_cb(InfXmlConnection* conn,
                                 xmlNodePtr xml,
                                 gpointer user_data)
{
  InfinotedTrafficLoggerConnection* tconn;
  xmlBufferPtr buffer;
  xmlSaveCtxtPtr ctx;

  tconn = (InfinotedTrafficLoggerConnection*)user_data;
  buffer = xmlBufferCreate();
  ctx = xmlSaveToBuffer(buffer, "UTF-8", 0);
  xmlSaveTree(ctx, xml);
  xmlSaveClose(ctx);

  infinoted_traffic_logger_write(
    tconn,
    ">>> %s",
    (const gchar*)xmlBufferContent(buffer)
  );

  xmlBufferFree(buffer);
}

static void
infinoted_traffic_logger_error_cb(InfXmlConnection* conn,
                                  const GError* error,
                                  gpointer user_data)
{
  InfinotedTrafficLoggerConnection* tconn;
  tconn = (InfinotedTrafficLoggerConnection*)user_data;

  infinoted_traffic_logger_write(
    tconn,
    "!!! Connection error: %s",
    error->message
  );
}

static void
infinoted_traffic_logger_add_connection(InfinotedTrafficLogger* logger,
                                        InfXmlConnection* conn)
{
  InfinotedTrafficLoggerConnection* tconn;
  gchar* remote_id;
  gchar* basename;
  gchar* c;

  tconn = g_slice_new(InfinotedTrafficLoggerConnection);
  tconn->connection = conn;
  g_object_ref(conn);

  g_object_get(G_OBJECT(conn), "remote-id", &remote_id, NULL);

  basename = g_strdup(remote_id);
  for(c = basename; *c != '\0'; ++c)
    if(*c == '[' || *c == ']')
      *c = '_';
  tconn->filename = g_build_filename(logger->path, basename, NULL);
  g_free(basename);

  tconn->file = fopen(tconn->filename, "a");

  if(tconn->file == NULL)
  {
    infinoted_log_warning(
      logger->log,
      "Failed to open file \"%s\": %s\nTraffic logging for connection %s is disabled.",
      tconn->filename,
      strerror(errno),
      remote_id
    );
  }
  else
  {
    infinoted_traffic_logger_write(tconn, "!!! %s Connected", remote_id);
  }

  g_signal_connect(
    G_OBJECT(conn),
    "received",
    G_CALLBACK(infinoted_traffic_logger_received_cb),
    tconn
  );

  g_signal_connect(
    G_OBJECT(conn),
    "sent",
    G_CALLBACK(infinoted_traffic_logger_sent_cb),
    tconn
  );

  g_signal_connect(
    G_OBJECT(conn),
    "error",
    G_CALLBACK(infinoted_traffic_logger_error_cb),
    tconn
  );

  logger->connections = g_slist_prepend(logger->connections, tconn);
  g_free(remote_id);
}

static void
infinoted_traffic_logger_remove_connection(InfinotedTrafficLogger* logger,
                                           InfXmlConnection* conn)
{
  GSList* item;
  InfinotedTrafficLoggerConnection* tconn;
  for(item = logger->connections; item != NULL; item = item->next)
  {
    tconn = (InfinotedTrafficLoggerConnection*)item->data;
    if(tconn->connection == conn)
    {
      inf_signal_handlers_disconnect_by_func(
        G_OBJECT(conn),
        G_CALLBACK(infinoted_traffic_logger_received_cb),
        tconn
      );

      inf_signal_handlers_disconnect_by_func(
        G_OBJECT(conn),
        G_CALLBACK(infinoted_traffic_logger_sent_cb),
        tconn
      );

      inf_signal_handlers_disconnect_by_func(
        G_OBJECT(conn),
        G_CALLBACK(infinoted_traffic_logger_error_cb),
        tconn
      );

      infinoted_traffic_logger_write(tconn, "!!! Log closed");

      if(tconn->file != NULL)
      {
        if(fclose(tconn->file) == -1)
        {
          infinoted_log_warning(
            logger->log,
            "Failed to close file \"%s\": %s",
            tconn->filename, strerror(errno)
          );
        }
      }

      g_free(tconn->filename);
      g_object_unref(tconn->connection);
      g_slice_free(InfinotedTrafficLoggerConnection, tconn);
      logger->connections = g_slist_remove(logger->connections, tconn);
      break;
    }
  }

  g_assert(item != NULL);
}

static void
infinoted_traffic_logger_new_foreach_connection_cb(InfXmlConnection* conn,
                                                   gpointer user_data)
{
  InfinotedTrafficLogger* logger;
  logger = (InfinotedTrafficLogger*)user_data;

  infinoted_traffic_logger_add_connection(logger, conn);
}

static void
infinoted_traffic_logger_connection_added_cb(InfdDirectory* directory,
                                             InfXmlConnection* conn,
                                             gpointer user_data)
{
  InfinotedTrafficLogger* logger;
  logger = (InfinotedTrafficLogger*)user_data;

  infinoted_traffic_logger_add_connection(logger, conn);
}

static void
infinoted_traffic_logger_connection_removed_cb(InfdDirectory* directory,
                                               InfXmlConnection* conn,
                                               gpointer user_data)
{
  InfinotedTrafficLogger* logger;
  logger = (InfinotedTrafficLogger*)user_data;

  infinoted_traffic_logger_remove_connection(logger, conn);
}

/**
 * infinoted_traffic_logger_new:
 * @directory: A #InfdDirectory.
 * @log: A #InfinotedLog used for logging warning messages.
 * @path: The path at which to store traffic logs.
 *
 * Creates a new #InfinotedTrafficLogger. This object logs all traffic of all
 * connections in @directory, to aid in debugging.
 *
 * Returns: A new #InfinotedTrafficLogger.
 */
InfinotedTrafficLogger*
infinoted_traffic_logger_new(InfdDirectory* directory,
                             InfinotedLog* log,
                             const gchar* path)
{
  InfinotedTrafficLogger* logger;
  logger = g_slice_new(InfinotedTrafficLogger);

  logger->directory = directory;
  logger->log = log;
  logger->path = g_strdup(path);
  logger->connections = NULL;
  g_object_ref(directory);

  infd_directory_foreach_connection(
    directory,
    infinoted_traffic_logger_new_foreach_connection_cb,
    logger
  );

  g_signal_connect(
    G_OBJECT(directory),
    "connection-added",
    G_CALLBACK(infinoted_traffic_logger_connection_added_cb),
    logger
  );

  g_signal_connect(
    G_OBJECT(directory),
    "connection-removed",
    G_CALLBACK(infinoted_traffic_logger_connection_removed_cb),
    logger
  );

  return logger;
}

/**
 * infinoted_traffic_logger_free:
 * @logger: A #InfinotedTrafficLogger.
 *
 * Frees @logger so that it can no longer be used. Allocated resources
 * are returned to the system and no more traffic logging will be performed.
 */
void
infinoted_traffic_logger_free(InfinotedTrafficLogger* logger)
{
  InfinotedTrafficLoggerConnection* tconn;

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(logger->directory),
    G_CALLBACK(infinoted_traffic_logger_connection_added_cb),
    logger
  );

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(logger->directory),
    G_CALLBACK(infinoted_traffic_logger_connection_removed_cb),
    logger
  );

  while(logger->connections != NULL)
  {
    tconn = (InfinotedTrafficLoggerConnection*)logger->connections->data;
    infinoted_traffic_logger_remove_connection(logger, tconn->connection);
  }
 
  g_free(logger->path);
  g_object_unref(logger->directory);
  g_slice_free(InfinotedTrafficLogger, logger);
}

/* vim:set et sw=2 ts=2: */
