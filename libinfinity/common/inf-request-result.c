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
 * SECTION:inf-request-result
 * @title: Result of an asynchronous request
 * @short_description: Request results for the infinote requests
 * @include: libinfinity/common/inf-request-result.h
 * @stability: Unstable
 *
 * These functions allow to create #InfRequestResult objects and to extract
 * the resulting values from them. In general, all objects in a
 * #InfRequestResult are not referenced and must point to an existing
 * reference which is guaranteed to live as long as the #InfRequestResult
 * object stays alive. This is typically the case for the primary use case of
 * #InfRequestResult, which is to serve as a common parameter for the
 * #InfRequest::finished signal.
 **/

#include <libinfinity/common/inf-request-result.h>

struct _InfRequestResult {
  gpointer data;
  gsize len;
};

GType
inf_request_result_get_type(void)
{
  static GType request_result_type = 0;

  if(!request_result_type)
  {
    request_result_type = g_boxed_type_register_static(
      "InfRequestResult",
      (GBoxedCopyFunc)inf_request_result_copy,
      (GBoxedFreeFunc)inf_request_result_free
    );
  }

  return request_result_type;
}

/**
 * inf_request_result_new:
 * @data: The data representing the result of the request.
 * @len: The length of the data.
 *
 * This function creates a new #InfRequestResult with the given data. The
 * function takes ownership of the data which must have been allocated with
 * g_malloc(). The memory segment at @data must not hold any object references
 * or require deinitialization in a way other than with g_free().
 *
 * Under normal circumstances, this function should not be used, and instead
 * one of the inf_request_result_make_*() functions should be used. 
 *
 * Returns: A new #InfRequestResult. Free with inf_request_result_free().
 */
InfRequestResult*
inf_request_result_new(gpointer data,
                       gsize len)
{
  InfRequestResult* result;
  result = g_slice_new(InfRequestResult);
  result->data = data;
  result->len = len;
  return result;
}

/**
 * inf_request_result_copy:
 * @result: A #InfRequestResult.
 *
 * Creates a copy of @result.
 *
 * Returns: A new #InfRequestResult. Free with inf_request_result_free().
 */
InfRequestResult*
inf_request_result_copy(const InfRequestResult* result)
{
  InfRequestResult* new_result;

  g_return_val_if_fail(result != NULL, NULL);

  new_result = g_slice_new(InfRequestResult);
  new_result->data = g_memdup(result->data, result->len);
  new_result->len = result->len;
  return new_result;
}

/**
 * inf_request_result_free:
 * @result: A #InfRequestResult.
 *
 * Releases all resources associated with @result.
 */
void
inf_request_result_free(InfRequestResult* result)
{
  g_return_if_fail(result != NULL);

  g_free(result->data);
  g_slice_free(InfRequestResult, result);
}

/**
 * inf_request_result_get:
 * @result: A #InfRequestResult.
 * @length: An output parameter for the length of the result data, or %NULL.
 *
 * Returns the data of @result, as given to inf_request_result_new(). The
 * length of the data is stored in @length. Normally this function does not
 * need to be used and one of the inf_request_result_get_*() functions
 * should be used instead.
 *
 * Returns: A pointer to the request data.
 */
gconstpointer
inf_request_result_get(const InfRequestResult* result,
                       gsize* length)
{
  g_return_val_if_fail(result != NULL, NULL);

  if(length != NULL) *length = result->len;
  return result->data;
}

typedef struct _InfRequestResultAddNode InfRequestResultAddNode;
struct _InfRequestResultAddNode {
  InfBrowser* browser;
  const InfBrowserIter* iter;
  const InfBrowserIter* new_node;
};

/**
 * inf_request_result_make_add_node:
 * @browser: A #InfBrowser.
 * @iter: An iterator pointing to the node to which a node has been added.
 * @new_node: An iterator pointing to the new node.
 *
 * Creates a new #InfRequestResult for an "add-node" request, see
 * inf_browser_add_note() or inf_browser_add_subdirectory(). The
 * #InfRequestResult object is only valid as long as the caller maintains
 * a reference to @browser.
 *
 * Returns: A new #InfRequestResult. Free with inf_request_result_free().
 */
