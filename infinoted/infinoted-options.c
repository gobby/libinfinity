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

#include "config.h"

#include <infinoted/infinoted-options.h>
#include <infinoted/infinoted-util.h>
#include <libinfinity/inf-i18n.h>
#include <libinfinity/common/inf-protocol.h>

#ifdef LIBINFINITY_HAVE_LIBDAEMON
# include <libdaemon/dpid.h>
#endif

#include <glib.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

static const gchar INFINOTED_OPTIONS_GROUP[] = "infinoted";

/* TODO: Split the functionality to load key files as options into a separate
 * file, and make it public, so it can be used by plugins. Suggested name:
 * InfinotedParameter. Everything that has to do with command line parsing
 * and actual InfinotedOptions dependence stays here then. */

typedef enum _InfinotedOptionsEntryType {
  INFINOTED_OPTIONS_ENTRY_INT,
  INFINOTED_OPTIONS_ENTRY_STRING,
  INFINOTED_OPTIONS_ENTRY_STRING_LIST
} InfinotedOptionsEntryType;

/* One value for an option, as it comes either from the command line or the
 * configuration file. The conversion function (see below) transforms this into
 * the actual target type. */
typedef union _InfinotedOptionsValue InfinotedOptionsValue;
union _InfinotedOptionsValue {
  int number;
  gchar* str;
  gchar** strv;
};

typedef struct _InfinotedOptionsTypedValue InfinotedOptionsTypedValue;
struct _InfinotedOptionsTypedValue {
  InfinotedOptionsEntryType type;
  InfinotedOptionsValue value;
};

typedef struct _InfinotedOptionsEntry InfinotedOptionsEntry;
struct _InfinotedOptionsEntry {
  const char* name;
  InfinotedOptionsEntryType type;
  size_t offset;

  /* The conversion function validates and converts the value read from the
   * key file into the target field inside the InfinotedOptions structure. */
  gboolean(*convert)(gpointer out, gpointer in, GError** error);

  /* The following three options are only used for commandline option parsing,
   * but not for reading the option value from a configuration file. */
  char short_name;
  const char* description;
  const char* arg_description;
};

static InfinotedOptionsTypedValue*
infinoted_options_typed_value_new(void)
{
  return g_slice_new(InfinotedOptionsTypedValue);
}

static void
infinoted_options_typed_value_free(gpointer data)
{
  InfinotedOptionsTypedValue* val;
  val = (InfinotedOptionsTypedValue*)data;

  switch(val->type)
  {
  case INFINOTED_OPTIONS_ENTRY_INT:
    break;
  case INFINOTED_OPTIONS_ENTRY_STRING:
    g_free(val->value.str);
    break;
  case INFINOTED_OPTIONS_ENTRY_STRING_LIST:
    g_strfreev(val->value.strv);
    break;
  default:
    g_assert_not_reached();
    break;
  }

  g_slice_free(InfinotedOptionsTypedValue, val);
}

