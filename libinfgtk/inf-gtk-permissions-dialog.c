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
 * SECTION:inf-gtk-permissions-dialog
 * @title: InfGtkPermissionsDialog
 * @short_description: A dialog to view and modify the ACL of a directory
 * node
 * @include: libinfgtk/inf-gtk-permissions-dialog.h
 * @stability: Unstable
 *
 * #InfGtkPermissionsDialog is a dialog widget which allows to view and
 * modify the ACL of a node in a infinote directory. It shows a list of all
 * available users and allows the permissions for each of them to be changed,
 * using a #InfGtkAclSheetView widget.
 *
 * If the "can-query-acl" permission is not granted for the local user, the
 * dialog only shows the permissions for the default user and the local user.
 * The dialog also comes with a status text to inform the user why certain
 * functionality is not available.
 *
 * The dialog class reacts to changes to the ACL in real time, and also if the
 * node that is being monitored is removed.
 **/

#include <libinfgtk/inf-gtk-permissions-dialog.h>
#include <libinfgtk/inf-gtk-acl-sheet-view.h>
#include <libinfinity/common/inf-request-result.h>
#include <libinfinity/common/inf-error.h>
#include <libinfinity/inf-i18n.h>
#include <gdk/gdkkeysyms.h>

enum {
  INF_GTK_PERMISSIONS_DIALOG_COLUMN_ID = 0,
  INF_GTK_PERMISSIONS_DIALOG_COLUMN_NAME = 1
};

typedef struct _InfGtkPermissionsDialogPendingSheet
  InfGtkPermissionsDialogPendingSheet;
struct _InfGtkPermissionsDialogPendingSheet {
  InfGtkPermissionsDialog* dialog;
  GtkTreeRowReference* row;
  InfAclSheet sheet;
  InfAclAccountId last_combo_changed_id;
  InfRequest* lookup_request;
};

typedef struct _InfGtkPermissionsDialogPrivate InfGtkPermissionsDialogPrivate;
struct _InfGtkPermissionsDialogPrivate {
  InfBrowser* browser;
  InfBrowserIter browser_iter;

  GtkListStore* account_store;

  /* If accounts is NULL, then the account list is not available. Note that we
   * only need the account list when the user adds a new sheet, to present her
   * the available users to choose from. If the list is not available, we
   * perform a reverse lookup. */
  InfRequest* query_acl_account_list_request;
  gboolean account_list_queried;
  InfAclAccount* accounts;
  guint n_accounts;

  InfRequest* query_acl_request;
  GSList* set_acl_requests;
  GSList* remove_acl_account_requests;
  GSList* lookup_acl_account_requests;

  GSList* pending_sheets;

  GtkMenu* popup_menu;
  InfAclAccountId popup_account;

  GtkCellRenderer* renderer;
  GtkWidget* tree_view;
  GtkWidget* sheet_view;
  GtkWidget* status_image;
  GtkWidget* status_text;

  GtkWidget* add_button;
  GtkWidget* remove_button;
};

enum {
  PROP_0,

  PROP_BROWSER,
  PROP_BROWSER_ITER
};

#define INF_GTK_PERMISSIONS_DIALOG_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_GTK_TYPE_PERMISSIONS_DIALOG, InfGtkPermissionsDialogPrivate))

static GtkDialogClass* parent_class;

/*
 * Private functionality
 */

static void
inf_gtk_permissions_dialog_update(InfGtkPermissionsDialog* dialog,
                                  const GError* error);

static void
inf_gtk_permissions_dialog_update_sheet(InfGtkPermissionsDialog* dialog);

static gboolean
inf_gtk_permissions_dialog_find_account(InfGtkPermissionsDialog* dialog,
                                        InfAclAccountId account,
                                        GtkTreeIter* out_iter)
{
  InfGtkPermissionsDialogPrivate* priv;
  gpointer row_account_id;
  GtkTreeModel* model;
  GtkTreeIter iter;

  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);
  model = GTK_TREE_MODEL(priv->account_store);

  if(gtk_tree_model_get_iter_first(model, &iter))
  {
    do
    {
      gtk_tree_model_get(
        model,
        &iter,
        INF_GTK_PERMISSIONS_DIALOG_COLUMN_ID,
        &row_account_id,
        -1
      );

      if(row_account_id == INF_ACL_ACCOUNT_ID_TO_POINTER(account))
      {
        if(out_iter != NULL)
          *out_iter = iter;
        return TRUE;
      }
    } while(gtk_tree_model_iter_next(model, &iter));
  }

  return FALSE;
}

static InfGtkPermissionsDialogPendingSheet*
inf_gtk_permissions_dialog_find_pending_sheet(InfGtkPermissionsDialog* dialog,
                                              GtkTreeIter* iter)
{
  InfGtkPermissionsDialogPrivate* priv;
  GtkTreeModel* model;
  GtkTreePath* path;
  GSList* item;
  InfGtkPermissionsDialogPendingSheet* pending;
  GtkTreePath* pending_path;

  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);
  model = GTK_TREE_MODEL(priv->account_store);

  path = gtk_tree_model_get_path(model, iter);
  for(item = priv->pending_sheets; item != NULL; item = item->next)
  {
    pending = (InfGtkPermissionsDialogPendingSheet*)item->data;
    pending_path = gtk_tree_row_reference_get_path(pending->row);
    g_assert(pending_path != NULL);

    if(gtk_tree_path_compare(path, pending_path) == 0)
    {
      gtk_tree_path_free(path);
      gtk_tree_path_free(pending_path);
      return pending;
    }

    gtk_tree_path_free(pending_path);
  }

  gtk_tree_path_free(path);
  return NULL;
}

static void
inf_gtk_permissions_dialog_set_acl_finished_cb(InfRequest* request,
                                               const InfRequestResult* result,
                                               const GError* error,
                                               gpointer user_data)
{
  InfGtkPermissionsDialog* dialog;
  InfGtkPermissionsDialogPrivate* priv;

  dialog = INF_GTK_PERMISSIONS_DIALOG(user_data);
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  if(error != NULL)
  {
    /* Show the error message */
    inf_gtk_permissions_dialog_update(dialog, error);

    /* Reset sheet to what we had before making the request */
    inf_gtk_permissions_dialog_update_sheet(dialog);
  }

  if(g_slist_find(priv->set_acl_requests, request) != NULL)
  {
    priv->set_acl_requests = g_slist_remove(priv->set_acl_requests, request);
    g_object_unref(request);
  }
}

static void
inf_gtk_permissions_dialog_selection_changed_cb(GtkTreeSelection* selection,
                                                gpointer user_data)
{
  InfGtkPermissionsDialog* dialog;
  dialog = INF_GTK_PERMISSIONS_DIALOG(user_data);

  /* Update the sheet that is displayed */
  inf_gtk_permissions_dialog_update_sheet(dialog);

  /* Also update the account list itself -- if the previously selected entry
   * does not have any permissions set, for example because the user set
   * everything to default, then remove the account from the account list. */
  inf_gtk_permissions_dialog_update(dialog, NULL);
}

static void
inf_gtk_permissions_dialog_sheet_changed_cb(InfGtkAclSheetView* sheet_view,
                                            gpointer user_data)
{
  InfGtkPermissionsDialog* dialog;
  InfGtkPermissionsDialogPrivate* priv;
  const InfAclSheet* sheet;
  InfAclSheetSet sheet_set;
  InfRequest* request;

  GtkTreeSelection* selection;
  gboolean has_selection;
  GtkTreeIter iter;
  InfGtkPermissionsDialogPendingSheet* pending;

  dialog = INF_GTK_PERMISSIONS_DIALOG(user_data);
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  sheet = inf_gtk_acl_sheet_view_get_sheet(
    INF_GTK_ACL_SHEET_VIEW(priv->sheet_view)
  );

  g_assert(sheet != NULL);

  /* If the sheet does not have an ID set, the lookup is still in progress.
   * In that case, we run the ACL setting once we have looked up the ID. */
  if(sheet->account != 0)
  {
    sheet_set.own_sheets = NULL;
    sheet_set.sheets = sheet;
    sheet_set.n_sheets = 1;

    request = inf_browser_set_acl(
      priv->browser,
      &priv->browser_iter,
      &sheet_set,
      inf_gtk_permissions_dialog_set_acl_finished_cb,
      dialog
    );

    if(request != NULL)
    {
      priv->set_acl_requests =
        g_slist_prepend(priv->set_acl_requests, request);
      g_object_ref(request);
    }
  }
  else
  {
    /* Must be a pending sheet */
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->tree_view));
    has_selection = gtk_tree_selection_get_selected(selection, NULL, &iter);
    g_assert(has_selection);

    pending = inf_gtk_permissions_dialog_find_pending_sheet(dialog, &iter);
    g_assert(pending != NULL);

    pending->sheet = *sheet;
  }
}

static int
inf_gtk_permissions_dialog_account_sort_func(GtkTreeModel* model,
                                             GtkTreeIter* a,
                                             GtkTreeIter* b,
                                             gpointer user_data)
{
  InfGtkPermissionsDialog* dialog;
  InfGtkPermissionsDialogPrivate* priv;

  InfAclAccountId default_id;
  gpointer account_a_id_ptr;
  gpointer account_b_id_ptr;
  InfAclAccountId account_a_id;
  InfAclAccountId account_b_id;
  const gchar* account_a_id_str;
  const gchar* account_b_id_str;
  gchar* account_a_name;
  gchar* account_b_name;

  int result;

  dialog = INF_GTK_PERMISSIONS_DIALOG(user_data);
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  gtk_tree_model_get(
    model,
    a,
    INF_GTK_PERMISSIONS_DIALOG_COLUMN_ID, &account_a_id_ptr,
    INF_GTK_PERMISSIONS_DIALOG_COLUMN_NAME, &account_a_name,
    -1
  );

  gtk_tree_model_get(
    model,
    b,
    INF_GTK_PERMISSIONS_DIALOG_COLUMN_ID, &account_b_id_ptr,
    INF_GTK_PERMISSIONS_DIALOG_COLUMN_NAME, &account_b_name,
    -1
  );

  account_a_id = INF_ACL_ACCOUNT_POINTER_TO_ID(account_a_id_ptr);
  account_b_id = INF_ACL_ACCOUNT_POINTER_TO_ID(account_b_id_ptr);
  account_a_id_str = inf_acl_account_id_to_string(account_a_id);
  account_b_id_str = inf_acl_account_id_to_string(account_b_id);

  /* default sorts before anything */
  default_id = inf_acl_account_id_from_string("default");
  if(account_a_id == default_id)
  {
    if(account_b_id == default_id)
      result = 0;
    else
      result = -1;
  }
  else if(account_b_id == default_id)
  {
    result = 1;
  }
  /* Next, accounts with user name and ID sort before accounts without
   * one of the two*/
  else if(account_a_name != NULL && account_a_id != 0)
  {
    if(account_b_name != NULL && account_b_id != 0)
      result = g_utf8_collate(account_a_name, account_b_name);
    else
      result = -1;
  }
  else if(account_b_name != NULL && account_b_id != 0)
  {
    result = 1;
  }
  /* Next, accounts with ID but no user name are preferred. Such accounts
   * have a lookup pending, but the sheet is synchronized. */
  else if(account_a_name == NULL && account_a_id != 0)
  {
    if(account_b_name == NULL && account_b_id != 0)
      result = g_utf8_collate(account_a_id_str, account_b_id_str);
    else
      result = -1;
  }
  else if(account_b_name == NULL && account_b_id != 0)
  {
    result = 1;
  }
  /* Next, accounts with user name but no ID. These are recently added
   * entries, and the ID lookup is still in progress. The sheets are
   * not yet synchronized. If the ID lookup fails, the entry is removed. */
  else if(account_a_name != NULL && account_a_id == 0)
  {
    if(account_b_name != NULL && account_b_id == 0)
      result = g_utf8_collate(account_a_name, account_b_name);
    else
      result = -1;
  }
  else if(account_b_name != NULL && account_b_id == 0)
  {
    result = 1;
  }
  /* Now, it would mean that both A and B do have neither ID nor name
   * set. This cannot be, since this can only happen with newly created
   * entries, but these entries get a name set immediately. */
  else
  {
    g_assert_not_reached();
    result = 0;
  }

  g_free(account_a_name);
  g_free(account_b_name);
  return result;
}

