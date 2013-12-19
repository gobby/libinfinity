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

#ifndef __INFC_BROWSER_H__
#define __INFC_BROWSER_H__

#include <libinfinity/client/infc-session-proxy.h>
#include <libinfinity/client/infc-node-request.h>
#include <libinfinity/client/infc-chat-request.h>
#include <libinfinity/client/infc-certificate-request.h>
#include <libinfinity/client/infc-note-plugin.h>
#include <libinfinity/common/inf-browser.h>
#include <libinfinity/common/inf-xml-connection.h>
#include <libinfinity/communication/inf-communication-manager.h>

#include <gnutls/x509.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INFC_TYPE_BROWSER                 (infc_browser_get_type())
#define INFC_BROWSER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INFC_TYPE_BROWSER, InfcBrowser))
#define INFC_BROWSER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INFC_TYPE_BROWSER, InfcBrowserClass))
#define INFC_IS_BROWSER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INFC_TYPE_BROWSER))
#define INFC_IS_BROWSER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INFC_TYPE_BROWSER))
#define INFC_BROWSER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INFC_TYPE_BROWSER, InfcBrowserClass))

typedef struct _InfcBrowser InfcBrowser;
typedef struct _InfcBrowserClass InfcBrowserClass;

/**
 * InfcBrowserClass:
 *
 * This structure does not contain any public fields.
 */
struct _InfcBrowserClass {
  /*< private >*/
  GObjectClass parent_class;
};

/**
 * InfcBrowser:
 *
 * #InfcBrowser is an opaque data type. You should only access it via the
 * public API functions.
 */
struct _InfcBrowser {
  /*< private >*/
  GObject parent;
};

GType
infc_browser_get_type(void) G_GNUC_CONST;

InfcBrowser*
infc_browser_new(InfIo* io,
                 InfCommunicationManager* comm_manager,
                 InfXmlConnection* connection);

InfCommunicationManager*
infc_browser_get_communication_manager(InfcBrowser* browser);

InfXmlConnection*
infc_browser_get_connection(InfcBrowser* browser);

gboolean
infc_browser_add_plugin(InfcBrowser* browser,
                        const InfcNotePlugin* plugin);

const InfcNotePlugin*
infc_browser_lookup_plugin(InfcBrowser* browser,
                           const gchar* note_type);

InfcNodeRequest*
infc_browser_iter_save_session(InfcBrowser* browser,
                               const InfBrowserIter* iter,
                               InfNodeRequestFunc func,
                               gpointer user_data);

InfcSessionProxy*
infc_browser_iter_get_sync_in(InfcBrowser* browser,
                              const InfBrowserIter* iter);

GSList*
infc_browser_iter_get_sync_in_requests(InfcBrowser* browser,
                                       const InfBrowserIter* iter);

gboolean
infc_browser_iter_is_valid(InfcBrowser* browser,
                           const InfBrowserIter* iter);

InfcChatRequest*
infc_browser_subscribe_chat(InfcBrowser* browser,
                            InfcChatRequestFunc func,
                            gpointer user_data);

InfcChatRequest*
infc_browser_get_subscribe_chat_request(InfcBrowser* browser);

InfcSessionProxy*
infc_browser_get_chat_session(InfcBrowser* browser);

InfcCertificateRequest*
infc_browser_request_certificate(InfcBrowser* browser,
                                 gnutls_x509_crq_t crq,
                                 const gchar* extra_data,
                                 InfcCertificateRequestFunc func,
                                 gpointer user_data,
                                 GError** error);

G_END_DECLS

#endif /* __INFC_BROWSER_H__ */

/* vim:set et sw=2 ts=2: */
