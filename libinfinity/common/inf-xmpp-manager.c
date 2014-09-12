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

#include <libinfinity/common/inf-xmpp-manager.h>
#include <libinfinity/common/inf-xml-connection.h>
#include <libinfinity/common/inf-tcp-connection.h>

#include <libinfinity/inf-signals.h>

#include <string.h>

/**
 * SECTION:inf-xmpp-manager
 * @title: InfXmppManager
 * @short_description: Reuse existing connections
 * @include: libinfinity/common/inf-xmpp-manager.h
 * @stability: Unstable
 *
 * #InfXmppManager stores #InfXmppConnection objects and allows to look them
 * up by the IP address and port number of their underlaying
 * #InfTcpConnection<!-- -->s. This can be used to reuse existing network
 * connections instead of creating new ones.
 *
 * Each object which needs to make connections should be passed a
 * #InfXmppManager. Then, when making a connection to a certain address/port
 * pair, it should first look in the XMPP manager whether there is already
 * an existing connection to the destination host, via
 * inf_xmpp_manager_lookup_connection_by_address(). If there is, it should
 * use it (maybe reopen it if it is closed). Otherwise, it should create a
 * new connection and add it to the XMPP manager via
 * inf_xmpp_manager_add_connection() for others to use.
 *
 * The XMPP manager can also handle connections whose address is still to be
 * looked up. Such connections are looked up by the hostname given to the
 * name resolver. Once the hostname has been looked up, and if another
 * connection with the same addressand port number exists already, the new
 * connection is removed in favor of the already existing one.
 */

typedef enum _InfXmppManagerKeyKind {
  INF_XMPP_MANAGER_KEY_HOSTNAME,
  INF_XMPP_MANAGER_KEY_ADDRESS
} InfXmppManagerKeyKind;

typedef struct _InfXmppManagerKey InfXmppManagerKey;
struct _InfXmppManagerKey {
  InfXmppManagerKeyKind kind;

  union {
    struct {
      InfIpAddress* address;
      guint port;
    } address;

    struct {
      gchar* hostname;
      gchar* service;
      gchar* srv;
    } hostname;
  } shared;
};

typedef struct _InfXmppManagerConnectionInfo InfXmppManagerConnectionInfo;
struct _InfXmppManagerConnectionInfo {
  InfXmppManager* manager;
  InfXmppConnection* xmpp;
  InfNameResolver* resolver; /* TCPs resolver */

  InfXmppManagerKey** keys;
  guint n_keys;
};

typedef struct _InfXmppManagerKeyChangedForeachFuncData
  InfXmppManagerKeyChangedForeachFuncData;
struct _InfXmppManagerKeyChangedForeachFuncData {
  InfTcpConnection* connection;
  InfXmppManagerConnectionInfo* info;
  const InfXmppManagerKey* key;
};

typedef struct _InfXmppManagerPrivate InfXmppManagerPrivate;
struct _InfXmppManagerPrivate {
  GTree* connections;
};

enum {
  CONNECTION_ADDED,
  CONNECTION_REMOVED,

  LAST_SIGNAL
};

#define INF_XMPP_MANAGER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TYPE_XMPP_MANAGER, InfXmppManagerPrivate))

static guint xmpp_manager_signals[LAST_SIGNAL];

G_DEFINE_TYPE_WITH_CODE(InfXmppManager, inf_xmpp_manager, G_TYPE_OBJECT,
  G_ADD_PRIVATE(InfXmppManager))

/* Make a deep copy of a key. This is typically not required for lookup, but
 * it is required for storage in the tree. */
