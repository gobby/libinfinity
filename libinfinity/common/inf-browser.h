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

#ifndef __INF_BROWSER_H__
#define __INF_BROWSER_H__

#include <glib-object.h>
#include <libinfinity/common/inf-browser-iter.h>
#include <libinfinity/common/inf-request.h>
#include <libinfinity/common/inf-session-proxy.h>
#include <libinfinity/common/inf-session.h>
#include <libinfinity/common/inf-acl.h>

G_BEGIN_DECLS

#define INF_TYPE_BROWSER                 (inf_browser_get_type())
#define INF_BROWSER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_BROWSER, InfBrowser))
#define INF_IS_BROWSER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_BROWSER))
#define INF_BROWSER_GET_IFACE(inst)      (G_TYPE_INSTANCE_GET_INTERFACE((inst), INF_TYPE_BROWSER, InfBrowserIface))

#define INF_TYPE_BROWSER_STATUS          (inf_browser_status_get_type())

/**
 * InfBrowser:
 *
 * #InfBrowser is an opaque data type. You should only access it
 * via the public API functions.
 */
typedef struct _InfBrowser InfBrowser;
typedef struct _InfBrowserIface InfBrowserIface;

/**
 * InfBrowserStatus:
 * @INF_BROWSER_CLOSED: The browser is closed and cannot be used currently.
 * @INF_BROWSER_OPENING: The browser is currently being opened but cannot be
 * used yet.
 * @INF_BROWSER_OPEN: The browser is open and can be used to browse the
 * directory.
 *
 * This enumeration contains possible status values for
 * #InfBrowser. Several operations, such as exploring a node or subscribing
 * to a session can only be performed when the browser is open (for example,
 * connected to a remote infinote server).
 */
typedef enum _InfBrowserStatus {
  INF_BROWSER_CLOSED,
  INF_BROWSER_OPENING,
  INF_BROWSER_OPEN
} InfBrowserStatus;

/**
 * InfBrowserIface:
 * @error: Default signal handler for the #InfBrowser::error signal.
 * @node_added: Default signal handler for the #InfBrowser::node-added
 * signal.
 * @node_removed: Default signal handler for the #InfBrowser::node-removed
 * signal.
 * @subscribe_session: Default signal handler for the
 * #InfBrowser::subscribe-session signal.
 * @begin_explore: Default signal handler for the
 * #InfBrowser::begin-explore signal.
 * @begin_request: Default signal handler for the
 * #InfBrowser::begin-request signal.
 * @acl_account_added: Default signal handler for the
 * #InfBrowser::acl-account-added signal.
 * @acl_account_removed: Default signal handler for the
 * #InfBrowser::acl-account-removed signal.
 * @acl_local_account_changed: Default signal handler for the
 * #InfBrowser::acl-local-account-changed signal.
 * @acl_changed: Default signal handler for the
 * #InfBrowser::acl-changed signal.
 * @get_root: Virtual function to return the root node of the browser.
 * @get_next: Virtual function to return the next sibling in a browser.
 * @get_prev: Virtual function to return the previous sibling in a browser.
 * @get_parent: Virtual function to return the parent node in a browser.
 * @get_child: Virtual function to return the first child node in a browser.
 * @explore: Virtual function to start exploring a node.
 * @get_explored: Virtual function to query whether a node is explored
 * already.
 * @add_note: Virtual function to add a new leaf node to the directory.
 * @add_subdirectory: Virtual function to a new subdirectory node to the
 * directory.
 * @remove_node: Virtual function to remove a node from the directory.
 * @get_node_name: Virtual function to return the name of a node in a browser.
 * @get_node_type: Virtual function to return the type of a node in a browser.
 * @subscribe: Virtual function to subscribe to a session of a node in a
 * browser.
 * @get_session: Virtual function to return a session for a node in a browser.
 * @list_pending_requests: Virtual function to return a list of all pending
 * requests for a node in a browser.
 * @iter_from_request: Virtual function to return an iterator pointing to the
 * node a given request was made for.
 * @query_acl_account_list: Virtual function for querying the list of
 * accounts.
 * @get_acl_account_list: Virtual function for obtaining the list of accounts.
 * @get_acl_local_account: Virtual function to return the ACL account of the
 * local host.
 * @lookup_acl_account: Virtual function to find an account by its ID.
 * @create_acl_account: Virtual function to create a new account.
 * @remove_acl_account: Virtual function to remove an account.
 * @query_acl: Virtual function for querying the ACL for a node for all
 * other users.
 * @has_acl: Virtual function for checking whether the ACL has been queried
 * or is otherwise available.
 * @get_acl: Virtual function for obtaining the full ACL for a node.
 * @set_acl: Virtual function for changing the ACL for one node.
 *
 * Signals and virtual functions for the #InfBrowser interface.
 */
