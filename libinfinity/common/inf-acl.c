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
 * SECTION:inf-acl
 * @title: Access Control Lists
 * @short_description: Common data structures for the infinote ACL Permissions
 * @include: libinfinity/common/inf-acl.h
 * @see_also: #InfBrowser
 * @stability: Unstable
 *
 * The basic ACL data structure is #InfAclSheet, which represents settings
 * for one user and one node in the infinote directory. One setting can have
 * three states: it can be enabled, it can be disabled or it can be set to
 * default which means to take the setting from the the default account or
 * the parent directory. The default user for the root node must not have any
 * setting set to default.
 *
 * The various settings are represented by the #InfAclSetting enumeration.
 * This specifies different operations for which access can be granted or
 * denied to different users. #InfAclMask represents a bitfield of all
 * A #InfAclSheetSet represents a set of #InfAclSheet<!-- -->s, for different
 * users. There is one #InfAclSheetSet for each node of a infinote directory.
 *
 * Usually, for application programming, the functions in this class need not
 * be used. All relevant functionality is exposed by the #InfBrowser
 * interface.
 */

#include <libinfinity/common/inf-acl.h>
#include <libinfinity/common/inf-xml-util.h>
#include <libinfinity/common/inf-error.h>
#include <libinfinity/inf-define-enum.h>
#include <libinfinity/inf-i18n.h>

#include <string.h>

#define MAKE_MASK(x) ((guint64)1 << (guint64)((x) & ((1 << 6) - 1)))

#define MAKE_CHECKED_MASK(n, x) \
  ((x >= (n) * (1 << 6) && x < (n + 1) * (1 << 6)) ? MAKE_MASK(x) : 0)

#define MAKE_DEFAULT_MASK(n) \
	MAKE_CHECKED_MASK(n, INF_ACL_CAN_ADD_SUBDIRECTORY) | \
	MAKE_CHECKED_MASK(n, INF_ACL_CAN_ADD_DOCUMENT) | \
	MAKE_CHECKED_MASK(n, INF_ACL_CAN_SYNC_IN) | \
	MAKE_CHECKED_MASK(n, INF_ACL_CAN_REMOVE_NODE) | \
	MAKE_CHECKED_MASK(n, INF_ACL_CAN_EXPLORE_NODE) | \
	MAKE_CHECKED_MASK(n, INF_ACL_CAN_SUBSCRIBE_CHAT) | \
	MAKE_CHECKED_MASK(n, INF_ACL_CAN_SUBSCRIBE_SESSION) | \
	MAKE_CHECKED_MASK(n, INF_ACL_CAN_JOIN_USER)

#define MAKE_ROOT_MASK(n) \
	MAKE_CHECKED_MASK(n, INF_ACL_CAN_SUBSCRIBE_CHAT) | \
	MAKE_CHECKED_MASK(n, INF_ACL_CAN_QUERY_ACCOUNT_LIST) | \
	MAKE_CHECKED_MASK(n, INF_ACL_CAN_CREATE_ACCOUNT) | \
	MAKE_CHECKED_MASK(n, INF_ACL_CAN_OVERRIDE_ACCOUNT) | \
	MAKE_CHECKED_MASK(n, INF_ACL_CAN_REMOVE_ACCOUNT)

#define MAKE_SUBDIRECTORY_MASK(n) \
	MAKE_CHECKED_MASK(n, INF_ACL_CAN_ADD_SUBDIRECTORY) | \
	MAKE_CHECKED_MASK(n, INF_ACL_CAN_ADD_DOCUMENT) | \
	MAKE_CHECKED_MASK(n, INF_ACL_CAN_SYNC_IN) | \
	MAKE_CHECKED_MASK(n, INF_ACL_CAN_REMOVE_NODE) | \
	MAKE_CHECKED_MASK(n, INF_ACL_CAN_EXPLORE_NODE)

#define MAKE_LEAF_MASK(n) \
	MAKE_CHECKED_MASK(n, INF_ACL_CAN_SUBSCRIBE_SESSION) | \
	MAKE_CHECKED_MASK(n, INF_ACL_CAN_JOIN_USER)

#define MAKE_FULL_MASK(m) \
	{ m(0), m(1), m(2), m(3) }

const InfAclMask INF_ACL_MASK_ALL = {
  INF_ACL_LAST >= 0x0040 ? 0xffffffffffffffff : MAKE_MASK(INF_ACL_LAST) - 1,
  INF_ACL_LAST >= 0x0080 ? 0xffffffffffffffff : MAKE_MASK(INF_ACL_LAST) - 1,
  INF_ACL_LAST >= 0x00c0 ? 0xffffffffffffffff : MAKE_MASK(INF_ACL_LAST) - 1,
  INF_ACL_LAST >= 0x0100 ? 0xffffffffffffffff : MAKE_MASK(INF_ACL_LAST) - 1,
};

const InfAclMask INF_ACL_MASK_DEFAULT = {
  MAKE_FULL_MASK(MAKE_DEFAULT_MASK)
};

const InfAclMask INF_ACL_MASK_ROOT = {
  MAKE_FULL_MASK(MAKE_ROOT_MASK)
};

const InfAclMask INF_ACL_MASK_SUBDIRECTORY = {
  MAKE_FULL_MASK(MAKE_SUBDIRECTORY_MASK)
};

const InfAclMask INF_ACL_MASK_LEAF = {
  MAKE_FULL_MASK(MAKE_LEAF_MASK)
};

