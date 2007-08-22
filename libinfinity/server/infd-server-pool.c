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

#include <libinfinity/server/infd-server-pool.h>

typedef struct _InfdServerPoolPrivate InfdServerPoolPrivate;
struct _InfdServerPoolPrivate {
  InfdDirectory* directory;
  GSList* servers;

  /* TODO: Service publishing */
};

enum {
  PROP_0,

  PROP_DIRECTORY
};

#define INFD_SERVER_POOL_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFD_TYPE_SERVER_POOL, InfdServerPoolPrivate))

static GObjectClass* parent_class;

/* Required by infd_server_pool_notify_status_cb() */
static void
infd_server_pool_remove_server(InfdServerPool* server_pool,
                               InfdXmlServer* server);

static void
infd_server_pool_notify_status_cb(InfdXmlServer* server,
                                  GParamSpec* pspec,
                                  gpointer user_data)
{
  InfdServerPool* server_pool;
  InfdServerPoolPrivate* priv;
  InfdXmlServerStatus status;

  server_pool = INFD_SERVER_POOL(user_data);
  priv = INFD_SERVER_POOL_PRIVATE(server_pool);

  g_object_get(G_OBJECT(server), "status", &status, NULL);

  if(status == INFD_XML_SERVER_OPEN)
  {
    /* TODO: Announce this one in service discoveries */
  }
  else
  {
    /* TODO: Unannounce this one in service discoveries */
  }

  if(status == INFD_XML_SERVER_CLOSED)
  {
    /* Perhaps, another signal handler already removed that server */
    if(g_slist_find(priv->servers, server) != NULL)
      infd_server_pool_remove_server(server_pool, server);
  }
}

static void
infd_server_pool_new_connection_cb(InfdXmlServer* server,
                                   InfXmlConnection* connection,
                                   gpointer user_data)
{
  InfdServerPool* server_pool;
  InfdServerPoolPrivate* priv;

  server_pool = INFD_SERVER_POOL(user_data);
  priv = INFD_SERVER_POOL_PRIVATE(server_pool);

  if(priv->directory != NULL)
    infd_directory_add_connection(priv->directory, connection);
}

static void
infd_server_pool_remove_server(InfdServerPool* server_pool,
                               InfdXmlServer* server)
{
  InfdServerPoolPrivate* priv;
  priv = INFD_SERVER_POOL_PRIVATE(server_pool);

  g_assert(g_slist_find(priv->servers, server) != NULL);
  priv->servers = g_slist_remove(priv->servers, server);

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(server),
    G_CALLBACK(infd_server_pool_notify_status_cb),
    server_pool
  );

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(server),
    G_CALLBACK(infd_server_pool_new_connection_cb),
    server_pool
  );

  g_object_unref(G_OBJECT(server));
}

static void
infd_server_pool_init(GTypeInstance* instance,
                      gpointer g_class)
{
  InfdServerPool* server_pool;
  InfdServerPoolPrivate* priv;

  server_pool = INFD_SERVER_POOL(instance);
  priv = INFD_SERVER_POOL_PRIVATE(server_pool);

  priv->directory = NULL;
  priv->servers = NULL;
}

