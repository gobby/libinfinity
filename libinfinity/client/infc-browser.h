/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008, 2009 Armin Burgmeier <armin@arbur.net>
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
#include <libinfinity/client/infc-browser-iter.h>
#include <libinfinity/client/infc-explore-request.h>
#include <libinfinity/client/infc-node-request.h>
#include <libinfinity/client/infc-note-plugin.h>
#include <libinfinity/common/inf-xml-connection.h>
#include <libinfinity/communication/inf-communication-manager.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INFC_TYPE_BROWSER                 (infc_browser_get_type())
#define INFC_BROWSER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INFC_TYPE_BROWSER, InfcBrowser))
#define INFC_BROWSER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INFC_TYPE_BROWSER, InfcBrowserClass))
#define INFC_IS_BROWSER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INFC_TYPE_BROWSER))
#define INFC_IS_BROWSER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INFC_TYPE_BROWSER))
#define INFC_BROWSER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INFC_TYPE_BROWSER, InfcBrowserClass))

#define INFC_TYPE_BROWSER_STATUS          (infc_browser_status_get_type())

typedef struct _InfcBrowser InfcBrowser;
typedef struct _InfcBrowserClass InfcBrowserClass;

typedef enum _InfcBrowserStatus {
  /* Not connected to any server */
  INFC_BROWSER_DISCONNECTED,
  /* Currently connecting */
  INFC_BROWSER_CONNECTING,
  /* Currently connected */
  INFC_BROWSER_CONNECTED
} InfcBrowserStatus;

typedef enum _InfcBrowserNodeStatus {
  /* The node is synchronized with the server */
  INFC_BROWSER_NODE_SYNC,
  /* The node has been deleted locally, but the server has not yet
   * acknowledged the deletion and might still reject the request. */
  INFC_BROWSER_NODE_DELETED,
  /* The node has been added locally, but the server has not yet
   * acknowledged the addition and might still reject the request. */
  INFC_BROWSER_NODE_ADDED,
  /* The node has been moved locally, but the server has not yet
   * acknowledged the move and might still reject the request. */
  INFC_BROWSER_NODE_MOVED,
  /* The node has been copied locally, but the server has not yet
   * acknowledged the copy and might still reject the request. */
  INFC_BROWSER_NODE_COPIED,
  /* Inherit status from parent node (used internally) */
  INFC_BROWSER_NODE_INHERIT
} InfcBrowserNodeStatus;

struct _InfcBrowserClass {
  GObjectClass parent_class;

  /* Signals */
  void (*error)(InfcBrowser* browser,
                const GError* error);

  void (*node_added)(InfcBrowser* browser,
                     InfcBrowserIter* iter);

  void (*node_removed)(InfcBrowser* browser,
                       InfcBrowserIter* iter);

  void (*subscribe_session)(InfcBrowser* browser,
                            InfcBrowserIter* iter,
                            InfcSessionProxy* proxy);

  void (*begin_explore)(InfcBrowser* browser,
                        InfcBrowserIter* iter,
                        InfcExploreRequest* request);

  void (*begin_subscribe)(InfcBrowser* browser,
                          InfcBrowserIter* iter,
                          InfcNodeRequest* request);
};

struct _InfcBrowser {
  GObject parent;
};

GType
infc_browser_status_get_type(void) G_GNUC_CONST;

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

InfcBrowserStatus
infc_browser_get_status(InfcBrowser* browser);

gboolean
infc_browser_add_plugin(InfcBrowser* browser,
                        const InfcNotePlugin* plugin);

const InfcNotePlugin*
infc_browser_lookup_plugin(InfcBrowser* browser,
                           const gchar* note_type);

void
infc_browser_iter_get_root(InfcBrowser* browser,
                           InfcBrowserIter* iter);

gboolean
infc_browser_iter_get_next(InfcBrowser* browser,
                           InfcBrowserIter* iter);

