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

#define INF_TYPE_ACL_USER                  (inf_acl_user_get_type())
#define INF_TYPE_ACL_SETTING               (inf_acl_setting_get_type())
#define INF_TYPE_ACL_SHEET                 (inf_acl_sheet_get_type())
#define INF_TYPE_ACL_SHEET_SET             (inf_acl_sheet_set_get_type())

/**
 * InfAclUser:
 * @user_id: A unique user ID for this user.
 * @user_name: A human readable name for this user.
 * @first_seen: Time at which the user was first seen by the local host, or 0.
 * @last_seen: Time at which the user was last seen by the local host, or 0.
 *
 * Defines one user for whom various permissions can applied.
 */
typedef struct _InfAclUser InfAclUser;
struct _InfAclUser {
  gchar* user_id;
  gchar* user_name;
  gint64 first_seen;
  gint64 last_seen;
};

/**
 * InfAclSetting:
 * @INF_ACL_CAN_SUBSCRIBE_SESSION: The user is allowed to subscribe to a
 * session in the directory tree.
 * @INF_ACL_CAN_JOIN_USER: The user is allowed to join a user into the
 * session which corresponds to the node.
 * @INF_ACL_CAN_QUERY_USER_LIST: The user is allowed to query the full list
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

  INF_ACL_CAN_QUERY_USER_LIST,
  INF_ACL_CAN_QUERY_ACL,
  INF_ACL_CAN_SET_ACL,

  /*< private >*/
  INF_ACL_LAST
} InfAclSetting;

/**
 * InfAclSheet:
 * @user: The user for whom to apply the permissions in this sheet.
 * @mask: Bitmask which specifies which of the permissions in the @perms
 * field take effect. Fields which are masked-out are left at their default
 * value and inherited from the parent node.
 * @perms: Bitmask which specifies whether or not the user is allowed to
 * perform the various operations defined by #InfAclSetting.
 *
 * A set of permissions to be applied for a particular user and a particular
 * node in the infinote directory.
 */
typedef struct _InfAclSheet InfAclSheet;
struct _InfAclSheet {
  const InfAclUser* user;
  guint64 mask;
  guint64 perms;  
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

#define INF_ACL_MASK_ALL \
  ((1 << (INF_ACL_LAST - 1)) - 1 + (1 << (INF_ACL_LAST - 1)))

/* Default permissions */
#define INF_ACL_MASK_DEFAULT \
  (1 << INF_ACL_CAN_SUBSCRIBE_SESSION) | \
  (1 << INF_ACL_CAN_JOIN_USER)

/* Non root-node permissions */
#define INF_ACL_MASK_NONROOT \
  (1 << INF_ACL_CAN_SUBSCRIBE_SESSION) | \
  (1 << INF_ACL_CAN_JOIN_USER) | \
  (1 << INF_ACL_CAN_QUERY_ACL) | \
  (1 << INF_ACL_CAN_SET_ACL)

GType
inf_acl_user_get_type(void) G_GNUC_CONST;

GType
inf_acl_setting_get_type(void) G_GNUC_CONST;

GType
inf_acl_sheet_get_type(void) G_GNUC_CONST;

GType
inf_acl_sheet_set_get_type(void) G_GNUC_CONST;

InfAclUser*
inf_acl_user_new(const gchar* user_id,
                 const gchar* user_name);

InfAclUser*
inf_acl_user_copy(const InfAclUser* user);

void
inf_acl_user_free(InfAclUser* user);

InfAclUser*
inf_acl_user_from_xml(xmlNodePtr xml,
                      GError** error);

void
inf_acl_user_to_xml(const InfAclUser* user,
                    xmlNodePtr xml,
                    gboolean include_times);

InfAclSheet*
inf_acl_sheet_copy(const InfAclSheet* sheet);

void
inf_acl_sheet_free(InfAclSheet* sheet);

gboolean
inf_acl_sheet_perms_from_xml(xmlNodePtr xml,
                             guint64* mask,
                             guint64* perms,
                             GError** error);

void
inf_acl_sheet_perms_to_xml(guint64 mask,
                           guint64 perms,
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
                            const InfAclUser* user);

void
inf_acl_sheet_set_remove_sheet(InfAclSheetSet* sheet_set,
                               InfAclSheet* sheet);

InfAclSheet*
inf_acl_sheet_set_find_sheet(InfAclSheetSet* sheet_set,
                             const InfAclUser* user);

const InfAclSheet*
inf_acl_sheet_set_find_const_sheet(const InfAclSheetSet* sheet_set,
                                   const InfAclUser* user);

G_END_DECLS

#endif /* __INF_ACL_SHEET_H__ */

/* vim:set et sw=2 ts=2: */
