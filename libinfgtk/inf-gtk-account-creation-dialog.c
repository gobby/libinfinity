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
#include <libinfinity/inf-marshal.h>
#include <libinfinity/inf-signals.h>

#include <gnutls/x509.h>
#include <string.h>

typedef struct _InfGtkAccountCreationDialogPrivate
  InfGtkAccountCreationDialogPrivate;
struct _InfGtkAccountCreationDialogPrivate {
  InfIo* io;
  InfBrowser* browser;

  GtkWidget* status_text;
  GtkWidget* generate_button;
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

static GtkDialogClass* parent_class;
static guint account_creation_dialog_signals[LAST_SIGNAL];

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
    gtk_widget_set_sensitive(priv->generate_button, TRUE);
  }
  else
  {
    /*gtk_widget_set_sensitive(priv->name_entry, FALSE);*/
    gtk_widget_set_sensitive(priv->generate_button, FALSE);
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
inf_gtk_account_creation_dialog_init(GTypeInstance* instance,
                                     gpointer g_class)
{
  InfGtkAccountCreationDialog* dialog;
  InfGtkAccountCreationDialogPrivate* priv;

  GtkWidget* name_label;
  GtkWidget* box;
  GtkWidget* vbox;
  GtkWidget* image;
  GtkWidget* imagebox;
  GtkWidget* dialog_vbox;

  dialog = INF_GTK_ACCOUNT_CREATION_DIALOG(instance);
  priv = INF_GTK_ACCOUNT_CREATION_DIALOG_PRIVATE(dialog);

  priv->browser = NULL;
  priv->key_generator = NULL;
  priv->create_account_request = NULL;
  priv->key = NULL;

  name_label = gtk_label_new(_("Account Name:"));
  gtk_widget_show(name_label);

  priv->name_entry = gtk_entry_new();
  gtk_entry_set_activates_default(GTK_ENTRY(priv->name_entry), TRUE);
  gtk_widget_show(priv->name_entry);

  g_signal_connect(
    G_OBJECT(priv->name_entry),
    "changed",
    G_CALLBACK(inf_gtk_account_creation_dialog_entry_changed_cb),
    dialog
  );

  priv->generate_button = gtk_button_new_with_mnemonic(_("Create _Account"));
  gtk_widget_set_sensitive(priv->generate_button, FALSE);
  gtk_widget_show(priv->generate_button);

  g_signal_connect(
    G_OBJECT(priv->generate_button),
    "clicked",
    G_CALLBACK(inf_gtk_account_creation_dialog_generate_clicked_cb),
    dialog
  );

  box = gtk_hbox_new(FALSE, 12);
  gtk_box_pack_start(GTK_BOX(box), name_label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(box), priv->name_entry, FALSE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(box), priv->generate_button, FALSE, FALSE, 0);
  gtk_widget_show(box);

  priv->status_text = gtk_label_new("");
  gtk_misc_set_alignment(GTK_MISC(priv->status_text), 0.0, 0.5);
  gtk_label_set_line_wrap(GTK_LABEL(priv->status_text), TRUE);
  gtk_label_set_max_width_chars(GTK_LABEL(priv->status_text), 50);
  gtk_widget_show(priv->status_text);
  vbox = gtk_vbox_new(FALSE, 12);
  gtk_box_pack_start(GTK_BOX(vbox), box, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), priv->status_text, FALSE, FALSE, 0);
  gtk_widget_show(vbox);

  /* TODO: Make sure that icon name is available, otherwise hide the
   * image in the dialog. */
  image = gtk_image_new_from_icon_name(
    "application-certificate",
    GTK_ICON_SIZE_DIALOG
  );

  gtk_misc_set_alignment(GTK_MISC(image), 0.0, 0.0);
  gtk_widget_show(image);

  imagebox = gtk_hbox_new(FALSE, 12);
  gtk_box_pack_start(GTK_BOX(imagebox), image, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(imagebox), vbox, FALSE, TRUE, 0);
  gtk_widget_show(imagebox);

  dialog_vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

  gtk_box_set_spacing(GTK_BOX(dialog_vbox), 12);
  gtk_box_pack_start(GTK_BOX(dialog_vbox), imagebox, FALSE, FALSE, 0);

  gtk_container_set_border_width(GTK_CONTAINER(dialog), 12);
  gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);

  gtk_widget_set_can_default(priv->generate_button, TRUE);
  gtk_widget_grab_default(priv->generate_button);

  gtk_window_set_title(GTK_WINDOW(dialog), _("Create New Account"));
}

