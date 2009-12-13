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

#ifndef __INF_SESSION_H__
#define __INF_SESSION_H__

#include <libinfinity/common/inf-buffer.h>
#include <libinfinity/common/inf-user-table.h>
#include <libinfinity/common/inf-user.h>
#include <libinfinity/common/inf-xml-connection.h>
#include <libinfinity/communication/inf-communication-manager.h>
#include <libinfinity/communication/inf-communication-group.h>

#include <libxml/tree.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_SESSION                 (inf_session_get_type())
#define INF_SESSION(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_SESSION, InfSession))
#define INF_SESSION_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TYPE_SESSION, InfSessionClass))
#define INF_IS_SESSION(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_SESSION))
#define INF_IS_SESSION_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TYPE_SESSION))
#define INF_SESSION_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TYPE_SESSION, InfSessionClass))

#define INF_TYPE_SESSION_STATUS          (inf_session_status_get_type())

typedef struct _InfSession InfSession;
typedef struct _InfSessionClass InfSessionClass;

/**
 * InfSessionStatus:
 * @INF_SESSION_PRESYNC: The session is scheduled to be synchronized from a
 * remote host. This can be useful if the session is needed to be present
 * before the actual synchronization begins. Use
 * inf_session_synchronize_from() to switch to
 * %INF_SESSION_SYNCHRONIZING.
 * @INF_SESSION_SYNCHRONIZING: The session is currently being synchronized
 * from a remote host. When done synchronizing, it will enter into
 * %INF_SESSION_RUNNING state.
 * @INF_SESSION_RUNNING: The session is running and ready to synchronize
 * other hosts. If a subscription group is set
 * (see inf_session_set_subscription_group()), then changes to the
 * underlying buffer are transmitted to all subscribed connections.
 * @INF_SESSION_CLOSED: The session is closed and can no longer be used. The
 * session enters this state if the synchronization fails in
 * %INF_SESSION_SYNCHRONIZING state or inf_session_close() is called.
 *
 * #InfSessionStatus defines in what state a session is in.
 */
typedef enum _InfSessionStatus {
  INF_SESSION_PRESYNC,
  INF_SESSION_SYNCHRONIZING,
  INF_SESSION_RUNNING,
  INF_SESSION_CLOSED
} InfSessionStatus;

/**
 * InfSessionSyncStatus:
 * @INF_SESSION_SYNC_NONE: No synchronization is ongoing.
 * @INF_SESSION_SYNC_IN_PROGRESS: Synchronization is currently in progress.
 * @INF_SESSION_SYNC_AWAITING_ACK: All synchronization data has been sent
 * (progress is 1.0), but we are still waiting for an acknowledgment from the
 * remote site. Synchronization can no longer be cancelled, but it can stiff
 * fail.
 *
 * #InfSessionSyncStatus represents the status of a synchronization. It is
 * used by inf_session_get_synchronization_status().
 */
typedef enum _InfSessionSyncStatus {
  INF_SESSION_SYNC_NONE,
  INF_SESSION_SYNC_IN_PROGRESS,
  INF_SESSION_SYNC_AWAITING_ACK
} InfSessionSyncStatus;

