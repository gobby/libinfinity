/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2011 Armin Burgmeier <armin@arbur.net>
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
#include <libinfinity/inf-marshal.h>

enum {
  ERROR_,
  NODE_ADDED,
  NODE_REMOVED,
  SUBSCRIBE_SESSION,
  BEGIN_REQUEST, /* detailed */

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
    g_string_append_c(string, '/');
    g_string_append(string, inf_browser_get_node_name(browser, iter));
  }
  else
  {
    g_string_assign(string, "/");
  }
}

static void
inf_browser_base_init(gpointer g_class)
{
  static gboolean initialized = FALSE;

  if(!initialized)
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
      G_STRUCT_OFFSET(InfBrowserIface, error),
      NULL, NULL,
      inf_marshal_VOID__POINTER,
      G_TYPE_NONE,
      1,
      G_TYPE_POINTER /* GError* */
    );

    /**
     * InfBrowser::node-added:
     * @browser: The #InfBrowser object emitting the signal.
     * @iter: An iterator pointing to the newly added node.
     *
     * This signal is emitted when a node is added to the browser.
     */
    browser_signals[NODE_ADDED] = g_signal_new(
      "node-added",
      INF_TYPE_BROWSER,
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET(InfBrowserIface, node_added),
      NULL, NULL,
      inf_marshal_VOID__BOXED,
      G_TYPE_NONE,
      1,
      INF_TYPE_BROWSER_ITER | G_SIGNAL_TYPE_STATIC_SCOPE
    );

    /**
     * InfBrowser::node-removed:
     * @browser: The #InfBrowser object emitting the signal.
     * @iter: An iterator pointing to the node being removed.
     *
     * This signal is emitted just before a node is being removed from the
     * browser. The iterator is still valid and can be used to access the
     * node which will be removed.
     */
    browser_signals[NODE_REMOVED] = g_signal_new(
      "node-removed",
      INF_TYPE_BROWSER,
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET(InfBrowserIface, node_removed),
      NULL, NULL,
      inf_marshal_VOID__BOXED,
      G_TYPE_NONE,
      1,
      INF_TYPE_BROWSER_ITER | G_SIGNAL_TYPE_STATIC_SCOPE
    );

    /**
     * InfBrowser::subscribe-session:
     * @browser: The #InfBrowser object emitting the signal.
     * @iter: An iterator pointing to the node to which a subscription.
     * was made.
     * @session: The subscribed session.
     *
     * This signal is emitted whenever the browser is subscribed to a session.
     * This can happen as a result of a inf_browser_subscribe() or
     * inf_browser_add_note() call, but it is also possible that a
     * subscription is initiated without user interaction.
     */
    browser_signals[SUBSCRIBE_SESSION] = g_signal_new(
      "subscribe-session",
      INF_TYPE_BROWSER,
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET(InfBrowserIface, subscribe_session),
      NULL, NULL,
      inf_marshal_VOID__BOXED_OBJECT,
      G_TYPE_NONE,
      2,
      INF_TYPE_BROWSER_ITER | G_SIGNAL_TYPE_STATIC_SCOPE,
      G_TYPE_OBJECT
    );

    /**
     * InfBrowser::begin-request:
     * @browser: The #InfBrowser object emitting the signal.
     * @iter: An iterator pointing to the node for which a request is made.
     * @request: The request being made.
     *
     * This signal is emitted whenever a request is made with the browser.
     * The signal is detailed with the request type. Possible request types
     * are  "add-node", "add-subdirectory", "remove-node", "explore-node" and
     * "subscribe-session".
     */
    browser_signals[BEGIN_REQUEST] = g_signal_new(
      "begin-request",
      INF_TYPE_BROWSER,
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      G_STRUCT_OFFSET(InfBrowserIface, begin_request),
      NULL, NULL,
      inf_marshal_VOID__BOXED_OBJECT,
      G_TYPE_NONE,
      2,
      INF_TYPE_BROWSER_ITER | G_SIGNAL_TYPE_STATIC_SCOPE,
      INF_TYPE_BROWSER_REQUEST
    );

    g_object_interface_install_property(
      g_class,
      g_param_spec_enum(
        "status",
        "Browser Status",
        "The connectivity status of the browser",
        INF_TYPE_BROWSER_STATUS,
        INF_BROWSER_CLOSED,
        G_PARAM_READABLE
      )
    );

    initialized = TRUE;
  }
}

