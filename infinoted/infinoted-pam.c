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

#include "config.h"

#include <infinoted/infinoted-util.h>
#include <libinfinity/common/inf-error.h>
#include <libinfinity/inf-i18n.h>

#ifdef LIBINFINITY_HAVE_PAM

#include <infinoted/infinoted-pam.h>
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

#ifdef HAVE_PAM_FAIL_DELAY
static void
infinoted_pam_delay_func(int retval,
                         unsigned usec_delay,
                         void *appdata_ptr)
{
  /* do not delay */
  /* TODO: figure out how to randomly delay a bit without blocking the entire
   * server. */
}
#endif /* HAVE_PAM_FAIL_DELAY */

static void
infinoted_pam_log_error(const char* username,
                        const char* detail,
                        int error_code,
                        GError** error)
{
  const char* msg;

  if(error_code == 0)
    msg = _("Entry not found");
  else
    msg = strerror(error_code);

  infinoted_util_log_error(
    _("Error while checking groups of user \"%s\", %s: %s."),
    username,
    detail,
    msg
  );

  /* TODO: use g_set_error_literal in glib 2.18 */
  g_set_error(
    error,
    inf_authentication_detail_error_quark(),
    INF_AUTHENTICATION_DETAIL_ERROR_SERVER_ERROR,
    "%s",
    inf_authentication_detail_strerror(INF_AUTHENTICATION_DETAIL_ERROR_SERVER_ERROR)
  );
}

static gboolean
infinoted_pam_user_is_in_group(const gchar* username,
                               gchar* required_group,
                               gchar* buf,
                               size_t buf_size,
                               GError** error)
{
  struct passwd user_entry, *user_pointer;
  struct group  group_entry, *group_pointer;
  char** iter;
  char msgbuf[128];
  int status;
  gid_t gid;

  /* first check against the user's primary group */
  status = getpwnam_r(username, &user_entry, buf, buf_size, &user_pointer);
  if(user_pointer == NULL)
  {
    infinoted_pam_log_error(
      username,
      _("looking up user information"),
      status,
      error);
    return FALSE;
  }

  gid = user_entry.pw_gid;
  status =
    getgrgid_r(gid, &group_entry, buf, buf_size, &group_pointer);
  if(group_pointer == NULL)
  {
    g_snprintf(msgbuf, sizeof msgbuf, _("looking up group %ld"), (long) gid);
    infinoted_pam_log_error(username, msgbuf, status, error);
    return FALSE;
  }

  if(strcmp(group_entry.gr_name, required_group) == 0)
    return TRUE;

  /* now go through all users listed for the required group */
  status =
    getgrnam_r(required_group, &group_entry, buf, buf_size, &group_pointer);
  if(group_pointer == NULL)
  {
    g_snprintf(msgbuf,
               sizeof msgbuf,
               _("looking up group \"%s\""),
               required_group);
    infinoted_pam_log_error(username, msgbuf, status, error);
    return FALSE;
  }

  for(iter = group_entry.gr_mem; *iter; ++iter)
  {
    if(strcmp(*iter, username) == 0)
      return TRUE;
  }

  /* Nothing worked. No success, but no error either. */
  return FALSE;
}

gboolean
infinoted_pam_user_is_allowed(InfinotedOptions* options,
                              const gchar* username,
                              GError** error)
{
  char* buf;
  long buf_size_gr, buf_size_pw, buf_size;
  gboolean status;
  GError* local_error;

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
      /* avoid reallocating this buffer over and over */
      buf_size_pw = sysconf(_SC_GETPW_R_SIZE_MAX);
      buf_size_gr = sysconf(_SC_GETGR_R_SIZE_MAX);
      buf_size = MAX(buf_size_pw, buf_size_gr);
      buf = g_malloc(buf_size);

      status = FALSE;
      local_error = NULL;
      for(iter = options->pam_allowed_groups; *iter; ++iter)
      {
        if(infinoted_pam_user_is_in_group(
             username, *iter, buf, buf_size, &local_error))
        {
          status = TRUE;
          break;
        }

        /* do not try to check all other groups on an actual error */
        if(local_error)
        {
          g_propagate_error(error, local_error);
          break;
        }
      }
      g_free(buf);
      return status;
    }
    else
    {
      return FALSE;
    }
  }
}

gboolean
infinoted_pam_authenticate(const char* service,
                           const char* username,
                           const char* password)
{
  pam_handle_t* pamh;
  struct pam_conv conv;
  int status;

#ifdef HAVE_PAM_FAIL_DELAY
  void (*delay_fp)(int, unsigned, void*);
  void* delay_void_ptr;
#endif

  conv.conv = infinoted_pam_conv_func;
  conv.appdata_ptr = *(void**) (void*) &password;

  if(pam_start(service, username, &conv, &pamh) != PAM_SUCCESS)
    return FALSE;

  status = PAM_SUCCESS;

#ifdef HAVE_PAM_FAIL_DELAY
  delay_fp = infinoted_pam_delay_func;
  /* avoid warnings for casting func-ptrs to object pointers
   * and for type-punning pointers */
  delay_void_ptr = *(void**) (void*) (char*) &delay_fp;
  status = pam_set_item(pamh, PAM_FAIL_DELAY, delay_void_ptr);
  if(status == PAM_SUCCESS)
    status = pam_authenticate(pamh, 0);
#endif

  /* TODO: consider pam_acct_mgmt */

  pam_end(pamh, status);
  return status == PAM_SUCCESS;
}

#endif /* LIBINFINITY_HAVE_PAM */

/* vim:set et sw=2 ts=2: */
