/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2010 Armin Burgmeier <armin@arbur.net>
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

/**
 * SECTION:inf-xml-util
 * @title: XML utility functions
 * @short_description: Helper functions to read basic data types from XML
 * @include: libinfinity/common/inf-xml-util.h
 * @stability: Unstable
 *
 * In the infinote protocol XML attributes are often required to contain
 * numbers. These function provide some convenience to set and retrieve them.
 * They are mostly used in libinfinity itself but can also be useful when
 * implementing new session types so they are public API.
 **/

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

static gboolean
inf_xml_util_valid_xml_char(gunichar codepoint)
{
  /* cf. http://www.w3.org/TR/REC-xml/#dt-text */
  return
    (codepoint >= 0x00020 && codepoint <= 0x00d7ff) /* probably most common */
    || codepoint == 0xd
    || codepoint == 0xa
    || codepoint == 0x9
    || (codepoint >= 0x0e000 && codepoint <= 0x00fffd)
    || (codepoint >= 0x10000 && codepoint <= 0x10ffff);
}

/* like the g_utf8_next_char macro, but without the cast to char* at the end
 */
#define inf_utf8_next_char(p) ((p) + g_utf8_skip[*(const guchar *)(p)])

/**
 * inf_xml_util_add_child_text:
 * @xml: A #xmlNodePtr.
 * @text: The child text to add.
 * @bytes: The number of bytes of @text.
 *
 * Adds the given text as child text to @xml in the same way
 * xmlNodeAddContentLen() would do. The difference is that @text is allowed
 * to contain characters that are not valid in
 * <ulink url="http://www.w3.org/TR/REC-xml/#dt-text">XML text</ulink>, such
 * as formfeed characters \f. In case one occurs in @text, the function adds
 * an &lt;uchar /&gt; element node instead to @xml as specified in the
 * infinote protocol.
 */