static void
infd_server_pool_dispose(GObject* object)
{
  InfdServerPool* server_pool;
  InfdServerPoolPrivate* priv;

  server_pool = INFD_SERVER_POOL(object);
  priv = INFD_SERVER_POOL_PRIVATE(server_pool);

  while(priv->servers != NULL)
    infd_server_pool_remove_server(server_pool, priv->servers->data);

  if(priv->directory != NULL)
  {
    g_object_unref(G_OBJECT(priv->directory));
    priv->directory = NULL;
  }

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
infd_server_pool_set_property(GObject* object,
                              guint prop_id,
                              const GValue* value,
                              GParamSpec* pspec)
{
  InfdServerPool* server_pool;
  InfdServerPoolPrivate* priv;

  server_pool = INFD_SERVER_POOL(object);
  priv = INFD_SERVER_POOL_PRIVATE(server_pool);

  switch(prop_id)
  {
  case PROP_DIRECTORY:
    if(priv->directory != NULL) g_object_unref(G_OBJECT(priv->directory));
    priv->directory = INFD_DIRECTORY(g_value_dup_object(value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infd_server_pool_get_property(GObject* object,
                              guint prop_id,
                              GValue* value,
                              GParamSpec* pspec)
{
  InfdServerPool* server_pool;
  InfdServerPoolPrivate* priv;

  server_pool = INFD_SERVER_POOL(object);
  priv = INFD_SERVER_POOL_PRIVATE(server_pool);

  switch(prop_id)
  {
  case PROP_DIRECTORY:
    g_value_set_object(value, G_OBJECT(priv->directory));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infd_server_pool_class_init(gpointer g_class,
                            gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfdServerPoolPrivate));

  object_class->dispose = infd_server_pool_dispose;
  object_class->set_property = infd_server_pool_set_property;
  object_class->get_property = infd_server_pool_get_property;

  g_object_class_install_property(
    object_class,
    PROP_DIRECTORY,
    g_param_spec_object(
      "directory",
      "Directory",
      "The directory to which to register incoming connections to",
      INFD_TYPE_DIRECTORY,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );
}

GType
infd_server_pool_get_type(void)
{
  static GType server_pool_type = 0;

  if(!server_pool_type)
  {
    static const GTypeInfo server_pool_type_info = {
      sizeof(InfdServerPoolClass),   /* class_size */
      NULL,                          /* base_init */
      NULL,                          /* base_finalize */
      infd_server_pool_class_init,   /* class_init */
      NULL,                          /* class_finalize */
      NULL,                          /* class_data */
      sizeof(InfdServerPool),        /* instance_size */
      0,                             /* n_preallocs */
      infd_server_pool_init,         /* instance_init */
      NULL                           /* value_table */
    };

    server_pool_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfdServerPool",
      &server_pool_type_info,
      0
    );
  }

  return server_pool_type;
}

/** infd_server_pool_new:
 *
 * @directory: A #InfdDirectory to which to add incoming connections.
 *
 * Creates a new #InfdServerPool.
 *
 * Return Value: A new #InfdServerPool.
 **/
InfdServerPool*
infd_server_pool_new(InfdDirectory* directory)
{
  GObject* object;

  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), NULL);

  object = g_object_new(
    INFD_TYPE_SERVER_POOL,
    "directory", directory,
    NULL
  );

  return INFD_SERVER_POOL(object);
}

/** infd_server_pool_add_server:
 *
 * @server_pool: A #InfdServerPool.
 * @server: A #InfdXmlServer.
 *
 * Adds @server to @server_pool. The server pool accepts incoming connections
 * and gives them to its directory which processes incoming requests.
 *
 * It is your responsibility to open @server. It will be automatically removed
 * from the pool when the server is closed. However, you might pass a closed
 * server to this function and open it afterwards.
 **/
void
infd_server_pool_add_server(InfdServerPool* server_pool,
                            InfdXmlServer* server)
{
  InfdServerPoolPrivate* priv;
  InfdXmlServerStatus status;

  g_return_if_fail(INFD_IS_SERVER_POOL(server_pool));
  g_return_if_fail(INFD_IS_XML_SERVER(server));

  priv = INFD_SERVER_POOL_PRIVATE(server_pool);
  g_return_if_fail(g_slist_find(priv->servers, server) == NULL);

  g_signal_connect_after(
    G_OBJECT(server),
    "notify::status",
    G_CALLBACK(infd_server_pool_notify_status_cb),
    server_pool
  );

  g_signal_connect_after(
    G_OBJECT(server),
    "new-connection",
    G_CALLBACK(infd_server_pool_new_connection_cb),
    server_pool
  );

  g_object_get(G_OBJECT(server), "status", &status, NULL);
  if(status == INFD_XML_SERVER_OPEN)
  {
    /* TODO: Announce this one in service discoveries */
  }

  g_object_ref(G_OBJECT(server));
}

/* vim:set et sw=2 ts=2: */
