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

#include <libinfgtk/inf-gtk-acl-sheet-view.h>

/**
 * SECTION:inf-gtk-acl-sheet-view
 * @title: InfGtkAclSheetView
 * @short_description: A widget showing a #InfAclSheet
 * @include: libinfgtk/inf-gtk-acl-sheet-view.h
 * @stability: Unstable
 *
 * #InfGtkAclSheetView is a widget that can show a #InfAclSheet in the user
 * interface. Optionally, it also allows the user to edit the sheet.
 **/

typedef struct _InfGtkAclSheetViewPrivate InfGtkAclSheetViewPrivate;
struct _InfGtkAclSheetViewPrivate {
  GtkListStore* sheet_store;
  GtkWidget* tree_view;

  GtkCellRendererToggle* default_renderer;
  GtkCellRendererToggle* yes_renderer;
  GtkCellRendererToggle* no_renderer;

  InfAclSheet* sheet;
  gboolean editable;
  InfAclMask permission_mask;
};

enum {
  PROP_0,

  PROP_SHEET,
  PROP_EDITABLE,

  PROP_SHOW_DEFAULT,
  PROP_PERMISSION_MASK
};

enum {
  SHEET_CHANGED,

  LAST_SIGNAL
};

#define INF_GTK_ACL_SHEET_VIEW_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_GTK_TYPE_ACL_SHEET_VIEW, InfGtkAclSheetViewPrivate))

static guint acl_sheet_view_signals[LAST_SIGNAL];

G_DEFINE_TYPE_WITH_CODE(InfGtkAclSheetView, inf_gtk_acl_sheet_view, GTK_TYPE_BOX,
  G_ADD_PRIVATE(InfGtkAclSheetView))

static void
inf_gtk_acl_sheet_view_default_toggled_cb(GtkCellRenderer* cell,
                                          const gchar* path_str,
                                          gpointer user_data)
{
  InfGtkAclSheetView* view;
  InfGtkAclSheetViewPrivate* priv;
  GtkTreeModel* model;
  GtkTreeIter iter;
  gboolean result;
  InfAclSetting setting;

  view = INF_GTK_ACL_SHEET_VIEW(user_data);
  priv = INF_GTK_ACL_SHEET_VIEW_PRIVATE(view);
  model = GTK_TREE_MODEL(priv->sheet_store);

  result = gtk_tree_model_get_iter_from_string(model, &iter, path_str);

  g_assert(result == TRUE);
  g_assert(priv->sheet != NULL);

  gtk_tree_model_get(model, &iter, 1, &setting, -1);

  if(inf_acl_mask_has(&priv->sheet->mask, setting))
  {
    inf_acl_mask_and1(&priv->sheet->mask, setting);

    g_signal_emit(G_OBJECT(view), acl_sheet_view_signals[SHEET_CHANGED], 0);
    g_object_notify(G_OBJECT(view), "sheet");
  }
}

static void
inf_gtk_acl_sheet_view_yes_toggled_cb(GtkCellRenderer* cell,
                                      const gchar* path_str,
                                      gpointer user_data)
{
  InfGtkAclSheetView* view;
  InfGtkAclSheetViewPrivate* priv;
  GtkTreeModel* model;
  GtkTreeIter iter;
  gboolean result;
  InfAclSetting setting;

  view = INF_GTK_ACL_SHEET_VIEW(user_data);
  priv = INF_GTK_ACL_SHEET_VIEW_PRIVATE(view);
  model = GTK_TREE_MODEL(priv->sheet_store);

  result = gtk_tree_model_get_iter_from_string(model, &iter, path_str);

  g_assert(result == TRUE);
  g_assert(priv->sheet != NULL);

  gtk_tree_model_get(model, &iter, 1, &setting, -1);

  if(!inf_acl_mask_has(&priv->sheet->mask, setting) ||
     !inf_acl_mask_has(&priv->sheet->perms, setting))
  {
    inf_acl_mask_or1(&priv->sheet->mask, setting);
    inf_acl_mask_or1(&priv->sheet->perms, setting);

    g_signal_emit(G_OBJECT(view), acl_sheet_view_signals[SHEET_CHANGED], 0);
    g_object_notify(G_OBJECT(view), "sheet");
  }
}