void
inf_xml_util_add_child_text(xmlNodePtr xml,
                            const gchar* text,
                            gsize bytes)
{
  const gchar* p;
  const gchar* next;
  gchar* node_value;
  xmlNodePtr child_node;
  gunichar ch;
  gsize i;
  for(i = 0, p = text; i < bytes; i += next - p, p = next)
  {
    next = inf_utf8_next_char(p);
    ch = g_utf8_get_char(p);
    if(!inf_xml_util_valid_xml_char(ch))
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

/**
 * inf_xml_util_get_child_text:
 * @xml: A #xmlNodePtr
 * @bytes: Location to store number of bytes of child text, or %NULL.
 * @chars: Location to store number of characters of child text, or %NULL.
 * @error: Locatian to store error information if any, or %NULL.
 *
 * Reads a node's child text. If there are &lt;uchar /&gt; child elements, as
 * added by inf_xml_util_add_child_text() this function will convert them
 * back to character codes. There should not be any other child elements in
 * @xml.
 *
 * Returns: The node's child text, or %NULL on error. Free with g_free() when
 * no longer needed.
 */
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

/**
 * inf_xml_util_get_attribute:
 * @xml: A #xmlNodePtr.
 * @attribute: The name of the attribute to query.
 *
 * Returns the value of the attribute called @attribute in the XML element
 * @xml. This function is a thin wrapper around xmlGetProp() which exists
 * mostly for consistency, and for not having to cast the @attribute argument
 * from char* to xmlChar*. The return value is a xmlChar*, though.
 *
 * Returns: The value of the attribute, or %NULL. Free with xmlFree() when no
 * longer needed.
 */
xmlChar*
inf_xml_util_get_attribute(xmlNodePtr xml,
                           const gchar* attribute)
{
  return xmlGetProp(xml, (const xmlChar*)attribute);
}

/**
 * inf_xml_util_get_attribute_required:
 * @xml: A #xmlNodePtr.
 * @attribute: The name of the attribute to query.
 * @error: Location to store error information, if any.
 *
 * Returns the value of the attribute called @attribute in the XML element
 * @xml. If there is no such attribute then the function returns %NULL and
 * @error is set.
 *
 * Returns: The attribute's value, or %NULL on error. Free with xmlFree()
 * when no longer needed.
 */
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

/**
 * inf_xml_util_get_attribute_int:
 * @xml: A #xmlNodePtr.
 * @attribute: The name of the attribute to query.
 * @result: Location to store the read value.
 * @error: Location to store error information, if any.
 *
 * Reads the attribute named @attribute from the XML element @xml. The
 * attribute value is expected to be a signed integral number. If it is the
 * function converts the text to an integere and stores the result into
 * @result. In this case, %TRUE is returned and @error is left untouched.
 *
 * If the value is not a signed integral number, then the function returns
 * %FALSE, @error is set and @result is left untouched.
 *
 * If the attribute does not exist the function returns %FALSE but @error is
 * not set.
 *
 * Returns: Whether @result was set.
 */
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

/**
 * inf_xml_util_get_attribute_int_required:
 * @xml: A #xmlNodePtr.
 * @attribute: The name of the attribute to query.
 * @result: Location to store the read value.
 * @error: Location to store error information, if any.
 *
 * Reads the attribute named @attribute from the XML element @xml. The
 * attribute value is expected to be a signed integral number. If it is the
 * function converts the text to an integere and stores the result into
 * @result. In this case, %TRUE is returned and @error is left untouched.
 *
 * If the value is not a signed integral number or the attribute does not
 * exist, then the function returns %FALSE, @error is set and @result is
 * left untouched.
 *
 * Returns: Whether @result was set.
 */
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

/**
 * inf_xml_util_get_attribute_long:
 * @xml: A #xmlNodePtr.
 * @attribute: The name of the attribute to query.
 * @result: Location to store the read value.
 * @error: Location to store error information, if any.
 *
 * Behaves exactly like inf_xml_util_get_attribute_int(). The only difference
 * is that the function reads a signed long integral number.
 *
 * Returns: Whether @result was set.
 */
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

/**
 * inf_xml_util_get_attribute_long_required:
 * @xml: A #xmlNodePtr.
 * @attribute: The name of the attribute to query.
 * @result: Location to store the read value.
 * @error: Location to store error information, if any.
 *
 * Behaves exactly like inf_xml_util_get_attribute_int_required(). The only
 * difference is that the function reads a signed long integral number.
 *
 * Returns: Whether @result was set.
 */
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

/**
 * inf_xml_util_get_attribute_uint:
 * @xml: A #xmlNodePtr.
 * @attribute: The name of the attribute to query.
 * @result: Location to store the read value.
 * @error: Location to store error information, if any.
 *
 * Behaves exactly like inf_xml_util_get_attribute_int(). The only difference
 * is that the function reads an unsigned integral number.
 *
 * Returns: Whether @result was set.
 */
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

/**
 * inf_xml_util_get_attribute_uint_required:
 * @xml: A #xmlNodePtr.
 * @attribute: The name of the attribute to query.
 * @result: Location to store the read value.
 * @error: Location to store error information, if any.
 *
 * Behaves exactly like inf_xml_util_get_attribute_int_required(). The only
 * difference is that the function reads an unsigned integral number.
 *
 * Returns: Whether @result was set.
 */
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

/**
 * inf_xml_util_get_attribute_ulong:
 * @xml: A #xmlNodePtr.
 * @attribute: The name of the attribute to query.
 * @result: Location to store the read value.
 * @error: Location to store error information, if any.
 *
 * Behaves exactly like inf_xml_util_get_attribute_int(). The only difference
 * is that the function reads an unsigned long integral number.
 *
 * Returns: Whether @result was set.
 */
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

/**
 * inf_xml_util_get_attribute_ulong_required:
 * @xml: A #xmlNodePtr.
 * @attribute: The name of the attribute to query.
 * @result: Location to store the read value.
 * @error: Location to store error information, if any.
 *
 * Behaves exactly like inf_xml_util_get_attribute_int_required(). The only
 * difference is that the function reads an unsigned long integral number.
 *
 * Returns: Whether @result was set.
 */
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

/**
 * inf_xml_util_get_attribute_double:
 * @xml: A #xmlNodePtr.
 * @attribute: The name of the attribute to query.
 * @result: Location to store the read value.
 * @error: Location to store error information, if any.
 *
 * Behaves exactly like inf_xml_util_get_attribute_int(). The only difference
 * is that the function reads a double-precision floating point number.
 *
 * Returns: Whether @result was set.
 */
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

/**
 * inf_xml_util_get_attribute_double_required:
 * @xml: A #xmlNodePtr.
 * @attribute: The name of the attribute to query.
 * @result: Location to store the read value.
 * @error: Location to store error information, if any.
 *
 * Behaves exactly like inf_xml_util_get_attribute_int_required(). The only
 * difference is that the function reads a double-precision floating point
 * number.
 *
 * Returns: Whether @result was set.
 */
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

/**
 * inf_xml_util_set_attribute:
 * @xml: A #xmlNodePtr.
 * @attribute: The name of the attribute to set.
 * @value: The value to set.
 *
 * Sets the attribute named @attribute to the given value of the XML element
 * @xml. This is a thin wrapper around xmlSetProp(), mainly provided for
 * consistency and for not having to cast the arguments to xmlChar*.
 */
void
inf_xml_util_set_attribute(xmlNodePtr xml,
                           const gchar* attribute,
                           const gchar* value)
{
  xmlSetProp(xml, (const xmlChar*)attribute, (const xmlChar*)value);
}

/**
 * inf_xml_util_set_attribute_int:
 * @xml: A #xmlNodePtr.
 * @attribute: The name of the attribute to set.
 * @value: The value to set.
 *
 * Sets the attribute named @attribute to the given signed integral value
 * converted to text.
 */
void
inf_xml_util_set_attribute_int(xmlNodePtr xml,
                               const gchar* attribute,
                               gint value)
{
  char buffer[sizeof(gint) * 3];
  sprintf(buffer, "%d", value);

  xmlSetProp(xml, (const xmlChar*)attribute, (const xmlChar*)buffer);
}

/**
 * inf_xml_util_set_attribute_long:
 * @xml: A #xmlNodePtr.
 * @attribute: The name of the attribute to set.
 * @value: The value to set.
 *
 * Sets the attribute named @attribute to the given signed long integral value
 * converted to text.
 */
void
inf_xml_util_set_attribute_long(xmlNodePtr xml,
                                const gchar* attribute,
                                glong value)
{
  char buffer[sizeof(glong) * 3];
  sprintf(buffer, "%ld", value);

  xmlSetProp(xml, (const xmlChar*)attribute, (const xmlChar*)buffer);
}

/**
 * inf_xml_util_set_attribute_uint:
 * @xml: A #xmlNodePtr.
 * @attribute: The name of the attribute to set.
 * @value: The value to set.
 *
 * Sets the attribute named @attribute to the given unsigned integral value
 * converted to text.
 */
void
inf_xml_util_set_attribute_uint(xmlNodePtr xml,
                                const gchar* attribute,
                                guint value)
{
  char buffer[sizeof(guint) * 3];
  sprintf(buffer, "%u", value);

  xmlSetProp(xml, (const xmlChar*)attribute, (const xmlChar*)buffer);
}

/**
 * inf_xml_util_set_attribute_ulong:
 * @xml: A #xmlNodePtr.
 * @attribute: The name of the attribute to set.
 * @value: The value to set.
 *
 * Sets the attribute named @attribute to the given unsigned long integral
 * value converted to text.
 */
void
inf_xml_util_set_attribute_ulong(xmlNodePtr xml,
                                 const gchar* attribute,
                                 gulong value)
{
  char buffer[sizeof(gulong) * 3];
  sprintf(buffer, "%lu", value);

  xmlSetProp(xml, (const xmlChar*)attribute, (const xmlChar*)buffer);
}

/**
 * inf_xml_util_set_attribute_double:
 * @xml: A #xmlNodePtr.
 * @attribute: The name of the attribute to set.
 * @value: The value to set.
 *
 * Sets the attribute named @attribute to the given double-precision
 * floating point number converted to text.
 */
void
inf_xml_util_set_attribute_double(xmlNodePtr xml,
                                  const gchar* attribute,
                                  gdouble value)
{
  char buffer[G_ASCII_DTOSTR_BUF_SIZE];
  g_ascii_dtostr(buffer, G_ASCII_DTOSTR_BUF_SIZE, value);
  xmlSetProp(xml, (const xmlChar*)attribute, (const xmlChar*)buffer);
}

/**
 * inf_xml_util_new_node_from_error:
 * @error: The error object to represent in xml.
 * @name_space: The element's namespace, or %NULL.
 * @name: An element name, or %NULL.
 *
 * Creates a new #xmlNode that encodes @error. The element's name is
 * optionally specified by @name, or "error" by default, @error's domain
 * and code are set as attributes and its message is set as child text using
 * inf_xml_util_add_child_text(). @name_space is set as the element's
 * namespace, if not %NULL.
 *
 * Returns: A new #xmlNodePtr. It is the caller's responsibility to dispose it
 * using xmlFreeNode().
 */
xmlNodePtr
inf_xml_util_new_node_from_error(GError* error,
                                 xmlNsPtr name_space,
                                 const gchar* name)
{
  xmlNodePtr xml;

  if(name == NULL)
    name = "error";

  xml = xmlNewNode(name_space, (const xmlChar*) name);

  inf_xml_util_set_attribute_int(xml, "code", error->code);
  xmlNewProp(
    xml,
    (const xmlChar*) "domain",
    (const xmlChar*) g_quark_to_string(error->domain)
  );
  inf_xml_util_add_child_text(xml, error->message, strlen(error->message));

  return xml;
}

/**
 * inf_xml_util_new_error_from_node:
 * @xml: A #xmlNodePtr as returned by inf_xml_util_new_node_from_error().
 *
 * Creates a new #GError with the domain and erro code given in @xml's
 * attributes. The message is parsed from the child text as with
 * inf_xml_util_get_child_text(). The element name and namespace are ignored.
 * If @xml does not have the attributes as expected, %NULL is returned.
 *
 * Returns: A pointer to a new #GError, or %NULL on failure. It is the
 * caller's responsibility to dispose the #GError object using g_error_free().
 */
GError*
inf_xml_util_new_error_from_node(xmlNodePtr xml)
{
  GError* result;
  xmlChar* domain_str;
  int code;

  if(!inf_xml_util_get_attribute_int(xml, "code", &code, NULL))
    return NULL;

  domain_str = xmlGetProp(xml, (const xmlChar*) "domain");
  if(domain_str == NULL)
    return NULL;
  result = g_slice_new(GError);
  result->code    = code;
  result->domain  = g_quark_from_string((const gchar*) domain_str);
  result->message = inf_xml_util_get_child_text(xml, NULL, NULL, NULL);

  xmlFree(domain_str);

  return result;
}

/* vim:set et sw=2 ts=2: */