static InfXmppManagerKey*
inf_xmpp_manager_key_copy(const InfXmppManagerKey* orig_key)
{
  InfXmppManagerKey* key;
  key = g_slice_new(InfXmppManagerKey);

  key->kind = orig_key->kind;

  switch(key->kind)
  {
  case INF_XMPP_MANAGER_KEY_ADDRESS:
    key->shared.address.address =
      inf_ip_address_copy(orig_key->shared.address.address);
    key->shared.address.port = orig_key->shared.address.port;
    break;
  case INF_XMPP_MANAGER_KEY_HOSTNAME:
    key->shared.hostname.hostname =
      g_strdup(orig_key->shared.hostname.hostname);
    key->shared.hostname.service =
      g_strdup(orig_key->shared.hostname.service);
    key->shared.hostname.srv =
      g_strdup(orig_key->shared.hostname.srv);
    break;
  default:
    g_assert_not_reached();
    break;
  }

  return key;
}

static void
inf_xmpp_manager_key_free(gpointer key_ptr)
{
  InfXmppManagerKey* key;
  key = (InfXmppManagerKey*)key_ptr;

  switch(key->kind)
  {
  case INF_XMPP_MANAGER_KEY_ADDRESS:
    inf_ip_address_free(key->shared.address.address);
    break;
  case INF_XMPP_MANAGER_KEY_HOSTNAME:
    g_free(key->shared.hostname.hostname);
    g_free(key->shared.hostname.service);
    g_free(key->shared.hostname.srv);
    break;
  default:
    g_assert_not_reached();
    break;
  }

  g_slice_free(InfXmppManagerKey, key);
}

static int
inf_xmpp_manager_strcmp_nul(const gchar* str1,
                            const gchar* str2)
{
  if(str1 == NULL && str2 == NULL)
    return 0;
  else if(str1 == NULL && str2 != NULL)
    return +1;
  else if(str1 != NULL && str2 == NULL)
    return -1;
  else
    return strcmp(str1, str2);
}

static int
inf_xmpp_manager_key_cmp(gconstpointer first,
                         gconstpointer second,
                         G_GNUC_UNUSED gpointer data)
{
  const InfXmppManagerKey* first_key;
  const InfXmppManagerKey* second_key;
  int cmp;

  first_key = (const InfXmppManagerKey*)first;
  second_key = (const InfXmppManagerKey*)second;

  if(first_key->kind < second_key->kind)
    return -1;
  else if(first_key->kind > second_key->kind)
    return 1;

  switch(first_key->kind)
  {
  case INF_XMPP_MANAGER_KEY_ADDRESS:
    if(first_key->shared.address.port < second_key->shared.address.port)
    {
      return -1;
    }
    else if(first_key->shared.address.port > second_key->shared.address.port)
    {
      return 1;
    }
    else
    {
      return inf_ip_address_collate(
        first_key->shared.address.address,
        second_key->shared.address.address
      );
    }
  case INF_XMPP_MANAGER_KEY_HOSTNAME:
    cmp = strcmp(
      first_key->shared.hostname.hostname,
      second_key->shared.hostname.hostname
    );
    if(cmp != 0) return cmp;

    cmp = inf_xmpp_manager_strcmp_nul(
      first_key->shared.hostname.service,
      second_key->shared.hostname.service
    );
    if(cmp != 0) return cmp;

    cmp = inf_xmpp_manager_strcmp_nul(
      first_key->shared.hostname.srv,
      second_key->shared.hostname.srv
    );
    return cmp;
  default:
    g_assert_not_reached();
    return 0;
  }
}

