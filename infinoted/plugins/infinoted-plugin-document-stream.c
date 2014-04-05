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

#include "util/infinoted-plugin-util-navigate-browser.h"

#include <infinoted/infinoted-plugin-manager.h>
#include <infinoted/infinoted-parameter.h>
#include <infinoted/infinoted-log.h>

#include <libinftext/inf-text-buffer.h>

#include <libinfinity/inf-i18n.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <fcntl.h>
#include <string.h>
#include <errno.h>

typedef enum _InfinotedPluginDocumentStreamStatus {
  INFINOTED_PLUGIN_DOCUMENT_STREAM_NORMAL,
  INFINOTED_PLUGIN_DOCUMENT_STREAM_RECEIVING,
  INFINOTED_PLUGIN_DOCUMENT_STREAM_CLOSED
} InfinotedPluginDocumentStreamStatus;

typedef struct _InfinotedPluginDocumentStream InfinotedPluginDocumentStream;
struct _InfinotedPluginDocumentStream {
  InfinotedPluginManager* manager;
  InfNativeSocket socket;
  InfIoWatch* watch;
  GSList* streams;
};

typedef struct _InfinotedPluginDocumentStreamQueue
  InfinotedPluginDocumentStreamQueue;
struct _InfinotedPluginDocumentStreamQueue {
  gchar* data;
  gsize pos;
  gsize len;
  gsize alloc;
};

typedef struct _InfinotedPluginDocumentStreamStream
  InfinotedPluginDocumentStreamStream;
struct _InfinotedPluginDocumentStreamStream {
  InfinotedPluginDocumentStream* plugin;
  InfNativeSocket socket;
  InfIoWatch* watch;

  InfinotedPluginDocumentStreamStatus status;
  InfinotedPluginDocumentStreamQueue send_queue;
  InfinotedPluginDocumentStreamQueue recv_queue;

  /* set if either subscribe_request or proxy are set */
  InfBrowserIter iter;

  InfinotedPluginUtilNavigateData* navigate_handle;
  InfRequest* subscribe_request;
  InfSessionProxy* proxy;
  InfBuffer* buffer;
};

static void
infinoted_plugin_document_stream_queue_initialize(
  InfinotedPluginDocumentStreamQueue* queue)
{
  queue->data = NULL;
  queue->pos = 0;
  queue->len = 0;
  queue->alloc = 0;
}

static void
infinoted_plugin_document_stream_queue_finalize(
  InfinotedPluginDocumentStreamQueue* queue)
{
  g_free(queue->data);
}

static void
infinoted_plugin_document_stream_queue_reserve(
  InfinotedPluginDocumentStreamQueue* queue,
  gsize len)
{
  if(queue->pos + queue->len + len > queue->alloc)
  {
    /* Need to make new space in the queue */
    if(queue->pos != 0)
    {
      /* First, try by moving contents to front */
      g_assert(queue->len > 0);

      g_memmove(
        queue->data,
        queue->data + queue->pos,
        queue->len
      );

      queue->pos = 0;
    }

    if(queue->len + len > queue->alloc)
    {
      queue->alloc = queue->len + len;
      queue->data = g_realloc(queue->data, queue->alloc);
    }
  }
}

static void
infinoted_plugin_document_stream_queue_append(
  InfinotedPluginDocumentStreamQueue* queue,
  const gchar* data,
  gsize len)
{
  infinoted_plugin_document_stream_queue_reserve(queue, len);

  g_assert(queue->len + len <= queue->alloc);
  memcpy(queue->data + queue->pos + queue->len, data, len);
  queue->len += len;
}

static void
infinoted_plugin_document_stream_queue_consume(
  InfinotedPluginDocumentStreamQueue* queue,
  gsize len)
{
  g_assert(len <= queue->len);
  queue->pos += len;
  queue->len -= len;

  if(queue->len == 0)
    queue->pos = 0;
}

static void
infinoted_plugin_document_stream_make_system_error(int code,
                                                   GError** error)
{
  g_set_error(
    error,
    g_quark_from_static_string("INFINOTED_PLUGIN_DOCUMENT_STREAM_SYSTEM_ERROR"),
    code,
    "%s",
    strerror(code)
  );
}

