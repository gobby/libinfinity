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
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <libinfinity/common/inf-xml-util.h>
#include <libinfinity/common/inf-error.h>
#include <libinfinity/inf-i18n.h>

#include <stdlib.h>
#include <math.h> /* HUGE_VAL */
#include <errno.h>

static gboolean
inf_xml_util_string_to_int(const gchar* attribute,
                           const xmlChar* value,
                           gint* output,
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
  else if( (errno == ERANGE && converted == LONG_MAX) ||
          converted > (long)G_MAXINT)
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
  else if( (errno == ERANGE && converted == LONG_MIN) ||
            converted < (long)G_MININT)
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
  else if(errno == ERANGE || converted > (unsigned long)G_MAXUINT)
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
  char buffer[24];
  sprintf(buffer, "%d", value);

  xmlSetProp(xml, (const xmlChar*)attribute, (const xmlChar*)buffer);
}

void
inf_xml_util_set_attribute_uint(xmlNodePtr xml,
                                const gchar* attribute,
                                guint value)
{
  char buffer[24];
  sprintf(buffer, "%u", value);

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
