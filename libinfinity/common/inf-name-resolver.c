/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2015 Armin Burgmeier <armin@arbur.net>
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
 * SECTION:inf-name-resolver
 * @title: InfNameResolver
 * @short_description: Asynchronous DNS name lookup with support for SRV
 * records
 * @include: libinfinity/common/inf-name-resolver.h
 * @stability: Unstable
 *
 * #InfNameResolver provides a portable interface to look up DNS entries. When
 * a resorver object has been created, the name lookup can be started with
 * inf_name_resolver_start(), and once it finishes, the
 * #InfNameResolver::resolved signal is emitted. The
 * inf_name_resolver_get_address() and inf_name_resolver_get_port() functions
 * can then be used to obtain the result.
 *
 * There can at most be one hostname lookup at a time. If you need more than
 * one concurrent hostname lookup, use multiple #InfNameResolver objects.
 **/

#include <libinfinity/common/inf-name-resolver.h>
#include <libinfinity/common/inf-async-operation.h>
#include <libinfinity/inf-i18n.h>

/* For getaddrinfo */
#ifdef G_OS_WIN32
# include <ws2tcpip.h>
/* We need to include wspiapi.h to support getaddrinfo on Windows 2000.
 * See the MSDN article for getaddrinfo
 * http://msdn.microsoft.com/en-us/library/ms738520(VS.85).aspx
 * and bug #425. */
# include <wspiapi.h>
#else
# include <sys/types.h>
# include <sys/socket.h>
# include <netinet/in.h> /* Required for FreeBSD. See bug #431. */
# include <netdb.h>
#endif

/* For SRV lookup */
#ifdef G_OS_WIN32
# include <windns.h>
#else
# include <resolv.h>
# include <arpa/nameser.h>
# include <arpa/nameser_compat.h>
#endif

#include <errno.h>
#include <string.h>

typedef struct _InfNameResolverEntry InfNameResolverEntry;
struct _InfNameResolverEntry {
  InfIpAddress* address;
  guint port;
};

typedef struct _InfNameResolverSRV InfNameResolverSRV;
struct _InfNameResolverSRV {
  guint priority;
  guint weight;
  guint port;
  gchar* address;
};

typedef struct _InfNameResolverResult InfNameResolverResult;
struct _InfNameResolverResult {
  /* The primarily resolved entries */
  InfNameResolverEntry* entries;
  guint n_entries;

  /* Not-yet-checked SRV records. If none of the above entries work, we can
   * look up these as a backup. */
  InfNameResolverSRV* srvs;
  guint n_srvs;

  GError* error;
};

typedef struct _InfNameResolverPrivate InfNameResolverPrivate;
struct _InfNameResolverPrivate {
  InfIo* io;
  gchar* hostname;
  gchar* service;
  gchar* srv;

  InfAsyncOperation* operation;

  /* Output */
  InfNameResolverResult result;
};

enum {
  PROP_0,

  PROP_IO,
  PROP_HOSTNAME,
  PROP_SERVICE,
  PROP_SRV
};

enum {
  RESOLVED,

  LAST_SIGNAL
};

#define INF_NAME_RESOLVER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TYPE_NAME_RESOLVER, InfNameResolverPrivate))

static guint name_resolver_signals[LAST_SIGNAL];

G_DEFINE_TYPE_WITH_CODE(InfNameResolver, inf_name_resolver, G_TYPE_OBJECT,
  G_ADD_PRIVATE(InfNameResolver))

static void
inf_name_resolver_result_nullify(InfNameResolverResult* result)
{
  result->entries = NULL;
  result->n_entries = 0;
  result->srvs = NULL;
  result->n_srvs = 0;
  result->error = NULL;
}

static void
inf_name_resolver_result_cleanup(InfNameResolverResult* result)
{
  guint i;

  for(i = 0; i < result->n_entries; ++i)
    inf_ip_address_free(result->entries[i].address);
  g_free(result->entries);

  for(i = 0; i < result->n_srvs; ++i)
    g_free(result->srvs[i].address);
  g_free(result->srvs);

  if(result->error != NULL)
    g_error_free(result->error);
}

static void
inf_name_resolver_result_free(gpointer result_ptr)
{
  InfNameResolverResult* result;

  result = (InfNameResolverResult*)result_ptr;

  inf_name_resolver_result_cleanup(result);
  g_slice_free(InfNameResolverResult, result);
}

/* Worker thread */

#ifndef G_OS_WIN32
static void
inf_name_resolver_set_herrno_error(GError** error,
                                   int code)
{
  g_set_error_literal(
    error,
    g_quark_from_static_string("INF_NAME_RESOLVER_HERRNO_ERROR"),
    code,
    hstrerror(code)
  );
}

static void
inf_name_resolver_set_errno_error(GError** error,
                                  int code)
{
  g_set_error_literal(
    error,
    g_quark_from_static_string("INF_NAME_RESOLVER_ERRNO_ERROR"),
    code,
    strerror(code)
  );
}

