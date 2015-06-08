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
 * SECTION:inf-browser
 * @title: InfBrowser
 * @short_description: Browsing of infinote directories
 * @include: libinfinity/common/inf-browser.h
 * @see_also: #InfBrowserIter
 * @stability: Unstable
 *
 * #InfBrowser provides an interface for browsing an infinote directory.
 * It can be used to navigate through the tree, add or remove nodes and
 * subscribe to sessions.
 *
 * Nodes in a directory can either contain more nodes in which case this is
 * called a subdirectory. Leaf nodes are also called notes. There is only
 * one root node which must be a subdirectory node.
 */

#include <libinfinity/common/inf-browser.h>
#include <libinfinity/inf-define-enum.h>

#include <string.h>

static const GEnumValue inf_browser_status_values[] = {
  {
    INF_BROWSER_CLOSED,
    "INF_BROWSER_CLOSED",
    "closed"
  }, {
    INF_BROWSER_OPENING,
    "INF_BROWSER_OPENING",
    "opening"
  }, {
    INF_BROWSER_OPEN,
    "INF_BROWSER_OPEN",
    "open"
  }
};

INF_DEFINE_ENUM_TYPE(InfBrowserStatus, inf_browser_status, inf_browser_status_values)
G_DEFINE_INTERFACE(InfBrowser, inf_browser, G_TYPE_OBJECT)

enum {
  ERROR_,
  NODE_ADDED,
  NODE_REMOVED,
  SUBSCRIBE_SESSION,
  UNSUBSCRIBE_SESSION,
  BEGIN_REQUEST, /* detailed */
  ACL_ACCOUNT_ADDED,
  ACL_ACCOUNT_REMOVED,
  ACL_LOCAL_ACCOUNT_CHANGED,
  ACL_CHANGED,

  LAST_SIGNAL
};

static guint browser_signals[LAST_SIGNAL];

/* Helper function for inf_browser_get_path */
static void
inf_browser_extract_path(InfBrowser* browser,
                         const InfBrowserIter* iter,
                         GString* string)
{
  InfBrowserIter parent_iter;

  parent_iter = *iter;
  if(inf_browser_get_parent(browser, &parent_iter))
  {
    inf_browser_extract_path(browser, &parent_iter, string);

    g_assert(string->len > 0);
    if(string->str[string->len - 1] != '/')
      g_string_append_c(string, '/');

    g_string_append(string, inf_browser_get_node_name(browser, iter));
  }
  else
  {
    g_string_assign(string, "/");
  }
}

static void
inf_browser_default_init(InfBrowserInterface* iface)
{
  /**
   * InfBrowser::error:
   * @browser: The #InfBrowser object emitting the signal.
   * @error: A #GError describing what went wrong.
   *
   * This signal is emitted whenever there was an asynchronous error with
   * the browser itself which was not the result of a particular user
   * request. The error may or may not be fatal. If it is fatal the browser
   * will also be closed which can be checked with the status property.
   */
  browser_signals[ERROR_] = g_signal_new(
    "error",
    INF_TYPE_BROWSER,
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfBrowserInterface, error),
    NULL, NULL,
    g_cclosure_marshal_VOID__BOXED,
    G_TYPE_NONE,
    1,
    G_TYPE_ERROR
  );

  /**
   * InfBrowser::node-added:
   * @browser: The #InfBrowser object emitting the signal.
   * @iter: An iterator pointing to the newly added node.
   * @request: The request that lead to the node being added, or %NULL.
   *
   * This signal is emitted when a node is added to the browser.
   */
  browser_signals[NODE_ADDED] = g_signal_new(
    "node-added",
    INF_TYPE_BROWSER,
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfBrowserInterface, node_added),
    NULL, NULL,
    NULL,
    G_TYPE_NONE,
    2,
    INF_TYPE_BROWSER_ITER | G_SIGNAL_TYPE_STATIC_SCOPE,
    INF_TYPE_REQUEST
  );

  /**
   * InfBrowser::node-removed:
   * @browser: The #InfBrowser object emitting the signal.
   * @iter: An iterator pointing to the node being removed.
   * @request: The request that lead to the node being removed, or %NULL.
   *
   * This signal is emitted just before a node is being removed from the
   * browser. The iterator is still valid and can be used to access the
   * node which will be removed.
   */
  browser_signals[NODE_REMOVED] = g_signal_new(
    "node-removed",
    INF_TYPE_BROWSER,
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfBrowserInterface, node_removed),
    NULL, NULL,
    NULL,
    G_TYPE_NONE,
    2,
    INF_TYPE_BROWSER_ITER | G_SIGNAL_TYPE_STATIC_SCOPE,
    INF_TYPE_REQUEST
  );

  /**
   * InfBrowser::subscribe-session:
   * @browser: The #InfBrowser object emitting the signal.
   * @iter: An iterator pointing to the node to which a subscription.
   * was made, or %NULL.
   * @session: The subscribed session.
   * @request: The request that lead to the subscription, or %NULL.
   *
   * This signal is emitted whenever the browser is subscribed to a session.
   * This can happen as a result of a inf_browser_subscribe() or
   * inf_browser_add_note() call, but it is also possible that a
   * subscription is initiated without user interaction.
   *
   * If @iter is %NULL the session was a global session and not attached to
   * a particular node.
   */
  browser_signals[SUBSCRIBE_SESSION] = g_signal_new(
    "subscribe-session",
    INF_TYPE_BROWSER,
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfBrowserInterface, subscribe_session),
    NULL, NULL,
    NULL,
    G_TYPE_NONE,
    3,
    INF_TYPE_BROWSER_ITER | G_SIGNAL_TYPE_STATIC_SCOPE,
    INF_TYPE_SESSION_PROXY,
    INF_TYPE_REQUEST
  );

  /**
   * InfBrowser::unsubscribe-session:
   * @browser: The #InfBrowser object emitting the signal.
   * @iter: An iterator pointing to the node from which a subscription.
   * was removed, or %NULL.
   * @session: The session to which the subscription was removed.
   * @request: The request that lead to the unsubscription, or %NULL.
   *
   * This signal is emitted whenever a session is detached from a browser
   * node. This can happen when a subscribed session is closed, or, in
   * the case of a server, if the session is idle for a long time it is
   * stored on disk and removed from memory.
   *
   * Note that this signal does not mean that the corresponding
   * session can no longer be used. It only means that it is no longer
   * associated to a browser node, for example also when the browser node
   * is deleted. The session might still be intact, though, and can continue
   * even when it is detached from the browser.
   *
   * In order to find out whether the local host was unsubscribed from a
   * session and the connection to the other session participants has been
   * lost, the #InfSession:subscription-group property should be monitored,
   * and if that property changes and
   * inf_session_get_subscription_group() returns %NULL afterwards,
   * it means the session is no longer connected.
   *
   * If @iter is %NULL the session was a global session and not attached to
   * a particular node.
   */
  browser_signals[UNSUBSCRIBE_SESSION] = g_signal_new(
    "unsubscribe-session",
    INF_TYPE_BROWSER,
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfBrowserInterface, unsubscribe_session),
    NULL, NULL,
    NULL,
    G_TYPE_NONE,
    3,
    INF_TYPE_BROWSER_ITER | G_SIGNAL_TYPE_STATIC_SCOPE,
    INF_TYPE_SESSION_PROXY,
    INF_TYPE_REQUEST
  );

  /**
   * InfBrowser::begin-request:
   * @browser: The #InfBrowser object emitting the signal.
   * @iter: An iterator pointing to the node for which a request is made, or
   * %NULL.
   * @request: The request being made.
   *
   * This signal is emitted whenever a request is made with the browser.
   * The signal is detailed with the request type, so that it is possible to
   * connect to e.g. "begin-request::add-subdirectory" to only get notified
   * about subdirectory creation requests.
   *
   * If @iter is %NULL the request is a global request and not attached to a
   * particular node.
   */
  browser_signals[BEGIN_REQUEST] = g_signal_new(
    "begin-request",
    INF_TYPE_BROWSER,
    G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
    G_STRUCT_OFFSET(InfBrowserInterface, begin_request),
    NULL, NULL,
    NULL,
    G_TYPE_NONE,
    2,
    INF_TYPE_BROWSER_ITER | G_SIGNAL_TYPE_STATIC_SCOPE,
    INF_TYPE_REQUEST
  );

  /**
   * InfBrowser::acl-account-added:
   * @browser: The #InfBrowser object emitting the signal.
   * @account: The new #InfAclAccount.
   * @request: The request which lead to the newly added account, or %NULL.
   *
   * This signal is emitted whenever a new account is added to the browser,
   * and the browser supports account list notification.
   */
  browser_signals[ACL_ACCOUNT_ADDED] = g_signal_new(
    "acl-account-added",
    INF_TYPE_BROWSER,
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfBrowserInterface, acl_account_added),
    NULL, NULL,
    NULL,
    G_TYPE_NONE,
    2,
    INF_TYPE_ACL_ACCOUNT | G_SIGNAL_TYPE_STATIC_SCOPE,
    INF_TYPE_REQUEST
  );

  /**
   * InfBrowser::acl-account-removed:
   * @browser: The #InfBrowser object emitting the signal.
   * @account: The removed #InfAclAccount.
   * @request: The request which lead to the removal of the account,
   * or %NULL.
   *
   * This signal is emitted whenever an account is removed from the browser,
   * and the browser supports account list notification.
   */
  browser_signals[ACL_ACCOUNT_REMOVED] = g_signal_new(
    "acl-account-removed",
    INF_TYPE_BROWSER,
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfBrowserInterface, acl_account_removed),
    NULL, NULL,
    NULL,
    G_TYPE_NONE,
    2,
    INF_TYPE_ACL_ACCOUNT | G_SIGNAL_TYPE_STATIC_SCOPE,
    INF_TYPE_REQUEST
  );

  /**
   * InfBrowser::acl-local-account-changed:
   * @browser: The #InfBrowser object emitting the signal.
   * @account: The new local #InfAclAccount.
   * @request: The request which triggered the account change, or %NULL.
   *
   * This signal is emitted whenever the account into which the local host
   * is logged into changes. This can happen for example on a delayed login,
   * or when the current account is deleted from the server in which case
   * the host is demoted to the default account.
   */
  browser_signals[ACL_LOCAL_ACCOUNT_CHANGED] = g_signal_new(
    "acl-local-account-changed",
    INF_TYPE_BROWSER,
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfBrowserInterface, acl_local_account_changed),
    NULL, NULL,
    NULL,
    G_TYPE_NONE,
    2,
    INF_TYPE_ACL_ACCOUNT | G_SIGNAL_TYPE_STATIC_SCOPE,
    INF_TYPE_REQUEST
  );

  /**
   * InfBrowser::acl-changed:
   * @browser: The #InfBrowser object emitting the signal.
   * @iter: An iterator pointing to the node for which the ACL has changed.
   * @sheet_set: A #InfAclSheetSet containing the changed ACL sheets.
   * @request: The request which lead to the ACL being changed, or %NULL.
   *
   * This signal is emitted whenever an ACL for the node @iter points to
   * are changed. This signal is emitted whenever the ACL change for the
   * local user, the default user, or for a node that all ACLs have been
   * queried with inf_browser_query_acl().
   *
   * The @sheet_set parameter contains only the ACL sheets that have
   * changed. In order to get the new full sheet set, call
   * inf_browser_get_acl().
   */
  browser_signals[ACL_CHANGED] = g_signal_new(
    "acl-changed",
    INF_TYPE_BROWSER,
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfBrowserInterface, acl_changed),
    NULL, NULL,
    NULL,
    G_TYPE_NONE,
    3,
    INF_TYPE_BROWSER_ITER | G_SIGNAL_TYPE_STATIC_SCOPE,
    INF_TYPE_ACL_SHEET_SET | G_SIGNAL_TYPE_STATIC_SCOPE,
    INF_TYPE_REQUEST
  );

  g_object_interface_install_property(
    iface,
    g_param_spec_enum(
      "status",
      "Browser Status",
      "The connectivity status of the browser",
      INF_TYPE_BROWSER_STATUS,
      INF_BROWSER_CLOSED,
      G_PARAM_READABLE
    )
  );
}