InfXmppManagerConnectionInfo*
inf_xmpp_manager_lookup_connection(InfXmppManager* manager,
                                   InfXmppConnection* connection)
{
  InfXmppManagerPrivate* priv;
  InfXmppManagerKey key;
  InfTcpConnection* tcp;
  InfNameResolver* resolver;
  InfXmppManagerConnectionInfo* info;

  priv = INF_XMPP_MANAGER_PRIVATE(manager);

  /* We can choose any key of connection, if things are consistent,
   * it should work. */
  g_object_get(G_OBJECT(connection), "tcp-connection", &tcp, NULL);

  key.shared.address.address = inf_tcp_connection_get_remote_address(tcp);
  key.shared.address.port = inf_tcp_connection_get_remote_port(tcp);

  if(key.shared.address.address == NULL || key.shared.address.port == 0)
  {
    g_object_get(G_OBJECT(tcp), "resolver", &resolver, NULL);
    if(resolver == NULL) return NULL; /* no keys available */

    key.kind = INF_XMPP_MANAGER_KEY_HOSTNAME;
    key.shared.hostname.hostname =
      (gchar*)inf_name_resolver_get_hostname(resolver);
    key.shared.hostname.service =
      (gchar*)inf_name_resolver_get_service(resolver);
    key.shared.hostname.srv =
      (gchar*)inf_name_resolver_get_srv(resolver);

    g_object_unref(resolver);
  }
  else
  {
    key.kind = INF_XMPP_MANAGER_KEY_ADDRESS;
  }

  g_object_unref(tcp);

  info = g_tree_lookup(priv->connections, &key);
  return info;
}

static InfXmppManagerConnectionInfo*
inf_xmpp_manager_check_key(InfXmppManager* manager,
                           InfXmppManagerConnectionInfo* info,
                           const InfXmppManagerKey* key,
                           gboolean* has_keys)
{
  InfXmppManagerPrivate* priv;
  InfXmppManagerKey* permanent_key;
  InfXmppManagerConnectionInfo* lookup;
  guint i;

  priv = INF_XMPP_MANAGER_PRIVATE(manager);
  lookup = g_tree_lookup(priv->connections, key);

  /* Check for duplicates */
  if(lookup != NULL && lookup != info)
    return lookup;

  if(lookup == NULL)
  {
    /* This key is not yet associated to the connection info, but it is
     * a valid key for the connection, so we add it. */
    permanent_key = inf_xmpp_manager_key_copy(key);
    g_tree_insert(priv->connections, permanent_key, info);

    info->keys = g_realloc(
      info->keys,
      sizeof(InfXmppManagerKey*) * (info->n_keys + 1)
    );

    info->keys[info->n_keys] = permanent_key;
    ++info->n_keys;
  }
  else
  {
    /* Check which of the saved keys, if any, this one corresponds to, so
     * that we can detect keys that are no longer active. */
    for(i = 0; i < info->n_keys; ++i)
    {
      if(inf_xmpp_manager_key_cmp(info->keys[i], key, NULL) == 0)
      {
        has_keys[i] = TRUE;
        break;
      }
    }
  }

  return NULL;
}

static void
inf_xmpp_manager_connection_info_free(InfXmppManagerConnectionInfo* info);

/* Updates all keys for the given connection info. is_added should be TRUE if
 * before the caller the connection was already added to the manager, i.e.
 * whether the connection-added signal has been emitted for the connection.
 * Returns FALSE if the connection is removed from the manager due to no keys
 * or a conflict. */
