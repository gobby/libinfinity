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

#include <infinoted/infinoted-plugin-manager.h>
#include <infinoted/infinoted-parameter.h>
#include <infinoted/infinoted-log.h>

#include <libinfinity/common/inf-cert-util.h>
#include <libinfinity/common/inf-error.h>
#include <libinfinity/inf-signals.h>
#include <libinfinity/inf-i18n.h>

typedef enum _InfinotedPluginCertificateAuthError {
  INFINOTED_PLUGIN_CERTIFICATE_AUTH_ERROR_NO_CREDENTIALS,
  INFINOTED_PLUGIN_CERTIFICATE_AUTH_ERROR_NO_CAS,
  INFINOTED_PLUGIN_CERTIFICATE_AUTH_ERROR_NO_CA_FOR_KEY,
  INFINOTED_PLUGIN_CERTIFICATE_AUTH_ERROR_NO_CA_KEY
} InfinotedPluginCertificateAuthError;

typedef struct _InfinotedPluginCertificateAuth InfinotedPluginCertificateAuth;
struct _InfinotedPluginCertificateAuth {
  InfinotedPluginManager* manager;

  gchar* ca_list_file;
  gchar* ca_key_file;
  gboolean accept_unauthenticated_clients;
  gchar* super_user;

  gnutls_x509_crt_t* cas;
  guint n_cas;
  gnutls_x509_privkey_t ca_key;
  guint ca_key_index;

  gint verify_flags;

  InfAclAccountId super_id;
  InfRequest* set_acl_request;
};

static GQuark
infinoted_plugin_certificate_auth_error_quark()
{
  return g_quark_from_static_string(
    "INFINOTED_PLUGIN_CERTIFICATE_AUTH_ERROR"
  );
}

static void
infinoted_plugin_certificate_auth_set_acl_cb(InfRequest* request,
                                             const InfRequestResult* result,
                                             const GError* error,
                                             gpointer user_data)
{
  InfinotedPluginCertificateAuth* plugin;
  plugin = (InfinotedPluginCertificateAuth*)user_data;

  if(error != NULL)
  {
    infinoted_log_warning(
      infinoted_plugin_manager_get_log(plugin->manager),
      _("Failed to set permissions for super user: %s"),
      error->message
    );
  }
}

static void
infinoted_plugin_certificate_auth_remove_acl_account_cb(
  InfRequest* request,
  const InfRequestResult* result,
  const GError* error,
  gpointer user_data)
{
  InfinotedPluginCertificateAuth* plugin;
  plugin = (InfinotedPluginCertificateAuth*)user_data;

  if(error != NULL)
  {
    infinoted_log_warning(
      infinoted_plugin_manager_get_log(plugin->manager),
      _("Failed to remove super user on server shutdown. This should not be "
        "a problem since the account is not made persistent, however might "
        "point to an inconsistency in the server setup. The error message "
        "was: %s"),
      error->message
    );
  }
}

static void
infinoted_plugin_certificate_auth_certificate_func(InfXmppConnection* xmpp,
                                                   gnutls_session_t session,
                                                   InfCertificateChain* chain,
                                                   gpointer user_data)
{
  InfinotedPluginCertificateAuth* plugin;
  int res;
  int verify_result;
  GError* error;

  plugin = (InfinotedPluginCertificateAuth*)user_data;

  if(chain != NULL)
  {
    res = gnutls_x509_crt_list_verify(
      inf_certificate_chain_get_raw(chain),
      inf_certificate_chain_get_n_certificates(chain),
      plugin->cas,
      plugin->n_cas,
      NULL,
      0,
      plugin->verify_flags,
      &verify_result
    );

    error = NULL;

    if(res != GNUTLS_E_SUCCESS)
      inf_gnutls_set_error(&error, res);
    else if( (verify_result & GNUTLS_CERT_INVALID) != 0)
      inf_gnutls_certificate_verification_set_error(&error, res);

    if(error != NULL)
    {
      inf_xmpp_connection_certificate_verify_cancel(xmpp, error);
      g_error_free(error);
    }
    else
    {
      inf_xmpp_connection_certificate_verify_continue(xmpp);
    }
  }
  else
  {
    /* If we do not accept unauthenticated clients, then GnuTLS should have
     * blocked the connection already, since we set the certificate request
     * to GNUTLS_CERT_REQUIRE in that case. */
    g_assert(plugin->accept_unauthenticated_clients == TRUE);
    inf_xmpp_connection_certificate_verify_continue(xmpp);
  }
}