/**
 * inf_browser_get_root:
 * @browser: A #InfBrowser.
 * @iter: (out): An uninitialized #InfBrowserIter.
 *
 * Sets @iter to point to the root node of @browser.
 *
 * Returns: %TRUE if @iter was set or %FALSE if there is no root node, i.e.
 * the browser is not open.
 */
gboolean
inf_browser_get_root(InfBrowser* browser,
                     InfBrowserIter* iter)
{
  InfBrowserInterface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->get_root != NULL, FALSE);

  return iface->get_root(browser, iter);
}

/**
 * inf_browser_get_next:
 * @browser: A #InfBrowser.
 * @iter: (inout): A #InfBrowserIter pointing to a node inside @browser.
 *
 * Sets @iter to point to its next sibling node. If @iter points already to the
 * last node then @iter is left untouched and the function returns %FALSE.
 *
 * Returns: %TRUE if @iter was moved or %FALSE otherwise.
 */
gboolean
inf_browser_get_next(InfBrowser* browser,
                     InfBrowserIter* iter)
{
  InfBrowserInterface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->get_next != NULL, FALSE);

  return iface->get_next(browser, iter);
}

/**
 * inf_browser_get_prev:
 * @browser: A #InfBrowser.
 * @iter: (inout): A #InfBrowserIter pointing to a node inside @browser.
 *
 * Sets @iter to point to its previous sibling node. If @iter points to the
 * first node already then @iter is left untouched and the function returns
 * %FALSE.
 *
 * Returns: %TRUE if @iter was moved or %FALSE otherwise.
 */
gboolean
inf_browser_get_prev(InfBrowser* browser,
                     InfBrowserIter* iter)
{
  InfBrowserInterface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->get_prev != NULL, FALSE);

  return iface->get_prev(browser, iter);
}

/**
 * inf_browser_get_parent:
 * @browser: A #InfBrowser.
 * @iter: (inout): A #InfBrowserIter pointing to a node inside @browser.
 *
 * Sets @iter to point to its parent node. If @iter is already the root node
 * then @iter is left untouched and the function returns %FALSE.
 *
 * Returns: %TRUE if @iter was moved or %FALSE otherwise.
 */
gboolean
inf_browser_get_parent(InfBrowser* browser,
                       InfBrowserIter* iter)
{
  InfBrowserInterface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->get_parent != NULL, FALSE);

  return iface->get_parent(browser, iter);
}

/**
 * inf_browser_get_child:
 * @browser: A #InfBrowser.
 * @iter: (inout): A #InfBrowserIter pointing to a subdirectory node inside
 * @browser.
 *
 * Sets @iter to point to the first child of the subdirectory node it
 * currently points to. If the subdirectory does not contain any children or
 * if @iter does not point to a subdirectory the function returns %FALSE.
 * This does not necessarily mean that there do not exist any children but it
 * can also be that they have not yet been explored. Nodes can be explored
 * with inf_browser_explore() and it can be checked whether a given node has
 * been explored with inf_browser_get_explored().
 *
 * Returns: %TRUE if @iter was moved or %FALSE otherwise.
 */