static void
inf_gtk_permissions_dialog_lookup_by_name_finished_cb(
  InfRequest* request,
  const InfRequestResult* result,
  const GError* error,
  gpointer user_data);

static void
inf_gtk_permissions_dialog_remove_pending_sheet(
  InfGtkPermissionsDialog* dialog,
  InfGtkPermissionsDialogPendingSheet* pending)
{
  InfGtkPermissionsDialogPrivate* priv;
  GtkTreePath* path;
  GtkTreeIter iter;
  gboolean has_iter;

  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  if(pending->lookup_request != NULL)
  {
    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(pending->lookup_request),
      G_CALLBACK(inf_gtk_permissions_dialog_lookup_by_name_finished_cb),
      pending
    );

    g_object_unref(pending->lookup_request);
  }

  /* Remove sheet from the list, so that when gtk_list_store_remove() causes
   * an update (due to the tree selection changing), there is no invalid
   * pending sheet in the list anymore. */
  priv->pending_sheets = g_slist_remove(priv->pending_sheets, pending);

  /* Remove the entry from the list, except the pending sheet was realized,
   * i.e. the ID was looked up. */
  if(pending->row != NULL)
  {
    path = gtk_tree_row_reference_get_path(pending->row);
    g_assert(path != NULL);

    has_iter = gtk_tree_model_get_iter(
      GTK_TREE_MODEL(priv->account_store),
      &iter,
      path
    );

    g_assert(has_iter == TRUE);
    gtk_list_store_remove(priv->account_store, &iter);
    gtk_tree_path_free(path);

    gtk_tree_row_reference_free(pending->row);
  }

  g_slice_free(InfGtkPermissionsDialogPendingSheet, pending);
}

static void
inf_gtk_permissions_dialog_realize_pending_sheet(
  InfGtkPermissionsDialog* dialog,
  InfGtkPermissionsDialogPendingSheet* pending,
  InfAclAccountId id,
  const gchar* name)
{
  InfGtkPermissionsDialogPrivate* priv;
  GtkTreeIter iter;
  GtkTreePath* path;
  gboolean has_iter;

  InfAclSheet pending_sheet;
  InfAclSheetSet sheet_set;
  InfRequest* request;

  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  pending_sheet = pending->sheet;
  pending_sheet.account = id;

  if(inf_gtk_permissions_dialog_find_account(dialog, id, &iter))
  {
    /* An entry with that ID exists already. Don't try to merge it with the
     * pending sheet here, but just discard the pending sheet. */
    path = gtk_tree_model_get_path(
      GTK_TREE_MODEL(priv->account_store),
      &iter
    );

    /* When selecting the already existing entry, block the
     * selection-changed handler, so that it does not already cause an
     * update of the dialog. We do the update after we have also removed
     * the pending sheet */
    inf_signal_handlers_block_by_func(
      G_OBJECT(gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->tree_view))),
      G_CALLBACK(inf_gtk_permissions_dialog_selection_changed_cb),
      dialog
    );

    gtk_tree_view_set_cursor(
      GTK_TREE_VIEW(priv->tree_view),
      path,
      gtk_tree_view_get_column(GTK_TREE_VIEW(priv->tree_view), 0),
      FALSE
    );

    inf_gtk_permissions_dialog_remove_pending_sheet(dialog, pending);

    inf_signal_handlers_unblock_by_func(
      G_OBJECT(gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->tree_view))),
      G_CALLBACK(inf_gtk_permissions_dialog_selection_changed_cb),
      dialog
    );

    gtk_tree_path_free(path);

    /* Update currently displayed sheet, since we have changed the selection
     * with blocked selection-changed signal handler */
    inf_gtk_permissions_dialog_update_sheet(dialog);
  }
  else
  {
    path = gtk_tree_row_reference_get_path(pending->row);
    g_assert(path != NULL);

    has_iter  = gtk_tree_model_get_iter(
      GTK_TREE_MODEL(priv->account_store),
      &iter,
      path
    );

    g_assert(has_iter == TRUE);

    /* Set the entry in the list store */
    gtk_list_store_set(
      priv->account_store,
      &iter,
      INF_GTK_PERMISSIONS_DIALOG_COLUMN_ID, INF_ACL_ACCOUNT_ID_TO_POINTER(id),
      INF_GTK_PERMISSIONS_DIALOG_COLUMN_NAME, name,
      -1
    );

    /* Remove the pending item. Free the tree row reference before, so that
     * inf_gtk_permissions_dialog_remove_pending_sheet does not remove the
     * realized entry from the list store. */
    gtk_tree_row_reference_free(pending->row);
    pending->row = NULL;

    inf_gtk_permissions_dialog_remove_pending_sheet(dialog, pending);

    /* Set the realized sheet on the sheet view. This is important, so that
     * when the sheet view emits its changed signal, the account ID is no
     * longer set to 0. */
    inf_signal_handlers_block_by_func(
      G_OBJECT(priv->sheet_view),
      G_CALLBACK(inf_gtk_permissions_dialog_sheet_changed_cb),
      dialog
    );

    inf_gtk_acl_sheet_view_set_sheet(
      INF_GTK_ACL_SHEET_VIEW(priv->sheet_view),
      &pending_sheet
    );

    inf_signal_handlers_unblock_by_func(
      G_OBJECT(priv->sheet_view),
      G_CALLBACK(inf_gtk_permissions_dialog_sheet_changed_cb),
      dialog
    );

    /* If there are alreay options set, then set the corresponding ACL. Note
     * that in principle our "changed" signal handler would do that, but we
     * have blocked it above. This allows us to reduce network traffic when the
     * mask is empty. */
    if(!inf_acl_mask_empty(&pending_sheet.mask))
    {
      sheet_set.own_sheets = NULL;
      sheet_set.sheets = &pending_sheet;
      sheet_set.n_sheets = 1;

      request = inf_browser_set_acl(
        priv->browser,
        &priv->browser_iter,
        &sheet_set,
        inf_gtk_permissions_dialog_set_acl_finished_cb,
        dialog
      );

      if(request != NULL)
      {
        priv->set_acl_requests =
          g_slist_prepend(priv->set_acl_requests, request);
        g_object_ref(request);
      }
    }
  }
}

static void
inf_gtk_permissions_dialog_lookup_by_name_finished_cb(
  InfRequest* request,
  const InfRequestResult* result,
  const GError* error,
  gpointer user_data)
{
  InfGtkPermissionsDialogPendingSheet* pending;
  InfGtkPermissionsDialog* dialog;
  InfGtkPermissionsDialogPrivate* priv;
  const InfAclAccount* accounts;
  guint n_accounts;

  pending = (InfGtkPermissionsDialogPendingSheet*)user_data;
  dialog = pending->dialog;
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  if(pending->lookup_request != NULL)
  {
    g_object_unref(pending->lookup_request);
    pending->lookup_request = NULL;
  }

  if(error != NULL)
  {
    g_warning("Failed to reverse lookup: %s", error->message);
    inf_gtk_permissions_dialog_remove_pending_sheet(dialog, pending);
  }
  else
  {
    inf_request_result_get_lookup_acl_accounts(
      result,
      NULL,
      &accounts,
      &n_accounts
    );

    if(n_accounts > 0)
    {
      /* There is at least one user with the given name. If there are more,
       * we cannot distinguish between them, so just take the first */
      if(n_accounts > 1)
      {
        g_warning(
          "Multiple accounts with the same name \"%s\"",
          accounts[0].name
        );
      }

      inf_gtk_permissions_dialog_realize_pending_sheet(
        dialog,
        pending,
        accounts[0].id,
        accounts[0].name
      );
    }
    else
    {
      /* There is no user with this name */
      inf_gtk_permissions_dialog_remove_pending_sheet(dialog, pending);
    }
  }
}

static void
inf_gtk_permissions_dialog_lookup_acl_accounts_finished_cb(
  InfRequest* request,
  const InfRequestResult* result,
  const GError* error,
  gpointer user_data);

static void
inf_gtk_permissions_dialog_remove_lookup_acl_accounts_request(
  InfGtkPermissionsDialog* dialog,
  InfRequest* request)
{
  InfGtkPermissionsDialogPrivate* priv;
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  inf_signal_handlers_disconnect_by_func(
    request,
    G_CALLBACK(inf_gtk_permissions_dialog_lookup_acl_accounts_finished_cb),
    dialog
  );

  /* Can finish instantly */
  if(g_slist_find(priv->lookup_acl_account_requests, request) != NULL)
  {
    priv->lookup_acl_account_requests =
      g_slist_remove(priv->lookup_acl_account_requests, request);

    g_object_unref(request);
  }
}

static void
inf_gtk_permissions_dialog_lookup_acl_accounts_finished_cb(
  InfRequest* request,
  const InfRequestResult* result,
  const GError* error,
  gpointer user_data)
{
  InfGtkPermissionsDialog* dialog;
  InfGtkPermissionsDialogPrivate* priv;
  const InfAclAccount* accounts;
  guint n_accounts;
  guint i;
  GtkTreeIter iter;
  InfAclAccountId account_id;

  dialog = INF_GTK_PERMISSIONS_DIALOG(user_data);
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  if(error != NULL)
  {
    /* TODO: Should we show this in the dialog? */
    g_warning("Failed to look up accounts: %s\n", error->message);
  }
  else
  {
    inf_request_result_get_lookup_acl_accounts(
      result,
      NULL,
      &accounts,
      &n_accounts
    );

    for(i = 0; i < n_accounts; ++i)
    {
      if(accounts[i].name != NULL)
      {
        account_id = accounts[i].id;
        if(inf_gtk_permissions_dialog_find_account(dialog, account_id, &iter))
        {
          gtk_list_store_set(
            GTK_LIST_STORE(priv->account_store),
            &iter,
            INF_GTK_PERMISSIONS_DIALOG_COLUMN_NAME,
            accounts[i].name,
            -1
          );
        }
      }
    }
  }

  inf_gtk_permissions_dialog_remove_lookup_acl_accounts_request(
    dialog,
    request
  );
}

static void
inf_gtk_permissions_dialog_fill_account_list(InfGtkPermissionsDialog* dialog,
                                             const InfAclAccountId* ids,
                                             guint n_ids)
{
  InfGtkPermissionsDialogPrivate* priv;
  GtkTreeModel* model;
  gboolean* have_accounts;
  GtkTreeIter iter;
  gpointer account_id_ptr;
  InfAclAccountId account_id;
  gboolean has_row;
  guint i;

  InfAclAccountId* lookup_ids;
  guint n_lookup_ids;
  guint lookup_index;
  const gchar* new_account_name;

  InfAclMask perms;
  const InfAclAccount* default_account;
  const InfAclAccount* local_account;
  InfRequest* request;

  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);
  model = GTK_TREE_MODEL(priv->account_store);

  /* Remove all accounts that are not present in the given account list.
   * Flag accounts that we have found, and then add all the un-flagged ones.
   * This way we keep the overlapping accounts in the list, which should
   * provide a smooth user experience, for example when an item in the list
   * is selected it is not removed and re-added. */
  have_accounts = g_malloc(n_ids * sizeof(gboolean));
  for(i = 0; i < n_ids; ++i)
    have_accounts[i] = FALSE;
  n_lookup_ids = n_ids;

  has_row = gtk_tree_model_get_iter_first(model, &iter);
  while(has_row)
  {
    gtk_tree_model_get(
      model,
      &iter,
      INF_GTK_PERMISSIONS_DIALOG_COLUMN_ID,
      &account_id_ptr,
      -1
    );
    
    account_id = INF_ACL_ACCOUNT_POINTER_TO_ID(account_id_ptr);

    if(account_id == 0)
    {
      /* This is a pending account, keep it */
      has_row = gtk_tree_model_iter_next(model, &iter);
    }
    else
    {
      for(i = 0; i < n_ids; ++i)
        if(account_id == ids[i])
          break;

      if(i < n_ids)
      {
        have_accounts[i] = TRUE;
        has_row = gtk_tree_model_iter_next(model, &iter);
        --n_lookup_ids;
      }
      else
      {
        has_row = gtk_list_store_remove(priv->account_store, &iter);
      }
    }
  }

  if(n_lookup_ids > 0)
    lookup_ids = g_malloc(sizeof(InfAclAccountId) * n_lookup_ids);
  lookup_index = 0;

  default_account = inf_browser_get_acl_default_account(priv->browser);
  local_account = inf_browser_get_acl_local_account(priv->browser);

  for(i = 0; i < n_ids; ++i)
  {
    if(!have_accounts[i])
    {
      if(ids[i] == default_account->id)
        new_account_name = default_account->name;
      else if(local_account != NULL && ids[i] == local_account->id)
        new_account_name = local_account->name;
      else
        new_account_name = NULL;

      gtk_list_store_insert_with_values(
        priv->account_store,
        NULL,
        -1,
        INF_GTK_PERMISSIONS_DIALOG_COLUMN_ID,
        INF_ACL_ACCOUNT_ID_TO_POINTER(ids[i]),
        INF_GTK_PERMISSIONS_DIALOG_COLUMN_NAME,
        new_account_name,
        -1
      );

      /* If we don't know the account name, we need to look it up. */
      if(new_account_name == NULL && ids[i] != default_account->id)
        lookup_ids[lookup_index++] = ids[i];
    }
  }

  /* Lookup accounts with unknown name, if we can. */
  if(lookup_index > 0)
  {
    g_assert(lookup_index <= n_lookup_ids);

    inf_acl_mask_set1(&perms, INF_ACL_CAN_QUERY_ACCOUNT_LIST);

    inf_browser_check_acl(
      priv->browser,
      &priv->browser_iter,
      local_account ? local_account->id : 0,
      &perms,
      &perms
    );

    if(inf_acl_mask_has(&perms, INF_ACL_CAN_QUERY_ACCOUNT_LIST))
    {
      request = inf_browser_lookup_acl_accounts(
        priv->browser,
        lookup_ids,
        lookup_index,
        inf_gtk_permissions_dialog_lookup_acl_accounts_finished_cb,
        dialog
      );

      if(request != NULL)
      {
        g_object_ref(request);

        priv->lookup_acl_account_requests = g_slist_prepend(
          priv->lookup_acl_account_requests,
          request
        );
      }
    }
  }

  if(n_lookup_ids > 0)
    g_free(lookup_ids);
  g_free(have_accounts);
}

