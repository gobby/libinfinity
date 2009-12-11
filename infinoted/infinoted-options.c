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

#include <infinoted/infinoted-options.h>
#include <libinfinity/inf-i18n.h>

#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "config.h"

static const gchar INFINOTED_OPTIONS_GROUP[] = "infinoted";

/* TODO: Split the functionality to load key files as options into a separate
 * file. */

/* We abuse the flags of a GOptionEntry to decide whether the option
 * can be set in the config file in addition to the command line */
static const gint G_OPTION_FLAG_NO_CONFIG_FILE = 1 << 31;

static gchar*
infinoted_options_policy_to_string(InfXmppConnectionSecurityPolicy policy)
{
  switch(policy)
  {
  case INF_XMPP_CONNECTION_SECURITY_ONLY_UNSECURED:
    return g_strdup("no-tls");
  case INF_XMPP_CONNECTION_SECURITY_ONLY_TLS:
    return g_strdup("require-tls");
  case INF_XMPP_CONNECTION_SECURITY_BOTH_PREFER_UNSECURED:
  case INF_XMPP_CONNECTION_SECURITY_BOTH_PREFER_TLS:
    return g_strdup("allow-tls");
  default:
    g_assert_not_reached();
    return NULL;
  }
}

static gboolean
infinoted_options_policy_from_string(const gchar* string,
                                     InfXmppConnectionSecurityPolicy* pol,
                                     GError** error)
{
  if(strcmp(string, "no-tls") == 0)
  {
    *pol = INF_XMPP_CONNECTION_SECURITY_ONLY_UNSECURED;
    return TRUE;
  }
  else if(strcmp(string, "allow-tls") == 0)
  {
    *pol = INF_XMPP_CONNECTION_SECURITY_BOTH_PREFER_TLS;
    return TRUE;
  }
  else if(strcmp(string, "require-tls") == 0)
  {
    *pol = INF_XMPP_CONNECTION_SECURITY_ONLY_TLS;
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
      string
    );

    return FALSE;
  }
}

/* TODO: Correct error handling? We only use this at one point where we know
 * the port is valid anyway. */
static gint
infinoted_options_port_to_integer(guint port)
{
  g_assert(port <= 65535);
  return port;
}

static gboolean
infinoted_options_autosave_interval_from_integer(gint value,
                                                 guint* result,
                                                 GError** error)
{
  if(value < 0)
  {
    g_set_error(
      error,
      infinoted_options_error_quark(),
      INFINOTED_OPTIONS_ERROR_INVALID_AUTOSAVE_INTERVAL,
      _("Autosave interval must not be negative")
    );

    return FALSE;
  }

  *result = value;
  return TRUE;
}

static gboolean
infinoted_options_port_from_integer(gint value,
                                    guint* port,
                                    GError** error)
{
  if(value <= 0 || value > 0xffff)
  {
    g_set_error(
      error,
      infinoted_options_error_quark(),
      INFINOTED_OPTIONS_ERROR_INVALID_PORT,
      _("\"%d\" is not a valid port number. Port numbers range from "
        "1 to 65535"),
      value
    );

    return FALSE;
  }

  *port = value;
  return TRUE;
}

static gboolean
infinoted_options_propagate_key_file_error(GError** error,
                                           GError* key_file_error)
{
  /* No error, always good */
  if(key_file_error == NULL)
    return TRUE;

  if(key_file_error->domain == G_KEY_FILE_ERROR &&
     (key_file_error->code == G_KEY_FILE_ERROR_GROUP_NOT_FOUND ||
      key_file_error->code == G_KEY_FILE_ERROR_KEY_NOT_FOUND))
  {
    /* We ignore these errors but just use default values instead */
    g_error_free(key_file_error);
    return TRUE;
  }
  else
  {
    g_propagate_error(error, key_file_error);
    return FALSE;
  }
}

