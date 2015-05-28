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

#define _XOPEN_SOURCE 700
#include "util/inf-test-util.h"

#include <libinfinity/server/infd-xml-server.h>
#include <libinfinity/server/infd-xmpp-server.h>
#include <libinfinity/common/inf-standalone-io.h>
#include <libinfinity/common/inf-cert-util.h>
#include <libinfinity/common/inf-init.h>

#include <gnutls/gnutls.h>
#include <gnutls/x509.h>

#include <libxml/xmlsave.h>

#include <time.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

typedef struct _InfTestTrafficReplay InfTestTrafficReplay;
struct _InfTestTrafficReplay {
  InfStandaloneIo* io;
  guint port;
  InfdXmppServer* xmpp;
  const gchar* filename;
  GSList* conns;
};

typedef enum _InfTestTrafficReplayMessageType {
  INF_TEST_TRAFFIC_REPLAY_MESSAGE_INCOMING,
  INF_TEST_TRAFFIC_REPLAY_MESSAGE_OUTGOING,
  INF_TEST_TRAFFIC_REPLAY_MESSAGE_CONNECT,
  INF_TEST_TRAFFIC_REPLAY_MESSAGE_DISCONNECT,
  INF_TEST_TRAFFIC_REPLAY_MESSAGE_ERROR
} InfTestTrafficReplayMessageType;

typedef struct _InfTestTrafficReplayMessage InfTestTrafficReplayMessage;
struct _InfTestTrafficReplayMessage {
  gint64 timestamp; /* microseconds since the epoch */
  InfTestTrafficReplayMessageType type;
  xmlNodePtr xml;
  xmlNodePtr xml_iter;
};

typedef struct _InfTestTrafficReplayConnection InfTestTrafficReplayConnection;
struct _InfTestTrafficReplayConnection {
  gchar* name;
  InfTestTrafficReplay* replay;
  InfCertificateCredentials* creds;
  InfXmppConnection* xmpp;
  FILE* file;
  InfTestTrafficReplayMessage* message;
  GHashTable* group_queues; /* group name -> GQueue */
};

typedef enum _InfTestTrafficReplayError {
  INF_TEST_TRAFFIC_REPLAY_ERROR_INVALID_LINE,
  INF_TEST_TRAFFIC_REPLAY_ERROR_UNEXPECTED_EOF
} InfTestTrafficReplayError;

static GQuark
inf_test_traffic_replay_error_quark()
{
  return g_quark_from_static_string("INF_TEST_TRAFFIC_REPLAY_ERROR");
}

static void
inf_test_traffic_replay_received_cb(InfXmppConnection* connection,
                                    xmlNodePtr xml,
                                    gpointer user_data);
static void
inf_test_traffic_replay_notify_status_cb(GObject* object,
                                         GParamSpec* pspec,
                                         gpointer user_data);

static void
inf_test_traffic_replay_queue_free(GQueue* queue)
{
  g_queue_free_full(queue, (GDestroyNotify)xmlFreeDoc);
}

static void
inf_test_traffic_replay_message_free(InfTestTrafficReplayMessage* message)
{
  if(message->type == INF_TEST_TRAFFIC_REPLAY_MESSAGE_INCOMING ||
     message->type == INF_TEST_TRAFFIC_REPLAY_MESSAGE_OUTGOING)
  {
    if(message->xml != NULL)
      xmlFreeNode(message->xml);
  }

  g_slice_free(InfTestTrafficReplayMessage, message);
}

static char*
inf_test_traffic_replay_get_next_line(InfTestTrafficReplayConnection* conn,
                                      size_t* len,
                                      GError** error)
{
  char* line;
  size_t n;
  ssize_t len_;
  int err;

  line = NULL;
  n = 0;

  len_ = getline(&line, &n, conn->file);
  if(len_ >= 0)
  {
    *len = len_;
    return line;
  }
  else
  {
    if(feof(conn->file))
    {
      /* TODO: We should treat this is a "log closed" event */
      g_set_error(
        error,
        inf_test_traffic_replay_error_quark(),
        INF_TEST_TRAFFIC_REPLAY_ERROR_UNEXPECTED_EOF,
        "Unexpected end of file"
      );
    }
    else
    {
      err = ferror(conn->file);

      g_set_error(
        error,
        G_FILE_ERROR,
        g_file_error_from_errno(err),
        strerror(err)
      );
    }

    return NULL;
  }
}