static void
infinoted_plugin_document_stream_subscribe_func(InfRequest* request,
                                                const InfRequestResult* res,
                                                const GError* error,
                                                gpointer user_data);

static void
infinoted_plugin_document_stream_close_stream(
  InfinotedPluginDocumentStreamStream* stream);

static gboolean
infinoted_plugin_document_stream_send(
  InfinotedPluginDocumentStreamStream* stream,
  const void* data,
  gsize len);

static void
infinoted_plugin_document_stream_send_error(
  InfinotedPluginDocumentStreamStream* stream,
  const gchar* message)
{
  guint32 errcom;
  guint16 errlen;

  errcom = 0;
  errlen = strlen(message);

  if(!infinoted_plugin_document_stream_send(stream, &errcom, 4)) return;
  if(!infinoted_plugin_document_stream_send(stream, &errlen, 2)) return;
  if(!infinoted_plugin_document_stream_send(stream, message, errlen)) return;
}

static void
infinoted_plugin_document_stream_text_inserted_cb(InfTextBuffer* buffer,
                                                  guint pos,
                                                  InfTextChunk* chunk,
                                                  InfUser* user,
                                                  gpointer user_data)
{
  InfinotedPluginDocumentStreamStream* stream;
  guint32 comm;
  guint32 pos32;
  gsize bytes;
  guint32 bytes32;
  gpointer text;
  gboolean alive;

  stream = (InfinotedPluginDocumentStreamStream*)user_data;
  text = inf_text_chunk_get_text(chunk, &bytes);

  comm = 3; /* INSERT */
  pos32 = (guint32)pos;
  bytes32 = (guint32)bytes;

  alive = infinoted_plugin_document_stream_send(stream, &comm, 4);
  if(alive)
    alive = infinoted_plugin_document_stream_send(stream, &pos32, 4);
  if(alive)
    alive = infinoted_plugin_document_stream_send(stream, &bytes32, 4);
  if(alive)
    alive = infinoted_plugin_document_stream_send(stream, text, bytes);

  g_free(text);
}

static void
infinoted_plugin_document_stream_text_erased_cb(InfTextBuffer* buffer,
                                                guint pos,
                                                InfTextChunk* chunk,
                                                InfUser* user,
                                                gpointer user_data)
{
  InfinotedPluginDocumentStreamStream* stream;
  guint32 comm;
  guint32 pos32;
  guint32 len32;
  gboolean alive;

  stream = (InfinotedPluginDocumentStreamStream*)user_data;

  comm = 4; /* ERASE */
  pos32 = (guint32)pos;
  len32 = inf_text_chunk_get_length(chunk);

  alive = infinoted_plugin_document_stream_send(stream, &comm, 4);
  if(alive)
    alive = infinoted_plugin_document_stream_send(stream, &pos32, 4);
  if(alive)
    alive = infinoted_plugin_document_stream_send(stream, &len32, 4);
}

static void
infinoted_plugin_document_stream_sync(
  InfinotedPluginDocumentStreamStream* stream)
{
  InfTextBuffer* buffer;
  InfTextBufferIter* iter;
  gpointer text;
  guint32 comm;
  guint32 len;
  gboolean alive;

  buffer = INF_TEXT_BUFFER(stream->buffer);
  iter = inf_text_buffer_create_begin_iter(buffer);
  alive = TRUE;

  if(iter != NULL)
  {
    do
    {
      comm = 1; /* SYNC */
      len = inf_text_buffer_iter_get_bytes(buffer, iter);

      alive = infinoted_plugin_document_stream_send(stream, &comm, 4);
      if(!alive) break;

      alive = infinoted_plugin_document_stream_send(stream, &len, 4);
      if(!alive) break;

      text = inf_text_buffer_iter_get_text(buffer, iter);
      alive = infinoted_plugin_document_stream_send(stream, text, len);
      g_free(text);
      if(!alive) break;
    } while(inf_text_buffer_iter_next(buffer, iter));

    inf_text_buffer_destroy_iter(buffer, iter);
  }

  if(alive)
  {
    comm = 2; /* SYNC DONE */
    alive = infinoted_plugin_document_stream_send(stream, &comm, 4);
  }
}

