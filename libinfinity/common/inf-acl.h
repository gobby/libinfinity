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

#ifndef __INF_ACL_SHEET_H__
#define __INF_ACL_SHEET_H__

#include <glib-object.h>

#include <libxml/tree.h>

G_BEGIN_DECLS

#define INF_TYPE_ACL_ACCOUNT               (inf_acl_account_get_type())
#define INF_TYPE_ACL_SETTING               (inf_acl_setting_get_type())
#define INF_TYPE_ACL_MASK                  (inf_acl_mask_get_type())
#define INF_TYPE_ACL_SHEET                 (inf_acl_sheet_get_type())
#define INF_TYPE_ACL_SHEET_SET             (inf_acl_sheet_set_get_type())

/**
 * InfAclAccount:
 * @id: A unique ID for this account.
 * @name: A human readable account name.
 *
 * This boxed type specifies basic user account information.
 */
typedef struct _InfAclAccount InfAclAccount;
struct _InfAclAccount {
  gchar* id;
  gchar* name;
};

/**
 * InfAclSetting:
 * @INF_ACL_CAN_SUBSCRIBE_SESSION: The user is allowed to subscribe to a
 * session in the directory tree.
 * @INF_ACL_CAN_JOIN_USER: The user is allowed to join a user into the
 * session which corresponds to the node.
 * @INF_ACL_CAN_QUERY_ACCOUNT_LIST: The user is allowed to query the full list
 * of ACL accounts.
 * @INF_ACL_CAN_QUERY_ACL: The user is allowed to query the full ACL for
 * this node.
 * @INF_ACL_CAN_SET_ACL: The user is allowed to change the ACL of this node,
 * or create new nodes with a non-default ACL.
 *
 * Defines the actual permissions that can be granted or revoked for different
 * users.
 */
typedef enum _InfAclSetting {
  INF_ACL_CAN_SUBSCRIBE_SESSION,
  INF_ACL_CAN_JOIN_USER,

  INF_ACL_CAN_QUERY_ACCOUNT_LIST,
  INF_ACL_CAN_QUERY_ACL,
  INF_ACL_CAN_SET_ACL,

  /*< private >*/
  INF_ACL_LAST
} InfAclSetting;

/**
 * InfAclMask:
 * @mask: A 256 bit wide bitfield of #InfAclSetting<!-- -->s.
 *
 * This structure represents a mask of #InfAclSetting<!-- -->s, where each
 * setting can be either turned on or off.
 */
typedef struct _InfAclMask InfAclMask;
struct _InfAclMask {
  /* leave quite some space for future use */
  guint64 mask[4];
};

/**
 * InfAclSheet:
 * @account: The account for which to apply the permissions in this sheet.
 * @mask: Mask which specifies which of the permissions in the @perms
 * field take effect. Fields which are masked-out are left at their default
 * value and inherited from the parent node.
 * @perms: Mask which specifies whether or not the user is allowed to
 * perform the various operations defined by #InfAclSetting.
 *
 * A set of permissions to be applied for a particular account and a
 * particular node in the infinote directory.
 */
typedef struct _InfAclSheet InfAclSheet;
struct _InfAclSheet {
  const InfAclAccount* account;
  InfAclMask mask;
  InfAclMask perms;
};

/** InfAclSheetSet:
 * @sheets: An array of #InfAclSheet objects.
 * @n_sheets: The number of elements in the @sheets array.
 *
 * A set of #InfAclSheet<!-- -->s, one for each user.
 */
typedef struct _InfAclSheetSet InfAclSheetSet;
struct _InfAclSheetSet {
  /*< private >*/
  InfAclSheet* own_sheets;
  /*< public >*/
  const InfAclSheet* sheets;
  guint n_sheets;
};

/**
 * InfAclAccountLookupFunc:
 * @id: The ID to look up.
 * @user_data: Additional data
 *
 * This callback function is used in inf_acl_sheet_set_from_xml() to resolve
 * account IDs to actual #InfAclAccount structures.
 */
typedef const InfAclAccount* (*InfAclAccountLookupFunc)(const gchar* id,
                                                        gpointer user_data);

extern const InfAclMask INF_ACL_MASK_ALL;
extern const InfAclMask INF_ACL_MASK_DEFAULT;
extern const InfAclMask INF_ACL_MASK_ROOT;

GType
inf_acl_account_get_type(void) G_GNUC_CONST;