InfRequestResult*
inf_request_result_make_add_node(InfBrowser* browser,
                                 const InfBrowserIter* iter,
                                 const InfBrowserIter* new_node)
{
  InfRequestResultAddNode* data;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(iter != NULL, NULL);
  g_return_val_if_fail(new_node != NULL, NULL);

  data = g_malloc(sizeof(InfRequestResultAddNode));

  data->browser = browser;
  data->iter = iter;
  data->new_node = new_node;

  return inf_request_result_new(data, sizeof(*data));
}

/**
 * inf_request_result_get_add_node:
 * @result: A #InfRequestResult:
 * @browser: Output value of the browser that made the request, or %NULL.
 * @iter: Output value for the node to which a node has been added, or %NULL.
 * @new_node: Output value for the new node, or %NULL.
 *
 * Decomposes @result into its components. The object must have been created
 * with inf_request_result_make_add_node().
 */
void
inf_request_result_get_add_node(const InfRequestResult* result,
                                InfBrowser** browser,
                                const InfBrowserIter** iter,
                                const InfBrowserIter** new_node)
{
  const InfRequestResultAddNode* data;

  g_return_if_fail(result != NULL);
  g_return_if_fail(result->len == sizeof(InfRequestResultAddNode));
  data = (const InfRequestResultAddNode*)result->data;

  if(browser != NULL) *browser = data->browser;
  if(iter != NULL) *iter = data->iter;
  if(new_node != NULL) *new_node = data->new_node;
}

typedef struct _InfRequestResultRenameNode InfRequestResultRenameNode;
struct _InfRequestResultRenameNode {
  InfBrowser* browser;
  const InfBrowserIter* iter;
  const gchar* new_name;
};

/**
 * inf_request_result_make_add_node:
 * @browser: A #InfBrowser.
 * @iter: An iterator pointing to the node to which was renamed.
 *
 * Creates a new #InfRequestResult for an "rename-node" request, see
 * inf_browser_rename_node(). The #InfRequestResult object is only valid
 * as long as the caller maintains a reference to @browser.
 *
 * Returns: A new #InfRequestResult. Free with inf_request_result_free().
 */
InfRequestResult*
inf_request_result_make_rename_node(InfBrowser* browser,
                                    const InfBrowserIter* iter,
                                    const gchar* new_name)
{
  InfRequestResultRenameNode* data;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(iter != NULL, NULL);

  data = g_malloc(sizeof(InfRequestResultRenameNode));

  data->browser = browser;
  data->iter = iter;
  data->new_name = new_name;

  return inf_request_result_new(data, sizeof(*data));
}

/**
 * inf_request_result_get_rename_node:
 * @result: A #InfRequestResult:
 * @browser: Output value of the browser that made the request, or %NULL.
 * @iter: Output value for the node that has been renamed, or %NULL.
 * @new_name: Output value for the name of the node that has been renamed, or %NULL.
 *
 * Decomposes @result into its components. The object must have been created
 * with inf_request_result_make_rename_node().
 */
void
inf_request_result_get_rename_node(const InfRequestResult* result,
                                   InfBrowser** browser,
                                   const InfBrowserIter** iter,
                                   const gchar** new_name)
{
  const InfRequestResultRenameNode* data;

  g_return_if_fail(result != NULL);
  g_return_if_fail(result->len == sizeof(InfRequestResultRenameNode));
  data = (const InfRequestResultRenameNode*)result->data;

  if(browser != NULL) *browser = data->browser;
  if(iter != NULL) *iter = data->iter;
  if(new_name != NULL) *new_name = data->new_name;
}

typedef struct _InfRequestResultRemoveNode InfRequestResultRemoveNode;
struct _InfRequestResultRemoveNode {
  InfBrowser* browser;
  const InfBrowserIter* iter;
};

/**
 * inf_request_result_make_add_node:
 * @browser: A #InfBrowser.
 * @iter: An iterator pointing to the node to which was removed.
 *
 * Creates a new #InfRequestResult for an "remove-node" request, see
 * inf_browser_remove_node(). The #InfRequestResult object is only valid
 * as long as the caller maintains a reference to @browser.
 *
 * Returns: A new #InfRequestResult. Free with inf_request_result_free().
 */