static gboolean
inf_xmpp_manager_update_keys(InfXmppManager* manager,
                             InfXmppManagerConnectionInfo* info,
                             gboolean is_added)
{
  InfXmppManagerPrivate* priv;

  guint n_keys;
  gboolean* has_keys;
  guint i;

  InfTcpConnection* tcp;
  InfNameResolver* resolver;
  InfXmppManagerKey key;
  InfXmppManagerConnectionInfo* duplicate_info;
  InfXmppConnection* xmpp;
  gboolean result;

  priv = INF_XMPP_MANAGER_PRIVATE(manager);

  n_keys = info->n_keys;
  if(n_keys > 0)
    has_keys = g_malloc(n_keys * sizeof(gboolean));
  else
    has_keys = NULL;

  for(i = 0; i < n_keys; ++i)
    has_keys[i] = FALSE;
  duplicate_info = NULL;

  g_object_get(G_OBJECT(info->xmpp), "tcp-connection", &tcp, NULL);

  /* Gather all keys */
  key.kind = INF_XMPP_MANAGER_KEY_ADDRESS;
  key.shared.address.address = inf_tcp_connection_get_remote_address(tcp);
  key.shared.address.port = inf_tcp_connection_get_remote_port(tcp);

  if(key.shared.address.address != NULL && key.shared.address.port != 0)
  {
    duplicate_info =
      inf_xmpp_manager_check_key(manager, info, &key, has_keys);
  }

  if(duplicate_info == NULL)
  {
    g_object_get(G_OBJECT(tcp), "resolver", &resolver, NULL);
    if(resolver != NULL)
    {
      key.kind = INF_XMPP_MANAGER_KEY_HOSTNAME;
      key.shared.hostname.hostname =
        (gchar*)inf_name_resolver_get_hostname(resolver);
      key.shared.hostname.service =
        (gchar*)inf_name_resolver_get_service(resolver);
      key.shared.hostname.srv =
        (gchar*)inf_name_resolver_get_srv(resolver);
      duplicate_info =
        inf_xmpp_manager_check_key(manager, info, &key, has_keys);

      /* TODO: We should also be able to access the resolved entries if we
       * are looking up backup addresses. */
      if(duplicate_info == NULL &&
         inf_name_resolver_finished(resolver) == TRUE)
      {
        for(i = 0; i < inf_name_resolver_get_n_addresses(resolver); ++i)
        {
          key.kind = INF_XMPP_MANAGER_KEY_ADDRESS;
          key.shared.address.address =
            (InfIpAddress*)inf_name_resolver_get_address(resolver, i);
          key.shared.address.port =
            inf_name_resolver_get_port(resolver, i);
          duplicate_info =
            inf_xmpp_manager_check_key(manager, info, &key, has_keys);

          if(duplicate_info != NULL)
            break;
        }
      }

      g_object_unref(resolver);
    }
  }

  g_object_unref(tcp);

  result = TRUE;
  if(duplicate_info == NULL)
  {
    for(i = 0; i < n_keys; )
    {
      if(has_keys[i] == FALSE)
      {
        /* This key is not valid anymore, so remove it */
        g_tree_remove(priv->connections, info->keys[i]);

        g_assert(n_keys > 0);
        g_assert(info->n_keys > 0);
        g_assert(info->n_keys >= n_keys);

        info->keys[i] = info->keys[n_keys - 1];
        has_keys[i] = has_keys[n_keys - 1];

        /* In case there are new keys added by the code above */
        info->keys[n_keys - 1] = info->keys[info->n_keys - 1];
        --n_keys;

        if(info->n_keys > 1)
        {
          info->keys = g_realloc(
            info->keys,
            sizeof(InfXmppManagerKey*) * (info->n_keys - 1)
          );

          --info->n_keys;
        }
        else
        {
          g_free(info->keys);
          info->keys = NULL;
          info->n_keys = 0;
        }
      }
      else
      {
        ++i;
      }
    }

    if(info->n_keys == 0)
    {
      xmpp = info->xmpp;
      g_object_ref(xmpp);

      g_warning("Connection has no keys anymore!");

      inf_xmpp_manager_connection_info_free(info);

      /* This should typically not happen. It only happens when someone resets
       * both the resolver and the remote-address and remote-port properties
       * of a connection that stays within the XMPP manager. */
      if(is_added == TRUE)
      {
        g_signal_emit(
          G_OBJECT(manager),
          xmpp_manager_signals[CONNECTION_REMOVED],
          0,
          xmpp,
          NULL
        );
      }

      g_object_unref(xmpp);
      result = FALSE;
    }
  }
  else
  {
    for(i = 0; i < info->n_keys; ++i)
      g_tree_remove(priv->connections, info->keys[i]);

    xmpp = info->xmpp;
    g_object_ref(xmpp);
    inf_xmpp_manager_connection_info_free(info);

    if(is_added == TRUE)
    {
      g_signal_emit(
        G_OBJECT(manager),
        xmpp_manager_signals[CONNECTION_REMOVED],
        0,
        xmpp,
        duplicate_info->xmpp
      );
    }

    g_object_unref(xmpp);
    result = FALSE;
  }

  g_free(has_keys);
  return result;
}