static const GEnumValue inf_acl_setting_values[] = {
  {
    INF_ACL_CAN_ADD_SUBDIRECTORY,
    "INF_ACL_CAN_ADD_SUBDIRECTORY",
    "can-add-subdirectory"
  }, {
    INF_ACL_CAN_ADD_DOCUMENT,
    "INF_ACL_CAN_ADD_DOCUMENT",
    "can-add-document"
  }, {
    INF_ACL_CAN_SYNC_IN,
    "INF_ACL_CAN_SYNC_IN",
    "can-sync-in"
  }, {
    INF_ACL_CAN_REMOVE_NODE,
    "INF_ACL_CAN_REMOVE_NODE",
    "can-remove-node"
  }, {
    INF_ACL_CAN_EXPLORE_NODE,
    "INF_ACL_CAN_EXPLORE_NODE",
    "can-explore-node"
  }, {
    INF_ACL_CAN_SUBSCRIBE_CHAT,
    "INF_ACL_CAN_SUBSCRIBE_CHAT",
    "can-subscribe-chat"
  }, {
    INF_ACL_CAN_SUBSCRIBE_SESSION,
    "INF_ACL_CAN_SUBSCRIBE_SESSION",
    "can-subscribe-session"
  }, {
    INF_ACL_CAN_JOIN_USER,
    "INF_ACL_CAN_JOIN_USER",
    "can-join-user"
  }, {
    INF_ACL_CAN_QUERY_ACCOUNT_LIST,
    "INF_ACL_CAN_QUERY_ACCOUNT_LIST",
    "can-query-account-list"
  }, {
    INF_ACL_CAN_CREATE_ACCOUNT,
    "INF_ACL_CAN_CREATE_ACCOUNT",
    "can-create-account"
  }, {
    INF_ACL_CAN_OVERRIDE_ACCOUNT,
    "INF_ACL_CAN_OVERRIDE_ACCOUNT",
    "can-override-account"
  }, {
    INF_ACL_CAN_REMOVE_ACCOUNT,
    "INF_ACL_CAN_REMOVE_ACCOUNT",
    "can-remove-account"
  }, {
    INF_ACL_CAN_QUERY_ACL,
    "INF_ACL_CAN_QUERY_ACL",
    "can-query-acl"
  }, {
    INF_ACL_CAN_SET_ACL,
    "INF_ACL_CAN_SET_ACL",
    "can-set-acl"
  }, {
    0,
    NULL,
    NULL
  }
};

G_DEFINE_BOXED_TYPE(InfAclAccount, inf_acl_account, inf_acl_account_copy, inf_acl_account_free)
INF_DEFINE_ENUM_TYPE(InfAclSetting, inf_acl_setting, inf_acl_setting_values)
G_DEFINE_BOXED_TYPE(InfAclMask, inf_acl_mask, inf_acl_mask_copy, inf_acl_mask_free)
G_DEFINE_BOXED_TYPE(InfAclSheet, inf_acl_sheet, inf_acl_sheet_copy, inf_acl_sheet_free)
G_DEFINE_BOXED_TYPE(InfAclSheetSet, inf_acl_sheet_set, inf_acl_sheet_set_copy, inf_acl_sheet_set_free)

/**
 * inf_acl_account_id_to_string:
 * @account: A #InfAclAccountId.
 *
 * Translates the given account ID to a unique string identifier.
 *
 * Returns: A string representation of the given account ID, or %NULL if
 * the account does not exist. The return value must not be freed.
 */
const gchar*
inf_acl_account_id_to_string(InfAclAccountId account)
{
  return g_quark_to_string(account);
}

/**
 * inf_acl_account_id_from_string:
 * @id: A string representation of an account ID.
 *
 * Converts the given string into a unique account identifier which can be
 * used with the rest of the ACL API.
 *
 * Returns: (transfer full): The account ID which is equivalent to the given
 * string.
 */
InfAclAccountId
inf_acl_account_id_from_string(const gchar* id)
{
  return g_quark_from_string(id);
}

/**
 * inf_acl_account_new:
 * @id: The unique ID of the new account.
 * @name: (allow-none): The human-readable name of the new account.
 *
 * Creates a new #InfAclAccount.
 *
 * Returns: (transfer full): A new #InfAclAccount object.
 */
InfAclAccount*
inf_acl_account_new(const InfAclAccountId id,
                    const gchar* name)
{
  InfAclAccount* account;

  g_return_val_if_fail(id != 0, NULL);

  account = g_slice_new(InfAclAccount);
  account->id = id;
  account->name = g_strdup(name);
  return account;
}

/**
 * inf_acl_account_copy:
 * @account: The #InfAclAccount to copy.
 *
 * Creates a copy of @account.
 *
 * Returns: (transfer full): A new #InfAclAccount. Free with
 * inf_acl_account_free() when no longer needed.
 */
InfAclAccount*
inf_acl_account_copy(const InfAclAccount* account)
{
  InfAclAccount* new_account;

  g_return_val_if_fail(account != NULL, NULL);

  new_account = g_slice_new(InfAclAccount);
  new_account->id = account->id;
  new_account->name = g_strdup(account->name);
  return new_account;
}

/**
 * inf_acl_account_free:
 * @account: A #InfAclAccount.
 *
 * Releases all resources allocated by @account.
 */
void
inf_acl_account_free(InfAclAccount* account)
{
  g_free(account->name);
  g_slice_free(InfAclAccount, account);
}

/**
 * inf_acl_account_array_free:
 * @accounts: (array length=n_accounts): An array of #InfAclAccount objects.
 * @n_accounts: The number of elements in the array.
 *
 * Releases all resources allocated by an array of #InfAclAccount<!-- -->s.
 */
void inf_acl_account_array_free(InfAclAccount* accounts,
                                guint n_accounts)
{
  guint i;

  for(i = 0; i < n_accounts; ++i)
    g_free(accounts[i].name);
  g_free(accounts);
}

/**
 * inf_acl_account_from_xml:
 * @xml: An XML node.
 * @error: Location to store error information, if any.
 *
 * Attempts to deserialize an #InfAclAccount that was written to an XML node
 * by the function inf_acl_account_to_xml(). If an error occurs, such as
 * mandatory fields being missing, the function returns %NULL and @error is
 * set.
 *
 * Returns: (transfer full): A new #InfAclAccount on success, or %NULL on
 * failure. Free with inf_acl_account_free() when no longer needed.
 */