static void
inf_gtk_permissions_dialog_update_sheet(InfGtkPermissionsDialog* dialog)
{
  InfGtkPermissionsDialogPrivate* priv;
  GtkTreeSelection* selection;

  GtkTreeModel* model;
  GtkTreeIter iter;
  gpointer account_id_ptr;
  InfAclAccountId account_id;
  InfAclAccountId default_id;
  const InfAclSheetSet* sheet_set;
  const InfAclSheet* sheet;
  InfAclSheet default_sheet;
  InfAclMask show_mask;
  InfAclMask neg_mask;

  InfGtkPermissionsDialogPendingSheet* pending;
  InfBrowserIter test_iter;

  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);
  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->tree_view));

  inf_signal_handlers_block_by_func(
    G_OBJECT(priv->sheet_view),
    G_CALLBACK(inf_gtk_permissions_dialog_sheet_changed_cb),
    dialog
  );

  if(!gtk_tree_selection_get_selected(selection, &model, &iter))
  {
    inf_gtk_acl_sheet_view_set_sheet(
      INF_GTK_ACL_SHEET_VIEW(priv->sheet_view),
      NULL
    );

    account_id = 0;
  }
  else
  {
    gtk_tree_model_get(
      model,
      &iter,
      INF_GTK_PERMISSIONS_DIALOG_COLUMN_ID,
      &account_id_ptr,
      -1
    );

    account_id = INF_ACL_ACCOUNT_POINTER_TO_ID(account_id_ptr);

    if(account_id != 0)
    {
      sheet = NULL;
      sheet_set = inf_browser_get_acl(priv->browser, &priv->browser_iter);
      if(sheet_set != NULL)
      {
        sheet = inf_acl_sheet_set_find_const_sheet(sheet_set, account_id);
      }
    }
    else
    {
      /* It is (must be) a pending sheet */
      pending = inf_gtk_permissions_dialog_find_pending_sheet(dialog, &iter);
      g_assert(pending != NULL);

      sheet = &pending->sheet;
    }

    if(sheet != NULL)
    {
      inf_gtk_acl_sheet_view_set_sheet(
        INF_GTK_ACL_SHEET_VIEW(priv->sheet_view),
        sheet
      );
    }
    else
    {
      /* No sheet: set default sheet (all permissions masked out) */
      default_sheet.account = account_id;
      inf_acl_mask_clear(&default_sheet.mask);
      inf_acl_mask_clear(&default_sheet.perms);

      inf_gtk_acl_sheet_view_set_sheet(
        INF_GTK_ACL_SHEET_VIEW(priv->sheet_view),
        &default_sheet
      );
    }
  }

  /* Block default column if this is the default sheet of the root node */
  test_iter = priv->browser_iter;
  show_mask = INF_ACL_MASK_ALL;
  if(!inf_browser_get_parent(priv->browser, &test_iter))
  {
    /* This is the root node. Block default column if this is the default
     * account. */
    default_id = inf_acl_account_id_from_string("default");

    if(account_id == default_id)
    {
      inf_gtk_acl_sheet_view_set_show_default(
        INF_GTK_ACL_SHEET_VIEW(priv->sheet_view),
        FALSE
      );
    }
    else
    {
      inf_gtk_acl_sheet_view_set_show_default(
        INF_GTK_ACL_SHEET_VIEW(priv->sheet_view),
        TRUE
      );
    }
  }
  else
  {
    /* This is a leaf node. Show the default column, and block non-root
     * permissions. */
    inf_gtk_acl_sheet_view_set_show_default(
      INF_GTK_ACL_SHEET_VIEW(priv->sheet_view),
      TRUE
    );

    /* Remove root-only permissions */
    inf_acl_mask_neg(&INF_ACL_MASK_ROOT, &neg_mask);
    inf_acl_mask_and(&show_mask, &neg_mask, &show_mask);
  }

  /* If the node is a subdirectory, we don't hide the permissions that
   * only work for leaf nodes, since they are applied to all leaf nodes in
   * the subdirectory, unless overridden. */
  if(!inf_browser_is_subdirectory(priv->browser, &priv->browser_iter))
  {
    inf_acl_mask_neg(&INF_ACL_MASK_SUBDIRECTORY, &neg_mask);
    inf_acl_mask_and(&show_mask, &neg_mask, &show_mask);
  }

  inf_gtk_acl_sheet_view_set_permission_mask(
    INF_GTK_ACL_SHEET_VIEW(priv->sheet_view),
    &show_mask
  );

  inf_signal_handlers_unblock_by_func(
    G_OBJECT(priv->sheet_view),
    G_CALLBACK(inf_gtk_permissions_dialog_sheet_changed_cb),
    dialog
  );
}

static void
inf_gtk_permissions_dialog_node_removed_cb(InfBrowser* browser,
                                           const InfBrowserIter* iter,
                                           InfRequest* request,
                                           gpointer user_data)
{
  InfGtkPermissionsDialog* dialog;
  InfGtkPermissionsDialogPrivate* priv;

  dialog = INF_GTK_PERMISSIONS_DIALOG(user_data);
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  if(inf_browser_is_ancestor(browser, iter, &priv->browser_iter))
    inf_gtk_permissions_dialog_set_node(dialog, NULL, NULL);
}

static void
inf_gtk_permissions_dialog_acl_account_added_cb(InfBrowser* browser,
                                                const InfAclAccount* account,
                                                InfRequest* request,
                                                gpointer user_data)
{
  InfGtkPermissionsDialog* dialog;
  InfGtkPermissionsDialogPrivate* priv;

  dialog = INF_GTK_PERMISSIONS_DIALOG(user_data);
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  /* Add the new user to the user list. Note that this is also called when
   * the given user was updated, in which case we need to call row_changed,
   * since its name might have changed. */
  if(account->id != 0 && account->name != NULL && priv->accounts != NULL)
  {
    priv->accounts = g_realloc(
      priv->accounts,
      (priv->n_accounts + 1) * sizeof(InfAclAccount)
    );

    priv->accounts[priv->n_accounts].id = account->id;
    priv->accounts[priv->n_accounts].name = g_strdup(account->name);
    ++priv->n_accounts;

    /* Need to update because the add button sensitivity might change */
    inf_gtk_permissions_dialog_update(dialog, NULL);
  }
}

static void
inf_gtk_permissions_dialog_acl_account_removed_cb(InfBrowser* browser,
                                                  const InfAclAccount* account,
                                                  InfRequest* request,
                                                  gpointer user_data)
{
  InfGtkPermissionsDialog* dialog;
  InfGtkPermissionsDialogPrivate* priv;
  gboolean have_account;
  guint i;

  dialog = INF_GTK_PERMISSIONS_DIALOG(user_data);
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  g_assert(account->id != 0);

  if(priv->popup_menu != NULL && account->id == priv->popup_account)
    gtk_menu_popdown(priv->popup_menu);

  g_assert(priv->popup_menu == NULL);
  g_assert(priv->popup_account == 0);

  /* The account should not be in the list anymore, since all ACL sheets for
   * this account should have been removed first. */
  have_account =
    inf_gtk_permissions_dialog_find_account(dialog, account->id, NULL);
  g_assert(have_account == FALSE);

  /* Update account list */
  if(priv->accounts != NULL)
  {
    for(i = 0; i < priv->n_accounts; ++i)
    {
      if(priv->accounts[i].id == account->id)
      {
        priv->accounts[i] = priv->accounts[priv->n_accounts - 1];
        --priv->n_accounts;

        priv->accounts = g_realloc(
          priv->accounts,
          sizeof(InfAclAccount) * priv->n_accounts
        );

        /* Need to update because the add button sensitivity might change */
        inf_gtk_permissions_dialog_update(dialog, NULL);
        break;
      }
    }
  }
}

static void
inf_gtk_permissions_dialog_acl_changed_cb(InfBrowser* browser,
                                          const InfBrowserIter* iter,
                                          const InfAclSheetSet* sheet_set,
                                          InfRequest* request,
                                          gpointer user_data)
{
  InfGtkPermissionsDialog* dialog;
  InfGtkPermissionsDialogPrivate* priv;

  dialog = INF_GTK_PERMISSIONS_DIALOG(user_data);
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  /* If the node we are currently viewing had its ACL changed, show the
   * new ACL. */
  if(iter->node == priv->browser_iter.node)
    inf_gtk_permissions_dialog_update_sheet(dialog);

  /* If the node or one of its ancestors had their ACL changed, update the
   * view, since we might have been granted or revoked rights to see the
   * user list or the ACL for this node, or the non-default ACL sheets
   * have changed. */
  if(inf_browser_is_ancestor(browser, iter, &priv->browser_iter))
    inf_gtk_permissions_dialog_update(dialog, NULL);
}

static void
inf_gtk_permissions_dialog_renderer_editing_started_cb(GtkCellRenderer* r,
                                                       GtkCellEditable* edit,
                                                       const gchar* path,
                                                       gpointer user_data)
{
  /* In the editing_canceled signal handler, we need to know the path of the
   * cell that was edited. However, it does not provide a path parameter.
   * Therefore, store the path here in the cell renderer.
   *
   * Normally, we could simply query the selected row, however the row can
   * be deselected without the editing actually being cancelled, for example
   * when focusing another widget. */
  g_object_set_data_full(
    G_OBJECT(r),
    "inf-gtk-permissions-dialog-path",
    g_strdup(path),
    g_free
  );
}

static void
inf_gtk_permissions_dialog_renderer_editing_canceled_cb(GtkCellRenderer* r,
                                                        gpointer user_data)
{
  InfGtkPermissionsDialog* dialog;
  InfGtkPermissionsDialogPrivate* priv;
  gchar* path_str;
  GtkTreePath* path;
  GtkTreeIter iter;
  gboolean has_selected;
  InfGtkPermissionsDialogPendingSheet* pending;

  dialog = INF_GTK_PERMISSIONS_DIALOG(user_data);
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  /* Remove the editing facility of the cell renderer */
  g_object_set(
    G_OBJECT(priv->renderer),
    "model", NULL,
    "editable", FALSE,
    NULL
  );

  path_str = g_object_steal_data(
    G_OBJECT(r),
    "inf-gtk-permissions-dialog-path"
  );

  /* Remove the pending sheet */
  path = gtk_tree_path_new_from_string(path_str);
  g_free(path_str);

  has_selected = gtk_tree_model_get_iter(
    GTK_TREE_MODEL(priv->account_store),
    &iter,
    path
  );

  g_assert(has_selected == TRUE);

  pending = inf_gtk_permissions_dialog_find_pending_sheet(dialog, &iter);
  g_assert(pending != NULL);

  inf_gtk_permissions_dialog_remove_pending_sheet(dialog, pending);
}

