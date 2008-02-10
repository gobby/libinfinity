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

#ifndef __INF_METHOD_MANAGER_H__
#define __INF_METHOD_MANAGER_H__

#include <libinfinity/common/inf-connection-manager.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_METHOD_MANAGER                 (inf_method_manager_get_type())
#define INF_METHOD_MANAGER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_METHOD_MANAGER, InfMethodManager))
#define INF_METHOD_MANAGER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TYPE_METHOD_MANAGER, InfMethodManagerClass))
#define INF_IS_METHOD_MANAGER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_METHOD_MANAGER))
#define INF_IS_METHOD_MANAGER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TYPE_METHOD_MANAGER))
#define INF_METHOD_MANAGER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TYPE_METHOD_MANAGER, InfMethodManagerClass))

#define INF_TYPE_METHOD_MANAGER_STATUS          (inf_method_manager_status_get_type())

typedef struct _InfMethodManager InfMethodManager;
typedef struct _InfMethodManagerClass InfMethodManagerClass;

struct _InfMethodManagerClass {
  GObjectClass parent_class;
};

struct _InfMethodManager {
  GObject parent;
};

GType
inf_method_manager_get_type(void) G_GNUC_CONST;

InfMethodManager*
inf_method_manager_get_default(void);

InfMethodManager*
inf_method_manager_new(const gchar* search_path);

void
inf_method_manager_add_method(InfMethodManager* manager,
                              const InfConnectionManagerMethodDesc* method);

const InfConnectionManagerMethodDesc*
inf_method_manager_lookup_method(InfMethodManager* manager,
                                 const gchar* network,
                                 const gchar* method_name);

GSList*
inf_method_manager_list_methods_with_name(InfMethodManager* manager,
                                          const gchar* name);

GSList*
inf_method_manager_list_methods_with_network(InfMethodManager* manager,
                                             const gchar* network);

GSList*
inf_method_manager_list_all_methods(InfMethodManager* manager);

G_END_DECLS

#endif /* __INF_METHOD_MANAGER_H__ */

/* vim:set et sw=2 ts=2: */
