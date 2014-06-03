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
 * If either the "can-query-user-list" or the "can-query-acl" permissions are
 * not granted for the local user, the dialog only shows the permissions for
 * the default user and the local user. The dialog also comes with a status
 * text to inform the user why certain functionality is not available.
 *
 * The dialog class reacts to changes to the ACL in real time, and also if the
 * node that is being monitored is removed.
 **/

#include <libinfgtk/inf-gtk-permissions-dialog.h>
#include <libinfgtk/inf-gtk-acl-sheet-view.h>
#include <libinfinity/inf-i18n.h>
#include <gdk/gdkkeysyms.h>

typedef struct _InfGtkPermissionsDialogPrivate InfGtkPermissionsDialogPrivate;
struct _InfGtkPermissionsDialogPrivate {
  InfBrowser* browser;
  InfBrowserIter browser_iter;

  GtkListStore* account_store;
  gboolean show_full_list;

  InfRequest* query_acl_account_list_request;
  InfRequest* query_acl_request;
  GSList* set_acl_requests;
  GSList* remove_acl_account_requests;

  GtkMenu* popup_menu;
  const InfAclAccount* popup_account;

  GtkWidget* tree_view;
  GtkWidget* sheet_view;
  GtkWidget* status_image;
  GtkWidget* status_text;
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
inf_gtk_permissions_dialog_sheet_changed_cb(InfGtkAclSheetView* sheet_view,
                                            gpointer user_data)
{
  InfGtkPermissionsDialog* dialog;
  InfGtkPermissionsDialogPrivate* priv;
  const InfAclSheet* sheet;
  InfAclSheetSet sheet_set;
  InfRequest* request;

  dialog = INF_GTK_PERMISSIONS_DIALOG(user_data);
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  sheet = inf_gtk_acl_sheet_view_get_sheet(
    INF_GTK_ACL_SHEET_VIEW(priv->sheet_view)
  );

  g_assert(sheet != NULL);

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
    priv->set_acl_requests = g_slist_prepend(priv->set_acl_requests, request);
    g_object_ref(request);
  }
}

static gboolean
inf_gtk_permissions_dialog_find_account(InfGtkPermissionsDialog* dialog,
                                        const InfAclAccount* account,
                                        GtkTreeIter* out_iter)
{
  InfGtkPermissionsDialogPrivate* priv;
  const InfAclAccount* row_account;
  GtkTreeModel* model;
  GtkTreeIter iter;

  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);
  model = GTK_TREE_MODEL(priv->account_store);

  if(gtk_tree_model_get_iter_first(model, &iter))
  {
    do
    {
      gtk_tree_model_get(model, &iter, 0, &row_account, -1);
      if(row_account == account)
      {
        if(out_iter != NULL)
          *out_iter = iter;
        return TRUE;
      }
    } while(gtk_tree_model_iter_next(model, &iter));
  }

  return FALSE;
}

static void
inf_gtk_permissions_dialog_fill_account_list(InfGtkPermissionsDialog* dialog,
                                             const InfAclAccount** accounts,
                                             guint n_accounts)
{
  InfGtkPermissionsDialogPrivate* priv;
  GtkTreeModel* model;
  gboolean* have_accounts;
  GtkTreeIter iter;
  const InfAclAccount* account;
  gboolean has_row;
  guint i;

  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);
  model = GTK_TREE_MODEL(priv->account_store);

  /* Remove all accounts that are not present in the given account list.
   * Flag accounts that we have found, and then add all the un-flagged ones.
   * This way we keep the overlapping accounts in the list, which should
   * provide a smooth user experience, for example when an item in the list
   * is selected it is not removed and re-added. */
  have_accounts = g_malloc(n_accounts * sizeof(gboolean));
  for(i = 0; i < n_accounts; ++i)
    have_accounts[i] = FALSE;

  has_row = gtk_tree_model_get_iter_first(model, &iter);
  while(has_row)
  {
    gtk_tree_model_get(model, &iter, 0, &account, -1);
    for(i = 0; i < n_accounts; ++i)
      if(account == accounts[i])
        break;

    if(i != n_accounts)
    {
      have_accounts[i] = TRUE;
      has_row = gtk_tree_model_iter_next(model, &iter);
    }
    else
    {
      has_row = gtk_list_store_remove(priv->account_store, &iter);
    }
  }

  for(i = 0; i < n_accounts; ++i)
  {
    if(!have_accounts[i])
    {
      gtk_list_store_insert_with_values(
        priv->account_store,
        NULL,
        -1,
        0,
        accounts[i],
        -1
      );
    }
  }

  g_free(have_accounts);
}