static GObject*
inf_gtk_account_creation_dialog_constructor(GType type,
                                            guint n_properties,
                                            GObjectConstructParam* properties)
{
  GObject* object;

  object = G_OBJECT_CLASS(parent_class)->constructor(
    type,
    n_properties,
    properties
  );

  inf_gtk_account_creation_dialog_update(
    INF_GTK_ACCOUNT_CREATION_DIALOG(object),
    NULL
  );

  return object;
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

  G_OBJECT_CLASS(parent_class)->dispose(object);
}


static void
inf_gtk_account_creation_dialog_finalize(GObject* object)
{
  InfGtkAccountCreationDialog* dialog;
  InfGtkAccountCreationDialogPrivate* priv;

  dialog = INF_GTK_ACCOUNT_CREATION_DIALOG(object);
  priv = INF_GTK_ACCOUNT_CREATION_DIALOG_PRIVATE(dialog);

  G_OBJECT_CLASS(parent_class)->finalize(object);
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
inf_gtk_account_creation_dialog_class_init(gpointer g_class,
                                           gpointer class_data)
{
  GObjectClass* object_class;
  InfGtkAccountCreationDialogClass* account_creation_dialog_class;

  object_class = G_OBJECT_CLASS(g_class);
  account_creation_dialog_class =
    INF_GTK_ACCOUNT_CREATION_DIALOG_CLASS(g_class);

  parent_class = GTK_DIALOG_CLASS(g_type_class_peek_parent(g_class));

  g_type_class_add_private(
    g_class,
    sizeof(InfGtkAccountCreationDialogPrivate)
  );

  object_class->constructor = inf_gtk_account_creation_dialog_constructor;
  object_class->dispose = inf_gtk_account_creation_dialog_dispose;
  object_class->finalize = inf_gtk_account_creation_dialog_finalize;
  object_class->set_property = inf_gtk_account_creation_dialog_set_property;
  object_class->get_property = inf_gtk_account_creation_dialog_get_property;

  account_creation_dialog_class->account_created = NULL;

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
    inf_marshal_VOID__POINTER_BOXED_BOXED,
    G_TYPE_NONE,
    3,
    G_TYPE_POINTER, /* gnutls_x509_privkey_t */
    INF_TYPE_CERTIFICATE_CHAIN | G_SIGNAL_TYPE_STATIC_SCOPE,
    INF_TYPE_ACL_ACCOUNT | G_SIGNAL_TYPE_STATIC_SCOPE
  );
}

GType
inf_gtk_account_creation_dialog_get_type(void)
{
  static GType account_creation_dialog_type = 0;

  if(!account_creation_dialog_type)
  {
    static const GTypeInfo account_creation_dialog_type_info = {
      sizeof(InfGtkAccountCreationDialogClass),    /* class_size */
      NULL,                                        /* base_init */
      NULL,                                        /* base_finalize */
      inf_gtk_account_creation_dialog_class_init,  /* class_init */
      NULL,                                        /* class_finalize */
      NULL,                                        /* class_data */
      sizeof(InfGtkAccountCreationDialog),         /* instance_size */
      0,                                           /* n_preallocs */
      inf_gtk_account_creation_dialog_init,        /* instance_init */
      NULL                                         /* value_table */
    };

    account_creation_dialog_type = g_type_register_static(
      GTK_TYPE_DIALOG,
      "InfGtkAccountCreationDialog",
      &account_creation_dialog_type_info,
      0
    );
  }

  return account_creation_dialog_type;
}

/*
 * Public API.
 */

/**
 * inf_gtk_account_creation_dialog_new:
 * @parent: Parent #GtkWindow of the dialog.
 * @dialog_flags: Flags for the dialog, see #GtkDialogFlags.
 * @io: A #InfIo object to schedule asynchronous operations.
 * @browser: The #InfBrowser for which to create a new account.
 *
 * Creates a new #InfGtkAccountCreationDialog, which can be used to generate
 * a new account on the infinote directory represented by the given browser.
 *
 * Returns: A new #InfGtkAccountCreationDialog. Free with gtk_widget_destroy()
 * when no longer needed.
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