static void
inf_gtk_acl_sheet_view_no_toggled_cb(GtkCellRenderer* cell,
                                     const gchar* path_str,
                                     gpointer user_data)
{
  InfGtkAclSheetView* view;
  InfGtkAclSheetViewPrivate* priv;
  GtkTreeModel* model;
  GtkTreeIter iter;
  gboolean result;
  InfAclSetting setting;

  view = INF_GTK_ACL_SHEET_VIEW(user_data);
  priv = INF_GTK_ACL_SHEET_VIEW_PRIVATE(view);
  model = GTK_TREE_MODEL(priv->sheet_store);

  result = gtk_tree_model_get_iter_from_string(model, &iter, path_str);

  g_assert(result == TRUE);
  g_assert(priv->sheet != NULL);

  gtk_tree_model_get(model, &iter, 1, &setting, -1);

  if(!inf_acl_mask_has(&priv->sheet->mask, setting) ||
     inf_acl_mask_has(&priv->sheet->perms, setting))
  {
    inf_acl_mask_or1(&priv->sheet->mask, setting);
    inf_acl_mask_and1(&priv->sheet->perms, setting);

    g_signal_emit(G_OBJECT(view), acl_sheet_view_signals[SHEET_CHANGED], 0);
    g_object_notify(G_OBJECT(view), "sheet");
  }
}

static void
inf_gtk_acl_sheet_view_default_cell_data_func(GtkTreeViewColumn* column,
                                              GtkCellRenderer* cell,
                                              GtkTreeModel* tree,
                                              GtkTreeIter* iter,
                                              gpointer user_data)
{
  InfGtkAclSheetView* view;
  InfGtkAclSheetViewPrivate* priv;
  InfAclSetting setting;

  view = INF_GTK_ACL_SHEET_VIEW(user_data);
  priv = INF_GTK_ACL_SHEET_VIEW_PRIVATE(view);

  gtk_tree_model_get(tree, iter, 1, &setting, -1);

  if(priv->sheet == NULL || !inf_acl_mask_has(&priv->sheet->mask, setting))
    g_object_set(cell, "active", TRUE, NULL);
  else
    g_object_set(cell, "active", FALSE, NULL);
}

static void
inf_gtk_acl_sheet_view_yes_cell_data_func(GtkTreeViewColumn* column,
                                          GtkCellRenderer* cell,
                                          GtkTreeModel* tree,
                                          GtkTreeIter* iter,
                                          gpointer user_data)
{
  InfGtkAclSheetView* view;
  InfGtkAclSheetViewPrivate* priv;
  InfAclSetting setting;

  view = INF_GTK_ACL_SHEET_VIEW(user_data);
  priv = INF_GTK_ACL_SHEET_VIEW_PRIVATE(view);

  gtk_tree_model_get(tree, iter, 1, &setting, -1);

  if(priv->sheet != NULL && inf_acl_mask_has(&priv->sheet->mask, setting) &&
     inf_acl_mask_has(&priv->sheet->perms, setting))
  {
    g_object_set(cell, "active", TRUE, NULL);
  }
  else
  {
    g_object_set(cell, "active", FALSE, NULL);
  }
}

static void
inf_gtk_acl_sheet_view_no_cell_data_func(GtkTreeViewColumn* column,
                                         GtkCellRenderer* cell,
                                         GtkTreeModel* tree,
                                         GtkTreeIter* iter,
                                         gpointer user_data)
{
  InfGtkAclSheetView* view;
  InfGtkAclSheetViewPrivate* priv;
  InfAclSetting setting;

  view = INF_GTK_ACL_SHEET_VIEW(user_data);
  priv = INF_GTK_ACL_SHEET_VIEW_PRIVATE(view);

  gtk_tree_model_get(tree, iter, 1, &setting, -1);

  if(priv->sheet != NULL && inf_acl_mask_has(&priv->sheet->mask, setting) &&
     !inf_acl_mask_has(&priv->sheet->perms, setting))
  {
    g_object_set(cell, "active", TRUE, NULL);
  }
  else
  {
    g_object_set(cell, "active", FALSE, NULL);
  }
}

static void
inf_gtk_acl_sheet_view_update_editable(InfGtkAclSheetView* view)
{
  InfGtkAclSheetViewPrivate* priv;
  gboolean activatable;
  guint i;
  GtkTreeViewColumn* column;
  GList* list;
  GList* item;
  GtkCellRendererToggle* cell;

  priv = INF_GTK_ACL_SHEET_VIEW_PRIVATE(view);

  if(priv->editable == TRUE && priv->sheet != NULL)
    activatable = TRUE;
  else
    activatable = FALSE;

  for(i = 1; i < 4; ++i)
  {
    column = gtk_tree_view_get_column(GTK_TREE_VIEW(priv->tree_view), i);
    list = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(column));

    for(item = list; item != NULL; item = item->next)
    {
      g_assert(GTK_IS_CELL_RENDERER_TOGGLE(item->data));
      cell = GTK_CELL_RENDERER_TOGGLE(item->data);
      g_object_set(G_OBJECT(cell), "activatable", activatable, NULL);
    }

    g_list_free(list);
  }
}