static void
inf_gtk_permissions_dialog_update_sheet(InfGtkPermissionsDialog* dialog)
{
  InfGtkPermissionsDialogPrivate* priv;
  GtkTreeSelection* selection;

  GtkTreeModel* model;
  GtkTreeIter iter;
  const InfAclAccount* account;
  const InfAclAccount* default_account;
  const InfAclSheetSet* sheet_set;
  const InfAclSheet* sheet;
  InfAclSheet default_sheet;
  InfAclMask nonroot_mask;

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

    account = NULL;
  }
  else
  {
    gtk_tree_model_get(model, &iter, 0, &account, -1);
    g_assert(account != NULL);

    sheet = NULL;
    sheet_set = inf_browser_get_acl(priv->browser, &priv->browser_iter);
    if(sheet_set != NULL)
    {
      sheet = inf_acl_sheet_set_find_const_sheet(sheet_set, account);
      if(sheet != NULL)
      {
        inf_gtk_acl_sheet_view_set_sheet(
          INF_GTK_ACL_SHEET_VIEW(priv->sheet_view),
          sheet
        );
      }
    }

    /* No sheet: set default sheet (all permissions masked out) */
    if(sheet == NULL)
    {
      default_sheet.account = account;
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
  if(!inf_browser_get_parent(priv->browser, &test_iter))
  {
    /* This is the root node. Block default column if this is the default
     * account. */
    default_account =
      inf_browser_lookup_acl_account(priv->browser, "default");
    g_assert(default_account != NULL);

    if(account == default_account)
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

    inf_gtk_acl_sheet_view_set_permission_mask(
      INF_GTK_ACL_SHEET_VIEW(priv->sheet_view),
      &INF_ACL_MASK_ALL
    );
  }
  else
  {
    /* This is a leaf node. Show the default column, and block non-root
     * permissions. */
    inf_gtk_acl_sheet_view_set_show_default(
      INF_GTK_ACL_SHEET_VIEW(priv->sheet_view),
      TRUE
    );

    inf_acl_mask_neg(&INF_ACL_MASK_ROOT, &nonroot_mask);
    inf_gtk_acl_sheet_view_set_permission_mask(
      INF_GTK_ACL_SHEET_VIEW(priv->sheet_view),
      &nonroot_mask
    );
  }

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
  GtkTreeIter iter;
  GtkTreePath* path;

  dialog = INF_GTK_PERMISSIONS_DIALOG(user_data);
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  /* Add the new user to the user list. Note that this is also called when
   * the given user was updated, in which case we need to call row_changed,
   * since its name might have changed. */
  if(priv->show_full_list == TRUE)
  {  
    /* Make sure not to fill an account twice */
    if(inf_gtk_permissions_dialog_find_account(dialog, account, &iter))
    {
      path = gtk_tree_model_get_path(
        GTK_TREE_MODEL(priv->account_store),
        &iter
      );
      
      gtk_tree_model_row_changed(
        GTK_TREE_MODEL(priv->account_store),
        path,
        &iter
      );

      gtk_tree_path_free(path);
    }
    else
    {
      gtk_list_store_insert_with_values(
        priv->account_store,
        NULL,
        -1,
        0,
        account,
        -1
      );
    }
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
  GtkTreeIter iter;

  dialog = INF_GTK_PERMISSIONS_DIALOG(user_data);
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  if(priv->popup_menu != NULL && account == priv->popup_account)
    gtk_menu_popdown(priv->popup_menu);

  g_assert(priv->popup_menu == NULL);
  g_assert(priv->popup_account == NULL);

  /* The account is not necessarily always in the list, for example if we have
   * permissions to query the user list but not to query the ACL for the
   * current node, we might get this callback but not have all accounts in the
   * list. */
  have_account =
    inf_gtk_permissions_dialog_find_account(dialog, account, &iter);

  if(have_account)
    gtk_list_store_remove(priv->account_store, &iter);
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
   * user list or the ACL for this node. */
  if(inf_browser_is_ancestor(browser, iter, &priv->browser_iter))
    inf_gtk_permissions_dialog_update(dialog, NULL);
}

static void
inf_gtk_permissions_dialog_selection_changed_cb(GtkTreeSelection* selection,
                                                gpointer user_data)
{
  InfGtkPermissionsDialog* dialog;
  dialog = INF_GTK_PERMISSIONS_DIALOG(user_data);
  inf_gtk_permissions_dialog_update_sheet(dialog);
}

static void
inf_gtk_permissions_dialog_remove_acl_account_finished_cb(
  InfRequest* request,
  const InfRequestResult* result,
  const GError* error,
  gpointer user_data);

static void
inf_gtk_permissions_dialog_remove_acl_account_request(
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

  inf_gtk_permissions_dialog_remove_acl_account_request(dialog, request);
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
  g_assert(priv->popup_account != NULL);

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
  const InfAclAccount** accounts;
  const InfAclAccount* account;
  GtkTreeSelection* selection;
  GtkTreeIter iter;

  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);
  g_assert(priv->popup_menu == NULL);

  /* Make sure that we have permissions to remove accounts */
  inf_browser_get_root(priv->browser, &root);
  inf_acl_mask_set1(&perms, INF_ACL_CAN_REMOVE_ACCOUNT);

  inf_browser_check_acl(
    priv->browser,
    &root,
    inf_browser_get_acl_local_account(priv->browser),
    &perms,
    &perms
  );

  if(!inf_acl_mask_has(&perms, INF_ACL_CAN_REMOVE_ACCOUNT))
    return FALSE;

  /* Make sure we have the account list queried */
  accounts = inf_browser_get_acl_account_list(priv->browser, &n_accounts);
  if(!accounts)
    return FALSE;
  g_free(accounts);

  /* Make sure the selected account is not the default account */
  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->tree_view));
  if(!gtk_tree_selection_get_selected(selection, NULL, &iter))
    return FALSE;

  gtk_tree_model_get(
    GTK_TREE_MODEL(priv->account_store),
    &iter,
    0, &account,
    -1
  );

  if(strcmp(account->id, "default") == 0)
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
  priv->popup_account = account;

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
  priv->popup_account = NULL;
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