static gboolean
infinoted_options_convert_string(gpointer out,
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

static gboolean
infinoted_options_convert_string_list(gpointer out,
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

static gboolean
infinoted_options_convert_filename(gpointer out,
                                   gpointer in,
                                   GError** error)
{
  gchar** out_str;
  gchar** in_str;

  out_str = (gchar**)out;
  in_str = (gchar**)in;

  *out_str = NULL;
  if(*in_str != NULL && **in_str != '\0')
    *out_str = g_filename_from_utf8(*in_str, -1, NULL, NULL, error);

  return TRUE;
}

static gboolean
infinoted_options_convert_port(gpointer out,
                               gpointer in,
                               GError** error)
{
  int number;
  number = *(gint*)in;

  if(number <= 0 || number > 0xffff)
  {
    g_set_error(
      error,
      infinoted_options_error_quark(),
      INFINOTED_OPTIONS_ERROR_INVALID_PORT,
      _("\"%d\" is not a valid port number. Port numbers range from "
        "1 to 65535"),
      number
    );

    return FALSE;
  }

  *(guint*)out = number;
  return TRUE;
}

static gboolean
infinoted_options_convert_interval(gpointer out,
                                   gpointer in,
                                   GError** error)
{
  int number;
  number = *(gint*)in;

  if(number < 0)
  {
    g_set_error(
      error,
      infinoted_options_error_quark(),
      INFINOTED_OPTIONS_ERROR_INVALID_INTERVAL,
      "%s",
      _("Interval must not be negative")
    );

    return FALSE;
  }

  *(guint*)out = number;
  return TRUE;
}

static gboolean
infinoted_options_convert_security_policy(gpointer out,
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
    return TRUE;
  }
  else if(strcmp(*in_str, "allow-tls") == 0)
  {
    *out_val = INF_XMPP_CONNECTION_SECURITY_BOTH_PREFER_TLS;
    return TRUE;
  }
  else if(strcmp(*in_str, "require-tls") == 0)
  {
    *out_val = INF_XMPP_CONNECTION_SECURITY_ONLY_TLS;
    return TRUE;
  }
  else
  {
    g_set_error(
      error,
      infinoted_options_error_quark(),
      INFINOTED_OPTIONS_ERROR_INVALID_SECURITY_POLICY,
      _("\"%s\" is not a valid security policy. Allowed values are "
        "\"no-tls\", \"allow-tls\" or \"require-tls\""),
      *in_str
    );

    return FALSE;
  }
}

const InfinotedOptionsEntry INFINOTED_OPTIONS[] = {
  {
    "log-file",
    INFINOTED_OPTIONS_ENTRY_STRING,
    offsetof(InfinotedOptions, log_path),
    infinoted_options_convert_filename,
    'l',
    N_("If set, write the server log to the given file, "
       "in addition to stdout"),
    N_("LOG-FILE")
  }, {
    "key-file",
    INFINOTED_OPTIONS_ENTRY_STRING,
    offsetof(InfinotedOptions, key_file),
    infinoted_options_convert_filename,
    'k',
    N_("Path to the server's private key. Must be the key with which the "
       "given certificate was signed. Not needed when security-policy is "
       "set to \"no-tls\"."),
    N_("KEY-FILE")
  }, {
    "certificate-file",
    INFINOTED_OPTIONS_ENTRY_STRING,
    offsetof(InfinotedOptions, certificate_file),
    infinoted_options_convert_filename,
    'c',
    N_("Path to the server's certificate. Must be signed with the given key "
       "file. Not needed when security-policy is set to \"no-tls\"."),
    N_("CERT-FILE"),
  }, {
    "certificate-chain",
    INFINOTED_OPTIONS_ENTRY_STRING,
    offsetof(InfinotedOptions, certificate_chain_file),
    infinoted_options_convert_filename,
    0,
    N_("Optional file which contains the issuer certificate of the server "
       "certificate, and the issuer's issuer, and so on. This option can be "
       "used when the issuer certificates are not stored in the same file as "
       "the server certificate. If the issuer certificates are not available "
       "the server will still run, but not show the issuer certificates to "
       "connecting clients."),
    N_("CERT-FILE")
  }, {
    "port",
    INFINOTED_OPTIONS_ENTRY_INT,
    offsetof(InfinotedOptions, port),
    infinoted_options_convert_port,
    'p',
    N_("The TCP port number to listen on."),
    N_("PORT")
  }, {
    "security-policy",
    INFINOTED_OPTIONS_ENTRY_STRING,
    offsetof(InfinotedOptions, security_policy),
    infinoted_options_convert_security_policy,
    0,
    N_("Whether to use Transport Layer Security (TLS) or not. Allowed "
       "values are \"no-tls\", \"allow-tls\" or \"require-tls\". When "
       "TLS is allowed or required, a server certificate must be provided. "
       "Infinoted has a built-in option to create a self-signed certificate "
       "with the --create-key and --create-certificate command line options. "
       "When TLS is allowed but not required, clients may choose not to use "
       "TLS. It is strongly encouraged to always require TLS. "
       "[Default=require-tls]"),
    N_("no-tls|allow-tls|require-tls")
  }, {
    "root-directory",
    INFINOTED_OPTIONS_ENTRY_STRING,
    offsetof(InfinotedOptions, root_directory),
    infinoted_options_convert_filename,
    'r',
    N_("The directory which infinoted uses to permanantly store all "
       "documents on the server, and where they are read from after a "
       "server restart. [Default=~/.infinote]"),
    N_("DIRECTORY")
  }, {
    "autosave-hook", 
    INFINOTED_OPTIONS_ENTRY_STRING,
    offsetof(InfinotedOptions, autosave_hook),
    infinoted_options_convert_filename,
    0,
    N_("Command to run after having saved a document"),
    N_("PROGRAM")
  }, {
    "autosave-interval",
    INFINOTED_OPTIONS_ENTRY_INT,
    offsetof(InfinotedOptions, autosave_interval),
    infinoted_options_convert_interval,
    0,
    N_("Interval, in seconds, after which to save documents into the root "
       "directory. An interval of 0 disables autosave. In this case "
       "documents are only stored to disk when there has been no user "
       "logged into them for 60 seconds. [Default=0]"),
    N_("INTERVAL")
  }, {
    "password",
    INFINOTED_OPTIONS_ENTRY_STRING,
    offsetof(InfinotedOptions, password),
    infinoted_options_convert_string,
    'P',
    N_("If set, require clients to enter a password before being allowed "
       "to connect to the server. This option cannot be combined with "
       "--pam-service."),
    N_("Password")
#ifdef LIBINFINITY_HAVE_PAM
  }, {
    "pam-service",
    INFINOTED_OPTIONS_ENTRY_STRING,
    offsetof(InfinotedOptions, pam_service),
    infinoted_options_convert_string,
    0,
    N_("Authenticate clients using the given PAM service. This option cannot "
       "be combined with --password. Clients are requested to send their "
       "user name and then enter the password for their account on "
       "the server machine with the same name."),
    N_("SERVICE")
  }, {
    "pam-allow-user",
    INFINOTED_OPTIONS_ENTRY_STRING_LIST,
    offsetof(InfinotedOptions, pam_allowed_users),
    infinoted_options_convert_string_list,
    0,
    N_("If set, only the given username is allowed to connect to the "
       "server. This option can be given multiple times to allow multiple "
       "users."),
    N_("USER")
  }, {
    "pam-allow-group",
    INFINOTED_OPTIONS_ENTRY_STRING_LIST,
    offsetof(InfinotedOptions, pam_allowed_groups),
    infinoted_options_convert_string_list,
    0,
    N_("If set, only users belonging to the given group are allowed to "
       "connect to the server. This option can be given multiple times to "
       "allow multiple groups."),
    N_("GROUPS")
#endif
  }, {
    "ca-list-file",
    INFINOTED_OPTIONS_ENTRY_STRING,
    offsetof(InfinotedOptions, ca_list_file),
    infinoted_options_convert_filename,
    0,
    N_("If set, require clients to authenticate themselves by showing a "
       "client certificate issued by one of the CAs from this file."),
    N_("CA-FILE"),
  }, {
    "sync-directory",
    INFINOTED_OPTIONS_ENTRY_STRING,
    offsetof(InfinotedOptions, sync_directory),
    infinoted_options_convert_filename,
    0,
    N_("A directory, into which to periodically store a copy of the document "
       "tree in plain text, without any infinote metadata such as which user "
       "wrote what part of the document. The infinote metadata is still "
       "available in the root directory. This option can be used to "
       "(automatically) process the files on the server whenever they "
       "change. Document synchronization is disabled when this option is "
       "not set."),
    N_("DIRECTORY"),
  }, {
    "sync-interval",
    INFINOTED_OPTIONS_ENTRY_INT,
    offsetof(InfinotedOptions, sync_interval),
    infinoted_options_convert_interval,
    0,
    N_("Interval, in seconds, within which to store documents to the "
       "specified sync-directory. If the interval is 0, document "
       "synchronization is disabled. [Default=0]"),
    N_("INTERVAL")
  }, {
    "sync-hook",
    INFINOTED_OPTIONS_ENTRY_STRING,
    offsetof(InfinotedOptions, sync_hook),
    infinoted_options_convert_filename,
    0,
    N_("Command to run every time a copy of a document has been saved "
       "into the sync-directory."),
    N_("PROGRAM")
  }, {
    "max-transformation-vdiff",
    INFINOTED_OPTIONS_ENTRY_INT,
    offsetof(InfinotedOptions, max_transformation_vdiff),
    infinoted_options_convert_interval,
    0,
    N_("Maximum number of transformations allowed for one request. If "
       "processing a request would exceed this number of transformations, "
       "the connection is automatically unsubscribed from the document. "
       "The option can be used to prevent server overload from clients "
       "lagging very far behind, or from malicious clients. Set to 0 to "
       "process all transformations. [Default=0]"),
    N_("TRANSFORMATIONS")
  }, {
    "traffic-log-directory",
    INFINOTED_OPTIONS_ENTRY_STRING,
    offsetof(InfinotedOptions, traffic_log_directory),
    infinoted_options_convert_filename,
    0,
    N_("A directory into which to store the (decrypted) network traffic "
       "between the server and the clients, with one file for each "
       "connection. This option should only be used for debugging purposes, "
       "since it stores the unencrypted network traffic on the server's "
       "file system."),
    N_("DIRECTORY")
  }, {
    NULL,
    0,
    0,
    NULL
  }
};

static gboolean
infinoted_options_validate(InfinotedOptions* options,
                           GError** error)
{
  InfXmppConnectionSecurityPolicy security_policy;
  gboolean requires_password;

  security_policy = options->security_policy;

#ifdef LIBINFINITY_HAVE_PAM
  if(options->password != NULL && options->pam_service != NULL)
  {
    g_set_error(
      error,
      infinoted_options_error_quark(),
      INFINOTED_OPTIONS_ERROR_INVALID_AUTHENTICATION_SETTINGS,
      "%s",
      _("Cannot use both server password and system authentication.")
    );
    return FALSE;
  }

  if(options->pam_service == NULL
     && (options->pam_allowed_users != NULL
         || options->pam_allowed_groups != NULL))
  {
    g_set_error(
      error,
      infinoted_options_error_quark(),
      INFINOTED_OPTIONS_ERROR_INVALID_AUTHENTICATION_SETTINGS,
      "%s",
      _("Need a pam service to authenticate users.")
    );
    return FALSE;
  }
#endif /* LIBINFINITY_HAVE_PAM */

  requires_password = options->password != NULL;
#ifdef LIBINFINITY_HAVE_PAM
  requires_password = requires_password || options->pam_service != NULL;
#endif /* LIBINFINITY_HAVE_PAM */

  if(requires_password &&
     options->security_policy == INF_XMPP_CONNECTION_SECURITY_ONLY_UNSECURED)
  {
    fprintf(
      stderr,
      "%s",
      _("WARNING: Requiring password through unencrypted connection.")
    );
  }

  if(options->create_key == TRUE && options->create_certificate == FALSE)
  {
    g_set_error(
      error,
      infinoted_options_error_quark(),
      INFINOTED_OPTIONS_ERROR_INVALID_CREATE_OPTIONS,
      "%s",
      _("Creating a new private key also requires creating a new certificate "
        "signed with it.")
    );

    return FALSE;
  }
  else if(security_policy != INF_XMPP_CONNECTION_SECURITY_ONLY_UNSECURED && 
          options->key_file == NULL)
  {
    g_set_error(
      error,
      infinoted_options_error_quark(),
      INFINOTED_OPTIONS_ERROR_EMPTY_KEY_FILE,
      "%s",
      _("No private key file given. If you don't have a suitable key file, "
        "either create one using the --create-key command line argument, "
        "or disable TLS by setting the security policy to \"no-tls\".")
    );

    return FALSE;
  }
  else if(security_policy != INF_XMPP_CONNECTION_SECURITY_ONLY_UNSECURED &&
          options->certificate_file == NULL)
  {
    g_set_error(
      error,
      infinoted_options_error_quark(),
      INFINOTED_OPTIONS_ERROR_EMPTY_CERTIFICATE_FILE,
      "%s",
      _("No certificate file given. If you don't have a suitable certificate "
        "file, either create one using the --create-certificate command line "
        "agument, or disable TLS via by setting the security policy to "
        "\"no-tls\".")
    );

    return FALSE;
  }
  else if( (options->sync_directory != NULL && options->sync_interval == 0))
  {
    g_set_error(
      error,
      infinoted_options_error_quark(),
      INFINOTED_OPTIONS_ERROR_INVALID_SYNC_COMBINATION,
      "%s",
      _("A synchronization directory is given, but synchronization interval "
        "is not set. Please either set a nonzero synchronization interval "
        "or unset the synchronization directory using the sync-directory "
        "and sync-interval command line or config file options.")
    );

    return FALSE;
  }
  else if(options->sync_directory == NULL && options->sync_interval != 0)
  {
    g_set_error(
      error,
      infinoted_options_error_quark(),
      INFINOTED_OPTIONS_ERROR_INVALID_SYNC_COMBINATION,
      "%s",
      _("A synchronization interval is given, but the synchronization "
        "directory is not set. Please either set a valid synchronization "
        "directory, or set the synchronization interval to zero using the "
        "sync-directory and sync-interval command line or config file "
        "options.")
    );

    return FALSE;
  }
  else if(options->sync_hook != NULL &&
          (options->sync_interval == 0 || options->sync_directory == NULL))
  {
    g_set_error(
      error,
      infinoted_options_error_quark(),
      INFINOTED_OPTIONS_ERROR_INVALID_SYNC_COMBINATION,
      "%s",
      _("A synchronization hook is given, but either the synchronization "
        "directory or the synchronization interval is not set. Please "
        "either set a valid synchronization interval and directory,"
        "or unset the synchronization hook using the sync-directory, "
        "sync-interval and sync-hook sync-hook command line or config file "
        "options.")
    );

    return FALSE;
  }
  else if(options->autosave_hook != NULL && options->autosave_interval == 0)
  {
    g_set_error(
      error,
      infinoted_options_error_quark(),
      INFINOTED_OPTIONS_ERROR_INVALID_AUTOSAVE_COMBINATION,
      "%s",
      _("An autosave hook is given, but the autosave interval is not set. "
        "Please either set a valid autosave interval or unset the "
        "autosave hook using the --autosave-interval and --autosave-hook "
        "command line or config file options.")
    );

    return FALSE;
  }

  return TRUE;
}

static gboolean
infinoted_options_read_entry_from_keyfile(GKeyFile* key_file,
                                          const InfinotedOptionsEntry* entry,
                                          gpointer base,
                                          GError** error)
{
  GError* local_error;
  InfinotedOptionsValue v;
  gpointer in;
  gpointer out;

  local_error = NULL;

  switch(entry->type)
  {
  case INFINOTED_OPTIONS_ENTRY_INT:
    v.number = g_key_file_get_integer(
      key_file,
      INFINOTED_OPTIONS_GROUP,
      entry->name,
      &local_error
    );

    in = &v.number;
    break;
  case INFINOTED_OPTIONS_ENTRY_STRING:
    v.str = g_key_file_get_string(
      key_file,
      INFINOTED_OPTIONS_GROUP,
      entry->name,
      &local_error
    );

    in = &v.str;
    break;
  case INFINOTED_OPTIONS_ENTRY_STRING_LIST:
    v.strv = g_key_file_get_string_list(
      key_file,
      INFINOTED_OPTIONS_GROUP,
      entry->name,
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
      /* keep default value */
      g_error_free(local_error);
      return TRUE;
    }

    g_propagate_error(error, local_error);
    return FALSE;
  }
  else
  {
    out = (char*)base + entry->offset;
    entry->convert(out, in, &local_error);

    switch(entry->type)
    {
    case INFINOTED_OPTIONS_ENTRY_INT:
      break;
    case INFINOTED_OPTIONS_ENTRY_STRING:
      g_free(v.str);
      break;
    case INFINOTED_OPTIONS_ENTRY_STRING_LIST:
      g_strfreev(v.strv);
      break;
    default:
      g_assert_not_reached();
      break;
    }

    if(local_error != NULL)
    {
      g_propagate_error(error, local_error);
      return FALSE;
    }

    return TRUE;
  }
}

static gboolean
infinoted_options_load_key_file(InfinotedOptions* options,
                                GKeyFile* key_file,
                                GError** error)
{
  const InfinotedOptionsEntry* entry;
  gboolean retval;

  for(entry = INFINOTED_OPTIONS; entry->name != NULL; ++entry)
  {
    retval = infinoted_options_read_entry_from_keyfile(
      key_file,
      entry,
      options,
      error
    );

    if(!retval)
      return FALSE;
  }

  return TRUE;
}

static GKeyFile*
infinoted_options_read_config_into_keyfile(const gchar* const* files,
                                           gboolean ignore_nonexisting_files,
                                           GError** error)
{
  const gchar* const* file;
  GKeyFile* key_file;
  GError* local_error;

  key_file = g_key_file_new();
  local_error = NULL;

  for(file = files; *file != NULL; ++file)
  {
    g_key_file_load_from_file(key_file, *file, G_KEY_FILE_NONE, &local_error);
    if(local_error != NULL)
    {
      if(ignore_nonexisting_files &&
         local_error->domain == G_FILE_ERROR &&
         local_error->code == G_FILE_ERROR_NOENT)
      {
        /* ignore this file, try next */
        g_error_free(local_error);
        local_error = NULL;
      }
      else
      {
        g_key_file_free(key_file);

        g_propagate_prefixed_error(
          error,
          local_error,
          _("Error reading configuration file \"%s\": "),
          *file
        );

        return NULL;
      }
    }
  }

  /* no configuration file exists; that's okay, return empty key file */
  return key_file;
}

static gboolean
infinoted_options_parse_arg_func(const gchar* option_name,
                                 const gchar* value,
                                 gpointer data,
                                 GError** error)
{
  const InfinotedOptionsEntry* entry;
  GHashTable* options;

  InfinotedOptionsTypedValue* optval;

  long l_val;
  char* endptr;

  gchar* const* str;
  guint n_strs;

  options = (GHashTable*)data;

  /* Find InfinotedOptionsEntry with the option name */
  g_assert(option_name[0] == '-');
  if(option_name[1] == '-')
  {
    for(entry = INFINOTED_OPTIONS; entry->name != NULL; ++entry)
      if(strcmp(entry->name, option_name + 2) == 0)
        break;
  }
  else
  {
    for(entry = INFINOTED_OPTIONS; entry->name != NULL; ++entry)
      if(entry->short_name == option_name[1])
        break;
  }

  g_assert(entry->name != NULL);

  switch(entry->type)
  {
  case INFINOTED_OPTIONS_ENTRY_INT:
    if(g_hash_table_lookup(options, entry) != NULL)
    {
      g_set_error(
        error,
        infinoted_options_error_quark(),
        INFINOTED_OPTIONS_ERROR_MULTIPLE_OPTIONS,
        _("The option \"%s\" can only be given once"),
        option_name
      );

      return FALSE;
    }

    errno = 0;
    l_val = strtol(value, &endptr, 10);
    if(*endptr != '\0')
    {
      g_set_error(
        error,
        infinoted_options_error_quark(),
        INFINOTED_OPTIONS_ERROR_INVALID_NUMBER,
        _("\"%s\" is not a number"),
        value
      );

      return FALSE;
    }
    else if(errno != 0)
    {
      g_set_error(
        error,
        infinoted_options_error_quark(),
        INFINOTED_OPTIONS_ERROR_INVALID_NUMBER,
        _("Could not read the number \"%s\": %s"),
        value,
        strerror(errno)
      );

      return FALSE;
    }
    else if(l_val < G_MININT)
    {
      g_set_error(
        error,
        infinoted_options_error_quark(),
        INFINOTED_OPTIONS_ERROR_INVALID_NUMBER,
        _("Number \"%s\" is too small"),
        value
      );

      return FALSE;
    }
    else if(l_val > G_MAXINT)
    {
      g_set_error(
        error,
        infinoted_options_error_quark(),
        INFINOTED_OPTIONS_ERROR_INVALID_NUMBER,
        _("Number \"%s\" is too large"),
        value
      );

      return FALSE;
    }
    else
    {
      optval = infinoted_options_typed_value_new();
      optval->type = INFINOTED_OPTIONS_ENTRY_INT;
      optval->value.number = l_val;
      g_hash_table_insert(options, (gpointer)entry, optval);
    }

    return TRUE;
  case INFINOTED_OPTIONS_ENTRY_STRING:
    if(g_hash_table_lookup(options, (gpointer)entry) != NULL)
    {
      g_set_error(
        error,
        infinoted_options_error_quark(),
        INFINOTED_OPTIONS_ERROR_MULTIPLE_OPTIONS,
        _("The option \"%s\" can only be given once"),
        option_name
      );

      return FALSE;
    }

    optval = infinoted_options_typed_value_new();
    optval->type = INFINOTED_OPTIONS_ENTRY_STRING;
    optval->value.str = g_strdup(value);
    g_hash_table_insert(options, (gpointer)entry, optval);

    return TRUE;
  case INFINOTED_OPTIONS_ENTRY_STRING_LIST:
    optval = g_hash_table_lookup(options, entry);
    if(optval == NULL)
    {
      optval = infinoted_options_typed_value_new();
      optval->type = INFINOTED_OPTIONS_ENTRY_STRING_LIST;
      optval->value.strv = g_malloc(2 * sizeof(gchar*));
      optval->value.strv[0] = g_strdup(value);
      optval->value.strv[1] = NULL;
      g_hash_table_insert(options, (gpointer)entry, optval);
    }
    else
    {
      g_assert(optval->type == INFINOTED_OPTIONS_ENTRY_STRING_LIST);

      n_strs = 0;
      for(str = optval->value.strv; *str != NULL; ++str)
        ++n_strs;

      optval->value.strv = g_realloc(
        optval->value.strv,
        (n_strs + 2) * sizeof(gchar*)
      );

      optval->value.strv[n_strs] = g_strdup(value);
      optval->value.strv[n_strs + 1] = NULL;
    }

    return TRUE;
  default:
    g_assert_not_reached();
    return FALSE;
  }
}

static void
infinoted_options_args_to_keyfile_foreach_func(gpointer key,
                                               gpointer value,
                                               gpointer user_data)
{
  const InfinotedOptionsEntry* entry;
  const InfinotedOptionsTypedValue* optval;
  GKeyFile* key_file;

  gchar* const* str;
  guint n_strs;

  entry = (const InfinotedOptionsEntry*)key;
  optval = (const InfinotedOptionsTypedValue*)value;
  key_file = (GKeyFile*)user_data;

  g_assert(entry->type == optval->type);

  switch(entry->type)
  {
  case INFINOTED_OPTIONS_ENTRY_INT:
    g_key_file_set_integer(
      key_file,
      INFINOTED_OPTIONS_GROUP,
      entry->name,
      optval->value.number
    );

    break;
  case INFINOTED_OPTIONS_ENTRY_STRING:
    g_key_file_set_string(
      key_file,
      INFINOTED_OPTIONS_GROUP,
      entry->name,
      optval->value.str
    );

    break;
  case INFINOTED_OPTIONS_ENTRY_STRING_LIST:
    n_strs = 0;
    for(str = optval->value.strv; *str != NULL; ++str)
      ++n_strs;

    g_key_file_set_string_list(
      key_file,
      INFINOTED_OPTIONS_GROUP,
      entry->name,
      (const gchar* const*)optval->value.strv,
      n_strs
    );

    break;
  default:
    g_assert_not_reached();
    break;
  }
}

static gboolean
infinoted_options_load(InfinotedOptions* options,
                       const gchar* const* config_files,
                       int* argc,
                       char*** argv,
                       GError** error)
{
  gchar* config_filename[2];
  gboolean create_key;
  gboolean create_certificate;
  gboolean daemonize;
  gboolean kill_daemon;
  gboolean display_version;

  GOptionContext *context;
  gchar* desc;

  const InfinotedOptionsEntry* option;

  GOptionGroup* group;
  GOptionEntry* entries;
  guint n_entries;
  guint index;
  GHashTable* cmdline_options;

  GKeyFile* key_file;

  const GOptionEntry STATIC_ENTRIES[] = {
    { "config-file", 0, 0,
      G_OPTION_ARG_FILENAME, &config_filename[0],
      N_("Configuration file to load, instead of the default "
         "configuration file"),
      N_("CONFIG-FILE")
    }, {
      "create-key", 0, 0,
      G_OPTION_ARG_NONE, &create_key,
      N_("Creates a new random private key. The new key will be stored at "
         "the given location for the server's private key."),
      NULL
    }, { "create-certificate", 0, 0,
      G_OPTION_ARG_NONE, &create_certificate,
      N_("Creates a new self-signed certificate signed with the given "
         "private key. The certificate is stored at the given location "
         "for the server's certificate."),
      NULL
    }, {
#ifdef LIBINFINITY_HAVE_LIBDAEMON
      "daemonize", 'd', 0,
      G_OPTION_ARG_NONE, &daemonize,
      N_("Daemonize the server, i.e. run it in the background"),
      NULL
    }, { "kill-daemon", 'D', 0,
      G_OPTION_ARG_NONE, &kill_daemon,
      N_("Kill a running daemon and exit"), NULL
    }, {
#endif
      "version", 'v', 0,
      G_OPTION_ARG_NONE, &display_version,
      N_("Display version information and exit"), NULL },
    { NULL, 0, 0, G_OPTION_ARG_NONE,
      NULL,
      NULL, 0 }
  };

  /* default values */
  config_filename[0] = NULL;
  create_key = options->create_key;
  create_certificate = options->create_certificate;
  daemonize = options->daemonize;
  kill_daemon = FALSE;
  display_version = FALSE;

  /* Build an array of all GOptionEntries -- the above static ones, plus one for each
   * configuration file option. The configuration file options parsed via command line
   * are stored in a hash table, and after the configuration file has been read, the
   * elements are replaced in the GKeyFile before the GKeyFile is parsed. */
  n_entries = G_N_ELEMENTS(INFINOTED_OPTIONS);
  entries = g_malloc(n_entries * sizeof(GOptionEntry));

  index = 0;
  for(option = INFINOTED_OPTIONS; option->name != NULL; ++option)
  {
    entries[index].long_name = option->name;
    entries[index].short_name = option->short_name;
    entries[index].flags = 0;
    entries[index].arg = G_OPTION_ARG_CALLBACK;
    entries[index].arg_data = infinoted_options_parse_arg_func;
    entries[index].description = option->description;
    entries[index].arg_description = option->arg_description;
    ++index;
  }

  g_assert(index == n_entries - 1);
  entries[index].long_name = NULL;
  entries[index].short_name = 0;
  entries[index].flags = 0;
  entries[index].arg = 0;
  entries[index].arg_data = NULL;
  entries[index].description = NULL;
  entries[index].arg_description = NULL;

  /* Now, parse the options */
  if(argc != NULL && argv != NULL)
  {
    cmdline_options = g_hash_table_new_full(
      NULL,
      NULL,
      NULL,
      infinoted_options_typed_value_free
    );

    /* Note that we take ownership of the hash table ourselves */
    group = g_option_group_new(
      "main",
      N_("Infinoted Options"),
      N_("Main Program Options"),
      cmdline_options,
      NULL
    );

    g_option_group_set_translation_domain(group, GETTEXT_PACKAGE);
    g_option_group_add_entries(group, STATIC_ENTRIES);
    g_option_group_add_entries(group, entries);

    desc = g_strdup_printf("- %s", _("infinote dedicated server"));
    context = g_option_context_new(desc);
    g_free(desc);

    g_option_context_set_main_group(context, group);

    if(g_option_context_parse(context, argc, argv, error) == FALSE)
    {
      g_option_context_free(context);
      g_free(entries);
      g_hash_table_unref(cmdline_options);
      return FALSE;
    }

    if(display_version)
    {
      printf("infinoted %s\n", PACKAGE_VERSION);
      exit(0);
    }

#ifdef LIBINFINITY_HAVE_LIBDAEMON
    if(kill_daemon)
    {
      infinoted_util_daemon_set_global_pid_file_proc();
      if(infinoted_util_daemon_pid_file_kill(SIGTERM) != 0)
      {
        infinoted_util_daemon_set_local_pid_file_proc();
        if(infinoted_util_daemon_pid_file_kill(SIGTERM) != 0)
        {
          infinoted_util_set_errno_error(error, errno,
            _("Could not kill daemon"));
          return FALSE;
        }
      }

      exit(0);
    }
#endif
  }

  g_option_context_free(context);
  g_free(entries);

  options->create_key = create_key;
  options->create_certificate = create_certificate;
  options->daemonize = daemonize;

  /* Next, read the configuration file into a GKeyFile. The configuration file
   * can be overridden on the command line, so we can only do this after
   * command line option parsing. */
  if(config_filename[0] != NULL)
  {
    config_filename[1] = NULL;

    key_file = infinoted_options_read_config_into_keyfile(
      (const gchar* const*)config_filename,
      FALSE,
      error
    );

    g_free(config_filename[0]);
  }
  else
  {
    key_file = infinoted_options_read_config_into_keyfile(
      config_files,
      TRUE,
      error
    );
  }

  if(!key_file)
  {
    g_hash_table_unref(cmdline_options);
    return FALSE;
  }

  /* With the key file in hands, we now override any options given on the
   * command line. */
  g_hash_table_foreach(
    cmdline_options,
    infinoted_options_args_to_keyfile_foreach_func,
    key_file
  );

  g_hash_table_unref(cmdline_options);

  /* Finally, load the key file into the actual options structure */
  if(!infinoted_options_load_key_file(options, key_file, error))
  {
    g_key_file_free(key_file);
    return FALSE;
  }

  g_key_file_free(key_file);
  return infinoted_options_validate(options, error);
}

/**
 * infinoted_options_new:
 * @config_files: A %NULL-terminated error of config filenames.
 * @argc: Pointer to command line argument count, or %NULL.
 * @argv: Pointer to command line argument vector, or %NULL.
 * @error: Location to store error information, if any.
 *
 * Creates a new #InfinotedOptions structure that contains options infinoted
 * is supposed to start with. Command line options always overwrite config
 * file options.
 *
 * The config files are checked in order, the first one that exists is being
 * loaded, and the other ones are being ignored. If the command line array
 * includes the --config-file option, the @config_files array is overriden
 * by the command line option.
 *
 * Returns: A new #InfinotedOptions, or %NULL in case of error.
 * Free with infinoted_options_free().
 */
InfinotedOptions*
infinoted_options_new(const gchar* const* config_files,
                      int* argc,
                      char*** argv,
                      GError** error)
{
  InfinotedOptions* options;

  options = g_slice_new(InfinotedOptions);

  /* Default options */
  options->log_path = NULL;
  options->key_file = NULL;
  options->certificate_file = NULL;
  options->certificate_chain_file = NULL;
  options->create_key = FALSE;
  options->create_certificate = FALSE;
  options->port = inf_protocol_get_default_port();
  options->security_policy = INF_XMPP_CONNECTION_SECURITY_ONLY_TLS;
  options->root_directory =
    g_build_filename(g_get_home_dir(), ".infinote", NULL);
  options->autosave_hook = NULL;
  options->autosave_interval = 0;
  options->password = NULL;
#ifdef LIBINFINITY_HAVE_PAM
  options->pam_service = NULL;
  options->pam_allowed_users = NULL;
  options->pam_allowed_groups = NULL;
#endif /* LIBINFINITY_HAVE_PAM */
  options->ca_list_file = NULL;
  options->sync_directory = NULL;
  options->sync_interval = 0;
  options->sync_hook = NULL;
  options->max_transformation_vdiff = 0;
  options->traffic_log_directory = NULL;

#ifdef LIBINFINITY_HAVE_LIBDAEMON
  options->daemonize = FALSE;
#endif

  if(!infinoted_options_load(options, config_files, argc, argv, error))
  {
    infinoted_options_free(options);
    return NULL;
  }

  return options;
}

/**
 * infinoted_options_free:
 * @options: A #InfinotedOptions.
 *
 * Frees @options and clears up all memory allocated by it.
 */
void
infinoted_options_free(InfinotedOptions* options)
{
  g_free(options->traffic_log_directory);
  g_free(options->log_path);
  g_free(options->key_file);
  g_free(options->certificate_file);
  g_free(options->certificate_chain_file);
  g_free(options->root_directory);
  g_free(options->autosave_hook);
  g_free(options->password);
#ifdef LIBINFINITY_HAVE_PAM
  g_free(options->pam_service);
  g_strfreev(options->pam_allowed_users);
  g_strfreev(options->pam_allowed_groups);
#endif
  g_free(options->ca_list_file);
  g_free(options->sync_directory);
  g_free(options->sync_hook);
  g_slice_free(InfinotedOptions, options);
}

/**
 * infinoted_options_error_quark:
 *
 * Returns the GQuark for errors from the InfinotedOptions module.
 *
 * Returns: The error domain for the InfinotedOptions module.
 */
GQuark
infinoted_options_error_quark(void)
{
  return g_quark_from_static_string("INFINOTED_OPTIONS_ERROR");
}

/* vim:set et sw=2 ts=2: */