static InfTestTrafficReplayMessage*
inf_test_traffic_replay_get_next_message(InfTestTrafficReplayConnection* conn,
                                         GError** error)
{
  char* line;
  size_t len;

  size_t n;
  struct tm tm;
  char* end;

  gulong msecs;

  InfTestTrafficReplayMessageType type;
  xmlDocPtr xml;
  GString* str;

  InfTestTrafficReplayMessage* message;

  line = inf_test_traffic_replay_get_next_line(conn, &len, error);
  if(!line) return NULL;

  /* Jump timestamp */
  if(line[0] != '[')
  {
    g_set_error(
      error,
      inf_test_traffic_replay_error_quark(),
      INF_TEST_TRAFFIC_REPLAY_ERROR_INVALID_LINE,
      "Line does not start with a timestamp"
    );

    free(line);
    return FALSE;
  }
  
  /* Parse timestamp */
  end = strptime(&line[1], "%a %d %b %Y %I:%M:%S %p %Z", &tm);
  if(end == NULL || *end != ' ' || *(end+1) != '.')
  {
    g_set_error(
      error,
      inf_test_traffic_replay_error_quark(),
      INF_TEST_TRAFFIC_REPLAY_ERROR_INVALID_LINE,
      "Failed to parse timestamp"
    );

    free(line);
    return FALSE;
  }

  errno = 0;
  msecs = strtoul(&end[2], &end, 10);
  if(errno != 0 || *end != ']' || msecs >= 1000000)
  {
    g_set_error(
      error,
      inf_test_traffic_replay_error_quark(),
      INF_TEST_TRAFFIC_REPLAY_ERROR_INVALID_LINE,
      "Failed to parse timestamp"
    );

    free(line);
    return FALSE;
  }

  /* skip ']' */
  ++end;

  /* This seems to be set at random -- for the moment assume there are no
   * timestamps with different timezones */
  tm.tm_isdst = 1;

  /* interpret message */
  n = (end - line) + 1;
  if(line[n] == '!')
  {
    if(strstr(line + n + 4, "connected") != NULL)
      type = INF_TEST_TRAFFIC_REPLAY_MESSAGE_CONNECT;
    else if(strstr(line + n + 4, "Connection error") != NULL)
      type = INF_TEST_TRAFFIC_REPLAY_MESSAGE_ERROR;
    else if(strstr(line + n + 4, "closed") != NULL)
      type = INF_TEST_TRAFFIC_REPLAY_MESSAGE_DISCONNECT;
    else
    {
      g_set_error(
        error,
        inf_test_traffic_replay_error_quark(),
        INF_TEST_TRAFFIC_REPLAY_ERROR_INVALID_LINE,
        "Unknown connection event \"%s\"",
        line + n + 4
      );

      free(line);
      return FALSE;
    }
  }
  else if(line[n] == '<')
  {
    if(FALSE) /* TODO as_server) */
      type = INF_TEST_TRAFFIC_REPLAY_MESSAGE_INCOMING;
    else
      type = INF_TEST_TRAFFIC_REPLAY_MESSAGE_OUTGOING;
  }
  else if(line[n] == '>')
  {
    if(FALSE) /* TODO as_server)*/
      type = INF_TEST_TRAFFIC_REPLAY_MESSAGE_OUTGOING;
    else
      type = INF_TEST_TRAFFIC_REPLAY_MESSAGE_INCOMING;
  }
  else
  {
    g_set_error(
      error,
      inf_test_traffic_replay_error_quark(),
      INF_TEST_TRAFFIC_REPLAY_ERROR_INVALID_LINE,
      "Unknown control character \"%c\" (%d)",
      line[n],
      (int)line[n]
    );

    free(line);
    return FALSE;
  }

  if(type == INF_TEST_TRAFFIC_REPLAY_MESSAGE_INCOMING ||
     type == INF_TEST_TRAFFIC_REPLAY_MESSAGE_OUTGOING)
  {
    str = g_string_new_len(line + n + 4, len - n - 5);
    xml = xmlReadDoc(str->str, NULL, "UTF-8", XML_PARSE_NOWARNING | XML_PARSE_NOERROR);
    while(!xml)
    {
      /* It might happen that we could not parse this if there is a newline
       * character in the XML.
       * TODO: We should use a SAX parser for this stuff... */
      free(line);
      line = inf_test_traffic_replay_get_next_line(conn, &len, error);
      if(!line)
      {
        g_string_free(str, TRUE);
        return NULL;
      }

      g_string_append_c(str, '\n');
      g_string_append_len(str, line, len - 1);
      xml = xmlReadDoc(str->str, NULL, "UTF-8", XML_PARSE_NOWARNING | XML_PARSE_NOERROR);
    }

    g_string_free(str, TRUE);
  }

  free(line);

  message = g_slice_new(InfTestTrafficReplayMessage);
  message->timestamp = (gint64)mktime(&tm) * 1000000 + msecs;
  message->type = type;
  if(type == INF_TEST_TRAFFIC_REPLAY_MESSAGE_INCOMING ||
     type == INF_TEST_TRAFFIC_REPLAY_MESSAGE_OUTGOING)
  {
    message->xml = xmlCopyNode(xmlDocGetRootElement(xml), 1);
    if(type == INF_TEST_TRAFFIC_REPLAY_MESSAGE_INCOMING)
      message->xml_iter = message->xml->children;
    xmlFreeDoc(xml);
  }

  return message;
}