struct _InfBrowserIface {
  /*< private >*/
  GTypeInterface parent;

  /* Signals */

  /*< public >*/
  void (*error)(InfBrowser* browser,
                const GError* error);

  void (*node_added)(InfBrowser* browser,
                     const InfBrowserIter* iter,
                     InfRequest* request);

  void (*node_renamed)(InfBrowser* browser,
                       const InfBrowserIter* iter,
                       InfRequest* request,
                       const gchar* new_name);

  void (*node_removed)(InfBrowser* browser,
                       const InfBrowserIter* iter,
                       InfRequest* request);

  void (*subscribe_session)(InfBrowser* browser,
                            const InfBrowserIter* iter,
                            InfSessionProxy* proxy,
                            InfRequest* request);

  void (*unsubscribe_session)(InfBrowser* browser,
                              const InfBrowserIter* iter,
                              InfSessionProxy* proxy,
                              InfRequest* request);

  void (*begin_request)(InfBrowser* browser,
                        const InfBrowserIter* iter,
                        InfRequest* request);

  void (*acl_account_added)(InfBrowser* browser,
                            const InfAclAccount* account,
                            InfRequest* request);

  void (*acl_account_removed)(InfBrowser* browser,
                              const InfAclAccount* account,
                              InfRequest* request);

  void (*acl_local_account_changed)(InfBrowser* browser,
                                    const InfAclAccount* account,
                                    InfRequest* request);

  void (*acl_changed)(InfBrowser* browser,
                      const InfBrowserIter* iter,
                      const InfAclSheetSet* sheet_set,
                      InfRequest* request);

  /* Virtual functions */

  gboolean (*get_root)(InfBrowser* browser,
                       InfBrowserIter* iter);
  gboolean (*get_next)(InfBrowser* browser,
                       InfBrowserIter* iter);
  gboolean (*get_prev)(InfBrowser* browser,
                       InfBrowserIter* iter);
  gboolean (*get_parent)(InfBrowser* browser,
                         InfBrowserIter* iter);
  gboolean (*get_child)(InfBrowser* browser,
                        InfBrowserIter* iter);
  InfRequest* (*explore)(InfBrowser* browser,
                         const InfBrowserIter* iter,
                         InfRequestFunc func,
                         gpointer user_data);
  gboolean (*get_explored)(InfBrowser* browser,
                           const InfBrowserIter* iter);
  gboolean (*is_subdirectory)(InfBrowser* browser,
                              const InfBrowserIter* iter);

  InfRequest* (*add_note)(InfBrowser* browser,
                          const InfBrowserIter* iter,
                          const char* name,
                          const char* type,
                          const InfAclSheetSet* acl,
                          InfSession* session,
                          gboolean initial_subscribe,
                          InfRequestFunc func,
                          gpointer user_data);
  InfRequest* (*add_subdirectory)(InfBrowser* browser,
                                  const InfBrowserIter* iter,
                                  const char* name,
                                  const InfAclSheetSet* acl,
                                  InfRequestFunc func,
                                  gpointer user_data);
  InfRequest* (*rename_node)(InfBrowser* browser,
                             const InfBrowserIter* iter,
			     const char* new_name,
                             InfRequestFunc func,
                             gpointer user_data);
  InfRequest* (*remove_node)(InfBrowser* browser,
                             const InfBrowserIter* iter,
                             InfRequestFunc func,
                             gpointer user_data);
  const gchar* (*get_node_name)(InfBrowser* browser,
                                const InfBrowserIter* iter);
  const gchar* (*get_node_type)(InfBrowser* browser,
                                const InfBrowserIter* iter);

