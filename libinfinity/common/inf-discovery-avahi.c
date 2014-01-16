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
 * SECTION:inf-discovery-avahi
 * @short_description: Service Discovery via Avahi
 * @include: libinfinity/common/inf-discovery-avahi.h
 * @see_also: #InfDiscovery, #InfLocalPublisher
 *
 * #InfDiscoveryAvahi implements the #InfDiscovery and #InfLocalPublisher
 * interfaces on top of avahi. It resolves services to client-side
 * #InfXmppConnection<!-- -->s.
 *
 * This class is only available if the macro
 * <literal>LIBINFINITY_HAVE_AVAHI</literal> is defined.
 */

#include <libinfinity/common/inf-discovery-avahi.h>
#include <libinfinity/common/inf-discovery.h>
#include <libinfinity/common/inf-local-publisher.h>
#include <libinfinity/inf-i18n.h>
#include <libinfinity/inf-config.h> /* LIBINFINITY_HAVE_AVAHI */

#ifdef LIBINFINITY_HAVE_AVAHI

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-client/publish.h>
#include <avahi-common/watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/timeval.h>
#include <avahi-common/alternative.h>
#include <avahi-common/error.h>

#include <sys/socket.h> /* Required by FreeBSD, see #430 */
#include <net/if.h> /* For if_indextoname */
#include <string.h>

struct AvahiWatch {
  InfDiscoveryAvahi* avahi;
  InfIoWatch* watch;

  InfNativeSocket socket;
  AvahiWatchEvent occured_events;

  AvahiWatchCallback callback;
  void* userdata;
};

struct AvahiTimeout {
  InfDiscoveryAvahi* avahi;
  InfIoTimeout* timeout;
  AvahiTimeoutCallback callback;
  void* userdata;
};

typedef struct _InfDiscoveryAvahiInfoResolv InfDiscoveryAvahiInfoResolv;
struct _InfDiscoveryAvahiInfoResolv {
  InfDiscoveryResolvCompleteFunc complete_func;
  InfDiscoveryResolvErrorFunc error_func;
  gpointer user_data;
};

struct _InfDiscoveryInfo {
  gchar* service_name;
  /* pointing to InfDiscoveryAvahiDiscoverInfo.type: */
  const gchar* service_type;
  gchar* domain;

  AvahiIfIndex interface;
  AvahiProtocol protocol;

  AvahiServiceResolver* service_resolver;
  InfXmppConnection* resolved;

  GSList* resolv;
};

struct _InfLocalPublisherItem {
  gchar* type;
  char* name;
  guint port;

  AvahiEntryGroup* entry_group;
};

typedef struct _InfDiscoveryAvahiDiscoverInfo InfDiscoveryAvahiDiscoverInfo;
struct _InfDiscoveryAvahiDiscoverInfo {
  gchar* type; /* also used as key in hash table */
  AvahiServiceBrowser* service_browser;
  GSList* discovered;
};

typedef struct _InfDiscoveryAvahiPrivate InfDiscoveryAvahiPrivate;
struct _InfDiscoveryAvahiPrivate {
  AvahiPoll poll;

  InfIo* io;
  InfXmppManager* xmpp_manager;
  InfXmppConnectionSecurityPolicy security_policy;
  InfCertificateCredentials* creds;
  InfSaslContext* sasl_context;
  gchar* sasl_mechanisms;

  AvahiClient* client;

  GSList* published;
  GHashTable* discovered; /* type -> InfDiscoveryAvahiDiscoverInfo */
};

enum {
  PROP_0,

  /* construct only */
  PROP_XMPP_MANAGER,
  PROP_IO,
  PROP_CREDENTIALS,
  PROP_SASL_CONTEXT,
  PROP_SASL_MECHANISMS,

  /* read/write */
  PROP_SECURITY_POLICY
};

#define INF_DISCOVERY_AVAHI_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TYPE_DISCOVERY_AVAHI, InfDiscoveryAvahiPrivate))

static GObjectClass* parent_class;
static GQuark inf_discovery_avahi_error_quark;

/*
 * Destroy notification callbacks
 */

static void
inf_discovery_avahi_discovery_info_resolved_destroy_cb(gpointer user_data,
                                                       GObject* object)
{
  InfDiscoveryInfo* info;
  info = (InfDiscoveryInfo*)user_data;

  /* Connection has gone, next time we resolve this discovery we cannot use
   * the cached connection anymore. */
  info->resolved = NULL;
}

static void
inf_discovery_avahi_info_resolv_complete(InfDiscoveryInfo* info)
{
  GSList* item;
  InfDiscoveryAvahiInfoResolv* resolv;

  g_assert(info->resolved != NULL);

  for(item = info->resolv; item != NULL; item = g_slist_next(item))
  {
    resolv = (InfDiscoveryAvahiInfoResolv*)item->data;

    resolv->complete_func(
      info,
      INF_XML_CONNECTION(info->resolved),
      resolv->user_data
    );

    g_slice_free(InfDiscoveryAvahiInfoResolv, resolv);
  }

  g_slist_free(info->resolv);
  info->resolv = NULL;
}

static void
inf_discovery_avahi_info_resolv_error(InfDiscoveryInfo* info,
                                      const GError* error)
{
  GSList* item;
  InfDiscoveryAvahiInfoResolv* resolv;

  for(item = info->resolv; item != NULL; item = g_slist_next(item))
  {
    resolv = (InfDiscoveryAvahiInfoResolv*)item->data;

    resolv->error_func(info, error, resolv->user_data);
    g_slice_free(InfDiscoveryAvahiInfoResolv, resolv);
  }

  g_slist_free(info->resolv);
  info->resolv = NULL;
}

static void
inf_discovery_avahi_discovery_info_free(InfDiscoveryInfo* info)
{
  g_free(info->service_name);
  g_free(info->domain);
  
  if(info->service_resolver != NULL)
    avahi_service_resolver_free(info->service_resolver);

  if(info->resolved != NULL)
  {
    g_object_weak_unref(
      G_OBJECT(info->resolved),
      inf_discovery_avahi_discovery_info_resolved_destroy_cb,
      info
    );
  }

  /* TODO: Get an error from somewhere. If the avahi daemon goes down,
   * all InfDiscoveryInfos are discarded, but it would be nice to show a
   * correct error message for pending resolvs. */
  inf_discovery_avahi_info_resolv_error(info, NULL);

  g_slist_free(info->resolv);
  g_slice_free(InfDiscoveryInfo, info);
}

