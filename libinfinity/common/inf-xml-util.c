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

#include <libinfinity/common/inf-xml-util.h>
#include <libinfinity/common/inf-error.h>
#include <libinfinity/inf-i18n.h>

#include <string.h>
#include <stdlib.h>
#include <math.h> /* HUGE_VAL */
#include <errno.h>

static gboolean
inf_xml_util_string_to_long(const gchar* attribute,
                            const xmlChar* value,
                            glong* output,
                            GError** error)
{
  long converted;
  char* endptr;

  errno = 0;
  converted = strtol((const char*)value, &endptr, 0);

  if(*value == '\0' || *endptr != '\0')
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_INVALID_NUMBER,
      _("Attribute '%s' does not contain a valid number"),
      attribute
    );

    return FALSE;
  }
  else if( (errno == ERANGE && converted == LONG_MAX))
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_INVALID_NUMBER,
      _("Attribute '%s' causes overflow (%s)"),
      attribute,
      (const gchar*)value
    );

    return FALSE;
  }
  else if( (errno == ERANGE && converted == LONG_MIN))
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_INVALID_NUMBER,
      _("Attribute '%s' causes underflow (%s)"),
      attribute,
      (const gchar*)value
    );

    return FALSE;
  }
  else
  {
    *output = converted;
    return TRUE;
  }
}

static gboolean
inf_xml_util_string_to_ulong(const gchar* attribute, 
                             const xmlChar* value,
                             gulong* output,
                             GError** error)
{
  unsigned long converted;
  char* endptr;

  errno = 0;
  converted = strtoul((const char*)value, &endptr, 0);

  if(*value == '\0' || *endptr != '\0')
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_INVALID_NUMBER,
      _("Attribute '%s' does not contain a valid number"),
      attribute
    );

    return FALSE;
  }
  else if(errno == ERANGE)
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_INVALID_NUMBER,
      _("Attribute '%s' causes overflow (%s)"),
      attribute,
      (const gchar*)value
    );

    return FALSE;
  }
  else
  {
    *output = converted;
    return TRUE;
  }
}

static gboolean
inf_xml_util_string_to_int(const gchar* attribute,
                           const xmlChar* value,
                           gint* output,
                           GError** error)
{
  glong converted;

  if(!inf_xml_util_string_to_long(attribute, value, &converted, error))
    return FALSE;

  if(converted > (long)G_MAXINT)
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_INVALID_NUMBER,
      _("Attribute '%s' causes overflow (%s)"),
      attribute,
      (const gchar*)value
    );

    return FALSE;
  }
  else if(converted < (long)G_MININT)
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_INVALID_NUMBER,
      _("Attribute '%s' causes underflow (%s)"),
      attribute,
      (const gchar*)value
    );

    return FALSE;
  }
  else
  {
    *output = (gint)converted;
    return TRUE;
  }
}

static gboolean
inf_xml_util_string_to_uint(const gchar* attribute, 
                            const xmlChar* value,
                            guint* output,
                            GError** error)
{
  unsigned long converted;

  if(!inf_xml_util_string_to_ulong(attribute, value, &converted, error))
    return FALSE;

  if(converted > (unsigned long)G_MAXUINT)
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_INVALID_NUMBER,
      _("Attribute '%s' causes overflow (%s)"),
      attribute,
      (const gchar*)value
    );

    return FALSE;
  }
  else
  {
    *output = (guint)converted;
    return TRUE;
  }
}

static gboolean
inf_xml_util_string_to_double(const gchar* attribute,
                              const xmlChar* value,
                              gdouble* output,
                              GError** error)
{
  double converted;
  char* endptr;

  errno = 0;
  converted = g_ascii_strtod((const char*)value, &endptr);

  if(*value == '\0' || *endptr != '\0')
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_INVALID_NUMBER,
      _("Attribute '%s' does not contain a valid number"),
      attribute
    );

    return FALSE;
  }
  else if(errno == ERANGE &&
          (converted == HUGE_VAL || converted == -HUGE_VAL))
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_INVALID_NUMBER,
      _("Attribute '%s' causes overflow (%s)"),
      attribute,
      (const gchar*)value
    );

    return FALSE;
  }
  else if(errno == ERANGE && converted == 0.0)
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_INVALID_NUMBER,
      _("Attribute '%s' causes underflow (%s)"),
      attribute,
      (const gchar*)value
    );

    return FALSE;
  }
  else
  {
    *output = converted;
    return TRUE;
  }
}

