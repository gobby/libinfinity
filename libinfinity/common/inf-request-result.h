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

#ifndef __INF_REQUEST_RESULT_H__
#define __INF_REQUEST_RESULT_H__

#include <libinfinity/common/inf-browser.h>
#include <libinfinity/common/inf-session-proxy.h>
#include <libinfinity/common/inf-request.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_REQUEST_RESULT                 (inf_request_result_get_type())

GType
inf_request_result_get_type(void) G_GNUC_CONST;

InfRequestResult*
inf_request_result_new(gpointer data,
                       gsize len);

InfRequestResult*
inf_request_result_copy(const InfRequestResult* result);

void
inf_request_result_free(InfRequestResult* result);

gconstpointer
inf_request_result_get(const InfRequestResult* result,
                       gsize* length);

InfRequestResult*
inf_request_result_make_add_node(InfBrowser* browser,
                                 const InfBrowserIter* iter,
                                 const InfBrowserIter* new_node);

void
inf_request_result_get_add_node(const InfRequestResult* result,
                                InfBrowser** browser,
                                const InfBrowserIter** iter,
                                const InfBrowserIter** new_node);

InfRequestResult*
inf_request_result_make_rename_node(InfBrowser* browser,
                                    const InfBrowserIter* iter,
				    const char* new_name);

void
inf_request_result_get_rename_node(const InfRequestResult* result,
                                   InfBrowser** browser,
                                   const InfBrowserIter** iter,
				   const char** new_name);

InfRequestResult*
inf_request_result_make_remove_node(InfBrowser* browser,
                                    const InfBrowserIter* iter);

void
inf_request_result_get_remove_node(const InfRequestResult* result,
                                   InfBrowser** browser,
                                   const InfBrowserIter** iter);

InfRequestResult*
inf_request_result_make_explore_node(InfBrowser* browser,
                                     const InfBrowserIter* iter);

void
inf_request_result_get_explore_node(const InfRequestResult* result,
                                    InfBrowser** browser,
                                    const InfBrowserIter** iter);

InfRequestResult*
inf_request_result_make_save_session(InfBrowser* browser,
                                     const InfBrowserIter* iter);

void
inf_request_result_get_save_session(const InfRequestResult* result,
                                    InfBrowser** browser,
                                    const InfBrowserIter** iter);

InfRequestResult*
inf_request_result_make_subscribe_session(InfBrowser* browser,
                                          const InfBrowserIter* iter,
                                          InfSessionProxy* proxy);

void
inf_request_result_get_subscribe_session(const InfRequestResult* result,
                                         InfBrowser** browser,
                                         const InfBrowserIter** iter,
                                         InfSessionProxy** proxy);

InfRequestResult*
inf_request_result_make_subscribe_chat(InfBrowser* browser,
                                       InfSessionProxy* proxy);

void
inf_request_result_get_subscribe_chat(const InfRequestResult* result,
                                      InfBrowser** browser,
                                      InfSessionProxy** proxy);

InfRequestResult*
inf_request_result_make_query_acl_account_list(InfBrowser* browser);

void
inf_request_result_get_query_acl_account_list(const InfRequestResult* result,
                                              InfBrowser** browser);

InfRequestResult*
inf_request_result_make_create_acl_account(InfBrowser* browser,
                                           const InfAclAccount* account,
                                           InfCertificateChain* certificate);

void
inf_request_result_get_create_acl_account(const InfRequestResult* result,
                                          InfBrowser** browser,
                                          const InfAclAccount** account,
                                          InfCertificateChain** certificate);

InfRequestResult*
inf_request_result_make_remove_acl_account(InfBrowser* browser,
                                           const InfAclAccount* account);

void
inf_request_result_get_remove_acl_account(const InfRequestResult* result,
                                          InfBrowser** browser,
                                          const InfAclAccount** account);

InfRequestResult*
inf_request_result_make_query_acl(InfBrowser* browser,
                                  const InfBrowserIter* iter,
                                  const InfAclSheetSet* sheet_set);

void
inf_request_result_get_query_acl(const InfRequestResult* result,
                                 InfBrowser** browser,
                                 const InfBrowserIter** iter,
                                 const InfAclSheetSet** sheet_set);

InfRequestResult*
inf_request_result_make_set_acl(InfBrowser* browser,
                                const InfBrowserIter* iter);

void
inf_request_result_get_set_acl(const InfRequestResult* result,
                               InfBrowser** browser,
                               const InfBrowserIter** iter);

InfRequestResult*
inf_request_result_make_join_user(InfSessionProxy* proxy,
                                  InfUser* user);

void
inf_request_result_get_join_user(const InfRequestResult* result,
                                 InfSessionProxy** proxy,
                                 InfUser** user);

G_END_DECLS

#endif /* __INF_REQUEST_RESULT_H__ */

/* vim:set et sw=2 ts=2: */