static void
inf_discovery_avahi_publisher_item_free(gpointer data)
{
  InfLocalPublisherItem* item;
  item = (InfLocalPublisherItem*)data;

  if(item->entry_group != NULL)
    avahi_entry_group_free(item->entry_group);

  g_free(item->type);
  avahi_free(item->name);
  g_slice_free(InfLocalPublisherItem, item);
}

static void
inf_discovery_avahi_discover_info_free(gpointer data)
{
  InfDiscoveryAvahiDiscoverInfo* info;
  GSList* item;

  info = (InfDiscoveryAvahiDiscoverInfo*)data;
  for(item = info->discovered; item != NULL; item = g_slist_next(item))
    inf_discovery_avahi_discovery_info_free((InfDiscoveryInfo*)item->data);

  if(info->service_browser != NULL)
    avahi_service_browser_free(info->service_browser);

  g_slist_free(info->discovered);
  g_free(info->type);
  g_slice_free(InfDiscoveryAvahiDiscoverInfo, info);
}

/*
 * Avahi callbacks and utilities
 */

static void
inf_discovery_avahi_service_resolver_callback(AvahiServiceResolver* resolver,
                                              AvahiIfIndex interface,
                                              AvahiProtocol protocol,
                                              AvahiResolverEvent event,
                                              const char* name,
                                              const char* type,
                                              const char* domain,
                                              const char* host_name,
                                              const AvahiAddress* address,
                                              uint16_t port,
                                              AvahiStringList* txt,
                                              AvahiLookupResultFlags flags,
                                              void* userdata)
{
  InfDiscoveryAvahi* avahi;
  InfDiscoveryAvahiPrivate* priv;
  InfDiscoveryAvahiDiscoverInfo* info;
  InfDiscoveryInfo* discovery_info;
  GSList* item;

  InfIpAddress* inf_addr;
  InfTcpConnection* tcp;
  InfXmppConnection* xmpp;
  InfXmlConnectionStatus status;
  GError* error;
  
  avahi = INF_DISCOVERY_AVAHI(userdata);
  priv = INF_DISCOVERY_AVAHI_PRIVATE(avahi);
  info = g_hash_table_lookup(priv->discovered, type);

  g_assert(info != NULL);

  /* Lookup discovery info for this service resolver */
  for(item = info->discovered; item != NULL; item = g_slist_next(item))
  {
    discovery_info = (InfDiscoveryInfo*)item->data;
    if(discovery_info->service_resolver == resolver)
      break;
  }

  /* If there was no discovery_info, we should have deleted the service
   * resolver before this callback could happen. */
  g_assert(item != NULL);

  /* There is no need to create a service resolver if this is
   * already resolved. */
  g_assert(discovery_info->resolved == NULL);

  /* So that the callbacks can recreate the resolver by resolving this
   * info again: */
  discovery_info->service_resolver = NULL;

  switch(event)
  {
  case AVAHI_RESOLVER_FOUND:
    switch(address->proto)
    {
    case AVAHI_PROTO_INET:
      inf_addr = inf_ip_address_new_raw4(address->data.ipv4.address);
      break;
    case AVAHI_PROTO_INET6:
      inf_addr = inf_ip_address_new_raw6(address->data.ipv6.address);
      break;
    default:
      g_assert_not_reached();
      break;
    }

    xmpp = inf_xmpp_manager_lookup_connection_by_address(
      priv->xmpp_manager,
      inf_addr,
      port
    );

    if(xmpp == NULL)
    {
      tcp = inf_tcp_connection_new(priv->io, inf_addr, port);

      g_object_set(
        G_OBJECT(tcp),
        "device-index", discovery_info->interface,
        NULL);

      error = NULL;
      if(inf_tcp_connection_open(tcp, &error) == FALSE)
      {
        inf_discovery_avahi_info_resolv_error(discovery_info, error);
        g_error_free(error);

        g_object_unref(tcp);
      }
      else
      {
        xmpp = inf_xmpp_connection_new(
          tcp,
          INF_XMPP_CONNECTION_CLIENT,
          NULL,
          host_name,
          priv->security_policy,
          priv->creds,
          priv->sasl_context,
          priv->sasl_context == NULL ? NULL : priv->sasl_mechanisms
        );

        g_object_unref(tcp);

        inf_xmpp_manager_add_connection(priv->xmpp_manager, xmpp);

        discovery_info->resolved = xmpp;

        g_object_weak_ref(
          G_OBJECT(xmpp),
          inf_discovery_avahi_discovery_info_resolved_destroy_cb,
          discovery_info
        );

        inf_discovery_avahi_info_resolv_complete(discovery_info);

        g_object_unref(xmpp);
      }
    }
    else
    {
      discovery_info->resolved = xmpp;

      g_object_weak_ref(
        G_OBJECT(xmpp),
        inf_discovery_avahi_discovery_info_resolved_destroy_cb,
        discovery_info
      );

      g_object_get(G_OBJECT(xmpp), "status", &status, NULL);

      /* TODO: There is similar code in inf_discovery_avahi_resolve; should
       * probably go into an extra function. */
      if(status == INF_XML_CONNECTION_CLOSING)
      {
        /* TODO: That's a bit a sad case here. We should wait for the
         * connection being closed, and then reopen it: */
        inf_discovery_avahi_info_resolv_error(discovery_info, NULL);
      }
      else if(status == INF_XML_CONNECTION_CLOSED)
      {
        error = NULL;
        if(!inf_xml_connection_open(INF_XML_CONNECTION(xmpp), &error))
        {
          inf_discovery_avahi_info_resolv_error(discovery_info, error);
          g_error_free(error);
        }
        else
        {
          inf_discovery_avahi_info_resolv_complete(discovery_info);
        }
      }
      else
      {
        inf_discovery_avahi_info_resolv_complete(discovery_info);
      }
    }
    
    inf_ip_address_free(inf_addr);
    break;
  case AVAHI_RESOLVER_FAILURE:
    error = NULL;

    g_set_error(
      &error,
      inf_discovery_avahi_error_quark,
      avahi_client_errno(avahi_service_resolver_get_client(resolver)),
      "%s",
      avahi_strerror(
        avahi_client_errno(avahi_service_resolver_get_client(resolver))
      )
    );

    inf_discovery_avahi_info_resolv_error(discovery_info, error);

    g_error_free(error);
    break;
  }

  avahi_service_resolver_free(resolver);
}

static void
inf_discovery_avahi_perform_undiscover(InfDiscoveryAvahi* avahi,
                                       InfDiscoveryAvahiDiscoverInfo* info);

