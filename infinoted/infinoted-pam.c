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

#define _POSIX_C_SOURCE 1 /* for getpwnam_r, getgrnam_r, getgrgid_r */

#include <infinoted/infinoted-pam.h>
#include <libinfinity/common/inf-error.h>

#include <security/pam_appl.h>
#include <sys/types.h>
#include <grp.h>
#include <pwd.h>

#include <stdlib.h>
#include <string.h>

/* cannot use g_strdup because that requires its return value to be free'd
 * with g_free(), but pam is not aware of that. */

static char*
infinoted_pam_strdup(const char* str)
{
  size_t size;
  char* new_str;

  size = strlen(str) + 1;
  new_str = malloc(size);
  memcpy(new_str, str, size);

  return new_str;
}

static int
infinoted_pam_conv_func(int num_msg,
                        const struct pam_message** msgs,
                        struct pam_response** resps,
                        void* appdata_ptr)
{
  int i;
  const struct pam_message* msg;
  struct pam_response* resp;

  *resps = malloc(sizeof(struct pam_response) * num_msg);

  for(i = 0; i < num_msg; ++i)
  {
    msg = msgs[i];
    resp = &(*resps)[i];
    resp->resp_retcode = 0;
    if(msg->msg_style == PAM_PROMPT_ECHO_OFF) /* looks like password prompt */
      resp->resp = infinoted_pam_strdup(appdata_ptr);
    else
      resp->resp = NULL;
  }
  return PAM_SUCCESS;
}

static void
infinoted_pam_delay_func(int retval,
                         unsigned usec_delay,
                         void *appdata_ptr)
{
  /* do not delay */
}

static gboolean
infinoted_pam_user_is_in_group(const gchar* username,
                                             const gchar* required_group)
{
  struct passwd user_entry, *user_pointer;
  struct group  group_entry, *group_pointer;
  char buf[1024];
  char** iter;
  int status;
  /* TODO: status receives an 'errno' style error code, should we log it?
   * We could do more proper error handling, but we are not going to
   * terminate because some user cannot log in, anyway, and I am not certain
   * how much we are supposed to tell users about what went wrong, so we might
   * either log it right here or plain ignore it. */

  /* first check against the user's primary group */
  status = getpwnam_r(username, &user_entry, buf, 1024, &user_pointer);
  if(user_pointer == NULL)
    return FALSE;

  status =
    getgrgid_r(user_entry.pw_gid, &group_entry, buf, 1024, &group_pointer);
  if(group_pointer == NULL)
    return FALSE;

  if(strcmp(group_entry.gr_name, required_group) == 0)
    return TRUE;

  /* now go through all users listed for the required group */
  status =
    getgrnam_r(required_group, &group_entry, buf, 1024, &group_pointer);
  if(group_pointer == NULL)
    return FALSE;

  for(iter = group_entry.gr_mem; *iter; ++iter)
  {
    if(strcmp(*iter, username) == 0)
      return TRUE;
  }

  /* nothing worked. Oh well! */
  return FALSE;
}

static gboolean
infinoted_pam_user_is_allowed(InfinotedOptions* options,
                              const gchar* username)
{
  gchar** iter;
  if(options->pam_allowed_users == NULL
     && options->pam_allowed_groups == NULL)
  {
    return TRUE;
  }
  else
  {
    if(options->pam_allowed_users != NULL)
    {
      for(iter = options->pam_allowed_users; *iter; ++iter)
      {
        if(strcmp(*iter, username) == 0)
          return TRUE;
      }
    }

    if(options->pam_allowed_groups != NULL)
    {
      for(iter = options->pam_allowed_groups; *iter; ++iter)
      {
        if(infinoted_pam_user_is_in_group(username, *iter))
          return TRUE;
      }
    }

    return FALSE;
  }
}

gboolean
infinoted_pam_authenticate(const char* service,
                           const char* username,
                           const char* password)
{
  pam_handle_t* pamh;
  struct pam_conv conv;
  void (*delay_fp)(int, unsigned, void*);
  void* delay_void_ptr;
  int status;

  conv.conv = infinoted_pam_conv_func;
  conv.appdata_ptr = *(void**) (void*) &password;

  if(pam_start(service, username, &conv, &pamh) != PAM_SUCCESS)
    return FALSE;

  delay_fp = infinoted_pam_delay_func;
  /* avoid warnings for casting func-ptrs to object pointers
   * and for type-punning pointers */
  delay_void_ptr = *(void**) (void*) (char*) &delay_fp;
  status = pam_set_item(pamh, PAM_FAIL_DELAY, delay_void_ptr);
  if(status == PAM_SUCCESS)
    status = pam_authenticate(pamh, 0);

  /* TODO: consider pam_acct_mgmt */

  pam_end(pamh, status);
  return status == PAM_SUCCESS;
}

GError*
infinoted_pam_user_authenticated_cb(InfdXmppServer* xmpp_server,
                                    InfXmppConnection* xmpp_connection,
                                    Gsasl_session* sasl_session,
                                    gpointer user_data)
{
  InfinotedOptions* options;
  const char* username;

  options = (InfinotedOptions*) user_data;
  /* if we did not authenticate the user, do nothing*/
  if (options->pam_service == NULL)
    return NULL;

  username = gsasl_property_get(sasl_session, GSASL_AUTHID);
  if(infinoted_pam_user_is_allowed(options, username))
    return NULL;

  return g_error_new_literal(
    inf_postauthentication_error_quark(),
    INF_POSTAUTHENTICATION_ERROR_USER_NOT_AUTHORIZED,
    inf_postauthentication_strerror(
      INF_POSTAUTHENTICATION_ERROR_USER_NOT_AUTHORIZED
    )
  );
}

/* vim:set et sw=2 ts=2: */