static gboolean
infinoted_options_key_file_get_string(GKeyFile* keyfile,
                                      const gchar* keyname,
                                      gchar** result,
                                      GError** error)
{
  GError* local_error;
  gchar* ret;

  local_error = NULL;
  ret = g_key_file_get_string(
    keyfile,
    INFINOTED_OPTIONS_GROUP,
    keyname,
    &local_error
  );

  if(local_error == NULL)
  {
    g_free(*result);
    *result = ret;
  }
  else
  {
    g_free(ret);
  }

  return infinoted_options_propagate_key_file_error(error, local_error);
}

static gboolean
infinoted_options_key_file_get_integer(GKeyFile* keyfile,
                                       const gchar* keyname,
                                       gint* result,
                                       GError** error)
{
  GError* local_error;
  gint ret;

  local_error = NULL;
  ret = g_key_file_get_integer(
    keyfile,
    INFINOTED_OPTIONS_GROUP,
    keyname,
    &local_error
  );

  if(local_error == NULL)
    *result = ret;

  return infinoted_options_propagate_key_file_error(error, local_error);
}

static gboolean
infinoted_options_key_file_get_boolean(GKeyFile* keyfile,
                                       const gchar* keyname,
                                       gboolean* result,
                                       GError** error)
{
  GError* local_error;
  gboolean ret;

  local_error = NULL;
  ret = g_key_file_get_boolean(
    keyfile,
    INFINOTED_OPTIONS_GROUP,
    keyname,
    &local_error
  );

  if(local_error == NULL)
    *result = ret;

  return infinoted_options_propagate_key_file_error(error, local_error);
}

static gboolean
infinoted_options_load_key_file(const GOptionEntry* entries,
                                GKeyFile* key_file,
                                GError** error)
{
  const GOptionEntry* entry;
  gboolean result;
  gchar* string;
  gchar* filename;

  for(entry = entries; entry->long_name != NULL; ++ entry)
  {
    if( (entry->flags & G_OPTION_FLAG_NO_CONFIG_FILE) == 0)
    {
      switch(entry->arg)
      {
      case G_OPTION_ARG_NONE:
        result = infinoted_options_key_file_get_boolean(
          key_file,
          entry->long_name,
          entry->arg_data,
          error
        );

        break;
      case G_OPTION_ARG_INT:
        result = infinoted_options_key_file_get_integer(
          key_file,
          entry->long_name,
          entry->arg_data,
          error
        );

        break;
      case G_OPTION_ARG_STRING:
        string = NULL;

        result = infinoted_options_key_file_get_string(
          key_file,
          entry->long_name,
          &string,
          error
        );

        if(result == TRUE)
        {
          /* Can return TRUE without having string set, for example in case
           * the key is not set at all, in which case we just don't overwrite
           * the existing value. */
          if(entry->arg_data && string != NULL)
          {
            g_free(*(gchar**)entry->arg_data);
            *(gchar**)entry->arg_data = string;
          }
          else
          {
            g_free(string);
          }
        }

        break;
      case G_OPTION_ARG_FILENAME:
        string = NULL;

        result = infinoted_options_key_file_get_string(
          key_file,
          entry->long_name,
          &string,
          error
        );

        if(result == TRUE)
        {
          /* Can return TRUE without having string set, for example in case
           * the key is not set at all, in which case we just don't overwrite
           * the existing value. */
          if(entry->arg_data != NULL && string != NULL)
          {
            filename = g_filename_from_utf8(string, -1, NULL, NULL, error);
            if(filename == NULL)
            {
              result = FALSE;
            }
            else
            {
              g_free(*(gchar**)entry->arg_data);
              *(gchar**)entry->arg_data = filename;
            }
          }

          g_free(string);
        }

        break;
      default:
        /* Other argtypes are not yet supported to be loaded via keyfiles */
        g_assert_not_reached();
        break;
      }

      if(!result)
        return FALSE;
    }
  }

  return TRUE;
}