static void
inf_gtk_permissions_dialog_renderer_changed_cb(GtkCellRendererCombo* combo,
                                               const gchar* path_str,
                                               GtkTreeIter* combo_iter,
                                               gpointer user_data)
{
  InfGtkPermissionsDialog* dialog;
  InfGtkPermissionsDialogPrivate* priv;
  GtkTreePath* path;
  GtkTreeIter view_iter;
  gboolean has_path;
  InfGtkPermissionsDialogPendingSheet* pending;
  GtkTreeModel* model;
  gpointer id_ptr;

  dialog = INF_GTK_PERMISSIONS_DIALOG(user_data);
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  path = gtk_tree_path_new_from_string(path_str);

  has_path = gtk_tree_model_get_iter(
    GTK_TREE_MODEL(priv->account_store),
    &view_iter,
    path
  );

  g_assert(has_path);
  gtk_tree_path_free(path);

  pending = inf_gtk_permissions_dialog_find_pending_sheet(dialog, &view_iter);
  g_assert(pending != NULL);

  g_object_get(G_OBJECT(combo), "model", &model, NULL);
  g_assert(model != NULL);

  gtk_tree_model_get(
    model,
    combo_iter,
    INF_GTK_PERMISSIONS_DIALOG_COLUMN_ID,
    &id_ptr,
    -1
  );

  pending->last_combo_changed_id = INF_ACL_ACCOUNT_POINTER_TO_ID(id_ptr);
  g_object_unref(model);
}

static void
inf_gtk_permissions_dialog_renderer_edited_cb(GtkCellRendererCombo* renderer,
                                              const gchar* path_str,
                                              const gchar* text,
                                              gpointer user_data)
{
  InfGtkPermissionsDialog* dialog;
  InfGtkPermissionsDialogPrivate* priv;
  gchar* path_str_obj;
  GtkTreePath* path;
  gboolean has_path;
  GtkTreeIter view_iter;
  InfGtkPermissionsDialogPendingSheet* pending;
  GtkTreeModel* model;
  InfRequest* request;

  dialog = INF_GTK_PERMISSIONS_DIALOG(user_data);
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  path_str_obj = g_object_steal_data(
    G_OBJECT(renderer),
    "inf-gtk-permissions-dialog-path"
  );

  g_assert(strcmp(path_str_obj, path_str) == 0);
  g_free(path_str_obj);

  path = gtk_tree_path_new_from_string(path_str);

  has_path = gtk_tree_model_get_iter(
    GTK_TREE_MODEL(priv->account_store),
    &view_iter,
    path
  );

  g_assert(has_path);
  gtk_tree_path_free(path);

  pending = inf_gtk_permissions_dialog_find_pending_sheet(dialog, &view_iter);
  g_assert(pending != NULL);

  /* If we selected a user with the combo box, find the corresponding ID */
  g_object_get(G_OBJECT(renderer), "model", &model, NULL);

  /* Remove the editing facility of the cell renderer */
  g_object_set(
    G_OBJECT(priv->renderer),
    "model", NULL,
    "editable", FALSE,
    NULL
  );

  g_assert(model != NULL);

  if(gtk_tree_model_iter_n_children(model, NULL) > 0)
  {
    g_assert(pending->last_combo_changed_id != 0);

    inf_gtk_permissions_dialog_realize_pending_sheet(
      dialog,
      pending,
      pending->last_combo_changed_id,
      text
    );
  }
  else
  {
    /* While we don't have an ID, set a name to show in the list */
    gtk_list_store_set(
      priv->account_store,
      &view_iter,
      INF_GTK_PERMISSIONS_DIALOG_COLUMN_NAME,
      text,
      -1
    );

    /* Make a reverse lookup for the ID */
    request = inf_browser_lookup_acl_account_by_name(
      priv->browser,
      text,
      inf_gtk_permissions_dialog_lookup_by_name_finished_cb,
      pending
    );

    if(request != NULL)
    {
      pending->lookup_request = request;
      g_object_ref(request);
    }
  }

  g_object_unref(model);
}

static void
inf_gtk_permissions_dialog_add_clicked_cb(GtkButton* button,
                                          gpointer user_data)
{
  InfGtkPermissionsDialog* dialog;
  InfGtkPermissionsDialogPrivate* priv;
  GtkTreeIter new_iter;
  GtkTreePath* new_path;
  InfGtkPermissionsDialogPendingSheet* pending;

  GtkListStore* store;
  const InfAclSheetSet* sheet_set;
  const InfAclSheet* sheet;
  guint i;

  dialog = INF_GTK_PERMISSIONS_DIALOG(user_data);
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  /* If we are currently naming another pending sheet, then stop editing
   * that. */
  gtk_cell_renderer_stop_editing(priv->renderer, TRUE);

  /* Insert a new entry without values */
  gtk_list_store_insert_with_values(
    priv->account_store,
    &new_iter,
    -1,
    INF_GTK_PERMISSIONS_DIALOG_COLUMN_ID,
    0,
    INF_GTK_PERMISSIONS_DIALOG_COLUMN_NAME,
    NULL,
    -1
  );

  new_path = gtk_tree_model_get_path(
    GTK_TREE_MODEL(priv->account_store),
    &new_iter
  );

  pending = g_slice_new(InfGtkPermissionsDialogPendingSheet);

  pending->dialog = dialog;
  pending->row = gtk_tree_row_reference_new(
    GTK_TREE_MODEL(priv->account_store),
    new_path
  );

  pending->sheet.account = 0;
  inf_acl_mask_clear(&pending->sheet.mask);
  inf_acl_mask_clear(&pending->sheet.perms);

  pending->last_combo_changed_id = 0;
  pending->lookup_request = NULL;

  priv->pending_sheets = g_slist_prepend(priv->pending_sheets, pending);

  /* Prepare the cell renderer editing */
  store = gtk_list_store_new(2, G_TYPE_POINTER, G_TYPE_STRING);
  g_object_set(
    G_OBJECT(priv->renderer),
    "model", store,
    "editable", TRUE,
    "text-column", INF_GTK_PERMISSIONS_DIALOG_COLUMN_NAME,
    NULL
  );

  /* TODO: If there are more than, say 25 accounts (should be configurable),
   * then use a free edit instead, and add a GtkEntryCompletion with all the
   * accounts, and perform a lookup-by-name in our cached list first. */
  if(priv->accounts != NULL)
  {
    /* Create a list of possible accounts */
    g_object_set(G_OBJECT(priv->renderer), "has-entry", FALSE, NULL);
    sheet_set = inf_browser_get_acl(priv->browser, &priv->browser_iter);

    for(i = 0; i < priv->n_accounts; ++i)
    {
      /* Skip the default user */
      if(priv->accounts[i].name != NULL)
      {
        sheet = inf_acl_sheet_set_find_const_sheet(
          sheet_set,
          priv->accounts[i].id
        );

        if(sheet == NULL)
        {
          gtk_list_store_insert_with_values(
            store,
            NULL,
            -1,
            INF_GTK_PERMISSIONS_DIALOG_COLUMN_ID,
            priv->accounts[i].id,
            INF_GTK_PERMISSIONS_DIALOG_COLUMN_NAME,
            priv->accounts[i].name,
            -1
          );
        }
      }
    }

    /* Otherwise the add button would not be set to sensitive */
    g_assert(gtk_tree_model_iter_n_children(GTK_TREE_MODEL(store), NULL) > 0);
  }
  else
  {
    /* Free editing. Note that we still need to set an (empty) model,
     * otherwise the editing widget cannot be set by GtkCellRendererCombo.
     * We could use GtkCellRendererText instead, but then we would need to
     * juggle around with two cell renderers. */
    g_object_set(G_OBJECT(priv->renderer), "has-entry", TRUE, NULL);
  }

  /* Sort the name list */
  gtk_tree_sortable_set_sort_column_id(
    GTK_TREE_SORTABLE(store),
    INF_GTK_PERMISSIONS_DIALOG_COLUMN_NAME,
    GTK_SORT_ASCENDING
  );

  gtk_tree_sortable_set_sort_func(
    GTK_TREE_SORTABLE(store),
    INF_GTK_PERMISSIONS_DIALOG_COLUMN_NAME,
    inf_gtk_permissions_dialog_account_sort_func,
    dialog,
    NULL
  );

  g_object_unref(store);

  /* Before we present the editing to the user, just select the row. This
   * runs our signal handler for the selection change, which might remove a
   * row from the list, which would close down the editing again. */
  gtk_tree_selection_select_path(
    gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->tree_view)),
    new_path
  );

  /* Obtain the path from the row reference, in case the list store was
   * altered by the selection change. */
  gtk_tree_path_free(new_path);
  new_path = gtk_tree_row_reference_get_path(pending->row);

  gtk_tree_view_set_cursor(
    GTK_TREE_VIEW(priv->tree_view),
    new_path,
    gtk_tree_view_get_column(GTK_TREE_VIEW(priv->tree_view), 0),
    TRUE
  );

  gtk_tree_path_free(new_path);
}

static void
inf_gtk_permissions_dialog_remove_clicked_cb(GtkButton* button,
                                             gpointer user_data)
{
  InfGtkPermissionsDialog* dialog;
  InfGtkPermissionsDialogPrivate* priv;
  GtkTreeSelection* selection;
  GtkTreeIter selected_iter;
  gpointer selected_id_ptr;
  InfAclAccountId selected_id;
  InfGtkPermissionsDialogPendingSheet* pending;
  const InfAclSheetSet* sheet_set;
  InfAclSheet set_sheet;
  InfAclSheetSet set_sheet_set;
  guint i;
  InfRequest* request;
  GtkTreeIter move_iter;
  gboolean could_move;

  dialog = INF_GTK_PERMISSIONS_DIALOG(user_data);
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->tree_view));
  if(!gtk_tree_selection_get_selected(selection, NULL, &selected_iter))
    return;

  gtk_tree_model_get(
    GTK_TREE_MODEL(priv->account_store),
    &selected_iter,
    INF_GTK_PERMISSIONS_DIALOG_COLUMN_ID,
    &selected_id_ptr,
    -1
  );

  selected_id = INF_ACL_ACCOUNT_POINTER_TO_ID(selected_id_ptr);
  sheet_set = inf_browser_get_acl(priv->browser, &priv->browser_iter);

  if(selected_id == 0)
  {
    /* This is a pending sheet */
    pending = inf_gtk_permissions_dialog_find_pending_sheet(
      dialog,
      &selected_iter
    );

    g_assert(pending != NULL);
    inf_gtk_permissions_dialog_remove_pending_sheet(dialog, pending);
  }
  else if(sheet_set == NULL || sheet_set->n_sheets == 0)
  {
    /* It can be that the entry is only kept in the list because it is still
     * selected, even though the sheet set is empty. In that case, simply
     * remove the entry from the list. */
    gtk_list_store_remove(priv->account_store, &selected_iter);
  }
  else
  {
    /* Mask-out all entries */
    set_sheet.account = selected_id;
    inf_acl_mask_clear(&set_sheet.mask);
    inf_acl_mask_clear(&set_sheet.perms);
    
    set_sheet_set.n_sheets = 1;
    set_sheet_set.own_sheets = NULL;
    set_sheet_set.sheets = &set_sheet;

    request = inf_browser_set_acl(
      priv->browser,
      &priv->browser_iter,
      &set_sheet_set,
      inf_gtk_permissions_dialog_set_acl_finished_cb,
      dialog
    );

    if(request != NULL)
    {
      priv->set_acl_requests =
        g_slist_prepend(priv->set_acl_requests, request);
      g_object_ref(request);
    }

    /* At this point, the ACL might either have been changed instantly or not.
     * In both cases, the selected item should not have been removed from the
     * list store, and our iterator is still valid.
     *
     * In any case, we need to change the selection to a different item,
     * otherwise, once the request finishes, the currently selected entry
     * would not be removed. Note that there must be at least one other item,
     * because the default entry is always shown and the default entry cannot
     * be removed. */
    move_iter = selected_iter;
    could_move = gtk_tree_model_iter_next(
      GTK_TREE_MODEL(priv->account_store),
      &move_iter
    );

    if(!could_move)
    {
      move_iter = selected_iter;
      could_move = gtk_tree_model_iter_previous(
        GTK_TREE_MODEL(priv->account_store),
        &move_iter
      );
    }

    g_assert(could_move);

    gtk_tree_selection_select_iter(
      GTK_TREE_SELECTION(selection),
      &move_iter
    );
  }
}

