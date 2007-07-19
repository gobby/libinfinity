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

#include <libinfinity/common/inf-xml-util.h>
#include <libinfinity/common/inf-error.h>

#include <errno.h>

static gboolean
inf_xml_util_string_to_uint(const xmlChar* value,
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
      "Attribute '%s' does not contain a valid number",
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
      "Attribute '%s' causes overflow (%s)",
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
      "Request does not contain required attribute '%s'",
      attribute
    );
  }

  return value;
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
  if(value == NULL) return 0;

  retval = inf_xml_util_string_to_uint(value, result, error);
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

  retval = inf_xml_util_string_to_uint(value, result, error);
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
inf_xml_util_set_attribute_uint(xmlNodePtr xml,
                                const gchar* attribute,
				guint value)
{
  char buffer[24];
  sprintf(buffer, "%u", value);

  xmlSetProp(xml, (const xmlChar*)attribute, (const xmlChar*)buffer);
}
