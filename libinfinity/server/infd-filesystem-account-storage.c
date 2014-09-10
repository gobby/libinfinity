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
 * SECTION:infd-filesystem-account-storage
 * @short_description: Standalone account storage backend
 * @see_also: #InfdAccountStorage, #InfdDirectory
 * @include: libinfinity/server/infd-filesystem-account-storage.h
 * @stability: Unstable
 *
 * This class implements the #InfdAccountStorage interface via an underlying
 * #InfdFilesystemStorage. It uses the &quot;root-directory&quot; of that
 * underlying storage to store an XML file there which contains the account
 * information.
 *
 * This is a simple implementation of an account storage which keeps all
 * accounts read from the file in memory. When you have more than a thousand
 * accounts or so you should start thinking of using a more sophisticated
 * account storage, for example a database backend.
 **/

#include <libinfinity/server/infd-filesystem-account-storage.h>
#include <libinfinity/server/infd-account-storage.h>
#include <libinfinity/common/inf-cert-util.h>
#include <libinfinity/common/inf-xml-util.h>
#include <libinfinity/common/inf-error.h>
#include <libinfinity/inf-i18n.h>

#include <gnutls/gnutls.h>
#include <gnutls/crypto.h>

#include <string.h>

typedef struct _InfdFilesystemAccountStorageAccountInfo
  InfdFilesystemAccountStorageAccountInfo;
struct _InfdFilesystemAccountStorageAccountInfo {
  InfAclAccountId id;
  gchar* name;
  gchar** certificates;
  guint n_certificates;
  gchar* password_salt;
  gchar* password_hash;
  gint64 first_seen;
  gint64 last_seen;
};

typedef struct _InfdFilesystemAccountStoragePrivate InfdFilesystemAccountStoragePrivate;
struct _InfdFilesystemAccountStoragePrivate {
  InfdFilesystemStorage* filesystem;

  GHashTable* accounts; /* by ID */
  GHashTable* accounts_by_certificate; /* by certificate DN */
  GHashTable* accounts_by_name; /* by name */
  /* Note that we require names to be unique */
};

enum {
  PROP_0,

  PROP_FILESYSTEM_STORAGE
};

#define INFD_FILESYSTEM_ACCOUNT_STORAGE_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFD_TYPE_FILESYSTEM_ACCOUNT_STORAGE, InfdFilesystemAccountStoragePrivate))

static void infd_filesystem_account_storage_account_storage_iface_init(InfdAccountStorageInterface* iface);
G_DEFINE_TYPE_WITH_CODE(InfdFilesystemAccountStorage, infd_filesystem_account_storage, G_TYPE_OBJECT,
  G_ADD_PRIVATE(InfdFilesystemAccountStorage)
  G_IMPLEMENT_INTERFACE(INFD_TYPE_ACCOUNT_STORAGE, infd_filesystem_account_storage_account_storage_iface_init))

static GQuark
infd_filesystem_account_storage_error_quark(void)
{
  return g_quark_from_static_string("INFD_FILESYSTEM_ACCOUNT_STORAGE_ERROR");
}

static void
infd_filesystem_account_storage_account_info_free(gpointer ptr)
{
  InfdFilesystemAccountStorageAccountInfo* info;
  guint i;

  info = (InfdFilesystemAccountStorageAccountInfo*)ptr;

  g_free(info->name);
  for(i = 0; i < info->n_certificates; ++i)
    g_free(info->certificates[i]);
  g_free(info->certificates);

  g_free(info->password_salt);
  g_free(info->password_hash);

  g_slice_free(InfdFilesystemAccountStorageAccountInfo, info);
}

