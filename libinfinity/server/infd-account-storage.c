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

/**
 * SECTION:infd-account-storage
 * @title: InfdAccountStorage
 * @short_description: Interface to user account database
 * @include: libinfinity/server/infd-account-storage.h
 * @see_also: #InfdDirectory, #InfdFilesystemAccountStorage
 * @stability: Unstable
 *
 * #InfdAccountStorage provides an interface for #InfdDirectory to access a
 * database of user accounts. It provides methods to list and lookup available
 * accounts, and to add and remove accounts.
 *
 * Each account is uniquely identified by an account ID, #InfAclAccount.id,
 * and is assigned a human-readable name, #InfAclAccount.name. Typically, most
 * operations can be performed with the ID, and for example permissions for
 * users are stored by referring to the account ID in #InfdDirectory. The
 * authentication storage can be used to look up a name for the ID, and it is
 * responsible for storing the account information permanantly.
 *
 * Interface implementations do not need to support every operation provided
 * by the API of this interface, however if they support a certain operation,
 * #InfdDirectory takes advantage of it. The only required operation is to be
 * able to look up an account name by its ID, and the reverse lookup, i.e.
 * find all accounts with a given name. The
 * #InfdAccountStorageInterface.get_support() function returns a bitmask of the
 * supported operations.
 *
 * Implementations of this interface can couple the available accounts to
 * various external sources, such as SQL databases, LDAP or PAM.
 * Libinfinity also provides a standalone implementation of this interface,
 * which stores the account list as a file in the file system, see
 * #InfdFilesystemAccountStorage.
 */

#include <libinfinity/server/infd-account-storage.h>
#include <libinfinity/common/inf-error.h>
#include <libinfinity/inf-define-enum.h>
#include <libinfinity/inf-i18n.h>

static const GFlagsValue infd_account_storage_support_values[] = {
  {
    INFD_ACCOUNT_STORAGE_SUPPORT_NOTIFICATION,
    "INFD_ACCOUNT_STORAGE_SUPPORT_NOTIFICATION",
    "notification"
  }, {
    INFD_ACCOUNT_STORAGE_SUPPORT_LIST_ACCOUNTS,
    "INFD_ACCOUNT_STORAGE_SUPPORT_LIST_ACCOUNTS",
    "list-accounts"
  }, {
    INFD_ACCOUNT_STORAGE_SUPPORT_ADD_ACCOUNT,
    "INFD_ACCOUNT_STORAGE_SUPPORT_ADD_ACCOUNT",
    "add-account"
  }, {
    INFD_ACCOUNT_STORAGE_SUPPORT_REMOVE_ACCOUNT,
    "INFD_ACCOUNT_STORAGE_SUPPORT_REMOVE_ACCOUNT",
    "remove-account"
  }, {
    INFD_ACCOUNT_STORAGE_SUPPORT_CERTIFICATE_LOGIN,
    "INFD_ACCOUNT_STORAGE_SUPPORT_CERTIFICATE_LOGIN",
    "certificate-login"
  }, {
    INFD_ACCOUNT_STORAGE_SUPPORT_PASSWORD_LOGIN,
    "INFD_ACCOUNT_STORAGE_SUPPORT_PASSWORD_LOGIN",
    "password-login"
  }, {
    INFD_ACCOUNT_STORAGE_SUPPORT_CERTIFICATE_CHANGE,
    "INFD_ACCOUNT_STORAGE_SUPPORT_CERTIFICATE_CHANGE",
    "certificate-change"
  }, {
    INFD_ACCOUNT_STORAGE_SUPPORT_PASSWORD_CHANGE,
    "INFD_ACCOUNT_STORAGE_SUPPORT_PASSWORD_CHANGE",
    "password-change"
  }, {
    0,
    NULL,
    NULL
  }
};

INF_DEFINE_FLAGS_TYPE(InfdAccountStorageSupport, infd_account_storage_support, infd_account_storage_support_values)
G_DEFINE_INTERFACE(InfdAccountStorage, infd_account_storage, G_TYPE_OBJECT)