static void
infinoted_plugin_document_stream_start(
  InfinotedPluginDocumentStreamStream* stream,
  InfSessionProxy* proxy)
{
  InfSession* session;
  InfBuffer* buffer;

  stream->proxy = proxy;
  g_object_ref(proxy);

  g_object_get(G_OBJECT(proxy), "session", &session, NULL);

  buffer = inf_session_get_buffer(session);
  stream->buffer = buffer;
  g_object_ref(buffer);

  infinoted_plugin_document_stream_sync(stream);

  g_signal_connect(
    G_OBJECT(buffer),
    "text-inserted",
    G_CALLBACK(infinoted_plugin_document_stream_text_inserted_cb),
    stream
  );

  g_signal_connect(
    G_OBJECT(buffer),
    "text-erased",
    G_CALLBACK(infinoted_plugin_document_stream_text_erased_cb),
    stream
  );

  g_object_unref(session);
}

static void
infinoted_plugin_document_stream_stop(
  InfinotedPluginDocumentStreamStream* stream,
  gboolean send_stop)
{
  guint32 comm;
  if(send_stop)
  {
    comm = 5; /* STOP */
    if(!infinoted_plugin_document_stream_send(stream, &comm, 4))
      return;
  }

  if(stream->buffer != NULL)
  {
    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(stream->buffer),
      G_CALLBACK(infinoted_plugin_document_stream_text_inserted_cb),
      stream
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(stream->buffer),
      G_CALLBACK(infinoted_plugin_document_stream_text_erased_cb),
      stream
    );

    g_object_unref(stream->buffer);
    stream->buffer = NULL;
  }

  if(stream->proxy != NULL)
  {
    g_object_unref(stream->proxy);
    stream->proxy = NULL;
  }

  if(stream->subscribe_request != NULL)
  {
    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(stream->subscribe_request),
      G_CALLBACK(infinoted_plugin_document_stream_subscribe_func),
      stream
    );

    stream->subscribe_request = NULL;
  }
}

static void
infinoted_plugin_document_stream_subscribe_func(InfRequest* request,
                                                const InfRequestResult* res,
                                                const GError* error,
                                                gpointer user_data)
{
  InfinotedPluginDocumentStreamStream* stream;
  InfSessionProxy* proxy;

  stream = (InfinotedPluginDocumentStreamStream*)user_data;
  stream->subscribe_request = NULL;

  if(error != NULL)
  {
    infinoted_plugin_document_stream_send_error(stream, error->message);
  }
  else
  {
    inf_request_result_get_subscribe_session(res, NULL, NULL, &proxy);
    infinoted_plugin_document_stream_start(stream, proxy);
  }
}

static void
infinoted_plugin_document_stream_navigate_func(InfBrowser* browser,
                                               const InfBrowserIter* iter,
                                               const GError* error,
                                               gpointer user_data)
{
  InfinotedPluginDocumentStreamStream* stream;
  InfSessionProxy* proxy;
  InfRequest* request;

  stream = (InfinotedPluginDocumentStreamStream*)user_data;
  stream->navigate_handle = NULL;

  if(error != NULL)
  {
    infinoted_plugin_document_stream_send_error(stream, error->message);
  }
  else
  {
    /* TODO: Support chat nodes, too */
    if(inf_browser_is_subdirectory(browser, iter) || 
       strcmp(inf_browser_get_node_type(browser, iter), "InfText") != 0)
    {
      infinoted_plugin_document_stream_send_error(stream, "Not a text node");
    }
    else
    {
      stream->iter = *iter;
      proxy = inf_browser_get_session(browser, iter);
      if(proxy != NULL)
      {
        infinoted_plugin_document_stream_start(stream, proxy);
      }
      else
      {
        request = inf_browser_get_pending_request(
          browser,
          iter,
          "subscribe-session"
        );

        if(request != NULL)
        {
          g_signal_connect(
            G_OBJECT(browser),
            "finished",
            G_CALLBACK(infinoted_plugin_document_stream_subscribe_func),
            stream
          );
        }
        else
        {
          request = inf_browser_subscribe(
            browser,
            iter,
            infinoted_plugin_document_stream_subscribe_func,
            stream
          );
        }

        stream->subscribe_request = request;
      }
    }
  }
}