static void
inf_discovery_avahi_perform_unpublish_item(InfLocalPublisherItem* item);

static void
inf_discovery_avahi_service_browser_callback(AvahiServiceBrowser* browser,
                                             AvahiIfIndex interface,
                                             AvahiProtocol protocol,
                                             AvahiBrowserEvent event,
                                             const char* name,
                                             const char* type,
                                             const char* domain,
                                             AvahiLookupResultFlags flags,
                                             void* userdata)
{
  InfDiscoveryAvahi* avahi;
  InfDiscoveryAvahiPrivate* priv;
  InfDiscoveryAvahiDiscoverInfo* info;
  InfDiscoveryInfo* discovery_info;
  GSList* item;

  avahi = INF_DISCOVERY_AVAHI(userdata);
  priv = INF_DISCOVERY_AVAHI_PRIVATE(avahi);
  info = g_hash_table_lookup(priv->discovered, type);
  g_assert(info != NULL);

  switch(event)
  {
  case AVAHI_BROWSER_NEW:
    /* Ignore what we published ourselves */
    if((flags & AVAHI_LOOKUP_RESULT_OUR_OWN) == 0)
    {
      discovery_info = g_slice_new(InfDiscoveryInfo);
      discovery_info->service_name = g_strdup(name);
      discovery_info->service_type = info->type;
      discovery_info->domain = g_strdup(domain);
      discovery_info->interface = interface;
      discovery_info->protocol = protocol;

      discovery_info->service_resolver = NULL;
      discovery_info->resolved = NULL;
      discovery_info->resolv = NULL;

      info->discovered = g_slist_prepend(info->discovered, discovery_info);
      inf_discovery_discovered(INF_DISCOVERY(avahi), discovery_info);
    }

    break;
  case AVAHI_BROWSER_REMOVE:
    for(item = info->discovered; item != NULL; item = g_slist_next(item))
    {
      discovery_info = (InfDiscoveryInfo*)item->data;
      g_assert(strcmp(discovery_info->service_type, type) == 0);

      /* TODO: Do we need to compare domain? */
      if(strcmp(discovery_info->service_name, name) == 0 &&
         discovery_info->interface == interface &&
         discovery_info->protocol == protocol)
      {
        inf_discovery_undiscovered(INF_DISCOVERY(avahi), discovery_info);
        info->discovered = g_slist_remove(info->discovered, discovery_info);
        inf_discovery_avahi_discovery_info_free(discovery_info);
        break;
      }
    }

    break;
  case AVAHI_BROWSER_CACHE_EXHAUSTED:
    /* Ignore */
    break;
  case AVAHI_BROWSER_ALL_FOR_NOW:
    /* Ignore */
    break;
  case AVAHI_BROWSER_FAILURE:
    g_warning(
      _("Avahi service browser is in failure state. Services of type '%s' "
        "are no longer discovered.\n\nThe failure was: %s\n"),
      info->type,
      avahi_strerror(
        avahi_client_errno(avahi_service_browser_get_client(browser))
      )
    );

    inf_discovery_avahi_perform_undiscover(avahi, info);
    break;
  default:
    g_assert_not_reached();
    break;
  }
}

static void
inf_discovery_avahi_entry_group_add_service(InfLocalPublisherItem* item)
{
  /* TODO: Error handling */
  avahi_entry_group_add_service(
    item->entry_group,
    AVAHI_IF_UNSPEC,
    AVAHI_PROTO_UNSPEC,
    0,
    item->name,
    item->type,
    NULL,
    NULL,
    item->port,
    NULL
  );
}

static void
inf_discovery_avahi_entry_group_callback(AvahiEntryGroup* group,
                                         AvahiEntryGroupState state,
                                         void* userdata)
{
  InfLocalPublisherItem* item;
  char* new_name;

  item = (InfLocalPublisherItem*)userdata;

  switch(state)
  {
  case AVAHI_ENTRY_GROUP_UNCOMMITED:
    /* Not yet committed */
    break;
  case AVAHI_ENTRY_GROUP_REGISTERING:
    /* This is currently being registered */
    break;
  case AVAHI_ENTRY_GROUP_ESTABLISHED:
    /* The service is published */
    break;
  case AVAHI_ENTRY_GROUP_COLLISION:
    /* There was a name collision, choose a new name */
    new_name = avahi_alternative_service_name(item->name);
    avahi_free(item->name);
    item->name = new_name;

    /* TODO: Error handling */
    avahi_entry_group_reset(item->entry_group);
    inf_discovery_avahi_entry_group_add_service(item);
    avahi_entry_group_commit(item->entry_group);
    break;
  case AVAHI_ENTRY_GROUP_FAILURE:
    g_warning(
      _("Avahi entry group is in failure state. The service '%s' of type "
        "'%s' is no longer published.\n\nThe failure was: %s\n"),
      item->name,
      item->type,
      avahi_strerror(avahi_client_errno(avahi_entry_group_get_client(group)))
    );

    /* There was a failure, remove entry group */
    inf_discovery_avahi_perform_unpublish_item(item);
    break;
  default:
    g_assert_not_reached();
    break;
  }
}

static void
inf_discovery_avahi_perform_publish_item(InfDiscoveryAvahi* avahi,
                                         InfLocalPublisherItem* item)
{
  InfDiscoveryAvahiPrivate* priv;

  priv = INF_DISCOVERY_AVAHI_PRIVATE(avahi);

  if(item->entry_group == NULL)
  {
    /* TODO: Error handling if one of these calls fail */
    item->entry_group = avahi_entry_group_new(
      priv->client,
      inf_discovery_avahi_entry_group_callback,
      item
    );

    inf_discovery_avahi_entry_group_add_service(item);

    avahi_entry_group_commit(item->entry_group);
  }
}

static void
inf_discovery_avahi_perform_unpublish_item(InfLocalPublisherItem* item)
{
  if(item->entry_group != NULL)
  {
    avahi_entry_group_free(item->entry_group);
    item->entry_group = NULL;
  }
}

static void
inf_discovery_avahi_perform_publish_all(InfDiscoveryAvahi* avahi)
{
  InfDiscoveryAvahiPrivate* priv;
  GSList* item;

  priv = INF_DISCOVERY_AVAHI_PRIVATE(avahi);

  for(item = priv->published; item != NULL; item = g_slist_next(item))
  {
    inf_discovery_avahi_perform_publish_item(
      avahi,
      (InfLocalPublisherItem*)item->data
    );
  }
}