static void
inf_xmpp_manager_notify_cb(GObject* object,
                           GParamSpec* pspec,
                           gpointer user_data)
{
  InfXmppManagerConnectionInfo* info;
  info = (InfXmppManagerConnectionInfo*)user_data;

  inf_xmpp_manager_update_keys(info->manager, info, TRUE);
}

static void
inf_xmpp_manager_resolved_cb(InfNameResolver* resolver,
                             const GError* error,
                             gpointer user_data)
{
  InfXmppManagerConnectionInfo* info;
  info = (InfXmppManagerConnectionInfo*)user_data;

  inf_xmpp_manager_update_keys(info->manager, info, TRUE);
}

static void
inf_xmpp_manager_connection_info_set_resolver(InfXmppManagerConnectionInfo* info,
                                              InfNameResolver* resolver)
{
  if(info->resolver != NULL)
  {
    inf_signal_handlers_disconnect_by_func(
      info->resolver,
      G_CALLBACK(inf_xmpp_manager_notify_cb),
      info
    );

    inf_signal_handlers_disconnect_by_func(
      info->resolver,
      G_CALLBACK(inf_xmpp_manager_resolved_cb),
      info
    );

    g_object_unref(info->resolver);
  }

  info->resolver = resolver;

  if(resolver != NULL)
  {
    g_object_ref(resolver);

    g_signal_connect(
      G_OBJECT(resolver),
      "notify::hostname",
      G_CALLBACK(inf_xmpp_manager_notify_cb),
      info
    );

    g_signal_connect(
      G_OBJECT(resolver),
      "notify::service",
      G_CALLBACK(inf_xmpp_manager_notify_cb),
      info
    );

    g_signal_connect(
      G_OBJECT(resolver),
      "notify::srv",
      G_CALLBACK(inf_xmpp_manager_notify_cb),
      info
    );

    g_signal_connect(
      G_OBJECT(resolver),
      "resolved",
      G_CALLBACK(inf_xmpp_manager_resolved_cb),
      info
    );
  }
}

static void
inf_xmpp_manager_notify_resolver_cb(GObject* object,
                                    GParamSpec* pspec,
                                    gpointer user_data)
{
  InfXmppManagerConnectionInfo* info;
  InfNameResolver* resolver;

  g_object_get(object, "resolver", &resolver, NULL);
  info = (InfXmppManagerConnectionInfo*)user_data;

  inf_xmpp_manager_connection_info_set_resolver(info, resolver);
  g_object_unref(resolver);

  inf_xmpp_manager_update_keys(info->manager, info, TRUE);
}

static InfXmppManagerConnectionInfo*
inf_xmpp_manager_connection_info_new(InfXmppManager* manager,
                                     InfXmppConnection* xmpp)
{
  InfXmppManagerConnectionInfo* info;
  InfTcpConnection* tcp;
  InfNameResolver* resolver;

  g_object_get(G_OBJECT(xmpp), "tcp-connection", &tcp, NULL);
  g_assert(tcp != NULL);

  info = g_slice_new(InfXmppManagerConnectionInfo);
  info->manager = manager;
  info->xmpp = xmpp;
  info->resolver = NULL;
  info->keys = NULL;
  info->n_keys = 0;
  g_object_ref(xmpp);

  g_signal_connect(
    G_OBJECT(tcp),
    "notify::remote-address",
    G_CALLBACK(inf_xmpp_manager_notify_cb),
    info
  );

  g_signal_connect(
    G_OBJECT(tcp),
    "notify::remote-port",
    G_CALLBACK(inf_xmpp_manager_notify_cb),
    info
  );

  g_signal_connect(
    G_OBJECT(tcp),
    "notify::resolver",
    G_CALLBACK(inf_xmpp_manager_notify_resolver_cb),
    info
  );

  g_object_get(G_OBJECT(tcp), "resolver", &resolver, NULL);

  if(resolver != NULL)
  {
    inf_xmpp_manager_connection_info_set_resolver(info, resolver);
    g_object_unref(resolver);
  }

  g_object_unref(tcp);
  return info;
}