static InfdFilesystemAccountStorageAccountInfo*
infd_filesystem_account_storage_account_info_from_xml(xmlNodePtr xml,
                                                      GError** error)
{
  InfdFilesystemAccountStorageAccountInfo* info;

  xmlChar* id;
  xmlChar* name;
  InfAclAccountId account_id;

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
  {
    g_propagate_error(error, local_error);
    return NULL;
  }

  has_last_seen = inf_xml_util_get_attribute_double(
    xml,
    "last-seen",
    &last_seen,
    &local_error
  );

  if(local_error != NULL)
  {
    g_propagate_error(error, local_error);
    return NULL;
  }

  id = inf_xml_util_get_attribute_required(xml, "id", error);
  if(id == NULL) return NULL;

  account_id = inf_acl_account_id_from_string((const gchar*)id);
  xmlFree(id);

  name = inf_xml_util_get_attribute_required(xml, "name", error);
  if(name == NULL) return NULL;

  password_salt = inf_xml_util_get_attribute(xml, "password-salt");
  password_hash = inf_xml_util_get_attribute(xml, "password-hash");

  if( (password_salt != NULL && password_hash == NULL) ||
      (password_salt == NULL && password_hash != NULL))
  {
    g_set_error(
      error,
      infd_filesystem_account_storage_error_quark(),
      INFD_FILESYSTEM_ACCOUNT_STORAGE_ERROR_INVALID_FORMAT,
      "%s",
      _("If one of \"password-hash\" or \"password-salt\" is provided, the "
        "other must be provided as well.")
    );

    if(password_salt != NULL) xmlFree(password_salt);
    if(password_hash != NULL) xmlFree(password_hash);

    xmlFree(name);
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
        infd_filesystem_account_storage_error_quark(),
        INFD_FILESYSTEM_ACCOUNT_STORAGE_ERROR_INVALID_FORMAT,
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
      xmlFree(name);
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
        infd_filesystem_account_storage_error_quark(),
        INFD_FILESYSTEM_ACCOUNT_STORAGE_ERROR_INVALID_FORMAT,
        _("The length of the password hash is incorrect, it should be "
          "%u bytes"),
        (unsigned int)gnutls_hash_get_len(GNUTLS_DIG_SHA256)
      );

      g_free(binary_salt);
      g_free(binary_hash);
      xmlFree(name);
      return NULL;
    }
    else if(res != GNUTLS_E_SUCCESS)
    {
      inf_gnutls_set_error(error, res);
      g_free(binary_salt);
      g_free(binary_hash);
      xmlFree(name);
      return NULL;
    }
  }
  else
  {
    binary_salt = NULL;
    binary_hash = NULL;

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

  info = g_slice_new(InfdFilesystemAccountStorageAccountInfo);
  info->id = account_id;
  info->name = g_strdup((const gchar*)name);
  xmlFree(name);

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

static void
infd_filesystem_account_storage_account_info_to_xml(
  const InfdFilesystemAccountStorageAccountInfo* info,
  xmlNodePtr xml)
{
  guint i;

  gnutls_datum_t datum;
  size_t out_size;
  gchar* out;
  int res;

  g_return_if_fail(info != NULL);
  g_return_if_fail(xml != NULL);

  inf_xml_util_set_attribute(
    xml,
    "id",
    inf_acl_account_id_to_string(info->id)
  );

  inf_xml_util_set_attribute(xml, "name", info->name);

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

void
infd_filesystem_account_storage_account_info_update_time(
  InfdFilesystemAccountStorageAccountInfo* info)
{
  GTimeVal timeval;
  gint64 current_time;

  g_get_current_time(&timeval);
  current_time = (gint64)timeval.tv_sec * 1000000 + timeval.tv_usec;

  if(info->first_seen == 0)
    info->first_seen = current_time;
  info->last_seen = current_time;
}

static gchar*
infd_filesystem_account_storage_generate_salt(GError** error)
{
  gchar* salt;
  int res;

  salt = g_malloc(32);
  res = gnutls_rnd(GNUTLS_RND_RANDOM, salt, 32);
  if(res != GNUTLS_E_SUCCESS)
  {
    g_free(salt);
    inf_gnutls_set_error(error, res);
    return NULL;
  }

  return salt;
}

static gchar*
infd_filesystem_account_storage_hash_password(const gchar* password,
                                              const gchar* salt,
                                              GError** error)
{
  guint password_len;
  gchar* salted_password;
  gchar* hash;
  int res;

  password_len = strlen(password);
  salted_password = g_malloc(32 + password_len);

  memcpy(salted_password, salt, 16);
  memcpy(salted_password + 16, password, password_len);
  memcpy(salted_password + 16 + password_len, salt + 16, 16);

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
    inf_gnutls_set_error(error, res);
    return FALSE;
  }

  return hash;
}

static GHashTable*
infd_filesystem_account_storage_load_file(InfdFilesystemStorage* storage,
                                          GError** error)
{
  GHashTable* table;
  GError* local_error;
  xmlDocPtr doc;
  xmlNodePtr root;
  xmlNodePtr child;
  InfdFilesystemAccountStorageAccountInfo* info;
  gpointer id_ptr;

  table = g_hash_table_new_full(
    NULL,
    NULL,
    NULL,
    infd_filesystem_account_storage_account_info_free
  );

  local_error = NULL;

  doc = infd_filesystem_storage_read_xml_file(
    storage,
    "xml",
    "accounts",
    "inf-acl-account-list",
    &local_error
  );

  if(local_error != NULL)
  {
    if(local_error->domain == G_FILE_ERROR &&
       local_error->code == G_FILE_ERROR_NOENT)
    {
      /* The account file does not exist. This is not an error, but just means
       * the account list is empty. */
      g_error_free(local_error);
      return table;
    }

    g_propagate_error(error, local_error);
    g_hash_table_destroy(table);
    return NULL;
  }

  root = xmlDocGetRootElement(doc);
  for(child = root->children; child != NULL; child = child->next)
  {
    if(child->type != XML_ELEMENT_NODE) continue;

    if(strcmp((const char*)child->name, "account") == 0)
    {
      info = infd_filesystem_account_storage_account_info_from_xml(
        child,
        error
      );

      if(info == NULL)
      {
        g_hash_table_destroy(table);
        return NULL;
      }
      
      id_ptr = INF_ACL_ACCOUNT_ID_TO_POINTER(info->id);
      if(g_hash_table_lookup(table, id_ptr) != NULL)
      {
        g_set_error(
          error,
          infd_filesystem_account_storage_error_quark(),
          INFD_FILESYSTEM_ACCOUNT_STORAGE_ERROR_INVALID_FORMAT,
          _("Duplicate account ID \"%s\" in file \"%s\""),
          inf_acl_account_id_to_string(info->id),
          doc->name
        );

        g_hash_table_destroy(table);
        return NULL;
      }

      g_hash_table_insert(table, id_ptr, info);
    }
  }

  xmlFreeDoc(doc);
  return table;
}

/* Given an accounts table, this fills the reverse lookup tables */
static gboolean
infd_filesystem_account_storage_xref_account_table(GHashTable* accounts,
                                                   GHashTable* by_certificate,
                                                   GHashTable* by_name,
                                                   GError** error)
{
  GHashTableIter hash_iter;
  gpointer value;
  InfdFilesystemAccountStorageAccountInfo* info;
  InfdFilesystemAccountStorageAccountInfo* other_info;
  guint i;

  g_hash_table_iter_init(&hash_iter, accounts);
  while(g_hash_table_iter_next(&hash_iter, NULL, &value))
  {
    info = (InfdFilesystemAccountStorageAccountInfo*)value;

    if(info->name != NULL)
    {
      other_info = g_hash_table_lookup(by_name, info->name);
      if(other_info != NULL)
      {
        g_set_error(
          error,
          infd_filesystem_account_storage_error_quark(),
          INFD_FILESYSTEM_ACCOUNT_STORAGE_ERROR_DUPLICATE_NAME,
          _("Accounts \"%s\" and \"%s\" have the same name. This is not "
            "supported by InfdFilesystemAccountStorage."),
          inf_acl_account_id_to_string(other_info->id),
          inf_acl_account_id_to_string(info->id)
        );

        return FALSE;
      }

      g_hash_table_insert(by_name, info->name, info);
    }

    for(i = 0; i < info->n_certificates; ++i)
    {
      other_info = g_hash_table_lookup(by_certificate, info->certificates[i]);
      if(other_info != NULL)
      {
        g_set_error(
          error,
          infd_filesystem_account_storage_error_quark(),
          INFD_FILESYSTEM_ACCOUNT_STORAGE_ERROR_DUPLICATE_CERTIFICATE,
          _("Accounts \"%s\" and \"%s\" have the same certificate with "
            "DN=%s. This is not supported by InfdFilesystemAccountStorage."),
          inf_acl_account_id_to_string(other_info->id),
          inf_acl_account_id_to_string(info->id),
          info->certificates[i]
        );

        return FALSE;
      }

      g_hash_table_insert(by_certificate, info->certificates[i], info);
    }
  }

  return TRUE;
}

static gboolean
infd_filesystem_account_storage_store_file(InfdFilesystemStorage* storage,
                                           GHashTable* table,
                                           GError** error)
{
  xmlNodePtr root;
  xmlNodePtr child;

  GHashTableIter hash_iter;
  gpointer value;
  InfdFilesystemAccountStorageAccountInfo* info;

  xmlDocPtr doc;
  gboolean result;

  root = xmlNewNode(NULL, (const xmlChar*)"inf-acl-account-list");

  g_hash_table_iter_init(&hash_iter, table);
  while(g_hash_table_iter_next(&hash_iter, NULL, &value))
  {
    info = (InfdFilesystemAccountStorageAccountInfo*)value;
    child = xmlNewChild(root, NULL, (const xmlChar*)"account", NULL);
    infd_filesystem_account_storage_account_info_to_xml(info, child);
  }

  doc = xmlNewDoc((const xmlChar*)"1.0");
  xmlDocSetRootElement(doc, root);

  result = infd_filesystem_storage_write_xml_file(
    storage,
    "xml",
    "accounts",
    doc,
    error
  );

  xmlFreeDoc(doc);
  return result;
}

static gboolean
infd_filesystem_account_storage_set_filesystem_impl(
    InfdFilesystemAccountStorage* s,
    InfdFilesystemStorage* fs,
    GError** error)
{
  InfdFilesystemAccountStoragePrivate* priv;
  gboolean success;

  GHashTable* old_accounts;
  GHashTable* old_accounts_by_name;
  GHashTable* old_accounts_by_certificate;

  GHashTable* new_accounts;
  GHashTable* new_accounts_by_name;
  GHashTable* new_accounts_by_certificate;

  GHashTableIter hash_iter;
  gpointer id_ptr;
  gpointer value;
  InfdFilesystemAccountStorageAccountInfo* info;
  InfAclAccount notify_account;

  priv = INFD_FILESYSTEM_ACCOUNT_STORAGE_PRIVATE(s);
  if(priv->filesystem == fs) return TRUE;

  /* Load the new accounts */
  new_accounts = infd_filesystem_account_storage_load_file(fs, error);
  if(new_accounts == NULL) return FALSE;

  new_accounts_by_certificate = g_hash_table_new(g_str_hash, g_str_equal);
  new_accounts_by_name = g_hash_table_new(g_str_hash, g_str_equal);

  success = infd_filesystem_account_storage_xref_account_table(
    new_accounts,
    new_accounts_by_certificate,
    new_accounts_by_name,
    error
  );

  if(success == FALSE)
  {
    g_hash_table_destroy(new_accounts_by_certificate);
    g_hash_table_destroy(new_accounts_by_name);
    g_hash_table_destroy(new_accounts);
    return FALSE;
  }

  if(priv->filesystem != NULL)
    g_object_unref(priv->filesystem);

  priv->filesystem = fs;
  if(fs != NULL)
    g_object_ref(fs);

  /* TODO: We should connect to notify::root-directory, and if the root
   * directory changes, re-load the file and update our accounts, emitting
   * signals for removed and added accounts. */

  /* Okay, new account table is consistent. */
  old_accounts = priv->accounts;
  old_accounts_by_name = priv->accounts_by_name;
  old_accounts_by_certificate = priv->accounts_by_certificate;

  priv->accounts = new_accounts;
  priv->accounts_by_name = new_accounts_by_name;
  priv->accounts_by_certificate = new_accounts_by_certificate;

  /* Notify about changed accounts */
  g_hash_table_iter_init(&hash_iter, old_accounts);
  while(g_hash_table_iter_next(&hash_iter, &id_ptr, &value))
  {
    info = (InfdFilesystemAccountStorageAccountInfo*)value;

    if(g_hash_table_lookup(new_accounts, id_ptr) == NULL)
    {
      notify_account.id = info->id;
      notify_account.name = info->name;

      infd_account_storage_account_removed(
        INFD_ACCOUNT_STORAGE(s),
        &notify_account
      );
    }
  }

  g_hash_table_iter_init(&hash_iter, new_accounts);
  while(g_hash_table_iter_next(&hash_iter, &id_ptr, &value))
  {
    info = (InfdFilesystemAccountStorageAccountInfo*)value;

    if(g_hash_table_lookup(old_accounts, id_ptr) == NULL)
    {
      notify_account.id = info->id;
      notify_account.name = info->name;

      infd_account_storage_account_added(
        INFD_ACCOUNT_STORAGE(s),
        &notify_account
      );
    }
  }

  g_hash_table_destroy(old_accounts_by_certificate);
  g_hash_table_destroy(old_accounts_by_name);
  g_hash_table_destroy(old_accounts);
  return TRUE;
}

/* Note this does no collission checks */
static void
infd_filesystem_account_storage_add_info(
  InfdFilesystemAccountStorage* storage,
  InfdFilesystemAccountStorageAccountInfo* info)
{
  InfdFilesystemAccountStoragePrivate* priv;
  guint i;

  priv = INFD_FILESYSTEM_ACCOUNT_STORAGE_PRIVATE(storage);

  g_hash_table_insert(
    priv->accounts,
    INF_ACL_ACCOUNT_ID_TO_POINTER(info->id),
    info
  );

  g_hash_table_insert(priv->accounts_by_name, info->name, info);

  for(i = 0; i < info->n_certificates; ++i)
  {
    g_hash_table_insert(
      priv->accounts_by_certificate,
      info->certificates[i],
      info
    );
  }
}

static void
infd_filesystem_account_storage_remove_info(
  InfdFilesystemAccountStorage* storage,
  InfdFilesystemAccountStorageAccountInfo* info)
{
  InfdFilesystemAccountStoragePrivate* priv;
  guint i;

  priv = INFD_FILESYSTEM_ACCOUNT_STORAGE_PRIVATE(storage);

  for(i = 0; i < info->n_certificates; ++i)
    g_hash_table_remove(priv->accounts_by_certificate, info->certificates[i]);
  g_hash_table_remove(priv->accounts_by_name, info->name);
  g_hash_table_steal(priv->accounts, INF_ACL_ACCOUNT_ID_TO_POINTER(info->id));
}

static void
infd_filesystem_account_storage_init(InfdFilesystemAccountStorage* storage)
{
  InfdFilesystemAccountStoragePrivate* priv;
  priv = INFD_FILESYSTEM_ACCOUNT_STORAGE_PRIVATE(storage);

  priv->filesystem = NULL;

  priv->accounts = g_hash_table_new_full(
    NULL,
    NULL,
    NULL,
    infd_filesystem_account_storage_account_info_free
  );

  priv->accounts_by_certificate = g_hash_table_new(
    g_str_hash,
    g_str_equal
  );

  priv->accounts_by_name = g_hash_table_new(
    g_str_hash,
    g_str_equal
  );
}

static void
infd_filesystem_account_storage_dispose(GObject* object)
{
  InfdFilesystemAccountStorage* storage;
  InfdFilesystemAccountStoragePrivate* priv;

  storage = INFD_FILESYSTEM_ACCOUNT_STORAGE(object);
  priv = INFD_FILESYSTEM_ACCOUNT_STORAGE_PRIVATE(storage);

  if(priv->filesystem != NULL)
  {
    g_object_unref(priv->filesystem);
    priv->filesystem = NULL;
  }

  G_OBJECT_CLASS(infd_filesystem_account_storage_parent_class)->dispose(object);
}

static void
infd_filesystem_account_storage_finalize(GObject* object)
{
  InfdFilesystemAccountStorage* storage;
  InfdFilesystemAccountStoragePrivate* priv;

  storage = INFD_FILESYSTEM_ACCOUNT_STORAGE(object);
  priv = INFD_FILESYSTEM_ACCOUNT_STORAGE_PRIVATE(storage);

  g_hash_table_destroy(priv->accounts_by_name);
  g_hash_table_destroy(priv->accounts_by_certificate);
  g_hash_table_destroy(priv->accounts);

  G_OBJECT_CLASS(infd_filesystem_account_storage_parent_class)->finalize(object);
}

static void
infd_filesystem_account_storage_set_property(GObject* object,
                                             guint prop_id,
                                             const GValue* value,
                                             GParamSpec* pspec)
{
  InfdFilesystemAccountStorage* storage;
  InfdFilesystemAccountStoragePrivate* priv;
  GError* error;

  storage = INFD_FILESYSTEM_ACCOUNT_STORAGE(object);
  priv = INFD_FILESYSTEM_ACCOUNT_STORAGE_PRIVATE(storage);

  switch(prop_id)
  {
  case PROP_FILESYSTEM_STORAGE:
    error = NULL;

    infd_filesystem_account_storage_set_filesystem_impl(
      storage,
      INFD_FILESYSTEM_STORAGE(g_value_get_object(value)),
      &error
    );

    if(error != NULL)
    {
      g_warning(
        _("Failed to read accounts from filesystem: %s"),
        error->message
      );

      g_error_free(error);
    }

    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infd_filesystem_account_storage_get_property(GObject* object,
                                             guint prop_id,
                                             GValue* value,
                                             GParamSpec* pspec)
{
  InfdFilesystemAccountStorage* storage;
  InfdFilesystemAccountStoragePrivate* priv;

  storage = INFD_FILESYSTEM_ACCOUNT_STORAGE(object);
  priv = INFD_FILESYSTEM_ACCOUNT_STORAGE_PRIVATE(storage);

  switch(prop_id)
  {
  case PROP_FILESYSTEM_STORAGE:
    g_value_set_object(value, G_OBJECT(priv->filesystem));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static InfdAccountStorageSupport
infd_filesystem_account_storage_get_support(InfdAccountStorage* storage)
{
  /* We support everything. Note that we don't do notifications, since the
   * account storage cannot be modified externally (if the file is modified
   * we don't pick it up). The only point where we make notifications is when
   * the underlying filesystem is changed. */
  return INFD_ACCOUNT_STORAGE_SUPPORT_NOTIFICATION |
         INFD_ACCOUNT_STORAGE_SUPPORT_LIST_ACCOUNTS |
         INFD_ACCOUNT_STORAGE_SUPPORT_ADD_ACCOUNT |
         INFD_ACCOUNT_STORAGE_SUPPORT_REMOVE_ACCOUNT |
         INFD_ACCOUNT_STORAGE_SUPPORT_CERTIFICATE_LOGIN |
         INFD_ACCOUNT_STORAGE_SUPPORT_PASSWORD_LOGIN |
         INFD_ACCOUNT_STORAGE_SUPPORT_CERTIFICATE_CHANGE |
         INFD_ACCOUNT_STORAGE_SUPPORT_PASSWORD_CHANGE;
}

static InfAclAccount*
infd_filesystem_account_storage_lookup_accounts(InfdAccountStorage* s,
                                                const InfAclAccountId* ids,
                                                guint n_accounts,
                                                GError** error)
{
  InfdFilesystemAccountStorage* storage;
  InfdFilesystemAccountStoragePrivate* priv;

  InfAclAccount* result;
  guint i;
  InfdFilesystemAccountStorageAccountInfo* info;

  storage = INFD_FILESYSTEM_ACCOUNT_STORAGE(s);
  priv = INFD_FILESYSTEM_ACCOUNT_STORAGE_PRIVATE(storage);

  result = g_malloc(sizeof(InfAclAccount) * n_accounts);

  for(i = 0; i < n_accounts; ++i)
  {
    info = g_hash_table_lookup(
      priv->accounts,
      INF_ACL_ACCOUNT_ID_TO_POINTER(ids[i])
    );

    if(info != NULL)
    {
      result[i].id = ids[i];
      result[i].name = g_strdup(info->name);
    }
    else
    {
      result[i].id = 0;
      result[i].name = NULL;
    }
  }

  return result;
}

static InfAclAccount*
infd_filesystem_account_storage_lookup_accounts_by_name(InfdAccountStorage* s,
                                                        const gchar* name,
                                                        guint* n_accounts,
                                                        GError** error)
{
  InfdFilesystemAccountStorage* storage;
  InfdFilesystemAccountStoragePrivate* priv;
  InfdFilesystemAccountStorageAccountInfo* info;
  InfAclAccount* result;

  storage = INFD_FILESYSTEM_ACCOUNT_STORAGE(s);
  priv = INFD_FILESYSTEM_ACCOUNT_STORAGE_PRIVATE(storage);

  info = g_hash_table_lookup(priv->accounts_by_name, name);
  if(info == NULL)
  {
    *n_accounts = 0;
    return NULL;
  }

  *n_accounts = 1;
  result = g_malloc(sizeof(InfAclAccount));
  result->id = info->id;
  result->name = g_strdup(info->name);
  return result;
}

static InfAclAccount*
infd_filesystem_account_storage_list_accounts(InfdAccountStorage* s,
                                              guint* n_accounts,
                                              GError** error)
{
  InfdFilesystemAccountStorage* storage;
  InfdFilesystemAccountStoragePrivate* priv;
  GHashTableIter hash_iter;
  gpointer value;
  InfdFilesystemAccountStorageAccountInfo* info;
  InfAclAccount* result;
  guint index;

  storage = INFD_FILESYSTEM_ACCOUNT_STORAGE(s);
  priv = INFD_FILESYSTEM_ACCOUNT_STORAGE_PRIVATE(storage);

  *n_accounts = g_hash_table_size(priv->accounts);
  if(*n_accounts == 0) return NULL;

  result = g_malloc( (*n_accounts) * sizeof(InfAclAccount));

  index = 0;
  g_hash_table_iter_init(&hash_iter, priv->accounts);
  while(g_hash_table_iter_next(&hash_iter, NULL, &value))
  {
    info = (InfdFilesystemAccountStorageAccountInfo*)value;
    result[index].id = info->id;
    result[index].name = g_strdup(info->name);
    ++index;
  }

  g_assert(index == *n_accounts);
  return result;
}

static InfAclAccountId
infd_filesystem_account_storage_add_account(InfdAccountStorage* s,
                                            const gchar* name,
                                            gnutls_x509_crt_t* certs,
                                            guint n_certs,
                                            const gchar* password,
                                            GError** error)
{
  InfdFilesystemAccountStorage* storage;
  InfdFilesystemAccountStoragePrivate* priv;
  InfdFilesystemAccountStorageAccountInfo* info;
  guint i;

  gchar* dn;
  gchar* fingerprint;
  gchar* password_salt;
  gchar* password_hash;
  gchar* id_str;
  InfAclAccountId id;

  gboolean success;

  storage = INFD_FILESYSTEM_ACCOUNT_STORAGE(s);
  priv = INFD_FILESYSTEM_ACCOUNT_STORAGE_PRIVATE(storage);

  /* Validity checks: */
  info = g_hash_table_lookup(priv->accounts_by_name, name);
  if(info != NULL)
  {
    g_set_error(
      error,
      infd_filesystem_account_storage_error_quark(),
      INFD_FILESYSTEM_ACCOUNT_STORAGE_ERROR_DUPLICATE_NAME,
      _("There is already an account with name \"%s\""),
      name
    );

    return 0;
  }

  if(strlen(name) > 48)
  {
    g_set_error(
      error,
      infd_filesystem_account_storage_error_quark(),
      INFD_FILESYSTEM_ACCOUNT_STORAGE_ERROR_INVALID_FORMAT,
      "%s",
      _("The account name is too long")
    );

    return 0;
  }

  for(i = 0; i < n_certs; ++i)
  {
    dn = inf_cert_util_get_dn(certs[i]);

    info = g_hash_table_lookup(priv->accounts_by_certificate, dn);
    if(info != NULL)
    {
      g_set_error(
        error,
        infd_filesystem_account_storage_error_quark(),
        INFD_FILESYSTEM_ACCOUNT_STORAGE_ERROR_DUPLICATE_CERTIFICATE,
        _("There is already an account with a certificate with DN=\"%s\""),
        dn
      );

      g_free(dn);
      return 0;
    }

    g_free(dn);

    /* Also check for the fingerprint, in case some old directories still
     * use fingerprints. */
    fingerprint = inf_cert_util_get_fingerprint(certs[i], GNUTLS_DIG_SHA256);

    info = g_hash_table_lookup(priv->accounts_by_certificate, fingerprint);
    if(info != NULL)
    {
      g_set_error(
        error,
        infd_filesystem_account_storage_error_quark(),
        INFD_FILESYSTEM_ACCOUNT_STORAGE_ERROR_DUPLICATE_CERTIFICATE,
        _("There is already an account with a "
          "certificate with fingerprint=\"%s\""),
        fingerprint
      );

      g_free(fingerprint);
      return 0;
    }

    g_free(fingerprint);
  }

  if(password != NULL)
  {
    password_salt = infd_filesystem_account_storage_generate_salt(error);
    if(password_salt == NULL) return 0;

    password_hash = infd_filesystem_account_storage_hash_password(
      password,
      password_salt,
      error
    );

    if(password_hash == NULL)
    {
      g_free(password_salt);
      return 0;
    }
  }
  else
  {
    password_salt = NULL;
    password_hash = NULL;
  }

  /* Okay, create the account. First, choose an ID */
  for(i = 0; i < 10000; ++i)
  {
    id_str = g_strdup_printf("fs:user:%s:%x", name, g_random_int());
    if(g_hash_table_lookup(priv->accounts, id_str) == NULL)
      break;

    g_free(id_str);
    id_str = NULL;
  }

  if(id_str == NULL)
  {
    g_set_error(
      error,
      infd_filesystem_account_storage_error_quark(),
      INFD_FILESYSTEM_ACCOUNT_STORAGE_ERROR_IDS_EXHAUSTED,
      _("Could not generate a unique ID for account with name \"%s\""),
      name
    );

    g_free(password_hash);
    g_free(password_salt);
    return 0;
  }

  id = inf_acl_account_id_from_string(id_str);
  g_free(id_str);

  info = g_slice_new(InfdFilesystemAccountStorageAccountInfo);
  info->id = id;
  info->name = g_strdup(name);
  info->certificates = g_malloc(sizeof(gchar*) * n_certs);
  info->n_certificates = n_certs;
  for(i = 0; i < n_certs; ++i)
    info->certificates[i] = inf_cert_util_get_dn(certs[i]);
  info->password_salt = password_salt;
  info->password_hash = password_hash;
  info->first_seen = 0;
  info->last_seen = 0;

  infd_filesystem_account_storage_add_info(storage, info);

  success = infd_filesystem_account_storage_store_file(
    priv->filesystem,
    priv->accounts,
    error
  );

  if(success == FALSE)
  {
    infd_filesystem_account_storage_remove_info(storage, info);
    infd_filesystem_account_storage_account_info_free(info);
    return 0;
  }

  return info->id;
}

static gboolean
infd_filesystem_account_storage_remove_account(InfdAccountStorage* s,
                                               InfAclAccountId account,
                                               GError** error)
{
  InfdFilesystemAccountStorage* storage;
  InfdFilesystemAccountStoragePrivate* priv;
  InfdFilesystemAccountStorageAccountInfo* info;
  guint i;
  gboolean success;

  storage = INFD_FILESYSTEM_ACCOUNT_STORAGE(s);
  priv = INFD_FILESYSTEM_ACCOUNT_STORAGE_PRIVATE(storage);

  info = g_hash_table_lookup(
    priv->accounts,
    INF_ACL_ACCOUNT_ID_TO_POINTER(account)
  );

  if(info == NULL)
  {
    g_set_error(
      error,
      infd_filesystem_account_storage_error_quark(),
      INFD_FILESYSTEM_ACCOUNT_STORAGE_ERROR_NO_SUCH_ACCOUNT,
      _("There is no such account with ID \"%s\""),
      inf_acl_account_id_to_string(account)
    );

    return FALSE;
  }

  infd_filesystem_account_storage_remove_info(storage, info);

  success = infd_filesystem_account_storage_store_file(
    priv->filesystem,
    priv->accounts,
    error
  );

  if(success == FALSE)
  {
    infd_filesystem_account_storage_add_info(storage, info);
    return FALSE;
  }

  infd_filesystem_account_storage_account_info_free(info);
  return TRUE;
}

static InfAclAccountId
infd_filesystem_account_storage_login_by_certificate(InfdAccountStorage* s,
                                                     gnutls_x509_crt_t cert,
                                                     GError** error)
{
  InfdFilesystemAccountStorage* storage;
  InfdFilesystemAccountStoragePrivate* priv;
  InfdFilesystemAccountStorageAccountInfo* info;
  gchar* dn;
  gchar* fingerprint;
  guint i;

  storage = INFD_FILESYSTEM_ACCOUNT_STORAGE(s);
  priv = INFD_FILESYSTEM_ACCOUNT_STORAGE_PRIVATE(storage);

  dn = inf_cert_util_get_dn(cert);
  info = g_hash_table_lookup(priv->accounts_by_certificate, dn);
  if(info == NULL)
  {
    /* If we could not find any certificate with the given DN, then check the
     * key fingerprint. In an earlier version, we identified users by key and
     * not by DN, so not to break existing directories, we also need to check
     * the key fingerprint. If we have a positive match, then we also replace
     * the fingerprint by the DN of the certificate to silently migrate to DN
     * everywhere. */

    fingerprint = inf_cert_util_get_fingerprint(cert, GNUTLS_DIG_SHA256);
    info = g_hash_table_lookup(priv->accounts_by_certificate, fingerprint);

    if(info != NULL)
    {
      /* Replace the fingerprint by the DN. Note there is no collision here,
       * because otherwise we would have found the certificate by DN eralier */
      g_hash_table_remove(priv->accounts_by_certificate, fingerprint);
      g_hash_table_insert(priv->accounts_by_certificate, dn, info);

      for(i = 0; i < info->n_certificates; ++i)
      {
        if(strcmp(info->certificates[i], fingerprint) == 0)
        {
          g_free(info->certificates[i]);
          info->certificates[i] = dn;
          dn = NULL;
          break;
        }
      }

      g_assert(i < info->n_certificates);
    }

    g_free(fingerprint);
  }

  g_free(dn);

  if(info == NULL)
    return 0;

  infd_filesystem_account_storage_account_info_update_time(info);

  /* Try to save the fingerprint/DN and time change to disk, but if it does
   * not work, that's okay for now, we still keep the login functional. */
  infd_filesystem_account_storage_store_file(
    priv->filesystem,
    priv->accounts,
    NULL
  );

  return info->id;
}

static InfAclAccountId
infd_filesystem_account_storage_login_by_password(InfdAccountStorage* s,
                                                  const gchar* username,
                                                  const gchar* password,
                                                  GError** error)
{
  InfdFilesystemAccountStorage* storage;
  InfdFilesystemAccountStoragePrivate* priv;
  InfdFilesystemAccountStorageAccountInfo* info;
  gchar* hash;
  gchar cmp;
  guint i;

  storage = INFD_FILESYSTEM_ACCOUNT_STORAGE(s);
  priv = INFD_FILESYSTEM_ACCOUNT_STORAGE_PRIVATE(storage);
  info = g_hash_table_lookup(priv->accounts_by_name, username);

  if(info == NULL || info->password_hash == NULL ||
     info->password_salt == NULL)
  {
    return 0;
  }

  hash = infd_filesystem_account_storage_hash_password(
    password,
    info->password_salt,
    error
  );

  if(hash == NULL)
    return 0;

  /* length-independent string compare */
  cmp = 0;
  for(i = 0; i < gnutls_hash_get_len(GNUTLS_DIG_SHA256); ++i)
    cmp |= (info->password_hash[i] ^ hash[i]);
  g_free(hash);

  if(cmp != 0)
    return 0;

  infd_filesystem_account_storage_account_info_update_time(info);

  /* Try to save the fingerprint/DN and time change to disk, but if it does
   * not work, that's okay for now, we still keep the login functional. */
  infd_filesystem_account_storage_store_file(
    priv->filesystem,
    priv->accounts,
    NULL
  );

  return info->id;
}

static gboolean
infd_filesystem_account_storage_set_certificate(InfdAccountStorage* s,
                                                InfAclAccountId account,
                                                gnutls_x509_crt_t* certs,
                                                guint n_certs,
                                                GError** error)
{
  InfdFilesystemAccountStorage* storage;
  InfdFilesystemAccountStoragePrivate* priv;
  InfdFilesystemAccountStorageAccountInfo* info;
  InfdFilesystemAccountStorageAccountInfo* cert_info;
  guint i;
  gchar* dn;

  gchar** old_certificates;
  guint old_n_certificates;
  gboolean success;

  storage = INFD_FILESYSTEM_ACCOUNT_STORAGE(s);
  priv = INFD_FILESYSTEM_ACCOUNT_STORAGE_PRIVATE(storage);

  info = g_hash_table_lookup(
    priv->accounts,
    INF_ACL_ACCOUNT_ID_TO_POINTER(account)
  );

  if(info == NULL)
  {
    g_set_error(
      error,
      infd_filesystem_account_storage_error_quark(),
      INFD_FILESYSTEM_ACCOUNT_STORAGE_ERROR_NO_SUCH_ACCOUNT,
      _("There is no such account with ID \"%s\""),
      inf_acl_account_id_to_string(account)
    );

    return FALSE;
  }

  for(i = 0; i < n_certs; ++i)
  {
    dn = inf_cert_util_get_dn(certs[i]);
    cert_info = g_hash_table_lookup(priv->accounts_by_certificate, dn);

    if(cert_info != NULL && cert_info != info)
    {
      g_set_error(
        error,
        infd_filesystem_account_storage_error_quark(),
        INFD_FILESYSTEM_ACCOUNT_STORAGE_ERROR_DUPLICATE_CERTIFICATE,
        _("The certificate with DN=%s is already in use by another account"),
        dn
      );

      g_free(dn);
      return FALSE;
    }

    g_free(dn);
  }

  old_certificates = info->certificates;
  old_n_certificates = info->n_certificates;

  if(n_certs > 0)
  {
    info->certificates = g_malloc(sizeof(gchar*) * n_certs);
    info->n_certificates = n_certs;

    for(i = 0; i < n_certs; ++i)
      info->certificates[i] = inf_cert_util_get_dn(certs[i]);
  }
  else
  {
    info->certificates = NULL;
    info->n_certificates = 0;
  }

  /* We have not updated the accounts_by_certificate table yet, but before we
   * do so, we write the accounts file -- if that files, we need to
   * rollback */

  success = infd_filesystem_account_storage_store_file(
    priv->filesystem,
    priv->accounts,
    NULL
  );

  if(success == FALSE)
  {
    for(i = 0; i < n_certs; ++i)
      g_free(info->certificates[i]);
    g_free(info->certificates);

    info->certificates = old_certificates;
    info->n_certificates = old_n_certificates;

    return FALSE;
  }

  for(i = 0; i < old_n_certificates; ++i)
  {
    g_hash_table_remove(priv->accounts_by_certificate, old_certificates[i]);
    g_free(old_certificates[i]);
  }

  g_free(old_certificates);

  for(i = 0; i < n_certs; ++i)
  {
    g_hash_table_insert(
      priv->accounts_by_certificate,
      info->certificates[i],
      info
    );
  }

  return TRUE;
}

static gboolean
infd_filesystem_account_storage_set_password(InfdAccountStorage* s,
                                             InfAclAccountId account,
                                             const gchar* password,
                                             GError** error)
{
  InfdFilesystemAccountStorage* storage;
  InfdFilesystemAccountStoragePrivate* priv;
  InfdFilesystemAccountStorageAccountInfo* info;
  gchar* password_hash;
  gchar* password_salt;
  gchar* old_hash;
  gchar* old_salt;
  gboolean success;

  info = g_hash_table_lookup(
    priv->accounts,
    INF_ACL_ACCOUNT_ID_TO_POINTER(account)
  );

  if(info == NULL)
  {
    g_set_error(
      error,
      infd_filesystem_account_storage_error_quark(),
      INFD_FILESYSTEM_ACCOUNT_STORAGE_ERROR_NO_SUCH_ACCOUNT,
      _("There is no such account with ID \"%s\""),
      inf_acl_account_id_to_string(account)
    );

    return FALSE;
  }

  if(password != NULL)
  {
    password_salt = infd_filesystem_account_storage_generate_salt(error);
    if(password_salt == NULL) return FALSE;

    password_hash = infd_filesystem_account_storage_hash_password(
      password,
      password_salt,
      error
    );

    if(password_hash == NULL)
    {
      g_free(password_salt);
      return FALSE;
    }
  }
  else
  {
    password_salt = NULL;
    password_hash = NULL;
  }

  old_hash = info->password_hash;
  old_salt = info->password_salt;
  info->password_hash = password_hash;
  info->password_salt = password_salt;

  /* Try to write the updated password to disk */

  success = infd_filesystem_account_storage_store_file(
    priv->filesystem,
    priv->accounts,
    NULL
  );

  if(success == FALSE)
  {
    /* rollback */
    info->password_hash = old_hash;
    info->password_salt = old_salt;
    g_free(password_hash);
    g_free(password_salt);
    return FALSE;
  }

  g_free(old_hash);
  g_free(old_salt);
  return TRUE;
}

static void
infd_filesystem_account_storage_class_init(
  InfdFilesystemAccountStorageClass* filesystem_account_storage_class)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(filesystem_account_storage_class);

  object_class->dispose = infd_filesystem_account_storage_dispose;
  object_class->finalize = infd_filesystem_account_storage_finalize;
  object_class->set_property = infd_filesystem_account_storage_set_property;
  object_class->get_property = infd_filesystem_account_storage_get_property;

  g_object_class_install_property(
    object_class,
    PROP_FILESYSTEM_STORAGE,
    g_param_spec_object(
      "filesystem-storage",
      "Filesystem Storage",
      "The filesystem storage which to use the store the accounts file",
      INFD_TYPE_FILESYSTEM_STORAGE,
      G_PARAM_READWRITE
    )
  );
}

static void
infd_filesystem_account_storage_account_storage_iface_init(
  InfdAccountStorageInterface* iface)
{
  iface->get_support = infd_filesystem_account_storage_get_support;
  iface->lookup_accounts = infd_filesystem_account_storage_lookup_accounts;
  iface->lookup_accounts_by_name =
    infd_filesystem_account_storage_lookup_accounts_by_name;
  iface->list_accounts = infd_filesystem_account_storage_list_accounts;
  iface->add_account = infd_filesystem_account_storage_add_account;
  iface->remove_account = infd_filesystem_account_storage_remove_account;
  iface->login_by_certificate =
    infd_filesystem_account_storage_login_by_certificate;
  iface->login_by_password =
    infd_filesystem_account_storage_login_by_password;
  iface->set_certificate = infd_filesystem_account_storage_set_certificate;
  iface->set_password = infd_filesystem_account_storage_set_password;

  iface->account_added = NULL;
  iface->account_removed = NULL;
}

/**
 * infd_filesystem_account_storage_new: (constructor)
 *
 * Creates a new #InfdFilesystemAccountStorage that stores its account list as
 * a file in the filesystem. Use
 * infd_filesystem_account_storage_set_filesystem() to set the underlying
 * #InfdFilesystemStorage object.
 *
 * Returns: (transfer full): A new #InfdFilesystemAccountStorage.
 **/
InfdFilesystemAccountStorage*
infd_filesystem_account_storage_new(void)
{
  GObject* object;

  object = g_object_new(
    INFD_TYPE_FILESYSTEM_ACCOUNT_STORAGE,
    NULL
  );

  return INFD_FILESYSTEM_ACCOUNT_STORAGE(object);
}

/**
 * infd_filesystem_account_storage_set_filesystem:
 * @s: A #InfdFilesystemAccountStorage.
 * @fs: The underlying #InfdFilesystemStorage to use.
 * @error: Location for error information, if any, or %NULL.
 *
 * Uses @fs as the underlying #InfdFilesystemStorage for @s. The
 * #InfdFilesystemStorage:root-directory property specifies where the account
 * list is stored.
 *
 * If an error occurs while loading the account list, the function returns
 * %FALSE and @error is set.
 *
 * Returns: %TRUE on success or %FALSE on error.
 */
gboolean
infd_filesystem_account_storage_set_filesystem(InfdFilesystemAccountStorage* s,
                                               InfdFilesystemStorage* fs,
                                               GError** error)
{
  return infd_filesystem_account_storage_set_filesystem_impl(s, fs, error);
}

/* vim:set et sw=2 ts=2: */