static void
inf_discovery_avahi_perform_unpublish_all(InfDiscoveryAvahi* avahi)
{
  InfDiscoveryAvahiPrivate* priv;
  GSList* item;

  priv = INF_DISCOVERY_AVAHI_PRIVATE(avahi);

  for(item = priv->published; item != NULL; item = g_slist_next(item))
  {
    inf_discovery_avahi_perform_unpublish_item(
      (InfLocalPublisherItem*)item->data
    );
  }
}

static void
inf_discovery_avahi_perform_discover(InfDiscoveryAvahi* avahi,
                                     InfDiscoveryAvahiDiscoverInfo* info)
{
  InfDiscoveryAvahiPrivate* priv;
  priv = INF_DISCOVERY_AVAHI_PRIVATE(avahi);

  if(info->service_browser == NULL)
  {
    info->service_browser = avahi_service_browser_new(
      priv->client,
      AVAHI_IF_UNSPEC,
      AVAHI_PROTO_UNSPEC,
      info->type,
      NULL,
      0,
      inf_discovery_avahi_service_browser_callback,
      avahi
    );
  }
}

static void
inf_discovery_avahi_perform_undiscover(InfDiscoveryAvahi* avahi,
                                       InfDiscoveryAvahiDiscoverInfo* info)
{
  /* Remove discovered infos, these might no longer be valid. They are
   * rediscovered when inf_discovery_avahi_perform_discover() is called 
   * again. */
  InfDiscoveryInfo* discovery_info;
  GSList* next;

  for(; info->discovered != NULL; info->discovered = next)
  {
    next = info->discovered->next;
    discovery_info = (InfDiscoveryInfo*)info->discovered->data;

    inf_discovery_undiscovered(INF_DISCOVERY(avahi), discovery_info);
    inf_discovery_avahi_discovery_info_free(discovery_info);

    info->discovered = g_slist_delete_link(
      info->discovered,
      info->discovered
    );
  }

  if(info->service_browser != NULL)
  {
    avahi_service_browser_free(info->service_browser);
    info->service_browser = NULL;
  }
}

static void
inf_discovery_avahi_perform_discover_all_foreach_func(gpointer key,
                                                      gpointer value,
                                                      gpointer user_data)
{
  inf_discovery_avahi_perform_discover(
    INF_DISCOVERY_AVAHI(user_data),
    (InfDiscoveryAvahiDiscoverInfo*)value
  );
}

static void
inf_discovery_avahi_perform_undiscover_all_foreach_func(gpointer key,
                                                        gpointer value,
                                                        gpointer user_data)
{
  inf_discovery_avahi_perform_undiscover(
    INF_DISCOVERY_AVAHI(user_data),
    (InfDiscoveryAvahiDiscoverInfo*)value
  );
}

static void
inf_discovery_avahi_perform_discover_all(InfDiscoveryAvahi* avahi)
{
  InfDiscoveryAvahiPrivate* priv;
  priv = INF_DISCOVERY_AVAHI_PRIVATE(avahi);

  g_hash_table_foreach(
    priv->discovered,
    inf_discovery_avahi_perform_discover_all_foreach_func,
    avahi
  );
}

static void
inf_discovery_avahi_perform_undiscover_all(InfDiscoveryAvahi* avahi)
{
  InfDiscoveryAvahiPrivate* priv;
  priv = INF_DISCOVERY_AVAHI_PRIVATE(avahi);

  g_hash_table_foreach(
    priv->discovered,
    inf_discovery_avahi_perform_undiscover_all_foreach_func,
    avahi
  );
}

/* Required by inf_discovery_avahi_create_client() */
static void
inf_discovery_avahi_client_callback(AvahiClient* client,
                                    AvahiClientState state,
                                    void* userdata);

static void
inf_discovery_avahi_create_client(InfDiscoveryAvahi* discovery)
{
  InfDiscoveryAvahiPrivate* priv;
  int error;

  priv = INF_DISCOVERY_AVAHI_PRIVATE(discovery);

  priv->client = avahi_client_new(
    &priv->poll,
    AVAHI_CLIENT_NO_FAIL,
    inf_discovery_avahi_client_callback,
    discovery,
    &error
  );

  /* This still seems to happen sometimes, even though we pass
   * AVAHI_CLIENT_NO_FAIL */
  if(priv->client == NULL)
  {
    g_warning(_
      ("Failed to start Avahi client. Service discovery or publishing "
       "will not be possible.\n\nThe occurred failure was: %s"),
      avahi_strerror(error)
    );
  }
}

static void
inf_discovery_avahi_client_callback(AvahiClient* client,
                                    AvahiClientState state,
                                    void* userdata)
{
  InfDiscoveryAvahi* avahi;
  InfDiscoveryAvahiPrivate* priv;

  avahi = INF_DISCOVERY_AVAHI(userdata);
  priv = INF_DISCOVERY_AVAHI_PRIVATE(avahi);

  switch(state)
  {
  case AVAHI_CLIENT_S_COLLISION:
    /* TODO: What to do in that case. Should we handle this as an error,
     * i.e. withdraw service browsers and entry groups? */
    break;
  case AVAHI_CLIENT_S_REGISTERING:
    /* Wait for client to become running */
    break;
  case AVAHI_CLIENT_S_RUNNING:
    /* Discovery and publish when running */
    inf_discovery_avahi_perform_publish_all(avahi);
    inf_discovery_avahi_perform_discover_all(avahi);
    break;
  case AVAHI_CLIENT_FAILURE:
    inf_discovery_avahi_perform_unpublish_all(avahi);
    inf_discovery_avahi_perform_undiscover_all(avahi);

    if(avahi_client_errno(client) != AVAHI_ERR_DISCONNECTED)
    {
      inf_discovery_avahi_create_client(avahi);
    }
    else
    {
      g_warning(
        _("Avahi client is in failure state. Service discovery or "
          "publishing is no longer possible.\n\nThe occured failure "
          "was: %s\n"),
        avahi_strerror(avahi_client_errno(client))
      );
    }
  case AVAHI_CLIENT_CONNECTING:
    /* Wait for connection */
    break;
  default:
    g_assert_not_reached();
    break;
  }
}

/*
 * AvahiPoll implementation
 */

static AvahiWatchEvent
inf_discovery_avahi_from_io_event(InfIoEvent event)
{
  AvahiWatchEvent res;

  res = 0;
  if(event & INF_IO_INCOMING) res |= AVAHI_WATCH_IN;
  if(event & INF_IO_OUTGOING) res |= AVAHI_WATCH_OUT;
  if(event & INF_IO_ERROR) res |= AVAHI_WATCH_ERR;

  return res;
}