enum {
  ACCOUNT_ADDED,
  ACCOUNT_REMOVED,

  LAST_SIGNAL
};

static guint account_storage_signals[LAST_SIGNAL];

static void
infd_account_storage_default_init(InfdAccountStorageInterface* iface)
{
  /**
   * InfdAccountStorage::account-added:
   * @storage: The #InfdAccountStorage emitting the signal.
   * @info: The #InfAclAccount containing the account ID and account name
   * of the added account.
   *
   * This signal is emitted whenever an account has been added to the
   * account storage. However, the signal is only emitted if the storage
   * implementations supports the %INFD_ACCOUNT_STORAGE_SUPPORT_NOTIFICATION
   * support flag.
   */
  account_storage_signals[ACCOUNT_ADDED] = g_signal_new(
    "account-added",
    INFD_TYPE_ACCOUNT_STORAGE,
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfdAccountStorageInterface, account_added),
    NULL, NULL,
    g_cclosure_marshal_VOID__BOXED,
    G_TYPE_NONE,
    1,
    INF_TYPE_ACL_ACCOUNT | G_SIGNAL_TYPE_STATIC_SCOPE
  );

  /**
   * InfdAccountStorage::account-removed:
   * @storage: The #InfdAccountStorage emitting the signal.
   * @info: The #InfAclAccount containing the account ID and account name
   * of the removed account.
   * 
   * This signal is emitted whenever an account has been permanently removed
   * from the storage. However, the signal is only emitted if the storage
   * implementations supports the %INFD_ACCOUNT_STORAGE_SUPPORT_NOTIFICATION
   * support flag.
   */
  account_storage_signals[ACCOUNT_REMOVED] = g_signal_new(
    "account-removed",
    INFD_TYPE_ACCOUNT_STORAGE,
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfdAccountStorageInterface, account_removed),
    NULL, NULL,
    g_cclosure_marshal_VOID__BOXED,
    G_TYPE_NONE,
    1,
    INF_TYPE_ACL_ACCOUNT | G_SIGNAL_TYPE_STATIC_SCOPE
  );
}

/**
 * infd_account_storage_get_support:
 * @storage: A #InfdAccountStorage.
 *
 * Returns a bitmask of operations supported by the account storage backend.
 * If unsupported operations are attempted on @storage, an @error with code
 * %INF_DIRECTORY_ERROR_OPERATION_UNSUPPORTED will be generated.
 *
 * Returns: A bitmask of supported operations.
 */
InfdAccountStorageSupport
infd_account_storage_get_support(InfdAccountStorage* storage)
{
  InfdAccountStorageInterface* iface;

  g_return_val_if_fail(INFD_IS_ACCOUNT_STORAGE(storage), 0);

  iface = INFD_ACCOUNT_STORAGE_GET_IFACE(storage);
  g_return_val_if_fail(iface->get_support != NULL, 0);

  return iface->get_support(storage);
}

/**
 * infd_account_storage_supports:
 * @storage: A #InfdAccountStorage.
 * @support: A bitmask of operations to test.
 *
 * Checks whether support for all operations specified in @support is
 * available for @storage. This is equivalent to calling
 * infd_account_storage_get_support() and testing the returned value for
 * containing the bits in @support.
 *
 * Returns: %TRUE if all operations in @support are supported or %FALSE
 * otherwise.
 */
gboolean
infd_account_storage_supports(InfdAccountStorage* storage,
                              InfdAccountStorageSupport support)
{
  InfdAccountStorageSupport available_support;

  available_support = infd_account_storage_get_support(storage);

  if( (available_support & support) != support)
    return FALSE;

  return TRUE;
}

