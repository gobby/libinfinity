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
 * SECTION:inf-acl-account-list-request
 * @title: InfAclAccountListRequest
 * @short_description: Asynchronous request to obtain a list of accounts
 * @include: libinfinity/common/inf-acl-account-list-request.h
 * @see_also: #InfBrowser, #InfRequest
 * @stability: Unstable
 *
 * #InfAclAccountListRequest represents a request that has been made via the
 * #InfBrowser API to request the list of known users. Usually such a request
 * is asynchronous, for example because it waits for a response from an
 * infinote server or because it performs I/O. The #InfAclAccountListRequest
 * class is used to be notified when the request finishes.
 */

#include <libinfinity/common/inf-acl-account-list-request.h>
#include <libinfinity/common/inf-request.h>
#include <libinfinity/inf-marshal.h>

enum {
  FINISHED,

  LAST_SIGNAL
};

static guint acl_account_list_request_signals[LAST_SIGNAL];

static void
inf_acl_account_list_request_base_init(gpointer g_class)
{
  static gboolean initialized = FALSE;

  if(!initialized)
  {
    g_object_interface_install_property(
      g_class,
      g_param_spec_uint(
        "current",
        "Current",
        "The number of users that have so far been transferred",
        0,
        G_MAXUINT,
        0,
        G_PARAM_READABLE
      )
    );

    g_object_interface_install_property(
      g_class,
      g_param_spec_uint(
        "total",
        "Total",
        "The total number of users to be transferred",
        0,
        G_MAXUINT,
        0,
        G_PARAM_READABLE
      )
    );

    /**
     * InfAclAccountListRequest::finished:
     * @request: The #InfAclAccountListRequest which finished.
     * @error: Error information in case the request failed, or %NULL
     * otherwise.
     *
     * This signal is emitted when the request finishes. If @error is
     * non-%NULL the request failed, otherwise it finished successfully.
     */
    acl_account_list_request_signals[FINISHED] = g_signal_new(
      "finished",
      INF_TYPE_ACL_ACCOUNT_LIST_REQUEST,
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET(InfAclAccountListRequestIface, finished),
      NULL, NULL,
      inf_marshal_VOID__POINTER,
      G_TYPE_NONE,
      1,
      G_TYPE_POINTER /* GError* */
    );

    initialized = TRUE;
  }
}

GType
inf_acl_account_list_request_get_type(void)
{
  static GType acl_account_list_request_type = 0;

  if(!acl_account_list_request_type)
  {
    static const GTypeInfo acl_account_list_request_info = {
      sizeof(InfAclAccountListRequestIface),  /* class_size */
      inf_acl_account_list_request_base_init, /* base_init */
      NULL,                                   /* base_finalize */
      NULL,                                   /* class_init */
      NULL,                                   /* class_finalize */
      NULL,                                   /* class_data */
      0,                                      /* instance_size */
      0,                                      /* n_preallocs */
      NULL,                                   /* instance_init */
      NULL                                    /* value_table */
    };

    acl_account_list_request_type = g_type_register_static(
      G_TYPE_INTERFACE,
      "InfAclAccountListRequest",
      &acl_account_list_request_info,
      0
    );

    g_type_interface_add_prerequisite(
      acl_account_list_request_type,
      INF_TYPE_REQUEST
    );
  }

  return acl_account_list_request_type;
}

/**
 * inf_acl_account_list_request_finished:
 * @request: A #InfAclAccountListRequest.
 * @error: A #GError containing error information in case the request failed,
 * or %NULL otherwise.
 *
 * This function emits the #InfAclAccountListRequest::finished signal on
 * @request. It is meant to be used by interface implementations only.
 */
void
inf_acl_account_list_request_finished(InfAclAccountListRequest* request,
                                      const GError* error)
{
  g_return_if_fail(INF_IS_ACL_ACCOUNT_LIST_REQUEST(request));

  g_signal_emit(
    request,
    acl_account_list_request_signals[FINISHED],
    0,
    error
  );
}

/* vim:set et sw=2 ts=2: */