static InfIoEvent
inf_discovery_avahi_to_io_event(AvahiWatchEvent event)
{
  InfIoEvent res;

  res = 0;
  if(event & AVAHI_WATCH_IN) res |= INF_IO_INCOMING;
  if(event & AVAHI_WATCH_OUT) res |= INF_IO_OUTGOING;
  if(event & (AVAHI_WATCH_ERR | AVAHI_WATCH_HUP)) res |= INF_IO_ERROR;

  return res;
}

static void
inf_discovery_avahi_watch_cb(InfNativeSocket* socket,
                             InfIoEvent event,
                             gpointer user_data)
{
  AvahiWatch* watch;
  watch = (AvahiWatch*)user_data;

  watch->occured_events = inf_discovery_avahi_from_io_event(event);
  watch->callback(watch, *socket, watch->occured_events, watch->userdata);
}

static void
inf_discovery_avahi_timeout_cb(gpointer user_data)
{
  AvahiTimeout* timeout;
  timeout = (AvahiTimeout*)user_data;

  timeout->timeout = NULL;
  timeout->callback(timeout, timeout->userdata);
}

static AvahiWatch*
inf_discovery_avahi_watch_new(const AvahiPoll* api,
                              int fd,
                              AvahiWatchEvent event,
                              AvahiWatchCallback callback,
                              void* userdata)
{
  InfDiscoveryAvahi* avahi;
  InfDiscoveryAvahiPrivate* priv;
  AvahiWatch* watch;

  avahi = INF_DISCOVERY_AVAHI(api->userdata);
  priv = INF_DISCOVERY_AVAHI_PRIVATE(avahi);
  watch = g_slice_new(AvahiWatch);

  watch->avahi = avahi;
  watch->socket = fd;
  watch->occured_events = 0;
  watch->callback = callback;
  watch->userdata = userdata;

  watch->watch = inf_io_add_watch(
    priv->io,
    &watch->socket,
    inf_discovery_avahi_to_io_event(event),
    inf_discovery_avahi_watch_cb,
    watch,
    NULL
  );

  return watch;
}

static void
inf_discovery_avahi_watch_update(AvahiWatch* watch,
                                 AvahiWatchEvent event)
{
  InfDiscoveryAvahiPrivate* priv;
  priv = INF_DISCOVERY_AVAHI_PRIVATE(watch->avahi);

  inf_io_update_watch(
    priv->io,
    watch->watch,
    inf_discovery_avahi_to_io_event(event)
  );
}

static AvahiWatchEvent
inf_discovery_avahi_watch_get_events(AvahiWatch* watch)
{
  return watch->occured_events;
}

static void
inf_discovery_avahi_watch_free(AvahiWatch* watch)
{
  InfDiscoveryAvahiPrivate* priv;
  priv = INF_DISCOVERY_AVAHI_PRIVATE(watch->avahi);

  inf_io_remove_watch(priv->io, watch->watch);
  g_slice_free(AvahiWatch, watch);
}

static AvahiTimeout*
inf_discovery_avahi_timeout_new(const AvahiPoll* api,
                                const struct timeval* tv,
                                AvahiTimeoutCallback callback,
                                void* userdata)
{
  InfDiscoveryAvahi* avahi;
  InfDiscoveryAvahiPrivate* priv;
  AvahiTimeout* timeout;
  AvahiUsec usec;

  avahi = INF_DISCOVERY_AVAHI(api->userdata);
  priv = INF_DISCOVERY_AVAHI_PRIVATE(avahi);
  timeout = g_slice_new(AvahiTimeout);

  timeout->avahi = avahi;
  timeout->callback = callback;
  timeout->userdata = userdata;

  if(tv != NULL)
  {
    /* Timeout in the past is triggered instantly */
    usec = avahi_age(tv);
    if(usec > 0) usec = 0;

    timeout->timeout = inf_io_add_timeout(
      priv->io,
      ((-usec) + 500) / 1000,
      inf_discovery_avahi_timeout_cb,
      timeout,
      NULL
    );
  }
  else
  {
    timeout->timeout = NULL;
  }

  return timeout;
}

static void
inf_discovery_avahi_timeout_update(AvahiTimeout* timeout,
                                   const struct timeval* tv)
{
  InfDiscoveryAvahiPrivate* priv;
  AvahiUsec usec;
  priv = INF_DISCOVERY_AVAHI_PRIVATE(timeout->avahi);

  if(timeout->timeout != NULL)
    inf_io_remove_timeout(priv->io, timeout->timeout);

  if(tv != NULL)
  {
    /* Timeout in the past is triggered instantly */
    usec = avahi_age(tv);
    if(usec > 0) usec = 0;

    timeout->timeout = inf_io_add_timeout(
      priv->io,
      ((-usec) + 500) / 1000,
      inf_discovery_avahi_timeout_cb,
      timeout,
      NULL
    );
  }
  else
  {
    timeout->timeout = NULL;
  }
}

static void
inf_discovery_avahi_timeout_free(AvahiTimeout* timeout)
{
  InfDiscoveryAvahiPrivate* priv;
  priv = INF_DISCOVERY_AVAHI_PRIVATE(timeout->avahi);

  if(timeout->timeout != NULL)
    inf_io_remove_timeout(priv->io, timeout->timeout);

  g_slice_free(AvahiTimeout, timeout);
}

/*
 * GObject overrides
 */

static void
inf_discovery_avahi_init(GTypeInstance* instance,
                         gpointer g_class)
{
  InfDiscoveryAvahi* avahi;
  InfDiscoveryAvahiPrivate* priv;

  avahi = INF_DISCOVERY_AVAHI(instance);
  priv = INF_DISCOVERY_AVAHI_PRIVATE(avahi);
  
  priv->poll.userdata = avahi;
  priv->poll.watch_new = inf_discovery_avahi_watch_new;
  priv->poll.watch_update = inf_discovery_avahi_watch_update;
  priv->poll.watch_get_events = inf_discovery_avahi_watch_get_events;
  priv->poll.watch_free = inf_discovery_avahi_watch_free;
  priv->poll.timeout_new = inf_discovery_avahi_timeout_new;
  priv->poll.timeout_update = inf_discovery_avahi_timeout_update;
  priv->poll.timeout_free = inf_discovery_avahi_timeout_free;

  priv->io = NULL;
  priv->xmpp_manager = NULL;
  priv->security_policy = INF_XMPP_CONNECTION_SECURITY_BOTH_PREFER_TLS;
  priv->creds = NULL;
  priv->sasl_context = NULL;
  priv->sasl_mechanisms = NULL;

  priv->client = NULL;
  priv->published = NULL;

  priv->discovered = g_hash_table_new_full(
    g_str_hash,
    g_str_equal,
    NULL, /* key is owned by value */
    inf_discovery_avahi_discover_info_free
  );
}