InfRequestResult*
inf_request_result_make_remove_node(InfBrowser* browser,
                                    const InfBrowserIter* iter)
{
  InfRequestResultRemoveNode* data;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(iter != NULL, NULL);

  data = g_malloc(sizeof(InfRequestResultRemoveNode));

  data->browser = browser;
  data->iter = iter;

  return inf_request_result_new(data, sizeof(*data));
}

/**
 * inf_request_result_get_remove_node:
 * @result: A #InfRequestResult:
 * @browser: Output value of the browser that made the request, or %NULL.
 * @iter: Output value for the node that has been removed, or %NULL.
 *
 * Decomposes @result into its components. The object must have been created
 * with inf_request_result_make_remove_node().
 */
void
inf_request_result_get_remove_node(const InfRequestResult* result,
                                   InfBrowser** browser,
                                   const InfBrowserIter** iter)
{
  const InfRequestResultRemoveNode* data;

  g_return_if_fail(result != NULL);
  g_return_if_fail(result->len == sizeof(InfRequestResultRemoveNode));
  data = (const InfRequestResultRemoveNode*)result->data;

  if(browser != NULL) *browser = data->browser;
  if(iter != NULL) *iter = data->iter;
}

typedef struct _InfRequestResultExploreNode InfRequestResultExploreNode;
struct _InfRequestResultExploreNode {
  InfBrowser* browser;
  const InfBrowserIter* iter;
};

/**
 * inf_request_result_make_explore_node:
 * @browser: A #InfBrowser.
 * @iter: An iterator pointing to the node to which was explored.
 *
 * Creates a new #InfRequestResult for an "explore-node" request, see
 * inf_browser_explore_node(). The #InfRequestResult object is only valid
 * as long as the caller maintains a reference to @browser.
 *
 * Returns: A new #InfRequestResult. Free with inf_request_result_free().
 */
InfRequestResult*
inf_request_result_make_explore_node(InfBrowser* browser,
                                     const InfBrowserIter* iter)
{
  InfRequestResultExploreNode* data;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(iter != NULL, NULL);

  data = g_malloc(sizeof(InfRequestResultExploreNode));

  data->browser = browser;
  data->iter = iter;

  return inf_request_result_new(data, sizeof(*data));
}

/**
 * inf_request_result_get_explore_node:
 * @result: A #InfRequestResult:
 * @browser: Output value of the browser that made the request, or %NULL.
 * @iter: Output value for the node that has been explored, or %NULL.
 *
 * Decomposes @result into its components. The object must have been created
 * with inf_request_result_make_explore_node().
 */
void
inf_request_result_get_explore_node(const InfRequestResult* result,
                                    InfBrowser** browser,
                                    const InfBrowserIter** iter)
{
  const InfRequestResultExploreNode* data;

  g_return_if_fail(result != NULL);
  g_return_if_fail(result->len == sizeof(InfRequestResultExploreNode));
  data = (const InfRequestResultExploreNode*)result->data;

  if(browser != NULL) *browser = data->browser;
  if(iter != NULL) *iter = data->iter;
}

typedef struct _InfRequestResultSaveSession InfRequestResultSaveSession;
struct _InfRequestResultSaveSession {
  InfBrowser* browser;
  const InfBrowserIter* iter;
};

/**
 * inf_request_result_make_save_session:
 * @browser: A #InfBrowser.
 * @iter: An iterator pointing to the node to which was saved.
 *
 * Creates a new #InfRequestResult for a "save-session" request, see
 * infc_browser_save_session(). The #InfRequestResult object is only valid
 * as long as the caller maintains a reference to @browser.
 *
 * Returns: A new #InfRequestResult. Free with inf_request_result_free().
 */
InfRequestResult*
inf_request_result_make_save_session(InfBrowser* browser,
                                     const InfBrowserIter* iter)
{
  InfRequestResultSaveSession* data;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(iter != NULL, NULL);

  data = g_malloc(sizeof(InfRequestResultSaveSession));

  data->browser = browser;
  data->iter = iter;

  return inf_request_result_new(data, sizeof(*data));
}

/**
 * inf_request_result_get_save_session:
 * @result: A #InfRequestResult:
 * @browser: Output value of the browser that made the request, or %NULL.
 * @iter: Output value for the node that has been saved, or %NULL.
 *
 * Decomposes @result into its components. The object must have been created
 * with inf_request_result_make_save_session().
 */