/*
 * GObject overrides
 */

static void
inf_gtk_acl_sheet_view_init(InfGtkAclSheetView* view)
{
  InfGtkAclSheetViewPrivate* priv;
  priv = INF_GTK_ACL_SHEET_VIEW_PRIVATE(view);

  priv->sheet = NULL;
  priv->editable = FALSE;
  inf_acl_mask_clear(&priv->permission_mask);

  gtk_widget_init_template(GTK_WIDGET(view));

  gtk_tree_selection_set_mode(
    gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->tree_view)),
    GTK_SELECTION_NONE
  );

  gtk_tree_sortable_set_sort_column_id(
    GTK_TREE_SORTABLE(priv->sheet_store),
    0,
    GTK_SORT_ASCENDING
  );

  gtk_tree_view_column_set_cell_data_func(
    gtk_tree_view_get_column(GTK_TREE_VIEW(priv->tree_view), 1),
    GTK_CELL_RENDERER(priv->default_renderer),
    inf_gtk_acl_sheet_view_default_cell_data_func,
    view,
    NULL
  );

  gtk_tree_view_column_set_cell_data_func(
    gtk_tree_view_get_column(GTK_TREE_VIEW(priv->tree_view), 2),
    GTK_CELL_RENDERER(priv->yes_renderer),
    inf_gtk_acl_sheet_view_yes_cell_data_func,
    view,
    NULL
  );

  gtk_tree_view_column_set_cell_data_func(
    gtk_tree_view_get_column(GTK_TREE_VIEW(priv->tree_view), 3),
    GTK_CELL_RENDERER(priv->no_renderer),
    inf_gtk_acl_sheet_view_no_cell_data_func,
    view,
    NULL
  );

  inf_gtk_acl_sheet_view_set_permission_mask(view, &INF_ACL_MASK_ALL);
}

static void
inf_gtk_acl_sheet_view_dispose(GObject* object)
{
  InfGtkAclSheetView* view;
  InfGtkAclSheetViewPrivate* priv;

  view = INF_GTK_ACL_SHEET_VIEW(object);
  priv = INF_GTK_ACL_SHEET_VIEW_PRIVATE(view);

  if(priv->sheet != NULL)
    inf_gtk_acl_sheet_view_set_sheet(view, NULL);

  G_OBJECT_CLASS(inf_gtk_acl_sheet_view_parent_class)->dispose(object);
}

static void
inf_gtk_acl_sheet_view_finalize(GObject* object)
{
  InfGtkAclSheetView* view;
  InfGtkAclSheetViewPrivate* priv;

  view = INF_GTK_ACL_SHEET_VIEW(object);
  priv = INF_GTK_ACL_SHEET_VIEW_PRIVATE(view);

  G_OBJECT_CLASS(inf_gtk_acl_sheet_view_parent_class)->finalize(object);
}

