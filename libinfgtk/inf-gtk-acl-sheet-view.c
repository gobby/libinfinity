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

#include <libinfinity/inf-marshal.h>

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
  GtkListStore* store;
  GtkWidget* treeview;
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

static GtkVBoxClass* parent_class;
static guint acl_sheet_view_signals[LAST_SIGNAL];

static void
inf_gtk_acl_sheet_view_default_toggled_cb(GtkCellRenderer* cell,
                                          const gchar* path_str,
                                          gpointer user_data)
{
  InfGtkAclSheetView* view;
  InfGtkAclSheetViewPrivate* priv;
  GtkTreeIter iter;
  gboolean result;
  InfAclSetting setting;

  view = INF_GTK_ACL_SHEET_VIEW(user_data);
  priv = INF_GTK_ACL_SHEET_VIEW_PRIVATE(view);

  result = gtk_tree_model_get_iter_from_string(
    GTK_TREE_MODEL(priv->store),
    &iter,
    path_str
  );

  g_assert(result == TRUE);
  g_assert(priv->sheet != NULL);

  gtk_tree_model_get(GTK_TREE_MODEL(priv->store), &iter, 1, &setting, -1);
  

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
  GtkTreeIter iter;
  gboolean result;
  InfAclSetting setting;

  view = INF_GTK_ACL_SHEET_VIEW(user_data);
  priv = INF_GTK_ACL_SHEET_VIEW_PRIVATE(view);

  result = gtk_tree_model_get_iter_from_string(
    GTK_TREE_MODEL(priv->store),
    &iter,
    path_str
  );

  g_assert(result == TRUE);
  g_assert(priv->sheet != NULL);

  gtk_tree_model_get(GTK_TREE_MODEL(priv->store), &iter, 1, &setting, -1);

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
  GtkTreeIter iter;
  gboolean result;
  InfAclSetting setting;

  view = INF_GTK_ACL_SHEET_VIEW(user_data);
  priv = INF_GTK_ACL_SHEET_VIEW_PRIVATE(view);

  result = gtk_tree_model_get_iter_from_string(
    GTK_TREE_MODEL(priv->store),
    &iter,
    path_str
  );

  g_assert(result == TRUE);
  g_assert(priv->sheet != NULL);

  gtk_tree_model_get(GTK_TREE_MODEL(priv->store), &iter, 1, &setting, -1);

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

static int
inf_gtk_acl_sheet_view_sort_func(GtkTreeModel* model,
                                 GtkTreeIter* a,
                                 GtkTreeIter* b,
                                 gpointer user_data)
{
  gchar* name_a;
  gchar* name_b;
  int result;

  gtk_tree_model_get(model, a, 0, &name_a, -1);
  gtk_tree_model_get(model, b, 0, &name_b, -1);

  result = g_utf8_collate(name_a, name_b);

  g_free(name_a);
  g_free(name_b);

  return result;
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
    column = gtk_tree_view_get_column(GTK_TREE_VIEW(priv->treeview), i);
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
inf_gtk_acl_sheet_view_init(GTypeInstance* instance,
                            gpointer g_class)
{
  InfGtkAclSheetView* view;
  InfGtkAclSheetViewPrivate* priv;

  GtkTreeSelection* selection;
  GtkTreeViewColumn* column;
  GtkCellRenderer* renderer;
  GtkWidget* scroll;

  view = INF_GTK_ACL_SHEET_VIEW(instance);
  priv = INF_GTK_ACL_SHEET_VIEW_PRIVATE(view);

  priv->store =
    gtk_list_store_new(2, G_TYPE_STRING, INF_TYPE_ACL_SETTING);
  priv->treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(priv->store));
  priv->sheet = NULL;
  priv->editable = FALSE;
  inf_acl_mask_clear(&priv->permission_mask);

  gtk_tree_sortable_set_sort_column_id(
    GTK_TREE_SORTABLE(priv->store),
    0,
    GTK_SORT_ASCENDING
  );

  gtk_tree_sortable_set_sort_func(
    GTK_TREE_SORTABLE(priv->store),
    0,
    inf_gtk_acl_sheet_view_sort_func,
    view,
    NULL
  );

  inf_gtk_acl_sheet_view_set_permission_mask(view, &INF_ACL_MASK_ALL);

  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new();
  gtk_tree_view_column_set_title(column, "Permission");
  gtk_tree_view_column_pack_start(column, renderer, TRUE);
  gtk_tree_view_column_add_attribute(column, renderer, "text", 0);
  gtk_tree_view_append_column(GTK_TREE_VIEW(priv->treeview), column);

  renderer = gtk_cell_renderer_toggle_new();
  g_object_set(renderer, "activatable", FALSE, NULL);

  g_signal_connect(
    G_OBJECT(renderer),
    "toggled",
    G_CALLBACK(inf_gtk_acl_sheet_view_default_toggled_cb),
    view
  );

  gtk_cell_renderer_toggle_set_radio(
    GTK_CELL_RENDERER_TOGGLE(renderer),
    TRUE
  );

  column = gtk_tree_view_column_new();
  gtk_tree_view_column_set_title(column, "Default");
  gtk_tree_view_column_pack_start(column, renderer, FALSE);
  gtk_tree_view_column_set_cell_data_func(
    column,
    renderer,
    inf_gtk_acl_sheet_view_default_cell_data_func,
    view,
    NULL
  );
  gtk_tree_view_append_column(GTK_TREE_VIEW(priv->treeview), column);

  renderer = gtk_cell_renderer_toggle_new();
  g_object_set(renderer, "activatable", FALSE, NULL);

  g_signal_connect(
    G_OBJECT(renderer),
    "toggled",
    G_CALLBACK(inf_gtk_acl_sheet_view_yes_toggled_cb),
    view
  );

  gtk_cell_renderer_toggle_set_radio(
    GTK_CELL_RENDERER_TOGGLE(renderer),
    TRUE
  );

  column = gtk_tree_view_column_new();
  gtk_tree_view_column_set_title(column, "Yes");
  gtk_tree_view_column_pack_start(column, renderer, FALSE);
  gtk_tree_view_column_set_cell_data_func(
    column,
    renderer,
    inf_gtk_acl_sheet_view_yes_cell_data_func,
    view,
    NULL
  );
  gtk_tree_view_append_column(GTK_TREE_VIEW(priv->treeview), column);

  renderer = gtk_cell_renderer_toggle_new();
  g_object_set(renderer, "activatable", FALSE, NULL);

  g_signal_connect(
    G_OBJECT(renderer),
    "toggled",
    G_CALLBACK(inf_gtk_acl_sheet_view_no_toggled_cb),
    view
  );

  gtk_cell_renderer_toggle_set_radio(
    GTK_CELL_RENDERER_TOGGLE(renderer),
    TRUE
  );

  column = gtk_tree_view_column_new();
  gtk_tree_view_column_set_title(column, "No");
  gtk_tree_view_column_pack_start(column, renderer, FALSE);
  gtk_tree_view_column_set_cell_data_func(
    column,
    renderer,
    inf_gtk_acl_sheet_view_no_cell_data_func,
    view,
    NULL
  );
  gtk_tree_view_append_column(GTK_TREE_VIEW(priv->treeview), column);

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->treeview));
  gtk_tree_selection_set_mode(selection, GTK_SELECTION_NONE);

  gtk_widget_show(priv->treeview);

  scroll = gtk_scrolled_window_new(NULL, NULL);

  gtk_scrolled_window_set_shadow_type(
    GTK_SCROLLED_WINDOW(scroll),
    GTK_SHADOW_IN
  );

  gtk_scrolled_window_set_policy(
    GTK_SCROLLED_WINDOW(scroll),
    GTK_POLICY_AUTOMATIC,
    GTK_POLICY_AUTOMATIC
  );

  gtk_widget_set_size_request(scroll, 250, -1);
  gtk_container_add(GTK_CONTAINER(scroll), priv->treeview);
  gtk_widget_show(scroll);

  gtk_box_pack_start(GTK_BOX(view), scroll, TRUE, TRUE, 0);
  gtk_box_set_spacing(GTK_BOX(view), 6);
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

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_gtk_acl_sheet_view_finalize(GObject* object)
{
  InfGtkAclSheetView* view;
  InfGtkAclSheetViewPrivate* priv;

  view = INF_GTK_ACL_SHEET_VIEW(object);
  priv = INF_GTK_ACL_SHEET_VIEW_PRIVATE(view);

  G_OBJECT_CLASS(parent_class)->finalize(object);
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
inf_gtk_acl_sheet_view_class_init(gpointer g_class,
                                  gpointer class_data)
{
  GObjectClass* object_class;
  InfGtkAclSheetViewClass* sheet_view_class;

  object_class = G_OBJECT_CLASS(g_class);
  sheet_view_class = INF_GTK_ACL_SHEET_VIEW_CLASS(object_class);

  parent_class = GTK_VBOX_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfGtkAclSheetViewPrivate));

  object_class->dispose = inf_gtk_acl_sheet_view_dispose;
  object_class->finalize = inf_gtk_acl_sheet_view_finalize;
  object_class->set_property = inf_gtk_acl_sheet_view_set_property;
  object_class->get_property = inf_gtk_acl_sheet_view_get_property;
  sheet_view_class->sheet_changed = NULL;

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
    inf_marshal_VOID__VOID,
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

