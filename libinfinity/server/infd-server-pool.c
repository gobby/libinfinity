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

typedef enum _InfdServerPoolPublisherType {
  INFD_SERVER_POOL_PUBLISHER_LOCAL
} InfdServerPoolPublisherType;

typedef struct _InfdServerPoolPublisher InfdServerPoolPublisher;
struct _InfdServerPoolPublisher {
  InfdServerPoolPublisherType type;

  union {
    struct {
      InfLocalPublisher* publisher;
      InfLocalPublisherItem* item;
    } local;
  } shared;
};

typedef struct _InfdServerPoolEntry InfdServerPoolEntry;
struct _InfdServerPoolEntry {
  InfdXmlServer* server;
  GSList* publishers;
};

typedef struct _InfdServerPoolPrivate InfdServerPoolPrivate;
struct _InfdServerPoolPrivate {
  InfdDirectory* directory;
  GHashTable* servers; /* server -> entry */
};

enum {
  PROP_0,

  PROP_DIRECTORY
};

#define INFD_SERVER_POOL_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFD_TYPE_SERVER_POOL, InfdServerPoolPrivate))

static GObjectClass* parent_class;

static const gchar*
infd_server_pool_get_local_service_name(void)
{
  /* TODO: It would be nice to have the host name as service name for
   * dedicated server and user name otherwise */
  const gchar* name;
  name = g_get_real_name();

  if(name == NULL || g_ascii_strcasecmp(name, "unknown") == 0)
    name = g_get_user_name();

  return name;
}

static void
infd_server_pool_entry_publish_with(InfdServerPoolEntry* entry,
                                    InfdServerPoolPublisher* publisher)
{
  InfdTcpServer* tcp;
  guint port;

  switch(publisher->type)
  {
  case INFD_SERVER_POOL_PUBLISHER_LOCAL:
    g_assert(INFD_IS_XMPP_SERVER(entry->server));

    if(publisher->shared.local.item == NULL)
    {
      g_object_get(G_OBJECT(entry->server), "tcp-server", &tcp, NULL);
      g_object_get(G_OBJECT(tcp), "local-port", &port, NULL);
      g_object_unref(G_OBJECT(tcp));

      publisher->shared.local.item = inf_local_publisher_publish(
        publisher->shared.local.publisher,
        "_infinote._tcp",
        infd_server_pool_get_local_service_name(),
        port
      );
    }

    break;
  default:
    g_assert_not_reached();
    break;
  }
}

static void
infd_server_pool_entry_unpublish_with(InfdServerPoolEntry* entry,
                                      InfdServerPoolPublisher* publisher)
{
  switch(publisher->type)
  {
  case INFD_SERVER_POOL_PUBLISHER_LOCAL:
    if(publisher->shared.local.item != NULL)
    {
      inf_local_publisher_unpublish(
        publisher->shared.local.publisher,
        publisher->shared.local.item
      );

      publisher->shared.local.item = NULL;
    }

    break;
  default:
    g_assert_not_reached();
    break;
  }
}

static void
infd_server_pool_entry_publish(InfdServerPoolEntry* entry)
{
  GSList* item;
  for(item = entry->publishers; item != NULL; item = g_slist_next(item))
  {
    infd_server_pool_entry_publish_with(
      entry,
      (InfdServerPoolPublisher*)item->data
    );
  }
}

static void
infd_server_pool_entry_unpublish(InfdServerPoolEntry* entry)
{
  GSList* item;
  for(item = entry->publishers; item != NULL; item = g_slist_next(item))
  {
    infd_server_pool_entry_unpublish_with(
      entry,
      (InfdServerPoolPublisher*)item->data
    );
  }
}

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
  InfdServerPoolEntry* entry;
  InfdXmlServerStatus status;

  server_pool = INFD_SERVER_POOL(user_data);
  priv = INFD_SERVER_POOL_PRIVATE(server_pool);

  entry = (InfdServerPoolEntry*)g_hash_table_lookup(priv->servers, server);
  g_assert(entry != NULL);

  g_object_get(G_OBJECT(server), "status", &status, NULL);

  if(status == INFD_XML_SERVER_CLOSED)
  {
    /* This unpublishes if necessary */
    infd_server_pool_remove_server(server_pool, server);
  }
  else
  {
    if(status == INFD_XML_SERVER_OPEN)
      infd_server_pool_entry_publish(entry);
    else
      infd_server_pool_entry_unpublish(entry);
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

  /* If the addition fails, then the directory does not ref the connection and
   * it will be unrefed (and therefore be closed) right after this function
   * terminates. */
  if(priv->directory != NULL)
    infd_directory_add_connection(priv->directory, connection);
}

static void
infd_server_pool_entry_free(InfdServerPool* server_pool,
                            InfdServerPoolEntry* entry)
{
  GSList* item;
  InfdServerPoolPublisher* publisher;

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(entry->server),
    G_CALLBACK(infd_server_pool_notify_status_cb),
    server_pool
  );

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(entry->server),
    G_CALLBACK(infd_server_pool_new_connection_cb),
    server_pool
  );

  for(item = entry->publishers; item != NULL; item = g_slist_next(item))
  {
    publisher = (InfdServerPoolPublisher*)item->data;
    switch(publisher->type)
    {
    case INFD_SERVER_POOL_PUBLISHER_LOCAL:
      infd_server_pool_entry_unpublish_with(entry, publisher);
      g_object_unref(G_OBJECT(publisher->shared.local.publisher));
      break;
    default:
      g_assert_not_reached();
      break;
    }
  }
  
  g_slist_free(entry->publishers);
  g_slice_free(InfdServerPoolEntry, entry);
}

