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
 * SECTION:infd-acl-account-info
 * @title: InfdAclAccountInfo
 * @short_description: Server account information
 * @include: libinfinity/server/infd-acl-account-info.h
 * @see_also: #InfAclAccount, #InfdDirectory
 * @stability: Unstable
 *
 * This structure contains all account information that is available on the
 * server side, including sensitive authentication information. This structure
 * is saved on storage, but only the #InfAclAccount part of it does ever
 * leave the server.
 */

#include <libinfinity/server/infd-acl-account-info.h>
#include <libinfinity/common/inf-xml-util.h>
#include <libinfinity/common/inf-error.h>
#include <libinfinity/inf-i18n.h>

#include <gnutls/gnutls.h>
#include <gnutls/crypto.h>

#include <string.h>

static gint64
infd_acl_account_info_get_real_time()
{
  /* TODO: Replace by g_get_real_time() once we depend on glib >=2.28 */
  GTimeVal timeval;
  g_get_current_time(&timeval);
  return (gint64)timeval.tv_sec * 1000000 + timeval.tv_usec;
}

GType
infd_acl_account_info_get_type(void)
{
  static GType acl_account_info_type = 0;

  if(!acl_account_info_type)
  {
    acl_account_info_type = g_boxed_type_register_static(
      "InfdAclAccountInfo",
      (GBoxedCopyFunc)infd_acl_account_info_copy,
      (GBoxedFreeFunc)infd_acl_account_info_free
    );
  }

  return acl_account_info_type;
}

/**
 * infd_acl_account_info_new:
 * @id: The unique account ID for the new account.
 * @name: A human readable account name, or %NULL.
 * @transient: Whether the account should be transient or not.
 *
 * Creates a new #InfdAclAccountInfo with the given ID and name. The @name
 * parameter is optional and allowed to be %NULL. The account is created with
 * no associated certificates, unset password and unspecified first and last
 * seen times (meaning the user was never seen).
 *
 * If @transient is set to %TRUE, the account is never stored to disk and only
 * exists for the lifetime of the current session.
 *
 * Returns: A new #InfdAclAccountInfo. Free with infd_acl_account_info_free()
 * when no longer needed.
 */
InfdAclAccountInfo*
infd_acl_account_info_new(const gchar* id,
                          const gchar* name,
                          gboolean transient)
{
  InfdAclAccountInfo* info;

  g_return_val_if_fail(id != NULL, NULL);

  info = g_slice_new(InfdAclAccountInfo);
  info->account.id = g_strdup(id);
  info->account.name = g_strdup(name);
  info->transient = transient;
  info->certificates = NULL;
  info->n_certificates = 0;
  info->password_salt = NULL;
  info->password_hash = NULL;
  info->first_seen = 0;
  info->last_seen = 0;

  return info;
}

/**
 * infd_acl_account_info_copy:
 * @info: The #InfdAclAccountInfo to copy.
 *
 * Creates a copy of @info.
 *
 * Returns: A new #InfdAclAccountInfo. Free with infd_acl_account_info_free()
 * when no longer needed.
 */
InfdAclAccountInfo*
infd_acl_account_info_copy(const InfdAclAccountInfo* info)
{
  InfdAclAccountInfo* new_info;
  guint i;

  g_return_val_if_fail(info != NULL, NULL);

  new_info = g_slice_new(InfdAclAccountInfo);
  new_info->account.id = g_strdup(info->account.id);
  new_info->account.name = g_strdup(info->account.name);
  if(info->n_certificates > 0)
  {
    new_info->certificates = g_malloc(sizeof(gchar*) * info->n_certificates);
    for(i = 0; i < info->n_certificates; ++i)
      new_info->certificates[i] = g_strdup(info->certificates[i]);
    new_info->n_certificates = info->n_certificates;
  }
  else
  {
    new_info->certificates = NULL;
    new_info->n_certificates = 0;
  }

  new_info->password_salt = g_strdup(info->password_salt);
  new_info->password_hash = g_strdup(info->password_hash);
  return new_info;
}

/**
 * infd_acl_account_info_free:
 * @info: A #InfdAclAccountInfo.
 *
 * Releases all resources allocated by @info.
 */
void
infd_acl_account_info_free(InfdAclAccountInfo* info)
{
  guint i;

  g_return_if_fail(info != NULL);

  g_free(info->password_hash);
  g_free(info->password_salt);

  for(i = 0; i < info->n_certificates; ++i)
    g_free(info->certificates[i]);
  g_free(info->certificates);

  g_free(info->account.name);
  g_free(info->account.id);

  g_slice_free(InfdAclAccountInfo, info);
}