gboolean
inf_browser_get_child(InfBrowser* browser,
                      InfBrowserIter* iter)
{
  InfBrowserInterface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->is_subdirectory != NULL, FALSE);
  g_return_val_if_fail(iface->is_subdirectory(browser, iter) == TRUE, FALSE);

  g_return_val_if_fail(iface->get_child != NULL, FALSE);
  return iface->get_child(browser, iter);
}

/**
 * inf_browser_is_ancestor:
 * @browser: A #InfBrowser.
 * @ancestor: An iterator pointing to the ancestor node.
 * @iter: An iterator pointing to the node to be checked.
 *
 * Returns whether @ancestor is an ancestor of @iter, i.e. either the two
 * iterators point to the same node or @ancestor is a parent, grand-parent,
 * grand-grand-parent, etc. of the node @iter points to.
 *
 * Returns: Whether @ancestor is an ancestor of @iter.
 */
gboolean
inf_browser_is_ancestor(InfBrowser* browser,
                        const InfBrowserIter* ancestor,
                        const InfBrowserIter* iter)
{
  InfBrowserIter check_iter;

  g_return_val_if_fail(INF_IS_BROWSER(browser), FALSE);
  g_return_val_if_fail(ancestor != NULL, FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);

  check_iter = *iter;

  do
  {
    if(check_iter.node == ancestor->node)
      return TRUE;
  } while(inf_browser_get_parent(browser, &check_iter));

  return FALSE;
}

/**
 * inf_browser_explore:
 * @browser: A #InfBrowser.
 * @iter: A #InfBrowserIter pointing to a subdirectory node inside
 * @browser.
 * @func: (scope async): The function to be called when the request finishes,
 * or %NULL.
 * @user_data: Additional data to pass to @func.
 *
 * Requests the node @iter points to to be explored. Initally, subdirectory
 * nodes are not explored, that is not known what content there is. Nodes can
 * be explored to learn about the children nodes they contain. Since exploring
 * is a potentially lengthy process involing networking or I/O with slow
 * devices this function returns a @InfExploreRequest which can be used to
 * monitor the progress of the operation and get notified when the exploration
 * finishes. During exploration #InfBrowser::node-added signals are already
 * emitted appropriately for every child explored inside @iter.
 *
 * The request might either finish during the call to this function, in which
 * case @func will be called and %NULL being returned. If the request does not
 * finish within the function call, a #InfRequest object is returned,
 * where @func has been installed for the #InfRequest::finished signal,
 * so that it is called as soon as the request finishes.
 *
 * Returns: (transfer none) (allow-none): A #InfRequest, or %NULL if @iter
 * points to a non-subdirectory node.
 */
InfRequest*
inf_browser_explore(InfBrowser* browser,
                    const InfBrowserIter* iter,
                    InfRequestFunc func,
                    gpointer user_data)
{
  InfBrowserInterface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(iter != NULL, NULL);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->is_subdirectory != NULL, NULL);
  g_return_val_if_fail(iface->is_subdirectory(browser, iter) == TRUE, NULL);

  g_return_val_if_fail(iface->explore != NULL, NULL);
  return iface->explore(browser, iter, func, user_data);
}

/**
 * inf_browser_get_explored:
 * @browser: A #InfBrowser.
 * @iter: A #InfBrowserIter pointing to a subdirectory node inside @browser.
 *
 * Returns whether the node @iter points to has already been explored or not.
 *
 * Returns: %TRUE if the node @iter points to has been explored
 * or %FALSE otherwise.
 */
gboolean
inf_browser_get_explored(InfBrowser* browser,
                         const InfBrowserIter* iter)
{
  InfBrowserInterface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->is_subdirectory != NULL, FALSE);
  g_return_val_if_fail(iface->is_subdirectory(browser, iter) == TRUE, FALSE);

  g_return_val_if_fail(iface->get_explored != NULL, FALSE);
  return iface->get_explored(browser, iter);
}

/**
 * inf_browser_is_subdirectory:
 * @browser: A #InfBrowser.
 * @iter: A #InfBrowserIter pointing to a node inside @browser.
 *
 * Returns whether the node @iter points to is a subdirectory node.
 *
 * Returns: %TRUE if the node @iter points to is a subdirectory node or
 * %FALSE otherwise.
 */
gboolean
inf_browser_is_subdirectory(InfBrowser* browser,
                            const InfBrowserIter* iter)
{
  InfBrowserInterface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->is_subdirectory != NULL, FALSE);

  return iface->is_subdirectory(browser, iter);
}

/**
 * inf_browser_add_note:
 * @browser: A #InfBrowser.
 * @iter: A #InfBrowserIter pointing to a subdirectory node inside @browser.
 * @name: The name of the node to add.
 * @type: The type of the node to add.
 * @acl: (allow-none): A #InfAclSheetSet representing the initial ACL for
 * this node, or %NULL.
 * @session: (allow-none): A #InfSession with a session of type @type, or
 * %NULL.
 * @initial_subscribe: Whether to subscribe to the newly created session.
 * @func: (scope async): The function to be called when the request finishes,
 * or %NULL.
 * @user_data: Additional data to pass to @func.
 *
 * Adds a new leaf node to the browser. The new node is of type @type. If
 * session is non-%NULL it will be used as the initial content of the new
 * node, otherwise the new node will start empty. In the case of non-%NULL
 * @session the session must be in status %INF_SESSION_RUNNING.
 *
 * The returned request finishes as soon as the creation of the node is
 * acknowledged. It is however not guaranteed that the content of the note has
 * been synchronized yet. In the case of a client connected to an infinote
 * server the content is usually not transmitted when the request finishes.
 * If an error in the process of transmission happens then the node will be
 * removed again.
 *
 * On the client side, the progress of synchronization to the server after the
 * request has finished can be monitored with the
 * InfSession::synchronization-failed,
 * InfSession::synchronization-complete and
 * InfSession::synchronization-progress signals. Note that a single session
 * might be synchronized to multiple servers at the same time, you will have
 * to check the connection parameter in the signal hander to find out to
 * which server the session is synchronized.
 *
 * You can safely unref session after having called this function. If the
 * request or the synchronization fails, the session will be discarded in
 * that case. When the returned request finishes, you can use
 * infc_browser_iter_get_sync_in() to get the session again.
 *
 * If @initial_subscribe is set, then, when the returned request finishes,
 * you might call inf_browser_get_session() on the resulting
 * #InfBrowserIter. However, that function is not guaranteed to return
 * non-%NULL in this case since the node might have been created, but the
 * subscription could have failed.
 *
 * The initial ACL for the new node is given by @acl. If this parameter
 * is %NULL, then the default ACL is used, which inherits all permissions
 * from the parent node. In order to apply non-%NULL ACL to the new node,
 * the %INF_ACL_CAN_SET_ACL permission must be granted to the local entity for
 * the node @iter points to.
 *
 * The request might either finish during the call to this function, in which
 * case @func will be called and %NULL being returned. If the request does not
 * finish within the function call, a #InfRequest object is returned,
 * where @func has been installed for the #InfRequest::finished signal,
 * so that it is called as soon as the request finishes.
 *
 * Returns: (transfer none) (allow-none): A #InfRequest which can be used to
 * get notified when the request finishes.
 */