static void
infinoted_plugin_certificate_auth_info_initialize(gpointer plugin_info)
{
  InfinotedPluginCertificateAuth* plugin;
  plugin = (InfinotedPluginCertificateAuth*)plugin_info;

  plugin->manager = NULL;

  plugin->ca_list_file = NULL;
  plugin->ca_key_file = NULL;
  plugin->accept_unauthenticated_clients = FALSE;
  plugin->super_user = NULL;

  plugin->cas = NULL;
  plugin->n_cas = 0;
  plugin->ca_key = NULL;
  plugin->ca_key_index = G_MAXUINT;

  /* Note that we don't require client certificates to be signed by a CA,
   * by default. We only require them to be signed by one of the certificates
   * in our list, but we don't care whether that's a CA or not. A common use
   * case is to sign client certificates with our own server certificate. */
  plugin->verify_flags =
    GNUTLS_VERIFY_ALLOW_X509_V1_CA_CRT |
    GNUTLS_VERIFY_DISABLE_CA_SIGN;

  plugin->super_id = 0;
  plugin->set_acl_request = NULL;
}

static gboolean
infinoted_plugin_certificate_auth_initialize(InfinotedPluginManager* manager,
                                             gpointer plugin_info,
                                             GError** error)
{
  InfinotedPluginCertificateAuth* plugin;
  InfCertificateCredentials* creds;
  GPtrArray* read_certs;
  int res;
  guint i;

  gnutls_x509_crt_t* sign_certs;
  InfCertificateChain* sign_chain;

  gnutls_x509_privkey_t super_key;
  InfCertUtilDescription desc;
  gnutls_x509_crt_t super_cert;
  InfAclAccountId super_id;
  gnutls_x509_crt_t chain[2];
  gboolean written;

  InfdDirectory* directory;
  InfBrowserIter iter;
  InfAclSheetSet sheet_set;
  InfAclSheet sheet;
  InfRequest* request;

  plugin = (InfinotedPluginCertificateAuth*)plugin_info;
  plugin->manager = manager;

  creds = infinoted_plugin_manager_get_credentials(manager);
  if(creds == NULL)
  {
    g_set_error(
      error,
      infinoted_plugin_certificate_auth_error_quark(),
      INFINOTED_PLUGIN_CERTIFICATE_AUTH_ERROR_NO_CREDENTIALS,
      "%s",
      _("The certificate-auth plugin can only be used when TLS is enabled "
        "and a server certificate has been set.")
    );

    return FALSE;
  }

  read_certs =
    inf_cert_util_read_certificate(plugin->ca_list_file, NULL, error);
  if(read_certs == NULL) return FALSE;

  if(read_certs->len == 0)
  {
    g_set_error(
      error,
      infinoted_plugin_certificate_auth_error_quark(),
      INFINOTED_PLUGIN_CERTIFICATE_AUTH_ERROR_NO_CAS,
      _("File \"%s\" does not contain any CA certificates"),
      plugin->ca_list_file
    );

    g_ptr_array_free(read_certs, TRUE);
    return FALSE;
  }

  plugin->n_cas = read_certs->len;
  plugin->cas = (gnutls_x509_crt_t*)g_ptr_array_free(read_certs, FALSE);

  res = gnutls_certificate_set_x509_trust(
    inf_certificate_credentials_get(creds),
    plugin->cas,
    plugin->n_cas
  );

  if(res < 0)
  {
    inf_gnutls_set_error(error, res);
    return FALSE;
  }

  if(plugin->ca_key_file != NULL)
  {
    plugin->ca_key =
      inf_cert_util_read_private_key(plugin->ca_key_file, error);
    if(plugin->ca_key == NULL)
      return FALSE;

    /* Walk through certificates and find the certificate that the key
     * belongs to. */
    for(i = 0; i < plugin->n_cas; ++i)
      if(inf_cert_util_check_certificate_key(plugin->cas[i], plugin->ca_key))
        break;

    if(i == plugin->n_cas)
    {
      gnutls_x509_privkey_deinit(plugin->ca_key);
      plugin->ca_key = NULL;

      g_set_error(
        error,
        infinoted_plugin_certificate_auth_error_quark(),
        INFINOTED_PLUGIN_CERTIFICATE_AUTH_ERROR_NO_CA_FOR_KEY,
        "%s",
        _("The given CA key does not match with any of the CA certificates")
      );

      return FALSE;
    }

    plugin->ca_key_index = i;

    /* Set the signing certificate of the directory, so that it can handle
     * account creation requests. Note that this takes ownership of the
     * certificate, so we take special care in the cleanup code in
     * infinoted_plugin_certificate_auth_deinitialize(). */
    sign_certs = g_malloc(sizeof(gnutls_x509_crt_t));
    sign_certs[0] = plugin->cas[plugin->ca_key_index];
    sign_chain = inf_certificate_chain_new(sign_certs, 1);

    infd_directory_set_certificate(
      infinoted_plugin_manager_get_directory(plugin->manager),
      plugin->ca_key,
      sign_chain
    );

    inf_certificate_chain_unref(sign_chain);
  }

  if(plugin->super_user != NULL)
  {
    if(plugin->ca_key == NULL)
    {
      g_set_error(
        error,
        infinoted_plugin_certificate_auth_error_quark(),
        INFINOTED_PLUGIN_CERTIFICATE_AUTH_ERROR_NO_CA_KEY,
        "%s",
        _("Cannot generate a superuser certificate without CA key")
      );

      return FALSE;
    }

    /* Create a private key and certificate for the super user. */
    infinoted_log_info(
      infinoted_plugin_manager_get_log(plugin->manager),
      _("Creating 4096-bit RSA private key for the super user account...")
    );

    super_key =
      inf_cert_util_create_private_key(GNUTLS_PK_RSA, 4096, error);
    if(super_key == NULL)
      return FALSE;

    desc.validity = 12 * 3600; /* 12 hours */
    desc.dn_common_name = "Super User";
    desc.san_dnsname = NULL;

    super_cert = inf_cert_util_create_signed_certificate(
      super_key,
      &desc,
      plugin->cas[plugin->ca_key_index],
      plugin->ca_key,
      error
    );

    if(super_cert == NULL)
    {
      gnutls_x509_privkey_deinit(super_key);
      return FALSE;
    }

    super_id = infd_directory_create_acl_account(
      infinoted_plugin_manager_get_directory(plugin->manager),
      _("Super User"),
      TRUE, /* transient */
      &super_cert,
      1,
      error
    );

    if(super_id == 0)
    {
      gnutls_x509_crt_deinit(super_cert);
      gnutls_x509_privkey_deinit(super_key);
      return FALSE;
    }

    plugin->super_id = super_id;

    chain[0] = super_cert;
    chain[1] = plugin->cas[plugin->ca_key_index];

    written = inf_cert_util_write_certificate_with_key(
      super_key,
      chain,
      2,
      plugin->super_user,
      error
    );

    gnutls_x509_crt_deinit(super_cert);
    gnutls_x509_privkey_deinit(super_key);

    if(written == FALSE)
      return FALSE;

    inf_browser_get_root(
      INF_BROWSER(infinoted_plugin_manager_get_directory(plugin->manager)),
      &iter
    );

    directory = infinoted_plugin_manager_get_directory(plugin->manager);

    sheet.account = super_id;
    sheet.mask = INF_ACL_MASK_ALL;
    infd_directory_get_support_mask(directory, &sheet.perms);
    sheet_set.n_sheets = 1;
    sheet_set.own_sheets = NULL;
    sheet_set.sheets = &sheet;

    request = inf_browser_set_acl(
      INF_BROWSER(directory),
      &iter,
      &sheet_set,
      infinoted_plugin_certificate_auth_set_acl_cb,
      plugin
    );

    if(request != NULL)
    {
      plugin->set_acl_request = request;
      g_object_ref(plugin->set_acl_request);
    }
  }

  return TRUE;
}

