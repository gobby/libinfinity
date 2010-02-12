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

#ifndef __INFC_REQUEST_MANAGER_H__
#define __INFC_REQUEST_MANAGER_H__

#include <libinfinity/client/infc-request.h>
#include <libxml/tree.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define INFC_TYPE_REQUEST_MANAGER                 (infc_request_manager_get_type())
#define INFC_REQUEST_MANAGER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INFC_TYPE_REQUEST_MANAGER, InfcRequestManager))
#define INFC_REQUEST_MANAGER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INFC_TYPE_REQUEST_MANAGER, InfcRequestManagerClass))
#define INFC_IS_REQUEST_MANAGER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INFC_TYPE_REQUEST_MANAGER))
#define INFC_IS_REQUEST_MANAGER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INFC_TYPE_REQUEST_MANAGER))
#define INFC_REQUEST_MANAGER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INFC_TYPE_REQUEST_MANAGER, InfcRequestManagerClass))

typedef struct _InfcRequestManager InfcRequestManager;
typedef struct _InfcRequestManagerClass InfcRequestManagerClass;

struct _InfcRequestManagerClass {
  GObjectClass parent_class;

  void (*request_add)(InfcRequestManager* manager,
                      InfcRequest* request);
  void (*request_remove)(InfcRequestManager* manager,
                         InfcRequest* request);
};

struct _InfcRequestManager {
  GObject parent;
};

typedef void(*InfcRequestManagerForeachFunc)(InfcRequest*, gpointer);

GType
infc_request_manager_get_type(void) G_GNUC_CONST;

InfcRequestManager*
infc_request_manager_new(guint seq_id);

InfcRequest*
infc_request_manager_add_request(InfcRequestManager* manager,
                                 GType request_type,
                                 const gchar* request_name,
                                 const gchar* first_property_name,
                                 ...);

InfcRequest*
infc_request_manager_add_request_valist(InfcRequestManager* manager,
                                        GType request_type,
                                        const gchar* request_name,
                                        const gchar* first_property_name,
                                        va_list arglist);

void
infc_request_manager_remove_request(InfcRequestManager* manager,
                                    InfcRequest* request);

void
infc_request_manager_fail_request(InfcRequestManager* manager,
                                  InfcRequest* request,
                                  GError* error);

void
infc_request_manager_clear(InfcRequestManager* manager);

InfcRequest*
infc_request_manager_get_request_by_seq(InfcRequestManager* manager,
                                        guint seq);

InfcRequest*
infc_request_manager_get_request_by_xml(InfcRequestManager* manager,
                                        const gchar* name,
                                        xmlNodePtr xml,
                                        GError** error);

InfcRequest*
infc_request_manager_get_request_by_xml_required(InfcRequestManager* manager,
                                                 const gchar* name,
                                                 xmlNodePtr xml,
                                                 GError** error);

void
infc_request_manager_foreach_request(InfcRequestManager* manager,
                                     InfcRequestManagerForeachFunc func,
                                     gpointer user_data);

void
infc_request_manager_foreach_named_request(InfcRequestManager* manager,
                                           const gchar* name,
                                           InfcRequestManagerForeachFunc func,
                                           gpointer user_data);

G_END_DECLS

#endif /* __INFC_REQUEST_MANAGER_H__ */

/* vim:set et sw=2 ts=2: */