/**
 * infd_account_storage_lookup_accounts:
 * @storage: A #InfdAccountStorage.
 * @accounts: (array length=n_accounts): An array of #InfAclAccountId<!-- -->s
 * to look up.
 * @n_accounts: The number of elements in @accounts.
 * @error: Location to store error information, if any, or %NULL.
 *
 * Looks up the #InfAclAccount structure for all IDs present in @accounts.
 * The return value is an array of #InfAclAccount structures which is in the
 * same order as the @accounts array. If an element in the output array has
 * the #InfAclAccount.id field set to 0, it means that the account with the
 * corresponding ID in the @accounts array does not exist in @storage.
 *
 * Returns: (array length=n_accounts) (transfer full): An array of
 * #InfAclAccount structures with @n_accounts entries. Free with
 * inf_acl_account_array_free().
 */
InfAclAccount*
infd_account_storage_lookup_accounts(InfdAccountStorage* storage,
                                     const InfAclAccountId* accounts,
                                     guint n_accounts,
                                     GError** error)
{
  InfdAccountStorageInterface* iface;

  g_return_val_if_fail(INFD_IS_ACCOUNT_STORAGE(storage), NULL);
  g_return_val_if_fail(accounts != NULL, NULL);
  g_return_val_if_fail(n_accounts > 0, NULL);
  g_return_val_if_fail(error == NULL || *error == NULL, NULL);

  iface = INFD_ACCOUNT_STORAGE_GET_IFACE(storage);
  g_return_val_if_fail(iface->lookup_accounts != NULL, NULL);

  return iface->lookup_accounts(storage, accounts, n_accounts, error);
}

/**
 * infd_account_storage_lookup_accounts_by_name:
 * @storage: A #InfdAccountStorage.
 * @name: The name to look up.
 * @n_accounts: (out): An output parameter holding the number of returned
 * accounts.
 * @error: Location to store error information, if any, or %NULL.
 *
 * This function performs the &quot;reverse&quot; lookup: Given an account
 * name, the function returns an array with all accounts that have this name.
 * Many backends make sure that there cannot be duplicated names, in which
 * case this can at most return one account, however in principle accounts
 * with the same name are supported.
 *
 * If there is no account with the given name, the function returns %NULL and
 * sets @n_accounts to 0. If an error occurs, %NULL is returned, @n_accounts
 * is undefined, and @error is set. Therefore, to reliably find out whether a
 * lookup error occurred or this is no account with the given name, a non-%NULL
 * error pointer should be passed and checked after the function call.
 *
 * Returns: (array length=n_accounts) (transfer full): An array of
 * #InfAclAccount structures with length @n_accounts, or %NULL on error
 * or when @n_accounts is 0 or @error is set. Free with
 * inf_acl_account_array_free().
 */
InfAclAccount*
infd_account_storage_lookup_accounts_by_name(InfdAccountStorage* storage,
                                             const gchar* name,
                                             guint* n_accounts,
                                             GError** error)
{
  InfdAccountStorageInterface* iface;

  g_return_val_if_fail(INFD_IS_ACCOUNT_STORAGE(storage), NULL);
  g_return_val_if_fail(name != NULL, NULL);
  g_return_val_if_fail(n_accounts != NULL, NULL);
  g_return_val_if_fail(error == NULL || *error == NULL, NULL);

  iface = INFD_ACCOUNT_STORAGE_GET_IFACE(storage);
  g_return_val_if_fail(iface->lookup_accounts_by_name != NULL, NULL);

  return iface->lookup_accounts_by_name(storage, name, n_accounts, error);
}

/**
 * infd_account_storage_list_accounts:
 * @storage: A #InfdAccountStorage.
 * @n_accounts: (out): An output parameter holding the number of accounts
 * in @storage.
 * @error: Location to store error information, if any, or %NULL.
 *
 * Returns an array of all accounts in @storage. The length of the array
 * is stored in the output parameter @n_accounts. The functions returns %NULL
 * and sets @n_accounts to 0 if there are no accounts in @storage. If there is
 * an error, the function returns %NULL, @n_accounts is undefined, and @error
 * is set.  Therefore, to reliably find out whether an error occurred or
 * whether there are really no accounts present, a non-%NULL error pointer
 * should be passed and checked after the function call.
 *
 * Note that this function might not be supported by the backend. See
 * infd_account_storage_get_support().
 *
 * Returns: (array length=n_accounts) (allow-none) (transfer full): An array
 * of #InfAclAccount structures with length @n_accounts, or %NULL if
 * @n_accounts is 0 or @error is set. Free with
 * inf_acl_account_array_free().
 */