InfRequest*
inf_browser_add_note(InfBrowser* browser,
                     const InfBrowserIter* iter,
                     const char* name,
                     const char* type,
                     const InfAclSheetSet* acl,
                     InfSession* session,
                     gboolean initial_subscribe,
                     InfRequestFunc func,
                     gpointer user_data)
{
  InfBrowserInterface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(iter != NULL, NULL);
  g_return_val_if_fail(name != NULL, NULL);
  g_return_val_if_fail(type != NULL, NULL);
  g_return_val_if_fail(session == NULL || INF_IS_SESSION(session), NULL);

  g_return_val_if_fail(
    session == NULL ||
    inf_session_get_status(session) == INF_SESSION_RUNNING,
    NULL
  );

  /* Can only subscribe if that session is not already subscribed elsewhere */
  g_return_val_if_fail(
    session == NULL ||
    initial_subscribe == FALSE ||
    inf_session_get_subscription_group(session) == NULL,
    NULL
  );

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->is_subdirectory != NULL, NULL);
  g_return_val_if_fail(iface->is_subdirectory(browser, iter) == TRUE, NULL);

  g_return_val_if_fail(iface->add_note != NULL, NULL);

  return iface->add_note(
    browser,
    iter,
    name,
    type,
    acl,
    session,
    initial_subscribe,
    func,
    user_data
  );
}

/**
 * inf_browser_add_subdirectory:
 * @browser: A #InfBrowser.
 * @iter: A #InfBrowserIter pointing to a subdirectory node inside @browser.
 * @name: The name of the node to add.
 * @acl: (allow-none): A #InfAclSheetSet representing the initial ACL for
 * this node, or %NULL.
 * @func: (scope async): The function to be called when the request finishes,
 * or %NULL.
 * @user_data: Additional data to pass to @func.
 *
 * Adds a new subdirectory node to the browser.
 *
 * The initial ACL for the new node is given by @acl. If this parameter
 * is %NULL, then the default ACL is used, which inherits all permissions
 * from the parent node. In order to apply non-%NULL ACL to the new node,
 * the %INF_ACL_CAN_SET_ACL permission must be granted to the local entity for
 * the node @iter points to.
 *
 * The request might either finish during the call to this function, in which
 * case @func will be called and %NULL being returned. If the request does not
 * finish within the function call, a #InfRequest object is returned,
 * where @func has been installed for the #InfRequest::finished signal,
 * so that it is called as soon as the request finishes.
 *
 * Returns: (transfer none) (allow-none): A #InfRequest which can be used to
 * get notified when the request finishes.
 */
InfRequest*
inf_browser_add_subdirectory(InfBrowser* browser,
                             const InfBrowserIter* iter,
                             const char* name,
                             const InfAclSheetSet* acl,
                             InfRequestFunc func,
                             gpointer user_data)
{
  InfBrowserInterface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(iter != NULL, NULL);
  g_return_val_if_fail(name != NULL, NULL);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->is_subdirectory != NULL, NULL);
  g_return_val_if_fail(iface->is_subdirectory(browser, iter) == TRUE, NULL);

  g_return_val_if_fail(iface->add_subdirectory != NULL, NULL);
  return iface->add_subdirectory(browser, iter, name, acl, func, user_data);
}

/**
 * inf_browser_remove_node:
 * @browser: A #InfBrowser.
 * @iter: A #InfBrowserIter pointing to a node inside @browser.
 * @func: (scope async): The function to be called when the request finishes,
 * or %NULL.
 * @user_data: Additional data to pass to @func.
 *
 * Requests to remove the node @iter points to. It may point to a
 * subdirectory node in which case all its children are removed recursively
 * as well.
 *
 * The request might either finish during the call to this function, in which
 * case @func will be called and %NULL being returned. If the request does not
 * finish within the function call, a #InfRequest object is returned,
 * where @func has been installed for the #InfRequest::finished signal,
 * so that it is called as soon as the request finishes.
 *
 * Returns: (transfer none) (allow-none): A #InfRequest which can be used to
 * get notified when the request finishes.
 */
InfRequest*
inf_browser_remove_node(InfBrowser* browser,
                        const InfBrowserIter* iter,
                        InfRequestFunc func,
                        gpointer user_data)
{
  InfBrowserInterface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(iter != NULL, NULL);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->remove_node != NULL, NULL);

  return iface->remove_node(browser, iter, func, user_data);
}

/**
 * inf_browser_get_node_name:
 * @browser: A #InfBrowser.
 * @iter: A #InfBrowserIter pointing to a node inside @browser.
 *
 * Returns the name of the node @iter points to.
 *
 * Returns: A string containing the node's name.
 */
const gchar*
inf_browser_get_node_name(InfBrowser* browser,
                          const InfBrowserIter* iter)
{
  InfBrowserInterface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(iter != NULL, NULL);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->get_node_name != NULL, NULL);

  return iface->get_node_name(browser, iter);
}

/**
 * inf_browser_get_node_type:
 * @browser: A #InfBrowser.
 * @iter: A #InfBrowserIter pointing to a leaf node inside @browser.
 *
 * Returns the type of the node @iter points to.
 *
 * Returns: The node type as a string.
 */
const gchar*
inf_browser_get_node_type(InfBrowser* browser,
                          const InfBrowserIter* iter)
{
  InfBrowserInterface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(iter != NULL, NULL);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->is_subdirectory != NULL, NULL);
  g_return_val_if_fail(iface->is_subdirectory(browser, iter) == FALSE, NULL);

  g_return_val_if_fail(iface->get_node_type != NULL, NULL);
  return iface->get_node_type(browser, iter);
}

/**
 * inf_browser_get_path:
 * @browser: A #InfBrowser.
 * @iter: A #InfBrowserIter pointing to a node inside @browser.
 *
 * Returns the full path to the node @iter points to. A path always starts
 * with a '/' and then has the name of the node and all its parents separated
 * by '/', much like a filesystem path on Unix.
 *
 * Returns: (transfer full): The path as a string. Free with g_free() when no
 * longer needed.
 */
gchar*
inf_browser_get_path(InfBrowser* browser,
                     const InfBrowserIter* iter)
{
  GString* str;
  str = g_string_sized_new(32);
  inf_browser_extract_path(browser, iter, str);
  return g_string_free(str, FALSE);
}

/**
 * inf_browser_subscribe:
 * @browser: A #InfBrowser.
 * @iter: A #InfBrowserIter pointing to a leaf node inside @browser.
 * @func: (scope async): The function to be called when the request finishes,
 * or %NULL.
 * @user_data: Additional data to pass to @func.
 *
 * Attempts to subscribe to the node @iter points to, i.e. obtain a
 * #InfSession representing its content. This also allows to change the
 * content of the node.
 *
 * The request might either finish during the call to this function, in which
 * case @func will be called and %NULL being returned. If the request does not
 * finish within the function call, a #InfRequest object is returned,
 * where @func has been installed for the #InfRequest::finished signal,
 * so that it is called as soon as the request finishes.
 *
 * Returns: (transfer none) (allow-none): A #InfRequest which can be used to
 * get notified when the request finishes.
 */
InfRequest*
inf_browser_subscribe(InfBrowser* browser,
                      const InfBrowserIter* iter,
                      InfRequestFunc func,
                      gpointer user_data)
{
  InfBrowserInterface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(iter != NULL, NULL);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->is_subdirectory != NULL, NULL);
  g_return_val_if_fail(iface->is_subdirectory(browser, iter) == FALSE, NULL);

  g_return_val_if_fail(iface->subscribe != NULL, NULL);
  return iface->subscribe(browser, iter, func, user_data);
}