GType
inf_gtk_acl_sheet_view_get_type(void)
{
  static GType acl_sheet_view_type = 0;

  if(!acl_sheet_view_type)
  {
    static const GTypeInfo acl_sheet_view_type_info = {
      sizeof(InfGtkAclSheetViewClass),     /* class_size */
      NULL,                                /* base_init */
      NULL,                                /* base_finalize */
      inf_gtk_acl_sheet_view_class_init,   /* class_init */
      NULL,                                /* class_finalize */
      NULL,                                /* class_data */
      sizeof(InfGtkAclSheetView),          /* instance_size */
      0,                                   /* n_preallocs */
      inf_gtk_acl_sheet_view_init,         /* instance_init */
      NULL                                 /* value_table */
    };

    acl_sheet_view_type = g_type_register_static(
      GTK_TYPE_VBOX,
      "InfGtkAclSheetView",
      &acl_sheet_view_type_info,
      0
    );
  }

  return acl_sheet_view_type;
}

/*
 * Public API.
 */

/**
 * inf_gtk_acl_sheet_view_new:
 *
 * Creates a new #InfGtkAclSheetView. To show a sheet in the view, call
 * inf_gtk_acl_sheet_view_set_sheet().
 *
 * Returns: A new #InfGtkAclSheetView.
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
 * @sheet: The #InfAclSheet to show.
 *
 * Sets the @sheet to be displayed by @view.
 */
