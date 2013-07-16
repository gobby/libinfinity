/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2011 Armin Burgmeier <armin@arbur.net>
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
 * SECTION:inf-acl-sheet
 * @title: InfAclSheet
 * @short_description: ACL Permissions
 * @include: libinfinity/common/inf-acl-sheet.h
 * @see_also: #InfAclTable
 * @stability: Unstable
 *
 * #InfAclSheet represents settings for one user and one node in the
 * directory. One setting can have three states: it can be enabled, it can be
 * disabled or it can be set to default which means to take the setting from
 * the parent directory.
 *
 * Along with #InfAclSheet comes #InfAclUser, which represents a single user
 * for whom permissions can be defined via ACLs.
 *
 * A #InfAclSheetSet represents a set of #InfAclSheet<!-- -->s, for different
 * users.
 */

#include <libinfinity/common/inf-acl-sheet.h>
#include <libinfinity/common/inf-xml-util.h>
#include <libinfinity/common/inf-error.h>
#include <libinfinity/inf-i18n.h>

#include <string.h>

GType
inf_acl_user_get_type(void)
{
  static GType acl_user_type = 0;

  if(!acl_user_type)
  {
    acl_user_type = g_boxed_type_register_static(
      "InfAclUser",
      (GBoxedCopyFunc)inf_acl_user_copy,
      (GBoxedFreeFunc)inf_acl_user_free
    );
  }

  return acl_user_type;
}

