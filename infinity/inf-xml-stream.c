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

#include <libxml/parser.h>

#include <string.h>

typedef struct _InfXmlStreamPrivate InfXmlStreamPrivate;
struct _InfXmlStreamPrivate {
  xmlParserCtxtPtr parser;
  xmlNodePtr root;
  xmlNodePtr cur;

  guint fed; /* Amount of bytes fed to the parser */
  guint finish; /* Position where the root element was closed */
  xmlErrorPtr error;
};

#define INF_XML_STREAM_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TYPE_XML_STREAM, InfXmlStreamPrivate))

static GObjectClass* parent_class;

static void
inf_xml_stream_start_element(void* context,
                             const xmlChar* name,
                             const xmlChar** attrs)
{
  InfXmlStream* stream;
  InfXmlStreamPrivate* priv;
  xmlNodePtr node;

  const xmlChar** attr;
  const xmlChar* attr_name;
  const xmlChar* attr_value;

  stream = INF_XML_STREAM(context);
  priv = INF_XML_STREAM_PRIVATE(stream);

  node = xmlNewNode(NULL, name);

  if(attrs != NULL)
  {
    attr = attrs;
    while(*attr != NULL)
    {
      attr_name = *attr;
      ++ attr;

      attr_value = *attr;
      ++ attr;

      xmlNewProp(node, attr_name, attr_value);
    }
  }

  if(priv->root == NULL)
  {
    g_assert(priv->cur == NULL);

    priv->root = node;
    priv->cur = node;
  }
  else
  {
    g_assert(priv->cur != NULL);

    priv->cur = xmlAddChild(priv->cur, node);
  }
}

static void
inf_xml_stream_end_element(void* context,
                           const xmlChar* name)
{
  InfXmlStream* stream;
  InfXmlStreamPrivate* priv;

  stream = INF_XML_STREAM(context);
  priv = INF_XML_STREAM_PRIVATE(stream);

  g_assert(priv->cur != NULL);
  g_assert(strcmp((const char*)priv->cur->name, (const char*)name) == 0);

  priv->cur = priv->cur->parent;

  if(priv->cur == NULL)
  {
    /* This was the terminating element of this message, so remember the
     * position to not eat some bytes from the next message already. */
    priv->finish = xmlByteConsumed(priv->parser);
  }
}

static void
inf_xml_stream_warning(void* context,
                       const char* msg,
                       ...)
{
  va_list arglist;
  va_start(arglist, msg);
  fprintf(stderr, "XML warning: ");
  vfprintf(stderr, msg, arglist);
  va_end(arglist);
}

static void
inf_xml_stream_error(void* context,
                     const char* msg,
                     ...)
{
  InfXmlStream* stream;
  InfXmlStreamPrivate* priv;
  xmlErrorPtr error;

  stream = INF_XML_STREAM(context);
  priv = INF_XML_STREAM_PRIVATE(stream);

  error = xmlCtxtGetLastError(priv->parser);
  g_assert(error != NULL);

  /* Ignore "Content after end of document" error because we do not feed
   * the parser byte-wise and it might therefore happen that the parser
   * already receives chunks from the next message. */
  if(error->domain != XML_FROM_PARSER || error->code != XML_ERR_DOCUMENT_END)
  {
    priv->error = error;
    priv->finish = xmlByteConsumed(priv->parser);

    if(priv->root != NULL)
    {
      xmlFreeNode(priv->root);
      priv->root = NULL;

      g_assert(priv->cur != NULL);
      priv->cur = NULL;
    }
    else
    {
      g_assert(priv->cur == NULL);
    }
  }
}

static void
inf_xml_stream_fatal_error(void* context,
                           const char* msg,
                           ...)
{
  va_list arglist;
  va_start(arglist, msg);
  fprintf(stderr, "XML Fatal error: ");
  vfprintf(stderr, msg, arglist);
  va_end(arglist);
}

static xmlSAXHandler inf_xml_stream_handler = {
  NULL,                          /* internalSubset */
  NULL,                          /* isStandalone */
  NULL,                          /* hasInternalSubset */
  NULL,                          /* hasExternalSubset */
  NULL,                          /* resolveEntity */
  NULL,                          /* getEntity */
  NULL,                          /* entityDecl */
  NULL,                          /* notationDecl */
  NULL,                          /* attributeDecl */
  NULL,                          /* elementDecl */
  NULL,                          /* unparsedEntityDecl */
  NULL,                          /* setDocumentLocator */
  NULL,                          /* startDocument */
  NULL,                          /* endDocument */
  inf_xml_stream_start_element,  /* startElement */
  inf_xml_stream_end_element,    /* endElement */
  NULL,                          /* reference */
  NULL,                          /* characters */
  NULL,                          /* ignorableWhitespace */
  NULL,                          /* processingInstruction */
  NULL,                          /* comment */
  inf_xml_stream_warning,        /* warning */
  inf_xml_stream_error,          /* error */
  inf_xml_stream_fatal_error,    /* fatalError */
  NULL,                          /* getParameterEntity */
  NULL,                          /* cdataBlock */
  NULL,                          /* externalSubset */
  0,                             /* initialized */
  NULL,                          /* _private */
  NULL,                          /* startElementNs */
  NULL,                          /* endElementNs */
  NULL                           /* serror */
};