static int
inf_gtk_permissions_dialog_account_sort_func(GtkTreeModel* model,
                                             GtkTreeIter* a,
                                             GtkTreeIter* b,
                                             gpointer user_data)
{
  InfGtkPermissionsDialog* dialog;
  InfGtkPermissionsDialogPrivate* priv;

  const InfAclAccount* account_a;
  const InfAclAccount* account_b;
  const InfAclAccount* default_account;

  dialog = INF_GTK_PERMISSIONS_DIALOG(user_data);
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  gtk_tree_model_get(model, a, 0, &account_a, -1);
  gtk_tree_model_get(model, b, 0, &account_b, -1);

  /* default sorts before anything */
  default_account = inf_browser_lookup_acl_account(priv->browser, "default");
  if(account_a == default_account)
  {
    if(account_b == default_account)
      return 0;
    else
      return 1;
  }
  else if(account_b == default_account)
  {
    return -1;
  }
  else
  {
    /* Next, accounts with user name sort before accounts without */
    if(account_a->name == NULL)
    {
      if(account_b->name == NULL)
        return g_utf8_collate(account_b->id, account_a->id);
      else
        return -1;
    }
    else
    {
      if(account_b->name == NULL)
        return 1;
      else
        return g_utf8_collate(account_b->name, account_a->name);
    }
  }
}