static void
inf_gtk_permissions_dialog_remove_acl_account_finished_cb(
  InfRequest* request,
  const InfRequestResult* result,
  const GError* error,
  gpointer user_data);

static void
inf_gtk_permissions_dialog_remove_remove_acl_account_request(
  InfGtkPermissionsDialog* dialog,
  InfRequest* request)
{
  InfGtkPermissionsDialogPrivate* priv;
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  g_assert(g_slist_find(priv->remove_acl_account_requests, request) != NULL);

  g_signal_handlers_disconnect_by_func(
    request,
    G_CALLBACK(inf_gtk_permissions_dialog_remove_acl_account_finished_cb),
    dialog
  );

  priv->remove_acl_account_requests =
    g_slist_remove(priv->remove_acl_account_requests, request);

  g_object_unref(request);
}

static void
inf_gtk_permissions_dialog_remove_acl_account_finished_cb(
  InfRequest* request,
  const InfRequestResult* result,
  const GError* error,
  gpointer user_data)
{
  InfGtkPermissionsDialog* dialog;

  dialog = INF_GTK_PERMISSIONS_DIALOG(user_data);

  /* TODO: Should we show this error to the user inside the dialog, or
   * with a message dialog? */
  if(error != NULL)
  {
    g_warning("Failed to remove account: %s\n", error->message);
  }

  inf_gtk_permissions_dialog_remove_remove_acl_account_request(
    dialog,
    request
  );
}

static void
inf_gtk_permissions_dialog_popup_delete_account_cb(GtkMenuItem* item,
                                                   gpointer user_data)
{
  InfGtkPermissionsDialog* dialog;
  InfGtkPermissionsDialogPrivate* priv;
  InfRequest* request;

  dialog = INF_GTK_PERMISSIONS_DIALOG(user_data);
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  g_assert(priv->popup_menu != NULL);
  g_assert(priv->popup_account != 0);

  request = inf_browser_remove_acl_account(
    priv->browser,
    priv->popup_account,
    inf_gtk_permissions_dialog_remove_acl_account_finished_cb,
    dialog
  );

  if(request != NULL)
  {
    g_object_ref(request);

    priv->remove_acl_account_requests = g_slist_prepend(
      priv->remove_acl_account_requests,
      request
    );
  }
}

/* TODO: The popup handling code should be shared between this class and
 * InfGtkBrowserView. */

static gboolean
inf_gtk_permissions_dialog_populate_popup(InfGtkPermissionsDialog* dialog,
                                          GtkMenu* menu)
{
  InfGtkPermissionsDialogPrivate* priv;
  GtkWidget* item;

  InfBrowserIter root;
  InfAclMask perms;

  guint n_accounts;
  InfAclAccountId default_id;
  const InfAclAccount* local_account;
  const InfAclAccount** accounts;
  gpointer account_id_ptr;
  InfAclAccountId account_id;
  GtkTreeSelection* selection;
  GtkTreeIter iter;

  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);
  g_assert(priv->popup_menu == NULL);

  /* Make sure that we have permissions to remove accounts */
  inf_browser_get_root(priv->browser, &root);
  inf_acl_mask_set1(&perms, INF_ACL_CAN_REMOVE_ACCOUNT);
  local_account = inf_browser_get_acl_local_account(priv->browser);

  inf_browser_check_acl(
    priv->browser,
    &root,
    local_account ? local_account->id : 0,
    &perms,
    &perms
  );

  if(!inf_acl_mask_has(&perms, INF_ACL_CAN_REMOVE_ACCOUNT))
    return FALSE;

  /* Make sure the selected account is not the default account or a
   * pending account. */
  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->tree_view));
  if(!gtk_tree_selection_get_selected(selection, NULL, &iter))
    return FALSE;

  gtk_tree_model_get(
    GTK_TREE_MODEL(priv->account_store),
    &iter,
    INF_GTK_PERMISSIONS_DIALOG_COLUMN_ID,
    &account_id_ptr,
    -1
  );

  account_id = INF_ACL_ACCOUNT_POINTER_TO_ID(account_id_ptr);

  default_id = inf_acl_account_id_from_string("default");
  if(account_id == 0 || account_id == default_id)
    return FALSE;
 
  /* Then, show a menu item to remove an account. */
  item = gtk_image_menu_item_new_with_mnemonic(_("_Delete Account"));

  gtk_image_menu_item_set_image(
    GTK_IMAGE_MENU_ITEM(item),
    gtk_image_new_from_stock(GTK_STOCK_DELETE, GTK_ICON_SIZE_MENU)
  );

  g_signal_connect(
    G_OBJECT(item),
    "activate",
    G_CALLBACK(inf_gtk_permissions_dialog_popup_delete_account_cb),
    dialog
  );

  gtk_widget_show(item);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

  /* TODO: These two items need to be added for popdown and stuff,
   * popup_menu needs to be maintained and tracked. */
  priv->popup_menu = menu;
  priv->popup_account = INF_ACL_ACCOUNT_POINTER_TO_ID(account_id);

  return TRUE;
}

static void
inf_gtk_permissions_dialog_popup_menu_detach_func(GtkWidget* attach_widget,
                                                  GtkMenu* menu)
{
}

static void
inf_gtk_permissions_dialog_popup_menu_position_func(GtkMenu* menu,
                                                    gint* x,
                                                    gint* y,
                                                    gboolean* push_in,
                                                    gpointer user_data)
{
  InfGtkPermissionsDialog* dialog;
  InfGtkPermissionsDialogPrivate* priv;
  GdkWindow* bin_window;
  GdkScreen* screen;
  GtkRequisition menu_req;
  GdkRectangle monitor;
  gint monitor_num;
  gint orig_x;
  gint orig_y;
  gint height;

  GtkTreeSelection* selection;
  GtkTreeModel* model;
  GtkTreeIter selected_iter;
  GtkTreePath* selected_path;
  GdkRectangle cell_area;

  dialog = INF_GTK_PERMISSIONS_DIALOG(user_data);
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  /* Place menu below currently selected row */

  bin_window = gtk_tree_view_get_bin_window(GTK_TREE_VIEW(priv->tree_view));
  gdk_window_get_origin(bin_window, &orig_x, &orig_y);

  screen = gtk_widget_get_screen(GTK_WIDGET(priv->tree_view));
  monitor_num = gdk_screen_get_monitor_at_window(screen, bin_window);
  if(monitor_num < 0) monitor_num = 0;
  gtk_menu_set_monitor(menu, monitor_num);

  gdk_screen_get_monitor_geometry(screen, monitor_num, &monitor);
  gtk_widget_size_request(GTK_WIDGET(menu), &menu_req);

#if GTK_CHECK_VERSION(2, 91, 0)
  height = gdk_window_get_height(bin_window);
#else
  gdk_drawable_get_size(GDK_DRAWABLE(bin_window), NULL, &height);
#endif

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->tree_view));
  gtk_tree_selection_get_selected(selection, &model, &selected_iter);
  selected_path = gtk_tree_model_get_path(model, &selected_iter);
  gtk_tree_view_get_cell_area(
    GTK_TREE_VIEW(priv->tree_view),
    selected_path,
    gtk_tree_view_get_column(GTK_TREE_VIEW(priv->tree_view), 0),
    &cell_area
  );
  gtk_tree_path_free(selected_path);

  g_assert(cell_area.height > 0);

  if(gtk_widget_get_direction(GTK_WIDGET(priv->tree_view)) ==
     GTK_TEXT_DIR_LTR)
  {
    *x = orig_x + cell_area.x + cell_area.width - menu_req.width;
  }
  else
  {
    *x = orig_x + cell_area.x;
  }

  *y = orig_y + cell_area.y + cell_area.height;

  /* Keep within widget */
  if(*y < orig_y)
    *y = orig_y;
  if(*y > orig_y + height)
    *y = orig_y + height;

  /* Keep on screen */
  if(*y + menu_req.height > monitor.y + monitor.height)
    *y = monitor.y + monitor.height - menu_req.height;
  if(*y < monitor.y)
    *y = monitor.y;

  *push_in = FALSE;
}

static void
inf_gtk_permissions_dialog_popup_selection_done_cb(GtkMenu* menu,
                                                   gpointer user_data)
{
  InfGtkPermissionsDialog* dialog;
  InfGtkPermissionsDialogPrivate* priv;

  dialog = INF_GTK_PERMISSIONS_DIALOG(user_data);
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  g_assert(priv->popup_menu != NULL);
  
  priv->popup_menu = NULL;
  priv->popup_account = 0;
}

static gboolean
inf_gtk_permissions_dialog_show_popup(InfGtkPermissionsDialog* dialog,
                                      guint button, /* 0 if triggered by keyboard */
                                      guint32 time)
{
  InfGtkPermissionsDialogPrivate* priv;
  GtkWidget* menu;
  gboolean result;

  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  menu = gtk_menu_new();

  g_signal_connect(
    G_OBJECT(menu),
    "selection-done",
    G_CALLBACK(inf_gtk_permissions_dialog_popup_selection_done_cb),
    dialog
  );

  gtk_menu_attach_to_widget(
    GTK_MENU(menu),
    GTK_WIDGET(priv->tree_view),
    inf_gtk_permissions_dialog_popup_menu_detach_func
  );

  if(inf_gtk_permissions_dialog_populate_popup(dialog, GTK_MENU(menu)))
  {
    result = TRUE;

    if(button)
    {
      gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, button, time);
    }
    else
    {
      gtk_menu_popup(
        GTK_MENU(menu),
        NULL,
        NULL,
        inf_gtk_permissions_dialog_popup_menu_position_func,
        priv->tree_view,
        button,
        time
      );

      gtk_menu_shell_select_first(GTK_MENU_SHELL(menu), FALSE);
    }
  }
  else
  {
    result = FALSE;
    gtk_widget_destroy(menu);
  }

  return result;
}

static gboolean
inf_gtk_permissions_dialog_button_press_event_cb(GtkWidget* treeview,
                                                 GdkEventButton* event,
                                                 gpointer user_data)
{
  InfGtkPermissionsDialog* dialog;
  GtkTreePath* path;
  gboolean has_path;

  dialog = INF_GTK_PERMISSIONS_DIALOG(user_data);

  if(event->button == 3 &&
     event->window == gtk_tree_view_get_bin_window(GTK_TREE_VIEW(treeview)))
  {
    has_path = gtk_tree_view_get_path_at_pos(
      GTK_TREE_VIEW(treeview),
      event->x,
      event->y,
      &path,
      NULL,
      NULL,
      NULL
    );

    if(has_path)
    {
      gtk_tree_selection_select_path(
        gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview)),
        path
      );

      gtk_tree_path_free(path);

      return inf_gtk_permissions_dialog_show_popup(
        dialog,
        event->button,
        event->time
      );
    }
  }

  return FALSE;
}

static gboolean
inf_gtk_permissions_dialog_key_press_event_cb(GtkWidget* treeview,
                                              GdkEventKey* event,
                                              gpointer user_data)
{
  InfGtkPermissionsDialog* dialog;
  GtkTreeSelection* selection;
  GtkTreeIter iter;

  dialog = INF_GTK_PERMISSIONS_DIALOG(user_data);

#if GTK_CHECK_VERSION(2,90,7)
  if(event->keyval == GDK_KEY_Menu)
#else
  if(event->keyval == GDK_Menu)
#endif
  {
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    if(gtk_tree_selection_get_selected(selection, NULL, &iter))
    {
      return inf_gtk_permissions_dialog_show_popup(dialog, 0, event->time);
    }
  }

  return FALSE;
}