static gboolean
infinoted_plugin_document_stream_process_get_document(
  InfinotedPluginDocumentStreamStream* stream,
  const gchar** data,
  gsize* len)
{
  guint16 doc_len;
  const gchar* doc_name;

  /* get size of document string */
  if(*len < 2) return FALSE;
  doc_len = *(guint16*)(*data);
  *data += 2; *len -= 2;

  /* get document string */
  if(*len < doc_len) return FALSE;
  doc_name = *data;
  *data += doc_len; *len -= doc_len;

  /* quit connection if we already have a buffer */
  if(stream->buffer != NULL)
  {
    infinoted_plugin_document_stream_send_error(
      stream,
      "Stream is already open"
    );
  }
  else
  {
    stream->navigate_handle = infinoted_plugin_util_navigate_to(
      INF_BROWSER(
        infinoted_plugin_manager_get_directory(stream->plugin->manager)
      ),
      doc_name,
      doc_len,
      infinoted_plugin_document_stream_navigate_func,
      stream
    );
  }

  return TRUE;
}

static gboolean
infinoted_plugin_document_stream_process(
  InfinotedPluginDocumentStreamStream* stream,
  const gchar** data,
  gsize* len)
{
  guint32 command;

  /* Get message */
  if(*len < 4) return FALSE;
  command = *(guint32*)(*data);
  *data += 4; *len -= 4;

  switch(command)
  {
  case 0: /* get document */
    return infinoted_plugin_document_stream_process_get_document(
      stream,
      data,
      len
    );
  default:
    /* unrecognized command; don't know how to proceed, so disconnect */
    infinoted_plugin_document_stream_close_stream(stream);
    return FALSE;
  }
}

static void
infinoted_plugin_document_stream_received(
  InfinotedPluginDocumentStreamStream* stream,
  gsize new_pos,
  gsize new_len)
{
  gsize prev_queue_len;
  const gchar* data;
  gsize len;

  g_assert(stream->status == INFINOTED_PLUGIN_DOCUMENT_STREAM_RECEIVING);

  prev_queue_len = 0;
  while(stream->status == INFINOTED_PLUGIN_DOCUMENT_STREAM_RECEIVING &&
        stream->recv_queue.len > 0 &&
        (prev_queue_len == 0 || stream->recv_queue.len < prev_queue_len))
  {
    prev_queue_len = stream->recv_queue.len;

    data = stream->recv_queue.data;
    len = stream->recv_queue.len;

    if(infinoted_plugin_document_stream_process(stream, &data, &len))
    {
      if(stream->status == INFINOTED_PLUGIN_DOCUMENT_STREAM_RECEIVING)
      {
        infinoted_plugin_document_stream_queue_consume(
          &stream->recv_queue,
          stream->recv_queue.len - len
        );
      }
    }
  }
}

static gsize
infinoted_plugin_document_stream_send_direct(
  InfinotedPluginDocumentStreamStream* stream,
  const gchar* data,
  gsize len,
  GError** error)
{
  int errcode;
  ssize_t bytes;
  gsize sent;

  sent = 0;

  g_assert(stream->status != INFINOTED_PLUGIN_DOCUMENT_STREAM_CLOSED);

  do
  {
    bytes = send(stream->socket, data, len, MSG_NOSIGNAL);
    errcode = errno;

    if(bytes > 0)
    {
      g_assert(bytes <= len);

      sent += bytes;
      data += bytes;
      len -= bytes;
    }
  } while(len > 0 && (bytes > 0 || (bytes < 0 && errcode == EINTR)));

  if(bytes == 0)
    return 0;

  if(bytes < 0 && errno != EAGAIN)
  {
    infinoted_plugin_document_stream_make_system_error(errno, error);
    return 0;
  }

  return sent;
}