/**
 * InfSessionSyncError:
 * @INF_SESSION_SYNC_ERROR_GOT_MESSAGE_IN_PRESYNC: Received a message
 * in state %INF_SESSION_PRESYNC. It is not processed because
 * inf_session_synchronize_from() was not yet called.
 * @INF_SESSION_SYNC_ERROR_UNEXPECTED_NODE: A message has been received that
 * was not understood.
 * @INF_SESSION_SYNC_ERROR_ID_NOT_PRESENT: An ID was not provided for a user
 * in the session.
 * @INF_SESSION_SYNC_ERROR_ID_IN_USE: The ID of a user is already in use by
 * another user.
 * @INF_SESSION_SYNC_ERROR_NAME_NOT_PRESENT: A name was not provided for a
 * user in the session.
 * @INF_SESSION_SYNC_ERROR_NAME_IN_USE: The name of a user is already in use
 * by another user.
 * @INF_SESSION_SYNC_ERROR_CONNECTION_CLOSED: The synchronization connection
 * has been closed.
 * @INF_SESSION_SYNC_ERROR_SENDER_CANCELLED: The sender has cancelled the
 * synchronization.
 * @INF_SESSION_SYNC_ERROR_RECEIVER_CANCELLED: The receiver has cancelled the
 * synchronization.
 * @INF_SESSION_SYNC_ERROR_UNEXPECTED_BEGIN_OF_SYNC: Received
 * &lt;sync-begin/&gt;
 * not a the beginning of the synchronization.
 * @INF_SESSION_SYNC_ERROR_NUM_MESSAGES_MISSING: The &lt;sync-begin/&gt;
 * message does not contain the number of synchronization messages to expect.
 * @INF_SESSION_SYNC_ERROR_UNEXPECTED_END_OF_SYNC: The &lt;sync-end/&gt;
 * message was not received at the end of the synchronization.
 * @INF_SESSION_SYNC_ERROR_EXPECTED_BEGIN_OF_SYNC: The &lt;sync-begin/&gt;
 * message was not received at the beginning of the synchronization.
 * @INF_SESSION_SYNC_ERROR_EXPECTED_END_OF_SYNC: The &lt;sync-end/&gt; message
 * was not received at the end of the synchronization.
 * @INF_SESSION_SYNC_ERROR_FAILED: Generic error code when no further reason
 * of failure is known.
 *
 * These are errors that can occur during a synchronization of a session.
 * Additional errors may occur depending on the session type.
 */
typedef enum _InfSessionSyncError {
  INF_SESSION_SYNC_ERROR_GOT_MESSAGE_IN_PRESYNC,
  INF_SESSION_SYNC_ERROR_UNEXPECTED_NODE,
  INF_SESSION_SYNC_ERROR_ID_NOT_PRESENT,
  INF_SESSION_SYNC_ERROR_ID_IN_USE,
  INF_SESSION_SYNC_ERROR_NAME_NOT_PRESENT,
  INF_SESSION_SYNC_ERROR_NAME_IN_USE,
  INF_SESSION_SYNC_ERROR_CONNECTION_CLOSED,
  INF_SESSION_SYNC_ERROR_SENDER_CANCELLED,
  INF_SESSION_SYNC_ERROR_RECEIVER_CANCELLED,
  INF_SESSION_SYNC_ERROR_UNEXPECTED_BEGIN_OF_SYNC,
  INF_SESSION_SYNC_ERROR_NUM_MESSAGES_MISSING,
  INF_SESSION_SYNC_ERROR_UNEXPECTED_END_OF_SYNC,
  INF_SESSION_SYNC_ERROR_EXPECTED_BEGIN_OF_SYNC,
  INF_SESSION_SYNC_ERROR_EXPECTED_END_OF_SYNC,

  INF_SESSION_SYNC_ERROR_FAILED
} InfSessionSyncError;

/**
 * InfSessionClass:
 * @to_xml_sync: Virtual function that saves the session within a XML
 * document. @parent is the root node of the document. It should create as
 * much nodes as possible within that root node and not in sub-nodes because
 * these are sent to a client and it is not allowed that other traffic is put
 * in between those nodes. This way, communication through the same connection
 * does not hang just because a large session is synchronized.
 * @process_xml_sync: Virtual function that is called for every node in the
 * XML document created by @to_xml_sync. It is supposed to reconstruct the
 * session content from the XML data.
 * @process_xml_run: Virtual function that is called for every received
 * message while the session is running. Return %INF_COMMUNICATION_SCOPE_GROUP
 * if the message is designated for all group members (see also
 * inf_net_object_received() on this topic).
 * @get_xml_user_props: Virtual function that creates a list of
 * #GParameter<!-- -->s for use with g_object_newv() from a XML node.
 * @set_xml_user_props: Virtual function that writes the passed user
 * properties into a XML node.
 * @validate_user_props: Virtual function that checks whether the given user
 * properties are valid for a user join. This prevents a user join if there is
 * already a user with the same name. If @exclude is not %NULL, then the 
 * function does ignore it when validating.
 * @user_new: Virtual function that creates a new user object with the given
 * properties.
 * @close: Default signal handler for the #InfSession::close signal. This
 * cancels currently running synchronization in #InfSession.
 * @synchronization_begin: Default signal handler for the
 * #InfSession::synchronization-begin signal. The default handler queues the
 * synchronization messages.
 * @synchronization_progress: Default signal handler for the
 * #InfSession::synchronization-progress signal.
 * @synchronization_complete: Default signal handler for the
 * #InfSession::synchronization-complete signal. If the session itself got
 * synchronized (and did not synchronize another session), then the default
 * handler changes status to %INF_SESSION_RUNNING.
 * @synchronization_failed: Default signal handler for the
 * #InfSession::synchronization-failed signal. If the session itself got
 * synchronized (and did not synchronize another session), then the default
 * handler changes status to %INF_SESSION_CLOSED.
 *
 * This structure contains the virtual functions and default signal handlers
 * of #InfSession.
 */