GType
inf_browser_status_get_type(void)
{
  static GType browser_status_type = 0;

  if(!browser_status_type)
  {
    static const GEnumValue browser_status_values[] = {
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

    browser_status_type = g_enum_register_static(
      "InfBrowserStatus",
      browser_status_values
    );
  }

  return browser_status_type;
}

GType
inf_browser_get_type(void)
{
  static GType browser_type = 0;

  if(!browser_type)
  {
    static const GTypeInfo browser_info = {
      sizeof(InfBrowserIface),     /* class_size */
      inf_browser_base_init,       /* base_init */
      NULL,                        /* base_finalize */
      NULL,                        /* class_init */
      NULL,                        /* class_finalize */
      NULL,                        /* class_data */
      0,                           /* instance_size */
      0,                           /* n_preallocs */
      NULL,                        /* instance_init */
      NULL                         /* value_table */
    };

    browser_type = g_type_register_static(
      G_TYPE_INTERFACE,
      "InfBrowser",
      &browser_info,
      0
    );

    g_type_interface_add_prerequisite(browser_type, G_TYPE_OBJECT);
  }

  return browser_type;
}

/**
 * inf_browser_get_root:
 * @browser: A #InfBrowser.
 * @iter: An uninitialized #InfBrowserIter.
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
  InfBrowserIface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->get_root != NULL, FALSE);

  return iface->get_root(browser, iter);
}

/**
 * inf_browser_get_next:
 * @browser: A #InfBrowser.
 * @iter: A #InfBrowserIter pointing to a node inside @browser.
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
  InfBrowserIface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->get_next != NULL, FALSE);

  return iface->get_next(browser, iter);
}

/**
 * inf_browser_get_prev:
 * @browser: A #InfBrowser.
 * @iter: A #InfBrowserIter pointing to a node inside @browser.
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
  InfBrowserIface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->get_prev != NULL, FALSE);

  return iface->get_prev(browser, iter);
}

/**
 * inf_browser_get_parent:
 * @browser: A #InfBrowser.
 * @iter: A #InfBrowserIter pointing to a node inside @browser.
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
  InfBrowserIface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->get_parent != NULL, FALSE);

  return iface->get_parent(browser, iter);
}

/**
 * inf_browser_get_child:
 * @browser: A #InfBrowser.
 * @iter: A #InfBrowserIter pointing to a subdirectory node inside @browser.
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
  InfBrowserIface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->get_child != NULL, FALSE);

  return iface->get_child(browser, iter);
}

/**
 * inf_browser_explore:
 * @browser: A #InfBrowser.
 * @iter: A #InfBrowserIter pointing to a subdirectory node inside
 * @browser.
 *
 * Requests the node @iter points to to be explored. Initally, subdirectory
 * nodes are not explored, that is not known what content there is. Nodes can
 * be explored to learn about the children nodes they contain. Since exploring
 * is a potentially lengthy process involing networking or I/O with slow
 * devices this function returns a @InfBrowserRequest which can be used to
 * monitor the progress of the operation and get notified when the exploration
 * finishes. During exploration @InfBrowser::node-added signals are already
 * emitted appropriately for every child explored inside @iter.
 *
 * Returns: A #InfBrowserRequest, or %NULL if @iter points to a
 * non-subdirectory node.
 */
InfBrowserRequest*
inf_browser_explore(InfBrowser* browser,
                    const InfBrowserIter* iter)
{
  InfBrowserIface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(iter != NULL, NULL);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->explore != NULL, NULL);

  return iface->explore(browser, iter);

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
  InfBrowserIface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->get_explored != NULL, FALSE);

  return iface->get_explored(browser, iter);
}

