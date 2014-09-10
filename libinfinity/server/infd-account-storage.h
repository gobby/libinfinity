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

#ifndef __INFD_ACCOUNT_STORAGE_H__
#define __INFD_ACCOUNT_STORAGE_H__

#include <libinfinity/common/inf-acl.h>

#include <glib-object.h>

#include <gnutls/x509.h>

G_BEGIN_DECLS

#define INFD_TYPE_ACCOUNT_STORAGE                 (infd_account_storage_get_type())
#define INFD_ACCOUNT_STORAGE(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INFD_TYPE_ACCOUNT_STORAGE, InfdAccountStorage))
#define INFD_IS_ACCOUNT_STORAGE(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INFD_TYPE_ACCOUNT_STORAGE))
#define INFD_ACCOUNT_STORAGE_GET_IFACE(inst)      (G_TYPE_INSTANCE_GET_INTERFACE((inst), INFD_TYPE_ACCOUNT_STORAGE, InfdAccountStorageInterface))

/**
 * InfdAccountStorage:
 *
 * #InfdAccountStorage is an opaque data type. You should only access it
 * via the public API functions.
 */
typedef struct _InfdAccountStorage InfdAccountStorage;
typedef struct _InfdAccountStorageInterface InfdAccountStorageInterface;

/**
 * InfdAccountStorageSupport:
 * @INFD_ACCOUNT_STORAGE_SUPPORT_NOTIFICATION: Whether the
 * #InfdAccountStorage::account-added and #InfdAccountStorage::account-removed
 * signals are emitted when accounts are added or removed externally.
 * @INFD_ACCOUNT_STORAGE_SUPPORT_LIST_ACCOUNTS: Whether obtaining a full list
 * of available accounts is supported.
 * @INFD_ACCOUNT_STORAGE_SUPPORT_ADD_ACCOUNT: Whether adding a new account to
 * the storage is supported.
 * @INFD_ACCOUNT_STORAGE_SUPPORT_REMOVE_ACCOUNT: Whether removing an existing
 * account from the storage is supported.
 * @INFD_ACCOUNT_STORAGE_SUPPORT_CERTIFICATE_LOGIN: Whether the account
 * storage supports authenticating users via client certificates.
 * @INFD_ACCOUNT_STORAGE_SUPPORT_PASSWORD_LOGIN: Whether the account storage
 * supports authenticating users via username and password.
 * @INFD_ACCOUNT_STORAGE_SUPPORT_CERTIFICATE_CHANGE: Whether the account
 * storage supports changing the certificate associated to a user.
 * @INFD_ACCOUNT_STORAGE_SUPPORT_PASSWORD_CHANGE: Whether the account storage
 * supports changing a user's password.
 *
 * This enumeration specifies operations that might or might not be supported
 * by a particular #InfdAccountStorage implementation. Looking up an account
 * by ID or name must always be supported.
 */
typedef enum _InfdAccountStorageSupport {
  INFD_ACCOUNT_STORAGE_SUPPORT_NOTIFICATION = 1 << 0,
  INFD_ACCOUNT_STORAGE_SUPPORT_LIST_ACCOUNTS = 1 << 1,
  INFD_ACCOUNT_STORAGE_SUPPORT_ADD_ACCOUNT = 1 << 2,
  INFD_ACCOUNT_STORAGE_SUPPORT_REMOVE_ACCOUNT = 1 << 3,
  INFD_ACCOUNT_STORAGE_SUPPORT_CERTIFICATE_LOGIN = 1 << 4,
  INFD_ACCOUNT_STORAGE_SUPPORT_PASSWORD_LOGIN = 1 << 5,
  INFD_ACCOUNT_STORAGE_SUPPORT_CERTIFICATE_CHANGE = 1 << 6,
  INFD_ACCOUNT_STORAGE_SUPPORT_PASSWORD_CHANGE = 1 << 7
} InfdAccountStorageSupport;

/**
 * InfdAccountStorageInterface:
 * @get_support: Virtual function to get the list of supported operations
 * on the backend.
 * @list_accounts: Virtual function to obtain a list of all available accounts.
 * Can be %NULL if not supported by the backend.
 * @lookup_accounts: Virtual function to look up account by their identifier.
 * @lookup_accounts_by_name: Virtual function to reverse-lookup an account
 * identifier when given the account name.
 * @add_account: Virtual function to add a new account. Can be %NULL if not
 * supported by the backend.
 * @remove_account: Virtual function to remove an existing account. Can be
 * %NULL if not supported by the backend.
 * @login_by_certificate: Virtual function to look up the account associated
 * with a given certificate. Can be %NULL if not supported by the backend.
 * @login_by_password: Virtual function to check a username and password, and
 * return the associated account. Can be %NULL if not supported by the
 * backend.
 * @set_certificate: Set the certificate to be associated to a given account,
 * or %NULL if not supported.
 * @set_password: Set the password for a given account, or %NULL if not
 * supported.
 * @account_added: Default signal handler for the
 * #InfdAccountStorage::account-added signal.
 * @account_removed: Default signal handler for the
 * #InfdAccountStorage::account-removed signal.
 *
 * The virtual methods and default signal handlers of #InfdAccountStorage.
 * Implementing these allows an infinote server to set a specific source of
 * user accounts.
 */