InfAclAccount*
inf_acl_account_from_xml(xmlNodePtr xml,
                         GError** error)
{
  xmlChar* account_id;
  xmlChar* account_name;
  InfAclAccount* new_account;

  account_id = inf_xml_util_get_attribute_required(xml, "id", error);
  if(account_id == NULL) return NULL;

  account_name = inf_xml_util_get_attribute(xml, "name");

  new_account = inf_acl_account_new(
    inf_acl_account_id_from_string((const gchar*)account_id),
    (const gchar*)account_name
  );

  xmlFree(account_id);
  if(account_name != NULL) xmlFree(account_name);

  return new_account;
}

/**
 * inf_acl_account_to_xml:
 * @account: A #InfAclAccount.
 * @xml: The XML node to write the account attributes to.
 *
 * Serializes the properties of @account into the XML node @xml. The function
 * inf_acl_account_from_xml() does the reverse operation and deserializes the
 * account object from the XML.
 */
void
inf_acl_account_to_xml(const InfAclAccount* account,
                       xmlNodePtr xml)
{
  inf_xml_util_set_attribute(
    xml,
    "id",
    inf_acl_account_id_to_string(account->id)
  );

  if(account->name != NULL)
    inf_xml_util_set_attribute(xml, "name", account->name);
}

/**
 * inf_acl_mask_copy:
 * @mask: The #InfAclMask to copy.
 *
 * Creates a dynamically allocated copy of @mask. This function should not
 * usually be used since masks can simply created on the stack and copied
 * by value. This function is mainly meant for the boxed type definition and
 * for language bindings.
 *
 * Returns: (transfer full): A new #InfAclMask. Free with inf_acl_mask_free()
 * when no longer needed.
 */
InfAclMask*
inf_acl_mask_copy(const InfAclMask* mask)
{
  InfAclMask* new_mask;

  g_return_val_if_fail(mask != NULL, NULL);

  new_mask = g_slice_new(InfAclMask);
  new_mask->mask[0] = mask->mask[0];
  new_mask->mask[1] = mask->mask[1];
  new_mask->mask[2] = mask->mask[2];
  new_mask->mask[3] = mask->mask[3];
  return new_mask;
}

/**
 * inf_acl_mask_free:
 * @mask: The #InfAclMask to free.
 *
 * Releases a #InfAclMask that was created with inf_acl_mask_copy().
 */
void
inf_acl_mask_free(InfAclMask* mask)
{
  g_return_if_fail(mask != NULL);
  g_slice_free(InfAclMask, mask);
}

/**
 * inf_acl_mask_clear:
 * @mask: The #InfAclMask to reset.
 *
 * Resets a mask so that all fields are zero.
 */
void
inf_acl_mask_clear(InfAclMask* mask)
{
  g_return_if_fail(mask != NULL);
  mask->mask[0] = 0;
  mask->mask[1] = 0;
  mask->mask[2] = 0;
  mask->mask[3] = 0;
}

/**
 * inf_acl_mask_empty:
 * @mask: A #InfAclMask.
 *
 * Checks whether the given mask is empty, i.e. all fields are set to zero.
 *
 * Returns: %TRUE if @mask has all fields zero, or %FALSE otherwies.
 */
gboolean
inf_acl_mask_empty(const InfAclMask* mask)
{
  g_return_val_if_fail(mask != NULL, FALSE);

  return (mask->mask[0] & INF_ACL_MASK_ALL.mask[0]) == 0 &&
         (mask->mask[1] & INF_ACL_MASK_ALL.mask[1]) == 0 &&
         (mask->mask[2] & INF_ACL_MASK_ALL.mask[2]) == 0 &&
         (mask->mask[3] & INF_ACL_MASK_ALL.mask[3]) == 0;
}

/**
 * inf_acl_mask_equal:
 * @lhs: The first mask.
 * @rhs: The second mask.
 *
 * Checks whether the two masks are equal.
 *
 * Returns: %TRUE if @lhs and @rhs are equal or %FALSE otherwise.
 */
gboolean
inf_acl_mask_equal(const InfAclMask* lhs,
                   const InfAclMask* rhs)
{
  g_return_val_if_fail(lhs != NULL, FALSE);
  g_return_val_if_fail(rhs != NULL, FALSE);

  return ((lhs->mask[0] ^ rhs->mask[0]) & INF_ACL_MASK_ALL.mask[0]) == 0 &&
         ((lhs->mask[1] ^ rhs->mask[1]) & INF_ACL_MASK_ALL.mask[1]) == 0 &&
         ((lhs->mask[2] ^ rhs->mask[2]) & INF_ACL_MASK_ALL.mask[2]) == 0 &&
         ((lhs->mask[3] ^ rhs->mask[3]) & INF_ACL_MASK_ALL.mask[3]) == 0;
}

/**
 * inf_acl_mask_set1:
 * @mask: The #InfAclMask to initialize.
 * @setting: The permission to set.
 *
 * Initializes @mask such that all permissions are off except the one
 * corresponding to @setting.
 *
 * Returns: (transfer none): The mask itself.
 */
InfAclMask*
inf_acl_mask_set1(InfAclMask* mask,
                  InfAclSetting setting)
{
  g_return_val_if_fail(mask != NULL, NULL);
  g_return_val_if_fail(setting < 0x100, NULL);

  mask->mask[0] = 0;
  mask->mask[1] = 0;
  mask->mask[2] = 0;
  mask->mask[3] = 0;

  mask->mask[setting >> 6] = MAKE_MASK(setting);
  return mask;
}