static gboolean
infinoted_plugin_document_stream_send(
  InfinotedPluginDocumentStreamStream* stream,
  const void* data,
  gsize len)
{
  GError* error;
  gsize sent;

  if(stream->send_queue.len > 0)
  {
    infinoted_plugin_document_stream_queue_append(
      &stream->send_queue,
      data,
      len
    );

    return TRUE;
  }
  else
  {
    error = NULL;
    sent = infinoted_plugin_document_stream_send_direct(
      stream,
      data,
      len,
      &error
    );

    if(error != NULL)
    {
      infinoted_log_warning(
        infinoted_plugin_manager_get_log(stream->plugin->manager),
        "Document stream error: %s",
        error->message
      );

      g_error_free(error);
      return FALSE;
    }
    else
    {
      if(sent < len)
      {
        infinoted_plugin_document_stream_queue_append(
          &stream->send_queue,
          (const gchar*)data + sent,
          len - sent
        );

        inf_io_update_watch(
          infinoted_plugin_manager_get_io(stream->plugin->manager),
          stream->watch,
          INF_IO_INCOMING | INF_IO_OUTGOING
        );
      }

      return TRUE;
    }
  }
}

static gboolean
infinoted_plugin_document_stream_io_in(
  InfinotedPluginDocumentStreamStream* stream,
  GError** error)
{
  int errcode;
  ssize_t bytes;
  gsize queue_offset;

  g_assert(stream->status == INFINOTED_PLUGIN_DOCUMENT_STREAM_NORMAL);
  stream->status = INFINOTED_PLUGIN_DOCUMENT_STREAM_RECEIVING;

  do
  {
    infinoted_plugin_document_stream_queue_reserve(&stream->recv_queue, 4096);
    queue_offset = stream->recv_queue.pos + stream->recv_queue.len;

    bytes = recv(
      stream->socket,
      stream->recv_queue.data + queue_offset,
      stream->recv_queue.alloc - queue_offset,
      MSG_NOSIGNAL
    );

    errcode = errno;
    if(bytes > 0)
    {
      queue_offset = stream->recv_queue.len;
      stream->recv_queue.len += bytes;

      infinoted_plugin_document_stream_received(
        stream,
        queue_offset,
        stream->recv_queue.len - queue_offset
      );
    }
  } while( (bytes < 0 && errcode == EINTR) ||
           (bytes > 0 && 
            stream->status == INFINOTED_PLUGIN_DOCUMENT_STREAM_RECEIVING));

  switch(stream->status)
  {
  case INFINOTED_PLUGIN_DOCUMENT_STREAM_NORMAL:
    g_assert_not_reached();
    break;
  case INFINOTED_PLUGIN_DOCUMENT_STREAM_RECEIVING:
    stream->status = INFINOTED_PLUGIN_DOCUMENT_STREAM_NORMAL;

    if(bytes < 0 && errcode != EAGAIN)
    {
      infinoted_plugin_document_stream_make_system_error(errno, error);
      infinoted_plugin_document_stream_close_stream(stream);
      return FALSE;
    }

    if(bytes == 0)
    {
      infinoted_plugin_document_stream_close_stream(stream);
      return TRUE;
    }
    
    break;
  case INFINOTED_PLUGIN_DOCUMENT_STREAM_CLOSED:
    /* The stream was closed during the received callback. */
    g_slice_free(InfinotedPluginDocumentStreamStream, stream);
    break;
  default:
    g_assert_not_reached();
    break;
  }
}

static gboolean
infinoted_plugin_document_stream_io_out(
  InfinotedPluginDocumentStreamStream* stream,
  GError** error)
{
  GError* local_error;
  gsize sent;

  g_assert(stream->status == INFINOTED_PLUGIN_DOCUMENT_STREAM_NORMAL);
  g_assert(stream->send_queue.len > 0);

  local_error = NULL;
  sent = infinoted_plugin_document_stream_send_direct(
    stream,
    stream->send_queue.data + stream->send_queue.pos,
    stream->send_queue.len,
    &local_error
  );

  if(local_error != NULL)
  {
    g_propagate_error(error, local_error);
    infinoted_plugin_document_stream_close_stream(stream);
    return FALSE;
  }
  else if(sent == 0)
  {
    infinoted_plugin_document_stream_close_stream(stream);
    return TRUE;
  }
  else
  {
    infinoted_plugin_document_stream_queue_consume(&stream->send_queue, sent);

    if(stream->send_queue.len == 0)
    {
      inf_io_update_watch(
        infinoted_plugin_manager_get_io(stream->plugin->manager),
        stream->watch,
        INF_IO_INCOMING
      );
    }

    return TRUE;
  }
}