void
inf_request_result_get_save_session(const InfRequestResult* result,
                                    InfBrowser** browser,
                                    const InfBrowserIter** iter)
{
  const InfRequestResultSaveSession* data;

  g_return_if_fail(result != NULL);
  g_return_if_fail(result->len == sizeof(InfRequestResultSaveSession));
  data = (const InfRequestResultSaveSession*)result->data;

  if(browser != NULL) *browser = data->browser;
  if(iter != NULL) *iter = data->iter;
}

typedef struct _InfRequestResultSubscribeSession
  InfRequestResultSubscribeSession;
struct _InfRequestResultSubscribeSession {
  InfBrowser* browser;
  const InfBrowserIter* iter;
  InfSessionProxy* proxy;
};

/**
 * inf_request_result_make_subscribe_session:
 * @browser: A #InfBrowser.
 * @iter: An iterator pointing to the node to which a subscription was made.
 * @proxy: The #InfSessionProxy for the subscription.
 *
 * Creates a new #InfRequestResult for a "subscribe-session" request, see
 * inf_browser_subscribe(). The #InfRequestResult object is only valid
 * as long as the caller maintains a reference to @browser and @proxy.
 *
 * Returns: A new #InfRequestResult. Free with inf_request_result_free().
 */
InfRequestResult*
inf_request_result_make_subscribe_session(InfBrowser* browser,
                                          const InfBrowserIter* iter,
                                          InfSessionProxy* proxy)
{
  InfRequestResultSubscribeSession* data;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(iter != NULL, NULL);
  g_return_val_if_fail(INF_IS_SESSION_PROXY(proxy), NULL);

  data = g_malloc(sizeof(InfRequestResultSubscribeSession));

  data->browser = browser;
  data->iter = iter;
  data->proxy = proxy;

  return inf_request_result_new(data, sizeof(*data));
}

/**
 * inf_request_result_get_subscribe_session:
 * @result: A #InfRequestResult:
 * @browser: Output value of the browser that made the request, or %NULL.
 * @iter: Output value for the node that has been subscribed to, or %NULL.
 * @proxy: Output value for the subscribed session's proxy, or %NULL.
 *
 * Decomposes @result into its components. The object must have been created
 * with inf_request_result_make_subscribe_session().
 */
void
inf_request_result_get_subscribe_session(const InfRequestResult* result,
                                         InfBrowser** browser,
                                         const InfBrowserIter** iter,
                                         InfSessionProxy** proxy)
{
  const InfRequestResultSubscribeSession* data;

  g_return_if_fail(result != NULL);
  g_return_if_fail(result->len == sizeof(InfRequestResultSubscribeSession));
  data = (const InfRequestResultSubscribeSession*)result->data;

  if(browser != NULL) *browser = data->browser;
  if(iter != NULL) *iter = data->iter;
  if(proxy != NULL) *proxy = data->proxy;
}

typedef struct _InfRequestResultSubscribeChat InfRequestResultSubscribeChat;
struct _InfRequestResultSubscribeChat {
  InfBrowser* browser;
  InfSessionProxy* proxy;
};

/**
 * inf_request_result_make_subscribe_chat:
 * @browser: A #InfBrowser.
 * @proxy: The #InfSessionProxy for the subscribed session.
 *
 * Creates a new #InfRequestResult for a "subscribe-chat" request, see
 * infc_browser_subscribe_chat(). The #InfRequestResult object is only valid
 * as long as the caller maintains a reference to @browser and @proxy.
 *
 * Returns: A new #InfRequestResult. Free with inf_request_result_free().
 */
InfRequestResult*
inf_request_result_make_subscribe_chat(InfBrowser* browser,
                                       InfSessionProxy* proxy)
{
  InfRequestResultSubscribeChat* data;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(INF_IS_SESSION_PROXY(proxy), NULL);

  data = g_malloc(sizeof(InfRequestResultSubscribeChat));

  data->browser = browser;
  data->proxy = proxy;

  return inf_request_result_new(data, sizeof(*data));
}