static void
inf_test_traffic_replay_connection_close(InfTestTrafficReplayConnection* conn)
{
  InfXmlConnectionStatus status;

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(conn->xmpp),
    G_CALLBACK(inf_test_traffic_replay_received_cb),
    conn
  );

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(conn->xmpp),
    G_CALLBACK(inf_test_traffic_replay_notify_status_cb),
    conn
  );

  if(conn->xmpp != NULL)
  {
    g_object_get(G_OBJECT(conn->xmpp), "status", &status, NULL);
    if(status == INF_XML_CONNECTION_OPEN ||
       status == INF_XML_CONNECTION_OPENING)
    {
      inf_xml_connection_close(INF_XML_CONNECTION(conn->xmpp));
    }
  }

  if(conn->creds != NULL)
    inf_certificate_credentials_unref(conn->creds);

  g_object_unref(conn->xmpp);
  if(conn->file != NULL) fclose(conn->file);

  g_hash_table_destroy(conn->group_queues);

  fprintf(stderr, "[%s] Disconnected\n", conn->name);
  g_free(conn->name);

  conn->replay->conns = g_slist_remove(conn->replay->conns, conn);

  if(conn->replay->conns == NULL)
    inf_standalone_io_loop_quit(conn->replay->io);

  g_slice_free(InfTestTrafficReplayConnection, conn);
}

static void
inf_test_traffic_replay_connection_check_message(
  InfTestTrafficReplayConnection* conn,
  xmlNodePtr xml)
{
  xmlBufferPtr received_buffer;
  xmlBufferPtr expected_buffer;
  xmlSaveCtxtPtr ctx;

  received_buffer = xmlBufferCreate();
  expected_buffer = xmlBufferCreate();

  /* Remove time field from chat messages, as this is not synchronized */
  if(strcmp(conn->message->xml_iter->name, "message") == 0)
    xmlSetProp(conn->message->xml_iter, "time", NULL);
  if(strcmp(xml->name, "message") == 0)
    xmlSetProp(xml, "time", NULL);

  ctx = xmlSaveToBuffer(expected_buffer, "UTF-8", 0);
  xmlSaveTree(ctx, conn->message->xml_iter);
  xmlSaveClose(ctx);

  ctx = xmlSaveToBuffer(received_buffer, "UTF-8", 0);
  xmlSaveTree(ctx, xml);
  xmlSaveClose(ctx);

  if(strcmp(xmlBufferContent(expected_buffer), xmlBufferContent(received_buffer)) != 0)
  {
    fprintf(
      stderr,
      "[WARNING] [%s] Mismatch between expected and received: "
      "\n\n\"%s\"\n\nvs.\n\n\"%s\"\n",
      conn->name,
      (const gchar*)xmlBufferContent(expected_buffer),
      (const gchar*)xmlBufferContent(received_buffer)
    );

    xmlBufferFree(expected_buffer);
    xmlBufferFree(received_buffer);
    if(inf_standalone_io_loop_running(conn->replay->io))
      inf_standalone_io_loop_quit(conn->replay->io);
    return;
  }

  xmlBufferFree(expected_buffer);
  xmlBufferFree(received_buffer);
}

