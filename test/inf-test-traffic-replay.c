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

#include "util/inf-test-util.h"

#include <libinfinity/server/infd-xml-server.h>
#include <libinfinity/server/infd-xmpp-server.h>
#include <libinfinity/common/inf-standalone-io.h>
#include <libinfinity/common/inf-cert-util.h>
#include <libinfinity/common/inf-init.h>

#include <gnutls/gnutls.h>
#include <gnutls/x509.h>

#include <libxml/xmlsave.h>

#include <string.h>
#include <errno.h>
#include <assert.h>

typedef struct _InfTestTrafficReplay InfTestTrafficReplay;
struct _InfTestTrafficReplay {
  InfStandaloneIo* io;
  InfdXmppServer* xmpp;
  const gchar* filename;
  GSList* conns;
};

typedef struct _InfTestTrafficReplayConnection InfTestTrafficReplayConnection;
struct _InfTestTrafficReplayConnection {
  InfTestTrafficReplay* replay;
  InfXmppConnection* xmpp;
  FILE* file;
  gchar* expected_line;
};

typedef enum _InfTestTrafficReplayError {
  INF_TEST_TRAFFIC_REPLAY_ERROR_INVALID_LINE
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

  g_object_get(G_OBJECT(conn->xmpp), "status", &status, NULL);
  if(status == INF_XML_CONNECTION_OPEN ||
     status == INF_XML_CONNECTION_OPENING)
  {
    inf_xml_connection_close(INF_XML_CONNECTION(conn->xmpp));
  }

  g_object_unref(conn->xmpp);
  if(conn->file != NULL) fclose(conn->file);

  infd_xml_server_close(INFD_XML_SERVER(conn->replay->xmpp));
  inf_standalone_io_loop_quit(conn->replay->io);

  conn->replay->conns = g_slist_remove(conn->replay->conns, conn);
  g_slice_free(InfTestTrafficReplayConnection, conn);
}

static gboolean
inf_test_traffic_replay_connection_process_line(
  InfTestTrafficReplayConnection* conn,
  const char* line,
  size_t len,
  GError** error)
{
  size_t n;
  xmlDocPtr xml;

  GString* str;
  char* next_line;
  size_t next_n;
  ssize_t next_len;

  assert(conn->expected_line == NULL);

  /* Jump timestamp */
  if(line[0] != '[')
  {
    g_set_error(
      error,
      inf_test_traffic_replay_error_quark(),
      INF_TEST_TRAFFIC_REPLAY_ERROR_INVALID_LINE,
      "Line does not start with a timestamp"
    );

    return FALSE;
  }

  for(n = 1; line[n] != '\0'; ++n)
    if(line[n] == ']')
      break;
  if(line[n] == '\0')
  {
    g_set_error(
      error,
      inf_test_traffic_replay_error_quark(),
      INF_TEST_TRAFFIC_REPLAY_ERROR_INVALID_LINE,
      "Timestamp does not end"
    );

    return FALSE;
  }

  /* interpret message */
  n += 2;
  if(line[n] == '!') return TRUE; /* info, ignore */
  else if(line[n] == '<')
  {
    /* incoming */
    conn->expected_line = g_strndup(line + n + 4, len - n - 5);
    printf("Expecting: %s\n", conn->expected_line);
    return TRUE;
  }
  else if(line[n] == '>') /* outgoing */
  {
    str = g_string_new_len(line + n + 4, len - n - 5);
    xml = xmlParseDoc(str->str);
    while(!xml)
    {
      /* It might happen that we could not parse this if there is a newline
       * character in the XML.
       * TODO: We should use a SAX parser for this stuff... */
      next_line = NULL;
      next_n = 0;
      next_len = getline(&next_line, &next_n, conn->file);
      if(next_len >= 0)
      {
        g_string_append_c(str, '\n');
        g_string_append_len(str, next_line, next_len - 1);
        free(next_line);
        xml = xmlParseDoc(str->str);
      }
      else
      {
        if(errno != 0)
        {
          g_set_error(
            error,
            G_FILE_ERROR,
            g_file_error_from_errno(errno),
            strerror(errno)
          );
        }
        else
        {
          g_set_error(
            error,
            inf_test_traffic_replay_error_quark(),
            INF_TEST_TRAFFIC_REPLAY_ERROR_INVALID_LINE,
            "Failed to parse XML: \"%s\"",
            str->str
          );
        }

        g_string_free(str, TRUE);
        return FALSE;
      }
    }

    g_string_free(str, TRUE);

    inf_xml_connection_send(
      INF_XML_CONNECTION(conn->xmpp),
      xmlCopyNode(xmlDocGetRootElement(xml), 1)
    );

    xmlFreeDoc(xml);
    return TRUE;
  }
  else
  {
    g_set_error(
      error,
      inf_test_traffic_replay_error_quark(),
      INF_TEST_TRAFFIC_REPLAY_ERROR_INVALID_LINE,
      "Unexpected control character: \"%c\" (%d)",
      line[n],
      (int)line[n]
    );

    return FALSE;
  }
}