/**
 * inf_acl_mask_setv:
 * @mask: The #InfAclMask to initialize.
 * @settings: (array length=n_settings): An array of
 * #InfAclSetting<!-- -->s to set.
 * @n_settings: The number of settings.
 *
 * Initializes @mask such that all permissions are off except the ones
 * specified in the @settings array.
 *
 * Returns: (transfer none): The mask itself.
 */
InfAclMask*
inf_acl_mask_setv(InfAclMask* mask,
                  const InfAclSetting* settings,
                  guint n_settings)
{
  guint i;

  g_return_val_if_fail(mask != NULL, NULL);
  g_return_val_if_fail(settings != NULL || n_settings == 0, NULL);

  mask->mask[0] = 0;
  mask->mask[1] = 0;
  mask->mask[2] = 0;
  mask->mask[3] = 0;

  for(i = 0; i < n_settings; ++i)
  {
    g_return_val_if_fail(settings[i] < 0x100, NULL);
    mask->mask[settings[i] >> 6] |= MAKE_MASK(settings[i]);
  }

  return mask;
}

/**
 * inf_acl_mask_and:
 * @lhs: First mask.
 * @rhs: Second mask.
 * @out: (out): Output mask.
 *
 * Computes the bitwise AND of @lhs and @rhs and writes the result to @out.
 * @out is allowed to be equivalent to @lhs and/or @rhs.
 *
 * Returns: (transfer none): The output mask.
 */
InfAclMask*
inf_acl_mask_and(const InfAclMask* lhs,
                 const InfAclMask* rhs,
                 InfAclMask* out)
{
  g_return_val_if_fail(lhs != NULL, NULL);
  g_return_val_if_fail(rhs != NULL, NULL);
  g_return_val_if_fail(out != NULL, NULL);

  out->mask[0] = rhs->mask[0] & lhs->mask[0];
  out->mask[1] = rhs->mask[1] & lhs->mask[1];
  out->mask[2] = rhs->mask[2] & lhs->mask[2];
  out->mask[3] = rhs->mask[3] & lhs->mask[3];

  return out;
}

/**
 * inf_acl_mask_and1:
 * @mask: A #InfAclMask.
 * @setting: The setting to unset.
 *
 * Disables the bit corresponding to setting in @mask, leaving all other bits
 * alone.
 *
 * Returns: (transfer none): The mask itself.
 */
InfAclMask*
inf_acl_mask_and1(InfAclMask* mask,
                  InfAclSetting setting)
{
  g_return_val_if_fail(mask != NULL, NULL);
  g_return_val_if_fail(setting < 0x100, NULL);

  mask->mask[setting >> 6] &= ~MAKE_MASK(setting);

  return mask;
}

/**
 * inf_acl_mask_or:
 * @lhs: First mask.
 * @rhs: Second mask.
 * @out: (out): Output mask.
 *
 * Computes the bitwise OR of @lhs and @rhs and writes the result to @out.
 * @out is allowed to be equivalent to @lhs and/or @rhs.
 *
 * Returns: (transfer none): The output mask.
 */
InfAclMask*
inf_acl_mask_or(const InfAclMask* lhs,
                const InfAclMask* rhs,
                InfAclMask* out)
{
  g_return_val_if_fail(lhs != NULL, NULL);
  g_return_val_if_fail(rhs != NULL, NULL);
  g_return_val_if_fail(out != NULL, NULL);

  out->mask[0] = rhs->mask[0] | lhs->mask[0];
  out->mask[1] = rhs->mask[1] | lhs->mask[1];
  out->mask[2] = rhs->mask[2] | lhs->mask[2];
  out->mask[3] = rhs->mask[3] | lhs->mask[3];

  return out;
}

/**
 * inf_acl_mask_or1:
 * @mask: A #InfAclMask.
 * @setting: The setting to add.
 *
 * Enables the bit corresponding to setting in @mask, leaving all other bits
 * alone.
 *
 * Returns: (transfer none): The mask itself.
 */
InfAclMask*
inf_acl_mask_or1(InfAclMask* mask,
                 InfAclSetting setting)
{
  g_return_val_if_fail(mask != NULL, NULL);
  g_return_val_if_fail(setting < 0x100, NULL);

  mask->mask[setting >> 6] |= MAKE_MASK(setting);

  return mask;
}

/**
 * inf_acl_mask_neg:
 * @mask: The mask to negate.
 * @out: (out): The output mask.
 *
 * Negates the given mask bitwise and writes the result to @out. The output
 * mask is allowed to be equivalent to @mask itself.
 *
 * Returns: (transfer full): The output mask.
 */
InfAclMask*
inf_acl_mask_neg(const InfAclMask* mask,
                 InfAclMask* out)
{
  g_return_val_if_fail(mask != NULL, NULL);
  g_return_val_if_fail(out != NULL, NULL);

  out->mask[0] = ~mask->mask[0];
  out->mask[1] = ~mask->mask[1];
  out->mask[2] = ~mask->mask[2];
  out->mask[3] = ~mask->mask[3];

  return out;
}

/**
 * inf_acl_mask_has:
 * @mask: A #InfAclMask.
 * @setting: The setting to check.
 *
 * Returns %TRUE if the given mask has the bit which corresponds to @setting
 * set, or %FALSE otherwise.
 *
 * Returns: %TRUE if the bit corresponding to @setting is set in @mask.
 */
gboolean
inf_acl_mask_has(const InfAclMask* mask,
                 InfAclSetting setting)
{
  g_return_val_if_fail(mask != NULL, FALSE);
  g_return_val_if_fail(setting < 0x100, FALSE);

  return (mask->mask[setting >> 6] & MAKE_MASK(setting)) != 0;
}