static gboolean
inf_test_traffic_replay_connection_process_next_message(
  InfTestTrafficReplayConnection* conn)
{
  InfIpAddress* addr;
  InfTcpConnection* tcp;
  GError* error;
  xmlChar* group;
  GQueue* queue;

  switch(conn->message->type)
  {
  case INF_TEST_TRAFFIC_REPLAY_MESSAGE_CONNECT:
    /* If we are already connecting, just wait until the connection has
     * finished. This can happen when we are connecting, and then another
     * connection receives something, after which messages are processed
     * again. */
    if(conn->xmpp != NULL)
      return FALSE;

    fprintf(stderr, "[%s] Connecting...\n", conn->name);

    addr = inf_ip_address_new_loopback4();

    tcp = inf_tcp_connection_new(
      INF_IO(conn->replay->io),
      addr,
      conn->replay->port
    );

    inf_ip_address_free(addr);

    conn->xmpp = inf_xmpp_connection_new(
      tcp,
      INF_XMPP_CONNECTION_CLIENT,
      NULL,
      "localhost",
      INF_XMPP_CONNECTION_SECURITY_ONLY_TLS,
      conn->creds,
      NULL,
      NULL
    );

    g_signal_connect(
      G_OBJECT(conn->xmpp),
      "received",
      G_CALLBACK(inf_test_traffic_replay_received_cb),
      conn
    );

    g_signal_connect(
      G_OBJECT(conn->xmpp),
      "notify::status",
      G_CALLBACK(inf_test_traffic_replay_notify_status_cb),
      conn
    );

    error = NULL;
    if(!inf_tcp_connection_open(tcp, &error))
    {
      fprintf(stderr, "[ERROR] [%s] %s\n", conn->name, error->message);
      g_error_free(error);

      if(inf_standalone_io_loop_running(conn->replay->io))
        inf_standalone_io_loop_quit(conn->replay->io);
      return FALSE;
    }

    g_object_unref(tcp);
    /* return false, to wait until the connection was established */
    return FALSE;
  case INF_TEST_TRAFFIC_REPLAY_MESSAGE_DISCONNECT:
    g_assert(conn->xmpp != NULL);
    inf_test_traffic_replay_connection_close(conn);
    return TRUE;
  case INF_TEST_TRAFFIC_REPLAY_MESSAGE_ERROR:
    g_assert(conn->xmpp != NULL);
    fprintf(stderr, "[%s] Recorded connection error, ignored\n", conn->name);
    return TRUE;
  case INF_TEST_TRAFFIC_REPLAY_MESSAGE_INCOMING:
    g_assert(conn->xmpp != NULL);
    group = xmlGetProp(conn->message->xml, "name");
    fprintf(stderr, "[%s] Expecting data (%s, %s)\n", conn->name, group, conn->message->xml_iter->name); /* TODO: write what data? */
    queue = g_hash_table_lookup(conn->group_queues, group);
    xmlFree(group);

    /* Queued data should have been processed before this function was called */
    g_assert(queue == NULL || g_queue_is_empty(queue));

    /* wait for data to arrive */
    return FALSE;
  case INF_TEST_TRAFFIC_REPLAY_MESSAGE_OUTGOING:
    g_assert(conn->xmpp != NULL);

    group = xmlGetProp(conn->message->xml, "name");
    fprintf(stderr, "[%s] Sending data (%s, %s)\n", conn->name, group, conn->message->xml->children->name); /* TODO: write what data? */
    xmlFree(group);

    /* send the data */
    inf_xml_connection_send(
      INF_XML_CONNECTION(conn->xmpp),
      conn->message->xml
    );

    conn->message->xml = NULL;
    return TRUE;
  default:
    g_assert_not_reached();
    break;
  }
}