static void
infinoted_plugin_certificate_auth_deinitialize(gpointer plugin_info)
{
  InfinotedPluginCertificateAuth* plugin;
  InfRequest* remove_acl_account_request;
  InfCertificateCredentials* creds;
  guint i;

  plugin = (InfinotedPluginCertificateAuth*)plugin_info;

  /* Remove super user account. Note that this is not strictly necessary,
   * since the acocunt is transient and therefore is not written to disk,
   * so will not be re-created at the next server start. However, to be sure,
   * we explicitly remove the account at this point. */
  if(plugin->super_id != 0)
  {
    remove_acl_account_request = inf_browser_remove_acl_account(
      INF_BROWSER(infinoted_plugin_manager_get_directory(plugin->manager)),
      plugin->super_id,
      infinoted_plugin_certificate_auth_remove_acl_account_cb,
      plugin
    );

    /* This should be instantaneous: if we are not called back within the call
     * to inf_browser_remove_acl_account(), then we don't care about the
     * result, since we are being deinitialized. */
    if(remove_acl_account_request != NULL)
    {
      inf_signal_handlers_disconnect_by_func(
        plugin->set_acl_request,
        G_CALLBACK(infinoted_plugin_certificate_auth_remove_acl_account_cb),
        plugin
      );
    }
  }

  if(plugin->set_acl_request != NULL)
  {
    inf_signal_handlers_disconnect_by_func(
      plugin->set_acl_request,
      G_CALLBACK(infinoted_plugin_certificate_auth_set_acl_cb),
      plugin
    );

    g_object_unref(plugin->set_acl_request);
  }

  creds = infinoted_plugin_manager_get_credentials(plugin->manager);
  if(creds != NULL)
    gnutls_certificate_free_cas(inf_certificate_credentials_get(creds));

  infd_directory_set_certificate(
    infinoted_plugin_manager_get_directory(plugin->manager),
    NULL,
    NULL
  );

  /* If we have a ca_key set, the certificate that belongs to the key had
   * its ownership transferred to the directory, so make sure not to free
   * it twice here. */
  for(i = 0; i < plugin->n_cas; ++i)
    if(plugin->ca_key == NULL || i != plugin->ca_key_index)
      gnutls_x509_crt_deinit(plugin->cas[i]);
  g_free(plugin->cas);

  if(plugin->ca_key != NULL)
    gnutls_x509_privkey_deinit(plugin->ca_key);

  g_free(plugin->ca_list_file);
  g_free(plugin->ca_key_file);
  g_free(plugin->super_user);
}