InfAclAccount*
infd_account_storage_list_accounts(InfdAccountStorage* storage,
                                   guint* n_accounts,
                                   GError** error)
{
  InfdAccountStorageInterface* iface;

  g_return_val_if_fail(INFD_IS_ACCOUNT_STORAGE(storage), NULL);
  g_return_val_if_fail(n_accounts != NULL, NULL);
  g_return_val_if_fail(error == NULL || *error == NULL, NULL);

  iface = INFD_ACCOUNT_STORAGE_GET_IFACE(storage);
  if(iface->list_accounts == NULL)
  {
    g_set_error_literal(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_OPERATION_UNSUPPORTED,
      _("The account backend does not support acount listing")
    );

    return NULL;
  }

  return iface->list_accounts(storage, n_accounts, error);
}

/**
 * infd_account_storage_add_account:
 * @storage: A #InfdAccountStorage.
 * @name: The name of the new account.
 * @certs: (array length=n_certs) (allow-none): An array of certificates that
 * can be used to login to the new account, or %NULL.
 * @n_certs: The length of the certificate array.
 * @password: (allow-none): A password that can be used to login to the
 * new account, or %NULL.
 * @error: Location to store error information, if any, or %NULL.
 *
 * Adds a new account to @storage. The account will have the given name. The
 * account ID is determined by the storage backend and if the operation is
 * successful it is returned.
 *
 * If the operation does not support storing certificates and/or passwords,
 * the function will fail if @certs or @password are not set to %NULL,
 * respectively. Note also that this function might not be supported at all
 * by the backend. See infd_account_storage_get_support().
 *
 * Returns: (transfer full): The #InfAclAccountId of the added account, or 0
 * on error.
 */
InfAclAccountId
infd_account_storage_add_account(InfdAccountStorage* storage,
                                 const gchar* name,
                                 gnutls_x509_crt_t* certs,
                                 guint n_certs,
                                 const gchar* password,
                                 GError** error)
{
  InfdAccountStorageInterface* iface;

  g_return_val_if_fail(INFD_IS_ACCOUNT_STORAGE(storage), 0);
  g_return_val_if_fail(name != NULL, 0);
  g_return_val_if_fail(certs == NULL || n_certs > 0, 0);
  g_return_val_if_fail(error == NULL || *error == NULL, 0);

  iface = INFD_ACCOUNT_STORAGE_GET_IFACE(storage);
  if(iface->add_account == NULL)
  {
    g_set_error_literal(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_OPERATION_UNSUPPORTED,
      _("The account backend does not support adding accounts")
    );

    return 0;
  }

  return iface->add_account(storage, name, certs, n_certs, password, error);
}

/**
 * infd_account_storage_remove_account:
 * @storage: A #InfdAccountStorage.
 * @account: The ID of the account to remove.
 * @error: Location to store error information, if any, or %NULL.
 *
 * Removes the account with the given ID from @storage.
 *
 * Note that this function might not be supported by the backend. See
 * infd_account_storage_get_support().
 *
 * Returns: %TRUE on success or %FALSE on error.
 */
gboolean
infd_account_storage_remove_account(InfdAccountStorage* storage,
                                    InfAclAccountId account,
                                    GError** error)
{
  InfdAccountStorageInterface* iface;

  g_return_val_if_fail(INFD_IS_ACCOUNT_STORAGE(storage), FALSE);
  g_return_val_if_fail(account != 0, FALSE);
  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

  iface = INFD_ACCOUNT_STORAGE_GET_IFACE(storage);
  if(iface->remove_account == NULL)
  {
    g_set_error_literal(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_OPERATION_UNSUPPORTED,
      _("The account backend does not support removing accounts")
    );

    return FALSE;
  }

  return iface->remove_account(storage, account, error);
}