/**
 * infd_acl_account_info_set_password:
 * @info: A #InfdAclAccountInfo.
 * @password: The new password for the account, or %NULL.
 * @error: Location to store error information, if any.
 *
 * Changes the password for the given account. If @password is %NULL the
 * password is unset, which means that it is not possible to log into this
 * account with password authentication.
 *
 * If @password is non-%NULL, a new random salt is generated and a SHA256
 * hash of the salt and the password is stored.
 *
 * If an error occurs while changing the password the functions set @error
 * and returns %FALSE.
 *
 * Returns: %TRUE in case of success or %FALSE otherwise.
 */
gboolean
infd_acl_account_info_set_password(InfdAclAccountInfo* info,
                                   const gchar* password,
                                   GError** error)
{
  gchar* new_salt;
  gchar* new_hash;

  gchar* salted_password;
  guint password_len;
  int res;

  g_return_val_if_fail(info != NULL, FALSE);

  if(password == NULL)
  {
    g_free(info->password_salt);
    g_free(info->password_hash);

    info->password_salt = NULL;
    info->password_hash = NULL;
  }
  else
  {
    new_salt = g_malloc(32);
    res = gnutls_rnd(GNUTLS_RND_RANDOM, new_salt, 32);
    if(res != GNUTLS_E_SUCCESS)
    {
      g_free(new_salt);
      inf_gnutls_set_error(error, res);
      return FALSE;
    }

    password_len = strlen(password);
    salted_password = g_malloc(32 + password_len);

    memcpy(salted_password, new_salt, 16);
    memcpy(salted_password + 16, password, password_len);
    memcpy(salted_password + 16 + password_len, new_salt + 16, 16);

    new_hash = g_malloc(gnutls_hash_get_len(GNUTLS_DIG_SHA256));

    res = gnutls_hash_fast(
      GNUTLS_DIG_SHA256,
      salted_password,
      32 + password_len,
      new_hash
    );

    g_free(salted_password);

    if(res != GNUTLS_E_SUCCESS)
    {
      g_free(new_hash);
      g_free(new_salt);
      inf_gnutls_set_error(error, res);
      return FALSE;
    }

    g_free(info->password_salt);
    g_free(info->password_hash);

    info->password_salt = new_salt;
    info->password_hash = new_hash;
  }

  return TRUE;
}

/**
 * infd_acl_account_info_check_password:
 * @info: A #InfdAclAccountInfo.
 * @password: The password to check.
 *
 * Check whether @password is the correct password to log into account.
 *
 * Returns: %TRUE if @password is correct or %FALSE otherwise.
 */
gboolean
infd_acl_account_info_check_password(const InfdAclAccountInfo* info,
                                     const gchar* password)
{
  guint password_len;
  gchar* salted_password;
  gchar* hash;
  int res;
  gchar cmp;
  guint i;

  g_return_val_if_fail(info != NULL, FALSE);
  g_return_val_if_fail(password != NULL, FALSE);

  password_len = strlen(password);
  salted_password = g_malloc(32 + password_len);

  memcpy(salted_password, info->password_salt, 16);
  memcpy(salted_password + 16, password, password_len);
  memcpy(salted_password + 16 + password_len, info->password_salt + 16, 16);

  hash = g_malloc(gnutls_hash_get_len(GNUTLS_DIG_SHA256));

  res = gnutls_hash_fast(
    GNUTLS_DIG_SHA256,
    salted_password,
    32 + password_len,
    hash
  );

  g_free(salted_password);

  if(res != GNUTLS_E_SUCCESS)
  {
    g_free(hash);
    return FALSE;
  }

  /* length-independent string compare */
  cmp = 0;
  for(i = 0; i < gnutls_hash_get_len(GNUTLS_DIG_SHA256); ++i)
    cmp |= (info->password_hash[i] ^ hash[i]);
  g_free(hash);

  if(cmp != 0)
    return FALSE;

  return TRUE;
}

/**
 * infd_acl_account_info_add_certificate:
 * @info: A #InfdAclAccountInfo.
 * @dn: A certificate DN.
 *
 * Registers a certificate with this account. This allows a client to log into
 * the account by showing a certificate with the given DN.
 */
void
infd_acl_account_info_add_certificate(InfdAclAccountInfo* info,
                                      const gchar* dn)
{
  g_return_if_fail(info != NULL);
  g_return_if_fail(dn != NULL);

  info->certificates = g_realloc(
    info->certificates,
    sizeof(gchar*) * (info->n_certificates + 1)
  );

  info->certificates[info->n_certificates] = g_strdup(dn);

  ++info->n_certificates;
}

/**
 * infd_acl_account_info_remove_certificate:
 * @info: A #InfdAclAccountInfo.
 * @dn: A certificate DN registered with @info.
 *
 * Removes the given DN from the list of certificates in @info.
 * Showing a certificate with the given DN no longer allows to log
 * into this account.
 */
