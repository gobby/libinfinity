/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2015 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INFINOTED_PARAMETER_H__
#define __INFINOTED_PARAMETER_H__

#include <libinfinity/common/inf-xmpp-connection.h>
#include <libinfinity/inf-config.h>

#include <glib.h>

G_BEGIN_DECLS

#define INFINOTED_PARAMETER_TYPED_VALUE_TYPE        (infinoted_parameter_typed_value_get_type())

/**
 * InfinotedParameterType:
 * @INFINOTED_PARAMETER_BOOLEAN: A boolean parameter.
 * @INFINOTED_PARAMETER_INT: A signed integer parameter.
 * @INFINOTED_PARAMETER_STRING: A string parameter.
 * @INFINOTED_PARAMETER_STRING_LIST: An array of strings.
 *
 * Allowed types for a parameter that can be given to a infinoted plugin.
 */
typedef enum _InfinotedParameterType {
  INFINOTED_PARAMETER_BOOLEAN,
  INFINOTED_PARAMETER_INT,
  INFINOTED_PARAMETER_STRING,
  INFINOTED_PARAMETER_STRING_LIST
} InfinotedParameterType;

/**
 * InfinotedParameterFlags:
 * @INFINOTED_PARAMETER_REQUIRED: The parameter is required and cannot be
 * omitted.
 *
 * Additional flags for parameters that can be given to infinoted plugins.
 */
typedef enum _InfinotedParameterFlags {
  INFINOTED_PARAMETER_REQUIRED = 1 << 0
} InfinotedParameterFlags;

/**
 * InfinotedParameterValue:
 * @yesno: The parameter value for type %INFINOTED_PARAMETER_BOOLEAN.
 * @number: The parameter value for type %INFINOTED_PARAMETER_INT.
 * @str: The parameter value for type %INFINOTED_PARAMETER_STRING.
 * @strv: The parameter value for type %INFINOTED_PARAMETER_STRING_LIST.
 *
 * Holds the value of a infinoted parameter. The type of the parameter must
 * be known. See also #InfinotedParameterTypedValue.
 */
typedef union _InfinotedParameterValue InfinotedParameterValue;
union _InfinotedParameterValue {
  gboolean yesno;
  gint number;
  gchar* str;
  gchar** strv;
};

/**
 * InfinotedParameterTypedValue:
 * @type: The type of the parameter.
 * @value: The value of the parameter.
 *
 * Holds the type and value of a parameter that can be passed to an
 * infinoted plugin.
 */
typedef struct _InfinotedParameterTypedValue InfinotedParameterTypedValue;
struct _InfinotedParameterTypedValue {
  InfinotedParameterType type;
  InfinotedParameterValue value;
};

/**
 * InfinotedParameterConvertFunc:
 * @out: Location where the converted value should be written to.
 * @in: Location where the original input value should be taken from.
 * @error: Location for error information, if any, or %NULL.
 *
 * Definition of a parameter conversion function. A parameter conversion
 * function transforms the value of a read which is one of the
 * @InfinotedParameterValue enumeration to its final internal representation.
 * It can change the C type of the parameter, and it can also validate the
 * input and produce an error if the input value is invalid.
 *
 * While plugin developers can write their own conversion functions, many are
 * already provided by libinfinoted-plugin-manager that cover the most basic
 * usecases. These functions are 
 * infinoted_parameter_convert_string(),
 * infinoted_parameter_convert_string_list(),
 * infinoted_parameter_convert_filename(),
 * infinoted_parameter_convert_boolean(),
 * infinoted_parameter_convert_port(),
 * infinoted_parameter_convert_nonnegative(),
 * infinoted_parameter_convert_positive(), and
 * infinoted_parameter_convert_security_policy().
 *
 * Returns: %TRUE on success or %FALSE if an error occurred.
 */
typedef gboolean(*InfinotedParameterConvertFunc)(gpointer out,
                                                 gpointer in,
                                                 GError** error);