/**
 * infd_account_storage_login_by_certificate:
 * @storage: A #InfdAccountStorage.
 * @cert: The certificate presented by the client.
 * @error: Location to store error information, if any, or %NULL.
 *
 * This function returns the ID of the account which belongs to the given
 * client certificate. If there is no such account on an error occurs,
 * the function returns 0, and, in the case of an error, @error is set as
 * well.
 *
 * Note that this function might not be supported by the backend. See
 * infd_account_storage_get_support().
 *
 * Returns: (transfer full): The account ID of the account associated to
 * @cert, or 0 on error or if there is no such account.
 */
InfAclAccountId
infd_account_storage_login_by_certificate(InfdAccountStorage* storage,
                                          gnutls_x509_crt_t cert,
                                          GError** error)
{
  InfdAccountStorageInterface* iface;

  g_return_val_if_fail(INFD_IS_ACCOUNT_STORAGE(storage), 0);
  g_return_val_if_fail(cert != 0, 0);
  g_return_val_if_fail(error == NULL || *error == NULL, 0);

  iface = INFD_ACCOUNT_STORAGE_GET_IFACE(storage);
  if(iface->login_by_certificate == NULL)
  {
    g_set_error_literal(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_OPERATION_UNSUPPORTED,
      _("The account backend does not support logging in with a certificate")
    );

    return 0;
  }

  return iface->login_by_certificate(storage, cert, error);
}

/**
 * infd_account_storage_login_by_password:
 * @storage: A #InfdAccountStorage.
 * @username: The username of the account to log into.
 * @password: The password of the corresponding account.
 * @error: Location to store error information, if any.
 *
 * This function returns the account ID which matches to the given username
 * and password. If there is no such account or if the password is incorrect,
 * the function returns 0. If an error occurs, the function returns 0 and
 * @error is set.
 *
 * Note that when the password is incorrect, @error is not set. It is only set
 * if there was an internal error and the login procedure could not be carried
 * out due to technical reasons, such as a database outage.
 *
 * Note that this function might not be supported by the backend. See
 * infd_account_storage_get_support().
 *
 * Returns: (transfer full): The account ID of the account associated to
 * @username if @password is correct, or 0 otherwise.
 */
InfAclAccountId
infd_account_storage_login_by_password(InfdAccountStorage* storage,
                                       const gchar* username,
                                       const gchar* password,
                                       GError** error)
{
  InfdAccountStorageInterface* iface;

  g_return_val_if_fail(INFD_IS_ACCOUNT_STORAGE(storage), 0);
  g_return_val_if_fail(username != 0, 0);
  g_return_val_if_fail(password != 0, 0);
  g_return_val_if_fail(error == NULL || *error == NULL, 0);

  iface = INFD_ACCOUNT_STORAGE_GET_IFACE(storage);
  if(iface->login_by_password == NULL)
  {
    g_set_error_literal(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_OPERATION_UNSUPPORTED,
      _("The account backend does not support logging in with a password")
    );

    return 0;
  }

  return iface->login_by_password(storage, username, password, error);
}

/**
 * infd_account_storage_set_certificate:
 * @storage: A #InfdAccountStorage.
 * @account: The ID of the account whose certificate to set.
 * @certs: (array length=n_certs) (allow-none): An array of certificates,
 * or %NULL if n_certs is 0.
 * @n_certs: The number of certificates in the certificate array.
 * @error: Location to store error information, if any.
 *
 * Changes the certificate(s) associated to the account with ID @account.
 * All certificates that are currently associated to it are removed, and the
 * given certificates are associated instead. If @n_certs is 0, there will
 * be no associated certificates and login by certificate will be disabled
 * for @account.
 *
 * Note that this function might not be supported by the backend. See
 * infd_account_storage_get_support().
 *
 * Returns: %TRUE if the operation was successful or %FALSE if an error
 * occurred.
 */