/**
 * inf_browser_get_session:
 * @browser: A #InfBrowser.
 * @iter: A #InfBrowserIter pointing to a leaf node inside @browser.
 *
 * Returns the session which has the content of the node @iter points to. The
 * session needs to be subscribed to, see inf_browser_subscribe(). If the
 * session is not subscribed or the subscription request has not yet finished
 * the function returns %NULL.
 *
 * Returns: (transfer none) (allow-none): A @InfSessionProxy which contains
 * the session. The proxy object can be used to join a user into the session.
 */
InfSessionProxy*
inf_browser_get_session(InfBrowser* browser,
                        const InfBrowserIter* iter)
{
  InfBrowserInterface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(iter != NULL, NULL);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->is_subdirectory != NULL, NULL);
  g_return_val_if_fail(iface->is_subdirectory(browser, iter) == FALSE, NULL);

  g_return_val_if_fail(iface->get_session != NULL, NULL);
  return iface->get_session(browser, iter);
}

/**
 * inf_browser_list_pending_requests:
 * @browser: A #InfBrowser.
 * @iter: (allow-none): A #InfBrowserIter pointing to a node inside @browser,
 * or %NULL.
 * @request_type: (allow-none): The type of request to return pending requests
 * for, or %NULL.
 *
 * Returns a list of all pending requests for the node @iter points to which
 * match type @request_type. A pending request is a request which has been
 * created but has not yet finished. @request_type can be %NULL in which case
 * all requests for the given node are returned. If it is non-%NULL only
 * requests which match the given type are included in the list of returned
 * requests.
 *
 * If @iter is %NULL then the function returns all pending global requests.
 *
 * Returns: (transfer container) (element-type InfRequest): A list of
 * #InfRequest<!-- -->s. Free with g_slist_free() when no longer needed.
 */
GSList*
inf_browser_list_pending_requests(InfBrowser* browser,
                                  const InfBrowserIter* iter,
                                  const gchar* request_type)
{
  InfBrowserInterface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->list_pending_requests != NULL, NULL);

  return iface->list_pending_requests(browser, iter, request_type);
}

/**
 * inf_browser_iter_from_request:
 * @browser: A #InfBrowser.
 * @request: A #InfRequest which has not yet finished and which was
 * issued by @browser.
 * @iter: (out): An uninitialized #InfBrowserIter.
 *
 * Sets @iter to the node for which @request was made. If that node does not
 * exist anymore or if @request has already finished the function returns
 * %FALSE and @iter is left untouched.
 *
 * Returns: %TRUE if @iter was moved or %FALSE otherwise.
 */
gboolean
inf_browser_iter_from_request(InfBrowser* browser,
                              InfRequest* request,
                              InfBrowserIter* iter)
{
  InfBrowserInterface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), FALSE);
  g_return_val_if_fail(INF_IS_REQUEST(request), FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->iter_from_request != NULL, FALSE);

  return iface->iter_from_request(browser, request, iter);
}

/**
 * inf_browser_get_pending_request:
 * @browser: A #InfBrowser.
 * @iter: (allow-none): A #InfBrowserIter pointing to a node inside @browser,
 * or %NULL.
 * @request_type: (allow-none): The type of request.
 *
 * Returns a pending request for the node @iter points to which matches type
 * @request_type. If there is no such request the function returns %NULL.
 * This function is a shortcut for calling
 * inf_browser_list_pending_requests() and retrieving the first item from
 * the list.
 *
 * If @iter is %NULL the function returns a global request.
 *
 * For many request types, such as "subscribe-session", "subscribe-chat",
 * "explore-node", "query-user-list" or "query-acl" there can only be one
 * request at a time, and therefore this function is more convenient to use
 * than inf_browser_list_pending_requests().
 *
 * Returns: (allow-none) (transfer none): A #InfRequest, or %NULL.
 */
InfRequest*
inf_browser_get_pending_request(InfBrowser* browser,
                                const InfBrowserIter* iter,
                                const gchar* request_type)
{
  GSList* list;
  InfRequest* request;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);

  list = inf_browser_list_pending_requests(browser, iter, request_type);

  request = NULL;
  if(list != NULL)
    request = INF_REQUEST(list->data);

  g_slist_free(list);
  return request;
}

/**
 * inf_browser_get_acl_default_account:
 * @browser: A #InfBrowser.
 *
 * Returns the default #InfAclAccount representing unauthenticated users, and
 * which is used to look up permissions if no permissions are explicitly
 * specified in the ACL of a particular node for a particular account.
 *
 * Returns: (transfer none): A #InfAclAccount. The returned value is owned by
 * the browser and must not be freed.
 */
const InfAclAccount*
inf_browser_get_acl_default_account(InfBrowser* browser)
{
  InfBrowserInterface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->get_acl_default_account != NULL, NULL);

  return iface->get_acl_default_account(browser);
}

/**
 * inf_browser_get_acl_local_account:
 * @browser: A #InfBrowser.
 *
 * Returns the #InfAclAccount representing the local host. This can be used to
 * check whether the local account is allowed to perform certain operations in
 * the browser. The function can also return %NULL, in which case all
 * operations are allowed, because the browser represents a local infinote
 * directory.
 *
 * Returns: (transfer none) (allow-none): A #InfAclAccount, or %NULL. The
 * returned value is owned by the browser and must not be freed.
 */
const InfAclAccount*
inf_browser_get_acl_local_account(InfBrowser* browser)
{
  InfBrowserInterface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->get_acl_local_account != NULL, NULL);

  return iface->get_acl_local_account(browser);
}

/**
 * inf_browser_query_acl_account_list:
 * @browser: A #InfBrowser.
 * @func: (scope async): The function to be called when the request finishes,
 * or %NULL.
 * @user_data: Additional data to pass to @func.
 *
 * Queries the list of accounts in @browser. When this call has finished,
 * a list of accounts is available, and also a flag whether the browser
 * supports notification of accounts. If it does, then the
 * #InfBrowser::acl-account-added and #InfBrowser::acl-account-removed signals
 * are emitted when the account list changes, and the next time this function
 * is called, it might be available to serve the account list from the cache
 * without blocking.
 *
 * It can also be that the browser does not support listing of accounts at
 * all. In this case, the request will fail with an error code of
 * %INF_DIRECTORY_ERROR_OPERATION_UNSUPPORTED.
 *
 * The request might either finish during the call to this function, in which
 * case @func will be called and %NULL being returned. If the request does not
 * finish within the function call, a #InfRequest object is returned, where
 * @func has been installed for the #InfRequest::finished signal, so that it
 * is called as soon as the request finishes.
 *
 * Returns: (transfer none) (allow-none): A #InfRequest that can be used to
 * be notified when the request finishes, or %NULL.
 */
InfRequest*
inf_browser_query_acl_account_list(InfBrowser* browser,
                                   InfRequestFunc func,
                                   gpointer user_data)
{
  InfBrowserInterface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->query_acl_account_list != NULL, NULL);

  return iface->query_acl_account_list(browser, func, user_data);
}