static void
infinoted_plugin_document_stream_io_func(InfNativeSocket* socket,
                                         InfIoEvent event,
                                         gpointer user_data)
{
  InfinotedPluginDocumentStreamStream* stream;
  int val;
  int errval;
  socklen_t len;
  GError* error;

  stream = (InfinotedPluginDocumentStreamStream*)user_data;

  if(event & INF_IO_ERROR)
  {
    len = sizeof(errval);
    val = getsockopt(*socket, SOL_SOCKET, SO_ERROR, &errval, &len);
    if(val == -1)
    {
      infinoted_log_warning(
        infinoted_plugin_manager_get_log(stream->plugin->manager),
        "Failed to obtain error from socket: %s",
        strerror(errno)
      );
    }
    else
    {
      if(errval == 0)
      {
        /* Connection closed */
        infinoted_plugin_document_stream_close_stream(stream);
      }
      else
      {
        infinoted_log_warning(
          infinoted_plugin_manager_get_log(stream->plugin->manager),
          "Document stream error: %s",
          strerror(errval)
        );
      }
    }
  }
  else if(event & INF_IO_INCOMING)
  {
    error = NULL;
    if(!infinoted_plugin_document_stream_io_in(stream, &error))
    {
      infinoted_log_warning(
        infinoted_plugin_manager_get_log(stream->plugin->manager),
        "Document stream error: %s",
        error->message
      );

      g_error_free(error);
    }
  }
  else if(event & INF_IO_OUTGOING)
  {
    error = NULL;
    if(!infinoted_plugin_document_stream_io_out(stream, &error))
    {
      infinoted_log_warning(
        infinoted_plugin_manager_get_log(stream->plugin->manager),
        "Document stream error: %s",
        error->message
      );

      g_error_free(error);
    }
  }
}

static void
infinoted_plugin_document_stream_add_stream(
  InfinotedPluginDocumentStream* plugin,
  InfNativeSocket new_socket)
{
  InfinotedPluginDocumentStreamStream* stream;
  stream = g_slice_new(InfinotedPluginDocumentStreamStream);

  stream->plugin = plugin;
  stream->socket = new_socket;
  stream->watch = inf_io_add_watch(
    infinoted_plugin_manager_get_io(plugin->manager),
    &stream->socket,
    INF_IO_INCOMING,
    infinoted_plugin_document_stream_io_func,
    stream,
    NULL
  );

  stream->status = INFINOTED_PLUGIN_DOCUMENT_STREAM_NORMAL;
  infinoted_plugin_document_stream_queue_initialize(&stream->send_queue);
  infinoted_plugin_document_stream_queue_initialize(&stream->recv_queue);

  stream->navigate_handle = NULL;
  stream->subscribe_request = NULL;
  stream->proxy = NULL;
  stream->buffer = NULL;

  plugin->streams = g_slist_prepend(plugin->streams, stream);
}

static void
infinoted_plugin_document_stream_close_stream(
  InfinotedPluginDocumentStreamStream* stream)
{
  stream->plugin->streams = g_slist_remove(stream->plugin->streams, stream);

  if(stream->buffer != NULL || stream->subscribe_request != NULL)
    infinoted_plugin_document_stream_stop(stream, FALSE);

  if(stream->navigate_handle != NULL)
  {
    infinoted_plugin_util_navigate_cancel(stream->navigate_handle);
    stream->navigate_handle = NULL;
  }

  infinoted_plugin_document_stream_queue_finalize(&stream->send_queue);
  infinoted_plugin_document_stream_queue_finalize(&stream->recv_queue);

  inf_io_remove_watch(
    infinoted_plugin_manager_get_io(stream->plugin->manager),
    stream->watch
  );

  close(stream->socket);

  if(stream->status == INFINOTED_PLUGIN_DOCUMENT_STREAM_NORMAL)
    g_slice_free(InfinotedPluginDocumentStreamStream, stream);
  else if(stream->status == INFINOTED_PLUGIN_DOCUMENT_STREAM_RECEIVING)
    stream->status = INFINOTED_PLUGIN_DOCUMENT_STREAM_CLOSED;
}