/**
 * inf_browser_add_note:
 * @browser: A #InfBrowser.
 * @iter: A #InfBrowserIter pointing to a subdirectory node inside @browser.
 * @name: The name of the node to add.
 * @type: The type of the node to add.
 * @session: A #InfSession with a session of type @type, or %NULL.
 * @initial_subscribe: Whether to subscribe to the newly created session.
 *
 * Adds a new leaf node to the browser. The new node is of type @type. If
 * session is non-%NULL it will be used as the initial content of the new
 * node, otherwise the new node will start empty.
 *
 * Returns: A #InfBrowserRequest which can be used to get notified when the
 * request finishes.
 */
InfBrowserRequest*
inf_browser_add_note(InfBrowser* browser,
                     const InfBrowserIter* iter,
                     const char* name,
                     const char* type,
                     InfSession* session,
                     gboolean initial_subscribe)
{
  InfBrowserIface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(iter != NULL, NULL);
  g_return_val_if_fail(name != NULL, NULL);
  g_return_val_if_fail(type != NULL, NULL);
  g_return_val_if_fail(session == NULL || INF_IS_SESSION(session), NULL);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->add_note != NULL, NULL);

  return iface->add_note(
    browser,
    iter,
    name,
    type,
    session,
    initial_subscribe
  );
}

/**
 * inf_browser_add_subdirectory:
 * @browser: A #InfBrowser.
 * @iter: A #InfBrowserIter pointing to a subdirectory node inside @browser.
 * @name: The name of the node to add.
 *
 * Adds a new subdirectory node to the browser.
 *
 * Returns: A #InfBrowserRequest which can be used to get notified when the
 * request finishes.
 */
InfBrowserRequest*
inf_browser_add_subdirectory(InfBrowser* browser,
                             const InfBrowserIter* iter,
                             const char* name)
{
  InfBrowserIface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(iter != NULL, NULL);
  g_return_val_if_fail(name != NULL, NULL);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->add_subdirectory != NULL, NULL);

  return iface->add_subdirectory(browser, iter, name);
}

/**
 * inf_browser_remove_node:
 * @browser: A #InfBrowser.
 * @iter: A #InfBrowserIter pointing to a node inside @browser.
 *
 * Requests to remove the node @iter points to. It may point to a
 * subdirectory node in which case all its children are removed recursively
 * as well.
 *
 * Returns: A #InfBrowserRequest which can be used to get notified when the
 * request finishes.
 */
InfBrowserRequest*
inf_browser_remove_node(InfBrowser* browser,
                        const InfBrowserIter* iter)
{
  InfBrowserIface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(iter != NULL, NULL);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->remove_node != NULL, NULL);

  return iface->remove_node(browser, iter);
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
  InfBrowserIface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(iter != NULL, NULL);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->get_node_name != NULL, NULL);

  return iface->get_node_name(browser, iter);
}

/**
 * inf_browser_get_node_type:
 * @browser: A #InfBrowser.
 * @iter: A #InfBrowserIter pointing to a node inside @browser.
 *
 * Returns the type of the node @iter points to. If this is a subdirectory
 * node then it will be "InfSubdirectory" or otherwise the type of the
 * note.
 *
 * Returns: The node type as a string.
 */
const gchar*
inf_browser_get_node_type(InfBrowser* browser,
                          const InfBrowserIter* iter)
{
  InfBrowserIface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(iter != NULL, NULL);

  iface = INF_BROWSER_GET_IFACE(browser);
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
 * Returns: The path as a string. Free with g_free() when no longer needed.
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
 *
 * Attempts to subscribe to the node @iter points to, i.e. obtain a
 * #InfSession representing its content. This also allows to change the
 * content of the node.
 *
 * Returns: A #InfBrowserRequest which can be used to get notified when the
 * request finishes.
 */
InfBrowserRequest*
inf_browser_subscribe(InfBrowser* browser,
                      const InfBrowserIter* iter)
{
  InfBrowserIface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(iter != NULL, NULL);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->subscribe != NULL, NULL);

  return iface->subscribe(browser, iter);
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
 * Returns: A @GObject which contains the session. This is either a
 * #InfcSessionProxy or a #InfdSessionProxy (TODO: Make a common base class
 * or interface).
 */