static gboolean
infinoted_options_load_file(const GOptionEntry* entries,
                            const gchar* file,
                            GError** error)
{
  GKeyFile* key_file;
  GError* local_error;
  gboolean result;

  key_file = g_key_file_new();
  local_error = NULL;

  g_key_file_load_from_file(key_file, file, G_KEY_FILE_NONE, &local_error);
  if(local_error != NULL)
  {
    g_key_file_free(key_file);
    if(local_error->domain == G_FILE_ERROR &&
       local_error->code == G_FILE_ERROR_NOENT)
    {
      /* ignore */
      g_error_free(local_error);
      return TRUE;
    }
    else
    {
      g_propagate_error(error, local_error);
      return FALSE;
    }
  }

  result = infinoted_options_load_key_file(entries, key_file, error);
  g_key_file_free(key_file);
  return result;
}

static gboolean
infinoted_options_validate(InfinotedOptions* options,
                           GError** error)
{
  InfXmppConnectionSecurityPolicy security_policy;
  security_policy = options->security_policy;

  if(options->password != NULL &&
     options->security_policy == INF_XMPP_CONNECTION_SECURITY_ONLY_UNSECURED)
  {
    infinoted_util_log_warning(
      _("Requiring password through unencrypted connection."));
  }


  if(options->create_key == TRUE && options->create_certificate == FALSE)
  {
    g_set_error(
      error,
      infinoted_options_error_quark(),
      INFINOTED_OPTIONS_ERROR_INVALID_CREATE_OPTIONS,
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
      _("No certificate file given. If you don't have a suitable certificate "
        "file, either create one using the --create-certificate command line "
        "agument, or disable TLS via by setting the security policy to "
        "\"no-tls\".")
    );

    return FALSE;
  }

  return TRUE;
}