static gboolean
infinoted_plugin_document_stream_set_nonblock(InfNativeSocket socket,
                                              GError** error)
{
  int result;

  result = fcntl(socket, F_GETFL);
  if(result == -1)
  {
    infinoted_plugin_document_stream_make_system_error(errno, error);
    return FALSE;
  }

  if(fcntl(socket, F_SETFL, result | O_NONBLOCK) == -1)
  {
    infinoted_plugin_document_stream_make_system_error(errno, error);
    return FALSE;
  }

  return TRUE;
}

static InfNativeSocket
infinoted_plugin_document_stream_accept_socket(InfNativeSocket socket,
                                               GError** error)
{
  InfNativeSocket new_socket;

  new_socket = accept(socket, NULL, NULL);
  if(new_socket == -1)
  {
    infinoted_plugin_document_stream_make_system_error(errno, error);
    return -1;
  }

  if(!infinoted_plugin_document_stream_set_nonblock(new_socket, error))
  {
    close(new_socket);
    return -1;
  }

  return new_socket;
}

static void
infinoted_plugin_manager_socket_accept_func(InfNativeSocket* socket,
                                            InfIoEvent event,
                                            gpointer user_data)
{
  InfinotedPluginDocumentStream* plugin;
  int val;
  int errval;
  socklen_t len;
  InfNativeSocket new_socket;
  GError* error;

  plugin = (InfinotedPluginDocumentStream*)user_data;

  if(event & INF_IO_ERROR)
  {
    len = sizeof(errval);
    val = getsockopt(*socket, SOL_SOCKET, SO_ERROR, &errval, &len);
    if(val == -1)
    {
      infinoted_log_warning(
        infinoted_plugin_manager_get_log(plugin->manager),
        "Failed to obtain error from socket: %s",
        strerror(errno)
      );
    }
    else
    {
      infinoted_log_warning(
        infinoted_plugin_manager_get_log(plugin->manager),
        "Document streaming server error: %s",
        strerror(errval)
      );
    }
  }
  else if(event & INF_IO_INCOMING)
  {
    error = NULL;

    new_socket = infinoted_plugin_document_stream_accept_socket(
      *socket,
      &error
    );

    if(error != NULL)
    {
      infinoted_log_warning(
        infinoted_plugin_manager_get_log(plugin->manager),
        "Failed to accept new stream: %s",
        error->message
      );

      g_error_free(error);
    }
    else
    {
      infinoted_plugin_document_stream_add_stream(plugin, new_socket);
    }
  }
}

static void
infinoted_plugin_document_stream_node_removed_cb(InfBrowser* browser,
                                                 InfBrowserIter* iter,
                                                 InfRequest* request,
                                                 gpointer user_data)
{
  InfinotedPluginDocumentStream* plugin;
  InfinotedPluginDocumentStreamStream* stream;
  GSList* item;

  plugin = (InfinotedPluginDocumentStream*)user_data;

  for(item = plugin->streams; item != NULL; item = item->next)
  {
    stream = (InfinotedPluginDocumentStreamStream*)item->data;
    if(stream->subscribe_request != NULL || stream->proxy != NULL)
    {
      if(inf_browser_is_ancestor(browser, iter, &stream->iter))
      {
        infinoted_plugin_document_stream_stop(stream, TRUE);
      }
    }
  }
}

static void
infinoted_plugin_document_stream_info_initialize(gpointer plugin_info)
{
  InfinotedPluginDocumentStream* plugin;
  plugin = (InfinotedPluginDocumentStream*)plugin_info;

  plugin->manager = NULL;
  plugin->socket = -1;
  plugin->watch = NULL;
  plugin->streams = NULL;
}