static void
inf_gtk_acl_sheet_view_set_property(GObject* object,
                                    guint prop_id,
                                    const GValue* value,
                                    GParamSpec* pspec)
{
  InfGtkAclSheetView* view;
  InfGtkAclSheetViewPrivate* priv;

  view = INF_GTK_ACL_SHEET_VIEW(object);
  priv = INF_GTK_ACL_SHEET_VIEW_PRIVATE(view);

  switch(prop_id)
  {
  case PROP_SHEET:
    inf_gtk_acl_sheet_view_set_sheet(
      view,
      (InfAclSheet*)g_value_get_boxed(value)
    );

    break;
  case PROP_EDITABLE:
    inf_gtk_acl_sheet_view_set_editable(view, g_value_get_boolean(value));
    break;
  case PROP_SHOW_DEFAULT:
    inf_gtk_acl_sheet_view_set_show_default(view, g_value_get_boolean(value));
    break;
  case PROP_PERMISSION_MASK:
    inf_gtk_acl_sheet_view_set_permission_mask(
      view,
      (const InfAclMask*)g_value_get_boxed(value)
    );

    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_gtk_acl_sheet_view_get_property(GObject* object,
                                    guint prop_id,
                                    GValue* value,
                                    GParamSpec* pspec)
{
  InfGtkAclSheetView* view;
  InfGtkAclSheetViewPrivate* priv;

  view = INF_GTK_ACL_SHEET_VIEW(object);
  priv = INF_GTK_ACL_SHEET_VIEW_PRIVATE(view);

  switch(prop_id)
  {
  case PROP_SHEET:
    g_value_set_boxed(value, priv->sheet);
    break;
  case PROP_EDITABLE:
    g_value_set_boolean(value, priv->editable);
    break;
  case PROP_SHOW_DEFAULT:
    g_value_set_boolean(value, inf_gtk_acl_sheet_view_get_show_default(view));
    break;
  case PROP_PERMISSION_MASK:
    g_value_set_boxed(value, &priv->permission_mask);
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
inf_gtk_acl_sheet_view_class_init(InfGtkAclSheetViewClass* sheet_view_class)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(sheet_view_class);

  object_class->dispose = inf_gtk_acl_sheet_view_dispose;
  object_class->finalize = inf_gtk_acl_sheet_view_finalize;
  object_class->set_property = inf_gtk_acl_sheet_view_set_property;
  object_class->get_property = inf_gtk_acl_sheet_view_get_property;
  sheet_view_class->sheet_changed = NULL;

  gtk_widget_class_set_template_from_resource(
    GTK_WIDGET_CLASS(object_class),
    "/de/0x539/libinfgtk/ui/infgtkaclsheetview.ui"
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(object_class),
    InfGtkAclSheetView,
    sheet_store
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(object_class),
    InfGtkAclSheetView,
    tree_view
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(object_class),
    InfGtkAclSheetView,
    default_renderer
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(object_class),
    InfGtkAclSheetView,
    yes_renderer
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(object_class),
    InfGtkAclSheetView,
    no_renderer
  );

  gtk_widget_class_bind_template_callback(
    GTK_WIDGET_CLASS(object_class),
    inf_gtk_acl_sheet_view_default_toggled_cb
  );

  gtk_widget_class_bind_template_callback(
    GTK_WIDGET_CLASS(object_class),
    inf_gtk_acl_sheet_view_yes_toggled_cb
  );

  gtk_widget_class_bind_template_callback(
    GTK_WIDGET_CLASS(object_class),
    inf_gtk_acl_sheet_view_no_toggled_cb
  );

  /**
   * InfGtkAclSheetView::sheet-changed:
   * @view: The #InfGtkAclSheetView that emitted the signal.
   *
   * This signal is emitted when the #InfAclSheet displayed by @view was
   * changed by the user.
   */
  acl_sheet_view_signals[SHEET_CHANGED] = g_signal_new(
    "sheet-changed",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfGtkAclSheetViewClass, sheet_changed),
    NULL, NULL,
    g_cclosure_marshal_VOID__VOID,
    G_TYPE_NONE,
    0
  );

  g_object_class_install_property(
    object_class,
    PROP_SHEET,
    g_param_spec_boxed(
      "sheet",
      "Sheet",
      "The ACL sheet the widget is displaying",
      INF_TYPE_ACL_SHEET,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_EDITABLE,
    g_param_spec_boolean(
      "editable",
      "Editable",
      "Whether the sheet can be edited by the user or not",
      FALSE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_SHOW_DEFAULT,
    g_param_spec_boolean(
      "show-default",
      "Show default",
      "Whether to show the \"default\" column",
      TRUE,
      G_PARAM_READWRITE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_PERMISSION_MASK,
    g_param_spec_boxed(
      "permission-mask",
      "Permission mask",
      "Specifies which permissions to show in the sheet view",
      INF_TYPE_ACL_MASK,
      G_PARAM_READWRITE
    )
  );
}

/*
 * Public API.
 */

/**
 * inf_gtk_acl_sheet_view_new: (constructor)
 *
 * Creates a new #InfGtkAclSheetView. To show a sheet in the view, call
 * inf_gtk_acl_sheet_view_set_sheet().
 *
 * Returns: (transfer full): A new #InfGtkAclSheetView.
 */
GtkWidget*
inf_gtk_acl_sheet_view_new(void)
{
  GObject* object;
  object = g_object_new(INF_GTK_TYPE_ACL_SHEET_VIEW, NULL);
  return GTK_WIDGET(object);
}

/**
 * inf_gtk_acl_sheet_view_set_sheet:
 * @view: A #InfGtkAclSheetView.
 * @sheet: (allow-none): The #InfAclSheet to show, or %NULL.
 *
 * Sets the @sheet to be displayed by @view.
 */
void
inf_gtk_acl_sheet_view_set_sheet(InfGtkAclSheetView* view,
                                 const InfAclSheet* sheet)
{
  InfGtkAclSheetViewPrivate* priv;
  GtkTreeModel* model;
  GtkTreeIter iter;
  GtkTreePath* path;

  g_return_if_fail(INF_GTK_IS_ACL_SHEET_VIEW(view));

  priv = INF_GTK_ACL_SHEET_VIEW_PRIVATE(view);

  if(priv->sheet != NULL)
    inf_acl_sheet_free(priv->sheet);

  if(sheet != NULL)
    priv->sheet = inf_acl_sheet_copy(sheet);
  else
    priv->sheet = NULL;

  model = GTK_TREE_MODEL(priv->sheet_store);
  if(gtk_tree_model_get_iter_first(model, &iter))
  {
    path = gtk_tree_path_new_first();

    do
    {
      gtk_tree_model_row_changed(model, path, &iter);
      gtk_tree_path_next(path);
    } while(gtk_tree_model_iter_next(model, &iter));

    gtk_tree_path_free(path);
  }
  
  if(priv->sheet == NULL)
    gtk_widget_set_sensitive(priv->tree_view, FALSE);
  else
    gtk_widget_set_sensitive(priv->tree_view, TRUE);

  inf_gtk_acl_sheet_view_update_editable(view);

  g_signal_emit(G_OBJECT(view), acl_sheet_view_signals[SHEET_CHANGED], 0);
  g_object_notify(G_OBJECT(view), "sheet");
}

/**
 * inf_gtk_acl_sheet_view_get_sheet:
 * @view: A #InfGtkAclSheetView.
 *
 * Returns the sheet that is currently being displayed by @view, or %NULL if
 * there is no sheet displayed.
 *
 * Returns: (transfer none) (allow-none): A #InfAclSheet owned by @view, or
 * %NULL.
 */
const InfAclSheet*
inf_gtk_acl_sheet_view_get_sheet(InfGtkAclSheetView* view)
{
  g_return_val_if_fail(INF_GTK_IS_ACL_SHEET_VIEW(view), NULL);
  return INF_GTK_ACL_SHEET_VIEW_PRIVATE(view)->sheet;
}

/**
 * inf_gtk_acl_sheet_view_set_editable:
 * @view: A #InfGtkAclSheetView.
 * @editable: Whether to make the sheet editable or not.
 *
 * Sets whether the sheet being displayed by @view can be edited by the user
 * or not.
 */
void
inf_gtk_acl_sheet_view_set_editable(InfGtkAclSheetView* view,
                                    gboolean editable)
{
  InfGtkAclSheetViewPrivate* priv;
  g_return_if_fail(INF_GTK_IS_ACL_SHEET_VIEW(view));

  priv = INF_GTK_ACL_SHEET_VIEW_PRIVATE(view);
  if(priv->editable != editable)
  {
    priv->editable = editable;
    inf_gtk_acl_sheet_view_update_editable(view);
    g_object_notify(G_OBJECT(view), "editable");
  }
}

/**
 * inf_gtk_acl_sheet_view_get_editable:
 * @view: A #InfGtkAclSheetView.
 *
 * Returns whether the sheet being displayed by @view can be edited by the
 * user or not.
 *
 * Returns: %TRUE when the sheet can be edited or %FALSE otherwise.
 */
gboolean
inf_gtk_acl_sheet_view_get_editable(InfGtkAclSheetView* view)
{
  g_return_val_if_fail(INF_GTK_IS_ACL_SHEET_VIEW(view), FALSE);
  return INF_GTK_ACL_SHEET_VIEW_PRIVATE(view)->editable;
}

/**
 * inf_gtk_acl_sheet_view_set_show_default:
 * @view: A #InfGtkAclSheetView.
 * @show: Whether to show the default column.
 *
 * Specifies whether the "default" column is shown, and whether it is
 * allowed to change certain permissions to the default value or not. The
 * ACL sheet for the default account of a directory's root node is not
 * allowed to have default permissions. In this case this function should be
 * called to hide the default column from the user interface.
 */
void
inf_gtk_acl_sheet_view_set_show_default(InfGtkAclSheetView* view,
                                        gboolean show)
{
  InfGtkAclSheetViewPrivate* priv;
  GtkTreeViewColumn* column;

  g_return_if_fail(INF_GTK_IS_ACL_SHEET_VIEW(view));

  priv = INF_GTK_ACL_SHEET_VIEW_PRIVATE(view);
  column = gtk_tree_view_get_column(GTK_TREE_VIEW(priv->tree_view), 1);

  if(gtk_tree_view_column_get_visible(column) != show)
  {
    gtk_tree_view_column_set_visible(column, show);
    g_object_notify(G_OBJECT(view), "show-default");
  }
}

/**
 * inf_gtk_acl_sheet_view_get_show_default:
 * @view: A #InfGtkAclSheetView.
 *
 * Returns whether the "default" column is shown.
 *
 * Returns: %TRUE if the "default" column is shown or %FALSE otherwise.
 */
gboolean
inf_gtk_acl_sheet_view_get_show_default(InfGtkAclSheetView* view)
{
  InfGtkAclSheetViewPrivate* priv;
  GtkTreeViewColumn* column;

  g_return_val_if_fail(INF_GTK_IS_ACL_SHEET_VIEW(view), FALSE);

  priv = INF_GTK_ACL_SHEET_VIEW_PRIVATE(view);
  column = gtk_tree_view_get_column(GTK_TREE_VIEW(priv->tree_view), 1);

  return gtk_tree_view_column_get_visible(column);
}

/**
 * inf_gtk_acl_sheet_view_set_permission_mask:
 * @view: A #InfGtkAclSheetView.
 * @mask: A #InfAclMask with the permissions to show.
 *
 * Sets which permissions of the sheet to show. Only the permissions that
 * are enabled in @mask ar being shown. By default all permissions are shown.
 */
void
inf_gtk_acl_sheet_view_set_permission_mask(InfGtkAclSheetView* view,
                                           const InfAclMask* mask)
{
  InfGtkAclSheetViewPrivate* priv;
  GtkTreeModel* model;

  InfAclMask add;
  InfAclMask remove;

  GEnumClass* enum_class;
  guint i;

  GtkTreeIter iter;
  gboolean has_element;
  InfAclSetting setting;

  priv = INF_GTK_ACL_SHEET_VIEW_PRIVATE(view);
  model = GTK_TREE_MODEL(priv->sheet_store);

  g_return_if_fail(INF_GTK_IS_ACL_SHEET_VIEW(view));
  g_return_if_fail(mask != NULL);

  inf_acl_mask_neg(&priv->permission_mask, &add);
  inf_acl_mask_and(mask, &add, &add);

  inf_acl_mask_neg(mask, &remove);
  inf_acl_mask_and(&priv->permission_mask, &remove, &remove);

  if(!inf_acl_mask_empty(&remove))
  {
    has_element = gtk_tree_model_get_iter_first(model, &iter);
    g_assert(has_element == TRUE);

    while(has_element == TRUE)
    {
      gtk_tree_model_get(model, &iter, 1, &setting, -1);
      if(inf_acl_mask_has(&remove, setting))
        has_element = gtk_list_store_remove(priv->sheet_store, &iter);
      else
        has_element = gtk_tree_model_iter_next(model, &iter);
    }
  }

  if(!inf_acl_mask_empty(&add))
  {
    enum_class = G_ENUM_CLASS(g_type_class_ref(INF_TYPE_ACL_SETTING));

    for(i = 0; i < enum_class->n_values; ++i)
    {
      if(inf_acl_mask_has(&add, enum_class->values[i].value))
      {
        gtk_list_store_insert_with_values(
          priv->sheet_store,
          NULL,
          -1,
          0, enum_class->values[i].value_nick,
          1, enum_class->values[i].value,
          -1
        );
      }
    }

    g_type_class_unref(enum_class);
  }

  if(!inf_acl_mask_equal(&priv->permission_mask, mask))
  {
    priv->permission_mask = *mask;
    g_object_notify(G_OBJECT(view), "permission-mask");
  }
}

/**
 * inf_gtk_acl_sheet_view_get_permission_mask:
 * @view: A #InfGtkAclSheetView.
 *
 * Returns a #InfAclMask specifies which permissions are currently being
 * shown by @view.
 *
 * Returns: (transfer none): A #InfAclMask owned by @view. It must not be
 * freed.
 */
const InfAclMask*
inf_gtk_acl_sheet_view_get_permission_mask(InfGtkAclSheetView* view)
{
  g_return_val_if_fail(INF_GTK_IS_ACL_SHEET_VIEW(view), 0);
  return &INF_GTK_ACL_SHEET_VIEW_PRIVATE(view)->permission_mask;
}

/* vim:set et sw=2 ts=2: */