/**
 * inf_acl_sheet_new:
 * @account: The #InfAclAccountId representing a unique account id.
 *
 * Creates a dynamically allocated #InfAclSheet. This is usually not
 * needed because you can copy the structs by value, but it is useful
 * for properties and bindings. The new sheet will hold permissions for the
 * given account. The permissions will initially be all masked out.
 *
 * Returns: (transfer full): A new #InfAclSheet. Free with
 * inf_acl_sheet_free() when no longer in use.
 */
InfAclSheet*
inf_acl_sheet_new(InfAclAccountId account)
{
  InfAclSheet* sheet;
  sheet = g_slice_new(InfAclSheet);
  sheet->account = account;
  inf_acl_mask_clear(&sheet->mask);
  inf_acl_mask_clear(&sheet->perms); /* not strictly required */
  return sheet;
}

/**
 * inf_acl_sheet_copy:
 * @sheet: A #InfAclSheet.
 *
 * Makes a dynamically allocated copy of @sheet. This should not be used by
 * applications because you can copy the structs by value, but it is useful
 * for properties and bindings. 
 *
 * Returns: (transfer full): A newly-allocated copy of @sheet. Free with
 * inf_acl_sheet_free() when no longer in use.
 */
InfAclSheet*
inf_acl_sheet_copy(const InfAclSheet* sheet)
{
  InfAclSheet* new_sheet;
  new_sheet = g_slice_new(InfAclSheet);
  new_sheet->account = sheet->account;
  new_sheet->mask = sheet->mask;
  new_sheet->perms = sheet->perms;
  return new_sheet;
}

/**
 * inf_acl_sheet_free:
 * @sheet: A #InfAclSheet.
 *
 * Frees a #InfAclSheet allocated by inf_acl_sheet_copy().
 **/
void
inf_acl_sheet_free(InfAclSheet* sheet)
{
  g_slice_free(InfAclSheet, sheet);
}

/**
 * inf_acl_sheet_perms_from_xml:
 * @xml: The XML node to read from.
 * @mask: (out): Output parameter to write the permission mask to.
 * @perms: (out): Output parameter to write the permissions to.
 * @error: Location to store error information, if any.
 *
 * This function extracts the permission mask and the permission flags from
 * the XML node @xml. The counterpart to this function is
 * inf_acl_sheet_perms_to_xml(). If an error occurs the function returns
 * %FALSE and @error is set.
 *
 * Returns: %TRUE if the operation was successful, or %FALSE on error.
 */
gboolean
inf_acl_sheet_perms_from_xml(xmlNodePtr xml,
                             InfAclMask* mask,
                             InfAclMask* perms,
                             GError** error)
{
  GEnumClass* enum_class;
  guint i;
  xmlChar* attr;
  InfAclSetting value;

  inf_acl_mask_clear(mask);
  /* not strictly required because the relevant bits are initialized
   * below, but helps in debugging: */
  inf_acl_mask_clear(perms);

  enum_class = G_ENUM_CLASS(g_type_class_ref(INF_TYPE_ACL_SETTING));
  for(i = 0; i < enum_class->n_values; ++i)
  {
    attr = inf_xml_util_get_attribute(xml, enum_class->values[i].value_nick);
    if(attr != NULL)
    {
      value = enum_class->values[i].value;
      mask->mask[value >> 6] |= MAKE_MASK(value);

      if(strcmp((const xmlChar*)attr, "yes") == 0)
      {
        perms->mask[value >> 6] |= MAKE_MASK(value);
      }
      else if(strcmp((const xmlChar*)attr, "no") == 0)
      {
        perms->mask[value >> 6] &= ~MAKE_MASK(value);
      }
      else
      {
        g_set_error(
          error,
          inf_request_error_quark(),
          INF_REQUEST_ERROR_INVALID_ATTRIBUTE,
          "%s",
          _("ACL field must be either \"yes\" or \"no\"")
        );
      }

      xmlFree(attr);
    }
  }

  g_type_class_unref(enum_class);
  return TRUE;
}

/**
 * inf_acl_sheet_perms_to_xml:
 * @mask: Permission mask to write.
 * @perms: Individiual permissions to write.
 * @xml: error: Location to store error information, if any.
 *
 * This function writes the given permission mask and permission flags to the
 * XML node @xml. They can be converted back using the
 * inf_acl_sheet_perms_from_xml() function.
 */
void
inf_acl_sheet_perms_to_xml(const InfAclMask* mask,
                           const InfAclMask* perms,
                           xmlNodePtr xml)
{
  GEnumClass* enum_class;
  guint i;
  guint64 value;
  const gchar* value_nick;

  enum_class = G_ENUM_CLASS(g_type_class_ref(INF_TYPE_ACL_SETTING));

  for(i = 0; i < enum_class->n_values; ++i)
  {
    value = enum_class->values[i].value;
    if((mask->mask[value >> 6] & MAKE_MASK(value)) != 0)
    {
      value_nick = enum_class->values[i].value_nick;
      if((perms->mask[value >> 6] & MAKE_MASK(value)) != 0)
        inf_xml_util_set_attribute(xml, value_nick, "yes");
      else
        inf_xml_util_set_attribute(xml, value_nick, "no");
    }
  }

  g_type_class_unref(enum_class);
}

/**
 * inf_acl_sheet_set_new:
 *
 * Creates a new #InfAclSheetSet. Add sheets with
 * inf_acl_sheet_set_add_sheet().
 *
 * Returns: (transfer full): A new #InfAclSheetSet. Free with
 * inf_acl_sheet_set_free().
 */
InfAclSheetSet*
inf_acl_sheet_set_new(void)
{
  InfAclSheetSet* sheet_set;
  sheet_set = g_slice_new(InfAclSheetSet);
  sheet_set->own_sheets = NULL;
  sheet_set->sheets = NULL;
  sheet_set->n_sheets = 0;
  return sheet_set;
}

