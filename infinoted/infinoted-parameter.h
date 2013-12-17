/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2013 Armin Burgmeier <armin@arbur.net>
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

typedef enum _InfinotedParameterType {
  INFINOTED_PARAMETER_INT,
  INFINOTED_PARAMETER_STRING,
  INFINOTED_PARAMETER_STRING_LIST
} InfinotedParameterType;

typedef union _InfinotedParameterValue InfinotedParameterValue;
union _InfinotedParameterValue {
  gint number;
  gchar* str;
  gchar** strv;
};

typedef struct _InfinotedParameterTypedValue InfinotedParameterTypedValue;
struct _InfinotedParameterTypedValue {
  InfinotedParameterType type;
  InfinotedParameterValue value;
};

typedef gboolean(*InfinotedParameterConvertFunc)(gpointer,
                                                 gpointer,
                                                 GError**);

typedef struct _InfinotedParameterInfo InfinotedParameterInfo;
struct _InfinotedParameterInfo {
  const char* name;
  InfinotedParameterType type;
  size_t offset;

  /* The conversion function validates and converts the value read from the
   * key file (either int, string or string list) into the target type. */
  InfinotedParameterConvertFunc convert;

  /* The following three options are only used for commandline option parsing,
   * but not for reading the option value from a configuration file. */
  char short_name;
  const char* description;
  const char* arg_description;
};

typedef enum _InfinotedParameterError {
  INFINOTED_PARAMETER_ERROR_INVALID_SECURITY_POLICY,
  INFINOTED_PARAMETER_ERROR_INVALID_PORT,
  INFINOTED_PARAMETER_ERROR_INVALID_INTERVAL
} InfinotedParameterError;

GQuark
infinoted_parameter_error_quark(void);

InfinotedParameterTypedValue*
infinoted_parameter_typed_value_new(void);

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
infinoted_parameter_convert_port(gpointer out,
                                 gpointer in,
                                 GError** error);

gboolean
infinoted_parameter_convert_interval(gpointer out,
                                     gpointer in,
                                     GError** error);

gboolean
infinoted_parameter_convert_security_policy(gpointer out,
                                            gpointer in,
                                            GError** error);

G_END_DECLS

#endif /* __INFINOTED_PARAMETER_H__ */

/* vim:set et sw=2 ts=2: */