static void
inf_test_traffic_replay_process_next_message(InfTestTrafficReplay* replay);

static void
inf_test_traffic_replay_connection_fetch_next_message(
  InfTestTrafficReplayConnection* conn)
{
  xmlChar* group;
  GQueue* queue;
  xmlNodePtr xml;

  if(!inf_standalone_io_loop_running(conn->replay->io))
    return;

  if(conn->message->type == INF_TEST_TRAFFIC_REPLAY_MESSAGE_INCOMING &&
     conn->message->xml_iter->next != NULL)
  {
    conn->message->xml_iter = conn->message->xml_iter->next;
  }
  else
  {
    GError* error;
    inf_test_traffic_replay_message_free(conn->message);

    /* Fetch the next message for this connection */
    error = NULL;
    conn->message = inf_test_traffic_replay_get_next_message(conn, &error);
    if(error != NULL)
    {
      fprintf(
        stderr,
        "[ERROR] [%s] Failed to fetch message: %s\n",
        conn->name,
        error->message
      );

      g_error_free(error);

      if(inf_standalone_io_loop_running(conn->replay->io))
        inf_standalone_io_loop_quit(conn->replay->io);
      return;
    }
  }

  /* Check queued received messages -- these should be delivered immediately,
   * and not when the timestamp expires; since they have been
   * received already. */
  if(conn->message->type == INF_TEST_TRAFFIC_REPLAY_MESSAGE_INCOMING)
  {
    group = xmlGetProp(conn->message->xml, "name");
    queue = g_hash_table_lookup(conn->group_queues, group);

    if(queue != NULL && !g_queue_is_empty(queue))
    {
      xml = g_queue_pop_head(queue);

      fprintf(stderr, "[%s] Replay data (%s, %s)\n", conn->name, group, xml->name);

      inf_test_traffic_replay_connection_check_message(conn, xml);
      inf_test_traffic_replay_connection_fetch_next_message(conn);
      xmlFree(group);
      return;
    }

    xmlFree(group);
  }

  /* Then, evaluate next message among all connections */
  inf_test_traffic_replay_process_next_message(conn->replay);
}

static void
inf_test_traffic_replay_process_next_message(InfTestTrafficReplay* replay)
{
  /* Find the connection with the next event, and process it */
  GSList* item;
  InfTestTrafficReplayConnection* conn;
  InfTestTrafficReplayConnection* low;

  if(!inf_standalone_io_loop_running(replay->io))
    return;

  low = NULL;
  for(item = replay->conns; item != NULL; item = item->next)
  {
    conn = (InfTestTrafficReplayConnection*)item->data;
    if(low == NULL || conn->message->timestamp < low->message->timestamp)
      low = conn;
    else if(conn->message->timestamp == low->message->timestamp)
    {
      /* If there are two messages with the same timestamp, then make sure we
       * first send data before we wait for data. */
      if(conn->message->type == INF_TEST_TRAFFIC_REPLAY_MESSAGE_OUTGOING &&
         low->message->type == INF_TEST_TRAFFIC_REPLAY_MESSAGE_INCOMING)
      {
        low = conn;
      }
    }
  }

  if(inf_test_traffic_replay_connection_process_next_message(low))
  {
    if(g_slist_find(replay->conns, low))
      inf_test_traffic_replay_connection_fetch_next_message(low);
    else
      inf_test_traffic_replay_process_next_message(replay);
  }
}