static void
inf_gtk_permissions_dialog_name_data_func(GtkTreeViewColumn* column,
                                          GtkCellRenderer* cell,
                                          GtkTreeModel* model,
                                          GtkTreeIter* iter,
                                          gpointer user_data)
{
  const InfAclAccount* account;
  gchar* str;

  gtk_tree_model_get(model, iter, 0, &account, -1);

  if(account->name != NULL)
  {
    g_object_set(G_OBJECT(cell), "text", account->name, NULL);
  }
  else
  {
    str = g_strdup_printf("<%s>", account->id);
    g_object_set(G_OBJECT(cell), "text", str, NULL);
    g_free(str);
  }
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

  dialog = INF_GTK_PERMISSIONS_DIALOG(user_data);
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  priv->query_acl_account_list_request = NULL;
  inf_gtk_permissions_dialog_update(dialog, error);
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

  const InfAclAccount** accounts;
  const InfAclAccount* custom_accounts[2];
  guint n_accounts;

  const InfAclSheetSet* sheet_set;
  gboolean have_full_acl;
  guint n_children;

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

  inf_browser_check_acl(
    priv->browser,
    &priv->browser_iter,
    inf_browser_get_acl_local_account(priv->browser),
    &perms,
    &perms
  );

  /* Request account list and ACL */
  have_full_acl = FALSE;
  accounts = inf_browser_get_acl_account_list(priv->browser, &n_accounts);
  if(accounts == NULL)
  {
    if(inf_acl_mask_has(&perms, INF_ACL_CAN_QUERY_ACCOUNT_LIST) &&
       priv->query_acl_account_list_request == NULL && error == NULL)
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
  else
  {
    if(!inf_browser_has_acl(priv->browser, &priv->browser_iter, NULL))
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
    else
    {
      have_full_acl = TRUE;
    }
  }

  /* Fill the account list widget */
  if(have_full_acl == TRUE)
  {
    if(priv->show_full_list == FALSE)
    {
      inf_gtk_permissions_dialog_fill_account_list(
        dialog,
        accounts,
        n_accounts
      );

      priv->show_full_list = TRUE;
    }
  }
  else
  {
    priv->show_full_list = FALSE;

    custom_accounts[0] =
      inf_browser_lookup_acl_account(priv->browser, "default");
    custom_accounts[1] =
      inf_browser_get_acl_local_account(priv->browser);

    if(custom_accounts[1] != NULL && custom_accounts[1] != custom_accounts[0])
      n_accounts = 2;
    else
      n_accounts = 1;

    inf_gtk_permissions_dialog_fill_account_list(
      dialog,
      custom_accounts,
      n_accounts
    );
  }

  g_free(accounts);

  /* Set editability of the sheet view */
  if(!inf_acl_mask_has(&perms, INF_ACL_CAN_SET_ACL) ||
     !inf_browser_has_acl(priv->browser, &priv->browser_iter, NULL))
  {
    inf_gtk_acl_sheet_view_set_editable(
      INF_GTK_ACL_SHEET_VIEW(priv->sheet_view),
      FALSE
    );

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

    gtk_image_set_from_stock(
      GTK_IMAGE(priv->status_image),
      GTK_STOCK_YES,
      GTK_ICON_SIZE_BUTTON
    );

    set_acl_str = _("Permission is <b>granted</b> to modify the permission list.");
  }

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
  else if(priv->query_acl_account_list_request != NULL)
  {
    query_acl_str = _("Querying the account list from the server...");
  }
  else if(priv->query_acl_request != NULL)
  {
    query_acl_str = _("Querying current permissions for this node from the server...");
  }
  else if(!inf_acl_mask_has(&perms, INF_ACL_CAN_QUERY_ACCOUNT_LIST) &&
          accounts == NULL)
  {
    query_acl_str = _("Permission is <b>not granted</b> to query the "
                      "account list from the server. Showing only default "
                      "permissions and permissions for the own account.");
  }
  else if(!inf_acl_mask_has(&perms, INF_ACL_CAN_QUERY_ACL) &&
          !inf_browser_has_acl(priv->browser, &priv->browser_iter, NULL))
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
  GtkCellRenderer* renderer;
  GtkTreeSelection* selection;
  GtkWidget* scroll;
  GtkWidget* hbox;
  GtkWidget* status_hbox;
  GtkWidget* vbox;

  GtkWidget* image;
  GtkWidget* image_hbox;

  GtkWidget* dialog_vbox;

  dialog = INF_GTK_PERMISSIONS_DIALOG(instance);
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

  /* Do not use INF_TYPE_ACL_USER type, to avoid making a copy of the
   * InfAclUser object. */
  priv->account_store = gtk_list_store_new(1, G_TYPE_POINTER);
  priv->show_full_list = FALSE;

  gtk_tree_sortable_set_sort_column_id(
    GTK_TREE_SORTABLE(priv->account_store),
    0,
    GTK_SORT_DESCENDING
  );

  gtk_tree_sortable_set_sort_func(
    GTK_TREE_SORTABLE(priv->account_store),
    0,
    inf_gtk_permissions_dialog_account_sort_func,
    dialog,
    NULL
  );

  priv->query_acl_account_list_request = NULL;
  priv->query_acl_request = NULL;
  priv->set_acl_requests = NULL;
  priv->remove_acl_account_requests = NULL;

  priv->popup_menu = NULL;
  priv->popup_account = NULL;

  column = gtk_tree_view_column_new();
  gtk_tree_view_column_set_title(column, _("Accounts"));
  gtk_tree_view_column_set_spacing(column, 6);

  renderer = gtk_cell_renderer_text_new();
  gtk_tree_view_column_pack_start(column, renderer, FALSE);

  gtk_tree_view_column_set_cell_data_func(
    column,
    renderer,
    inf_gtk_permissions_dialog_name_data_func,
    NULL,
    NULL
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

  priv->sheet_view = inf_gtk_acl_sheet_view_new();

  g_signal_connect(
    G_OBJECT(priv->sheet_view),
    "sheet-changed",
    G_CALLBACK(inf_gtk_permissions_dialog_sheet_changed_cb),
    dialog
  );

  gtk_widget_show(priv->sheet_view);

  hbox = gtk_hbox_new(FALSE, 12);
  gtk_box_pack_start(GTK_BOX(hbox), scroll, FALSE, TRUE, 0);
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
  gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
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

  while(priv->remove_acl_account_requests != NULL)
  {
    inf_gtk_permissions_dialog_remove_acl_account_request(
      dialog,
      priv->remove_acl_account_requests->data
    );
  }

  if(priv->browser != NULL)
  {
    inf_gtk_permissions_dialog_set_node(dialog, NULL, NULL);
  }

  if(priv->account_store != NULL)
  {
    g_object_unref(priv->account_store);
    priv->account_store = NULL;
  }

  G_OBJECT_CLASS(parent_class)->dispose(object);
}


static void
inf_gtk_permissions_dialog_finalize(GObject* object)
{
  InfGtkPermissionsDialog* dialog;
  InfGtkPermissionsDialogPrivate* priv;

  dialog = INF_GTK_PERMISSIONS_DIALOG(object);
  priv = INF_GTK_PERMISSIONS_DIALOG_PRIVATE(dialog);

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
  }

  gtk_list_store_clear(priv->account_store);
  priv->show_full_list = FALSE;

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
