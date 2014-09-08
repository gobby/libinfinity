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

/**
 * SECTION:infinoted-parameter
 * @title: InfinotedParameter
 * @short_description: Declare parameters of infinoted plugins.
 * @include: infinoted/infinoted-parameter.h
 * @stability: Unstable
 *
 * The #InfinotedParameterInfo structure allows to declare a parameter that
 * can then be used as an input value to a plugin. While the types for input
 * data are limited, the mechanism allows to provide a powerful validation and
 * transformation function which can turn the input value to the final
 * internal representation in the plugin data structure.
 *
 * Parameters are declared by providing a #InfinotedParameterInfo structure,
 * and an array of such structures is then given to #InfinotedPlugin which
 * declares a plugin.
 */

#include "config.h"

#include <infinoted/infinoted-parameter.h>
#include <libinfinity/inf-i18n.h>

#include <string.h>

static void
infinoted_parameter_free_data(InfinotedParameterType type,
                              InfinotedParameterValue* value)
{
  switch(type)
  {
  case INFINOTED_PARAMETER_BOOLEAN:
  case INFINOTED_PARAMETER_INT:
    break;
  case INFINOTED_PARAMETER_STRING:
    g_free(value->str);
    break;
  case INFINOTED_PARAMETER_STRING_LIST:
    g_strfreev(value->strv);
    break;
  default:
    g_assert_not_reached();
    break;
  }
}

static gboolean
infinoted_parameter_load_one_from_key_file(const InfinotedParameterInfo* info,
                                           GKeyFile* key_file,
                                           const gchar* group,
                                           gpointer base,
                                           GError** error)
{
  GError* local_error;
  InfinotedParameterValue v;
  gpointer in;
  gpointer out;

  local_error = NULL;

  switch(info->type)
  {
  case INFINOTED_PARAMETER_BOOLEAN:
    v.yesno = g_key_file_get_boolean(
      key_file,
      group,
      info->name,
      &local_error
    );

    in = &v.yesno;
    break;
  case INFINOTED_PARAMETER_INT:
    v.number = g_key_file_get_integer(
      key_file,
      group,
      info->name,
      &local_error
    );

    in = &v.number;
    break;
  case INFINOTED_PARAMETER_STRING:
    v.str = g_key_file_get_string(
      key_file,
      group,
      info->name,
      &local_error
    );

    in = &v.str;
    break;
  case INFINOTED_PARAMETER_STRING_LIST:
    v.strv = g_key_file_get_string_list(
      key_file,
      group,
      info->name,
      NULL,
      &local_error
    );

    in = &v.strv;
    break;
  default:
    g_assert_not_reached();
    break;
  }

  if(local_error != NULL)
  {
    if(local_error->domain == G_KEY_FILE_ERROR &&
       (local_error->code == G_KEY_FILE_ERROR_GROUP_NOT_FOUND ||
        local_error->code == G_KEY_FILE_ERROR_KEY_NOT_FOUND))
    {
      g_error_free(local_error);
      local_error = NULL;

      if(info->flags & INFINOTED_PARAMETER_REQUIRED)
      {
        g_set_error(
          error,
          infinoted_parameter_error_quark(),
          INFINOTED_PARAMETER_ERROR_REQUIRED,
          _("The parameter \"%s\" is required"),
          info->name
        );

        return FALSE;
      }

      return TRUE;
    }

    g_propagate_error(error, local_error);
    return FALSE;
  }
  else
  {
    out = (char*)base + info->offset;
    info->convert(out, in, &local_error);

    infinoted_parameter_free_data(info->type, &v);

    if(local_error != NULL)
    {
      g_propagate_error(error, local_error);
      return FALSE;
    }

    return TRUE;
  }
}

/**
 * infinoted_parameter_error_quark:
 *
 * Returns the #GQuark for errors from the InfinotedParameter module.
 *
 * Returns: The error domain for the InfinotedParameter module.
 */
GQuark
infinoted_parameter_error_quark(void)
{
  return g_quark_from_static_string("INFINOTED_PARAMETER_ERROR");
}

/**
 * infinoted_parameter_typed_value_new:
 *
 * Creates a new instance of a @InfinotedParameterTypedValue. The new instance
 * will be uninitialized. Its @type and @value members need to be set before
 * the object can be used or given to infinoted_parameter_typed_value_free().
 *
 * Returns: A new #InfinotedParameterTypedValue. Free with
 * infinoted_parameter_typed_value_free() when no longer needed.
 */
InfinotedParameterTypedValue*
infinoted_parameter_typed_value_new(void)
{
  return g_slice_new(InfinotedParameterTypedValue);
}