static void
inf_gtk_permissions_dialog_name_data_func(GtkTreeViewColumn* column,
                                          GtkCellRenderer* cell,
                                          GtkTreeModel* model,
                                          GtkTreeIter* iter,
                                          gpointer user_data)
{
  gpointer account_id_ptr;
  InfAclAccountId account_id;
  const gchar* account_id_str;
  InfAclAccountId default_id;
  gchar* account_name;
  gchar* str;

  gtk_tree_model_get(
    model,
    iter,
    INF_GTK_PERMISSIONS_DIALOG_COLUMN_ID, &account_id_ptr,
    INF_GTK_PERMISSIONS_DIALOG_COLUMN_NAME, &account_name,
    -1
  );

  account_id = INF_ACL_ACCOUNT_POINTER_TO_ID(account_id_ptr);
  account_id_str = inf_acl_account_id_to_string(account_id);

  if(account_name != NULL)
  {
    g_object_set(G_OBJECT(cell), "text", account_name, NULL);
  }
  else if(account_id_str != NULL)
  {
    str = g_strdup_printf("<%s>", account_id_str);
    g_object_set(G_OBJECT(cell), "text", str, NULL);
    g_free(str);
  }
  else
  {
    g_object_set(G_OBJECT(cell), "text", "", NULL);
  }

  /* Set red foreground color if either ID or name is missing, meaning we are
   * still looking them up, or that some information is denied from us by
   * the server. */
  default_id = inf_acl_account_id_from_string("default");
  if( (account_id == 0 || account_name == NULL) && account_id != default_id)
    g_object_set(G_OBJECT(cell), "foreground", "red", NULL);
  else
    g_object_set(G_OBJECT(cell), "foreground-set", FALSE, NULL);

  g_free(account_name);
}

static void
inf_gtk_permissions_dialog_query_acl_account_list_finished_cb(
  InfRequest* request,
  const InfRequestResult* res,
  const GError* error,
  gpointer user_data)
{
  InfGtkPermissionsDialog* dialog;
  InfGtkPermissionsDialogPrivate* priv;
  const InfAclAccount* accounts;
  guint n_accounts;
  guint i;

  dialog = INF_GTK_PERMISSIONS_DIALOG(user_data);
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  priv->query_acl_account_list_request = NULL;
  priv->account_list_queried = TRUE;

  if(error != NULL)
  {
    if(error->domain != inf_directory_error_quark() ||
       error->code != INF_DIRECTORY_ERROR_OPERATION_UNSUPPORTED)
    {
      g_warning("Error while querying account list: %s\n", error->message);
    }
  }
  else
  {
    inf_request_result_get_query_acl_account_list(
      res,
      NULL,
      &accounts,
      &n_accounts,
      NULL
    );

    for(i = 0; i < priv->n_accounts; ++i)
      g_free(priv->accounts[i].name);

    priv->accounts = g_realloc(
      priv->accounts,
      n_accounts * sizeof(InfAclAccount)
    );

    for(i = 0; i < n_accounts; ++i)
    {
      priv->accounts[i].id = accounts[i].id;
      priv->accounts[i].name = g_strdup(accounts[i].name);
    }

    priv->n_accounts = n_accounts;
  }
}

static void
inf_gtk_permissions_dialog_query_acl_finished_cb(InfRequest* request,
                                                 const InfRequestResult* res,
                                                 const GError* error,
                                                 gpointer user_data)
{
  InfGtkPermissionsDialog* dialog;
  InfGtkPermissionsDialogPrivate* priv;

  dialog = INF_GTK_PERMISSIONS_DIALOG(user_data);
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  priv->query_acl_request = NULL;
  inf_gtk_permissions_dialog_update(dialog, error);
}

static void
inf_gtk_permissions_dialog_update(InfGtkPermissionsDialog* dialog,
                                  const GError* error)
{
  InfGtkPermissionsDialogPrivate* priv;
  gchar* path;
  gchar* title;

  InfAclMask perms;

  GArray* accounts;
  const InfAclAccount* local_account;
  const InfAclSheetSet* sheet_set;
  gboolean has_default;
  InfAclAccountId default_id;
  guint i;

  GtkTreeSelection* selection;
  GtkTreeIter selected_iter;
  gpointer selected_id_ptr;
  GtkTreePath* selected_path;
  InfAclAccountId selected_id;
  gboolean has_selected;
  GSList* item;
  InfGtkPermissionsDialogPendingSheet* pending;
  GtkTreePath* pending_path;

  const gchar* query_acl_str;
  const gchar* set_acl_str;
  gchar* error_str;
  gchar* str;

  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  /* Reset all widgets if no node is set */
  if(priv->browser == NULL)
  {
    gtk_list_store_clear(priv->account_store);
    gtk_label_set_text(GTK_LABEL(priv->status_text), _("No node selected"));

    gtk_image_set_from_stock(
      GTK_IMAGE(priv->status_image),
      GTK_STOCK_DISCONNECT,
      GTK_ICON_SIZE_BUTTON
    );

    return;
  }

  /* Set the dialog title */
  path = inf_browser_get_path(priv->browser, &priv->browser_iter);
  title = g_strdup_printf(_("Permissions for %s"), path);
  gtk_window_set_title(GTK_WINDOW(dialog), title);
  g_free(path);
  g_free(title);

  inf_acl_mask_set1(&perms, INF_ACL_CAN_QUERY_ACCOUNT_LIST);
  inf_acl_mask_or1(&perms, INF_ACL_CAN_QUERY_ACL);
  inf_acl_mask_or1(&perms, INF_ACL_CAN_SET_ACL);
  local_account = inf_browser_get_acl_local_account(priv->browser);

  inf_browser_check_acl(
    priv->browser,
    &priv->browser_iter,
    local_account ? local_account->id : 0,
    &perms,
    &perms
  );

  /* Request account list */
  if(priv->query_acl_account_list_request == NULL &&
     priv->account_list_queried == FALSE)
  {
    if(inf_acl_mask_has(&perms, INF_ACL_CAN_QUERY_ACCOUNT_LIST) &&
       inf_acl_mask_has(&perms, INF_ACL_CAN_SET_ACL))
    {
      priv->query_acl_account_list_request = inf_browser_get_pending_request(
        priv->browser,
        NULL,
        "query-acl-account-list"
      );

      if(priv->query_acl_account_list_request == NULL)
      {
        priv->query_acl_account_list_request =
          inf_browser_query_acl_account_list(
            priv->browser,
            inf_gtk_permissions_dialog_query_acl_account_list_finished_cb,
            dialog
          );
      }
      else
      {
        g_signal_connect(
          G_OBJECT(priv->query_acl_account_list_request),
          "finished",
          G_CALLBACK(
            inf_gtk_permissions_dialog_query_acl_account_list_finished_cb
          ),
          dialog
        );
      }
    }
  }

  /* Request ACL */
  if(!inf_browser_has_acl(priv->browser, &priv->browser_iter, 0))
  {
    if(inf_acl_mask_has(&perms, INF_ACL_CAN_QUERY_ACL) &&
       priv->query_acl_request == NULL && error == NULL)
    {
      priv->query_acl_request = inf_browser_get_pending_request(
        priv->browser,
        &priv->browser_iter,
        "query-acl"
      );

      if(priv->query_acl_request == NULL)
      {
        priv->query_acl_request = inf_browser_query_acl(
          priv->browser,
          &priv->browser_iter,
          inf_gtk_permissions_dialog_query_acl_finished_cb,
          dialog
        );
      }
      else
      {
        g_signal_connect(
          G_OBJECT(priv->query_acl_request),
          "finished",
          G_CALLBACK(inf_gtk_permissions_dialog_query_acl_finished_cb),
          dialog
        );
      }
    }
  }

  /* Fill the account list widget. If an account is currently selected,
   * keep it there until the selection changes, even if does not show
   * up in the ACL sheet (anymore). */
  accounts = g_array_new(FALSE, FALSE, sizeof(InfAclAccountId));
  sheet_set = inf_browser_get_acl(priv->browser, &priv->browser_iter);
  default_id = inf_acl_account_id_from_string("default");

  selected_id = 0;
  selected_path = NULL;
  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->tree_view));
  if(gtk_tree_selection_get_selected(selection, NULL, &selected_iter))
  {
    gtk_tree_model_get(
      GTK_TREE_MODEL(priv->account_store),
      &selected_iter,
      INF_GTK_PERMISSIONS_DIALOG_COLUMN_ID,
      &selected_id_ptr,
      -1
    );

    selected_id = INF_ACL_ACCOUNT_POINTER_TO_ID(selected_id_ptr);

    selected_path = gtk_tree_model_get_path(
      GTK_TREE_MODEL(priv->account_store),
      &selected_iter
    );
  }

  has_default = FALSE;
  has_selected = FALSE;
  if(sheet_set != NULL)
  {
    for(i = 0; i < sheet_set->n_sheets; ++i)
    {
      g_array_append_val(accounts, sheet_set->sheets[i].account);
      if(sheet_set->sheets[i].account == default_id)
        has_default = TRUE;
      if(sheet_set->sheets[i].account == selected_id)
        has_selected = TRUE;
    }
  }

  if(has_default == FALSE)
    g_array_append_val(accounts, default_id);
  if(selected_id != 0 && selected_id != default_id && has_selected == FALSE)
    g_array_append_val(accounts, selected_id);

  inf_gtk_permissions_dialog_fill_account_list(
    dialog,
    (InfAclAccountId*)accounts->data,
    accounts->len
  );

  /* Remove all non-selected pending sheets that have an empty mask */
  for(item = priv->pending_sheets; item != NULL; item = item->next)
  {
    pending = (InfGtkPermissionsDialogPendingSheet*)item->data;
    if(inf_acl_mask_empty(&pending->sheet.mask))
    {
      pending_path = gtk_tree_row_reference_get_path(pending->row);
      g_assert(pending_path != NULL);

      if(selected_path == NULL ||
         gtk_tree_path_compare(pending_path, selected_path) != 0)
      {
        gtk_tree_path_free(pending_path);
        inf_gtk_permissions_dialog_remove_pending_sheet(dialog, pending);
        break;
      }

      gtk_tree_path_free(pending_path);
    }
  }
  
  if(selected_path != NULL)
    gtk_tree_path_free(selected_path);

  /* Set editability of the sheet view */
  if(!inf_acl_mask_has(&perms, INF_ACL_CAN_SET_ACL) ||
     !inf_browser_has_acl(priv->browser, &priv->browser_iter, 0))
  {
    inf_gtk_acl_sheet_view_set_editable(
      INF_GTK_ACL_SHEET_VIEW(priv->sheet_view),
      FALSE
    );

    gtk_widget_set_sensitive(priv->add_button, FALSE);
    gtk_widget_set_sensitive(priv->remove_button, FALSE);

    gtk_image_set_from_stock(
      GTK_IMAGE(priv->status_image),
      GTK_STOCK_NO,
      GTK_ICON_SIZE_BUTTON
    );

    set_acl_str = _("Permission is <b>not granted</b> to modify the permission list. It is read-only.");
  }
  else
  {
    inf_gtk_acl_sheet_view_set_editable(
      INF_GTK_ACL_SHEET_VIEW(priv->sheet_view),
      TRUE
    );

    /* Set add button to sensitive if:
     * we don't have user list OR
     * we have user list and not all users exist already in the sheet */
    if(priv->accounts == NULL || accounts->len < priv->n_accounts)
      gtk_widget_set_sensitive(priv->add_button, TRUE);
    else
      gtk_widget_set_sensitive(priv->add_button, FALSE);

    /* Set remove button to sensitive if something other than the
     * default account is selected. */
    if(selected_id != default_id)
      gtk_widget_set_sensitive(priv->remove_button, TRUE);
    else
      gtk_widget_set_sensitive(priv->remove_button, FALSE);

    gtk_image_set_from_stock(
      GTK_IMAGE(priv->status_image),
      GTK_STOCK_YES,
      GTK_ICON_SIZE_BUTTON
    );

    set_acl_str = _("Permission is <b>granted</b> to modify the permission list.");
  }

  g_array_free(accounts, TRUE);

  /* Update status text */
  error_str = NULL;
  if(error != NULL)
  {
    error_str = g_markup_printf_escaped(
      _("<b>Server Error:</b> %s"),
      error->message
    );

    query_acl_str = error_str;
  }
  else if(priv->query_acl_request != NULL)
  {
    query_acl_str = _("Querying current permissions for "
                      "this node from the server...");
  }
  else if(!inf_acl_mask_has(&perms, INF_ACL_CAN_QUERY_ACL) &&
          !inf_browser_has_acl(priv->browser, &priv->browser_iter, 0))
  {
    query_acl_str = _("Permission is <b>not granted</b> to query the "
                      "permission list for this node from the server. "
                      "Showing only default permissions and permissions "
                      "for the own account.");
  }
  else
  {
    query_acl_str = _("Permissions are <b>granted</b> to query the full "
                      "permission list from the server. "
                      "Showing all permissions.");
  }

  str = g_strdup_printf("%s\n\n%s", query_acl_str, set_acl_str);
  g_free(error_str);

  gtk_label_set_markup(GTK_LABEL(priv->status_text), str);
  g_free(str);
}