static void
inf_test_traffic_replay_received_cb(InfXmppConnection* connection,
                                    xmlNodePtr xml,
                                    gpointer user_data)
{
  InfTestTrafficReplayConnection* conn;
  xmlBufferPtr buffer;
  xmlSaveCtxtPtr ctx;

  xmlNodePtr child;
  GQueue* queue;
  xmlChar* received_group;
  xmlChar* expected_group;

  conn = (InfTestTrafficReplayConnection*)user_data;

  g_assert(strcmp(xml->name, "group") == 0);

  for(child = xml->children; child != NULL; child = child->next)
  {
    if(!inf_standalone_io_loop_running(conn->replay->io))
      break;

    if(!conn->message ||
       conn->message->type != INF_TEST_TRAFFIC_REPLAY_MESSAGE_INCOMING)
    {
      buffer = xmlBufferCreate();
      ctx = xmlSaveToBuffer(buffer, "UTF-8", 0);
      xmlSaveTree(ctx, child);
      xmlSaveClose(ctx);

      fprintf(
        stderr,
        "[ERROR] [%s] Received text \"%s\" without expecting any\n",
        conn->name,
        (const gchar*)xmlBufferContent(buffer)
      );

      xmlBufferFree(buffer);
      inf_standalone_io_loop_quit(conn->replay->io);
      return;
    }

    received_group = xmlGetProp(xml, "name");
    expected_group = xmlGetProp(conn->message->xml, "name");

    fprintf(
      stderr,
      "[%s] Received data (%s, %s), expected %s\n",
      conn->name,
      received_group,
      child->name,
      expected_group
    );

    /* TODO: Figure out why this assertion fires */
    queue = g_hash_table_lookup(conn->group_queues, expected_group);
    g_assert(queue == NULL || g_queue_is_empty(queue));

    if(strcmp(received_group, expected_group) != 0)
    {
      /* We received a message for a different group than what we expected. Cache
       * the message for later, in case the server schedules message delivery
       * differently. */
      queue = g_hash_table_lookup(conn->group_queues, received_group);
      if(!queue)
      {
        queue = g_queue_new();

        g_hash_table_insert(
          conn->group_queues,
          g_strdup(received_group),
          queue
        );
      }

      g_queue_push_tail(queue, xmlCopyNode(child, 1));

      xmlFree(received_group);
      xmlFree(expected_group);
    }
    else
    {
      /* We received a message for the expected group; so check whether it is
       * also the message that we expected. */
      xmlFree(received_group);
      xmlFree(expected_group);

      inf_test_traffic_replay_connection_check_message(conn, child);
      inf_test_traffic_replay_connection_fetch_next_message(conn);
    }
  }
}

static void
inf_test_traffic_replay_notify_status_cb(GObject* object,
                                         GParamSpec* pspec,
                                         gpointer user_data)
{
  InfTestTrafficReplayConnection* conn;
  InfXmlConnectionStatus status;

  conn = (InfTestTrafficReplayConnection*)user_data;
  g_object_get(object, "status", &status, NULL);

  switch(status)
  {
  case INF_XML_CONNECTION_OPENING:
    /* wait for it to open */
    break;
  case INF_XML_CONNECTION_OPEN:
    fprintf(stderr, "[%s] Connected\n", conn->name);
    inf_test_traffic_replay_connection_fetch_next_message(conn);
    break;
  case INF_XML_CONNECTION_CLOSING:
  case INF_XML_CONNECTION_CLOSED:
    fprintf(stderr, "[ERROR] [%s] Remote connection closed\n", conn->name);
    inf_standalone_io_loop_quit(conn->replay->io);
    /*inf_test_traffic_replay_connection_close(conn);*/
    break;
  }
}

