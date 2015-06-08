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
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __INF_CERTIFICATE_VERIFY_H__
#define __INF_CERTIFICATE_VERIFY_H__

#include <libinfinity/common/inf-xmpp-connection.h>
#include <libinfinity/common/inf-xmpp-manager.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_CERTIFICATE_VERIFY                 (inf_certificate_verify_get_type())
#define INF_CERTIFICATE_VERIFY(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_CERTIFICATE_VERIFY, InfCertificateVerify))
#define INF_CERTIFICATE_VERIFY_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TYPE_CERTIFICATE_VERIFY, InfCertificateVerifyClass))
#define INF_IS_CERTIFICATE_VERIFY(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_CERTIFICATE_VERIFY))
#define INF_IS_CERTIFICATE_VERIFY_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TYPE_CERTIFICATE_VERIFY))
#define INF_CERTIFICATE_VERIFY_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TYPE_CERTIFICATE_VERIFY, InfCertificateVerifyClass))

#define INF_TYPE_CERTIFICATE_VERIFY_FLAGS           (inf_certificate_verify_flags_get_type())

typedef struct _InfCertificateVerify InfCertificateVerify;
typedef struct _InfCertificateVerifyClass InfCertificateVerifyClass;

/**
 * InfCertificateVerifyFlags:
 * @INF_CERTIFICATE_VERIFY_HOSTNAME_MISMATCH: The hostname of the
 * machine connected to does not match the one from the certificate.
 * @INF_CERTIFICATE_VERIFY_ISSUER_NOT_KNOWN: The issuer of the
 * certificate is not trusted, i.e. is not in the list of trusted CAs.
 * @INF_CERTIFICATE_VERIFY_NOT_PINNED: We have pinned a certificate for this
 * host, but the certificate presented is a different one.
 *
 * Various flags for why a certificate is not trusted.
 */
typedef enum _InfCertificateVerifyFlags {
  INF_CERTIFICATE_VERIFY_HOSTNAME_MISMATCH  = 1 << 0,
  INF_CERTIFICATE_VERIFY_ISSUER_NOT_KNOWN   = 1 << 1,
  INF_CERTIFICATE_VERIFY_NOT_PINNED         = 1 << 2
} InfCertificateVerifyFlags;

/**
 * InfCertificateVerifyClass:
 * @check_certificate: Default signal handler for the
 * #InfCertificateVerify::check-certificate signal.
 *
 * This structure contains default signal handlers for #InfCertificateVerify.
 */
struct _InfCertificateVerifyClass {
  /*< private >*/
  GObjectClass parent_class;

  /*< public >*/
  void (*check_certificate)(InfCertificateVerify* verify,
                            InfXmppConnection* connection,
                            InfCertificateChain* certificate_chain,
                            gnutls_x509_crt_t pinned_certificate,
                            InfCertificateVerifyFlags flags);

  void (*check_cancelled)(InfCertificateVerify* verify,
                          InfXmppConnection* connection);
};

/**
 * InfCertificateVerify:
 *
 * #InfCertificateVerify is an opaque data type. You should only access it
 * via the public API functions.
 */
struct _InfCertificateVerify {
  /*< private >*/
  GObject parent;
};

GType
inf_certificate_verify_flags_get_type(void) G_GNUC_CONST;

GType
inf_certificate_verify_get_type(void) G_GNUC_CONST;

InfCertificateVerify*
inf_certificate_verify_new(InfXmppManager* xmpp_manager,
                           const gchar* known_hosts_file);

void
inf_certificate_verify_checked(InfCertificateVerify* verify,
                               InfXmppConnection* connection,
                               gboolean result);

G_END_DECLS

#endif /* __INF_CERTIFICATE_VERIFY_H__ */

/* vim:set et sw=2 ts=2: */