static void
inf_name_resolver_set_incomplete_error(GError** error)
{
  g_set_error_literal(
    error,
    g_quark_from_static_string("INF_NAME_RESOLVER_INCOMPLETE_ERROR"),
    0,
    _("The reply from the DNS server is incomplete")
  );
}

static const char*
inf_name_resolver_parse_dns_uint16(const char* msg,
                                   const char* end,
                                   const char* cur,
                                   guint16* result,
                                   GError** error)
{
  if(cur + 2 > end)
  {
    inf_name_resolver_set_incomplete_error(error);
    return NULL;
  }

  *result = ntohs(*((guint16*)cur));
  return cur + 2;
}

static const char*
inf_name_resolver_parse_dns_uint32(const char* msg,
                                   const char* end,
                                   const char* cur,
                                   guint32* result,
                                   GError** error)
{
  if(cur + 4 > end)
  {
    inf_name_resolver_set_incomplete_error(error);
    return NULL;
  }

  *result = ntohl(*((guint32*)cur));
  return cur + 4;
}

static const char*
inf_name_resolver_parse_dns_host(const char* msg,
                                 const char* end,
                                 const char* cur,
                                 gchar* result,
                                 gsize reslen,
                                 GError** error)
{
  int len;

  len = dn_expand(msg, end, cur, result, reslen);
  if(len < 0)
  {
    inf_name_resolver_set_errno_error(error, errno);
    return NULL;
  }

  if(cur + len > end)
  {
    inf_name_resolver_set_incomplete_error(error);
    return NULL;
  }

  return cur + len;
}

static const char*
inf_name_resolver_parse_srv_record(const char* msg,
                                   const char* end,
                                   const char* cur,
                                   InfNameResolverSRV* srv,
                                   GError** error)
{
  gchar buf[256];
  gsize bufsize;

  guint16 type;
  guint16 cls;
  guint32 ttl;
  guint16 msglen;
  guint16 prio;
  guint16 weight;
  guint16 port;

  bufsize = sizeof(buf);

  cur = inf_name_resolver_parse_dns_host(msg, end, cur, buf, bufsize, error);
  if(cur == NULL) return NULL;

  cur = inf_name_resolver_parse_dns_uint16(msg, end, cur, &type, error);
  if(cur == NULL) return NULL;

  cur = inf_name_resolver_parse_dns_uint16(msg, end, cur, &cls, error);
  if(cur == NULL) return NULL;

  cur = inf_name_resolver_parse_dns_uint32(msg, end, cur, &ttl, error);
  if(cur == NULL) return NULL;

  cur = inf_name_resolver_parse_dns_uint16(msg, end, cur, &msglen, error);
  if(cur == NULL) return NULL;

  if(type != T_SRV)
  {
    srv->address = NULL;
    cur += msglen;
    return cur;
  }

  cur = inf_name_resolver_parse_dns_uint16(msg, end, cur, &prio, error);
  if(cur == NULL) return NULL;

  cur = inf_name_resolver_parse_dns_uint16(msg, end, cur, &weight, error);
  if(cur == NULL) return NULL;

  cur = inf_name_resolver_parse_dns_uint16(msg, end, cur, &port, error);
  if(cur == NULL) return NULL;

  cur = inf_name_resolver_parse_dns_host(msg, end, cur, buf, bufsize, error);
  if(cur == NULL) return NULL;

  srv->priority = prio;
  srv->weight = weight;
  srv->port = port;
  srv->address = g_strdup(buf);
  return cur;
}
#endif

static gchar*
inf_name_resolver_choose_srv(InfNameResolverSRV** srvs,
                             guint* n_srvs,
                             guint* port)
{
  InfNameResolverSRV** low_prio_srvs;
  guint n_low_prio_srvs;
  guint total_weight;
  guint accum_weight;
  guint rand;
  guint i;
  guint index;

  gchar* selected_address;
  guint selected_port;

  g_assert(*n_srvs > 0);

  /* Choose the SRV records with lowest priority */
  low_prio_srvs = g_malloc(sizeof(InfNameResolverSRV*) * (*n_srvs));
  n_low_prio_srvs = 0;
  total_weight = 0;
  for(i = 0; i < *n_srvs; ++i)
  {
    if(n_low_prio_srvs == 0 ||
       (*srvs)[i].priority < low_prio_srvs[0]->priority)
    {
      n_low_prio_srvs = 1;
      low_prio_srvs[0] = &(*srvs)[i];
      total_weight = (*srvs)[i].weight;
    }
    else if( (*srvs)[i].priority == low_prio_srvs[0]->priority)
    {
      low_prio_srvs[n_low_prio_srvs++] = &(*srvs)[i];
      total_weight += (*srvs)[i].weight;
    }
  }

  /* Choose a server randomly, according to their weight */
  if(total_weight == 0)
  {
    /* No weight available, chose uniformly */
    i = g_random_int_range(0, n_low_prio_srvs);
  }
  else
  {
    rand = g_random_int_range(0, total_weight);
    accum_weight = 0;
    for(i = 0; i < n_low_prio_srvs; ++i)
    {
      accum_weight += low_prio_srvs[i]->weight;
      if(rand < accum_weight) break;
    }
  }

  g_assert(i < n_low_prio_srvs);

  /* Select the SRV record with index i */
  index = low_prio_srvs[i] - *srvs;
  selected_address = (*srvs)[index].address;
  selected_port = (*srvs)[index].port;
  g_free(low_prio_srvs);

  /* Remove the chosen SRV entry from the array, so that we don't try the
   * same record again if the selected one does not work. */
  (*srvs)[index] = (*srvs)[*n_srvs - 1];
  *srvs = g_realloc(*srvs, sizeof(InfNameResolverSRV) * (*n_srvs - 1));
  --*n_srvs;

  if(port != NULL) *port = selected_port;
  return selected_address;
}