/**
 * infinoted_parameter_typed_value_free:
 * @data: The #InfinotedParameterTypedValue to free.
 *
 * Frees an instance of #InfinotedParameterTypedValue. Formally the argument
 * is kept as a generic pointer so that this function can be used as a
 * #GDestroyNotify callback.
 *
 * Note that the #InfinotedParameterTypedValue needs to be correctly
 * initialized, i.e. its type must be set, before it can be freed.
 */
void
infinoted_parameter_typed_value_free(gpointer data)
{
  InfinotedParameterTypedValue* val;
  val = (InfinotedParameterTypedValue*)data;

  infinoted_parameter_free_data(val->type, &val->value);
  g_slice_free(InfinotedParameterTypedValue, val);
}

/**
 * infinoted_parameter_load_from_key_file:
 * @infos: A 0-terminated array of #InfinotedParameterInfo objects.
 * @key_file: The #GKeyFile to load parameter values from.
 * @group: The keyfile group to load the values from.
 * @base: The instance into which to write the read parameters.
 * @error: Location to store error information, if any, or %NULL.
 *
 * Attempts to read each parameter in @infos from @key_file and store them in
 * a user-specified structure @base. The @offset field of
 * #InfinotedParameterInfo specifies where inside @base the read parameter
 * value will be written, and the @convert field specifies a function which
 * converts the parameter type (integer, string or string list) into the
 * type of the field in the target structure.
 *
 * If the key file does not have an entry for one of the entries in @infos,
 * then the current value in the base structure is untouched. This allows
 * setting default values prior to calling this function.
 *
 * If the function fails, for example because the conversion into the target
 * type failed (which, in turn, might be due to invalid user input), %FALSE
 * is returned and @error is set.
 *
 * Returns: %TRUE on success, otherwise %FALSE.
 */
gboolean
infinoted_parameter_load_from_key_file(const InfinotedParameterInfo* infos,
                                       GKeyFile* key_file,
                                       const gchar* group,
                                       gpointer base,
                                       GError** error)
{
  const InfinotedParameterInfo* info;
  gboolean retval;

  for(info = infos; info->name != NULL; ++info)
  {
    retval = infinoted_parameter_load_one_from_key_file(
      info,
      key_file,
      group,
      base,
      error
    );

    if(!retval)
      return FALSE;
  }

  return TRUE;
}

/**
 * infinoted_parameter_convert_string:
 * @out: The pointer to the output string location.
 * @in: A pointer to the input string location.
 * @error: Location to store error information, if any, or %NULL.
 *
 * This is basically a no-op, moving the string from the @in location to the
 * @out location. In case @in points to the empty string, it is freed and the
 * output string is set to be %NULL.
 *
 * This is a #InfinotedParameterConvertFunc function that can be used for
 * strings that should not be processed further or validated.
 *
 * Returns: This function always returns %TRUE.
 */
gboolean
infinoted_parameter_convert_string(gpointer out,
                                   gpointer in,
                                   GError** error)
{
  gchar** out_str;
  gchar** in_str;

  out_str = (gchar**)out;
  in_str = (gchar**)in;

  /* free previous entry */
  g_free(*out_str);
  /* set new value */
  *out_str = *in_str;
  /* reset old value, to avoid it being freed */
  *in_str = NULL;

  /* Set empty strings to NULL */
  if(*out_str != NULL && **out_str == '\0')
  {
    g_free(*out_str);
    *out_str = NULL;
  }

  return TRUE;
}

/**
 * infinoted_parameter_convert_string_list:
 * @out: The pointer to the output string list.
 * @in: The pointer to the input string list.
 * @error: Location to store error information, if any, or %NULL.
 *
 * This is basically a no-op, moving the string list from the @in location to
 * the @out location. In case @in points to an empty string list, or to a
 * string list with only one entry which is the empty string, then the string
 * list is freed and the output string list is set to be %NULL.
 *
 * This is a #InfinotedParameterConvertFunc function that can be used for
 * string lists that should not be processed further or validated.
 *
 * Returns: This function always returns %TRUE.
 */
gboolean
infinoted_parameter_convert_string_list(gpointer out,
                                        gpointer in,
                                        GError** error)
{
  gchar*** out_str;
  gchar*** in_str;

  out_str = (gchar***)out;
  in_str = (gchar***)in;

  /* free previous entry */
  g_strfreev(*out_str);
  /* set new value */
  *out_str = *in_str;
  /* reset old value, to avoid it being freed */
  *in_str = NULL;

  /* Set empty string lists, or a string list with only one empty string,
   * to NULL. */
  if(*out_str != NULL)
  {
    if( (*out_str)[0] != NULL)
    {
      if(*(*out_str)[0] == '\0' && (*out_str)[1] == NULL)
      {
        g_free( (*out_str)[0]);
        (*out_str)[0] = NULL;
      }
    }

    if( (*out_str)[0] == NULL)
    {
      g_free(*out_str);
      *out_str = NULL;
    }
  }

  return TRUE;
}