static void
infd_server_pool_remove_server(InfdServerPool* server_pool,
                               InfdXmlServer* server)
{
  InfdServerPoolPrivate* priv;
  InfdServerPoolEntry* entry;

  priv = INFD_SERVER_POOL_PRIVATE(server_pool);
  entry = (InfdServerPoolEntry*)g_hash_table_lookup(priv->servers, server);
  g_assert(entry != NULL);

  infd_server_pool_entry_free(server_pool, entry);
  g_hash_table_remove(priv->servers, server);
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

  /* We don't set a value destroy func but rather make sure we free the
   * InfdServerPoolEntrys ourselves before removing them from the hashtable.
   * This is because the GDestroyNotify doesn't allow a user_data parameter
   * and therefore doesn't know the InfdServerPool from which the entry is
   * removed, but it is required to correctly disconnect the signals from
   * the InfXmlServer. */
  priv->servers = g_hash_table_new(NULL, NULL);
}

static void
infd_server_pool_dispose_foreach_func(gpointer key,
                                      gpointer value,
                                      gpointer user_data)
{
  infd_server_pool_entry_free(
    INFD_SERVER_POOL(user_data),
    (InfdServerPoolEntry*)value
  );
}

static void
infd_server_pool_dispose(GObject* object)
{
  InfdServerPool* server_pool;
  InfdServerPoolPrivate* priv;

  server_pool = INFD_SERVER_POOL(object);
  priv = INFD_SERVER_POOL_PRIVATE(server_pool);

  g_hash_table_foreach(
    priv->servers,
    infd_server_pool_dispose_foreach_func,
    server_pool
  );

  g_hash_table_remove_all(priv->servers);

  if(priv->directory != NULL)
  {
    g_object_unref(G_OBJECT(priv->directory));
    priv->directory = NULL;
  }

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
infd_server_pool_finalize(GObject* object)
{
  InfdServerPool* server_pool;
  InfdServerPoolPrivate* priv;

  server_pool = INFD_SERVER_POOL(object);
  priv = INFD_SERVER_POOL_PRIVATE(server_pool);

  /* Should have been cleared in dispose */
  g_assert(g_hash_table_size(priv->servers) == 0);
  g_hash_table_destroy(priv->servers);

  G_OBJECT_CLASS(parent_class)->finalize(object);
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
    g_assert(priv->directory == NULL); /* construct only */
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
  object_class->finalize = infd_server_pool_finalize;
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

/**
 * infd_server_pool_new:
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

/**
 * infd_server_pool_add_server:
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
  InfdServerPoolEntry* entry;

  g_return_if_fail(INFD_IS_SERVER_POOL(server_pool));
  g_return_if_fail(INFD_IS_XML_SERVER(server));

  priv = INFD_SERVER_POOL_PRIVATE(server_pool);
  g_return_if_fail(g_hash_table_lookup(priv->servers, server) == NULL);

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

  entry = g_slice_new(InfdServerPoolEntry);
  entry->server = server;
  entry->publishers = NULL;
  g_hash_table_insert(priv->servers, server, entry);

  g_object_ref(G_OBJECT(server));
}

/**
 * inf_server_pool_add_local_publisher:
 * @server_pool: A #InfdServerPool.
 * @server: A #InfdXmppServer added to @server_pool.
 * @publisher: A #InfLocalPublisher.
 *
 * Publishes a service offered by @server on the local network via
 * @publisher. This can safely be called when @server is not yet open. The
 * service will be published as soon as the server opens.
 **/
/* TODO: Make a InfdLocalServer interface to query the port? */
void
infd_server_pool_add_local_publisher(InfdServerPool* server_pool,
                                     InfdXmppServer* server,
                                     InfLocalPublisher* publisher)
{
  InfdServerPoolPrivate* priv;
  InfdServerPoolEntry* entry;
  InfdServerPoolPublisher* server_pool_publisher;
  InfdXmlServerStatus status;

  g_return_if_fail(INFD_IS_SERVER_POOL(server_pool));
  g_return_if_fail(INFD_IS_XMPP_SERVER(server));
  g_return_if_fail(INF_IS_LOCAL_PUBLISHER(publisher));

  priv = INFD_SERVER_POOL_PRIVATE(server_pool);
  entry = (InfdServerPoolEntry*)g_hash_table_lookup(priv->servers, server);
  g_return_if_fail(entry != NULL);

  /* TODO: Bail if we are already publishing via this publisher */

  /* TODO: Only announce on the address family server is listening on.
   * Otherwise we might announce the service on ipv6 without anyone
   * listening there. */
  server_pool_publisher = g_slice_new(InfdServerPoolPublisher);
  server_pool_publisher->type = INFD_SERVER_POOL_PUBLISHER_LOCAL;
  server_pool_publisher->shared.local.publisher = publisher;
  server_pool_publisher->shared.local.item = NULL;
  g_object_ref(G_OBJECT(publisher));

  entry->publishers = g_slist_prepend(
    entry->publishers,
    server_pool_publisher
  );

  /* Initial publish when server is open */
  g_object_get(G_OBJECT(server), "status", &status, NULL);
  if(status == INFD_XML_SERVER_OPEN)
    infd_server_pool_entry_publish_with(entry, server_pool_publisher);
}

/* vim:set et sw=2 ts=2: */