/**
 * inf_browser_lookup_acl_accounts:
 * @browser: A #InfBrowser.
 * @ids: (array length=n_ids): An array of account IDs to look up.
 * @n_ids: The number of elements in the array.
 * @func: (scope async): The function to be called when the request finishes,
 * or %NULL.
 * @user_data: Additional data to pass to @func.
 *
 * Looks up the accounts with the given IDs. If the account list has been
 * queried with inf_browser_query_acl_account_list() before, then it is
 * likely that this function returns instantly, otherwise an asynchronous
 * operation might be started.
 *
 * The request result contains an array of #InfAclAccount objects for the
 * given account IDs, which contain the account names as well. If one account
 * ID does not belong to an actual account, the #InfAclAccount.name field will
 * be %NULL.
 *
 * The request might either finish during the call to this function, in which
 * case @func will be called and %NULL being returned. If the request does not
 * finish within the function call, a #InfRequest object is returned, where
 * @func has been installed for the #InfRequest::finished signal, so that it
 * is called as soon as the request finishes.
 *
 * Returns: (transfer none) (allow-none): A #InfRequest that can be used to
 * be notified when the request finishes, or %NULL.
 */
InfRequest*
inf_browser_lookup_acl_accounts(InfBrowser* browser,
                                const InfAclAccountId* ids,
                                guint n_ids,
                                InfRequestFunc func,
                                gpointer user_data)
{
  InfBrowserInterface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(ids != NULL, NULL);
  g_return_val_if_fail(n_ids > 0, NULL);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->lookup_acl_accounts != NULL, NULL);

  return iface->lookup_acl_accounts(browser, ids, n_ids, func, user_data);
}

/**
 * inf_browser_lookup_acl_account_by_name:
 * @browser: A #InfBrowser.
 * @name: The name of the user account to look up.
 * @func: (scope async): The function to be called when the request finishes,
 * or %NULL.
 * @user_data: Additional data to pass to @func.
 *
 * Looks up the account(s) with the given name. While in principal, there can
 * be many accounts with the same name, in practise user names are often
 * unique.
 *
 * The request result contains an array of #InfAclAccount objects, all with
 * the same name. If there is only one entry with ID set to 0, there are no
 * accounts with the given name.
 *
 * The request might either finish during the call to this function, in which
 * case @func will be called and %NULL being returned. If the request does not
 * finish within the function call, a #InfRequest object is returned, where
 * @func has been installed for the #InfRequest::finished signal, so that it
 * is called as soon as the request finishes.
 *
 * Returns: (allow-none) (transfer none): A #InfRequest that can be used to be
 * notified when the request finishes, or %NULL.
 */
InfRequest*
inf_browser_lookup_acl_account_by_name(InfBrowser* browser,
                                       const gchar* name,
                                       InfRequestFunc func,
                                       gpointer user_data)
{
  InfBrowserInterface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(name != NULL, NULL);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->lookup_acl_account_by_name != NULL, NULL);

  return iface->lookup_acl_account_by_name(browser, name, func, user_data);
}

/**
 * inf_browser_create_acl_account:
 * @browser: A #InfBrowser.
 * @crq: A certificate request.
 * @func: (scope async): The function to be called when the request finishes,
 * or %NULL.
 * @user_data: Additional data to pass to @func.
 *
 * Requests creation of a new account. If the request succeeds, @crq will be
 * used by the server to create a certificate which is sent back to the
 * client and is associated to the newly created account.
 *
 * The request might either finish during the call to this function, in which
 * case @func will be called and %NULL being returned. If the request does not
 * finish within the function call, a #InfRequest object is returned, where
 * @func has been installed for the #InfRequest::finished signal, so that it
 * is called as soon as the request finishes.
 *
 * Returns: (transfer none) (allow-none): A #InfRequest that can be used to
 * get notified when the request finishes or fails.
 */
InfRequest*
inf_browser_create_acl_account(InfBrowser* browser,
                               gnutls_x509_crq_t crq,
                               InfRequestFunc func,
                               gpointer user_data)
{
  InfBrowserInterface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(crq != NULL, NULL);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->create_acl_account != NULL, NULL);

  return iface->create_acl_account(browser, crq, func, user_data);
}

/**
 * inf_browser_remove_acl_account:
 * @browser: A #InfBrowser.
 * @account: The ID of the account to remove.
 * @func: (scope async): The function to be called when the request finishes,
 * or %NULL.
 * @user_data: Additional data to pass to @func.
 *
 * Requests deletion of the given account.
 *
 * The request might either finish during the call to this function, in which
 * case @func will be called and %NULL being returned. If the request does not
 * finish within the function call, a #InfRequest object is returned, where
 * @func has been installed for the #InfRequest::finished signal, so that it
 * is called as soon as the request finishes.
 *
 * Returns: (transfer none) (allow-none): A #InfRequest that can be used to
 * get notified when the request finishes or fails.
 */
InfRequest*
inf_browser_remove_acl_account(InfBrowser* browser,
                               InfAclAccountId account,
                               InfRequestFunc func,
                               gpointer user_data)
{
  InfBrowserInterface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(account != 0, NULL);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->get_acl_default_account != NULL, NULL);

  g_return_val_if_fail(
    account != iface->get_acl_default_account(browser)->id,
    NULL
  );

  g_return_val_if_fail(iface->remove_acl_account != NULL, NULL);
  return iface->remove_acl_account(browser, account, func, user_data);
}

/**
 * inf_browser_query_acl:
 * @browser: A #InfBrowser.
 * @iter: An iterator pointing to a node for which to query the ACLs.
 * @func: (scope async): The function to be called when the request finishes,
 * or %NULL.
 * @user_data: Additional data to pass to @func.
 *
 * Queries the ACLs for all users of the node @iter points to. When the
 * request has finished, inf_browser_get_acl() can be used to retrieve the
 * ACLs.
 *
 * The request might either finish during the call to this function, in which
 * case @func will be called and %NULL being returned. If the request does not
 * finish within the function call, a #InfRequest object is returned,
 * where @func has been installed for the #InfRequest::finished signal,
 * so that it is called as soon as the request finishes.
 *
 * Returns: (transfer none) (allow-none): A #InfRequest which can be used to
 * be notified when the request finishes.
 */
InfRequest*
inf_browser_query_acl(InfBrowser* browser,
                      const InfBrowserIter* iter,
                      InfRequestFunc func,
                      gpointer user_data)
{
  InfBrowserInterface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(iter != NULL, NULL);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->query_acl != NULL, NULL);

  return iface->query_acl(browser, iter, func, user_data);
}

/**
 * inf_browser_has_acl:
 * @browser: A #InfBrowser.
 * @iter: An iterator pointing to a node for which to check full ACL
 * availability.
 * @account: The account to check ACL availability for, or 0.
 *
 * This function returns whether the ACL sheet for the given account is
 * available or not. If the function returns %FALSE then
 * inf_browser_query_acl() can be called in order to retrieve the full ACL.
 * If @account is 0, the function checks whether the full ACL is available,
 * i.e. the ACL sheets for all accounts. Usually the ACL sheets for the
 * default account and the local account are always available.
 *
 * Returns: %TRUE when the ACL sheet for @account is available or %FALSE
 * otherwise.
 */
gboolean
inf_browser_has_acl(InfBrowser* browser,
                    const InfBrowserIter* iter,
                    InfAclAccountId account)
{
  InfBrowserInterface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->has_acl != NULL, FALSE);

  return iface->has_acl(browser, iter, account);
}