GObject*
inf_browser_get_session(InfBrowser* browser,
                        const InfBrowserIter* iter)
{
  InfBrowserIface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(iter != NULL, NULL);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->get_session != NULL, NULL);

  return iface->get_session(browser, iter);
}

/**
 * inf_browser_list_pending_requests:
 * @browser: A #InfBrowser.
 * @iter: A #InfBrowserIter pointing to a node inside @browser.
 * @request_type: The type of request to return pending requests for, or
 * %NULL.
 *
 * Returns a list of all pending requests for the node @iter points to which
 * match type @request_type. A pending request is a request which has been
 * created but has not yet finished. @request_type can be %NULL in which case
 * all requests for the given node are returned. If it is non-%NULL it should
 * be one of "add-node", "add-subdirectory", "remove-node", "explore-node" or
 * "subscribe-session". In this case only requests which match the given type
 * are included in the list of returned requests.
 *
 * Returns: A list #InfBrowserRequest<!-- -->s. Free with g_slist_free() when
 * no longer needed.
 */
GSList*
inf_browser_list_pending_requests(InfBrowser* browser,
                                  const InfBrowserIter* iter,
                                  const gchar* request_type)
{
  InfBrowserIface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(iter != NULL, NULL);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->list_pending_requests != NULL, NULL);

  return iface->list_pending_requests(browser, iter, request_type);
}

/**
 * inf_browser_iter_from_request:
 * @browser: A #InfBrowser.
 * @request: A #InfBrowserRequest which has not yet finished and which was
 * issued by @browser.
 * @iter: An uninitialized #InfBrowserIter.
 *
 * Sets @iter to the node for which @request was made. If that node does not
 * exist anymore or if @request has already finished the function returns
 * %FALSE and @iter is left untouched.
 *
 * Returns: %TRUE if @iter was moved or %FALSE otherwise.
 */
gboolean
inf_browser_iter_from_request(InfBrowser* browser,
                              InfBrowserRequest* request,
                              InfBrowserIter* iter)
{
  InfBrowserIface* iface;

  g_return_val_if_fail(INF_IS_BROWSER(browser), FALSE);
  g_return_val_if_fail(INF_IS_BROWSER_REQUEST(request), FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);

  iface = INF_BROWSER_GET_IFACE(browser);
  g_return_val_if_fail(iface->iter_from_request != NULL, FALSE);

  return iface->iter_from_request(browser, request, iter);
}

/**
 * inf_browser_get_pending_explore_request:
 * @browser: A #InfBrowser.
 * @iter: A #InfBrowserIter pointing to a subdirectory node inside @browser.
 *
 * Returns a pending explore request for the subdirectory @iter points to. If
 * there is no such request the function returns %NULL. This function is a
 * shortcut for calling inf_browser_list_pending_requests() with
 * "explore-node" as request type and retrieving the first item from the list.
 * Note that there can be at most one explore request for any node.
 *
 * Returns: A #InfBrowserRequest, or %NULL.
 */
InfBrowserRequest*
inf_browser_get_pending_explore_request(InfBrowser* browser,
                                        const InfBrowserIter* iter)
{
  GSList* list;
  InfBrowserRequest* request;

  g_return_val_if_fail(INF_IS_BROWSER(browser),NULL);
  g_return_val_if_fail(iter != NULL, NULL);

  list = inf_browser_list_pending_requests(browser, iter, "explore-node");
  g_assert(list == NULL || list->next == NULL);

  request = NULL;
  if(list != NULL)
    request = (InfBrowserRequest*)list->data;

  g_slist_free(list);
  return request;
}