  InfRequest* (*subscribe)(InfBrowser* browser,
                           const InfBrowserIter* iter,
                           InfRequestFunc func,
                           gpointer user_data);
  InfSessionProxy* (*get_session)(InfBrowser* browser,
                                  const InfBrowserIter* iter);

  GSList* (*list_pending_requests)(InfBrowser* browser,
                                   const InfBrowserIter* iter,
                                   const gchar* request_type);
  gboolean (*iter_from_request)(InfBrowser* browser,
                                InfRequest* request,
                                InfBrowserIter* iter);

  InfRequest* (*query_acl_account_list)(InfBrowser* browser,
                                        InfRequestFunc func,
                                        gpointer user_data);

  const InfAclAccount** (*get_acl_account_list)(InfBrowser* browser,
                                                guint* n_accounts);

  const InfAclAccount* (*get_acl_local_account)(InfBrowser* browser);

  const InfAclAccount* (*lookup_acl_account)(InfBrowser* browser,
                                             const gchar* id);

  InfRequest* (*create_acl_account)(InfBrowser* browser,
                                    gnutls_x509_crq_t crq,
                                    InfRequestFunc func,
                                    gpointer user_data);

  InfRequest* (*remove_acl_account)(InfBrowser* browser,
                                    const InfAclAccount* acc,
                                    InfRequestFunc func,
                                    gpointer user_data);

  InfRequest* (*query_acl)(InfBrowser* browser,
                           const InfBrowserIter* iter,
                           InfRequestFunc func,
                           gpointer user_data);

  gboolean (*has_acl)(InfBrowser* browser,
                      const InfBrowserIter* iter,
                      const InfAclAccount* account);

  const InfAclSheetSet* (*get_acl)(InfBrowser* browser,
                                   const InfBrowserIter* iter);

  InfRequest* (*set_acl)(InfBrowser* browser,
                         const InfBrowserIter* iter,
                         const InfAclSheetSet* sheet_set,
                         InfRequestFunc func,
                         gpointer user_data);
};

GType
inf_browser_status_get_type(void) G_GNUC_CONST;

GType
inf_browser_get_type(void) G_GNUC_CONST;

gboolean
inf_browser_get_root(InfBrowser* browser,
                     InfBrowserIter* iter);

gboolean
inf_browser_get_next(InfBrowser* browser,
                     InfBrowserIter* iter);

gboolean
inf_browser_get_prev(InfBrowser* browser,
                     InfBrowserIter* iter);

gboolean
inf_browser_get_parent(InfBrowser* browser,
                       InfBrowserIter* iter);

gboolean
inf_browser_get_child(InfBrowser* browser,
                      InfBrowserIter* iter);

gboolean
inf_browser_is_ancestor(InfBrowser* browser,
                        const InfBrowserIter* ancestor,
                        const InfBrowserIter* iter);

InfRequest*
inf_browser_explore(InfBrowser* browser,
                    const InfBrowserIter* iter,
                    InfRequestFunc func,
                    gpointer user_data);

gboolean
inf_browser_get_explored(InfBrowser* browser,
                         const InfBrowserIter* iter);

gboolean
inf_browser_is_subdirectory(InfBrowser* browser,
                            const InfBrowserIter* iter);

InfRequest*
inf_browser_add_note(InfBrowser* browser,
                     const InfBrowserIter* iter,
                     const char* name,
                     const char* type,
                     const InfAclSheetSet* acl,
                     InfSession* session,
                     gboolean initial_subscribe,
                     InfRequestFunc func,
                     gpointer user_data);

InfRequest*
inf_browser_add_subdirectory(InfBrowser* browser,
                             const InfBrowserIter* iter,
                             const char* name,
                             const InfAclSheetSet* acl,
                             InfRequestFunc func,
                             gpointer user_data);

InfRequest*
inf_browser_rename_node(InfBrowser* browser,
                        const InfBrowserIter* iter,
                        const char* new_name,
                        InfRequestFunc func,
                        gpointer user_data);

InfRequest*
inf_browser_remove_node(InfBrowser* browser,
                        const InfBrowserIter* iter,
                        InfRequestFunc func,
                        gpointer user_data);

const gchar*
inf_browser_get_node_name(InfBrowser* browser,
                          const InfBrowserIter* iter);