struct _InfSessionClass {
  /*< private >*/
  GObjectClass parent_class;

  /*< public >*/

  /* Virtual table */
  void(*to_xml_sync)(InfSession* session,
                     xmlNodePtr parent);

  gboolean(*process_xml_sync)(InfSession* session,
                              InfXmlConnection* connection,
                              xmlNodePtr xml,
                              GError** error);

  InfCommunicationScope(*process_xml_run)(InfSession* session,
                                          InfXmlConnection* connection,
                                          xmlNodePtr xml,
                                          GError** error);

  GArray*(*get_xml_user_props)(InfSession* session,
                               InfXmlConnection* conn,
                               xmlNodePtr xml);

  void (*set_xml_user_props)(InfSession* session,
                             const GParameter* params,
                             guint n_params,
                             xmlNodePtr xml);

  gboolean(*validate_user_props)(InfSession* session,
                                 const GParameter* params,
                                 guint n_params,
                                 InfUser* exclude,
                                 GError** error);

  InfUser*(*user_new)(InfSession* session,
                      GParameter* params,
                      guint n_params);

  /* Signals */
  void(*close)(InfSession* session);

  void(*synchronization_begin)(InfSession* session,
                               InfCommunicationGroup* group,
                               InfXmlConnection* connection);

  void(*synchronization_progress)(InfSession* session,
                                  InfXmlConnection* connection,
                                  gdouble percentage);

  void(*synchronization_complete)(InfSession* session,
                                  InfXmlConnection* connection);

  void(*synchronization_failed)(InfSession* session,
                                InfXmlConnection* connection,
                                const GError* error);
};

/**
 * InfSession:
 *
 * #InfSession is an opaque data type. You should only access it via the
 * public API functions.
 */
struct _InfSession {
  /*< private >*/
  GObject parent;
};

const GParameter*
inf_session_lookup_user_property(const GParameter* params,
                                 guint n_params,
                                 const gchar* name);

GParameter*
inf_session_get_user_property(GArray* array,
                              const gchar* name);

GType
inf_session_status_get_type(void) G_GNUC_CONST;

GType
inf_session_get_type(void) G_GNUC_CONST;

void
inf_session_user_to_xml(InfSession* session,
                        InfUser* user,
                        xmlNodePtr xml);

void
inf_session_close(InfSession* session);

InfCommunicationManager*
inf_session_get_communication_manager(InfSession* session);

InfBuffer*
inf_session_get_buffer(InfSession* session);

InfUserTable*
inf_session_get_user_table(InfSession* session);

InfSessionStatus
inf_session_get_status(InfSession* session);

InfUser*
inf_session_add_user(InfSession* session,
                     const GParameter* params,
                     guint n_params,
                     GError** error);

void
inf_session_set_user_status(InfSession* session,
                            InfUser* user,
                            InfUserStatus status);

void
inf_session_synchronize_from(InfSession* session);

void
inf_session_synchronize_to(InfSession* session,
                           InfCommunicationGroup* group,
                           InfXmlConnection* connection);

InfSessionSyncStatus
inf_session_get_synchronization_status(InfSession* session,
                                       InfXmlConnection* connection);

gdouble
inf_session_get_synchronization_progress(InfSession* session,
                                         InfXmlConnection* connection);

gboolean
inf_session_has_synchronizations(InfSession* session);

InfCommunicationGroup*
inf_session_get_subscription_group(InfSession* session);

void
inf_session_set_subscription_group(InfSession* session,
                                   InfCommunicationGroup* group);

void
inf_session_send_to_subscriptions(InfSession* session,
                                  xmlNodePtr xml);

G_END_DECLS

#endif /* __INF_SESSION_H__ */

/* vim:set et sw=2 ts=2: */