static void
inf_gtk_permissions_dialog_register(InfGtkPermissionsDialog* dialog)
{
  InfGtkPermissionsDialogPrivate* priv;
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  g_assert(priv->browser != NULL);

  g_signal_connect(
    priv->browser,
    "node-removed",
    G_CALLBACK(inf_gtk_permissions_dialog_node_removed_cb),
    dialog
  );

  g_signal_connect(
    priv->browser,
    "acl-account-added",
    G_CALLBACK(inf_gtk_permissions_dialog_acl_account_added_cb),
    dialog
  );

  g_signal_connect(
    priv->browser,
    "acl-account-removed",
    G_CALLBACK(inf_gtk_permissions_dialog_acl_account_removed_cb),
    dialog
  );

  g_signal_connect(
    priv->browser,
    "acl-changed",
    G_CALLBACK(inf_gtk_permissions_dialog_acl_changed_cb),
    dialog
  );
}

static void
inf_gtk_permissions_dialog_unregister(InfGtkPermissionsDialog* dialog)
{
  InfGtkPermissionsDialogPrivate* priv;
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  g_assert(priv->browser != NULL);

  inf_signal_handlers_disconnect_by_func(
    priv->browser,
    G_CALLBACK(inf_gtk_permissions_dialog_node_removed_cb),
    dialog
  );

  inf_signal_handlers_disconnect_by_func(
    priv->browser,
    G_CALLBACK(inf_gtk_permissions_dialog_acl_account_added_cb),
    dialog
  );

  inf_signal_handlers_disconnect_by_func(
    priv->browser,
    G_CALLBACK(inf_gtk_permissions_dialog_acl_account_removed_cb),
    dialog
  );

  inf_signal_handlers_disconnect_by_func(
    priv->browser,
    G_CALLBACK(inf_gtk_permissions_dialog_acl_changed_cb),
    dialog
  );
}

/*
 * GObject virtual functions
 */

static void
inf_gtk_permissions_dialog_init(GTypeInstance* instance,
                                gpointer g_class)
{
  InfGtkPermissionsDialog* dialog;
  InfGtkPermissionsDialogPrivate* priv;
  GtkTreeViewColumn* column;
  GtkTreeSelection* selection;
  GtkWidget* scroll;
  GtkWidget* hbox;
  GtkWidget* buttons_hbox;
  GtkWidget* status_hbox;
  GtkWidget* vbox;
  GtkWidget* account_list_vbox;

  GtkWidget* image;
  GtkWidget* image_hbox;

  GtkWidget* dialog_vbox;

  dialog = INF_GTK_PERMISSIONS_DIALOG(instance);
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  /* The pointer is the account ID with INF_ACL_ACCOUNT_ID_TO_POINTER */
  priv->account_store = gtk_list_store_new(2, G_TYPE_POINTER, G_TYPE_STRING);

  gtk_tree_sortable_set_sort_column_id(
    GTK_TREE_SORTABLE(priv->account_store),
    INF_GTK_PERMISSIONS_DIALOG_COLUMN_NAME,
    GTK_SORT_ASCENDING
  );

  gtk_tree_sortable_set_sort_func(
    GTK_TREE_SORTABLE(priv->account_store),
    INF_GTK_PERMISSIONS_DIALOG_COLUMN_NAME,
    inf_gtk_permissions_dialog_account_sort_func,
    dialog,
    NULL
  );

  priv->query_acl_account_list_request = NULL;
  priv->account_list_queried = FALSE;
  priv->accounts = NULL;
  priv->n_accounts = 0;

  priv->query_acl_request = NULL;
  priv->set_acl_requests = NULL;
  priv->remove_acl_account_requests = NULL;
  priv->lookup_acl_account_requests = NULL;

  priv->pending_sheets = NULL;

  priv->popup_menu = NULL;
  priv->popup_account = 0;

  column = gtk_tree_view_column_new();
  gtk_tree_view_column_set_title(column, _("Accounts"));
  gtk_tree_view_column_set_spacing(column, 6);

  priv->renderer = gtk_cell_renderer_combo_new();
  gtk_tree_view_column_pack_start(column, priv->renderer, FALSE);

  gtk_tree_view_column_set_cell_data_func(
    column,
    priv->renderer,
    inf_gtk_permissions_dialog_name_data_func,
    NULL,
    NULL
  );

  g_signal_connect(
    G_OBJECT(priv->renderer),
    "editing-started",
    G_CALLBACK(inf_gtk_permissions_dialog_renderer_editing_started_cb),
    dialog
  );

  g_signal_connect(
    G_OBJECT(priv->renderer),
    "editing-canceled",
    G_CALLBACK(inf_gtk_permissions_dialog_renderer_editing_canceled_cb),
    dialog
  );

  g_signal_connect(
    G_OBJECT(priv->renderer),
    "edited",
    G_CALLBACK(inf_gtk_permissions_dialog_renderer_edited_cb),
    dialog
  );

  g_signal_connect(
    G_OBJECT(priv->renderer),
    "changed",
    G_CALLBACK(inf_gtk_permissions_dialog_renderer_changed_cb),
    dialog
  );

  priv->tree_view =
    gtk_tree_view_new_with_model(GTK_TREE_MODEL(priv->account_store));
  gtk_tree_view_append_column(GTK_TREE_VIEW(priv->tree_view), column);

  g_signal_connect(
    G_OBJECT(priv->tree_view),
    "key-press-event",
    G_CALLBACK(inf_gtk_permissions_dialog_key_press_event_cb),
    dialog
  );

  g_signal_connect(
    G_OBJECT(priv->tree_view),
    "button-press-event",
    G_CALLBACK(inf_gtk_permissions_dialog_button_press_event_cb),
    dialog
  );

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->tree_view));
  gtk_tree_selection_set_mode(selection, GTK_SELECTION_BROWSE);

  g_signal_connect(
    G_OBJECT(selection),
    "changed",
    G_CALLBACK(inf_gtk_permissions_dialog_selection_changed_cb),
    dialog
  );

  gtk_widget_show(priv->tree_view);

  scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_widget_set_size_request(scroll, 200, 350);

  gtk_scrolled_window_set_shadow_type(
    GTK_SCROLLED_WINDOW(scroll),
    GTK_SHADOW_IN
  );

  gtk_scrolled_window_set_policy(
    GTK_SCROLLED_WINDOW(scroll),
    GTK_POLICY_AUTOMATIC,
    GTK_POLICY_AUTOMATIC
  );

  gtk_container_add(GTK_CONTAINER(scroll), priv->tree_view);
  gtk_widget_show(scroll);

  image = gtk_image_new_from_stock(GTK_STOCK_ADD, GTK_ICON_SIZE_BUTTON);
  gtk_widget_show(image);

  priv->add_button = gtk_button_new();
  gtk_container_add(GTK_CONTAINER(priv->add_button), image);

  g_signal_connect(
    G_OBJECT(priv->add_button),
    "clicked",
    G_CALLBACK(inf_gtk_permissions_dialog_add_clicked_cb),
    dialog
  );

  gtk_widget_show(priv->add_button);

  image = gtk_image_new_from_stock(GTK_STOCK_REMOVE, GTK_ICON_SIZE_BUTTON);
  gtk_widget_show(image);

  priv->remove_button = gtk_button_new();
  gtk_container_add(GTK_CONTAINER(priv->remove_button), image);

  g_signal_connect(
    G_OBJECT(priv->remove_button),
    "clicked",
    G_CALLBACK(inf_gtk_permissions_dialog_remove_clicked_cb),
    dialog
  );

  gtk_widget_show(priv->remove_button);

  buttons_hbox = gtk_hbox_new(FALSE, 12);

  gtk_box_pack_start(
    GTK_BOX(buttons_hbox),
    priv->add_button,
    FALSE,
    FALSE,
    0
  );

  gtk_box_pack_start(
    GTK_BOX(buttons_hbox),
    priv->remove_button,
    FALSE,
    FALSE,
    0
  );

  gtk_widget_show(buttons_hbox);
  account_list_vbox = gtk_vbox_new(FALSE, 12);

  gtk_box_pack_start(
    GTK_BOX(account_list_vbox),
    scroll,
    TRUE,
    TRUE,
    0
  );

  gtk_box_pack_start(
    GTK_BOX(account_list_vbox),
    buttons_hbox,
    FALSE,
    FALSE,
    0
  );

  gtk_widget_show(account_list_vbox);

  priv->sheet_view = inf_gtk_acl_sheet_view_new();

  g_signal_connect(
    G_OBJECT(priv->sheet_view),
    "sheet-changed",
    G_CALLBACK(inf_gtk_permissions_dialog_sheet_changed_cb),
    dialog
  );

  gtk_widget_show(priv->sheet_view);

  hbox = gtk_hbox_new(FALSE, 12);
  gtk_box_pack_start(GTK_BOX(hbox), account_list_vbox, FALSE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(hbox), priv->sheet_view, TRUE, TRUE, 0);
  gtk_widget_show(hbox);

  priv->status_image = gtk_image_new();
  gtk_widget_show(priv->status_image);

  priv->status_text = gtk_label_new(NULL);
  gtk_label_set_max_width_chars(GTK_LABEL(priv->status_text), 50);
  gtk_label_set_width_chars(GTK_LABEL(priv->status_text), 50);
  gtk_label_set_line_wrap(GTK_LABEL(priv->status_text), TRUE);
  gtk_misc_set_alignment(GTK_MISC(priv->status_text), 0.0, 0.5);
  gtk_widget_show(priv->status_text);

  status_hbox = gtk_hbox_new(FALSE, 12);

  gtk_box_pack_start(
    GTK_BOX(status_hbox),
    priv->status_image,
    FALSE,
    FALSE,
    0
  );

  gtk_box_pack_start(
    GTK_BOX(status_hbox),
    priv->status_text,
    TRUE,
    TRUE,
    0
  );

  gtk_widget_show(status_hbox);

  vbox = gtk_vbox_new(FALSE, 12);
  gtk_box_pack_start(GTK_BOX(vbox), status_hbox, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show(vbox);

  image = gtk_image_new_from_stock(
    GTK_STOCK_DIALOG_AUTHENTICATION,
    GTK_ICON_SIZE_DIALOG
  );

  gtk_misc_set_alignment(GTK_MISC(image), 0.0, 0.0);
  gtk_widget_show(image);

  image_hbox = gtk_hbox_new(FALSE, 12);
  gtk_box_pack_start(GTK_BOX(image_hbox), image, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(image_hbox), vbox, TRUE, TRUE, 0);
  gtk_widget_show(image_hbox);

#if GTK_CHECK_VERSION(2,14,0)
  dialog_vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
#else
  dialog_vbox = GTK_DIALOG(dialog)->vbox;
#endif

  gtk_box_set_spacing(GTK_BOX(dialog_vbox), 12);
  gtk_box_pack_start(GTK_BOX(dialog_vbox), image_hbox, FALSE, FALSE, 0);

  gtk_container_set_border_width(GTK_CONTAINER(dialog), 12);
  gtk_window_set_resizable(GTK_WINDOW(dialog), TRUE);
  gtk_window_set_default_size(GTK_WINDOW(dialog), 640, 480);
}

