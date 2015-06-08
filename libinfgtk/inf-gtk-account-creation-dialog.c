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

/**
 * SECTION:inf-gtk-account-creation-dialog
 * @title: InfGtkAccountCreationDialog
 * @short_description: A dialog to request a new account on a server.
 * @include: libinfgtk/inf-gtk-account-creation-dialog.h
 * @stability: Unstable
 *
 * #InfGtkAccountCreationDialog is a dialog widget which allows to request
 * creation of a new account on an infinote server. If the
 * "can-create-acl-account" permission is not granted, the dialog shows an
 * error message instead.
 **/

#include <libinfgtk/inf-gtk-account-creation-dialog.h>
#include <libinfinity/common/inf-request-result.h>
#include <libinfinity/common/inf-async-operation.h>
#include <libinfinity/common/inf-cert-util.h>
#include <libinfinity/common/inf-error.h>
#include <libinfinity/inf-i18n.h>
#include <libinfinity/inf-signals.h>

#include <gnutls/x509.h>
#include <string.h>

typedef struct _InfGtkAccountCreationDialogPrivate
  InfGtkAccountCreationDialogPrivate;
struct _InfGtkAccountCreationDialogPrivate {
  InfIo* io;
  InfBrowser* browser;

  GtkWidget* status_text;
  GtkWidget* create_account_button;
  GtkWidget* name_entry;

  InfAsyncOperation* key_generator;
  InfRequest* create_account_request;

  gnutls_x509_privkey_t key;
};

typedef struct _InfGtkAccountCreationDialogKeygenResult
  InfGtkAccountCreationDialogKeygenResult;
struct _InfGtkAccountCreationDialogKeygenResult {
  gnutls_x509_privkey_t key;
  GError* error;
};

enum {
  PROP_0,

  PROP_IO,
  PROP_BROWSER
};

enum {
  ACCOUNT_CREATED,

  LAST_SIGNAL
};

#define INF_GTK_ACCOUNT_CREATION_DIALOG_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_GTK_TYPE_ACCOUNT_CREATION_DIALOG, InfGtkAccountCreationDialogPrivate))

static guint account_creation_dialog_signals[LAST_SIGNAL];

G_DEFINE_TYPE_WITH_CODE(InfGtkAccountCreationDialog, inf_gtk_account_creation_dialog, GTK_TYPE_DIALOG,
  G_ADD_PRIVATE(InfGtkAccountCreationDialog))

static void
inf_gtk_account_creation_dialog_keygen_result_free(gpointer data)
{
  InfGtkAccountCreationDialogKeygenResult* result;
  result = (InfGtkAccountCreationDialogKeygenResult*)data;

  if(result->key != NULL) gnutls_x509_privkey_deinit(result->key);
  if(result->error != NULL) g_error_free(result->error);
  g_slice_free(InfGtkAccountCreationDialogKeygenResult, result);
}

static int
inf_gtk_account_creation_dialog_fill_crq(gnutls_x509_crq_t crq,
                                         gnutls_x509_privkey_t key,
                                         const gchar* name)
{
  int res;

  res = gnutls_x509_crq_set_key(crq, key);
  if(res != GNUTLS_E_SUCCESS) return res;

  res = gnutls_x509_crq_set_key_usage(crq, GNUTLS_KEY_DIGITAL_SIGNATURE);
  if(res != GNUTLS_E_SUCCESS) return res;

  res = gnutls_x509_crq_set_version(crq, 3);
  if(res != GNUTLS_E_SUCCESS) return res;

  res = gnutls_x509_crq_set_dn_by_oid(
    crq,
    GNUTLS_OID_X520_COMMON_NAME,
    0,
    name,
    strlen(name)
  );
  if(res != GNUTLS_E_SUCCESS) return res;

  /* gnutls_x509_crq_sign2 is deprecated in favor of
   * gnutls_x509_crq_privkey_sign, but the latter returns the
   * error code GNUTLS_E_UNIMPLEMENTED_FEATURE, so we keep using
   * the deprecated version for now. */
  res = gnutls_x509_crq_sign2(crq, key, GNUTLS_DIG_SHA1, 0);
  if(res != GNUTLS_E_SUCCESS) return res;

  return GNUTLS_E_SUCCESS;
}