/**
 * inf_request_result_get_subscribe_chat:
 * @result: A #InfRequestResult:
 * @browser: Output value of the browser that made the request, or %NULL.
 * @proxy: Output value for the subscribed session's proxy, or %NULL.
 *
 * Decomposes @result into its components. The object must have been created
 * with inf_request_result_make_subscribe_chat().
 */
void
inf_request_result_get_subscribe_chat(const InfRequestResult* result,
                                      InfBrowser** browser,
                                      InfSessionProxy** proxy)
{
  const InfRequestResultSubscribeChat* data;

  g_return_if_fail(result != NULL);
  g_return_if_fail(result->len == sizeof(InfRequestResultSubscribeChat));
  data = (const InfRequestResultSubscribeChat*)result->data;

  if(browser != NULL) *browser = data->browser;
  if(proxy != NULL) *proxy = data->proxy;
}

typedef struct _InfRequestResultQueryAclAccountList
  InfRequestResultQueryAclAccountList;
struct _InfRequestResultQueryAclAccountList {
  InfBrowser* browser;
};

/**
 * inf_request_result_make_query_acl_account_list:
 * @browser: A #InfBrowser.
 *
 * Creates a new #InfRequestResult for a "query-acl-account-list" request, see
 * inf_browser_query_acl_account_list(). The #InfRequestResult object is only valid
 * as long as the caller maintains a reference to @browser.
 *
 * Returns: A new #InfRequestResult. Free with inf_request_result_free().
 */
InfRequestResult*
inf_request_result_make_query_acl_account_list(InfBrowser* browser)
{
  InfRequestResultQueryAclAccountList* data;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);

  data = g_malloc(sizeof(InfRequestResultQueryAclAccountList));

  data->browser = browser;

  return inf_request_result_new(data, sizeof(*data));
}

/**
 * inf_request_result_get_query_acl_account_list:
 * @result: A #InfRequestResult:
 * @browser: Output value of the browser that made the request, or %NULL.
 *
 * Decomposes @result into its components. The object must have been created
 * with inf_request_result_make_query_acl_account_list().
 */
void
inf_request_result_get_query_acl_account_list(const InfRequestResult* result,
                                              InfBrowser** browser)
{
  const InfRequestResultQueryAclAccountList* data;

  g_return_if_fail(result != NULL);
  g_return_if_fail(
    result->len == sizeof(InfRequestResultQueryAclAccountList)
  );

  data = (const InfRequestResultQueryAclAccountList*)result->data;

  if(browser != NULL) *browser = data->browser;
}

typedef struct _InfRequestResultCreateAclAccount
  InfRequestResultCreateAclAccount;
struct _InfRequestResultCreateAclAccount {
  InfBrowser* browser;
  const InfAclAccount* account;
  InfCertificateChain* certificate;
};

/**
 * inf_request_result_make_create_acl_account:
 * @browser: A #InfBrowser.
 * @account: The created #InfAclAccount.
 * @certificate: The certificate which can be used to log into @account.
 *
 * Creates a new #InfRequestResult for a "create-acl-account" request, see
 * inf_browser_create_acl_account(). The #InfRequestResult object is only valid
 * as long as the caller maintains a reference to @browser.
 *
 * Returns: A new #InfRequestResult. Free with inf_request_result_free().
 */
InfRequestResult*
inf_request_result_make_create_acl_account(InfBrowser* browser,
                                           const InfAclAccount* account,
                                           InfCertificateChain* certificate)
{
  InfRequestResultCreateAclAccount* data;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(certificate != NULL, NULL);

  data = g_malloc(sizeof(InfRequestResultCreateAclAccount));

  data->browser = browser;
  data->account = account;
  data->certificate = certificate;

  return inf_request_result_new(data, sizeof(*data));
}

/**
 * inf_request_result_get_create_acl_account:
 * @result: A #InfRequestResult:
 * @browser: Output value of the browser that made the request, or %NULL.
 * @account: Output value for the created #InfAclAccount, or %NULL.
 * @certificate: Output value for the certificate which can be used to log
 * into the account, or %NULL.
 *
 * Decomposes @result into its components. The object must have been created
 * with inf_request_result_make_create_acl_account().
 */
