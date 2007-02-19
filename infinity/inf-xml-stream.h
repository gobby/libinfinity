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

#ifndef __INF_XML_STREAM_H__
#define __INF_XML_STREAM_H__

#include <libxml/tree.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_XML_STREAM                 (inf_xml_stream_get_type())
#define INF_XML_STREAM(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_XML_STREAM, InfXmlStream))
#define INF_XML_STREAM_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TYPE_XML_STREAM, InfXmlStreamClass))
#define INF_IS_XML_STREAM(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_XML_STREAM))
#define INF_IS_XML_STREAM_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TYPE_XML_STREAM))
#define INF_XML_STREAM_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TYPE_XML_STREAM, InfXmlStreamClass))

typedef struct _InfXmlStream InfXmlStream;
typedef struct _InfXmlStreamClass InfXmlStreamClass;

struct _InfXmlStreamClass {
  GObjectClass parent_class;
};

struct _InfXmlStream {
  GObject parent;
};

GType
inf_xml_stream_get_type(void) G_GNUC_CONST;

InfXmlStream*
inf_xml_stream_new(void);

xmlNodePtr
inf_xml_stream_parse(InfXmlStream* stream,
                     const gchar* data,
                     gsize len,
                     gsize* bytes_read,
                     GError** error);

G_END_DECLS

#endif /* __INF_XML_STREAM_H__ */
