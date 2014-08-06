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

#ifndef __INFD_ACL_ACCOUNT_INFO_H__
#define __INFD_ACL_ACCOUNT_INFO_H__

#include <libinfinity/common/inf-acl.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INFD_TYPE_ACL_ACCOUNT_INFO               (infd_acl_account_info_get_type())

/**
 * InfdAclAccountInfo:
 * @account: The basic account information for this account.
 * @transient: If %TRUE, the account is not stored to disk and only exists
 * as long as the server is running.
 * @certificates: The SHA256 fingerprints of the certificates associated with
 * this account.
 * @n_certificates: The number of certificates associated to this account.
 * @password_salt: A salt which is prepended to the password before hashing.
 * @password_hash: The SHA256 hash of the salted password to log into this
 * account.
 * @first_seen: Time at which the account was first logged in into.
 * @last_seen: Time at which the account was last logged in into.
 *
 * Full user account information as stored on the server side.
 */
typedef struct _InfdAclAccountInfo InfdAclAccountInfo;
struct _InfdAclAccountInfo {
  InfAclAccount account;
  gboolean transient;
  gchar** certificates;
  guint n_certificates;
  gchar* password_salt;
  gchar* password_hash;
  gint64 first_seen;
  gint64 last_seen;
};

GType
infd_acl_account_info_get_type(void) G_GNUC_CONST;

InfdAclAccountInfo*
infd_acl_account_info_new(const gchar* id,
                          const gchar* name,
                          gboolean transient);

InfdAclAccountInfo*
infd_acl_account_info_copy(const InfdAclAccountInfo* info);

void
infd_acl_account_info_free(InfdAclAccountInfo* info);

gboolean
infd_acl_account_info_set_password(InfdAclAccountInfo* info,
                                   const gchar* password,
                                   GError** error);

gboolean
infd_acl_account_info_check_password(const InfdAclAccountInfo* info,
                                     const gchar* password);

void
infd_acl_account_info_add_certificate(InfdAclAccountInfo* info,
                                      const gchar* dn);

void
infd_acl_account_info_remove_certificate(InfdAclAccountInfo* info,
                                         const gchar* dn);

void
infd_acl_account_info_update_time(InfdAclAccountInfo* info);

InfdAclAccountInfo*
infd_acl_account_info_from_xml(xmlNodePtr xml,
                               GError** error);

void
infd_acl_account_info_to_xml(const InfdAclAccountInfo* info,
                             xmlNodePtr xml);

G_END_DECLS

#endif /* __INFD_ACL_ACCOUNT_INFO_H__ */

/* vim:set et sw=2 ts=2: */