void
inf_request_result_get_create_acl_account(const InfRequestResult* result,
                                          InfBrowser** browser,
                                          const InfAclAccount** account,
                                          InfCertificateChain** certificate)
{
  const InfRequestResultCreateAclAccount* data;

  g_return_if_fail(result != NULL);
  g_return_if_fail(result->len == sizeof(InfRequestResultCreateAclAccount));

  data = (const InfRequestResultCreateAclAccount*)result->data;

  if(browser != NULL) *browser = data->browser;
  if(account != NULL) *account = data->account;
  if(certificate != NULL) *certificate = data->certificate;
}

typedef struct _InfRequestResultRemoveAclAccount
  InfRequestResultRemoveAclAccount;
struct _InfRequestResultRemoveAclAccount {
  InfBrowser* browser;
  const InfAclAccount* account;
};

/**
 * inf_request_result_make_remove_acl_account:
 * @browser: A #InfBrowser.
 * @account: The removed #InfAclAccount.
 *
 * Creates a new #InfRequestResult for a "remove-acl-account" request, see
 * inf_browser_remove_acl_account(). The #InfRequestResult object is only valid
 * as long as the caller maintains a reference to @browser.
 *
 * Returns: A new #InfRequestResult. Free with inf_request_result_free().
 */
InfRequestResult*
inf_request_result_make_remove_acl_account(InfBrowser* browser,
                                           const InfAclAccount* account)
{
  InfRequestResultRemoveAclAccount* data;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(account != NULL, NULL);

  data = g_malloc(sizeof(InfRequestResultRemoveAclAccount));

  data->browser = browser;
  data->account = account;

  return inf_request_result_new(data, sizeof(*data));
}

/**
 * inf_request_result_get_remove_acl_account:
 * @result: A #InfRequestResult:
 * @browser: Output value of the browser that made the request, or %NULL.
 * @account: Output value for the removed #InfAclAccount, or %NULL.
 *
 * Decomposes @result into its components. The object must have been created
 * with inf_request_result_make_remove_acl_account().
 */
void
inf_request_result_get_remove_acl_account(const InfRequestResult* result,
                                          InfBrowser** browser,
                                          const InfAclAccount** account)
{
  const InfRequestResultRemoveAclAccount* data;

  g_return_if_fail(result != NULL);
  g_return_if_fail(result->len == sizeof(InfRequestResultRemoveAclAccount));

  data = (const InfRequestResultRemoveAclAccount*)result->data;

  if(browser != NULL) *browser = data->browser;
  if(account != NULL) *account = data->account;
}

typedef struct _InfRequestResultQueryAcl InfRequestResultQueryAcl;
struct _InfRequestResultQueryAcl {
  InfBrowser* browser;
  const InfBrowserIter* iter;
  const InfAclSheetSet* sheet_set;
};

/**
 * inf_request_result_make_query_acl:
 * @browser: A #InfBrowser.
 * @iter: An iterator pointing to the node whose ACL was queried.
 * @sheet_set: The sheet set for the queried node.
 *
 * Creates a new #InfRequestResult for a "query-acl" request, see
 * inf_browser_query_acl(). The #InfRequestResult object is only valid
 * as long as the caller maintains a reference to @browser and @proxy.
 *
 * Returns: A new #InfRequestResult. Free with inf_request_result_free().
 */
InfRequestResult*
inf_request_result_make_query_acl(InfBrowser* browser,
                                  const InfBrowserIter* iter,
                                  const InfAclSheetSet* sheet_set)
{
  InfRequestResultQueryAcl* data;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(iter != NULL, NULL);

  data = g_malloc(sizeof(InfRequestResultQueryAcl));

  data->browser = browser;
  data->iter = iter;
  data->sheet_set = sheet_set;

  return inf_request_result_new(data, sizeof(*data));
}

/**
 * inf_request_result_get_query_acl:
 * @result: A #InfRequestResult:
 * @browser: Output value of the browser that made the request, or %NULL.
 * @iter: Output value for the node whose ACL was queried.
 * @sheet_set: Output value for the node's ACL sheets.
 *
 * Decomposes @result into its components. The object must have been created
 * with inf_request_result_make_query_acl().
 */