/**
 * infinoted_parameter_convert_filename:
 * @out: The pointer to the output string location.
 * @in: A pointer to the input string location.
 * @error: Location to store error information, if any, or %NULL.
 *
 * This function converts the input string from UTF-8
 * to the Glib file name encoding.
 *
 * This is a #InfinotedParameterConvertFunc function that can be used for
 * strings that should be in Glib file name encoding format instead of
 * UTF-8.
 *
 * Returns: %TRUE on success, or %FALSE otherwise.
 */
gboolean
infinoted_parameter_convert_filename(gpointer out,
                                     gpointer in,
                                     GError** error)
{
  gchar** out_str;
  gchar** in_str;

  out_str = (gchar**)out;
  in_str = (gchar**)in;

  /* free previous entry */
  g_free(*out_str);

  if(*in_str != NULL && **in_str != '\0')
  {
    *out_str = g_filename_from_utf8(*in_str, -1, NULL, NULL, error);

    if(*out_str == NULL)
      return FALSE;
  }
  else
  {
    *out_str = NULL;
  }

  return TRUE;
}

/**
 * infinoted_parameter_convert_boolean:
 * @out: The pointer to the output #gboolean.
 * @in: The pointer to the input #gboolean.
 * @error: Location to store error information, if any, or %NULL.
 *
 * This function simply writes the boolean value from @in to @out without any
 * further validation.
 *
 * This is a #InfinotedParameterConvertFunc function that can be used for
 * boolean values.
 *
 * Returns: This function always returns %TRUE.
 */
gboolean
infinoted_parameter_convert_boolean(gpointer out,
                                    gpointer in,
                                    GError** error)
{
  gboolean value;
  value = *(gboolean*)in;
  *(gboolean*)out = value;
  return TRUE;
}

/**
 * infinoted_parameter_convert_port:
 * @out: The pointer to the output #guint.
 * @in: The pointer to the input #gint.
 * @error: Location to store error information, if any, or %NULL.
 *
 * This function validates the input number to be in the valid range for
 * TCP or UDP ports between 1 and 65535, and converts it to an unsigned
 * integer.
 *
 * This is a #InfinotedParameterConvertFunc function that can be used for
 * TCP or UDP port numbers.
 *
 * Returns: %TRUE on success, or %FALSE otherwise.
 */
gboolean
infinoted_parameter_convert_port(gpointer out,
                                 gpointer in,
                                 GError** error)
{
  gint number;
  number = *(gint*)in;

  if(number <= 0 || number > 0xffff)
  {
    g_set_error(
      error,
      infinoted_parameter_error_quark(),
      INFINOTED_PARAMETER_ERROR_INVALID_NUMBER,
      _("\"%d\" is not a valid port number. Port numbers range from "
        "1 to 65535"),
      number
    );

    return FALSE;
  }

  *(guint*)out = number;

  return TRUE;
}

/**
 * infinoted_parameter_convert_nonnegative:
 * @out: The pointer to the output #guint.
 * @in: The pointer to the input #gint.
 * @error: Location to store error information, if any, or %NULL.
 *
 * This function validates the input number to be non-negative, and
 * converts it into an unsigned integer.
 *
 * This is a #InfinotedParameterConvertFunc function that can be used for
 * any non-negative numbers.
 *
 * Returns: %TRUE on success, or %FALSE otherwise.
 */
gboolean
infinoted_parameter_convert_nonnegative(gpointer out,
                                        gpointer in,
                                        GError** error)
{
  gint number;
  number = *(gint*)in;

  if(number < 0)
  {
    g_set_error(
      error,
      infinoted_parameter_error_quark(),
      INFINOTED_PARAMETER_ERROR_INVALID_NUMBER,
      "%s",
      _("Number must not be negative")
    );

    return FALSE;
  }

  *(guint*)out = number;

  return TRUE;
}

/**
 * infinoted_parameter_convert_positive:
 * @out: The pointer to the output #guint.
 * @in: The pointer to the input #gint.
 * @error: Location to store error information, if any, or %NULL.
 *
 * This function validates the input number to be positve, i.e. greater than
 * zero, and converts it into an unsigned integer.
 *
 * This is a #InfinotedParameterConvertFunc function that can be used for
 * any non-negative numbers.
 *
 * Returns: %TRUE on success, or %FALSE otherwise.
 */