static void
inf_xml_stream_init(GTypeInstance* instance,
                    gpointer g_class)
{
  InfXmlStream* stream;
  InfXmlStreamPrivate* priv;

  stream = INF_XML_STREAM(instance);
  priv = INF_XML_STREAM_PRIVATE(stream);

  priv->parser = NULL;
  priv->root = NULL;
  priv->cur = NULL;

  priv->fed = 0;
  priv->finish = 0;
  priv->error = NULL;
}

static void
inf_xml_stream_finalize(GObject* object)
{
  InfXmlStream* stream;
  InfXmlStreamPrivate* priv;

  stream = INF_XML_STREAM(object);
  priv = INF_XML_STREAM_PRIVATE(stream);

  if(priv->root == NULL)
  {
    xmlFreeNode(priv->root);
    priv->root = NULL;
  }

  if(priv->parser)
  {
    xmlFreeParserCtxt(priv->parser);
    priv->parser = NULL;
  }

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_xml_stream_class_init(gpointer g_class,
                          gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfXmlStreamPrivate));

  object_class->finalize = inf_xml_stream_finalize;
}

GType
inf_xml_stream_get_type(void)
{
  static GType xml_stream_type = 0;

  if(!xml_stream_type)
  {
    static const GTypeInfo xml_stream_type_info = {
      sizeof(InfXmlStreamClass),  /* class_size */
      NULL,                               /* base_init */
      NULL,                               /* base_finalize */
      inf_xml_stream_class_init,  /* class_init */
      NULL,                               /* class_finalize */
      NULL,                               /* class_data */
      sizeof(InfXmlStream),       /* instance_size */
      0,                                  /* n_preallocs */
      inf_xml_stream_init,        /* instance_init */
      NULL                                /* value_table */
    };

    xml_stream_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfXmlStream",
      &xml_stream_type_info,
      0
    );
  }

  return xml_stream_type;
}

/** inf_xml_stream_new:
 *
 * Creates a new XML stream.
 *
 * Return Value: A new #InfXmlStream.
 **/
InfXmlStream*
inf_xml_stream_new(void)
{
  GObject* object;

  object = g_object_new(INF_TYPE_XML_STREAM, NULL);

  return INF_XML_STREAM(object);
}

/** inf_xml_stream_parse:
 *
 * @stream: A #InfXmlStream.
 * @data: Data to parse.
 * @len: Length of @data
 * @bytes_read: Output parameter to indicate how much of the data has been
 *              parsed.
 * @error Set if an error occurs.
 *
 * Parses the given XML input. If a XML message is complete, it is returned
 * and @bytes_read is updated to indicate how much input has been consumed.
 * If all input was consumed and the message was still not complete, the
 * function returns NULL and might be called again later with more input.
 * If an error occurs, the function returns NULL as well and @error is set.
 *
 * Return Value: The parsed message, or %NULL.
 **/
xmlNodePtr
inf_xml_stream_parse(InfXmlStream* stream,
                     const gchar* data,
                     gsize len,
                     gsize* bytes_read,
                     GError** error)
{
  InfXmlStreamPrivate* priv;
  xmlNodePtr node;
  long chars;

  g_return_val_if_fail(INF_IS_XML_STREAM(stream), NULL);
  g_return_val_if_fail(data != NULL, NULL);
  g_return_val_if_fail(len > 0, NULL);

  priv = INF_XML_STREAM_PRIVATE(stream);

  if(priv->parser == NULL)
  {
    priv->parser = xmlCreatePushParserCtxt(
      &inf_xml_stream_handler,
      stream,
      NULL,
      0,
      NULL
    );
  }

  chars = xmlByteConsumed(priv->parser);
  xmlParseChunk(priv->parser, data, len, 0);

  /* finish is set to non-null if a complete message has been parsed */
  if(priv->finish > 0)
  {
    if(priv->error != NULL)
    {
      g_set_error(
        error,
        g_quark_from_static_string("INF_XML_STREAM_ERROR"),
        priv->error->code,
        "%s",
        priv->error->message
      );

      /* The error is owned by priv->parser */
      priv->error = NULL;
      node = NULL;
    }
    else
    {
      g_assert(priv->root != NULL);
      g_assert(priv->cur == NULL);

      node = priv->root;
      priv->root = NULL;
    }

    *bytes_read = priv->finish - priv->fed;

    /* Reset for next call which parses nexn message */
    xmlFreeParserCtxt(priv->parser);
    priv->parser = NULL;

    priv->fed = 0;
    priv->finish = 0;

    return node;
  }
  else
  {
    priv->fed += len;
    *bytes_read = len;

    return NULL;
  }
}