/**
 * inf_acl_sheet_set_new_external:
 * @sheets: (array length=n_sheets): An array of #InfAclSheet<!-- -->s
 * @n_sheets: Number of elements in @sheets.
 *
 * Creates a new #InfAclSheetSet refererencing the given ACL sheets. The
 * created sheet set is only holding a reference to the given array, so it
 * must stay alive as long as the sheet set is alive.
 *
 * No new sheets can be added to the returned sheet set with
 * inf_acl_sheet_set_add_sheet(), or removed with
 * inf_acl_sheet_set_remove_sheet().
 *
 * Returns: (transfer full): A new #InfAclSheetSet. Free with
 * inf_acl_sheet_set_free() when no longer needed.
 */
InfAclSheetSet*
inf_acl_sheet_set_new_external(const InfAclSheet* sheets,
                               guint n_sheets)
{
  InfAclSheetSet* sheet_set;

  g_return_val_if_fail(sheets != NULL || n_sheets == 0, NULL);

  sheet_set = g_slice_new(InfAclSheetSet);
  sheet_set->own_sheets = NULL;
  sheet_set->sheets = sheets;
  sheet_set->n_sheets = n_sheets;
  return sheet_set;
}

/**
 * inf_acl_sheet_set_copy:
 * @sheet_set: A #InfAclSheetSet.
 *
 * Creates a copy of @sheet_set. If @sheet_set was created with
 * inf_acl_sheet_set_new_external(), the copied sheet set will also only hold
 * a reference to the external sheets, and the same restrictions apply.
 *
 * Returns: (transfer full): A new #InfAclSheetSet. Free with
 * inf_acl_sheet_set_free() when no longer needed.
 */
InfAclSheetSet*
inf_acl_sheet_set_copy(const InfAclSheetSet* sheet_set)
{
  InfAclSheetSet* new_sheet_set;

  g_return_val_if_fail(sheet_set != NULL, NULL);

  new_sheet_set = g_slice_new(InfAclSheetSet);

  if(sheet_set->own_sheets != NULL)
  {
    new_sheet_set->own_sheets =
      g_malloc(sheet_set->n_sheets * sizeof(InfAclSheet));
    new_sheet_set->sheets = new_sheet_set->own_sheets;
    new_sheet_set->n_sheets = sheet_set->n_sheets;

    memcpy(
      new_sheet_set->own_sheets,
      sheet_set->own_sheets,
      sheet_set->n_sheets * sizeof(InfAclSheet)
    );
  }
  else
  {
    new_sheet_set->own_sheets = NULL;
    new_sheet_set->sheets = sheet_set->sheets;
    new_sheet_set->n_sheets = sheet_set->n_sheets;
  }

  return new_sheet_set;
}

/**
 * inf_acl_sheet_set_sink:
 * @sheet_set: A #InfAclSheetSet.
 *
 * If a sheet set was created with inf_acl_sheet_set_new_external(), this
 * function lifts the restrictions that come with it by making an internal
 * copy of the ACL sheets.
 */
void
inf_acl_sheet_set_sink(InfAclSheetSet* sheet_set)
{
  g_return_if_fail(sheet_set != NULL);

  if(sheet_set->own_sheets == NULL && sheet_set->n_sheets > 0)
  {
    sheet_set->own_sheets =
      g_malloc(sheet_set->n_sheets * sizeof(InfAclSheet));

    memcpy(
      sheet_set->own_sheets,
      sheet_set->sheets,
      sheet_set->n_sheets * sizeof(InfAclSheet)
    );

    sheet_set->sheets = sheet_set->own_sheets;
  }
}

/**
 * inf_acl_sheet_set_free:
 * @sheet_set: A #InfAclSheetSet.
 *
 * Releases all resources allocated for @sheet_set.
 */
void
inf_acl_sheet_set_free(InfAclSheetSet* sheet_set)
{
  g_return_if_fail(sheet_set != NULL);
  g_free(sheet_set->own_sheets);
  g_slice_free(InfAclSheetSet, sheet_set);
}

/**
 * inf_acl_sheet_set_add_sheet:
 * @sheet_set: A #InfAclSheetSet.
 * @account: The #InfAclAccountId representing a unique account ID.
 *
 * Adds a new default sheet for @account to @sheet_set. The function returns
 * a pointer to the new sheet. The pointer stays valid as long as no other
 * sheet is added to the set. If there is already a sheet for @account in the
 * set, then the existing sheet is returned instead.
 *
 * This function can only be used if the sheet set has not been created with
 * the inf_acl_sheet_set_new_external() function.
 *
 * Returns: (transfer none): A #InfAclSheet for the new account.
 */
InfAclSheet*
inf_acl_sheet_set_add_sheet(InfAclSheetSet* sheet_set,
                            InfAclAccountId account)
{
  guint i;

  g_return_val_if_fail(sheet_set != NULL, NULL);
  g_return_val_if_fail(account != 0, NULL);

  g_return_val_if_fail(
    sheet_set->own_sheets != NULL || sheet_set->n_sheets == 0,
    NULL
  );

  for(i = 0; i < sheet_set->n_sheets; ++i)
    if(sheet_set->own_sheets[i].account == account)
      return &sheet_set->own_sheets[i];

  ++sheet_set->n_sheets;
  sheet_set->own_sheets = g_realloc(
    sheet_set->own_sheets,
    sheet_set->n_sheets * sizeof(InfAclSheet)
  );

  sheet_set->sheets = sheet_set->own_sheets;

  sheet_set->own_sheets[i].account = account;
  inf_acl_mask_clear(&sheet_set->own_sheets[i].mask);
  inf_acl_mask_clear(&sheet_set->own_sheets[i].perms); /* not strictly required */
  return &sheet_set->own_sheets[i];
}