static InfNameResolverSRV*
inf_name_resolver_lookup_srv(const gchar* query,
                             guint* n_srvs,
                             GError** error)
{
#ifdef G_OS_WIN32
  PDNS_RECORD data;
  PDNS_RECORD item;
  DNS_STATUS status;
  gchar* str;

  GArray* array;
  InfNameResolverSRV srv;

  status = DnsQuery_UTF8(
    query,
    DNS_TYPE_SRV,
    DNS_QUERY_STANDARD,
    NULL,
    &data,
    NULL
  );

  if(status != 0)
  {
    str = g_win32_error_message(status);

    g_set_error_literal(
      error,
      g_quark_from_static_string("WIN32_ERROR"),
      status,
      str
    );

    g_free(str);
    return NULL;
  }

  array = g_array_new(FALSE, FALSE, sizeof(InfNameResolverSRV));
  for(item = data; item != NULL; item = item->pNext)
  {
    if(item->wType != DNS_TYPE_SRV)
      continue;

    srv.priority = item->Data.SRV.wPriority;
    srv.weight = item->Data.SRV.wWeight;
    srv.port = item->Data.SRV.wPort;
    srv.address = g_strdup(item->Data.SRV.pNameTarget); // TODO: utf16_to_utf8?
    g_array_append_val(array, srv);
  }

  DnsRecordListFree(data, DnsFreeRecordListDeep);

  *n_srvs = array->len;
  return (InfNameResolverSRV*)g_array_free(array, FALSE);
#else
  char ansbuf[4096];
  char hostbuf[256];
  int len;

  HEADER* header;
  int query_count;
  int answer_count;

  const char* msg;
  const char* cur;
  const char* end;

  InfNameResolverSRV* srvs;
  guint n_answers;
  int i;

  /* libresolv uses a global struct for its operation, and is not threadsafe.
   * Therefore, we protect this function callby a mutex. */
  static GMutex mutex;

  /* Make the DNS query */
  g_mutex_lock(&mutex);

  *n_srvs = 0;
  h_errno = 0;
  len = res_query(query, C_IN, T_SRV, ansbuf, sizeof(ansbuf));

  if(h_errno != 0)
  {
    /* If the host was not found, simply return 0 SRV entries */
    if(h_errno != HOST_NOT_FOUND)
      inf_name_resolver_set_herrno_error(error, h_errno);
    g_mutex_unlock(&mutex);
    return NULL;
  }

  /* At this point we have the DNS answer, and the rest of the function
   * is thread-safe. */
  g_mutex_unlock(&mutex);

  if(len < sizeof(HEADER))
  {
    inf_name_resolver_set_incomplete_error(error);
    return NULL;
  }

  msg = ansbuf;
  end = ansbuf + len;
  cur = ansbuf + sizeof(HEADER);

  header = (HEADER*)msg;
  query_count = ntohs(header->qdcount);
  answer_count = ntohs(header->ancount);

  /* Skip over the query */
  for(i = 0; i < query_count; ++i)
  {
    len = dn_expand(msg, end, cur, hostbuf, sizeof(hostbuf));
    if(len < 0)
    {
      inf_name_resolver_set_errno_error(error, errno);
      return NULL;
    }

    if(cur + len + QFIXEDSZ > end)
    {
      inf_name_resolver_set_incomplete_error(error);
      return NULL;
    }

    cur += len + QFIXEDSZ;
  }

  /* Go over the answers */
  srvs = g_malloc(sizeof(InfNameResolverSRV) * answer_count);
  n_answers = 0;

  for(i = 0; i < answer_count; ++i)
  {
    cur = inf_name_resolver_parse_srv_record(
      msg,
      end,
      cur,
      &srvs[n_answers],
      error
    );

    if(cur == NULL)
    {
      for(i = 0; i < n_answers; ++i)
        g_free(srvs[i].address);
      g_free(srvs);
      return NULL;
    }

    if(srvs[n_answers].address != NULL)
      ++n_answers;
  }
  
  if(n_answers < answer_count)
    srvs = g_realloc(srvs, n_answers * sizeof(InfNameResolverSRV));

  *n_srvs = n_answers;
  return srvs;
#endif
}