GType
inf_acl_setting_get_type(void) G_GNUC_CONST;

GType
inf_acl_mask_get_type(void) G_GNUC_CONST;

GType
inf_acl_sheet_get_type(void) G_GNUC_CONST;

GType
inf_acl_sheet_set_get_type(void) G_GNUC_CONST;

InfAclAccount*
inf_acl_account_new(const gchar* id,
                    const gchar* name);

InfAclAccount*
inf_acl_account_copy(const InfAclAccount* account);

void
inf_acl_account_free(InfAclAccount* account);

InfAclAccount*
inf_acl_account_from_xml(xmlNodePtr xml,
                         GError** error);

void
inf_acl_account_to_xml(const InfAclAccount* account,
                       xmlNodePtr xml);

InfAclMask*
inf_acl_mask_copy(const InfAclMask* mask);

void
inf_acl_mask_free(InfAclMask* mask);

void
inf_acl_mask_clear(InfAclMask* mask);

gboolean
inf_acl_mask_empty(const InfAclMask* mask);

gboolean
inf_acl_mask_equal(const InfAclMask* lhs,
                   const InfAclMask* rhs);

InfAclMask*
inf_acl_mask_set1(InfAclMask* mask,
                  const InfAclSetting setting);

InfAclMask*
inf_acl_mask_setv(InfAclMask* mask,
                  const InfAclSetting* settings,
                  guint n_settings);

InfAclMask*
inf_acl_mask_and(const InfAclMask* lhs,
                 const InfAclMask* rhs,
                 InfAclMask* out);

InfAclMask*
inf_acl_mask_and1(InfAclMask* mask,
                  InfAclSetting setting);

InfAclMask*
inf_acl_mask_or(const InfAclMask* lhs,
                const InfAclMask* rhs,
                InfAclMask* out);

InfAclMask*
inf_acl_mask_or1(InfAclMask* mask,
                 InfAclSetting setting);

InfAclMask*
inf_acl_mask_neg(const InfAclMask* mask,
                 InfAclMask* out);

gboolean
inf_acl_mask_has(const InfAclMask* mask,
                 InfAclSetting setting);

InfAclSheet*
inf_acl_sheet_copy(const InfAclSheet* sheet);

void
inf_acl_sheet_free(InfAclSheet* sheet);

gboolean
inf_acl_sheet_perms_from_xml(xmlNodePtr xml,
                             InfAclMask* mask,
                             InfAclMask* perms,
                             GError** error);

void
inf_acl_sheet_perms_to_xml(const InfAclMask* mask,
                           const InfAclMask* perms,
                           xmlNodePtr xml);

InfAclSheetSet*
inf_acl_sheet_set_new(void);

InfAclSheetSet*
inf_acl_sheet_set_new_external(const InfAclSheet* sheets,
                               guint n_sheets);

InfAclSheetSet*
inf_acl_sheet_set_copy(const InfAclSheetSet* sheet_set);

void
inf_acl_sheet_set_free(InfAclSheetSet* sheet_set);

InfAclSheet*
inf_acl_sheet_set_add_sheet(InfAclSheetSet* sheet_set,
                            const InfAclAccount* account);

void
inf_acl_sheet_set_remove_sheet(InfAclSheetSet* sheet_set,
                               InfAclSheet* sheet);

InfAclSheetSet*
inf_acl_sheet_set_merge_sheets(InfAclSheetSet* sheet_set,
                               const InfAclSheetSet* other);

InfAclSheetSet*
inf_acl_sheet_set_get_clear_sheets(const InfAclSheetSet* sheet_set);

InfAclSheet*
inf_acl_sheet_set_find_sheet(InfAclSheetSet* sheet_set,
                             const InfAclAccount* account);

const InfAclSheet*
inf_acl_sheet_set_find_const_sheet(const InfAclSheetSet* sheet_set,
                                   const InfAclAccount* account);

InfAclSheetSet*
inf_acl_sheet_set_from_xml(xmlNodePtr xml,
                           InfAclAccountLookupFunc lookup_func,
                           gpointer user_data,
                           GError** error);

void
inf_acl_sheet_set_to_xml(const InfAclSheetSet* sheet_set,
                         xmlNodePtr xml);

G_END_DECLS

#endif /* __INF_ACL_SHEET_H__ */

/* vim:set et sw=2 ts=2: */