void
infd_acl_account_info_remove_certificate(InfdAclAccountInfo* info,
                                         const gchar* dn)
{
  guint i;

  g_return_if_fail(info != NULL);
  g_return_if_fail(dn != NULL);

  for(i = 0; i < info->n_certificates; ++i)
  {
    if(strcmp(info->certificates[i], dn) == 0)
    {
      g_free(info->certificates[i]);
      info->certificates[i] = info->certificates[info->n_certificates - 1];

      info->certificates = g_realloc(
        info->certificates,
        sizeof(gchar*) * (info->n_certificates - 1)
      );

      --info->n_certificates;
      return;
    }
  }

  g_return_if_reached();
}

/**
 * infd_acl_account_info_update_time:
 * @info: A #InfdAclAccountInfo.
 *
 * Updates the last seen time of @info to the current time. Also, if the
 * first seen time is not set (0), it is set to the current time as well.
 */
void
infd_acl_account_info_update_time(InfdAclAccountInfo* info)
{
  gint64 current_time;

  g_return_if_fail(info != NULL);

  current_time = infd_acl_account_info_get_real_time();
  if(info->first_seen == 0)
    info->first_seen = current_time;
  info->last_seen = current_time;
}

/**
 * infd_acl_account_info_from_xml:
 * @xml: The XML node from which to read the account information.
 * @error: Location to store error information, if any.
 *
 * Reads information for one account from a serialized XML node. The account
 * info can be written to XML with the infd_acl_account_info_to_xml()
 * function. If the function fails it returns %NULL and @error is set.
 *
 * Returns: A #InfdAclAccountInfo, or %NULL. Free with
 * infd_acl_account_info_free() when no longer needed.
 */
InfdAclAccountInfo*
infd_acl_account_info_from_xml(xmlNodePtr xml,
                               GError** error)
{
  InfdAclAccountInfo* info;
  InfAclAccount* account;

  GError* local_error;
  gboolean has_first_seen;
  gdouble first_seen;
  gboolean has_last_seen;
  gdouble last_seen;

  xmlChar* password_salt;
  xmlChar* password_hash;
  gnutls_datum_t datum;
  size_t hash_len;
  int res;
  gchar* binary_salt;
  gchar* binary_hash;

  xmlNodePtr child;
  GPtrArray* certificate_array;
  guint i;

  g_return_val_if_fail(xml != NULL, NULL);
  g_return_val_if_fail(error == NULL || *error == NULL, NULL);

  local_error = NULL;

  has_first_seen = inf_xml_util_get_attribute_double(
    xml,
    "first-seen",
    &first_seen,
    &local_error
  );

  if(local_error != NULL)
    return NULL;

  has_last_seen = inf_xml_util_get_attribute_double(
    xml,
    "last-seen",
    &last_seen,
    &local_error
  );

  if(local_error != NULL)
    return NULL;

  account = inf_acl_account_from_xml(xml, error);
  if(account == NULL) return NULL;

  password_salt = inf_xml_util_get_attribute(xml, "password-salt");
  password_hash = inf_xml_util_get_attribute(xml, "password-hash");

  if( (password_salt != NULL && password_hash == NULL) ||
      (password_salt == NULL && password_hash != NULL))
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_INVALID_ATTRIBUTE,
      "%s",
      _("If one of \"password-hash\" or \"password-salt\" is provided, the "
        "other must be provided as well.")
    );

    if(password_salt != NULL) xmlFree(password_salt);
    if(password_hash != NULL) xmlFree(password_hash);

    inf_acl_account_free(account);
    return NULL;
  }

  if(password_salt != NULL && password_hash != NULL)
  {
    datum.data = password_salt;
    datum.size = strlen(password_salt);

    hash_len = 32;
    binary_salt = g_malloc(hash_len);
    res = gnutls_hex_decode(&datum, binary_salt, &hash_len);
    xmlFree(password_salt);

    if(hash_len != 32)
    {
      g_set_error(
        error,
        inf_request_error_quark(),
        INF_REQUEST_ERROR_INVALID_ATTRIBUTE,
        "%s",
        _("The length of the password salt is incorrect, it should "
          "be 32 bytes")
      );

      xmlFree(password_hash);
      g_free(binary_salt);
      return NULL;
    }
    else if(res != GNUTLS_E_SUCCESS)
    {
      inf_gnutls_set_error(error, res);
      xmlFree(password_hash);
      g_free(binary_salt);
      return NULL;
    }

    datum.data = password_hash;
    datum.size = strlen(password_hash);

    hash_len = gnutls_hash_get_len(GNUTLS_DIG_SHA256);
    binary_hash = g_malloc(hash_len);
    res = gnutls_hex_decode(&datum, binary_hash, &hash_len);
    xmlFree(password_hash);
  
    if(hash_len != gnutls_hash_get_len(GNUTLS_DIG_SHA256))
    {
      g_set_error(
        error,
        inf_request_error_quark(),
        INF_REQUEST_ERROR_INVALID_ATTRIBUTE,
        _("The length of the password hash is incorrect, it should be "
          "%u bytes"),
        (unsigned int)gnutls_hash_get_len(GNUTLS_DIG_SHA256)
      );

      g_free(binary_salt);
      g_free(binary_hash);
      return NULL;
    }
    else if(res != GNUTLS_E_SUCCESS)
    {
      inf_gnutls_set_error(error, res);
      g_free(binary_salt);
      g_free(binary_hash);
      return NULL;
    }
  }
  else
  {
    binary_salt = NULL;
    binary_hash = NULL;\

    if(password_salt != NULL) xmlFree(password_salt);
    if(password_hash != NULL) xmlFree(password_hash);
  }

  certificate_array = g_ptr_array_new();
  for(child = xml->children; child != NULL; child = child->next)
  {
    if(child->type != XML_ELEMENT_NODE) continue;
    if(strcmp((const char*)child->name, "certificate") == 0)
      g_ptr_array_add(certificate_array, xmlNodeGetContent(child));
  }

  info = infd_acl_account_info_new(account->id, account->name, FALSE);
  inf_acl_account_free(account);

  info->certificates = g_malloc(sizeof(gchar*) * certificate_array->len);
  for(i = 0; i < certificate_array->len; ++i)
  {
    info->certificates[i] = g_strdup(certificate_array->pdata[i]);
    xmlFree(certificate_array->pdata[i]);
  }
  
  info->n_certificates = certificate_array->len;
  g_ptr_array_free(certificate_array, TRUE);

  info->password_salt = binary_salt;
  info->password_hash = binary_hash;
  if(has_first_seen == TRUE)
    info->first_seen = first_seen * 1e6;
  else
    info->first_seen = 0;

  if(has_last_seen == TRUE)
    info->last_seen = last_seen * 1e6;
  else
    info->last_seen = 0;

  return info;
}