GType
inf_acl_setting_get_type(void)
{
  static GType acl_setting_type = 0;

  if(!acl_setting_type)
  {
    static const GEnumValue acl_setting_values[] = {
      {
        INF_ACL_CAN_SUBSCRIBE_SESSION,
        "INF_ACL_CAN_SUBSCRIBE_SESSION",
        "can-subscribe-session"
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

    acl_setting_type = g_enum_register_static(
      "InfAclSetting",
      acl_setting_values
    );
  }

  return acl_setting_type;
}

GType
inf_acl_sheet_get_type(void)
{
  static GType acl_sheet_type = 0;

  if(!acl_sheet_type)
  {
    acl_sheet_type = g_boxed_type_register_static(
      "InfAclSheet",
      (GBoxedCopyFunc)inf_acl_sheet_copy,
      (GBoxedFreeFunc)inf_acl_sheet_free
    );
  }

  return acl_sheet_type;
}

GType
inf_acl_sheet_set_get_type(void)
{
  static GType acl_sheet_set_type = 0;

  if(!acl_sheet_set_type)
  {
    acl_sheet_set_type = g_boxed_type_register_static(
      "InfAclSheetSet",
      (GBoxedCopyFunc)inf_acl_sheet_set_copy,
      (GBoxedFreeFunc)inf_acl_sheet_set_free
    );
  }

  return acl_sheet_set_type;
}

/**
 * inf_acl_user_new:
 * @user_id: The (unique) ID of the new user.
 * @user_name: The human-readable name of the new user.
 *
 * Creates a new #InfAclUser object with the mandatory fields. All other
 * fields are set to the default values and can be changed later.
 *
 * Returns: A new #InfAclUser object.
 */
InfAclUser*
inf_acl_user_new(const gchar* user_id,
                 const gchar* user_name)
{
  InfAclUser* user;
  user = g_slice_new(InfAclUser);
  user->user_id = g_strdup(user_id);
  if(user_name != NULL)
    user->user_name = g_strdup(user_name);
  user->first_seen = 0;
  user->last_seen = 0;
  return user;
}

/**
 * inf_acl_user_copy:
 * @user: The #InfAclUser to copy.
 *
 * Creates a copy of @user.
 *
 * Returns: A new #InfAclUser. Free with inf_acl_user_free() when no longer
 * needed.
 */
InfAclUser*
inf_acl_user_copy(const InfAclUser* user)
{
  InfAclUser* new_user;
  new_user = g_slice_new(InfAclUser);
  new_user->user_id = g_strdup(user->user_id);
  if(user->user_name != NULL)
    new_user->user_name = g_strdup(user->user_name);
  new_user->first_seen = user->first_seen;
  new_user->last_seen = user->last_seen;
  return new_user;
}

/**
 * inf_acl_user_free:
 * @user: A #InfAclUser.
 *
 * Releases all resources allocated by @user.
 */
void
inf_acl_user_free(InfAclUser* user)
{
  g_free(user->user_id);
  g_free(user->user_name);
  g_slice_free(InfAclUser, user);
}

/**
 * inf_acl_user_from_xml:
 * @xml: An XML node.
 * @error: Location to store error information, if any.
 *
 * Attempts to deserialize an #InfAclUser that was written to an XML node
 * by the function inf_acl_user_to_xml(). If an error occurs, such as
 * mandatory fields being missing, the function returns %NULL and @error is
 * set.
 *
 * Returns: A new #InfAclUser on success, or %NULL on failure. Free with
 * inf_acl_user_free() when no longer needed.
 */
InfAclUser*
inf_acl_user_from_xml(xmlNodePtr xml,
                      GError** error)
{
  xmlChar* user_id;
  xmlChar* user_name;
  double first_seen;
  gboolean first_seen_result;
  double last_seen;
  gboolean last_seen_result;
  InfAclUser* new_user;
  GError* local_error;

  user_id = inf_xml_util_get_attribute_required(xml, "id", error);
  if(user_id == NULL) return NULL;

  user_name = inf_xml_util_get_attribute(xml, "name");

  new_user = NULL;
  local_error = NULL;

  first_seen_result = inf_xml_util_get_attribute_double(
    xml,
    "first_seen",
    &first_seen,
    &local_error
  );

  if(local_error == NULL)
  {
    last_seen_result = inf_xml_util_get_attribute_double(
      xml,
      "last_seen",
      &last_seen,
      &local_error
    );

    if(local_error == NULL)
    {
      new_user = inf_acl_user_new(user_id, user_name);
      if(first_seen_result) new_user->first_seen = (gint64)(first_seen * 1e6);
      if(last_seen_result) new_user->last_seen = (gint64)(last_seen * 1e6);
    }
  }

  xmlFree(user_id);
  if(user_name != NULL) xmlFree(user_name);

  if(local_error != NULL)
    g_propagate_error(error, local_error);
  return new_user;
}

/**
 * inf_acl_user_to_xml:
 * @user: A #InfAclUser.
 * @xml: The XML node to write the user attributes to.
 * @include_times: Whether to include the first and last seen time.
 *
 * Serializes the properties of @user into the XML node @xml. The function
 * inf_acl_user_from_xml() does the reverse operation and deserializes the
 * user object from the XML.
 *
 * If @include_times is set to %FALSE the @first_seen and @last_seen fields
 * of #InfAclUser are not serialized. On deserialization these values will be
 * left at the default, which is 0.
 */
void
inf_acl_user_to_xml(const InfAclUser* user,
                    xmlNodePtr xml,
                    gboolean include_times)
{
  inf_xml_util_set_attribute(xml, "id", user->user_id);
  if(user->user_name != NULL)
    inf_xml_util_set_attribute(xml, "name", user->user_name);

  if(include_times && user->first_seen != 0)
  {
    inf_xml_util_set_attribute_double(
      xml,
      "first_seen",
      user->first_seen / 1e6
    );
  }

  if(include_times && user->last_seen != 0)
  {
    inf_xml_util_set_attribute_double(
      xml,
      "last_seen",
      user->last_seen / 1e6
    );
  }
}

/**
 * inf_acl_sheet_copy:
 * @sheet: A #InfAclSheet.
 *
 * Makes a dynamically allocated copy of @sheet. This should not be used by
 * applications because you can copy the structs by value, but it is useful
 * for properties and bindings. 
 *
 * Returns: A newly-allocated copy of @sheet. Free with
 * inf_acl_sheet_free() when no longer in use.
 */
InfAclSheet*
inf_acl_sheet_copy(const InfAclSheet* sheet)
{
  InfAclSheet* new_sheet;
  new_sheet = g_slice_new(InfAclSheet);
  new_sheet->user = sheet->user;
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
 * @mask: Output parameter to write the permission mask to.
 * @perms: Output parameter to write the permissions to.
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
                             guint64* mask,
                             guint64* perms,
                             GError** error)
{
  GEnumClass* enum_class;
  guint i;
  xmlChar* attr;
  guint64 value_mask;

  *mask = 0;
  /* not strictly required because the relevant bits are initialized
   * below, but helps in debugging: */
  *perms = 0;

  enum_class = G_ENUM_CLASS(g_type_class_ref(INF_TYPE_ACL_SETTING));
  for(i = 0; i < enum_class->n_values; ++i)
  {
    attr = inf_xml_util_get_attribute(xml, enum_class->values[i].value_nick);
    if(attr != NULL)
    {
      value_mask = ((guint64)1 << (guint64)enum_class->values[i].value);

      *mask |= value_mask;
      if(strcmp((const xmlChar*)attr, "yes") == 0)
      {
        *perms |= value_mask;
      }
      else if(strcmp((const xmlChar*)attr, "no") == 0)
      {
        *perms &= ~value_mask;
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
 * This function writes teh given permission mask and permission flags to the
 * XML node @xml. They can be converted back using the
 * inf_acl_sheet_perms_from_xml() function.
 */
void
inf_acl_sheet_perms_to_xml(guint64 mask,
                           guint64 perms,
                           xmlNodePtr xml)
{
  GEnumClass* enum_class;
  guint i;
  guint64 value_mask;
  const gchar* value_nick;

  enum_class = G_ENUM_CLASS(g_type_class_ref(INF_TYPE_ACL_SETTING));

  for(i = 0; i < enum_class->n_values; ++i)
  {
    value_mask = ((guint64)1 << (guint64)enum_class->values[i].value);
    if((mask & value_mask) != 0)
    {
      value_nick = enum_class->values[i].value_nick;
      if((perms & value_mask) != 0)
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
 * Returns: A new #InfAclSheetSet. Free with inf_acl_sheet_set_free().
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
 * @sheets: An array of #InfAclSheet<!-- -->s
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
 * Returns: A new #InfAclSheetSet. Free with inf_acl_sheet_set_free() when
 * no longer needed.
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
 * Returns: A new #InfAclSheetSet. Free with inf_acl_sheet_set_free() when
 * no longer needed.
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
 * @user: The user for which to add a new sheet.
 *
 * Adds a new default sheet for @user to @sheet_set. The function returns
 * a pointer to the new sheet. The pointer stays valid as long as no other
 * sheet is added to the set. If there is already a sheet for @user in the
 * set, then the existing sheet is returned instead.
 *
 * This function can only be used if the sheet set has not been created with
 * the inf_acl_sheet_set_new_external() function.
 *
 * Returns: A #InfAclSheet for the new user.
 */
InfAclSheet*
inf_acl_sheet_set_add_sheet(InfAclSheetSet* sheet_set,
                            const InfAclUser* user)
{
  guint i;

  g_return_val_if_fail(sheet_set != NULL, NULL);
  g_return_val_if_fail(user != NULL, NULL);

  g_return_val_if_fail(
    sheet_set->own_sheets != NULL || sheet_set->n_sheets == 0,
    NULL
  );

  for(i = 0; i < sheet_set->n_sheets; ++i)
    if(sheet_set->own_sheets[i].user == user)
      return &sheet_set->own_sheets[i];

  ++sheet_set->n_sheets;
  sheet_set->own_sheets = g_realloc(
    sheet_set->own_sheets,
    sheet_set->n_sheets * sizeof(InfAclSheet)
  );

  sheet_set->sheets = sheet_set->own_sheets;

  sheet_set->own_sheets[i].user = user;
  sheet_set->own_sheets[i].mask = 0;
  sheet_set->own_sheets[i].perms = 0; /* not strictly required */
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
 * inf_acl_sheet_set_find_sheet:
 * @sheet_set: A #InfAclSheetSet.
 * @user: The #InfAclUser whose ACL sheet is to be found.
 *
 * Returns the #InfAclSheet for @user. If there is no such sheet in
 * @sheet_set, the function returns %NULL.
 *
 * This function can only be used if the sheet set has not been created with
 * the inf_acl_sheet_set_new_external() function.
 *
 * Returns: A #InfAclSheet for @user, or %NULL.
 */
InfAclSheet*
inf_acl_sheet_set_find_sheet(InfAclSheetSet* sheet_set,
                             const InfAclUser* user)
{
  guint i;

  g_return_val_if_fail(sheet_set != NULL, NULL);
  g_return_val_if_fail(user != NULL, NULL);

  g_return_val_if_fail(
    sheet_set->own_sheets != NULL || sheet_set->n_sheets == 0,
    NULL
  );

  for(i = 0; i < sheet_set->n_sheets; ++i)
    if(sheet_set->own_sheets[i].user == user)
      return &sheet_set->own_sheets[i];

  return NULL;
}

/**
 * inf_acl_sheet_set_find_const_sheet:
 * @sheet_set: A #InfAclSheetSet.
 * @user: The #InfAclUser whose ACL sheet is to be found.
 *
 * Returns the #InfAclSheet for @user. If there is no such sheet in
 * @sheet_set, the function returns %NULL.
 *
 * The difference between this function and
 * inf_acl_sheet_set_find_sheet() is that this function returns a sheet
 * that cannot be modified, but it can also be used on a sheet set created
 * with the inf_acl_sheet_set_new_external() function.
 */
const InfAclSheet*
inf_acl_sheet_set_find_const_sheet(const InfAclSheetSet* sheet_set,
                                   const InfAclUser* user)
{
  guint i;

  g_return_val_if_fail(sheet_set != NULL, NULL);
  g_return_val_if_fail(user != NULL, NULL);

  for(i = 0; i < sheet_set->n_sheets; ++i)
    if(sheet_set->sheets[i].user == user)
      return &sheet_set->sheets[i];

  return NULL;
}

/* vim:set et sw=2 ts=2: */