gboolean
infd_account_storage_set_certificate(InfdAccountStorage* storage,
                                     InfAclAccountId account,
                                     gnutls_x509_crt_t* certs,
                                     guint n_certs,
                                     GError** error)
{
  InfdAccountStorageInterface* iface;

  g_return_val_if_fail(INFD_IS_ACCOUNT_STORAGE(storage), FALSE);
  g_return_val_if_fail(account != 0, FALSE);
  g_return_val_if_fail(certs != NULL || n_certs == 0, FALSE);
  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

  iface = INFD_ACCOUNT_STORAGE_GET_IFACE(storage);
  if(iface->set_certificate == NULL)
  {
    g_set_error_literal(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_OPERATION_UNSUPPORTED,
      _("The account backend does not support changing the certificate")
    );

    return FALSE;
  }

  return iface->set_certificate(storage, account, certs, n_certs, error);
}

/**
 * infd_account_storage_set_password:
 * @storage: A #InfdAccountStorage.
 * @account: The ID of the account whose password to change.
 * @password: The new password for the account, or %NULL.
 * @error: Location to store error information, if any, or %NULL.
 *
 * Changes the password for the account with the given ID. If this call
 * succeeds, the new password will have to be provided to
 * infd_account_storage_login_by_password() for the login to succeed. If
 * @password is %NULL, the password will be unset and login by password
 * will no longer be possible.
 *
 * Note that this function might not be supported by the backend. See
 * infd_account_storage_get_support().
 *
 * Returns: %TRUE on success or %FALSE on error.
 */
gboolean
infd_account_storage_set_password(InfdAccountStorage* storage,
                                  InfAclAccountId account,
                                  const gchar* password,
                                  GError** error)
{
  InfdAccountStorageInterface* iface;

  g_return_val_if_fail(INFD_IS_ACCOUNT_STORAGE(storage), FALSE);
  g_return_val_if_fail(account != 0, FALSE);
  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

  iface = INFD_ACCOUNT_STORAGE_GET_IFACE(storage);
  if(iface->set_password == NULL)
  {
    g_set_error_literal(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_OPERATION_UNSUPPORTED,
      _("The account backend does not support changing the password")
    );

    return FALSE;
  }

  return iface->set_password(storage, account, password, error);
}

/**
 * infd_account_storage_account_added:
 * @storage: A #InfdAccountStorage.
 * @account: The #InfAclAccount that was added to the storage.
 *
 * Emits the #InfdAccountStorage::account-added signal on @storage. This
 * should only be used by interface implementations.
 */
void
infd_account_storage_account_added(InfdAccountStorage* storage,
                                   const InfAclAccount* account)
{
  g_return_if_fail(INFD_IS_ACCOUNT_STORAGE(storage));
  g_return_if_fail(account != NULL);
  
  g_return_if_fail(
    infd_account_storage_supports(
      storage,
      INFD_ACCOUNT_STORAGE_SUPPORT_NOTIFICATION
    )
  );

  g_signal_emit(
    G_OBJECT(storage),
    account_storage_signals[ACCOUNT_ADDED],
    0,
    account
  );
}

/**
 * infd_account_storage_account_removed:
 * @storage: A #InfdAccountStorage.
 * @account: The #InfAclAccount with the account information for the removed
 * account.
 *
 * Emits the #InfdAccountStorage::account-removed signal on @storage. This
 * should only be used by interface implementations.
 */
void
infd_account_storage_account_removed(InfdAccountStorage* storage,
                                     const InfAclAccount* account)
{
  g_return_if_fail(INFD_IS_ACCOUNT_STORAGE(storage));
  g_return_if_fail(account != NULL);
  
  g_return_if_fail(
    infd_account_storage_supports(
      storage,
      INFD_ACCOUNT_STORAGE_SUPPORT_NOTIFICATION
    )
  );

  g_signal_emit(
    G_OBJECT(storage),
    account_storage_signals[ACCOUNT_REMOVED],
    0,
    account
  );
}

/* vim:set et sw=2 ts=2: */