void
inf_xml_util_add_child_text(xmlNodePtr xml,
                            const gchar* text,
                            gsize bytes)
{
  const gchar* p;
  const gchar* next;
  gchar* node_value;
  xmlNodePtr child_node;
  gsize i;
  for(i = 0, p = text; i < bytes; i += next - p, p = next)
  {
    next = g_utf8_next_char(p);
    gunichar ch = g_utf8_get_char(p);
    if(!g_unichar_isprint(ch))
    {
      xmlNodeAddContentLen(xml, (const xmlChar*) text, p - text);
      child_node = xmlNewNode(NULL, (const xmlChar*)"uchar");
      node_value = g_strdup_printf("%"G_GUINT32_FORMAT, ch);
      xmlNewProp(child_node,
        (const xmlChar*) "codepoint",
        (const xmlChar*) node_value);
      g_free(node_value);
      xmlAddChild(xml, child_node);
      text = next;
    }
  }

  if(p != text)
    xmlNodeAddContentLen(xml, (const xmlChar*) text, p - text);
}

gchar*
inf_xml_util_get_child_text(xmlNodePtr xml,
                            gsize* bytes,
                            guint* chars,
                            GError** error)
{
  xmlNodePtr child;
  /* Every keypress will have to be get_child_text'ed, so
   * we assume that most child texts are very short. */
  GString* result = g_string_sized_new(16);
  guint num_codepoint;
  gsize char_count = 0;
  for(child = xml->children; child; child = child->next)
  {
    switch(child->type)
    {
    case XML_TEXT_NODE:
      g_string_append(result, (const gchar*)child->content);
      char_count += g_utf8_strlen((const gchar*)child->content, -1);
      break;
    case XML_ELEMENT_NODE:
      if(strcmp((const char*) child->name, "uchar") != 0) {
        g_warning("unexpected child element in child text: %s", child->name);
        break;
      }

      if(!inf_xml_util_get_attribute_uint_required(child,
                                                   "codepoint",
                                                   &num_codepoint,
                                                   error))
      {
        g_string_free(result, TRUE);
        return NULL;
      }
      g_string_append_unichar(result, (gunichar) num_codepoint);
      ++char_count;
      break;
    default:
      g_warning(
        "unexpected node type in child text: %d",
        (int) child->type);
      break;
    }
  }

  if(chars) *chars = char_count;
  if(bytes) *bytes = result->len;
  return g_string_free(result, FALSE);
}

xmlChar*
inf_xml_util_get_attribute(xmlNodePtr xml,
                           const gchar* attribute)
{
  return xmlGetProp(xml, (const xmlChar*)attribute);
}

xmlChar*
inf_xml_util_get_attribute_required(xmlNodePtr xml,
                                    const gchar* attribute,
                                    GError** error)
{
  xmlChar* value;
  value = xmlGetProp(xml, (const xmlChar*)attribute);

  if(value == NULL)
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_NO_SUCH_ATTRIBUTE,
      _("Request '%s' does not contain required attribute '%s'"),
      (const gchar*)xml->name,
      attribute
    );
  }

  return value;
}

gboolean
inf_xml_util_get_attribute_int(xmlNodePtr xml,
                               const gchar* attribute,
                               gint* result,
                               GError** error)
{
  xmlChar* value;
  gboolean retval;

  value = xmlGetProp(xml, (const xmlChar*)attribute);
  if(value == NULL) return FALSE;

  retval = inf_xml_util_string_to_int(attribute, value, result, error);
  xmlFree(value);
  return retval;
}

gboolean
inf_xml_util_get_attribute_int_required(xmlNodePtr xml,
                                        const gchar* attribute,
                                        gint* result,
                                        GError** error)
{
  xmlChar* value;
  gboolean retval;

  value = inf_xml_util_get_attribute_required(xml, attribute, error);
  if(value == NULL) return FALSE;

  retval = inf_xml_util_string_to_int(attribute, value, result, error);
  xmlFree(value);
  return retval;
}

gboolean
inf_xml_util_get_attribute_long(xmlNodePtr xml,
                                const gchar* attribute,
                                glong* result,
                                GError** error)
{
  xmlChar* value;
  gboolean retval;

  value = xmlGetProp(xml, (const xmlChar*)attribute);
  if(value == NULL) return FALSE;

  retval = inf_xml_util_string_to_long(attribute, value, result, error);
  xmlFree(value);
  return retval;
}

gboolean
inf_xml_util_get_attribute_long_required(xmlNodePtr xml,
                                         const gchar* attribute,
                                         glong* result,
                                         GError** error)
{
  xmlChar* value;
  gboolean retval;

  value = inf_xml_util_get_attribute_required(xml, attribute, error);
  if(value == NULL) return FALSE;

  retval = inf_xml_util_string_to_long(attribute, value, result, error);
  xmlFree(value);
  return retval;
}