static void
inf_test_traffic_replay_connection_process_next_line(
  InfTestTrafficReplayConnection* conn)
{
  char* line;
  size_t n;
  ssize_t len;
  GError* error;

  assert(conn->expected_line == NULL);

  line = NULL;
  n = 0;

  len = getline(&line, &n, conn->file);
  if(len < 0)
  {
    if(errno != 0)
      fprintf(stderr, "Failed to read file: %s\n", strerror(errno));
    else
      fprintf(stderr, "Traffic replay complete!\n");
    inf_test_traffic_replay_connection_close(conn);
    return;
  }

  error = NULL;
  if(!inf_test_traffic_replay_connection_process_line(conn, line, len, &error))
  {
    fprintf(stderr, "Failed to interpret line: %s\n", error->message);
    free(line);

    inf_test_traffic_replay_connection_close(conn);
    return;
  }

  free(line);
  if(!conn->expected_line)
  {
    inf_test_traffic_replay_connection_process_next_line(conn);
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

  conn = (InfTestTrafficReplayConnection*)user_data;

  buffer = xmlBufferCreate();
  ctx = xmlSaveToBuffer(buffer, "UTF-8", 0);
  xmlSaveTree(ctx, xml);
  xmlSaveClose(ctx);

  if(!conn->expected_line)
  {
    fprintf(
      stderr,
      "Received text \"%s\" without expecting any, ignoring...\n",
      (const gchar*)xmlBufferContent(buffer)
    );

    xmlBufferFree(buffer);
    return;
  }

  if(strcmp(conn->expected_line, (const gchar*)xmlBufferContent(buffer)) != 0)
  {
    fprintf(
      stderr,
      "Mismatch between expected and received: "
      "\"%s\" vs. \"%s\", ignoring...\n",
      conn->expected_line,
      (const gchar*)xmlBufferContent(buffer)
    );
  }

  g_free(conn->expected_line);
  conn->expected_line = NULL;
  xmlBufferFree(buffer);

  inf_test_traffic_replay_connection_process_next_line(conn);
}

static void
inf_test_traffic_replay_notify_status_cb(GObject* object,
                                         GParamSpec* pspec,
                                         gpointer user_data)
{
  InfXmlConnectionStatus status;
  g_object_get(object, "status", &status, NULL);

  switch(status)
  {
  case INF_XML_CONNECTION_OPENING:
    /* wait for it to open */
    break;
  case INF_XML_CONNECTION_OPEN:
    inf_test_traffic_replay_connection_process_next_line(user_data);
    break;
  case INF_XML_CONNECTION_CLOSING:
  case INF_XML_CONNECTION_CLOSED:
    fprintf(stderr, "Remote connection closed\n");
    inf_test_traffic_replay_connection_close(user_data);
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

  conn = g_slice_new(InfTestTrafficReplayConnection);
  replay = (InfTestTrafficReplay*)user_data;

  conn->replay = replay;
  conn->xmpp = xmpp;
  conn->expected_line = NULL;
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
      inf_test_traffic_replay_connection_process_next_line(conn);
    }
  }
}

static InfCertificateCredentials*
inf_test_traffic_replay_load_credentials(GError** error)
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

int main(int argc, char* argv[])
{
  InfTestTrafficReplay replay;
  InfdTcpServer* server;
  InfCertificateCredentials* creds;
  GError* error;

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

  creds = inf_test_traffic_replay_load_credentials(&error);
  if(!creds)
  {
    fprintf(stderr, "%s\n", error->message);
    g_error_free(error);
    return -1;
  }

  replay.io = inf_standalone_io_new();
  replay.xmpp = NULL;
  replay.filename = argv[1];
  replay.conns = NULL;

  /* Start a server listening on port 6524 */
  server = g_object_new(
    INFD_TYPE_TCP_SERVER,
    "io", replay.io,
    "local-address", NULL,
    "local-port", 6524,
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
    return -1;
  }

  g_object_unref(server);
  inf_standalone_io_loop(replay.io);
}