/**
 * inf_browser_get_pending_subscribe_request:
 * @browser: A #InfBrowser.
 * @iter: A #InfBrowserIter pointing to a leaf node inside @browser.
 *
 * Returns a pending subscribe request for the note @iter points to. If
 * there is no such request the function returns %NULL. This function is a
 * shortcut for calling inf_browser_list_pending_requests() with
 * "subscribe-session" as request type and retrieving the first item from the
 * list. Note that there can be at most one subscribe request for any node.
 *
 * Returns: A #InfBrowserRequest, or %NULL.
 */
InfBrowserRequest*
inf_browser_get_pending_subscribe_request(InfBrowser* browser,
                                          const InfBrowserIter* iter)
{
  GSList* list;
  InfBrowserRequest* request;

  g_return_val_if_fail(INF_IS_BROWSER(browser),NULL);
  g_return_val_if_fail(iter != NULL, NULL);

  list = inf_browser_list_pending_requests(
    browser,
    iter,
    "subscribe-session"
  );

  g_assert(list == NULL || list->next == NULL);

  request = NULL;
  if(list != NULL)
    request = (InfBrowserRequest*)list->data;

  g_slist_free(list);
  return request;
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
 *
 * This function emits the #InfBrowser::node-added signal on @browser. It is
 * meant to be used by interface implementations only.
 */
void
inf_browser_node_added(InfBrowser* browser,
                       InfBrowserIter* iter)
{
  g_return_if_fail(INF_IS_BROWSER(browser));
  g_return_if_fail(iter != NULL);

  g_signal_emit(
    browser,
    browser_signals[NODE_ADDED],
    0,
    iter
  );
}

/**
 * inf_browser_node_removed:
 * @browser: A #InfBrowser.
 * @iter: A #InfBrowserIter pointing to the node to be removed.
 *
 * This function emits the #InfBrowser::node-removed signal on @browser. It
 * is meant to be used by interface implementations only.
 */
void
inf_browser_node_removed(InfBrowser* browser,
                         InfBrowserIter* iter)
{
  g_return_if_fail(INF_IS_BROWSER(browser));
  g_return_if_fail(iter != NULL);

  g_signal_emit(
    browser,
    browser_signals[NODE_REMOVED],
    0,
    iter
  );
}

/**
 * inf_browser_subscribe_session:
 * @browser: A #InfBrowser.
 * @iter: A #InfBrowserIter pointing to the node to whose session a
 * subscription was made.
 * @proxy: A session proxy for the newly subscribed session.
 *
 * This function emits the #InfBrowser::subscribe-session signal on @browser.
 * It is meant to be used by interface implementations only.
 */
void
inf_browser_subscribe_session(InfBrowser* browser,
                              InfBrowserIter* iter,
                              GObject* proxy)
{
  g_return_if_fail(INF_IS_BROWSER(browser));
  g_return_if_fail(iter != NULL);
  g_return_if_fail(G_IS_OBJECT(proxy));

  g_signal_emit(
    browser,
    browser_signals[SUBSCRIBE_SESSION],
    0,
    iter,
    proxy
  );
}

/**
 * inf_browser_begin_request:
 * @browser: A #InfBrowser.
 * @iter: A #infBrowserIter pointing to the node for which a request was made.
 * @request: The request which was made.
 *
 * This function emits the #InfBrowser::begin_request signal on @browser. It
 * is meant to be used by interface implementations only.
 */
void
inf_browser_begin_request(InfBrowser* browser,
                          InfBrowserIter* iter,
                          InfBrowserRequest* request)
{
  GValue value = { 0 };

  g_return_if_fail(INF_IS_BROWSER(browser));
  g_return_if_fail(iter != NULL);
  g_return_if_fail(INF_IS_BROWSER_REQUEST(request));

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

/* vim:set et sw=2 ts=2: */