static void
inf_xmpp_manager_connection_info_free(InfXmppManagerConnectionInfo* info)
{
  InfTcpConnection* tcp;

  g_object_get(G_OBJECT(info->xmpp), "tcp-connection", &tcp, NULL);
  g_assert(tcp != NULL);

  inf_xmpp_manager_connection_info_set_resolver(info, NULL);

  inf_signal_handlers_disconnect_by_func(
    tcp,
    G_CALLBACK(inf_xmpp_manager_notify_cb),
    info
  );

  inf_signal_handlers_disconnect_by_func(
    tcp,
    G_CALLBACK(inf_xmpp_manager_notify_resolver_cb),
    info
  );

  g_object_unref(tcp);
  g_object_unref(info->xmpp);
  g_free(info->keys);
  g_slice_free(InfXmppManagerConnectionInfo, info);
}

static gboolean
inf_xmpp_manager_dispose_destroy_func(gpointer key,
                                      gpointer value,
                                      gpointer data)
{
  InfXmppManagerConnectionInfo* info;
  info = (InfXmppManagerConnectionInfo*)value;

  if(--info->n_keys == 0)
    inf_xmpp_manager_connection_info_free(value);

  return FALSE;
}

static void
inf_xmpp_manager_init(InfXmppManager* manager)
{
  InfXmppManagerPrivate* priv;
  priv = INF_XMPP_MANAGER_PRIVATE(manager);

  priv->connections = g_tree_new_full(
    inf_xmpp_manager_key_cmp,
    NULL,
    inf_xmpp_manager_key_free,
    NULL
  );
}

static void
inf_xmpp_manager_dispose(GObject* object)
{
  InfXmppManager* manager;
  InfXmppManagerPrivate* priv;

  manager = INF_XMPP_MANAGER(object);
  priv = INF_XMPP_MANAGER_PRIVATE(object);

  g_tree_foreach(
    priv->connections,
    inf_xmpp_manager_dispose_destroy_func,
    manager
  );

  g_tree_destroy(priv->connections);
  priv->connections = NULL;

  G_OBJECT_CLASS(inf_xmpp_manager_parent_class)->dispose(object);
}

static void
inf_xmpp_manager_class_init(InfXmppManagerClass* xmpp_manager_class)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(xmpp_manager_class);

  object_class->dispose = inf_xmpp_manager_dispose;
  xmpp_manager_class->connection_added = NULL;
  xmpp_manager_class->connection_removed = NULL;

  /**
   * InfXmppManager::connection-added:
   * @xmpp_manager: The #InfXmppManager emitting the signal.
   * @connection: The #InfXmppConnection that was added to @xmpp_manager.
   *
   * This signal is emitted whenever a new connection has been added to the
   * #InfXmppManager, via inf_xmpp_manager_add_connection().
   */
  xmpp_manager_signals[CONNECTION_ADDED] = g_signal_new(
    "connection-added",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfXmppManagerClass, connection_added),
    NULL, NULL,
    g_cclosure_marshal_VOID__OBJECT,
    G_TYPE_NONE,
    1,
    INF_TYPE_XMPP_CONNECTION
  );

  /**
   * InfXmppManager::remove-connection:
   * @xmpp_manager: The #InfXmppManager emitting the signal.
   * @connection: The #InfXmppConnection that was removed from @xmpp_manager.
   * @replaced_by: A #InfXmppConnection to the same host, if there was a
   * conflict, or %NULL otherwise.
   *
   * This signal is emitted whenever a connection has been removed from the
   * #InfXmppManager, via inf_xmpp_manager_remove_connection(), or when a
   * collision occurs such that two connections point to the same host, or
   * when a connection loses all its address and hostname information.
   */
  xmpp_manager_signals[CONNECTION_REMOVED] = g_signal_new(
    "connection-removed",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfXmppManagerClass, connection_removed),
    NULL, NULL,
    NULL,
    G_TYPE_NONE,
    2,
    INF_TYPE_XMPP_CONNECTION,
    INF_TYPE_XMPP_CONNECTION
  );
}