static gboolean
infinoted_options_load(InfinotedOptions* options,
                       const gchar* const* config_files,
                       int* argc,
                       char*** argv,
                       GError** error)
{
  const gchar* const* file;

  gchar* security_policy;
  gint port_number;
  gboolean display_version;
#ifdef LIBINFINITY_HAVE_LIBDAEMON
  gboolean kill_daemon;
#endif
  gint autosave_interval;

  gboolean result;

  GOptionContext *context;
  gchar* desc;

  const GOptionEntry entries[] = {
    { "key-file", 'k', 0,
      G_OPTION_ARG_FILENAME, &options->key_file,
      N_("The server's private key"), N_("KEY-FILE") },
    { "certificate-file", 'c', 0,
      G_OPTION_ARG_FILENAME, &options->certificate_file,
      N_("The server's certificate"), N_("CERTIFICATE-FILE") },
    { "certificate-chain", 0, 0,
      G_OPTION_ARG_FILENAME, &options->certificate_chain_file,
      N_("The certificates chain down to the root certificate"), NULL },
    { "create-key", 0, G_OPTION_FLAG_NO_CONFIG_FILE,
      G_OPTION_ARG_NONE, &options->create_key,
      N_("Creates a new random private key"), NULL },
    { "create-certificate", 0, G_OPTION_FLAG_NO_CONFIG_FILE,
      G_OPTION_ARG_NONE, &options->create_certificate,
      N_("Creates a new self-signed certificate using the given key"), NULL },
    { "port-number", 'p', 0,
      G_OPTION_ARG_INT, &port_number,
      N_("The port number to listen on"), N_("PORT") },
    { "security-policy", 0, 0,
      G_OPTION_ARG_STRING, &security_policy,
      N_("How to decide whether to use TLS"), "no-tls|allow-tls|require-tls" },
    { "root-directory", 'r', 0,
      G_OPTION_ARG_FILENAME, &options->root_directory,
      N_("The directory to store documents into"), N_("DIRECTORY") },
    { "autosave-interval", 0, 0,
      G_OPTION_ARG_INT, &autosave_interval,
      N_("Interval within which to save documents, in seconds, or 0 to "
         "disable autosave"), N_("INTERVAL") },
    { "password", 'P', 0,
      G_OPTION_ARG_STRING, &options->password,
      N_("Require given password on connections"), N_("PASSWORD") },
#ifdef LIBINFINITY_HAVE_LIBDAEMON
    { "daemonize", 'd', 0,
      G_OPTION_ARG_NONE, &options->daemonize,
      N_("Daemonize the server"), NULL },
    { "kill-daemon", 'D', 0,
      G_OPTION_ARG_NONE, &kill_daemon,
      N_("Kill a running daemon"), NULL },
#endif
    { "version", 'v', G_OPTION_FLAG_NO_CONFIG_FILE,
      G_OPTION_ARG_NONE, &display_version,
      N_("Display version information and exit"), NULL },
    { NULL, 0, 0, G_OPTION_ARG_NONE,
      NULL,
      NULL, 0 }
  };

  display_version = FALSE;
#ifdef LIBINFINITY_HAVE_LIBDAEMON
  kill_daemon = FALSE;
#endif
  security_policy = NULL;
  port_number = infinoted_options_port_to_integer(options->port);
  autosave_interval = options->autosave_interval;

  if(config_files)
  {
    for(file = config_files; *file != NULL; ++ file)
    {
      if(infinoted_options_load_file(entries, *file, error) == FALSE)
      {
        g_prefix_error(error, "%s: ", *file);
        g_free(security_policy);
        return FALSE;
      }
    }
  }

  if(argc != NULL && argv != NULL)
  {
    desc = g_strdup_printf("- %s", _("infinote dedicated server"));
    context = g_option_context_new(desc);
    g_free(desc);
    g_option_context_add_main_entries(context, entries, GETTEXT_PACKAGE);

    if(g_option_context_parse(context, argc, argv, error) == FALSE)
    {
      g_option_context_free(context);
      g_free(security_policy);
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
      g_free(security_policy);

      if(!infinoted_util_set_daemon_pid_file_proc(error))
        return FALSE;

#ifdef DAEMON_PID_FILE_KILL_WAIT_AVAILABLE
      if(daemon_pid_file_kill_wait(SIGTERM, 5) != 0)
#else
      if(daemon_pid_file_kill(SIGTERM) != 0)
#endif
      {
        infinoted_util_set_errno_error(error, errno,
          _("Could not kill daemon"));
        return FALSE;
      }

      exit(0);
    }
#endif

    g_option_context_free(context);
  }

  if(security_policy != NULL)
  {
    result = infinoted_options_policy_from_string(
      security_policy,
      &options->security_policy,
      error
    );

    g_free(security_policy);
    if(!result) return FALSE;
  }

  /* TODO: Do we leak security_policy at this point? */

  result = infinoted_options_port_from_integer(
    port_number,
    &options->port,
    error
  );
  if(!result) return FALSE;

  result = infinoted_options_autosave_interval_from_integer(
    autosave_interval,
    &options->autosave_interval,
    error
  );
  if(!result) return FALSE;

  if(options->password != NULL)
  {
    if(strcmp("", options->password) == NULL)
    {
      g_free(options->password);
      options->password = NULL;
    }
  }

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
 * The config files are loaded in order, which means that config files at the
 * back of the array overwrite options of config files in front of the array.
 * Config files are not required to exist. If a given config file does not
 * exist, it is simply ignored.
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
  options->key_file = NULL;
  options->certificate_file = NULL;
  options->certificate_chain_file = NULL;
  options->create_key = FALSE;
  options->create_certificate = FALSE;
  options->port = 6523;
  options->security_policy = INF_XMPP_CONNECTION_SECURITY_BOTH_PREFER_TLS;
  options->root_directory =
    g_build_filename(g_get_home_dir(), ".infinote", NULL);
  options->autosave_interval = 0;
  options->password = NULL;

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
  g_free(options->key_file);
  g_free(options->certificate_file);
  g_free(options->certificate_chain_file);
  g_free(options->root_directory);
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
