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

#ifndef __INF_GTK_ACCOUNT_CREATION_DIALOG_H__
#define __INF_GTK_ACCOUNT_CREATION_DIALOG_H__

#include <libinfinity/common/inf-browser.h>
#include <libinfinity/common/inf-io.h>

#include <gtk/gtk.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_GTK_TYPE_ACCOUNT_CREATION_DIALOG                 (inf_gtk_account_creation_dialog_get_type())
#define INF_GTK_ACCOUNT_CREATION_DIALOG(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_GTK_TYPE_ACCOUNT_CREATION_DIALOG, InfGtkAccountCreationDialog))
#define INF_GTK_ACCOUNT_CREATION_DIALOG_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_GTK_TYPE_ACCOUNT_CREATION_DIALOG, InfGtkAccountCreationDialogClass))
#define INF_GTK_IS_ACCOUNT_CREATION_DIALOG(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_GTK_TYPE_ACCOUNT_CREATION_DIALOG))
#define INF_GTK_IS_ACCOUNT_CREATION_DIALOG_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_GTK_TYPE_ACCOUNT_CREATION_DIALOG))
#define INF_GTK_ACCOUNT_CREATION_DIALOG_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_GTK_TYPE_ACCOUNT_CREATION_DIALOG, InfGtkAccountCreationDialogClass))

typedef struct _InfGtkAccountCreationDialog InfGtkAccountCreationDialog;
typedef struct _InfGtkAccountCreationDialogClass InfGtkAccountCreationDialogClass;

/**
 * InfGtkAccountCreationDialogClass:
 * @account_created: Default signal handler for the
 * #InfGtkAccountCreationDialog::account-created signal.
 *
 * This structure contains the default signal handlers for the
 * #InfGtkAccountCreationDialog class.
 */
struct _InfGtkAccountCreationDialogClass {
  /*< private >*/
  GtkDialogClass parent_class;

  /*< public >*/
  void(*account_created)(InfGtkAccountCreationDialog* dialog,
                         gnutls_x509_privkey_t key,
                         InfCertificateChain* chain,
                         const InfAclAccount* account);
};

/**
 * InfGtkAccountCreationDialog:
 *
 * #InfGtkAccountCreationDialog is an opaque data type. You should only access
 * it via the public API functions.
 */
struct _InfGtkAccountCreationDialog {
  /*< private >*/
  GtkDialog parent;
};

GType
inf_gtk_account_creation_dialog_get_type(void) G_GNUC_CONST;

InfGtkAccountCreationDialog*
inf_gtk_account_creation_dialog_new(GtkWindow* parent,
                                    GtkDialogFlags dialog_flags,
                                    InfIo* io,
                                    InfBrowser* browser);

void
inf_gtk_account_creation_dialog_set_browser(InfGtkAccountCreationDialog* dlg,
                                            InfBrowser* browser);

G_END_DECLS

#endif /* __INF_GTK_ACCOUNT_CREATION_DIALOG_H__ */

/* vim:set et sw=2 ts=2: */