static GObject*
inf_discovery_avahi_constructor(GType type,
                                guint n_construct_properties,
                                GObjectConstructParam* construct_properties)
{
  GObject* object;
  InfDiscoveryAvahiPrivate* priv;
  
  object = G_OBJECT_CLASS(parent_class)->constructor(
    type,
    n_construct_properties,
    construct_properties
  );

  priv = INF_DISCOVERY_AVAHI_PRIVATE(object);

  /* Must have been set as construct only property */
  g_assert(priv->io != NULL);

  inf_discovery_avahi_create_client(INF_DISCOVERY_AVAHI(object));
  return object;
}

static void
inf_discovery_avahi_dispose(GObject* object)
{
  InfDiscoveryAvahi* avahi;
  InfDiscoveryAvahiPrivate* priv;
  GSList* item;

  avahi = INF_DISCOVERY_AVAHI(object);
  priv = INF_DISCOVERY_AVAHI_PRIVATE(avahi);

  g_hash_table_destroy(priv->discovered);
  priv->discovered = NULL;

  for(item = priv->published; item != NULL; item = g_slist_next(item))
    inf_discovery_avahi_publisher_item_free(item->data);
  g_slist_free(priv->published);
  priv->published = NULL;

  if(priv->client != NULL)
  {
    avahi_client_free(priv->client);
    priv->client = NULL;
  }

  if(priv->xmpp_manager != NULL)
  {
    g_object_unref(G_OBJECT(priv->xmpp_manager));
    priv->xmpp_manager = NULL;
  }

  if(priv->creds != NULL)
  {
    inf_certificate_credentials_unref(priv->creds);
    priv->creds = NULL;
  }
  
  if(priv->sasl_context != NULL)
  {
    inf_sasl_context_unref(priv->sasl_context);
    priv->sasl_context = NULL;
  }

  if(priv->io != NULL)
  {
    g_object_unref(G_OBJECT(priv->io));
    priv->io = NULL;
  }

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_discovery_avahi_finalize(GObject* object)
{
  InfDiscoveryAvahi* avahi;
  InfDiscoveryAvahiPrivate* priv;

  avahi = INF_DISCOVERY_AVAHI(object);
  priv = INF_DISCOVERY_AVAHI_PRIVATE(avahi);

  g_free(priv->sasl_mechanisms);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_discovery_avahi_set_property(GObject* object,
                                 guint prop_id,
                                 const GValue* value,
                                 GParamSpec* pspec)
{
  InfDiscoveryAvahi* avahi;
  InfDiscoveryAvahiPrivate* priv;

  avahi = INF_DISCOVERY_AVAHI(object);
  priv = INF_DISCOVERY_AVAHI_PRIVATE(avahi);

  switch(prop_id)
  {
  case PROP_IO:
    g_assert(priv->io == NULL); /* construct only */
    priv->io = INF_IO(g_value_dup_object(value));
    break;
  case PROP_XMPP_MANAGER:
    g_assert(priv->xmpp_manager == NULL); /* construct only */
    priv->xmpp_manager = INF_XMPP_MANAGER(g_value_dup_object(value));
    break;
  case PROP_CREDENTIALS:
    if(priv->creds != NULL) inf_certificate_credentials_unref(priv->creds);
    priv->creds = (InfCertificateCredentials*)g_value_dup_boxed(value);
    break;
  case PROP_SASL_CONTEXT:
    priv->sasl_context = (InfSaslContext*)g_value_dup_boxed(value);
    break;
  case PROP_SASL_MECHANISMS:
    g_free(priv->sasl_mechanisms);
    priv->sasl_mechanisms = g_value_dup_string(value);
    break;
  case PROP_SECURITY_POLICY:
    priv->security_policy = g_value_get_enum(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_discovery_avahi_get_property(GObject* object,
                                 guint prop_id,
                                 GValue* value,
                                 GParamSpec* pspec)
{
  InfDiscoveryAvahi* avahi;
  InfDiscoveryAvahiPrivate* priv;

  avahi = INF_DISCOVERY_AVAHI(object);
  priv = INF_DISCOVERY_AVAHI_PRIVATE(avahi);

  switch(prop_id)
  {
  case PROP_IO:
    g_value_set_object(value, G_OBJECT(priv->io));
    break;
  case PROP_XMPP_MANAGER:
    g_value_set_object(value, G_OBJECT(priv->xmpp_manager));
    break;
  case PROP_CREDENTIALS:
    g_value_set_boxed(value, priv->creds);
    break;
  case PROP_SASL_CONTEXT:
    g_value_set_boxed(value, priv->sasl_context);
    break;
  case PROP_SASL_MECHANISMS:
    g_value_set_string(value, priv->sasl_mechanisms);
    break;
  case PROP_SECURITY_POLICY:
    g_value_set_enum(value, priv->security_policy);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

/*
 * InfDiscovery implementation.
 */

static void
inf_discovery_avahi_discover(InfDiscovery* discovery,
                             const gchar* type)
{
  InfDiscoveryAvahiPrivate* priv;
  InfDiscoveryAvahiDiscoverInfo* info;

  priv = INF_DISCOVERY_AVAHI_PRIVATE(discovery);
  info = g_hash_table_lookup(priv->discovered, type);

  if(info == NULL)
  {
    info = g_slice_new(InfDiscoveryAvahiDiscoverInfo);

    info->type = g_strdup(type);
    info->service_browser = NULL;
    info->discovered = NULL;
    g_hash_table_insert(priv->discovered, info->type, info);

    if(priv->client != NULL &&
       avahi_client_get_state(priv->client) == AVAHI_CLIENT_S_RUNNING)
    {
      inf_discovery_avahi_perform_discover(
        INF_DISCOVERY_AVAHI(discovery),
        info
      );
    }
  }
}

static GSList*
inf_discovery_avahi_get_discovered(InfDiscovery* discovery,
                                   const gchar* type)
{
  InfDiscoveryAvahiPrivate* priv;
  InfDiscoveryAvahiDiscoverInfo* info;

  priv = INF_DISCOVERY_AVAHI_PRIVATE(discovery);
  info = g_hash_table_lookup(priv->discovered, type);
  if(info == NULL) return NULL;

  return g_slist_copy(info->discovered);
}

static void
inf_discovery_avahi_resolve(InfDiscovery* discovery,
                            InfDiscoveryInfo* info,
                            InfDiscoveryResolvCompleteFunc complete_func,
                            InfDiscoveryResolvErrorFunc error_func,
                            gpointer user_data)
{
  InfDiscoveryAvahiPrivate* priv;
  InfDiscoveryAvahiInfoResolv* resolv;
  InfXmlConnectionStatus status;
  int errno;
  GError* error;

  priv = INF_DISCOVERY_AVAHI_PRIVATE(discovery);
  g_assert(priv->client != NULL);
  g_assert(avahi_client_get_state(priv->client) == AVAHI_CLIENT_S_RUNNING);

  if(info->resolved != NULL)
  {
    g_object_get(G_OBJECT(info->resolved), "status", &status, NULL);

    switch(status)
    {
    case INF_XML_CONNECTION_CLOSED:
      error = NULL;
      if(!inf_xml_connection_open(INF_XML_CONNECTION(info->resolved), &error))
      {
        error_func(info, error, user_data);
        g_error_free(error);
      }
      else
      {
        complete_func(info, INF_XML_CONNECTION(info->resolved), user_data);
      }

      break;
    case INF_XML_CONNECTION_CLOSING:
      /* TODO: We should add ourselves to the resolver list, and wait for
       * the connection being closed and reopen it afterwards. */
      error_func(info, NULL, user_data);
      break;
    case INF_XML_CONNECTION_OPENING:
    case INF_XML_CONNECTION_OPEN:
      complete_func(info, INF_XML_CONNECTION(info->resolved), user_data);
      break;
    default:
      g_assert_not_reached();
      break;
    }
  }
  else
  {
    resolv = g_slice_new(InfDiscoveryAvahiInfoResolv);
    resolv->complete_func = complete_func;
    resolv->error_func = error_func;
    resolv->user_data = user_data;
    info->resolv = g_slist_prepend(info->resolv, resolv);

    if(info->service_resolver == NULL)
    {
      info->service_resolver = avahi_service_resolver_new(
        priv->client,
        info->interface,
        info->protocol,
        info->service_name,
        info->service_type,
        info->domain,
        AVAHI_PROTO_UNSPEC,
        0,
        inf_discovery_avahi_service_resolver_callback,
        discovery
      );

      if(info->service_resolver == NULL)
      {
        error = NULL;

        errno = avahi_client_errno(
          avahi_service_resolver_get_client(info->service_resolver)
        );

        g_set_error(
          &error,
          inf_discovery_avahi_error_quark,
          errno,
          "%s",
          avahi_strerror(errno)
        );

        inf_discovery_avahi_info_resolv_error(info, error);
        g_error_free(error);
      }
    }
  }
}

static gchar*
inf_discovery_avahi_info_get_service_name(InfDiscovery* discovery,
                                          InfDiscoveryInfo* info)
{
  char device_name[IF_NAMESIZE];
  if(if_indextoname(info->interface, device_name) == NULL)
    return NULL;

  return g_strdup_printf(
    info->protocol == AVAHI_PROTO_INET ?
    /* Translators: This is "<Service Name> (via <Network Interface> on
     * <address family>)", for example "ck (via eth0 on IPv4)". */
      _("%s (via %s on IPv4)") :
      _("%s (via %s on IPv6)"),
    info->service_name,
    device_name
  );
}

static const gchar*
inf_discovery_avahi_info_get_service_type(InfDiscovery* discovery,
                                          InfDiscoveryInfo* info)
{
  return info->service_type;
}

static InfLocalPublisherItem*
inf_discovery_avahi_publish(InfLocalPublisher* publisher,
                            const gchar* type,
                            const gchar* name,
                            guint port)
{
  InfDiscoveryAvahiPrivate* priv;
  InfLocalPublisherItem* item;

  priv = INF_DISCOVERY_AVAHI_PRIVATE(publisher);
  item = g_slice_new(InfLocalPublisherItem);

  item->type = g_strdup(type);
  item->name = avahi_strdup(name);
  item->port = port;
  item->entry_group = NULL;
  priv->published = g_slist_prepend(priv->published, item);

  if(priv->client != NULL &&
     avahi_client_get_state(priv->client) == AVAHI_CLIENT_S_RUNNING)
  {
    inf_discovery_avahi_perform_publish_item(
      INF_DISCOVERY_AVAHI(publisher),
      item
    );
  }

  return item;
}

static void
inf_discovery_avahi_unpublish(InfLocalPublisher* publisher,
                              InfLocalPublisherItem* item)
{
  InfDiscoveryAvahiPrivate* priv;
  priv = INF_DISCOVERY_AVAHI_PRIVATE(publisher);

  g_assert(g_slist_find(priv->published, item) != NULL);

  inf_discovery_avahi_publisher_item_free(item);
  priv->published = g_slist_remove(priv->published, item);
}

/*
 * Gype registration.
 */

static void
inf_discovery_avahi_class_init(gpointer g_class,
                               gpointer class_data)
{
  GObjectClass* object_class;
  InfDiscoveryAvahiClass* avahi_class;

  object_class = G_OBJECT_CLASS(g_class);
  avahi_class = INF_DISCOVERY_AVAHI_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfDiscoveryAvahiPrivate));

  object_class->constructor = inf_discovery_avahi_constructor;
  object_class->dispose = inf_discovery_avahi_dispose;
  object_class->finalize = inf_discovery_avahi_finalize;
  object_class->set_property = inf_discovery_avahi_set_property;
  object_class->get_property = inf_discovery_avahi_get_property;

  inf_discovery_avahi_error_quark = g_quark_from_static_string(
    "INF_DISCOVERY_AVAHI_ERROR"
  );

  g_object_class_install_property(
    object_class,
    PROP_IO,
    g_param_spec_object(
      "io",
      "IO",
      "The IO object used for watching sockets and timeouts",
      INF_TYPE_IO,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_XMPP_MANAGER,
    g_param_spec_object(
      "xmpp-manager",
      "XMPP manager",
      "The XMPP manager to register resolved XMPP connections",
      INF_TYPE_XMPP_MANAGER,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_CREDENTIALS,
    g_param_spec_boxed(
      "credentials",
      "Certificate credentials",
      "The GnuTLS certificate credentials used for encrypting XMPP streams",
      INF_TYPE_CERTIFICATE_CREDENTIALS,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_SASL_CONTEXT,
    g_param_spec_boxed(
      "sasl-context",
      "SASL context",
      "The SASL context used for authentication",
      INF_TYPE_SASL_CONTEXT,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_SASL_MECHANISMS,
    g_param_spec_string(
      "sasl-mechanisms",
      "SASL mechanisms",
      "The accepted SASL mechanisms for authentication",
      NULL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_SECURITY_POLICY,
    g_param_spec_enum(
      "security-policy",
      "Security policy",
      "How to decide whether to use TLS",
      INF_TYPE_XMPP_CONNECTION_SECURITY_POLICY,
      INF_XMPP_CONNECTION_SECURITY_BOTH_PREFER_TLS,
      G_PARAM_READWRITE
    )
  );
}

static void
inf_discovery_avahi_discovery_init(gpointer g_iface,
                                   gpointer iface_data)
{
  InfDiscoveryIface* iface;
  iface = (InfDiscoveryIface*)g_iface;

  iface->discover = inf_discovery_avahi_discover;
  iface->get_discovered = inf_discovery_avahi_get_discovered;
  iface->resolve = inf_discovery_avahi_resolve;
  iface->info_get_service_name = inf_discovery_avahi_info_get_service_name;
  iface->info_get_service_type = inf_discovery_avahi_info_get_service_type;
  iface->discovered = NULL;
  iface->undiscovered = NULL;
}

static void
inf_discovery_avahi_local_publisher_init(gpointer g_iface,
                                         gpointer iface_data)
{
  InfLocalPublisherIface* iface;
  iface = (InfLocalPublisherIface*)g_iface;

  iface->publish = inf_discovery_avahi_publish;
  iface->unpublish = inf_discovery_avahi_unpublish;
}

GType
inf_discovery_avahi_get_type(void)
{
  static GType discovery_avahi_type = 0;

  if(!discovery_avahi_type)
  {
    static const GTypeInfo discovery_avahi_type_info = {
      sizeof(InfDiscoveryAvahiClass),  /* class_size */
      NULL,                            /* base_init */
      NULL,                            /* base_finalize */
      inf_discovery_avahi_class_init,  /* class_init */
      NULL,                            /* class_finalize */
      NULL,                            /* class_data */
      sizeof(InfDiscoveryAvahi),       /* instance_size */
      0,                               /* n_preallocs */
      inf_discovery_avahi_init,        /* instance_init */
      NULL                             /* value_table */
    };

    static const GInterfaceInfo discovery_info = {
      inf_discovery_avahi_discovery_init,
      NULL,
      NULL
    };

    static const GInterfaceInfo local_publisher_info = {
      inf_discovery_avahi_local_publisher_init,
      NULL,
      NULL
    };

    discovery_avahi_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfDiscoveryAvahi",
      &discovery_avahi_type_info,
      0
    );

    g_type_add_interface_static(
      discovery_avahi_type,
      INF_TYPE_DISCOVERY,
      &discovery_info
    );

    g_type_add_interface_static(
      discovery_avahi_type,
      INF_TYPE_LOCAL_PUBLISHER,
      &local_publisher_info
    );
  }

  return discovery_avahi_type;
}

/*
 * Public API.
 */

/**
 * inf_discovery_avahi_new:
 * @io: A #InfIo object used for watching sockets and timeouts.
 * @manager: A #InfXmppManager.
 * @creds: The certificate credentials used for GnuTLS encryption.
 * @sasl_context: A SASL context used for authentication.
 * @sasl_mechanisms: A whitespace-separated list of accepted SASL mechanisms,
 * or %NULL.
 *
 * Created a new #InfDiscoveryAvahi object which can be used to publish and
 * discovery Infinote services on the local network. When resolving a
 * #InfDiscoveryInfo (which means obtaining a #InfXmlConnection for the
 * discovered service) a new #InfXmppConnection needs to be created if there
 * is not already one for the destination host in @manager. Such
 * connections are created with the @creds and @sasl_context parameters.
 * These may be %NULL in which case #InfXmppConnection uses builtin
 * credentials or a builtin context, respectively.
 *
 * If this #InfDiscoveryAvahi is not used to discover services but only to
 * publish any, then @creds, @sasl_context and @sasl_mechanisms are ignored
 * and can safely set to be %NULL.
 *
 * @sasl_mechanisms specifies allowed mechanisms used for authentication with
 * the server. It can be %NULL, in which case all available mechanisms are
 * accepted.
 *
 * Return Value: A new #InfDiscoveryAvahi.
 **/
InfDiscoveryAvahi*
inf_discovery_avahi_new(InfIo* io,
                        InfXmppManager* manager,
                        InfCertificateCredentials* creds,
                        InfSaslContext* sasl_context,
                        const gchar* sasl_mechanisms)
{
  GObject* object;

  g_return_val_if_fail(INF_IS_IO(io), NULL);
  g_return_val_if_fail(INF_IS_XMPP_MANAGER(manager), NULL);

  object = g_object_new(
    INF_TYPE_DISCOVERY_AVAHI,
    "io", io,
    "xmpp-manager", manager,
    "credentials", creds,
    "sasl-context", sasl_context,
    "sasl-mechanisms", sasl_mechanisms,
    NULL
  );

  return INF_DISCOVERY_AVAHI(object);
}

/**
 * inf_discovery_avahi_set_security_policy:
 * @discovery: A #InfDiscoveryAvahi.
 * @plcy: The new security policy.
 *
 * Sets the #InfXmppConnectionSecurityPolicy for newly created
 * #InfXmppConnection<!-- -->s. It does not affect already existing
 * connections.
 */
void
inf_discovery_avahi_set_security_policy(InfDiscoveryAvahi* discovery,
                                        InfXmppConnectionSecurityPolicy plcy)
{
  g_return_if_fail(INF_IS_DISCOVERY_AVAHI(discovery));
  INF_DISCOVERY_AVAHI_PRIVATE(discovery)->security_policy = plcy;
}

/**
 * inf_discovery_avahi_get_security_policy:
 * @discovery: A #InfDiscoveryAvahi.
 *
 * Returns the current security policy used for new
 * #InfXmppConnection<!-- -->s.
 *
 * Returns: The current security policy.
 */
InfXmppConnectionSecurityPolicy
inf_discovery_avahi_get_security_policy(InfDiscoveryAvahi* discovery)
{
  g_return_val_if_fail(
    INF_IS_DISCOVERY_AVAHI(discovery),
    INF_XMPP_CONNECTION_SECURITY_BOTH_PREFER_TLS
  );

  return INF_DISCOVERY_AVAHI_PRIVATE(discovery)->security_policy;
}

#endif /* LIBINFINITY_HAVE_AVAHI */

/* vim:set et sw=2 ts=2: */