/**
 * inf_xmpp_manager_new: (constructor)
 *
 * Creates a new xmpp manager.
 *
 * Returns: (transfer full): A new #InfXmppManager.
 **/
InfXmppManager*
inf_xmpp_manager_new(void)
{
  GObject* object;

  object = g_object_new(INF_TYPE_XMPP_MANAGER, NULL);

  return INF_XMPP_MANAGER(object);
}

/**
 * inf_xmpp_manager_lookup_connection_by_address:
 * @manager: A #InfXmppManager.
 * @address: The remote #InfIpAddress of the connection to look for.
 * @port: The remote port number of the connection to look for.
 *
 * Looks for a #InfXmppConnection contained in @manager whose underlaying
 * #InfTcpConnection has the given address and port set. Returns %NULL if
 * there is no such connection.
 *
 * This function may also return a closed connection. You can then attempt to
 * reopen it, or remove it from the manager using
 * inf_xmpp_manager_remove_connection() when that fails.
 *
 * Returns: (transfer none) (allow-none): A #InfXmppConnection with the given
 * address and port, or %NULL if not found.
 **/
InfXmppConnection*
inf_xmpp_manager_lookup_connection_by_address(InfXmppManager* manager,
                                              const InfIpAddress* address,
                                              guint port)
{
  InfXmppManagerPrivate* priv;
  InfXmppManagerKey key;
  InfXmppManagerConnectionInfo* info;

  g_return_val_if_fail(INF_IS_XMPP_MANAGER(manager), NULL);
  g_return_val_if_fail(address != NULL, NULL);

  priv = INF_XMPP_MANAGER_PRIVATE(manager);
  key.kind = INF_XMPP_MANAGER_KEY_ADDRESS;
  key.shared.address.address = (InfIpAddress*)address;
  key.shared.address.port = port;

  info = g_tree_lookup(priv->connections, &key);
  if(info == NULL) return NULL;

  return info->xmpp;
}

/**
 * inf_xmpp_manager_lookup_connection_by_hostname:
 * @manager: A #InfXmppManager.
 * @hostname: The remote hostname to look for.
 * @service: (allow-none): The service string of the connection to look for.
 * @srv: (allow-none): The SRV record corresponding to the connection.
 *
 * Looks for a #InfXmppConnection contained in @manager whose underlaying
 * #InfTcpConnection has a #InfNameResolver with the given properties.
 *
 * This function may also return a closed connection. You can then attempt to
 * reopen it, or remove it from the manager using
 * inf_xmpp_manager_remove_connection() when that fails.
 *
 * Returns: (transfer none) (allow-none): A #InfXmppConnection with the given
 * hostname, service and srv, or %NULL if not found.
 **/