static InfNameResolverEntry*
inf_name_resolver_lookup_a_aaaa(const gchar* hostname,
                                const gchar* service,
                                guint* n_entries,
                                GError** error)
{
  struct addrinfo hint;
  struct addrinfo* res;
  int err;

  GArray* array;
  InfNameResolverEntry entry;
  struct addrinfo* item;

#ifdef AI_ADDRCONFIG
  hint.ai_flags = AI_ADDRCONFIG;
#else
  hint.ai_flags = 0;
#endif
  hint.ai_family = AF_UNSPEC;
  hint.ai_socktype = SOCK_STREAM;
  hint.ai_protocol = 0;
  hint.ai_addrlen = 0;
  hint.ai_canonname = NULL;
  hint.ai_addr = NULL;
  hint.ai_next = NULL;

  res = NULL;
  err = getaddrinfo(hostname, service, &hint, &res);

  if(err != 0)
  {
    g_set_error_literal(
      error,
      g_quark_from_static_string("GETADDRINFO_ERROR"),
      err,
      gai_strerror(err)
    );

    *n_entries = 0;
    return NULL;
  }
  else
  {
    g_assert(res != NULL);
    array = g_array_new(FALSE, FALSE, sizeof(InfNameResolverEntry));

    for(item = res; item != NULL; item = item->ai_next)
    {
      switch(item->ai_family)
      {
      case AF_INET:
        entry.address = inf_ip_address_new_raw4(
          ((struct sockaddr_in*)item->ai_addr)->sin_addr.s_addr
        );

        entry.port = ntohs( ((struct sockaddr_in*)item->ai_addr)->sin_port);
        break;
      case AF_INET6:
        entry.address = inf_ip_address_new_raw6(
          ((struct sockaddr_in6*)item->ai_addr)->sin6_addr.s6_addr
        );

        entry.port = ntohs( ((struct sockaddr_in6*)item->ai_addr)->sin6_port);
        break;
      default:
        g_assert_not_reached();
        break;
      }

      g_array_append_val(array, entry);
    }

    freeaddrinfo(res);

    *n_entries = array->len;
    return (InfNameResolverEntry*)g_array_free(array, FALSE);
  }
}

static InfNameResolverEntry*
inf_name_resolver_resolve_srv(InfNameResolverSRV** srvs,
                              guint* n_srvs,
                              const gchar* service,
                              guint* n_entries,
                              GError** error)
{
  gchar* srvaddr;
  guint srvport;
  GError* local_error;
  InfNameResolverEntry* entries;
  guint i;

  g_assert(*n_srvs > 0);

  local_error = NULL;

  /* We have at least one SRV record. Choose one of the SRV records. */
  srvaddr = inf_name_resolver_choose_srv(srvs, n_srvs, &srvport);

  while(srvaddr != NULL)
  {
    /* Look it up */
    entries = inf_name_resolver_lookup_a_aaaa(
      srvaddr,
      service,
      n_entries,
      &local_error
    );

    g_free(srvaddr);
    srvaddr = NULL;

    /* If there was an error, try the next SRV record */
    if(entries == NULL)
    {
      if(*n_srvs > 0)
        srvaddr = inf_name_resolver_choose_srv(srvs, n_srvs, &srvport);

      /* Skip error if we have another record */
      if(srvaddr != NULL)
      {
        g_error_free(local_error);
        local_error = NULL;
      }
    }
    else
    {
      /* Fill in default port from SRV record */
      for(i = 0; i < *n_entries; ++i)
        if(entries[i].port == 0)
          entries[i].port = srvport;

      /* Remember the remaining SRV entries, in case connecting to the
       * chosen one does not work. */
      return entries;
    }
  }

  /* SRV records exhausted; report last error */
  g_assert(local_error != NULL);
  g_propagate_error(error, local_error);
  return NULL;
}