/**
 * inf_browser_get_acl:
 * @browser: A #InfBrowser.
 * @iter: An iterator pointing to a node for which to retrieve ACLs.
 *
 * Retrieves the ACL for the node @iter points to. This function can also
 * be called if the ACL has not been queried before using
 * inf_browser_query_acl(). In that case, the returned sheet set will only
 * contain sheets for the default user and the local user. The function
 * can return %NULL which is equivalent to an empty sheet set, i.e. no ACL.
 *
 * When the full ACL has been successfully queried with
 * inf_browser_query_acl(), the full ACL is returned by this function. The
 * function inf_browser_has_acl() can be used to check whether this function
 * will return the full ACL or only the sheets for the default and local
 * users.
 *
 * Returns: A #InfAclSheetSet containing the requested ACL, or %NULL. The
 * returned value is owned by the #InfBrowser and should not be freed.
 */
const InfAclSheetSet*
inf_browser_get_acl(InfBrowser* browser,
                    const InfBrowserIter* iter)
{
  InfBrowserInterface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(iter != NULL, NULL);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->get_acl != NULL, NULL);

  return iface->get_acl(browser, iter);
}

/**
 * inf_browser_set_acl:
 * @browser: A #InfBrowser.
 * @iter: An iterator pointing to the node for which to change ACLs.
 * @sheet_set: An #InfAclSheetSet with the sheets to update.
 * @func: (scope async): The function to be called when the request finishes,
 * or %NULL.
 * @user_data: Additional data to pass to @func.
 *
 * Changes the ACLs for the node @iter points to. Existing sheets that are not
 * in @sheet_set are left untouched. This operation is only allowed when the
 * ACL for the node @iter points to has been retrieved already with
 * inf_browser_query_acl(). Use inf_browser_has_acl() to check whether this
 * function can be called or whether the ACL needs to be queried first.
 *
 * The request might either finish during the call to this function, in which
 * case @func will be called and %NULL being returned. If the request does not
 * finish within the function call, a #InfRequest object is returned,
 * where @func has been installed for the #InfRequest::finished signal,
 * so that it is called as soon as the request finishes.
 *
 * Returns: (transfer none) (allow-none): A #InfRequest which can be used to
 * be notified when the request finishes.
 */
InfRequest*
inf_browser_set_acl(InfBrowser* browser,
                    const InfBrowserIter* iter,
                    const InfAclSheetSet* sheet_set,
                    InfRequestFunc func,
                    gpointer user_data)
{
  InfBrowserInterface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(iter != NULL, NULL);
  g_return_val_if_fail(sheet_set != NULL, NULL);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->set_acl != NULL, NULL);

  return iface->set_acl(browser, iter, sheet_set, func, user_data);
}

/**
 * inf_browser_check_acl:
 * @browser: A #InfBrowser.
 * @iter: A #InfBrowserIter pointing to a node in a browser.
 * @account: The ID of the account whose permission to check, or %NULL.
 * @check_mask: A bitmask of #InfAclSetting<!-- -->s with permissions to
 * check.
 * @out_mask: (out): Output parameter with the granted permissions, or %NULL.
 *
 * Checks whether the given account has permissions to perform the operations
 * specified by @mask on the node @iter points to. The @mask parameter
 * should have all permissions enabled that are to be checked. The function
 * will then write those permissions that are actually granted to the
 * mask specified by the @out_mask parameter.
 *
 * The function returns %TRUE if all permissions asked for are granted, i.e.
 * when *@out_mask equals *@mask after the function call. The @out_mask
 * parameter is allowed to be %NULL which is useful if only the return value
 * is of interest.
 *
 * In order for this function to work, the ACL sheet for @account has to be
 * available for the node @iter points to and all of its parent nodes. If
 * @account is not the default or the local account, these need to be queried
 * before using inf_browser_query_acl().
 *
 * If account is 0, it is assumed that local access to the directory is
 * available and the function always returns %TRUE.
 *
 * Returns: %TRUE if all checked permissions are granted, or %FALSE otherwise.
 */
gboolean
inf_browser_check_acl(InfBrowser* browser,
                      const InfBrowserIter* iter,
                      InfAclAccountId account,
                      const InfAclMask* check_mask,
                      InfAclMask* out_mask)
{
  const InfAclAccount* default_account;
  InfBrowserIter check_iter;
  InfAclMask remaining_mask;
  InfAclMask perms;
  const InfAclSheetSet* sheet_set;
  const InfAclSheet* sheet;
  InfAclMask temp_mask;

  g_return_val_if_fail(INF_IS_BROWSER(browser), FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);
  g_return_val_if_fail(check_mask != NULL, FALSE);

  if(account == 0)
  {
    if(out_mask != NULL)
      *out_mask = *check_mask;
    return TRUE;
  }

  default_account = inf_browser_get_acl_default_account(browser);
  if(default_account->id == account)
    default_account = NULL;

  remaining_mask = *check_mask;
  perms = *check_mask;
  check_iter = *iter;

  do
  {
    g_return_val_if_fail(
      inf_browser_has_acl(browser, &check_iter, account),
      FALSE
    );

    sheet_set = inf_browser_get_acl(browser, &check_iter);
    if(sheet_set != NULL)
    {
      sheet = inf_acl_sheet_set_find_const_sheet(sheet_set, account);
      if(sheet != NULL)
      {
        inf_acl_mask_and(&sheet->mask, &remaining_mask, &temp_mask);
        inf_acl_mask_neg(&temp_mask, &temp_mask);
        inf_acl_mask_or(&sheet->perms, &temp_mask, &temp_mask);
        inf_acl_mask_and(&perms, &temp_mask, &perms);
        
        inf_acl_mask_neg(&sheet->mask, &temp_mask);
        inf_acl_mask_and(&remaining_mask, &temp_mask, &remaining_mask);
      }

      if(!inf_acl_mask_empty(&remaining_mask) && default_account != NULL)
      {
        sheet = inf_acl_sheet_set_find_const_sheet(
          sheet_set,
          default_account->id
        );

        if(sheet != NULL)
        {
          inf_acl_mask_and(&sheet->mask, &remaining_mask, &temp_mask);
          inf_acl_mask_neg(&temp_mask, &temp_mask);
          inf_acl_mask_or(&sheet->perms, &temp_mask, &temp_mask);
          inf_acl_mask_and(&perms, &temp_mask, &perms);
        
          inf_acl_mask_neg(&sheet->mask, &temp_mask);
          inf_acl_mask_and(&remaining_mask, &temp_mask, &remaining_mask);
        }
      }
    }
  } while(!inf_acl_mask_empty(&remaining_mask) &&
          inf_browser_get_parent(browser, &check_iter));

  g_assert(inf_acl_mask_empty(&remaining_mask));

  if(out_mask != NULL)
    *out_mask = perms;

  if(inf_acl_mask_equal(&perms, check_mask))
    return TRUE;
  return FALSE;
}

/**
 * inf_browser_error:
 * @browser: A #InfBrowser.
 * @error: A #GError explaining what went wronig.
 *
 * This function emits the #InfBrowser::error signal on @browser. It is meant
 * to be used by interface implementations only.
 */
void
inf_browser_error(InfBrowser* browser,
                  const GError* error)
{
  g_return_if_fail(INF_IS_BROWSER(browser));
  g_return_if_fail(error != NULL);

  g_signal_emit(
    browser,
    browser_signals[ERROR_],
    0,
    error
  );
}

/**
 * inf_browser_node_added:
 * @browser: A #InfBrowser.
 * @iter: A #InfBrowserIter pointing to the newly added node.
 * @request: The #InfRequest that was used to add or explore the node,
 * or %NULL.
 *
 * This function emits the #InfBrowser::node-added signal on @browser. It is
 * meant to be used by interface implementations only.
 */