/**
 * InfinotedParameterInfo:
 * @name: The name of the parameter.
 * @type: The input type of the parameter.
 * @flags: Additional flags for the parameter.
 * @offset: Offset of the output value in the structure of the plugin. Should
 * be determined with %G_STRUCT_OFFSET.
 * @convert: The conversion function for the parameter, see
 * #InfinotedParameterConvertFunc.
 * @short_name: A short name (one character) for the parameter, used for
 * command line option parsing.
 * @description: A description for the parameter that can be shown in
 * <literal>--help</literal> output
 * @arg_description: A description for the argument of the parameter in
 * <literal>--help</literal> output, if any.
 *
 * This structure contains generic information about a parameter that can
 * be passed to an infinoted plugin.
 */
typedef struct _InfinotedParameterInfo InfinotedParameterInfo;
struct _InfinotedParameterInfo {
  const char* name;
  InfinotedParameterType type;
  InfinotedParameterFlags flags;
  size_t offset;
  InfinotedParameterConvertFunc convert;
  char short_name;
  const char* description;
  const char* arg_description;
};

/**
 * InfinotedParameterError:
 * @INFINOTED_PARAMETER_ERROR_REQUIRED: A parameter is required but was not
 * provided to the plugin.
 * @INFINOTED_PARAMETER_ERROR_INVALID_NUMBER: The number given as a parameter
 * is not valid, for example a negative time interval.
 * @INFINOTED_PARAMETER_ERROR_INVALID_FLAG: The flag with the given name does
 * not exist.
 * @INFINOTED_PARAMETER_ERROR_INVALID_SECURITY_POLICY: A security policy given
 * as a parameter is not valid. The only allowed values are
 * &quot;no-tls&quot;, &quot;allow-tls&quot;, and &quot;require-tls&quot;.
 *
 * Specifies the possible error conditions for errors in the
 * <literal>INFINOTED_PARAMETER_ERROR</literal> domain. These typically
 * occur when parsing and processing input parameters for plugins.
 */
typedef enum _InfinotedParameterError {
  INFINOTED_PARAMETER_ERROR_REQUIRED,
  INFINOTED_PARAMETER_ERROR_INVALID_NUMBER,
  INFINOTED_PARAMETER_ERROR_INVALID_FLAG,
  INFINOTED_PARAMETER_ERROR_INVALID_SECURITY_POLICY
} InfinotedParameterError;

GQuark
infinoted_parameter_error_quark(void);

GType
infinoted_parameter_typed_value_get_type(void) G_GNUC_CONST;

InfinotedParameterTypedValue*
infinoted_parameter_typed_value_new(void);

InfinotedParameterTypedValue*
infinoted_parameter_typed_value_copy(const InfinotedParameterTypedValue* val);

void
infinoted_parameter_typed_value_free(gpointer data);

gboolean
infinoted_parameter_load_from_key_file(const InfinotedParameterInfo* infos,
                                       GKeyFile* key_file,
                                       const gchar* group,
                                       gpointer base,
                                       GError** error);

gboolean
infinoted_parameter_convert_string(gpointer out,
                                   gpointer in,
                                   GError** error);

gboolean
infinoted_parameter_convert_string_list(gpointer out,
                                        gpointer in,
                                        GError** error);

gboolean
infinoted_parameter_convert_filename(gpointer out,
                                     gpointer in,
                                     GError** error);

gboolean
infinoted_parameter_convert_boolean(gpointer out,
                                    gpointer in,
                                    GError** error);

gboolean
infinoted_parameter_convert_port(gpointer out,
                                 gpointer in,
                                 GError** error);

gboolean
infinoted_parameter_convert_nonnegative(gpointer out,
                                        gpointer in,
                                        GError** error);

gboolean
infinoted_parameter_convert_positive(gpointer out,
                                     gpointer in,
                                     GError** error);

gboolean
infinoted_parameter_convert_security_policy(gpointer out,
                                            gpointer in,
                                            GError** error);

gboolean
infinoted_parameter_convert_flags(gpointer out,
                                  gpointer in,
                                  const GFlagsValue* values,
                                  GError** error);

G_END_DECLS

#endif /* __INFINOTED_PARAMETER_H__ */

/* vim:set et sw=2 ts=2: */