/**
 * inf_acl_sheet_set_remove_sheet:
 * @sheet_set: A #InfAclSheetSet.
 * @sheet: The sheet to remove.
 *
 * Removes a sheet from @sheet_set. @sheet must be one of the sheets inside
 * @sheet_set. The sheet is removed by replacing it with the last sheet in
 * the set, so the order of sheets is not preserved.
 *
 * This function can only be used if the sheet set has not been created with
 * the inf_acl_sheet_set_new_external() function.
 */
void
inf_acl_sheet_set_remove_sheet(InfAclSheetSet* sheet_set,
                               InfAclSheet* sheet)
{
  g_return_if_fail(sheet_set != NULL);
  g_return_if_fail(sheet_set->own_sheets != NULL);

  g_return_if_fail(sheet != NULL);
  g_return_if_fail(sheet >= sheet_set->own_sheets);
  g_return_if_fail(sheet < sheet_set->own_sheets + sheet_set->n_sheets);

  if(sheet != &sheet_set->own_sheets[sheet_set->n_sheets - 1])
    *sheet = sheet_set->own_sheets[sheet_set->n_sheets - 1];

  --sheet_set->n_sheets;

  sheet_set->own_sheets = g_realloc(
    sheet_set->own_sheets,
    sheet_set->n_sheets * sizeof(InfAclSheet)
  );

  sheet_set->sheets = sheet_set->own_sheets;
}

/**
 * inf_acl_sheet_set_merge_sheets:
 * @sheet_set: (allow-none) (transfer full): A #InfAclSheetSet, or %NULL.
 * @other: The sheet set to merge.
 *
 * Replaces all sheets that are present in @other in @sheet_set with the ones
 * from @other. Note that an empty sheet in @other (with all permissions
 * masked out) causes the corresponding sheet in @sheet_set to be removed.
 *
 * If @sheet_set is %NULL it is treated like an empty sheet set, i.e. the
 * merged sheet set is a copy of @other. In that case a new sheet set is
 * created and returned, unless @other is empty. If the merged sheet set
 * ends up empty, it is freed and the function returns %NULL.
 *
 * Returns: (allow-none) (transfer full): The merged sheet set, or %NULL
 * when the merged sheet set would be empty.
 */
InfAclSheetSet*
inf_acl_sheet_set_merge_sheets(InfAclSheetSet* sheet_set,
                               const InfAclSheetSet* other)
{
  guint i;
  InfAclSheet* sheet;

  g_return_val_if_fail(other != NULL, NULL);

  if(sheet_set == NULL)
    sheet_set = inf_acl_sheet_set_new();

  for(i = 0; i < other->n_sheets; ++i)
  {
    if(inf_acl_mask_empty(&other->sheets[i].mask))
    {
      /* Sheet is empty: remove */
      sheet = inf_acl_sheet_set_find_sheet(
        sheet_set,
        other->sheets[i].account
      );

      if(sheet != NULL)
        inf_acl_sheet_set_remove_sheet(sheet_set, sheet);
    }
    else
    {
      /* Sheet is not empty: take */
      sheet = inf_acl_sheet_set_add_sheet(
        sheet_set,
        other->sheets[i].account
      );

      sheet->mask = other->sheets[i].mask;
      sheet->perms = other->sheets[i].perms;
    }
  }

  if(sheet_set->n_sheets == 0)
  {
    inf_acl_sheet_set_free(sheet_set);
    sheet_set = NULL;
  }

  return sheet_set;
}

/**
 * inf_acl_sheet_set_get_clear_sheets:
 * @sheet_set: A #InfAclSheetSet.
 *
 * Returns a new sheet set with all sheets that are present in @sheet_set,
 * but with all permissions masked. When this set is merged with the original
 * set, all permissions will be reset to default for all accounts. Before
 * the merge, the returned sheet set can be modified. This allows to replace
 * the current permissions with new ones atomically.
 *
 * Returns: (transfer full): A new #InfAclSheetSet. Free with
 * inf_acl_sheet_set_free() when no longer needed.
 */
InfAclSheetSet*
inf_acl_sheet_set_get_clear_sheets(const InfAclSheetSet* sheet_set)
{
  InfAclSheetSet* set;
  guint i;

  g_return_val_if_fail(sheet_set != NULL, NULL);

  set = g_slice_new(InfAclSheetSet);
  set->own_sheets = g_malloc(sizeof(InfAclSheet) * sheet_set->n_sheets);

  set->n_sheets = 0;
  for(i = 0; i < sheet_set->n_sheets; ++i)
  {
    if(!inf_acl_mask_empty(&sheet_set->sheets[i].mask))
    {
      set->own_sheets[set->n_sheets].account = sheet_set->sheets[i].account;
      inf_acl_mask_clear(&set->own_sheets[set->n_sheets].mask);
      set->own_sheets[set->n_sheets].perms = sheet_set->sheets[i].perms;

      ++set->n_sheets;
    }
  }

  if(set->n_sheets < sheet_set->n_sheets)
  {
    set->own_sheets = g_realloc(
      set->own_sheets,
      sizeof(InfAclSheet) * set->n_sheets
    );
  }

  set->sheets = set->own_sheets;
  return set;
}

/**
 * inf_acl_sheet_set_find_sheet:
 * @sheet_set: A #InfAclSheetSet.
 * @account: The #InfAclAccountId representing the unique account ID of the
 * account whose ACL sheet is to be found.
 *
 * Returns the #InfAclSheet for @account. If there is no such sheet in
 * @sheet_set, the function returns %NULL.
 *
 * This function can only be used if the sheet set has not been created with
 * the inf_acl_sheet_set_new_external() function.
 *
 * Returns: (transfer none): A #InfAclSheet for @account, or %NULL.
 */
InfAclSheet*
inf_acl_sheet_set_find_sheet(InfAclSheetSet* sheet_set,
                             InfAclAccountId account)
{
  guint i;

  g_return_val_if_fail(sheet_set != NULL, NULL);
  g_return_val_if_fail(account != 0, NULL);

  g_return_val_if_fail(
    sheet_set->own_sheets != NULL || sheet_set->n_sheets == 0,
    NULL
  );

  for(i = 0; i < sheet_set->n_sheets; ++i)
    if(sheet_set->own_sheets[i].account == account)
      return &sheet_set->own_sheets[i];

  return NULL;
}