static void
inf_test_traffic_replay_new_connection_cb(InfdXmppServer* server,
                                          InfXmppConnection* xmpp,
                                          gpointer user_data)
{
  InfTestTrafficReplayConnection* conn;
  InfTestTrafficReplay* replay;
  InfXmlConnectionStatus status;
  GError* error;

  conn = g_slice_new(InfTestTrafficReplayConnection);
  replay = (InfTestTrafficReplay*)user_data;

  conn->name = g_strdup("server");
  conn->replay = replay;
  conn->creds = NULL;
  conn->xmpp = xmpp;
  
  conn->group_queues = g_hash_table_new_full(
    g_str_hash,
    g_str_equal,
    g_free,
    (GDestroyNotify)inf_test_traffic_replay_queue_free
  );

  error = NULL;
  conn->message = inf_test_traffic_replay_get_next_message(conn, &error);
  if(error != NULL)
  {
    fprintf(
      stderr,
      "Failed to read initial message for %s: %s\n",
      conn->name,
      error->message
    );

    inf_test_traffic_replay_connection_close(conn);
  }

  replay->conns = g_slist_prepend(replay->conns, conn);
  g_object_ref(xmpp);

  g_signal_connect(
    G_OBJECT(xmpp),
    "received",
    G_CALLBACK(inf_test_traffic_replay_received_cb),
    conn
  );

  g_signal_connect(
    G_OBJECT(conn->xmpp),
    "notify::status",
    G_CALLBACK(inf_test_traffic_replay_notify_status_cb),
    conn
  );

  conn->file = fopen(replay->filename, "r");
  if(!conn->file)
  {
    fprintf(
      stderr,
      "Failed to open %s: %s\n",
      replay->filename,
      strerror(errno)
    );

    inf_test_traffic_replay_connection_close(conn);
  }
  else
  {
    g_object_get(G_OBJECT(conn->xmpp), "status", &status, NULL);
    if(status == INF_XML_CONNECTION_OPEN)
    {
      /* TODO: Consume first connection message */
      inf_test_traffic_replay_connection_fetch_next_message(conn);
    }
  }

  /* TODO: Shut down server after first client connected! */
}

static InfCertificateCredentials*
inf_test_traffic_replay_load_server_credentials(GError** error)
{
  GPtrArray* array;
  gnutls_x509_privkey_t key;
  InfCertificateCredentials* creds;
  gnutls_certificate_credentials_t gcreds;
  guint i;

  /* TODO: Paths should be configurable */
  key = inf_cert_util_read_private_key(
    "/home/armin/kombia/kombia.cert",
    error
  );

  if(!key) return NULL;

  array = inf_cert_util_read_certificate(
    "/home/armin/kombia/kombia.cert",
    NULL,
    error
  );

  if(!array) return NULL;

  creds = inf_certificate_credentials_new();
  gcreds = inf_certificate_credentials_get(creds);

  gnutls_certificate_set_x509_key(
    gcreds,
    (gnutls_x509_crt_t*)array->pdata,
    array->len,
    key
  );

  gnutls_certificate_set_x509_trust(
    gcreds,
    (gnutls_x509_crt_t*)array->pdata,
    array->len
  );

  gnutls_x509_privkey_deinit(key);
  for(i = 0; i < array->len; ++i)
    gnutls_x509_crt_deinit(array->pdata[i]);
  g_ptr_array_free(array, TRUE);

  return creds;
}

static InfCertificateCredentials*
inf_test_traffic_replay_load_client_credentials(const gchar* path,
                                                GError** error)
{
  gchar* basename;
  gchar* dirname;
  gchar* full;

  GPtrArray* array;
  gnutls_x509_privkey_t key;
  InfCertificateCredentials* creds;
  gnutls_certificate_credentials_t gcreds;
  guint i;

  basename = g_path_get_basename(path);
  dirname = g_path_get_dirname(path);
  full = g_build_filename(dirname, "certs", basename, NULL);
  g_free(basename);
  g_free(dirname);

  /* TODO: Paths should be configurable */
  key = inf_cert_util_read_private_key(full, error);
  if(!key) { g_free(full); return NULL; }

  array = inf_cert_util_read_certificate(full, NULL, error);
  if(!array) { g_free(full); return NULL; }

  g_free(full);

  creds = inf_certificate_credentials_new();
  gcreds = inf_certificate_credentials_get(creds);

  gnutls_certificate_set_x509_key(
    gcreds,
    (gnutls_x509_crt_t*)array->pdata,
    array->len,
    key
  );

  gnutls_certificate_set_x509_trust(
    gcreds,
    (gnutls_x509_crt_t*)&array->pdata[array->len - 1],
    1
  );

  gnutls_x509_privkey_deinit(key);
  for(i = 0; i < array->len; ++i)
    gnutls_x509_crt_deinit(array->pdata[i]);
  g_ptr_array_free(array, TRUE);

  return creds;
}