gboolean
infinoted_parameter_convert_positive(gpointer out,
                                     gpointer in,
                                     GError** error)
{
  gint number;
  number = *(gint*)in;

  if(number <= 0)
  {
    g_set_error(
      error,
      infinoted_parameter_error_quark(),
      INFINOTED_PARAMETER_ERROR_INVALID_NUMBER,
      "%s",
      _("Number must be positive")
    );

    return FALSE;
  }

  *(guint*)out = number;

  return TRUE;
}

/**
 * infinoted_parameter_convert_security_policy:
 * @out: The pointer to the output #InfXmppConnectionSecurityPolicy.
 * @in: The pointer to the input string location.
 * @error: Location to store error information, if any, or %NULL.
 *
 * Converts the string that @in points to to an
 * #InfXmppConnectionSecurityPolicy value, by requiring that it is either
 * "no-tls", "allow-tls" or "require-tls". If the string is none of these
 * three the function fails and @error is set.
 *
 * This is a #InfinotedParameterConvertFunc function that can be used for
 * fields of type #InfXmppConnectionSecurityPolicy.
 *
 * Returns: %TRUE on success, or %FALSE otherwise.
 */
gboolean
infinoted_parameter_convert_security_policy(gpointer out,
                                            gpointer in,
                                            GError** error)
{
  gchar** in_str;
  InfXmppConnectionSecurityPolicy* out_val;

  in_str = (gchar**)in;
  out_val = (InfXmppConnectionSecurityPolicy*)out;

  if(strcmp(*in_str, "no-tls") == 0)
  {
    *out_val = INF_XMPP_CONNECTION_SECURITY_ONLY_UNSECURED;
  }
  else if(strcmp(*in_str, "allow-tls") == 0)
  {
    *out_val = INF_XMPP_CONNECTION_SECURITY_BOTH_PREFER_TLS;
  }
  else if(strcmp(*in_str, "require-tls") == 0)
  {
    *out_val = INF_XMPP_CONNECTION_SECURITY_ONLY_TLS;
  }
  else
  {
    g_set_error(
      error,
      infinoted_parameter_error_quark(),
      INFINOTED_PARAMETER_ERROR_INVALID_SECURITY_POLICY,
      _("\"%s\" is not a valid security policy. Allowed values are "
        "\"no-tls\", \"allow-tls\" or \"require-tls\""),
      *in_str
    );

    return FALSE;
  }
  
  return TRUE;
}

/**
 * infinoted_parameter_convert_flags:
 * @out: The pointer to the output flags (a #gint).
 * @in: The pointer to the input string list.
 * @values: Allowed flag values.
 * @error: Location to store error information, if any, or %NULL.
 *
 * Converts the string list that @in points to to a bitmask. This function
 * can not directly be used as a convert function as expected by
 * #InfinotedParameter since it has an additional argument. However, an
 * actual convert function can make use of this function.
 *
 * Each string entry is interpreted as a value of a bitmask. The @values
 * list specifies which string constant corresponds to which flag value.
 *
 * Returns: %TRUE on success, or %FALSE otherwise.
 */
gboolean
infinoted_parameter_convert_flags(gpointer out,
                                  gpointer in,
                                  const GFlagsValue* values,
                                  GError** error)
{
  GString* error_string;
  const GFlagsValue* value;
  gchar*** in_str;
  gchar** cur;

  *(gint*)out = 0;
  in_str = (gchar***)in;

  if(*in_str != NULL)
  {
    for(cur = *in_str; *cur != NULL; ++cur)
    {
      if( (*cur)[0] == '\0') continue;

      for(value = values; value->value_name != NULL; ++value)
      {
        if(strcmp(*cur, value->value_nick) == 0 ||
           strcmp(*cur, value->value_name) == 0)
        {
          break;
        }
      }

      if(value->value_name == NULL)
      {
        error_string = g_string_sized_new(256);
        for(value = values; value->value_name != NULL; ++value)
        {
          if(error_string->len > 0)
            g_string_append(error_string, ", ");
          g_string_append(error_string, value->value_nick);
        }

        g_set_error(
          error,
          infinoted_parameter_error_quark(),
          INFINOTED_PARAMETER_ERROR_INVALID_FLAG,
          _("\"%s\" is not a valid value. Allowed values are: %s."),
          *cur,
          error_string->str
        );

        g_string_free(error_string, TRUE);
        return FALSE;
      }

      *(gint*)out |= value->value;
    }
  }

  return TRUE;
}

/* vim:set et sw=2 ts=2: */
