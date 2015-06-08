/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2015 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INF_GTK_PERMISSIONS_DIALOG_H__
#define __INF_GTK_PERMISSIONS_DIALOG_H__

#include <libinfinity/common/inf-browser.h>

#include <gtk/gtk.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_GTK_TYPE_PERMISSIONS_DIALOG                 (inf_gtk_permissions_dialog_get_type())
#define INF_GTK_PERMISSIONS_DIALOG(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_GTK_TYPE_PERMISSIONS_DIALOG, InfGtkPermissionsDialog))
#define INF_GTK_PERMISSIONS_DIALOG_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_GTK_TYPE_PERMISSIONS_DIALOG, InfGtkPermissionsDialogClass))
#define INF_GTK_IS_PERMISSIONS_DIALOG(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_GTK_TYPE_PERMISSIONS_DIALOG))
#define INF_GTK_IS_PERMISSIONS_DIALOG_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_GTK_TYPE_PERMISSIONS_DIALOG))
#define INF_GTK_PERMISSIONS_DIALOG_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_GTK_TYPE_PERMISSIONS_DIALOG, InfGtkPermissionsDialogClass))

typedef struct _InfGtkPermissionsDialog InfGtkPermissionsDialog;
typedef struct _InfGtkPermissionsDialogClass InfGtkPermissionsDialogClass;

/**
 * InfGtkPermissionsDialogClass:
 *
 * This structure does not contain any public fields.
 */
struct _InfGtkPermissionsDialogClass {
  /*< private >*/
  GtkDialogClass parent_class;
};

/**
 * InfGtkPermissionsDialog:
 *
 * #InfGtkPermissionsDialog is an opaque data type. You should only access
 * it via the public API functions.
 */
struct _InfGtkPermissionsDialog {
  /*< private >*/
  GtkDialog parent;
};

GType
inf_gtk_permissions_dialog_get_type(void) G_GNUC_CONST;

InfGtkPermissionsDialog*
inf_gtk_permissions_dialog_new(GtkWindow* parent,
                               GtkDialogFlags dialog_flags,
                               InfBrowser* browser,
                               const InfBrowserIter* iter);

void
inf_gtk_permissions_dialog_set_node(InfGtkPermissionsDialog* dialog,
                                    InfBrowser* browser,
                                    const InfBrowserIter* iter);

G_END_DECLS

#endif /* __INF_GTK_PERMISSIONS_DIALOG_H__ */

/* vim:set et sw=2 ts=2: */
