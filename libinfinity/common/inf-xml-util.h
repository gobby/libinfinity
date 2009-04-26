/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008, 2009 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INF_XML_UTIL_H__
#define __INF_XML_UTIL_H__

#include <glib/gtypes.h>
#include <glib/gerror.h>
#include <libxml/tree.h>

G_BEGIN_DECLS

xmlChar*
inf_xml_util_get_attribute(xmlNodePtr xml,
                           const gchar* attribute);

xmlChar*
inf_xml_util_get_attribute_required(xmlNodePtr xml,
                                    const gchar* attribute,
                                    GError** error);

gboolean
inf_xml_util_get_attribute_int(xmlNodePtr xml,
                               const gchar* attribute,
                               gint* result,
                               GError** error);

gboolean
inf_xml_util_get_attribute_int_required(xmlNodePtr xml,
                                        const gchar* attribute,
                                        gint* result,
                                        GError** error);

gboolean
inf_xml_util_get_attribute_long(xmlNodePtr xml,
                                const gchar* attribute,
                                glong* result,
                                GError** error);

gboolean
inf_xml_util_get_attribute_long_required(xmlNodePtr xml,
                                         const gchar* attribute,
                                         glong* result,
                                         GError** error);

gboolean
inf_xml_util_get_attribute_uint(xmlNodePtr xml,
                                const gchar* attribute,
                                guint* result,
                                GError** error);

gboolean
inf_xml_util_get_attribute_uint_required(xmlNodePtr xml,
                                         const gchar* attribute,
                                         guint* result,
                                         GError** error);

gboolean
inf_xml_util_get_attribute_ulong(xmlNodePtr xml,
                                 const gchar* attribute,
                                 gulong* result,
                                 GError** error);

gboolean
inf_xml_util_get_attribute_ulong_required(xmlNodePtr xml,
                                          const gchar* attribute,
                                          gulong* result,
                                          GError** error);

gboolean
inf_xml_util_get_attribute_double(xmlNodePtr xml,
                                  const gchar* attribute,
                                  gdouble* result,
                                  GError** error);

gboolean
inf_xml_util_get_attribute_double_required(xmlNodePtr xml,
                                           const gchar* attribute,
                                           gdouble* result,
                                           GError** error);

void
inf_xml_util_set_attribute(xmlNodePtr xml,
                           const gchar* attribute,
                           const gchar* value);

void
inf_xml_util_set_attribute_int(xmlNodePtr xml,
                               const gchar* attribute,
                               gint value);

void
inf_xml_util_set_attribute_long(xmlNodePtr xml,
                                const gchar* attribute,
                                glong value);

void
inf_xml_util_set_attribute_uint(xmlNodePtr xml,
                                const gchar* attribute,
                                guint value);

void
inf_xml_util_set_attribute_ulong(xmlNodePtr xml,
                                 const gchar* attribute,
                                 gulong value);

void
inf_xml_util_set_attribute_double(xmlNodePtr xml,
                                  const gchar* attribute,
                                  gdouble value);

G_END_DECLS

#endif /* __INF_XML_UTIL_H__ */

/* vim:set et sw=2 ts=2: */
