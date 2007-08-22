/* infinote - Collaborative notetaking application
 * Copyright (C) 2007 Armin Burgmeier
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
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __INF_SESSION_H__
#define __INF_SESSION_H__

#include <libinfinity/common/inf-connection-manager.h>
#include <libinfinity/common/inf-buffer.h>
#include <libinfinity/common/inf-user.h>
#include <libinfinity/common/inf-xml-connection.h>

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

typedef enum _InfSessionStatus {
  INF_SESSION_SYNCHRONIZING,
  INF_SESSION_RUNNING,
  INF_SESSION_CLOSED
} InfSessionStatus;

typedef enum _InfSessionSyncStatus {
  /* No synchronization in progress */
  INF_SESSION_SYNC_NONE,
  /* The synchronization is in progress */
  INF_SESSION_SYNC_IN_PROGRESS,
  /* The synchronization is in progress and cannot be cancelled anymore. */
  /*INF_SESSION_SYNC_END_ENQUEUED*/
  /* The synchronization is finished, we are only waiting for the
   * acknowledgment from the remote site. */
  INF_SESSION_SYNC_AWAITING_ACK
} InfSessionSyncStatus;

typedef enum _InfSessionSyncError {
  /* Got unexpected XML node during synchronization */
  INF_SESSION_SYNC_ERROR_UNEXPECTED_NODE,
  /* id attribute not present in XML node */
  INF_SESSION_SYNC_ERROR_ID_NOT_PRESENT,
  /* The ID is already in use by another user */
  INF_SESSION_SYNC_ERROR_ID_IN_USE,
  /* name attribute not present in XML node */
  INF_SESSION_SYNC_ERROR_NAME_NOT_PRESENT,
  /* The name is already in use by another user */
  INF_SESSION_SYNC_ERROR_NAME_IN_USE,
  /* The underlaying connection has been closed */
  INF_SESSION_SYNC_ERROR_CONNECTION_CLOSED,
  /* The sender has cancelled the synchronization */
  INF_SESSION_SYNC_ERROR_SENDER_CANCELLED,
  /* The receiver has cancelled the synchronization */
  INF_SESSION_SYNC_ERROR_RECEIVER_CANCELLED,
  /* Got begin-of-sync message, but sync is already in progress */
  INF_SESSION_SYNC_ERROR_UNEXPECTED_BEGIN_OF_SYNC,
  /* The begin-of-sync message does not contain the number of messages
   * to expect */
  INF_SESSION_SYNC_ERROR_NUM_MESSAGES_MISSING,
  /* Got end-of-sync, but sync is still in progress */
  INF_SESSION_SYNC_ERROR_UNEXPECTED_END_OF_SYNC,
  /* Sync has just started, but first message was not begin-of-sync */
  INF_SESSION_SYNC_ERROR_EXPECTED_BEGIN_OF_SYNC,
  /* Last sync message shoud be end-of-sync, but it is not */
  INF_SESSION_SYNC_ERROR_EXPECTED_END_OF_SYNC,

  INF_SESSION_SYNC_ERROR_FAILED
} InfSessionSyncError;

struct _InfSessionClass {
  GObjectClass parent_class;

  /* Virtual table */

  /* This should save the session within a XML document. parent is the root
   * node of the document. It should create as much nodes as possible within
   * that root node and not in sub-nodes because these are sent to the client
   * and it is allowed that other traffic is put inbetween those notes. This
   * way, communication through the same connection does not hang just because
   * a large document is synchronized. */
  void(*to_xml_sync)(InfSession* session,
                     xmlNodePtr parent);

  /* This method is called for every node in the XML document created above
   * on the other site. It should reconstruct the session. */
  gboolean(*process_xml_sync)(InfSession* session,
                              InfXmlConnection* connection,
                              const xmlNodePtr xml,
                              GError** error);

  /* This method is called for every received message while the session is
   * running. */
  void(*process_xml_run)(InfSession* session,
                         InfXmlConnection* connection,
                         const xmlNodePtr xml);

  GArray*(*get_xml_user_props)(InfSession* session,
                               InfXmlConnection* conn, /* ? */
                               const xmlNodePtr xml);

  void (*set_xml_user_props)(InfSession* session,
                             const GParameter* params,
                             guint n_params,
                             xmlNodePtr xml);

  /* TODO: Add a parameter what kind of xml user props should be fetched.
   * From a UserJoin request, from synchronization, or from something else. */
  gboolean(*validate_user_props)(InfSession* session,
                                 const GParameter* params,
                                 guint n_params,
                                 InfUser* exclude,
                                 GError** error);

  InfUser*(*user_new)(InfSession* session,
                      const GParameter* params,
                      guint n_params);

  void(*close)(InfSession* session);

  /* Signals */
  void(*add_user)(InfSession* session,
                  InfUser* user);

  void(*remove_user)(InfSession* session,
                     InfUser* user);

  void(*synchronization_progress)(InfSession* session,
                                  InfXmlConnection* connection,
                                  gdouble percentage);

  void(*synchronization_complete)(InfSession* session,
                                  InfXmlConnection* connection);

  void(*synchronization_failed)(InfSession* session,
                                InfXmlConnection* connection,
                                const GError* error);
};

struct _InfSession {
  GObject parent;
};

typedef void(*InfSessionForeachUserFunc)(InfUser* user,
                                         gpointer user_data);

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

InfConnectionManager*
inf_session_get_connection_manager(InfSession* session);

InfBuffer*
inf_session_get_buffer(InfSession* session);

InfUser*
inf_session_add_user(InfSession* session,
                     const GParameter* params,
                     guint n_params,
                     GError** error);

InfUser*
inf_session_lookup_user_by_id(InfSession* session,
                              guint user_id);

InfUser*
inf_session_lookup_user_by_name(InfSession* session,
                                const gchar* name);

void
inf_session_foreach_user(InfSession* session,
                         InfSessionForeachUserFunc func,
                         gpointer user_data);

void
inf_session_synchronize_to(InfSession* session,
                           InfXmlConnection* connection,
                           const gchar* identifier);

InfSessionSyncStatus
inf_session_get_synchronization_status(InfSession* session,
                                       InfXmlConnection* connection);

G_END_DECLS

#endif /* __INF_SESSION_H__ */

/* vim:set et sw=2 ts=2: */