const gchar*
inf_browser_get_node_type(InfBrowser* browser,
                          const InfBrowserIter* iter);

gchar*
inf_browser_get_path(InfBrowser* browser,
                     const InfBrowserIter* iter);

InfRequest*
inf_browser_subscribe(InfBrowser* browser,
                      const InfBrowserIter* iter,
                      InfRequestFunc func,
                      gpointer user_data);

InfSessionProxy*
inf_browser_get_session(InfBrowser* browser,
                        const InfBrowserIter* iter);

GSList*
inf_browser_list_pending_requests(InfBrowser* browser,
                                  const InfBrowserIter* iter,
                                  const gchar* request_type);

gboolean
inf_browser_iter_from_request(InfBrowser* browser,
                              InfRequest* request,
                              InfBrowserIter* iter);

InfRequest*
inf_browser_get_pending_request(InfBrowser* browser,
                                const InfBrowserIter* iter,
                                const gchar* request_type);

InfRequest*
inf_browser_query_acl_account_list(InfBrowser* browser,
                                   InfRequestFunc func,
                                   gpointer user_data);

const InfAclAccount**
inf_browser_get_acl_account_list(InfBrowser* browser,
                                 guint* n_accounts);

const InfAclAccount*
inf_browser_get_acl_local_account(InfBrowser* browser);

const InfAclAccount*
inf_browser_lookup_acl_account(InfBrowser* browser,
                               const gchar* id);

InfRequest*
inf_browser_create_acl_account(InfBrowser* browser,
                               gnutls_x509_crq_t crq,
                               InfRequestFunc func,
                               gpointer user_data);

InfRequest*
inf_browser_remove_acl_account(InfBrowser* browser,
                               const InfAclAccount* acc,
                               InfRequestFunc func,
                               gpointer user_data);

InfRequest*
inf_browser_query_acl(InfBrowser* browser,
                      const InfBrowserIter* iter,
                      InfRequestFunc func,
                      gpointer user_data);

gboolean
inf_browser_has_acl(InfBrowser* browser,
                    const InfBrowserIter* iter,
                    const InfAclAccount* account);

const InfAclSheetSet*
inf_browser_get_acl(InfBrowser* browser,
                    const InfBrowserIter* iter);

InfRequest*
inf_browser_set_acl(InfBrowser* browser,
                    const InfBrowserIter* iter,
                    const InfAclSheetSet* sheet_set,
                    InfRequestFunc func,
                    gpointer user_data);

gboolean
inf_browser_check_acl(InfBrowser* browser,
                      const InfBrowserIter* iter,
                      const InfAclAccount* account,
                      const InfAclMask* check_mask,
                      InfAclMask* out_mask);

void
inf_browser_error(InfBrowser* browser,
                  const GError* error);

void
inf_browser_node_added(InfBrowser* browser,
                       const InfBrowserIter* iter,
                       InfRequest* request);

void
inf_browser_node_removed(InfBrowser* browser,
                         const InfBrowserIter* iter,
                         InfRequest* request);

void
inf_browser_subscribe_session(InfBrowser* browser,
                              const InfBrowserIter* iter,
                              InfSessionProxy* proxy,
                              InfRequest* request);

void
inf_browser_unsubscribe_session(InfBrowser* browser,
                                const InfBrowserIter* iter,
                                InfSessionProxy* proxy,
                                InfRequest* request);

void
inf_browser_begin_request(InfBrowser* browser,
                          const InfBrowserIter* iter,
                          InfRequest* request);

void
inf_browser_acl_account_added(InfBrowser* browser,
                              const InfAclAccount* account,
                              InfRequest* request);

void
inf_browser_acl_account_removed(InfBrowser* browser,
                                const InfAclAccount* account,
                                InfRequest* request);

void
inf_browser_acl_local_account_changed(InfBrowser* browser,
                                      const InfAclAccount* account,
                                      InfRequest* request);

void
inf_browser_acl_changed(InfBrowser* browser,
                        const InfBrowserIter* iter,
                        const InfAclSheetSet* update,
                        InfRequest* request);

G_END_DECLS

#endif /* __INF_BROWSER_H__ */

/* vim:set et sw=2 ts=2: */