static void
inf_test_traffic_replay_start_func(gpointer user_data)
{
  inf_test_traffic_replay_process_next_message(user_data);
}

int main(int argc, char* argv[])
{
  InfTestTrafficReplay replay;
  InfdTcpServer* server;
  InfCertificateCredentials* creds;
  GError* error;
  gboolean as_server;
  guint port;

  int i;
  FILE* f;
  InfTestTrafficReplayConnection* conn;

  as_server = FALSE;
  port = 6524;

  if(argc < 2)
  {
    fprintf(stderr, "Usage: %s <traffic-log>\n", argv[0]);
    return -1;
  }

  error = NULL;
  if(!inf_init(&error))
  {
    fprintf(stderr, "%s\n", error->message);
    g_error_free(error);
    return -1;
  }

  replay.io = inf_standalone_io_new();
  replay.port = port;
  replay.xmpp = NULL;
  replay.conns = NULL;

  if(as_server == TRUE)
  {
    replay.filename = argv[1];

    creds = inf_test_traffic_replay_load_server_credentials(&error);
    if(!creds)
    {
      fprintf(stderr, "%s\n", error->message);
      g_error_free(error);
      return -1;
    }

    /* Start a server listening on port 6524 */
    server = g_object_new(
      INFD_TYPE_TCP_SERVER,
      "io", replay.io,
      "local-address", NULL,
      "local-port", port,
      NULL
    );

    replay.xmpp = infd_xmpp_server_new(
      server,
      INF_XMPP_CONNECTION_SECURITY_ONLY_TLS,
      creds,
      NULL,
      NULL
    );

    inf_certificate_credentials_unref(creds);

    g_signal_connect(
      G_OBJECT(replay.xmpp),
      "new-connection",
      G_CALLBACK(inf_test_traffic_replay_new_connection_cb),
      &replay
    );

    if(!infd_tcp_server_open(server, &error))
    {
      fprintf(stderr, "%s\n", error->message);
      g_error_free(error);
      return 1;
    }

    g_object_unref(server);
  }
  else
  {
    replay.filename = NULL;

    for(i = 1; i < argc; ++i)
    {
      f = fopen(argv[i], "r");
      if(!f)
      {
        fprintf(
          stderr,
          "Failed to open %s: %s\n",
          argv[i],
          strerror(errno)
        );

        return 1;
      }

      conn = g_slice_new(InfTestTrafficReplayConnection);
      conn->replay = &replay;
      conn->name = g_strdup_printf("client %d (%s)", i, argv[i]);
      conn->xmpp = NULL;
      conn->file = f;

      conn->group_queues = g_hash_table_new_full(
        g_str_hash,
        g_str_equal,
        g_free,
        (GDestroyNotify)inf_test_traffic_replay_queue_free
      );

      conn->creds = inf_test_traffic_replay_load_client_credentials(argv[i], &error);
      if(error != NULL)
      {
        if(error->domain == G_FILE_ERROR && error->code == G_FILE_ERROR_NOENT)
        {
          printf("No client credentials for %s\n", conn->name);

          /* no credentials, that's okay */
          g_error_free(error);
          error = NULL;
        }
        else
        {
          fprintf(
            stderr,
            "Failed to load client credentials for %s: %s\n",
            conn->name,
            error->message
          );

          return 1;
        }
      }
      else
      {
        printf("Loaded client credentials for %s\n", conn->name);
      }

      replay.conns = g_slist_prepend(replay.conns, conn);

      conn->message = inf_test_traffic_replay_get_next_message(conn, &error);
      if(error != NULL)
      {
        fprintf(
          stderr,
          "Failed to read initial message for %s: %s\n",
          conn->name,
          error->message
        );

        return 1;
      }
    }

    inf_io_add_dispatch(
      INF_IO(replay.io),
      inf_test_traffic_replay_start_func,
      &replay,
      NULL
    );
  }

  inf_standalone_io_loop(replay.io);

  /* TODO: cleanup... */

  return 0;
}

/* vim:set et sw=2 ts=2: */