void
inf_gtk_acl_sheet_view_set_sheet(InfGtkAclSheetView* view,
                                 const InfAclSheet* sheet)
{
  InfGtkAclSheetViewPrivate* priv;
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

  if(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(priv->store), &iter))
  {
    path = gtk_tree_path_new_first();

    do
    {
      gtk_tree_model_row_changed(GTK_TREE_MODEL(priv->store), path, &iter);
      gtk_tree_path_next(path);
    } while(gtk_tree_model_iter_next(GTK_TREE_MODEL(priv->store), &iter));

    gtk_tree_path_free(path);
  }
  
  if(priv->sheet == NULL)
    gtk_widget_set_sensitive(priv->treeview, FALSE);
  else
    gtk_widget_set_sensitive(priv->treeview, TRUE);

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
 * Returns: A #InfAclSheet owned by @view, or %NULL.
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
  column = gtk_tree_view_get_column(GTK_TREE_VIEW(priv->treeview), 1);

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
  column = gtk_tree_view_get_column(GTK_TREE_VIEW(priv->treeview), 1);

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

  InfAclMask add;
  InfAclMask remove;

  GEnumClass* enum_class;
  guint i;

  GtkTreeIter iter;
  gboolean has_element;
  InfAclSetting setting;

  priv = INF_GTK_ACL_SHEET_VIEW_PRIVATE(view);

  inf_acl_mask_neg(&priv->permission_mask, &add);
  inf_acl_mask_and(mask, &add, &add);

  inf_acl_mask_neg(mask, &remove);
  inf_acl_mask_and(&priv->permission_mask, &remove, &remove);

  if(!inf_acl_mask_empty(&remove))
  {
    has_element = gtk_tree_model_get_iter_first(
      GTK_TREE_MODEL(priv->store),
      &iter
    );

    g_assert(has_element == TRUE);

    while(has_element == TRUE)
    {
      gtk_tree_model_get(GTK_TREE_MODEL(priv->store), &iter, 1, &setting, -1);
      if(inf_acl_mask_has(&remove, setting))
      {
        has_element = gtk_list_store_remove(priv->store, &iter);
      }
      else
      {
        has_element = gtk_tree_model_iter_next(
          GTK_TREE_MODEL(priv->store),
          &iter
        );
      }
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
          priv->store,
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
 * Returns: A #InfAclMask owned by @view. It must not be freed.
 */
const InfAclMask*
inf_gtk_acl_sheet_view_get_permission_mask(InfGtkAclSheetView* view)
{
  g_return_val_if_fail(INF_GTK_IS_ACL_SHEET_VIEW(view), 0);
  return &INF_GTK_ACL_SHEET_VIEW_PRIVATE(view)->permission_mask;
}

/* vim:set et sw=2 ts=2: */