static GObject*
inf_gtk_permissions_dialog_constructor(GType type,
                                       guint n_properties,
                                       GObjectConstructParam* properties)
{
  GObject* object;
  InfGtkPermissionsDialogPrivate* priv;

  object = G_OBJECT_CLASS(parent_class)->constructor(
    type,
    n_properties,
    properties
  );

  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(object);

  inf_gtk_permissions_dialog_update(
    INF_GTK_PERMISSIONS_DIALOG(object),
    NULL
  );

  return object;
}

static void
inf_gtk_permissions_dialog_dispose(GObject* object)
{
  InfGtkPermissionsDialog* dialog;
  InfGtkPermissionsDialogPrivate* priv;

  dialog = INF_GTK_PERMISSIONS_DIALOG(object);
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  if(priv->renderer != NULL)
  {
    gtk_cell_renderer_stop_editing(GTK_CELL_RENDERER(priv->renderer), TRUE);

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->renderer),
      G_CALLBACK(inf_gtk_permissions_dialog_renderer_editing_started_cb),
      dialog
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->renderer),
      G_CALLBACK(inf_gtk_permissions_dialog_renderer_editing_canceled_cb),
      dialog
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->renderer),
      G_CALLBACK(inf_gtk_permissions_dialog_renderer_edited_cb),
      dialog
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->renderer),
      G_CALLBACK(inf_gtk_permissions_dialog_renderer_changed_cb),
      dialog
    );

    priv->renderer = NULL;
  }

  while(priv->remove_acl_account_requests != NULL)
  {
    inf_gtk_permissions_dialog_remove_remove_acl_account_request(
      dialog,
      priv->remove_acl_account_requests->data
    );
  }

  while(priv->lookup_acl_account_requests != NULL)
  {
    inf_gtk_permissions_dialog_remove_lookup_acl_accounts_request(
      dialog,
      priv->lookup_acl_account_requests->data
    );
  }

  if(priv->browser != NULL)
  {
    inf_gtk_permissions_dialog_set_node(dialog, NULL, NULL);
  }

  g_assert(priv->set_acl_requests == NULL);
  g_assert(priv->pending_sheets == NULL);

  if(priv->account_store != NULL)
  {
    g_object_unref(priv->account_store);
    priv->account_store = NULL;
  }

  /* During parent disposure, this callback might be called, leading to a
   * crash since we have already disposed of all our resources, therefore
   * explicitly disconnect here. */
  if(priv->sheet_view != NULL)
  {
    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->sheet_view),
      G_CALLBACK(inf_gtk_permissions_dialog_sheet_changed_cb),
      dialog
    );

    priv->sheet_view = NULL;
  }

  G_OBJECT_CLASS(parent_class)->dispose(object);
}


static void
inf_gtk_permissions_dialog_finalize(GObject* object)
{
  InfGtkPermissionsDialog* dialog;
  InfGtkPermissionsDialogPrivate* priv;
  guint i;

  dialog = INF_GTK_PERMISSIONS_DIALOG(object);
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  for(i = 0; i < priv->n_accounts; ++i)
    g_free(priv->accounts[i].name);
  g_free(priv->accounts);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_gtk_permissions_dialog_set_property(GObject* object,
                                        guint prop_id,
                                        const GValue* value,
                                        GParamSpec* pspec)
{
  InfGtkPermissionsDialog* dialog;
  InfGtkPermissionsDialogPrivate* priv;

  dialog = INF_GTK_PERMISSIONS_DIALOG(object);
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  switch(prop_id)
  {
  case PROP_BROWSER:
    g_assert(priv->browser == NULL); /* construct only */
    priv->browser = INF_BROWSER(g_value_dup_object(value));

    if(priv->browser != NULL)
      inf_gtk_permissions_dialog_register(dialog);

    break;
  case PROP_BROWSER_ITER:
    priv->browser_iter = *(InfBrowserIter*)g_value_get_boxed(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_gtk_permissions_dialog_get_property(GObject* object,
                                        guint prop_id,
                                        GValue* value,
                                        GParamSpec* pspec)
{
  InfGtkPermissionsDialog* dialog;
  InfGtkPermissionsDialogPrivate* priv;

  dialog = INF_GTK_PERMISSIONS_DIALOG(object);
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  switch(prop_id)
  {
  case PROP_BROWSER:
    g_value_set_object(value, priv->browser);
    break;
  case PROP_BROWSER_ITER:
    g_value_set_boxed(value, &priv->browser_iter);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

/*
 * GType registration
 */

static void
inf_gtk_permissions_dialog_class_init(gpointer g_class,
                                       gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = GTK_DIALOG_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfGtkPermissionsDialogPrivate));

  object_class->constructor = inf_gtk_permissions_dialog_constructor;
  object_class->dispose = inf_gtk_permissions_dialog_dispose;
  object_class->finalize = inf_gtk_permissions_dialog_finalize;
  object_class->set_property = inf_gtk_permissions_dialog_set_property;
  object_class->get_property = inf_gtk_permissions_dialog_get_property;

  g_object_class_install_property(
    object_class,
    PROP_BROWSER,
    g_param_spec_object(
      "browser",
      "Browser",
      "The browser with the node for which to show the permissions",
      INF_TYPE_BROWSER,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_BROWSER_ITER,
    g_param_spec_boxed(
      "browser-iter",
      "Browser Iter",
      "An iterator pointing to the node inside the browser for which to show "
      "the permissions",
      INF_TYPE_BROWSER_ITER,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );
}

GType
inf_gtk_permissions_dialog_get_type(void)
{
  static GType permissions_dialog_type = 0;

  if(!permissions_dialog_type)
  {
    static const GTypeInfo permissions_dialog_type_info = {
      sizeof(InfGtkPermissionsDialogClass),    /* class_size */
      NULL,                                    /* base_init */
      NULL,                                    /* base_finalize */
      inf_gtk_permissions_dialog_class_init,   /* class_init */
      NULL,                                    /* class_finalize */
      NULL,                                    /* class_data */
      sizeof(InfGtkPermissionsDialog),         /* instance_size */
      0,                                       /* n_preallocs */
      inf_gtk_permissions_dialog_init,         /* instance_init */
      NULL                                     /* value_table */
    };

    permissions_dialog_type = g_type_register_static(
      GTK_TYPE_DIALOG,
      "InfGtkPermissionsDialog",
      &permissions_dialog_type_info,
      0
    );
  }

  return permissions_dialog_type;
}

/*
 * Public API.
 */

/**
 * inf_gtk_permissions_dialog_new:
 * @parent: Parent #GtkWindow of the dialog.
 * @dialog_flags: Flags for the dialog, see #GtkDialogFlags.
 * @browser: The #InfBrowser containing the node to show permissions for, or
 * %NULL.
 * @iter: An iterator pointing to the node to show permissions for, or %NULL.
 *
 * Creates a new #InfGtkPermissionsDialog, showing the ACL for the node
 * @iter points to inside @browser. If @browser is %NULL, @iter must be %NULL,
 * too. In that case no permissions are shown, and the node to be shown can
 * be set later with inf_gtk_permissions_dialog_set_node().
 *
 * Returns: A new #InfGtkPermissionsDialog. Free with gtk_widget_destroy()
 * when no longer needed.
 */
InfGtkPermissionsDialog*
inf_gtk_permissions_dialog_new(GtkWindow* parent,
                               GtkDialogFlags dialog_flags,
                               InfBrowser* browser,
                               const InfBrowserIter* iter)
{
  GObject* object;

  g_return_val_if_fail(parent == NULL || GTK_IS_WINDOW(parent), NULL);
  g_return_val_if_fail(browser == NULL || INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(browser == NULL || iter != NULL, NULL);

  object = g_object_new(
    INF_GTK_TYPE_PERMISSIONS_DIALOG,
    "browser", browser,
    "browser-iter", iter,
    NULL
  );

  if(dialog_flags & GTK_DIALOG_MODAL)
    gtk_window_set_modal(GTK_WINDOW(object), TRUE);

  if(dialog_flags & GTK_DIALOG_DESTROY_WITH_PARENT)
    gtk_window_set_destroy_with_parent(GTK_WINDOW(object), TRUE);

#if !GTK_CHECK_VERSION(2,90,7)
  if(dialog_flags & GTK_DIALOG_NO_SEPARATOR)
    gtk_dialog_set_has_separator(GTK_DIALOG(object), FALSE);
#endif

  gtk_window_set_transient_for(GTK_WINDOW(object), parent);
  return INF_GTK_PERMISSIONS_DIALOG(object);
}

/**
 * inf_gtk_permissions_dialog_set_node:
 * @dialog: A #InfGtkPermissionsDialog.
 * @browser: The #InfBrowser containing the node to show permissions for, or
 * %NULL.
 * @iter: An iterator pointing to the node to show permissions for, or %NULL.
 *
 * Changes the node the dialog shows permissions for. To unset the node, both
 * @browser and @iter should be %NULL.
 */
void
inf_gtk_permissions_dialog_set_node(InfGtkPermissionsDialog* dialog,
                                    InfBrowser* browser,
                                    const InfBrowserIter* iter)
{
  InfGtkPermissionsDialogPrivate* priv;
  GSList* item;
  guint i;

  g_return_if_fail(INF_GTK_IS_PERMISSIONS_DIALOG(dialog));
  g_return_if_fail(browser == NULL || INF_IS_BROWSER(browser));
  g_return_if_fail((browser == NULL) == (iter == NULL));

  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  if(priv->popup_menu != NULL)
    gtk_menu_popdown(priv->popup_menu);

  if(priv->browser != NULL)
  {
    if(priv->query_acl_account_list_request != NULL)
    {
      inf_signal_handlers_disconnect_by_func(
        priv->query_acl_account_list_request,
        G_CALLBACK(
          inf_gtk_permissions_dialog_query_acl_account_list_finished_cb
        ),
        dialog
      );
      
      priv->query_acl_account_list_request = NULL;
    }

    if(priv->query_acl_request != NULL)
    {
      inf_signal_handlers_disconnect_by_func(
        priv->query_acl_request,
        G_CALLBACK(inf_gtk_permissions_dialog_query_acl_finished_cb),
        dialog
      );

      priv->query_acl_request = NULL;
    }

    for(item = priv->set_acl_requests; item != NULL; item = item->next)
    {
      inf_signal_handlers_disconnect_by_func(
        G_OBJECT(item->data),
        G_CALLBACK(inf_gtk_permissions_dialog_set_acl_finished_cb),
        dialog
      );

      g_object_unref(item->data);
    }

    g_slist_free(priv->set_acl_requests);
    priv->set_acl_requests = NULL;

    while(priv->pending_sheets != NULL)
    {
      inf_gtk_permissions_dialog_remove_pending_sheet(
        dialog,
        priv->pending_sheets->data
      );
    }
  }

  for(i = 0; i < priv->n_accounts; ++i)
    g_free(priv->accounts[i].name);
  g_free(priv->accounts);
  priv->accounts = NULL;
  priv->n_accounts = 0;

  /* While clearing the list store, block the selection changed callback of
   * the treeview, otherwise it would cause
   * inf_gtk_permissions_dialog_update() to be called, which would fill the
   * tree view again while it is being cleared. We issue one update at the
   * end of the node change. */
  inf_signal_handlers_block_by_func(
    gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->tree_view)),
    G_CALLBACK(inf_gtk_permissions_dialog_selection_changed_cb),
    dialog
  );

  gtk_list_store_clear(priv->account_store);

  inf_signal_handlers_unblock_by_func(
    gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->tree_view)),
    G_CALLBACK(inf_gtk_permissions_dialog_selection_changed_cb),
    dialog
  );

  if(priv->browser != browser)
  {
    if(priv->browser != NULL)
    {
      inf_gtk_permissions_dialog_unregister(dialog);
      g_object_unref(priv->browser);
    }

    priv->browser = browser;
    if(iter != NULL)
      priv->browser_iter = *iter;

    if(priv->browser != NULL)
    {
      g_object_ref(priv->browser);
      inf_gtk_permissions_dialog_register(dialog);
    }

    g_object_notify(G_OBJECT(dialog), "browser");
    g_object_notify(G_OBJECT(dialog), "browser-iter");
  }

  inf_gtk_permissions_dialog_update(dialog, NULL);
}

/* vim:set et sw=2 ts=2: */