struct _InfdAccountStorageInterface {
  /*< private >*/
  GTypeInterface parent;

  /*< public >*/

  InfdAccountStorageSupport (*get_support)(InfdAccountStorage* storage);

  InfAclAccount* (*lookup_accounts)(InfdAccountStorage* storage,
                                    const InfAclAccountId* accounts,
                                    guint n_accounts,
                                    GError** error);

  InfAclAccount* (*lookup_accounts_by_name)(InfdAccountStorage* storage,
                                            const gchar* name,
                                            guint* n_accounts,
                                            GError** error);

  InfAclAccount* (*list_accounts)(InfdAccountStorage* storage,
                                  guint* n_accounts,
                                  GError** error);

  InfAclAccountId (*add_account)(InfdAccountStorage* storage,
                                 const gchar* name,
                                 gnutls_x509_crt_t* certs,
                                 guint n_certs,
                                 const gchar* password,
                                 GError** error);

  gboolean (*remove_account)(InfdAccountStorage* storage,
                             InfAclAccountId account,
                             GError** error);

  InfAclAccountId (*login_by_certificate)(InfdAccountStorage* storage,
                                          gnutls_x509_crt_t cert,
                                          GError** error);

  InfAclAccountId (*login_by_password)(InfdAccountStorage* storage,
                                       const gchar* username,
                                       const gchar* password,
                                       GError** error);

  gboolean (*set_certificate)(InfdAccountStorage* storage,
                              InfAclAccountId account,
                              gnutls_x509_crt_t* certs,
                              guint n_certs,
                              GError** error);

  gboolean (*set_password)(InfdAccountStorage* storage,
                           InfAclAccountId account,
                           const gchar* password,
                           GError** error);

  /* Signals */
  void (*account_added)(InfdAccountStorage* storage,
                        const InfAclAccount* account);

  void (*account_removed)(InfdAccountStorage* storage,
                          const InfAclAccount* account);
};

GType
infd_account_storage_support_get_type(void) G_GNUC_CONST;

GType
infd_account_storage_get_type(void) G_GNUC_CONST;

InfdAccountStorageSupport
infd_account_storage_get_support(InfdAccountStorage* storage);

gboolean
infd_account_storage_supports(InfdAccountStorage* storage,
                              InfdAccountStorageSupport support);

InfAclAccount*
infd_account_storage_lookup_accounts(InfdAccountStorage* storage,
                                     const InfAclAccountId* accounts,
                                     guint n_accounts,
                                     GError** error);

InfAclAccount*
infd_account_storage_lookup_accounts_by_name(InfdAccountStorage* storage,
                                             const gchar* name,
                                             guint* n_accounts,
                                             GError** error);

InfAclAccount*
infd_account_storage_list_accounts(InfdAccountStorage* storage,
                                   guint* n_accounts,
                                   GError** error);

InfAclAccountId
infd_account_storage_add_account(InfdAccountStorage* storage,
                                 const gchar* name,
                                 gnutls_x509_crt_t* certs,
                                 guint n_certs,
                                 const gchar* password,
                                 GError** error);

gboolean
infd_account_storage_remove_account(InfdAccountStorage* storage,
                                    InfAclAccountId account,
                                    GError** error);

InfAclAccountId
infd_account_storage_login_by_certificate(InfdAccountStorage* storage,
                                          gnutls_x509_crt_t cert,
                                          GError** error);

InfAclAccountId
infd_account_storage_login_by_password(InfdAccountStorage* storage,
                                       const gchar* username,
                                       const gchar* password,
                                       GError** error);

gboolean
infd_account_storage_set_certificate(InfdAccountStorage* storage,
                                     InfAclAccountId account,
                                     gnutls_x509_crt_t* certs,
                                     guint n_certs,
                                     GError** error);

gboolean
infd_account_storage_set_password(InfdAccountStorage* storage,
                                  InfAclAccountId account,
                                  const gchar* password,
                                  GError** error);

void
infd_account_storage_account_added(InfdAccountStorage* storage,
                                   const InfAclAccount* account);

void
infd_account_storage_account_removed(InfdAccountStorage* storage,
                                     const InfAclAccount* account);

G_END_DECLS

#endif /* __INFD_ACCOUNT_STORAGE_H__ */

/* vim:set et sw=2 ts=2: */