/**
 * inf_acl_sheet_set_find_const_sheet:
 * @sheet_set: A #InfAclSheetSet.
 * @account: The #InfAclAccountId representing the unique account ID of the
 * account whose ACL sheet is to be found.
 *
 * Returns the #InfAclSheet for @account. If there is no such sheet in
 * @sheet_set, the function returns %NULL.
 *
 * The difference between this function and
 * inf_acl_sheet_set_find_sheet() is that this function returns a sheet
 * that cannot be modified, but it can also be used on a sheet set created
 * with the inf_acl_sheet_set_new_external() function.
 *
 * Returns: (transfer none): A #InfAclSheet for @account, or %NULL.
 */
const InfAclSheet*
inf_acl_sheet_set_find_const_sheet(const InfAclSheetSet* sheet_set,
                                   InfAclAccountId account)
{
  guint i;

  g_return_val_if_fail(sheet_set != NULL, NULL);
  g_return_val_if_fail(account != 0, NULL);

  for(i = 0; i < sheet_set->n_sheets; ++i)
    if(sheet_set->sheets[i].account == account)
      return &sheet_set->sheets[i];

  return NULL;
}

/**
 * inf_acl_sheet_set_from_xml:
 * @xml: The XML node from which to read the sheet set.
 * @error: Location to read error information, if any.
 *
 * Reads a sheet set from @xml that has been written with
 * inf_acl_sheet_set_to_xml(). If an error occurs the function returns
 * %NULL and @error is set. If there is no ACL stored in @xml, the function
 * returns %NULL without setting @error.
 *
 * Returns: (transfer full): A #InfAclSheetSet, or %NULL. Free with
 * inf_acl_sheet_set_free() when no longer needed.
 */
InfAclSheetSet*
inf_acl_sheet_set_from_xml(xmlNodePtr xml,
                           GError** error)
{
  xmlNodePtr acl;
  xmlNodePtr sheet;
  GArray* array;
  InfAclSheet read_sheet;

  xmlChar* account_id;
  guint i;
  gboolean result;

  InfAclSheetSet* sheet_set;

  g_return_val_if_fail(xml != NULL, NULL);
  g_return_val_if_fail(error == NULL || *error == NULL, NULL);

  for(acl = xml->children; acl != NULL; acl = acl->next)
  {
    if(acl->type != XML_ELEMENT_NODE) continue;
    if(strcmp((const char*)acl->name, "acl") != 0) continue;

    array = g_array_sized_new(FALSE, FALSE, sizeof(InfAclSheet), 16);
    for(sheet = acl->children; sheet != NULL; sheet = sheet->next)
    {
      if(sheet->type != XML_ELEMENT_NODE) continue;
      if(strcmp((const char*)sheet->name, "sheet") != 0) continue;

      account_id = inf_xml_util_get_attribute_required(sheet, "id", error);
      if(account_id == NULL)
      {
        g_array_free(array, TRUE);
        return NULL;
      }

      read_sheet.account = g_quark_from_string((const char*)account_id);

      xmlFree(account_id);

      for(i = 0; i < array->len; ++i)
      {
        if(g_array_index(array, InfAclSheet, i).account == read_sheet.account)
        {
          g_set_error(
            error,
            inf_request_error_quark(),
            INF_REQUEST_ERROR_INVALID_ATTRIBUTE,
            _("Permissions for account ID \"%s\" defined more than once"),
            g_quark_to_string(read_sheet.account)
          );

          g_array_free(array, TRUE);
          return FALSE;
        }
      }

      result = inf_acl_sheet_perms_from_xml(
        sheet,
        &read_sheet.mask,
        &read_sheet.perms,
        error
      );

      if(result == FALSE)
      {
        g_array_free(array, TRUE);
        return NULL;
      }

      g_array_append_vals(array, &read_sheet, 1);
    }

    if(array->len == 0)
    {
      g_array_free(array, TRUE);
      return NULL;
    }

    sheet_set = inf_acl_sheet_set_new();
    sheet_set->n_sheets = array->len;
    sheet_set->own_sheets = (InfAclSheet*)g_array_free(array, FALSE);
    sheet_set->sheets = sheet_set->own_sheets;
    return sheet_set;
  }

  return NULL;

}

/**
 * inf_acl_sheet_set_to_xml:
 * @sheet_set: The #InfAclSheetSet to serialize.
 * @xml: The XML node to serialize @sheet_set into.
 *
 * Serializes the sheet set given by @sheet_set into an XML node. The sheet
 * set can be deserialized again with inf_acl_sheet_set_from_xml().
 */
void
inf_acl_sheet_set_to_xml(const InfAclSheetSet* sheet_set,
                         xmlNodePtr xml)
{
  xmlNodePtr acl;
  xmlNodePtr sheet;
  guint i;

  g_return_if_fail(sheet_set != NULL);
  g_return_if_fail(xml != NULL);

  if(sheet_set->n_sheets > 0)
  {
    acl = xmlNewChild(xml, NULL, (const xmlChar*)"acl", NULL);
    for(i = 0; i < sheet_set->n_sheets; ++i)
    {
      sheet = xmlNewChild(acl, NULL, (const xmlChar*)"sheet", NULL);

      inf_xml_util_set_attribute(
        sheet,
        "id",
        g_quark_to_string(sheet_set->sheets[i].account)
      );

      inf_acl_sheet_perms_to_xml(
        &sheet_set->sheets[i].mask,
        &sheet_set->sheets[i].perms,
        sheet
      );
    }
  }
}

/* vim:set et sw=2 ts=2: */