static gnutls_x509_crq_t
inf_gtk_account_creation_dialog_create_crq(gnutls_x509_privkey_t key,
                                           const gchar* name,
                                           GError** error)
{
  int res;
  gnutls_x509_crq_t crq;

  res = gnutls_x509_crq_init(&crq);
  if(res != GNUTLS_E_SUCCESS)
  {
    inf_gnutls_set_error(error, res);
    return NULL;
  }

  res = inf_gtk_account_creation_dialog_fill_crq(crq, key, name);
  if(res != GNUTLS_E_SUCCESS)
  {
    gnutls_x509_crq_deinit(crq);
    inf_gnutls_set_error(error, res);
    return NULL;
  }

  return crq;
}

static void
inf_gtk_account_creation_dialog_set_activatable(
  InfGtkAccountCreationDialog* dialog,
  gboolean activatable)
{
  InfGtkAccountCreationDialogPrivate* priv;
  priv = INF_GTK_ACCOUNT_CREATION_DIALOG_PRIVATE(dialog);

  if(activatable)
  {
    /*gtk_widget_set_sensitive(priv->name_entry, TRUE);*/
    gtk_widget_set_sensitive(priv->create_account_button, TRUE);
  }
  else
  {
    /*gtk_widget_set_sensitive(priv->name_entry, FALSE);*/
    gtk_widget_set_sensitive(priv->create_account_button, FALSE);
  }
}

static void
inf_gtk_account_creation_dialog_update(InfGtkAccountCreationDialog* dialog,
                                       const GError* error)
{
  InfGtkAccountCreationDialogPrivate* priv;
  InfBrowserStatus status;
  InfBrowserIter iter;
  InfAclMask perms;
  const gchar* text;
  gchar* markup;
  gboolean activatable;
  const InfAclAccount* local_account;

  priv = INF_GTK_ACCOUNT_CREATION_DIALOG_PRIVATE(dialog);
  activatable = TRUE;

  text = NULL;
  if(priv->browser == NULL)
  {
    text = _("Not connected to a server");
    activatable = FALSE;
  }
  else
  {
    g_object_get(G_OBJECT(priv->browser), "status", &status, NULL);
    if(status != INF_BROWSER_OPEN)
    {
      text = _("Not connected to a server");
      activatable = FALSE;
    }
    else
    {
      inf_browser_get_root(priv->browser, &iter);
      inf_acl_mask_set1(&perms, INF_ACL_CAN_CREATE_ACCOUNT);

      local_account = inf_browser_get_acl_local_account(priv->browser);

      inf_browser_check_acl(
        priv->browser,
        &iter,
        local_account != NULL ? local_account->id : 0,
        &perms,
        &perms
      );

      if(!inf_acl_mask_has(&perms, INF_ACL_CAN_CREATE_ACCOUNT))
      {
        text = _("Permissions to create an account are not granted");
        activatable = FALSE;
      }
    }
  }

  if(error != NULL)
  {
    g_assert(priv->create_account_request == NULL);
    g_assert(priv->key_generator == NULL);

    if(priv->key != NULL)
    {
      gnutls_x509_privkey_deinit(priv->key);
      priv->key = NULL;
    }

    /* Overwrite text with error message */
    text = error->message;
  }

  if(activatable == TRUE)
    if(gtk_entry_get_text(GTK_ENTRY(priv->name_entry))[0] == '\0')
      activatable = FALSE;

  if(text != NULL)
  {
    markup = g_markup_printf_escaped("<span color=\"red\">%s</span>", text);
    gtk_label_set_markup(GTK_LABEL(priv->status_text), markup);
    g_free(markup);

    inf_gtk_account_creation_dialog_set_activatable(dialog, activatable);
  }
  else
  {
    if(priv->key_generator != NULL)
    {
      gtk_label_set_text(
        GTK_LABEL(priv->status_text),
        _("A private key is being generated. "
          "This process might take a few seconds...")
      );

      inf_gtk_account_creation_dialog_set_activatable(dialog, FALSE);
    }
    else if(priv->create_account_request != NULL)
    {
      gtk_label_set_text(
        GTK_LABEL(priv->status_text),
        _("New account is being requested from the server. "
          "Usually, this should not take very long.")
      );

      inf_gtk_account_creation_dialog_set_activatable(dialog, FALSE);
    }
    else
    {
      inf_gtk_account_creation_dialog_set_activatable(dialog, activatable);
    }
  }
}