static gboolean
infinoted_plugin_document_stream_initialize(InfinotedPluginManager* manager,
                                            gpointer plugin_info,
                                            GError** error)
{
  static const char ADDRESS_NAME[] = "org.infinote.infinoted";
  struct sockaddr_un addr;

  InfinotedPluginDocumentStream* plugin;
  plugin = (InfinotedPluginDocumentStream*)plugin_info;

  plugin->manager = manager;

  plugin->socket = socket(AF_UNIX, SOCK_STREAM, 0);
  if(plugin->socket == -1)
  {
    infinoted_plugin_document_stream_make_system_error(errno, error);
    return FALSE;
  }

  /* TODO: Make the address configurable -- note that abstract paths
   * are a Linux extension. */
  addr.sun_family = AF_UNIX;
  addr.sun_path[0] = '\0';
  memcpy(&addr.sun_path[1], ADDRESS_NAME, sizeof(ADDRESS_NAME) - 1);

  memset(
    &addr.sun_path[1] + sizeof(ADDRESS_NAME) - 1,
    '\0',
    sizeof(addr.sun_path) - 1 - (sizeof(ADDRESS_NAME) - 1)
  );

  if(!infinoted_plugin_document_stream_set_nonblock(plugin->socket, error))
  {
    close(plugin->socket);
    plugin->socket = -1;
    return FALSE;
  }

  if(bind(plugin->socket, (struct sockaddr*)&addr, sizeof(addr)) == -1)
  {
    infinoted_plugin_document_stream_make_system_error(errno, error);
    close(plugin->socket);
    plugin->socket = -1;
    return FALSE;
  }

  if(listen(plugin->socket, 5) == -1)
  {
    infinoted_plugin_document_stream_make_system_error(errno, error);
    close(plugin->socket);
    plugin->socket = -1;
    return FALSE;
  }

  plugin->watch = inf_io_add_watch(
    infinoted_plugin_manager_get_io(plugin->manager),
    &plugin->socket,
    INF_IO_INCOMING,
    infinoted_plugin_manager_socket_accept_func,
    plugin,
    NULL
  );

  g_signal_connect(
    G_OBJECT(infinoted_plugin_manager_get_directory(plugin->manager)),
    "node-removed",
    G_CALLBACK(infinoted_plugin_document_stream_node_removed_cb),
    plugin
  );

  return TRUE;
}

static void
infinoted_plugin_document_stream_deinitialize(gpointer plugin_info)
{
  InfinotedPluginDocumentStream* plugin;
  plugin = (InfinotedPluginDocumentStream*)plugin_info;

  while(plugin->streams != NULL)
  {
    infinoted_plugin_document_stream_close_stream(
      (InfinotedPluginDocumentStreamStream*)plugin->streams->data
    );
  }

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(infinoted_plugin_manager_get_directory(plugin->manager)),
    G_CALLBACK(infinoted_plugin_document_stream_node_removed_cb),
    plugin
  );

  if(plugin->watch != NULL)
  {
    inf_io_remove_watch(
      infinoted_plugin_manager_get_io(plugin->manager),
      plugin->watch
    );
  }

  if(plugin->socket != -1)
  {
    close(plugin->socket);
  }
}

static const InfinotedParameterInfo INFINOTED_PLUGIN_DOCUMENT_STREAM_OPTIONS[] = {
  {
    NULL,
    0,
    0,
    0,
    NULL
  }
};

const InfinotedPlugin INFINOTED_PLUGIN = {
  "document-stream",
  N_("Allows streaming of document changes to external programs"),
  INFINOTED_PLUGIN_DOCUMENT_STREAM_OPTIONS,
  sizeof(InfinotedPluginDocumentStream),
  0,
  0,
  NULL,
  infinoted_plugin_document_stream_info_initialize,
  infinoted_plugin_document_stream_initialize,
  infinoted_plugin_document_stream_deinitialize,
  NULL,
  NULL,
  NULL,
  NULL
};

/* vim:set et sw=2 ts=2: */
