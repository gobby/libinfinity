/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2010 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INF_SASL_CONTEXT_H__
#define __INF_SASL_CONTEXT_H__

#include <libinfinity/common/inf-io.h>

#include <glib-object.h>

#include <gsasl.h>

G_BEGIN_DECLS

#define INF_TYPE_SASL_CONTEXT                (inf_sasl_context_get_type())

/**
 * InfSaslContext:
 *
 * #InfSaslContext is an opaque data type. You should only access it via the
 * public API functions.
 */
typedef struct _InfSaslContext InfSaslContext;

/**
 * InfSaslContextSession:
 *
 * #InfSaslContextSession represents an ongoing authentication session. Create
 * with inf_sasl_context_server_start_session() or
 * inf_sasl_context_client_start_session().
 */
typedef struct _InfSaslContextSession InfSaslContextSession;

/**
 * InfSaslContextCallbackFunc:
 * @session: A #InfSaslContextSession.
 * @property: The property requested.
 * @session_data: The session data for session specified in
 * inf_sasl_context_server_start_session() or
 * inf_sasl_context_client_start_session().
 * @user_data: The user data specified in inf_sasl_context_set_callback().
 *
 * This callback is called whenever a property is required to proceed with
 * authentication. For example, when a password is required, the callback is
 * called with @property set to %GSASL_PASSCODE.
 *
 * The function is then expected to set that property using
 * inf_sasl_context_session_set_property() and, once it is done, call
 * inf_sasl_context_session_continue(). This can happen fully asynchronously,
 * that is it does not need to take place directly within the callback but the
 * callback can, for example, open a dialog for the user to enter a password
 * and then once the user closes the dialog call the two functions mentioned
 * above.
 */
typedef void(*InfSaslContextCallbackFunc)(InfSaslContextSession* session,
                                          Gsasl_property property,
                                          gpointer session_data,
                                          gpointer user_data);

/**
 * InfSaslContextSessionFeedFunc:
 * @session: A #InfSaslContextSession.
 * @data: The response to the fed data, base64 encoded and null-terminated.
 * @needs_more: If %TRUE then inf_sasl_context_session_feed() needs to be
 * called again with more data, otherwise the authentication has finished.
 * @error: This is nonzero if an error occured while processing the input
 * data.
 * @user_data: The user data specified in inf_sasl_context_session_feed().
 *
 * This function is called in response to inf_sasl_context_session_feed().
 * When all required properties (if any) have been provided by the callback
 * function then this function is called with the response to send to the
 * remote site.
 *
 * If an error occurred then @error will be set and @data will be %NULL.
 */
typedef void(*InfSaslContextSessionFeedFunc)(InfSaslContextSession* session,
                                             const char* data,
                                             gboolean needs_more,
                                             const GError* error,
                                             gpointer user_data);

GType
inf_sasl_context_get_type(void) G_GNUC_CONST;

InfSaslContext*
inf_sasl_context_new(GError** error);

InfSaslContext*
inf_sasl_context_ref(InfSaslContext* context);

void
inf_sasl_context_unref(InfSaslContext* context);

void
inf_sasl_context_set_callback(InfSaslContext* context,
                              InfSaslContextCallbackFunc callback,
                              gpointer user_data);

InfSaslContextSession*
inf_sasl_context_client_start_session(InfSaslContext* context,
                                      InfIo* io,
                                      const char* mech,
                                      gpointer session_data,
                                      GError** error);

char*
inf_sasl_context_client_list_mechanisms(InfSaslContext* context,
                                        GError** error);

gboolean
inf_sasl_context_client_supports_mechanism(InfSaslContext* context,
                                           const char* mech);

const char*
inf_sasl_context_client_suggest_mechanism(InfSaslContext* context,
                                          const char* mechanisms);

InfSaslContextSession*
inf_sasl_context_server_start_session(InfSaslContext* context,
                                      InfIo* io,
                                      const char* mech,
                                      gpointer session_data,
                                      GError** error);

char*
inf_sasl_context_server_list_mechanisms(InfSaslContext* context,
                                        GError** error);

gboolean
inf_sasl_context_server_supports_mechanism(InfSaslContext* context,
                                           const char* mech);

void
inf_sasl_context_stop_session(InfSaslContext* context,
                              InfSaslContextSession* session);

const char*
inf_sasl_context_session_get_property(InfSaslContextSession* session,
                                      Gsasl_property prop);

void
inf_sasl_context_session_set_property(InfSaslContextSession* session,
                                      Gsasl_property prop,
                                      const char* value);

void
inf_sasl_context_session_continue(InfSaslContextSession* session,
                                  int retval);

void
inf_sasl_context_session_feed(InfSaslContextSession* session,
                              const char* data,
                              InfSaslContextSessionFeedFunc func,
                              gpointer user_data);

G_END_DECLS

#endif /* __INF_SASL_CONTEXT_H__ */

/* vim:set et sw=2 ts=2: */