void
inf_request_result_get_query_acl(const InfRequestResult* result,
                                 InfBrowser** browser,
                                 const InfBrowserIter** iter,
                                 const InfAclSheetSet** sheet_set)
{
  const InfRequestResultQueryAcl* data;

  g_return_if_fail(result != NULL);
  g_return_if_fail(result->len == sizeof(InfRequestResultQueryAcl));

  data = (const InfRequestResultQueryAcl*)result->data;

  if(browser != NULL) *browser = data->browser;
  if(iter != NULL) *iter = data->iter;
  if(sheet_set != NULL) *sheet_set = data->sheet_set;
}

typedef struct _InfRequestResultSetAcl InfRequestResultSetAcl;
struct _InfRequestResultSetAcl {
  InfBrowser* browser;
  const InfBrowserIter* iter;
};

/**
 * inf_request_result_make_set_acl:
 * @browser: A #InfBrowser.
 * @iter: An iterator pointing to the node whose ACL was set.
 *
 * Creates a new #InfRequestResult for a "set-acl" request, see
 * inf_browser_set_acl(). The #InfRequestResult object is only valid
 * as long as the caller maintains a reference to @browser and @proxy.
 *
 * Returns: A new #InfRequestResult. Free with inf_request_result_free().
 */
InfRequestResult*
inf_request_result_make_set_acl(InfBrowser* browser,
                                const InfBrowserIter* iter)
{
  InfRequestResultSetAcl* data;

  g_return_val_if_fail(INF_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(iter != NULL, NULL);

  data = g_malloc(sizeof(InfRequestResultSetAcl));

  data->browser = browser;
  data->iter = iter;

  return inf_request_result_new(data, sizeof(*data));
}

/**
 * inf_request_result_get_set_acl:
 * @result: A #InfRequestResult:
 * @browser: Output value of the browser that made the request, or %NULL.
 * @iter: Output value for the node whose ACL was set.
 *
 * Decomposes @result into its components. The object must have been created
 * with inf_request_result_make_set_acl().
 */
void
inf_request_result_get_set_acl(const InfRequestResult* result,
                               InfBrowser** browser,
                               const InfBrowserIter** iter)
{
  const InfRequestResultSetAcl* data;

  g_return_if_fail(result != NULL);
  g_return_if_fail(result->len == sizeof(InfRequestResultSetAcl));

  data = (const InfRequestResultSetAcl*)result->data;

  if(browser != NULL) *browser = data->browser;
  if(iter != NULL) *iter = data->iter;
}

typedef struct _InfRequestResultJoinUser InfRequestResultJoinUser;
struct _InfRequestResultJoinUser {
  InfSessionProxy* proxy;
  InfUser* user;
};

/**
 * inf_request_result_make_join_user:
 * @proxy: A #InfSessionProxy.
 * @user: The joined user.
 *
 * Creates a new #InfRequestResult for a "join-user" request, see
 * inf_session_proxy_join_user(). The #InfRequestResult object is only valid
 * as long as the caller maintains a reference to @proxy.
 *
 * Returns: A new #InfRequestResult. Free with inf_request_result_free().
 */
InfRequestResult*
inf_request_result_make_join_user(InfSessionProxy* proxy,
                                  InfUser* user)
{
  InfRequestResultJoinUser* data;

  g_return_val_if_fail(INF_IS_SESSION_PROXY(proxy), NULL);
  g_return_val_if_fail(user != NULL, NULL);

  data = g_malloc(sizeof(InfRequestResultJoinUser));

  data->proxy = proxy;
  data->user = user;

  return inf_request_result_new(data, sizeof(*data));
}

/**
 * inf_request_result_get_join_user:
 * @result: A #InfRequestResult:
 * @proxy: Output value of the session proxy that made the request, or %NULL.
 * @user: Output value for the joined user.
 *
 * Decomposes @result into its components. The object must have been created
 * with inf_request_result_make_join_user().
 */
void
inf_request_result_get_join_user(const InfRequestResult* result,
                                 InfSessionProxy** proxy,
                                 InfUser** user)
{
  const InfRequestResultJoinUser* data;

  g_return_if_fail(result != NULL);
  g_return_if_fail(result->len == sizeof(InfRequestResultJoinUser));

  data = (const InfRequestResultJoinUser*)result->data;

  if(proxy != NULL) *proxy = data->proxy;
  if(user != NULL) *user = data->user;
}

/* vim:set et sw=2 ts=2: */