static void
infinoted_plugin_certificate_auth_connection_added(InfXmlConnection* conn,
                                                   gpointer plugin_info,
                                                   gpointer connection_info)
{
  InfinotedPluginCertificateAuth* plugin;
  InfXmppConnection* xmpp;
  gnutls_certificate_request_t cert_req;

  plugin = (InfinotedPluginCertificateAuth*)plugin_info;

  if(INF_IS_XMPP_CONNECTION(conn))
  {
    xmpp = INF_XMPP_CONNECTION(conn);

    if(plugin->accept_unauthenticated_clients == TRUE)
      cert_req = GNUTLS_CERT_REQUEST;
    else
      cert_req = GNUTLS_CERT_REQUIRE;

    inf_xmpp_connection_set_certificate_callback(
      xmpp,
      cert_req,
      infinoted_plugin_certificate_auth_certificate_func,
      plugin,
      NULL
    );
  }
}

static void
infinoted_plugin_certificate_auth_connection_removed(InfXmlConnection* conn,
                                                     gpointer plugin_info,
                                                     gpointer session_info)
{
  InfinotedPluginCertificateAuth* plugin;
  InfXmppConnection* xmpp;

  plugin = (InfinotedPluginCertificateAuth*)plugin_info;

  if(INF_IS_XMPP_CONNECTION(conn))
  {
    xmpp = INF_XMPP_CONNECTION(conn);

    inf_xmpp_connection_set_certificate_callback(
      xmpp,
      GNUTLS_CERT_IGNORE,
      NULL,
      NULL,
      NULL
    );
  }
}

static const GFlagsValue INFINOTED_PLUGIN_CERTIFICATE_AUTH_VERIFY_FLAGS[] = {
  {
    GNUTLS_VERIFY_DISABLE_CA_SIGN,
    "GNUTLS_VERIFY_DISABLE_CA_SIGN",
    "disable-ca-sign"
  }, {
    GNUTLS_VERIFY_ALLOW_X509_V1_CA_CRT,
    "GNUTLS_VERIFY_ALLOW_X509_V1_CA_CRT",
    "allow-v1-ca-certificate"
  }, {
    GNUTLS_VERIFY_ALLOW_SIGN_RSA_MD2,
    "GNUTLS_VERIFY_ALLOW_SIGN_RSA_MD2",
    "allow-md2"
  }, {
    GNUTLS_VERIFY_ALLOW_SIGN_RSA_MD2,
    "GNUTLS_VERIFY_ALLOW_SIGN_RSA_MD2",
    "allow-md5"
  }, {
    GNUTLS_VERIFY_DISABLE_TIME_CHECKS,
    "GNUTLS_VERIFY_DISABLE_TIME_CHECKS",
    "disable-time-checks"
  }, {
    0,
    NULL,
    NULL
  }
};