static InfNameResolverResult*
inf_name_resolver_resolve(const gchar* hostname,
                          const gchar* service,
                          const gchar* srv)
{
  InfNameResolverResult* result;
  gchar* query;
  GError* error;

  error = NULL;

  result = g_slice_new(InfNameResolverResult);
  inf_name_resolver_result_nullify(result);

  /* Look up a SRV record */
  if(srv != NULL)
  {
    query = g_strdup_printf("%s.%s", srv, hostname);

    result->srvs = inf_name_resolver_lookup_srv(
      query,
      &result->n_srvs,
      &error
    );

    g_free(query);

    if(error != NULL)
    {
      g_warning(
        _("Failure during SRV record lookup: %s\n"
          "Will go on with normal A/AAAA lookup"),
        error->message
      );

      g_error_free(error);
      error = NULL;
    }
    else if(result->n_srvs > 0)
    {
      result->entries = inf_name_resolver_resolve_srv(
        &result->srvs,
        &result->n_srvs,
        service,
        &result->n_entries,
        &result->error
      );

      /* If we failed to look up the SRV record, we do not attempt to look up
       * the original hostname as A/AAAA record -- we only do that if no SRV
       * records exist. */
      return result;
    }
  }

  /* If that did not yield a result, lookup A/AAAA record */
  result->entries = inf_name_resolver_lookup_a_aaaa(
    hostname,
    service,
    &result->n_entries,
    &result->error
  );

  return result;
}

static void
inf_name_resolver_run_func(gpointer* run_data,
                           GDestroyNotify* run_notify,
                           gpointer user_data)
{
  InfNameResolver* resolver;
  InfNameResolverPrivate* priv;

  gchar* hostname;
  gchar* service;
  gchar* srv;

  InfNameResolverResult* result;

  resolver = INF_NAME_RESOLVER(user_data);
  priv = INF_NAME_RESOLVER_PRIVATE(resolver);

  hostname = g_strdup(priv->hostname);
  service = g_strdup(priv->service);
  srv = g_strdup(priv->srv);
  g_object_unref(resolver);

  result = inf_name_resolver_resolve(hostname, service, srv);

  g_free(hostname);
  g_free(service);
  g_free(srv);

  *run_data = result;
  *run_notify = inf_name_resolver_result_free;
}

static void
inf_name_resolver_backup_run_func(gpointer* run_data,
                                  GDestroyNotify* run_notify,
                                  gpointer user_data)
{
  InfNameResolver* resolver;
  InfNameResolverPrivate* priv;
  InfNameResolverResult* result;
  gchar* service;

  resolver = INF_NAME_RESOLVER(user_data);
  priv = INF_NAME_RESOLVER_PRIVATE(resolver);

  result = g_slice_new(InfNameResolverResult);
  result->srvs = priv->result.srvs;
  result->n_srvs = priv->result.n_srvs;
  service = g_strdup(priv->service);

  priv->result.srvs = NULL;
  priv->result.n_srvs = 0;
  g_object_unref(resolver);

  g_assert(result->n_srvs > 0);

  result->error = NULL;

  result->entries = inf_name_resolver_resolve_srv(
    &result->srvs,
    &result->n_srvs,
    service,
    &result->n_entries,
    &result->error
  );

  g_free(service);

  *run_data = result;
  *run_notify = inf_name_resolver_result_free;
  
}

/* Main thread */

static void
inf_name_resolver_done_func(gpointer run_data,
                            gpointer user_data)
{
  InfNameResolver* resolver;
  InfNameResolverPrivate* priv;
  InfNameResolverResult* result;

  resolver = INF_NAME_RESOLVER(user_data);
  priv = INF_NAME_RESOLVER_PRIVATE(resolver);
  result = (InfNameResolverResult*)run_data;

  g_assert(priv->result.n_entries == 0);
  g_assert(priv->result.n_srvs == 0);
  g_assert(priv->result.error == NULL);

  priv->operation = NULL;
  priv->result = *result;

  /* Nullify this so that the destroy notify lets the data alive */
  inf_name_resolver_result_nullify(result);

  g_signal_emit(
    G_OBJECT(resolver),
    name_resolver_signals[RESOLVED],
    0,
    priv->result.error
  );
}

static void
inf_name_resolver_backup_done_func(gpointer run_data,
                                   gpointer user_data)
{
  InfNameResolver* resolver;
  InfNameResolverPrivate* priv;
  InfNameResolverResult* result;
  guint i;

  resolver = INF_NAME_RESOLVER(user_data);
  priv = INF_NAME_RESOLVER_PRIVATE(resolver);
  result = (InfNameResolverResult*)run_data;

  g_assert(priv->result.n_srvs == 0);
  g_assert(priv->result.error == NULL);

  /* Push the remaining SRV entries back for later lookup */
  priv->operation = NULL;
  priv->result.srvs = result->srvs;
  priv->result.n_srvs = result->n_srvs;
  priv->result.error = result->error;

  /* If there are new addresses, append them */
  if(result->n_entries > 0)
  {
    priv->result.entries = g_realloc(
      priv->result.entries,
      sizeof(InfNameResolverEntry) *
        (priv->result.n_entries + result->n_entries)
    );

    for(i = 0; i < result->n_entries; ++i)
      priv->result.entries[priv->result.n_entries + i] = result->entries[i];

    priv->result.n_entries += result->n_entries;
  }

  /* Nullify this so that the destroy notify lets the data alive */
  inf_name_resolver_result_nullify(result);

  g_signal_emit(
    G_OBJECT(resolver),
    name_resolver_signals[RESOLVED],
    0,
    priv->result.error
  );
}