static void
inf_gtk_account_creation_dialog_entry_changed_cb(GtkEntry* entry,
                                                 gpointer user_data)
{
  InfGtkAccountCreationDialog* dialog;
  dialog = INF_GTK_ACCOUNT_CREATION_DIALOG(user_data);

  inf_gtk_account_creation_dialog_update(dialog, NULL);
}

static void
inf_gtk_account_creation_dialog_create_account_finished_cb(
  InfRequest* request,
  const InfRequestResult* result,
  const GError* error,
  gpointer user_data)
{
  InfGtkAccountCreationDialog* dialog;
  InfGtkAccountCreationDialogPrivate* priv;
  const InfAclAccount* account;
  InfCertificateChain* chain;
  gnutls_x509_privkey_t key;

  dialog = INF_GTK_ACCOUNT_CREATION_DIALOG(user_data);
  priv = INF_GTK_ACCOUNT_CREATION_DIALOG_PRIVATE(dialog);

  g_assert(priv->key != NULL);
  if(priv->create_account_request != NULL)
  {
    g_object_unref(priv->create_account_request);
    priv->create_account_request = NULL;
  }

  if(error != NULL)
  {
    inf_gtk_account_creation_dialog_update(dialog, error);
  }
  else
  {
    inf_request_result_get_create_acl_account(
      result,
      NULL,
      &account,
      &chain
    );

    key = priv->key;
    priv->key = NULL;

    inf_gtk_account_creation_dialog_update(dialog, NULL);

    g_signal_emit(
      G_OBJECT(dialog),
      account_creation_dialog_signals[ACCOUNT_CREATED],
      0,
      key,
      chain,
      account
    );

    gnutls_x509_privkey_deinit(key);
  }
}

static void
inf_gtk_account_creation_dialog_keygen_run(gpointer* run_data,
                                           GDestroyNotify* run_notify,
                                           gpointer user_data)
{
  InfGtkAccountCreationDialogKeygenResult* result;

  result = g_slice_new(InfGtkAccountCreationDialogKeygenResult);

  result->error = NULL;
  result->key = inf_cert_util_create_private_key(
    GNUTLS_PK_RSA,
    4096,
    &result->error
  );

  *run_data = result;
  *run_notify = inf_gtk_account_creation_dialog_keygen_result_free;
}

static void
inf_gtk_account_creation_dialog_keygen_done(gpointer run_data,
                                            gpointer user_data)
{
  InfGtkAccountCreationDialog* dialog;
  InfGtkAccountCreationDialogPrivate* priv;
  InfGtkAccountCreationDialogKeygenResult* result;
  gnutls_x509_crq_t crq;
  InfRequest* request;

  dialog = INF_GTK_ACCOUNT_CREATION_DIALOG(user_data);
  priv = INF_GTK_ACCOUNT_CREATION_DIALOG_PRIVATE(dialog);
  result = (InfGtkAccountCreationDialogKeygenResult*)run_data;

  priv->key_generator = NULL;

  if(result->key != NULL)
  {
    priv->key = result->key;
    result->key = NULL;

    g_assert(priv->create_account_request == NULL);
    g_assert(result->error == NULL);

    crq = inf_gtk_account_creation_dialog_create_crq(
      priv->key,
      gtk_entry_get_text(GTK_ENTRY(priv->name_entry)),
      &result->error
    );

    if(crq != NULL)
    {
      request = inf_browser_create_acl_account(
        priv->browser,
        crq,
        inf_gtk_account_creation_dialog_create_account_finished_cb,
        dialog
      );

      gnutls_x509_crq_deinit(crq);

      if(request != NULL)
      {
        priv->create_account_request = request;
        g_object_ref(priv->create_account_request);

        inf_gtk_account_creation_dialog_update(dialog, NULL);
      }
    }
    else
    {
      inf_gtk_account_creation_dialog_update(dialog, result->error);
    }
  }
  else
  {
    inf_gtk_account_creation_dialog_update(dialog, result->error);
  }
}

