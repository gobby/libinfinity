/* infinote - Collaborative notetaking application
 * Copyright (C) 2007 Armin Burgmeier
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
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <infinity/inf-xml-stream.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define BUFFER_SIZE 32

static void parse(InfXmlStream* stream,
                  const gchar* data,
                  gsize len)
{
  gsize bytes_read;
  gsize bytes_cur;

  xmlNodePtr node;
  GError* error;

  xmlDocPtr doc;
  xmlBufferPtr buffer;

  bytes_read = 0;
  bytes_cur = 0;
  error = NULL;

  while(bytes_read < len && (node = inf_xml_stream_parse(stream, data + bytes_read, len - bytes_read, &bytes_cur, &error)) != NULL)
  {
    bytes_read += bytes_cur;

    doc = xmlNewDoc((const xmlChar*)NULL);
    xmlDocSetRootElement(doc, node);

    buffer = xmlBufferCreate();
    xmlNodeDump(buffer, doc, node, 0, 0);

    printf("%.*s\n", xmlBufferLength(buffer), xmlBufferContent(buffer));

    xmlBufferFree(buffer);
    xmlFreeDoc(doc);
  }

  if(error)
  {
    fprintf(stderr, "Error: %s\n", error->message);
    g_error_free(error);
  }
}

int main(int argc, char* argv[])
{
  InfXmlStream* stream;
  char buffer[BUFFER_SIZE];
  size_t len;
  FILE* f;

  g_type_init();

  stream = inf_xml_stream_new();

  f = fopen("inf-test-stream.xml", "r");
  if(f == NULL)
  {
    fprintf(stderr, "%s\n", strerror(errno));
    return EXIT_FAILURE;
  }

  while((len = fread(buffer, 1, BUFFER_SIZE, f)) > 0)
    parse(stream, buffer, len);

  fclose(f);
  return 0;
}