InfXmppConnection*
inf_xmpp_manager_lookup_connection_by_hostname(InfXmppManager* manager,
                                               const gchar* hostname,
                                               const gchar* service,
                                               const gchar* srv)
{
  /* TODO: Allow not to provide service, srv or both, and then return a
   * connection to the given hostname for any service or srv.
   * This could be done with g_tree_search, and the current tree
   * sort order. */
  InfXmppManagerPrivate* priv;
  InfXmppManagerKey key;
  InfXmppManagerConnectionInfo* info;

  g_return_val_if_fail(INF_IS_XMPP_MANAGER(manager), NULL);
  g_return_val_if_fail(hostname != NULL, NULL);

  priv = INF_XMPP_MANAGER_PRIVATE(manager);
  key.kind = INF_XMPP_MANAGER_KEY_HOSTNAME;
  key.shared.hostname.hostname = (gchar*)hostname;
  key.shared.hostname.service = (gchar*)service;
  key.shared.hostname.srv = (gchar*)srv;

  info = g_tree_lookup(priv->connections, &key);
  if(info == NULL) return NULL;

  return info->xmpp;
}

/**
 * inf_xmpp_manager_contains_connection:
 * @manager: A #InfXmppManager.
 * @connection: A #InfXmppConnection.
 *
 * Returns whether @connection is contained in @manager.
 *
 * Returns: %TRUE if @connection is contained in @manager, %FALSE
 * otherwise.
 */
gboolean
inf_xmpp_manager_contains_connection(InfXmppManager* manager,
                                     InfXmppConnection* connection)
{
  InfXmppManagerConnectionInfo* info;

  g_return_val_if_fail(INF_IS_XMPP_MANAGER(manager), FALSE);
  g_return_val_if_fail(INF_IS_XMPP_CONNECTION(connection), FALSE);

  info = inf_xmpp_manager_lookup_connection(manager, connection);
  if(info == NULL) return FALSE;

  return TRUE;
}

/**
 * inf_xmpp_manager_add_connection:
 * @manager: A #InfXmppManager.
 * @connection: A #InfXmppConnection not yet contained in @manager.
 *
 * Adds the given connection to @manager so that it is found by
 * inf_xmpp_manager_lookup_connection_by_address(),
 * inf_xmpp_manager_lookup_connection_by_hostname() and
 * inf_xmpp_manager_contains_connection().
 */
void
inf_xmpp_manager_add_connection(InfXmppManager* manager,
                                InfXmppConnection* connection)
{
  InfXmppManagerConnectionInfo* info;
  gboolean was_added;

  g_return_if_fail(INF_IS_XMPP_MANAGER(manager));
  g_return_if_fail(INF_IS_XMPP_CONNECTION(connection));

  info = inf_xmpp_manager_connection_info_new(manager, connection);
  was_added = inf_xmpp_manager_update_keys(manager, info, FALSE);

  if(was_added == FALSE)
    inf_xmpp_manager_connection_info_free(info);
  g_return_if_fail(was_added == TRUE);

  g_signal_emit(
    G_OBJECT(manager),
    xmpp_manager_signals[CONNECTION_ADDED],
    0,
    connection
  );
}
/**
 * inf_xmpp_manager_remove_connection:
 * @manager: A #InfXmppManager.
 * @connection: A #InfXmppConnection contained in @manager.
 *
 * Removes the given connection from @manager.
 */
void
inf_xmpp_manager_remove_connection(InfXmppManager* manager,
                                   InfXmppConnection* connection)
{
  InfXmppManagerPrivate* priv;
  InfXmppManagerConnectionInfo* info;
  guint i;

  g_return_if_fail(INF_IS_XMPP_MANAGER(manager));
  g_return_if_fail(INF_IS_XMPP_CONNECTION(connection));

  priv = INF_XMPP_MANAGER_PRIVATE(manager);
  info = inf_xmpp_manager_lookup_connection(manager, connection);
  g_return_if_fail(info != NULL);

  /* Remove all keys */
  for(i = 0; i < info->n_keys; ++i)
    g_tree_remove(priv->connections, info->keys[i]);

  g_object_ref(connection);
  inf_xmpp_manager_connection_info_free(info);

  g_signal_emit(
    G_OBJECT(manager),
    xmpp_manager_signals[CONNECTION_REMOVED],
    0,
    connection,
    NULL
  );

  g_object_unref(connection);
}

/* vim:set et sw=2 ts=2: */