static void
inf_gtk_account_creation_dialog_generate_clicked_cb(GtkButton* button,
                                                    gpointer user_data)
{
  InfGtkAccountCreationDialog* dialog;
  InfGtkAccountCreationDialogPrivate* priv;
  GError* error;

  dialog = INF_GTK_ACCOUNT_CREATION_DIALOG(user_data);
  priv = INF_GTK_ACCOUNT_CREATION_DIALOG_PRIVATE(dialog);

  g_assert(priv->io != NULL);
  g_assert(priv->key_generator == NULL);

  priv->key_generator = inf_async_operation_new(
    priv->io,
    inf_gtk_account_creation_dialog_keygen_run,
    inf_gtk_account_creation_dialog_keygen_done,
    dialog
  );

  error = NULL;
  inf_async_operation_start(priv->key_generator, &error);

  if(error != NULL)
  {
    priv->key_generator = NULL;
    inf_gtk_account_creation_dialog_update(dialog, error);
    g_error_free(error);
  }
  else
  {
    inf_gtk_account_creation_dialog_update(dialog, NULL);
  }
}

static void
inf_gtk_account_creation_dialog_acl_changed_cb(InfBrowser* browser,
                                               const InfBrowserIter* iter,
                                               const InfAclSheetSet* sheetset,
                                               InfRequest* request,
                                               gpointer user_data)
{
  InfGtkAccountCreationDialog* dialog;
  InfBrowserIter root_iter;

  dialog = INF_GTK_ACCOUNT_CREATION_DIALOG(user_data);
  inf_browser_get_root(browser, &root_iter);

  if(root_iter.node_id == iter->node_id && root_iter.node == iter->node)
    inf_gtk_account_creation_dialog_update(dialog, NULL);
}

static void
inf_gtk_account_creation_dialog_notify_status_cb(GObject* object,
                                                 GParamSpec* pspec,
                                                 gpointer user_data)
{
  InfGtkAccountCreationDialog* dialog;
  dialog = INF_GTK_ACCOUNT_CREATION_DIALOG(user_data);

  inf_gtk_account_creation_dialog_update(dialog, NULL);
}

/*
 * GObject virtual functions
 */

static void
inf_gtk_account_creation_dialog_init(InfGtkAccountCreationDialog* dialog)
{
  InfGtkAccountCreationDialogPrivate* priv;
  priv = INF_GTK_ACCOUNT_CREATION_DIALOG_PRIVATE(dialog);

  priv->io = NULL;
  priv->browser = NULL;
  priv->key_generator = NULL;
  priv->create_account_request = NULL;
  priv->key = NULL;

  gtk_widget_init_template(GTK_WIDGET(dialog));

  gtk_widget_grab_default(priv->create_account_button);
}

static void
inf_gtk_account_creation_dialog_constructed(GObject* object)
{
  G_OBJECT_CLASS(inf_gtk_account_creation_dialog_parent_class)->constructed(
    object
  );

  inf_gtk_account_creation_dialog_update(
    INF_GTK_ACCOUNT_CREATION_DIALOG(object),
    NULL
  );
}

static void
inf_gtk_account_creation_dialog_dispose(GObject* object)
{
  InfGtkAccountCreationDialog* dialog;
  InfGtkAccountCreationDialogPrivate* priv;

  dialog = INF_GTK_ACCOUNT_CREATION_DIALOG(object);
  priv = INF_GTK_ACCOUNT_CREATION_DIALOG_PRIVATE(dialog);

  if(priv->browser != NULL)
  {
    inf_gtk_account_creation_dialog_set_browser(dialog, NULL);
  }

  g_assert(priv->key == NULL);

  G_OBJECT_CLASS(inf_gtk_account_creation_dialog_parent_class)->dispose(object);
}


static void
inf_gtk_account_creation_dialog_finalize(GObject* object)
{
  InfGtkAccountCreationDialog* dialog;
  InfGtkAccountCreationDialogPrivate* priv;

  dialog = INF_GTK_ACCOUNT_CREATION_DIALOG(object);
  priv = INF_GTK_ACCOUNT_CREATION_DIALOG_PRIVATE(dialog);

  G_OBJECT_CLASS(inf_gtk_account_creation_dialog_parent_class)->finalize(object);
}