gboolean
inf_xml_util_get_attribute_uint(xmlNodePtr xml,
                                const gchar* attribute,
                                guint* result,
                                GError** error)
{
  xmlChar* value;
  gboolean retval;

  value = xmlGetProp(xml, (const xmlChar*)attribute);
  if(value == NULL) return FALSE;

  retval = inf_xml_util_string_to_uint(attribute, value, result, error);
  xmlFree(value);
  return retval;
}

gboolean
inf_xml_util_get_attribute_uint_required(xmlNodePtr xml,
                                         const gchar* attribute,
                                         guint* result,
                                         GError** error)
{
  xmlChar* value;
  gboolean retval;

  value = inf_xml_util_get_attribute_required(xml, attribute, error);
  if(value == NULL) return FALSE;

  retval = inf_xml_util_string_to_uint(attribute, value, result, error);
  xmlFree(value);
  return retval;
}

gboolean
inf_xml_util_get_attribute_ulong(xmlNodePtr xml,
                                 const gchar* attribute,
                                 gulong* result,
                                 GError** error)
{
  xmlChar* value;
  gboolean retval;

  value = xmlGetProp(xml, (const xmlChar*)attribute);
  if(value == NULL) return FALSE;

  retval = inf_xml_util_string_to_ulong(attribute, value, result, error);
  xmlFree(value);
  return retval;
}

gboolean
inf_xml_util_get_attribute_ulong_required(xmlNodePtr xml,
                                          const gchar* attribute,
                                          gulong* result,
                                          GError** error)
{
  xmlChar* value;
  gboolean retval;

  value = inf_xml_util_get_attribute_required(xml, attribute, error);
  if(value == NULL) return FALSE;

  retval = inf_xml_util_string_to_ulong(attribute, value, result, error);
  xmlFree(value);
  return retval;
}

gboolean
inf_xml_util_get_attribute_double(xmlNodePtr xml,
                                  const gchar* attribute,
                                  gdouble* result,
                                  GError** error)
{
  xmlChar* value;
  gboolean retval;

  value = xmlGetProp(xml, (const xmlChar*)attribute);
  if(value == NULL) return FALSE;

  retval = inf_xml_util_string_to_double(attribute, value, result, error);
  xmlFree(value);
  return retval;
}

gboolean
inf_xml_util_get_attribute_double_required(xmlNodePtr xml,
                                           const gchar* attribute,
                                           gdouble* result,
                                           GError** error)
{
  xmlChar* value;
  gboolean retval;

  value = inf_xml_util_get_attribute_required(xml, attribute, error);
  if(value == NULL) return FALSE;

  retval = inf_xml_util_string_to_double(attribute, value, result, error);
  xmlFree(value);
  return retval;
}

void
inf_xml_util_set_attribute(xmlNodePtr xml,
                           const gchar* attribute,
                           const gchar* value)
{
  xmlSetProp(xml, (const xmlChar*)attribute, (const xmlChar*)value);
}

void
inf_xml_util_set_attribute_int(xmlNodePtr xml,
                               const gchar* attribute,
                               gint value)
{
  char buffer[sizeof(gint) * 3];
  sprintf(buffer, "%d", value);

  xmlSetProp(xml, (const xmlChar*)attribute, (const xmlChar*)buffer);
}

void
inf_xml_util_set_attribute_long(xmlNodePtr xml,
                                const gchar* attribute,
                                glong value)
{
  char buffer[sizeof(glong) * 3];
  sprintf(buffer, "%ld", value);

  xmlSetProp(xml, (const xmlChar*)attribute, (const xmlChar*)buffer);
}

void
inf_xml_util_set_attribute_uint(xmlNodePtr xml,
                                const gchar* attribute,
                                guint value)
{
  char buffer[sizeof(guint) * 3];
  sprintf(buffer, "%u", value);

  xmlSetProp(xml, (const xmlChar*)attribute, (const xmlChar*)buffer);
}

void
inf_xml_util_set_attribute_ulong(xmlNodePtr xml,
                                 const gchar* attribute,
                                 gulong value)
{
  char buffer[sizeof(gulong) * 3];
  sprintf(buffer, "%lu", value);

  xmlSetProp(xml, (const xmlChar*)attribute, (const xmlChar*)buffer);
}

void
inf_xml_util_set_attribute_double(xmlNodePtr xml,
                                  const gchar* attribute,
                                  gdouble value)
{
  char buffer[G_ASCII_DTOSTR_BUF_SIZE];
  g_ascii_dtostr(buffer, G_ASCII_DTOSTR_BUF_SIZE, value);
  xmlSetProp(xml, (const xmlChar*)attribute, (const xmlChar*)buffer);
}

/* vim:set et sw=2 ts=2: */