/**
 * infd_acl_account_info_to_xml:
 * @info: A #InfdAclAccountInfo.
 * @xml: XML node to write the account information to.
 *
 * Serializes a #InfdAclAccountInfo object into an XML node. The account
 * information can be deserialized again with
 * infd_acl_account_info_from_xml().
 */
void
infd_acl_account_info_to_xml(const InfdAclAccountInfo* info,
                             xmlNodePtr xml)
{
  guint i;

  gnutls_datum_t datum;
  size_t out_size;
  gchar* out;
  int res;

  g_return_if_fail(info != NULL);
  g_return_if_fail(xml != NULL);

  inf_acl_account_to_xml(&info->account, xml);

  for(i = 0; i < info->n_certificates; ++i)
  {
    xmlNewChild(
      xml,
      NULL,
      (const xmlChar*)"certificate",
      (const xmlChar*)info->certificates[i]
    );
  }

  if(info->password_salt != NULL)
  {
    datum.data = info->password_salt;
    datum.size = 32;

    res = gnutls_hex_encode(&datum, NULL, &out_size);
    g_assert(res == GNUTLS_E_SHORT_MEMORY_BUFFER);

    out = g_malloc(out_size + 1);
    res = gnutls_hex_encode(&datum, out, &out_size);
    g_assert(res == GNUTLS_E_SUCCESS);

    out[out_size] = '\0';
    inf_xml_util_set_attribute(xml, "password-salt", out);
    g_free(out);
  }

  if(info->password_hash != NULL)
  {
    datum.data = info->password_hash;
    datum.size = gnutls_hash_get_len(GNUTLS_DIG_SHA256);

    res = gnutls_hex_encode(&datum, NULL, &out_size);
    g_assert(res == GNUTLS_E_SHORT_MEMORY_BUFFER);

    out = g_malloc(out_size + 1);
    res = gnutls_hex_encode(&datum, out, &out_size);
    g_assert(res == GNUTLS_E_SUCCESS);

    out[out_size] = '\0';
    inf_xml_util_set_attribute(xml, "password-hash", out);
    g_free(out);
  }

  if(info->first_seen != 0)
  {
    inf_xml_util_set_attribute_double(
      xml,
      "first-seen",
      info->first_seen / 1e6
    );
  }

  if(info->last_seen != 0)
  {
    inf_xml_util_set_attribute_double(
      xml,
      "last-seen",
      info->last_seen / 1e6
    );
  }
}

/* vim:set et sw=2 ts=2: */