static void
inf_gtk_account_creation_dialog_set_property(GObject* object,
                                             guint prop_id,
                                             const GValue* value,
                                             GParamSpec* pspec)
{
  InfGtkAccountCreationDialog* dialog;
  InfGtkAccountCreationDialogPrivate* priv;

  dialog = INF_GTK_ACCOUNT_CREATION_DIALOG(object);
  priv = INF_GTK_ACCOUNT_CREATION_DIALOG_PRIVATE(dialog);

  switch(prop_id)
  {
  case PROP_IO:
    g_assert(priv->io == NULL); /* construct/only */
    priv->io = INF_IO(g_value_dup_object(value));
    break;
  case PROP_BROWSER:
    inf_gtk_account_creation_dialog_set_browser(
      dialog,
      INF_BROWSER(g_value_get_object(value))
    );

    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_gtk_account_creation_dialog_get_property(GObject* object,
                                             guint prop_id,
                                             GValue* value,
                                             GParamSpec* pspec)
{
  InfGtkAccountCreationDialog* dialog;
  InfGtkAccountCreationDialogPrivate* priv;

  dialog = INF_GTK_ACCOUNT_CREATION_DIALOG(object);
  priv = INF_GTK_ACCOUNT_CREATION_DIALOG_PRIVATE(dialog);

  switch(prop_id)
  {
  case PROP_IO:
    g_value_set_object(value, priv->io);
    break;
  case PROP_BROWSER:
    g_value_set_object(value, priv->browser);
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
inf_gtk_account_creation_dialog_class_init(
  InfGtkAccountCreationDialogClass* account_creation_dialog_class)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(account_creation_dialog_class);

  object_class->constructed = inf_gtk_account_creation_dialog_constructed;
  object_class->dispose = inf_gtk_account_creation_dialog_dispose;
  object_class->finalize = inf_gtk_account_creation_dialog_finalize;
  object_class->set_property = inf_gtk_account_creation_dialog_set_property;
  object_class->get_property = inf_gtk_account_creation_dialog_get_property;

  account_creation_dialog_class->account_created = NULL;

  gtk_widget_class_set_template_from_resource(
    GTK_WIDGET_CLASS(object_class),
    "/de/0x539/libinfgtk/ui/infgtkaccountcreationdialog.ui"
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(object_class),
    InfGtkAccountCreationDialog,
    status_text
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(object_class),
    InfGtkAccountCreationDialog,
    create_account_button
  );

  gtk_widget_class_bind_template_child_private(
    GTK_WIDGET_CLASS(object_class),
    InfGtkAccountCreationDialog,
    name_entry
  );

  gtk_widget_class_bind_template_callback(
    GTK_WIDGET_CLASS(object_class),
    inf_gtk_account_creation_dialog_entry_changed_cb
  );

  gtk_widget_class_bind_template_callback(
    GTK_WIDGET_CLASS(object_class),
    inf_gtk_account_creation_dialog_generate_clicked_cb
  );

  g_object_class_install_property(
    object_class,
    PROP_IO,
    g_param_spec_object(
      "io",
      "Io",
      "The InfIo object to schedule asynchronous operations",
      INF_TYPE_IO,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_BROWSER,
    g_param_spec_object(
      "browser",
      "Browser",
      "The infinote directory for which to create an account",
      INF_TYPE_BROWSER,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT
    )
  );

  /**
   * InfGtkAccountCreationDialog::account-created:
   * @dialog: The #InfGtkAccountCreationDialog emitting the signal.
   * @key: A newly generated private key that the user certificate is
   * signed with.
   * @certificate: A certificate signed by the server associated to the new
   * account.
   * @account: The newly created account.
   *
   * This signal is emitted whenever a new account has been created with the
   * dialog. Along with the created account, the login credentials are
   * provided. Note that the private key is owned by the dialog, and will be
   * deleted after the signal was emitted.
   */
  account_creation_dialog_signals[ACCOUNT_CREATED] = g_signal_new(
    "account-created",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfGtkAccountCreationDialogClass, account_created),
    NULL, NULL,
    NULL,
    G_TYPE_NONE,
    3,
    G_TYPE_POINTER, /* gnutls_x509_privkey_t */
    INF_TYPE_CERTIFICATE_CHAIN | G_SIGNAL_TYPE_STATIC_SCOPE,
    INF_TYPE_ACL_ACCOUNT | G_SIGNAL_TYPE_STATIC_SCOPE
  );
}

/*
 * Public API.
 */

/**
 * inf_gtk_account_creation_dialog_new: (constructor)
 * @parent: Parent #GtkWindow of the dialog.
 * @dialog_flags: Flags for the dialog, see #GtkDialogFlags.
 * @io: A #InfIo object to schedule asynchronous operations.
 * @browser: The #InfBrowser for which to create a new account.
 *
 * Creates a new #InfGtkAccountCreationDialog, which can be used to generate
 * a new account on the infinote directory represented by the given browser.
 *
 * Returns: (transfer full): A new #InfGtkAccountCreationDialog. Free with
 * gtk_widget_destroy() when no longer needed.
 */
InfGtkAccountCreationDialog*
inf_gtk_account_creation_dialog_new(GtkWindow* parent,
                                    GtkDialogFlags dialog_flags,
                                    InfIo* io,
                                    InfBrowser* browser)
{
  GObject* object;

  g_return_val_if_fail(parent == NULL || GTK_IS_WINDOW(parent), NULL);
  g_return_val_if_fail(INF_IS_IO(io), NULL);
  g_return_val_if_fail(browser == NULL || INF_IS_BROWSER(browser), NULL);

  object = g_object_new(
    INF_GTK_TYPE_ACCOUNT_CREATION_DIALOG,
    "io", io,
    "browser", browser,
    NULL
  );

  if(dialog_flags & GTK_DIALOG_MODAL)
    gtk_window_set_modal(GTK_WINDOW(object), TRUE);

  if(dialog_flags & GTK_DIALOG_DESTROY_WITH_PARENT)
    gtk_window_set_destroy_with_parent(GTK_WINDOW(object), TRUE);

  gtk_window_set_transient_for(GTK_WINDOW(object), parent);
  return INF_GTK_ACCOUNT_CREATION_DIALOG(object);
}

/**
 * inf_gtk_account_creation_dialog_set_browser:
 * @dlg: A #InfGtkAccountCreationDialog.
 * @browser: The #InfBrowser for which to create a new account, or %NULL.
 *
 * Changes the browser for which to create a new account.
 */
void
inf_gtk_account_creation_dialog_set_browser(InfGtkAccountCreationDialog* dlg,
                                            InfBrowser* browser)
{
  InfGtkAccountCreationDialogPrivate* priv;
  GSList* item;

  g_return_if_fail(INF_GTK_IS_ACCOUNT_CREATION_DIALOG(dlg));
  g_return_if_fail(browser == NULL || INF_IS_BROWSER(browser));

  priv = INF_GTK_ACCOUNT_CREATION_DIALOG_PRIVATE(dlg);

  if(priv->browser != NULL)
  {
    if(priv->key != NULL)
    {
      gnutls_x509_privkey_deinit(priv->key);
      priv->key = NULL;
    }

    if(priv->key_generator != NULL)
    {
      inf_async_operation_free(priv->key_generator);
      priv->key_generator = NULL;
    }

    if(priv->create_account_request != NULL)
    {
      inf_signal_handlers_disconnect_by_func(
        priv->create_account_request,
        G_CALLBACK(
          inf_gtk_account_creation_dialog_create_account_finished_cb
        ),
        dlg
      );

      g_object_unref(priv->create_account_request);
      priv->create_account_request = NULL;
    }

    inf_signal_handlers_disconnect_by_func(
      priv->browser,
      G_CALLBACK(inf_gtk_account_creation_dialog_acl_changed_cb),
      dlg
    );

    inf_signal_handlers_disconnect_by_func(
      priv->browser,
      G_CALLBACK(inf_gtk_account_creation_dialog_notify_status_cb),
      dlg
    );

    g_object_unref(priv->browser);
  }

  priv->browser = browser;

  if(priv->browser != NULL)
  {
    g_object_ref(priv->browser);

    g_signal_connect(
      G_OBJECT(priv->browser),
      "acl-changed",
      G_CALLBACK(inf_gtk_account_creation_dialog_acl_changed_cb),
      dlg
    );

    g_signal_connect(
      G_OBJECT(priv->browser),
      "notify::status",
      G_CALLBACK(inf_gtk_account_creation_dialog_notify_status_cb),
      dlg
    );
  }

  g_object_notify(G_OBJECT(dlg), "browser");
  inf_gtk_account_creation_dialog_update(dlg, NULL);
}

/* vim:set et sw=2 ts=2: */