gboolean
infc_browser_iter_get_prev(InfcBrowser* browser,
                           InfcBrowserIter* iter);

gboolean
infc_browser_iter_get_parent(InfcBrowser* browser,
                             InfcBrowserIter* iter);

gboolean
infc_browser_iter_get_explored(InfcBrowser* browser,
                               const InfcBrowserIter* iter);

gboolean
infc_browser_iter_get_child(InfcBrowser* browser,
                            InfcBrowserIter* iter);

InfcExploreRequest*
infc_browser_iter_explore(InfcBrowser* browser,
                          const InfcBrowserIter* iter);

const gchar*
infc_browser_iter_get_name(InfcBrowser* browser,
                           const InfcBrowserIter* iter);

gchar*
infc_browser_iter_get_path(InfcBrowser* browser,
                           const InfcBrowserIter* iter);

gboolean
infc_browser_iter_is_subdirectory(InfcBrowser* browser,
                                  const InfcBrowserIter* iter);

InfcNodeRequest*
infc_browser_add_subdirectory(InfcBrowser* browser,
                              const InfcBrowserIter* parent,
                              const gchar* name);

InfcNodeRequest*
infc_browser_add_note(InfcBrowser* browser,
                      const InfcBrowserIter* parent,
                      const gchar* name,
                      const InfcNotePlugin* plugin,
                      gboolean initial_subscribe);

InfcNodeRequest*
infc_browser_add_note_with_content(InfcBrowser* browser,
                                   const InfcBrowserIter* parent,
                                   const gchar* name,
                                   const InfcNotePlugin* plugin,
                                   InfSession* session,
                                   gboolean initial_subscribe);

InfcNodeRequest*
infc_browser_remove_node(InfcBrowser* browser,
                         const InfcBrowserIter* iter);

const gchar*
infc_browser_iter_get_note_type(InfcBrowser* browser,
                                const InfcBrowserIter* iter);

const InfcNotePlugin*
infc_browser_iter_get_plugin(InfcBrowser* browser,
                             const InfcBrowserIter* iter);

InfcNodeRequest*
infc_browser_iter_subscribe_session(InfcBrowser* browser,
                                    const InfcBrowserIter* iter);

InfcNodeRequest*
infc_browser_iter_save_session(InfcBrowser* browser,
                               const InfcBrowserIter* iter);

InfcSessionProxy*
infc_browser_iter_get_session(InfcBrowser* browser,
                              const InfcBrowserIter* iter);

InfcSessionProxy*
infc_browser_iter_get_sync_in(InfcBrowser* browser,
                              const InfcBrowserIter* iter);

InfcNodeRequest*
infc_browser_iter_get_subscribe_request(InfcBrowser* browser,
                                        const InfcBrowserIter* iter);

InfcExploreRequest*
infc_browser_iter_get_explore_request(InfcBrowser* browser,
                                      const InfcBrowserIter* iter);

GSList*
infc_browser_iter_get_sync_in_requests(InfcBrowser* browser,
                                       const InfcBrowserIter* iter);

gboolean
infc_browser_iter_from_node_request(InfcBrowser* browser,
                                    InfcNodeRequest* request,
                                    InfcBrowserIter* iter);

gboolean
infc_browser_iter_from_explore_request(InfcBrowser* browser,
                                       InfcExploreRequest* request,
                                       InfcBrowserIter* iter);

gboolean
infc_browser_iter_is_valid(InfcBrowser* browser,
                           const InfcBrowserIter* iter);

/* TODO: This should not use InfcNodeRequest, but another request type
 * which is not identified with a node. */
InfcNodeRequest*
infc_browser_subscribe_chat(InfcBrowser* browser);

InfcNodeRequest*
infc_browser_get_subscribe_chat_request(InfcBrowser* browser);

InfcSessionProxy*
infc_browser_get_chat_session(InfcBrowser* browser);

G_END_DECLS

#endif /* __INFC_BROWSER_H__ */

/* vim:set et sw=2 ts=2: */