static void
inf_name_resolver_init(InfNameResolver* resolver)
{
  InfNameResolverPrivate* priv;
  priv = INF_NAME_RESOLVER_PRIVATE(resolver);

  priv->io = NULL;
  priv->hostname = NULL;
  priv->service = NULL;
  priv->srv = NULL;

  priv->operation = NULL;

  inf_name_resolver_result_nullify(&priv->result);
}

static void
inf_name_resolver_dispose(GObject* object)
{
  InfNameResolver* resolver;
  InfNameResolverPrivate* priv;

  resolver = INF_NAME_RESOLVER(object);
  priv = INF_NAME_RESOLVER_PRIVATE(resolver);

  if(priv->operation != NULL)
  {
    inf_async_operation_free(priv->operation);
    priv->operation = NULL;
  }

  if(priv->io != NULL)
  {
    g_object_unref(G_OBJECT(priv->io));
    priv->io = NULL;
  }

  G_OBJECT_CLASS(inf_name_resolver_parent_class)->dispose(object);
}

static void
inf_name_resolver_finalize(GObject* object)
{
  InfNameResolver* resolver;
  InfNameResolverPrivate* priv;

  resolver = INF_NAME_RESOLVER(object);
  priv = INF_NAME_RESOLVER_PRIVATE(resolver);

  inf_name_resolver_result_cleanup(&priv->result);

  g_free(priv->hostname);
  g_free(priv->service);
  g_free(priv->srv);

  G_OBJECT_CLASS(inf_name_resolver_parent_class)->finalize(object);
}