static gboolean
infinoted_plugin_certificate_auth_convert_verify_flags(gpointer in,
                                                       gpointer out,
                                                       GError** error)
{
  return infinoted_parameter_convert_flags(
    in,
    out,
    INFINOTED_PLUGIN_CERTIFICATE_AUTH_VERIFY_FLAGS,
    error
  );
}

static const InfinotedParameterInfo INFINOTED_PLUGIN_CERTIFICATE_AUTH_OPTIONS[] = {
  { "ca-list",
    INFINOTED_PARAMETER_STRING,
    INFINOTED_PARAMETER_REQUIRED,
    offsetof(InfinotedPluginCertificateAuth, ca_list_file),
    infinoted_parameter_convert_filename,
    0,
    N_("The trusted CA or list of trusted CAs. Only clients presenting a "
       "certificate signed by one of these CAs are accepted."),
    N_("CA-LIST")
  }, {
    "ca-key",
    INFINOTED_PARAMETER_STRING,
    0,
    offsetof(InfinotedPluginCertificateAuth, ca_key_file),
    infinoted_parameter_convert_filename,
    0,
    N_("If given, this is the private key for one of the CA certificates in "
       "the list given by the \"ca-list\" parameter. In this case, the server "
       "itself acts as a CA and can issue certificates to clients. This can "
       "be used to allow clients to create their own accounts."),
    N_("CA-KEY")
  }, {
    "accept-unauthenticated-clients",
    INFINOTED_PARAMETER_BOOLEAN,
    0,
    offsetof(InfinotedPluginCertificateAuth, accept_unauthenticated_clients),
    infinoted_parameter_convert_boolean,
    0,
    N_("If this value is set to false, then clients that cannot authenticate "
       "themselves with a valid certificate are rejected and the connection "
       "is closed. If it is set to true, the connection will be accepted, "
       "but the client will only have unauthenticated access to the server. "
       "[Default: false]"),
    NULL
  }, {
    "super-user",
    INFINOTED_PARAMETER_STRING,
    0,
    offsetof(InfinotedPluginCertificateAuth, super_user),
    infinoted_parameter_convert_string,
    0,
    N_("Filename to which to write a short-lived super-user private key and "
       "certificate. The user is deleted when the infinoted server goes down "
       "or the plugin is re-loaded. This option can only be given when "
       "the \"ca-key\" parameter is set."),
    N_("FILENAME")
  }, {
    "verification-flags",
    INFINOTED_PARAMETER_STRING_LIST,
    0,
    offsetof(InfinotedPluginCertificateAuth, verify_flags),
    infinoted_plugin_certificate_auth_convert_verify_flags,
    0,
    N_("Flags to be used when verifying a client certificate. Each of these "
       "flags weakens the security, and so should be set only when "
       "absolutely necessary, and it should be done with care. "
       "[Default: disable-ca-sign]"),
    N_("flag1;flag2;[...]")
  }, {
    NULL,
    0,
    0,
    0,
    NULL
  }
};

const InfinotedPlugin INFINOTED_PLUGIN = {
  "certificate-auth",
  N_("Allows clients to authenticate themselves with client-side "
     "certificates. If authentication is successful, the certificate ID "
     "is used to log the user into its account."),
  INFINOTED_PLUGIN_CERTIFICATE_AUTH_OPTIONS,
  sizeof(InfinotedPluginCertificateAuth),
  0,
  0,
  NULL,
  infinoted_plugin_certificate_auth_info_initialize,
  infinoted_plugin_certificate_auth_initialize,
  infinoted_plugin_certificate_auth_deinitialize,
  infinoted_plugin_certificate_auth_connection_added,
  infinoted_plugin_certificate_auth_connection_removed,
  NULL,
  NULL
};

/* vim:set et sw=2 ts=2: */