void
inf_browser_node_added(InfBrowser* browser,
                       const InfBrowserIter* iter,
                       InfRequest* request)
{
  g_return_if_fail(INF_IS_BROWSER(browser));
  g_return_if_fail(iter != NULL);
  g_return_if_fail(request == NULL || INF_IS_REQUEST(request));

  g_signal_emit(
    browser,
    browser_signals[NODE_ADDED],
    0,
    iter,
    request
  );
}

/**
 * inf_browser_node_removed:
 * @browser: A #InfBrowser.
 * @iter: A #InfBrowserIter pointing to the node to be removed.
 * @request: The #InfRequest that was used to delete the node, or %NULL.
 *
 * This function emits the #InfBrowser::node-removed signal on @browser. It
 * is meant to be used by interface implementations only.
 */
void
inf_browser_node_removed(InfBrowser* browser,
                         const InfBrowserIter* iter,
                         InfRequest* request)
{
  g_return_if_fail(INF_IS_BROWSER(browser));
  g_return_if_fail(iter != NULL);
  g_return_if_fail(request == NULL || INF_IS_REQUEST(request));

  g_signal_emit(
    browser,
    browser_signals[NODE_REMOVED],
    0,
    iter,
    request
  );
}

/**
 * inf_browser_subscribe_session:
 * @browser: A #InfBrowser.
 * @iter: A #InfBrowserIter pointing to the node to whose session a
 * subscription was made, or %NULL.
 * @proxy: A session proxy for the newly subscribed session.
 * @request: The #InfRequest that was used to initiate the subscription,
 * or %NULL.
 *
 * This function emits the #InfBrowser::subscribe-session signal on @browser.
 * It is meant to be used by interface implementations only. 
 */
void
inf_browser_subscribe_session(InfBrowser* browser,
                              const InfBrowserIter* iter,
                              InfSessionProxy* proxy,
                              InfRequest* request)
{
  g_return_if_fail(INF_IS_BROWSER(browser));
  g_return_if_fail(G_IS_OBJECT(proxy));
  g_return_if_fail(request == NULL || INF_IS_REQUEST(request));

  g_signal_emit(
    browser,
    browser_signals[SUBSCRIBE_SESSION],
    0,
    iter,
    proxy,
    request
  );
}

/**
 * inf_browser_unsubscribe_session:
 * @browser: A #InfBrowser.
 * @iter: A #InfBrowserIter pointing to the node to whose session the
 * subscription was removed, or %NULL.
 * @proxy: A session proxy for the unsubscribed session.
 * @request: The #InfRequest due to which the session was unsubscribed,
 * or %NULL.
 *
 * This function emits the #InfBrowser::unsubscribe-session signal on
 * @browser. It is meant to be used by interface implementations only.
 */
void
inf_browser_unsubscribe_session(InfBrowser* browser,
                                const InfBrowserIter* iter,
                                InfSessionProxy* proxy,
                                InfRequest* request)
{
  g_return_if_fail(INF_IS_BROWSER(browser));
  g_return_if_fail(G_IS_OBJECT(proxy));
  g_return_if_fail(request == NULL || INF_IS_REQUEST(request));

  g_signal_emit(
    browser,
    browser_signals[UNSUBSCRIBE_SESSION],
    0,
    iter,
    proxy,
    request
  );
}

/**
 * inf_browser_begin_request:
 * @browser: A #InfBrowser.
 * @iter: A #infBrowserIter pointing to the node for which a request was made,
 * or %NULL.
 * @request: The request which was made.
 *
 * This function emits the #InfBrowser::begin_request signal on @browser. It
 * is meant to be used by interface implementations only.
 */
void
inf_browser_begin_request(InfBrowser* browser,
                          const InfBrowserIter* iter,
                          InfRequest* request)
{
  GValue value = { 0 };

  g_return_if_fail(INF_IS_BROWSER(browser));
  g_return_if_fail(INF_IS_REQUEST(request));

  g_value_init(&value, G_TYPE_STRING);
  g_object_get_property(G_OBJECT(request), "type", &value);

  g_signal_emit(
    browser,
    browser_signals[BEGIN_REQUEST],
    g_quark_from_string(g_value_get_string(&value)),
    iter,
    request
  );

  g_value_unset(&value);
}

/**
 * inf_browser_acl_account_added:
 * @browser: A #InfBrowser.
 * @account: The new #InfAclAccount.
 * @request: The #InfRequest that was used to add the account, or %NULL.
 *
 * This function emits the #InfBrowser::acl-account-added signal on @browser.
 * It is meant to be used by interface implementations only.
 */
void
inf_browser_acl_account_added(InfBrowser* browser,
                              const InfAclAccount* account,
                              InfRequest* request)
{
  g_return_if_fail(INF_IS_BROWSER(browser));
  g_return_if_fail(account != NULL);
  g_return_if_fail(request == NULL || INF_IS_REQUEST(request));

  g_signal_emit(
    browser,
    browser_signals[ACL_ACCOUNT_ADDED],
    0,
    account,
    request
  );
}

/**
 * inf_browser_acl_account_removed:
 * @browser: A #InfBrowser.
 * @account: The removed #InfAclAccount.
 * @request: The #InfRequest that was used to remove the account, or %NULL.
 *
 * This function emits the #InfBrowser::acl-account-removed signal on @browser.
 * It is meant to be used by interface implementations only.
 */
void
inf_browser_acl_account_removed(InfBrowser* browser,
                                const InfAclAccount* account,
                                InfRequest* request)
{
  g_return_if_fail(INF_IS_BROWSER(browser));
  g_return_if_fail(account != NULL);
  g_return_if_fail(request == NULL || INF_IS_REQUEST(request));

  g_signal_emit(
    browser,
    browser_signals[ACL_ACCOUNT_REMOVED],
    0,
    account,
    request
  );
}

/**
 * inf_browser_acl_local_account_changed:
 * @browser: A #InfBrowser.
 * @account: The new local #InfAclAccount.
 * @request: The #InfRequest that triggered the account change, or %NULL.
 *
 * This function emits the #InfBrowser::acl-local-account-changed signal
 * on @browser. It is meant to be used by interface implementations only.
 */
void
inf_browser_acl_local_account_changed(InfBrowser* browser,
                                      const InfAclAccount* account,
                                      InfRequest* request)
{
  g_return_if_fail(INF_IS_BROWSER(browser));
  g_return_if_fail(account != NULL);
  g_return_if_fail(request == NULL || INF_IS_REQUEST(request));

  g_signal_emit(
    browser,
    browser_signals[ACL_LOCAL_ACCOUNT_CHANGED],
    0,
    account,
    request
  );
}

/**
 * inf_browser_acl_changed:
 * @browser: A #InfBrowser.
 * @iter: An iterator pointing to the node for which the ACL has changed.
 * @sheet_set: A #InfAclSheetSet containing the changed ACL sheets.
 * @request: The #InfRequest that was used to change the ACL, or %NULL.
 *
 * This function emits the #InfBrowser::acl-changed signal on @browser. It
 * is meant to be used by interface implementations only.
 */
void
inf_browser_acl_changed(InfBrowser* browser,
                        const InfBrowserIter* iter,
                        const InfAclSheetSet* sheet_set,
                        InfRequest* request)
{
  g_return_if_fail(INF_IS_BROWSER(browser));
  g_return_if_fail(iter != NULL);
  g_return_if_fail(sheet_set != NULL);
  g_return_if_fail(request == NULL || INF_IS_REQUEST(request));

  g_signal_emit(
    browser,
    browser_signals[ACL_CHANGED],
    0,
    iter,
    sheet_set,
    request
  );
}

/* vim:set et sw=2 ts=2: */