static void
inf_name_resolver_set_property(GObject* object,
                               guint prop_id,
                               const GValue* value,
                               GParamSpec* pspec)
{
  InfNameResolver* connection;
  InfNameResolverPrivate* priv;

  connection = INF_NAME_RESOLVER(object);
  priv = INF_NAME_RESOLVER_PRIVATE(connection);

  switch(prop_id)
  {
  case PROP_IO:
    g_assert(priv->operation == NULL);

    if(priv->io != NULL) g_object_unref(priv->io);
    priv->io = INF_IO(g_value_dup_object(value));
    break;
  case PROP_HOSTNAME:
    g_assert(priv->operation == NULL);

    g_free(priv->hostname);
    priv->hostname = g_value_dup_string(value);
    break;
  case PROP_SERVICE:
    g_assert(priv->operation == NULL);

    g_free(priv->service);
    priv->service = g_value_dup_string(value);
    break;
  case PROP_SRV:
    g_assert(priv->operation == NULL);

    g_free(priv->srv);
    priv->srv = g_value_dup_string(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_name_resolver_get_property(GObject* object,
                               guint prop_id,
                               GValue* value,
                               GParamSpec* pspec)
{
  InfNameResolver* connection;
  InfNameResolverPrivate* priv;

  connection = INF_NAME_RESOLVER(object);
  priv = INF_NAME_RESOLVER_PRIVATE(connection);

  switch(prop_id)
  {
  case PROP_IO:
    g_value_set_object(value, G_OBJECT(priv->io));
    break;
  case PROP_HOSTNAME:
    g_value_set_string(value, priv->hostname);
    break;
  case PROP_SERVICE:
    g_value_set_string(value, priv->service);
    break;
  case PROP_SRV:
    g_value_set_string(value, priv->srv);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_name_resolver_class_init(InfNameResolverClass* name_resolver_class)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(name_resolver_class);

  object_class->dispose = inf_name_resolver_dispose;
  object_class->finalize = inf_name_resolver_finalize;
  object_class->set_property = inf_name_resolver_set_property;
  object_class->get_property = inf_name_resolver_get_property;

  name_resolver_class->resolved = NULL;

  g_object_class_install_property(
    object_class,
    PROP_IO,
    g_param_spec_object(
      "io",
      "IO",
      "I/O handler",
      INF_TYPE_IO,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_HOSTNAME,
    g_param_spec_string(
      "hostname",
      "Hostname",
      "The hostname to be looked up",
      NULL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_SERVICE,
    g_param_spec_string(
      "service",
      "Service",
      "The expected service at the remote endpoint",
      NULL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_SRV,
    g_param_spec_string(
      "srv",
      "SRV",
      "The SRV record to look up for the given hostname, e.g. _jabber._tcp",
      NULL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT
    )
  );

  /**
   * InfNameResolver::resolved:
   * @resolver: The #InfNameResolver that has resolved a hostname.
   * @error: A description of the error that occurred, or %NULL.
   *
   * This signal is emitted when the hostname has been resolved. The function
   * inf_name_resolver_get_address() and inf_name_resolver_get_port() can be
   * called to obtain the resolved addresses.
   */
  name_resolver_signals[RESOLVED] = g_signal_new(
    "resolved",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfNameResolverClass, resolved),
    NULL, NULL,
    g_cclosure_marshal_VOID__BOXED,
    G_TYPE_NONE,
    1,
    G_TYPE_ERROR
  );
}

/**
 * inf_name_resolver_new: (constructor)
 * @io: A #InfIo object used to schedule events in the main thread.
 * @hostname: The hostname to look up.
 * @service: (allow-none): The name of the service to look up, or %NULL.
 * @srv: (allow-none): The SRV record to look up, or %NULL.
 *
 * Creates a new #InfNameResolver. Use inf_name_resolver_start() to start
 * resolving the hostname.
 *
 * If @service is not %NULL, it should be a decimal port number or a
 * well-known service name that is translated into a port number, such as
 * &quot;http&quot. If @srv is not %NULL, the function will first attempt
 * a SRV lookup, and fall back to a regular A/AAAA lookup in case no SRV
 * record exists. Otherwise the SRV result is taken. If @service is
 * provided, it always overwrites the port number obtained from the hostname
 * lookup.
 *
 * The recommended procedure is to call this function with @service equal to
 * the port number entered by the user, or %NULL if the user did not provide
 * an explicit port number. If the resulting port number obtained with
 * inf_name_resolver_get_port() is then different from 0, then use that
 * port number, otherwise the default port number for the service.
 *
 * Returns: (transfer full): A new #InfNameResolver. Free with
 * g_object_unref().
 **/
InfNameResolver*
inf_name_resolver_new(InfIo* io,
                      const gchar* hostname,
                      const gchar* service,
                      const gchar* srv)
{
  InfNameResolver* resolver;

  g_return_val_if_fail(INF_IS_IO(io), NULL);

  resolver = INF_NAME_RESOLVER(
    g_object_new(
      INF_TYPE_NAME_RESOLVER,
      "io", io,
      "hostname", hostname,
      "service", service,
      "srv", srv,
      NULL
    )
  );

  return resolver;
}

/**
 * inf_name_resolver_get_hostname:
 * @resolver: A #InfNameResolver.
 *
 * Returns the currently configured hostname that @resolver will look up.
 *
 * Returns: (allow-none): The currently configured hostname.
 */
const gchar*
inf_name_resolver_get_hostname(InfNameResolver* resolver)
{
  g_return_val_if_fail(INF_IS_NAME_RESOLVER(resolver), NULL);
  return INF_NAME_RESOLVER_PRIVATE(resolver)->hostname;
}

/**
 * inf_name_resolver_get_service:
 * @resolver: A #InfNameResolver.
 *
 * Returns the currently configured service that @resolver will look up.
 *
 * Returns: (allow-none): The currently configured service.
 */
const gchar*
inf_name_resolver_get_service(InfNameResolver* resolver)
{
  g_return_val_if_fail(INF_IS_NAME_RESOLVER(resolver), NULL);
  return INF_NAME_RESOLVER_PRIVATE(resolver)->service;
}

/**
 * inf_name_resolver_get_srv:
 * @resolver: A #InfNameResolver.
 *
 * Returns the currently configured SRV record that @resolver will look up.
 *
 * Returns: (allow-none): The currently configured SRV record.
 */
const gchar*
inf_name_resolver_get_srv(InfNameResolver* resolver)
{
  g_return_val_if_fail(INF_IS_NAME_RESOLVER(resolver), NULL);
  return INF_NAME_RESOLVER_PRIVATE(resolver)->srv;
}

/**
 * inf_name_resolver_start:
 * @resolver: A #InfNameResolver.
 * @error: Location to store error information, if any.
 *
 * Starts the name resolution for the hostname configured with @resolver.
 *
 * When the hostname lookup has finished, the #InfNameResolver::resolved
 * signal is emitted. The function can only be called again once this signal
 * has been emitted. When this function is called, the previously looked up
 * result can no longer be obtained with the inf_name_resolver_get_address()
 * and inf_name_resolver_get_port() functions.
 *
 * Returns: %TRUE on success or %FALSE if a (synchronous) error occurred.
 */
gboolean
inf_name_resolver_start(InfNameResolver* resolver,
                        GError** error)
{
  InfNameResolverPrivate* priv;
  gboolean success;

  g_return_val_if_fail(INF_IS_NAME_RESOLVER(resolver), FALSE);
  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

  priv = INF_NAME_RESOLVER_PRIVATE(resolver);
  g_return_val_if_fail(priv->operation == NULL, FALSE);

  inf_name_resolver_result_cleanup(&priv->result);
  inf_name_resolver_result_nullify(&priv->result);

  priv->operation = inf_async_operation_new(
    priv->io,
    inf_name_resolver_run_func,
    inf_name_resolver_done_func,
    resolver
  );

  g_object_ref(resolver);
  success = inf_async_operation_start(priv->operation, error);

  if(success == FALSE)
  {
    g_object_unref(resolver);
    priv->operation = FALSE;
    return FALSE;
  }

  /* The launched thread takes over the reference on resolver */
  return TRUE;
}

/**
 * inf_name_resolver_lookup_backup:
 * @resolver: A #InfNameResolver.
 * @error: Location to store error information, if any, or %NULL.
 *
 * Tries to look up backup addresses for the configured hostname. This should
 * be used if connecting to none of the initially reported addresses works.
 * The function returns %FALSE if there are no backup addresses available, or
 * %TRUE otherwise. If it returns %TRUE, it the #InfNameResolver::resolved
 * signal will be emitted again, and when it is, more addresses might be
 * available from the resolver object.
 *
 * Returns: %TRUE if looking up backup addresses is attempted, or %FALSE
 * otherwise.
 */
gboolean
inf_name_resolver_lookup_backup(InfNameResolver* resolver,
                                GError** error)
{
  InfNameResolverPrivate* priv;
  gboolean success;

  g_return_val_if_fail(INF_IS_NAME_RESOLVER(resolver), FALSE);
  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

  priv = INF_NAME_RESOLVER_PRIVATE(resolver);
  g_return_val_if_fail(priv->operation == NULL, FALSE);

  if(priv->result.n_srvs == 0)
    return FALSE;

  if(priv->result.error != NULL)
  {
    g_error_free(priv->result.error);
    priv->result.error = NULL;
  }

  priv->operation = inf_async_operation_new(
    priv->io,
    inf_name_resolver_backup_run_func,
    inf_name_resolver_backup_done_func,
    resolver
  );

  g_object_ref(resolver);
  success = inf_async_operation_start(priv->operation, error);

  if(success == FALSE)
  {
    g_object_unref(resolver);
    priv->operation = FALSE;
    return FALSE;
  }

  /* The launched thread takes over the reference on resolver */
  return TRUE;
}

/**
 * inf_name_resolver_finished:
 * @resolver: A #InfNameResolver.
 *
 * Returns whether the resolver is currently looking up a hostname, or whether
 * it is ready to start a new lookup with inf_name_resolver_start().
 *
 * Returns: Whether a call to inf_name_resolver_start() can be made.
 */
gboolean
inf_name_resolver_finished(InfNameResolver* resolver)
{
  InfNameResolverPrivate* priv;

  g_return_val_if_fail(INF_IS_NAME_RESOLVER(resolver), FALSE);

  priv = INF_NAME_RESOLVER_PRIVATE(resolver);

  if(priv->operation != NULL)
    return FALSE;

  return TRUE;
}

/**
 * inf_name_resolver_get_n_addresses:
 * @resolver: A #InfNameResolver.
 *
 * Returns the number of resolved addresses that belongs to the hostname
 * that was looked up.
 *
 * Returns: The number of addresses available.
 */
guint
inf_name_resolver_get_n_addresses(InfNameResolver* resolver)
{
  g_return_val_if_fail(INF_IS_NAME_RESOLVER(resolver), 0);
  return INF_NAME_RESOLVER_PRIVATE(resolver)->result.n_entries;
}

/**
 * inf_name_resolver_get_address:
 * @resolver: A #InfNameResolver.
 * @index: The index for which to return the address, in case multiple
 * IP addresses correspond to the same hostname.
 *
 * Returns the @index<!-- -->th address looked up in the last hostname
 * resolution operation.
 *
 * Returns: (transfer none): The looked up #InfIpAddress.
 */
const InfIpAddress*
inf_name_resolver_get_address(InfNameResolver* resolver,
                              guint index)
{
  InfNameResolverPrivate* priv;

  g_return_val_if_fail(INF_IS_NAME_RESOLVER(resolver), NULL);

  priv = INF_NAME_RESOLVER_PRIVATE(resolver);

  g_return_val_if_fail(index < priv->result.n_entries, NULL);
  return priv->result.entries[index].address;
}

/**
 * inf_name_resolver_get_port:
 * @resolver: A #InfNameResolver.
 * @index: The index for which to return the port, in case multiple
 * IP addresses correspond to the same hostname.
 *
 * Returns the @index<!-- -->th port looked up in the last hostname
 * resolution operation. This can be 0 if the @service parameter in
 * inf_name_resolver_start() has been set to %NULL and no SRV record has
 * been found.
 *
 * Returns: The looked up #InfIpAddress.
 */
guint
inf_name_resolver_get_port(InfNameResolver* resolver,
                           guint index)
{
  InfNameResolverPrivate* priv;

  g_return_val_if_fail(INF_IS_NAME_RESOLVER(resolver), 0);

  priv = INF_NAME_RESOLVER_PRIVATE(resolver);

  g_return_val_if_fail(index < priv->result.n_entries, 0);
  return priv->result.entries[index].port;
}

/* vim:set et sw=2 ts=2: */
