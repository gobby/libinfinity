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

#ifndef __INF_GTK_ACL_SHEET_VIEW_H__
#define __INF_GTK_ACL_SHEET_VIEW_H__

#include <libinfinity/common/inf-acl.h>

#include <gtk/gtk.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_GTK_TYPE_ACL_SHEET_VIEW                 (inf_gtk_acl_sheet_view_get_type())
#define INF_GTK_ACL_SHEET_VIEW(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_GTK_TYPE_ACL_SHEET_VIEW, InfGtkAclSheetView))
#define INF_GTK_ACL_SHEET_VIEW_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_GTK_TYPE_ACL_SHEET_VIEW, InfGtkAclSheetViewClass))
#define INF_GTK_IS_ACL_SHEET_VIEW(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_GTK_TYPE_ACL_SHEET_VIEW))
#define INF_GTK_IS_ACL_SHEET_VIEW_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_GTK_TYPE_ACL_SHEET_VIEW))
#define INF_GTK_ACL_SHEET_VIEW_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_GTK_TYPE_ACL_SHEET_VIEW, InfGtkAclSheetViewClass))

typedef struct _InfGtkAclSheetView InfGtkAclSheetView;
typedef struct _InfGtkAclSheetViewClass InfGtkAclSheetViewClass;

/**
 * InfGtkAclSheetViewClass:
 * @sheet_changed: Default signal handler for the
 * #InfGtkAclSheetView::sheet-changed signal.
 *
 * This structure contains the default signal handlers for the
 * #InfGtkAclSheetView class.
 */
struct _InfGtkAclSheetViewClass {
  /*< private >*/
  GtkVBoxClass parent_class;

  /*< public >*/
  void (*sheet_changed)(InfGtkAclSheetView* view);
};

/**
 * InfGtkAclSheetView:
 *
 * #InfGtkAclSheetView is an opaque data type. You should only access it via the
 * public API functions.
 */
struct _InfGtkAclSheetView {
  /*< private >*/
  GtkVBox parent;
};

GType
inf_gtk_acl_sheet_view_get_type(void) G_GNUC_CONST;

GtkWidget*
inf_gtk_acl_sheet_view_new(void);

void
inf_gtk_acl_sheet_view_set_sheet(InfGtkAclSheetView* view,
                                 const InfAclSheet* sheet);

const InfAclSheet*
inf_gtk_acl_sheet_view_get_sheet(InfGtkAclSheetView* view);

void
inf_gtk_acl_sheet_view_set_editable(InfGtkAclSheetView* view,
                                    gboolean editable);

gboolean
inf_gtk_acl_sheet_view_get_editable(InfGtkAclSheetView* view);

void
inf_gtk_acl_sheet_view_set_show_default(InfGtkAclSheetView* view,
                                        gboolean show);

gboolean
inf_gtk_acl_sheet_view_get_show_default(InfGtkAclSheetView* view);

void
inf_gtk_acl_sheet_view_set_permission_mask(InfGtkAclSheetView* view,
                                           const InfAclMask* mask);

const InfAclMask*
inf_gtk_acl_sheet_view_get_permission_mask(InfGtkAclSheetView* view);

G_END_DECLS

#endif /* __INF_GTK_ACL_SHEET_VIEW_H__ */

/* vim:set et sw=2 ts=2: */
