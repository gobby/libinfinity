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
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <libinfinity/common/inf-standalone-io.h>
#include <libinfinity/common/inf-io.h>

#ifdef G_OS_WIN32
# include <winsock2.h>
#else
# include <poll.h>
# include <errno.h>
#endif /* !G_OS_WIN32 */

#include <string.h>

#ifdef G_OS_WIN32
typedef WSAEVENT InfStandaloneIoNativeEvent;
typedef DWORD InfStandaloneIoPollTimeout;
typedef DWORD InfStandaloneIoPollResult;
static const InfStandaloneIoPollResult INF_STANDALONE_IO_POLL_TIMEOUT =
  WSA_WAIT_TIMEOUT;
static const InfStandaloneIoPollTimeout INF_STANDALONE_IO_POLL_INFINITE =
  WSA_INFINITE;
#define inf_standalone_io_poll(events, num_events, timeout) \
  ((num_events) == 0 ? \
    (Sleep(timeout), WSA_WAIT_TIMEOUT) : \
    (WSAWaitForMultipleEvents(num_events, events, FALSE, timeout, TRUE)))
#else
typedef struct pollfd InfStandaloneIoNativeEvent;
typedef int InfStandaloneIoPollTimeout;
typedef int InfStandaloneIoPollResult;
static const InfStandaloneIoPollResult INF_STANDALONE_IO_POLL_TIMEOUT = 0;
static const InfStandaloneIoPollTimeout INF_STANDALONE_IO_POLL_INFINITE = -1;
#define inf_standalone_io_poll(events, num_events, timeout) \
  (poll(events, (nfds_t)num_events, timeout))
#endif

typedef struct _InfStandaloneIoWatch InfStandaloneIoWatch;
struct _InfStandaloneIoWatch {
  InfStandaloneIoNativeEvent* event;
  InfNativeSocket* socket;
  InfIoFunc func;
  gpointer user_data;
  GDestroyNotify notify;
};

typedef struct _InfStandaloneIoTimeout InfStandaloneIoTimeout;
struct _InfStandaloneIoTimeout {
  GTimeVal begin;
  guint msecs;
  InfIoTimeoutFunc func;
  gpointer user_data;
  GDestroyNotify notify;
};

typedef struct _InfStandaloneIoPrivate InfStandaloneIoPrivate;
struct _InfStandaloneIoPrivate {
  InfStandaloneIoNativeEvent* events;
  InfStandaloneIoWatch* watches;

  guint fd_size;
  guint fd_alloc;

  GList* timeouts;

  gboolean loop_running;
};

#ifdef G_OS_WIN32
/* Mapping between WSAEventSelect's FD_ flags and libinfinity's
 * INF_IO flags */
typedef struct _InfStandaloneIoEventTableEntry InfStandaloneIoEventTableEntry;
struct _InfStandaloneIoEventTableEntry {
  guint flag_val;
  guint flag_bit;
  guint io_val;
};

static const InfStandaloneIoEventTableEntry inf_standalone_io_event_table[] =
  {
    { FD_READ,    FD_READ_BIT,    INF_IO_INCOMING },
    { FD_CLOSE,   FD_CLOSE_BIT,   INF_IO_INCOMING },
    { FD_ACCEPT,  FD_ACCEPT_BIT,  INF_IO_INCOMING },
    { FD_WRITE,   FD_WRITE_BIT,   INF_IO_OUTGOING },
    { FD_CONNECT, FD_CONNECT_BIT, INF_IO_OUTGOING }
  };
#endif

#define INF_STANDALONE_IO_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TYPE_STANDALONE_IO, InfStandaloneIoPrivate))

static GObjectClass* parent_class;

static guint
inf_standalone_io_timeval_diff(GTimeVal* first,
                               GTimeVal* second)
{
  g_assert(first->tv_sec > second->tv_sec ||
           (first->tv_sec == second->tv_sec &&
            first->tv_usec >= second->tv_usec));

  /* Don't risk overflow, don't need to convert to signed int */
  return (first->tv_sec - second->tv_sec) * 1000 +
         (first->tv_usec+500)/1000 - (second->tv_usec+500)/1000;
}

static void
inf_standalone_io_iteration_impl(InfStandaloneIo* io,
                                 InfStandaloneIoPollTimeout timeout)
{
  InfStandaloneIoPrivate* priv;
  InfIoEvent events;
  InfStandaloneIoPollResult result;
  guint i;

  GList* item;
  GTimeVal current;
  InfStandaloneIoTimeout* cur_timeout;
  GList* next_timeout;
  guint elapsed;

#ifdef G_OS_WIN32
  gchar* error_message;
  InfStandaloneIoWatch* watch;
  WSANETWORKEVENTS wsa_events;
  const InfStandaloneIoEventTableEntry* entry;
#endif

  priv = INF_STANDALONE_IO_PRIVATE(io);

  g_get_current_time(&current);
  next_timeout = NULL;

  for(item = priv->timeouts; item != NULL; item = g_list_next(item))
  {
    cur_timeout = item->data;
    elapsed = inf_standalone_io_timeval_diff(&current, &cur_timeout->begin);

    if(elapsed >= cur_timeout->msecs)
    {
      /* already elapsed */
      /* TODO: Don't even poll */
      timeout = 0;
      next_timeout = item;

      /* no need to check other timeouts */
      break;
    }
    else
    {
      if(timeout == INF_STANDALONE_IO_POLL_INFINITE ||
         cur_timeout->msecs - elapsed > (guint)timeout)
      {
        next_timeout = item;
        timeout = cur_timeout->msecs - elapsed;
      }
    }
  }

  result = inf_standalone_io_poll(priv->events, priv->fd_size, timeout);

#ifdef G_OS_WIN32
  switch(result)
  {
  case WSA_WAIT_FAILED:
    error_message = g_win32_error_message(WSAGetLastError());
    g_warning("WSAWaitForMultipleEvents() failed: %s\n", error_message);
    g_free(error_message);
    return;
  case WSA_WAIT_IO_COMPLETION:
    return;
  default:
    break;
  }
#else
  if(result == -1)
  {
    if(errno != EINTR)
      g_warning("poll() failed: %s\n", strerror(errno));

    return;
  }
#endif

  g_object_ref(io);

  if(next_timeout != NULL && result == INF_STANDALONE_IO_POLL_TIMEOUT)
  {
    /* No file descriptor is active, but a timeout elapsed */
    cur_timeout = next_timeout->data;
    priv->timeouts = g_list_delete_link(priv->timeouts, next_timeout);

    cur_timeout->func(cur_timeout->user_data);
    if(cur_timeout->notify)
      cur_timeout->notify(cur_timeout->user_data);
    g_slice_free(InfStandaloneIoTimeout, cur_timeout);
  }
#ifdef G_OS_WIN32
  else if(result >= WSA_WAIT_EVENT_0 &&
          result < WSA_WAIT_EVENT_0 + priv->fd_size)
  {
    watch = &priv->watches[result - WSA_WAIT_EVENT_0];
    if(WSAEnumNetworkEvents(*watch->socket, *watch->event, &wsa_events) ==
       SOCKET_ERROR)
    {
      error_message = g_win32_error_message(WSAGetLastError());
      g_warning("WSAEnumNetworkEvents failed: %s\n", error_message);
      g_free(error_message);

      events = INF_IO_ERROR;
    }
    else
    {
      events = 0;
      for(i = 0; i < G_N_ELEMENTS(inf_standalone_io_event_table); ++ i)
      {
        entry = &inf_standalone_io_event_table[i];
        if(wsa_events.lNetworkEvents & entry->flag_val)
        {
          events |= entry->io_val;
          if(wsa_events.iErrorCode[entry->flag_bit])
            events |= INF_IO_ERROR;
        }
      }
    }

    watch->func(watch->socket, events, watch->user_data);
  }
#else
  else if(result > 0)
  {
    while(result--)
    {
      for(i = 0; i < priv->fd_size; ++ i)
      {
        if(priv->events[i].revents != 0)
        {
          events = 0;
          if(priv->events[i].revents & POLLIN)
            events |= INF_IO_INCOMING;
          if(priv->events[i].revents & POLLOUT)
            events |= INF_IO_OUTGOING;
          /* We treat POLLPRI as error because it should not occur in
           * infinote. */
          if(priv->events[i].revents & (POLLERR | POLLPRI | POLLHUP | POLLNVAL))
            events |= INF_IO_ERROR;
          
          priv->events[i].revents = 0;

          priv->watches[i].func(
            priv->watches[i].socket,
            events,
            priv->watches[i].user_data
          );

          /* The callback might have done everything, including completly
           * screwing up the array of file descriptors. This is why we break
           * here and iterate from the beginning to find the next event. */
          break;
        }
      }
    }
  }
#endif

  g_object_unref(io);
}

static void
inf_standalone_io_init(GTypeInstance* instance,
                       gpointer g_class)
{
  InfStandaloneIo* io;
  InfStandaloneIoPrivate* priv;

  io = INF_STANDALONE_IO(instance);
  priv = INF_STANDALONE_IO_PRIVATE(io);

  priv->fd_size = 0;
  priv->fd_alloc = 4;

  priv->events =
    g_malloc(sizeof(InfStandaloneIoNativeEvent) * priv->fd_alloc);
  priv->watches = g_malloc(sizeof(InfStandaloneIoWatch) * priv->fd_alloc);

  priv->loop_running = FALSE;
}

static void
inf_standalone_io_finalize(GObject* object)
{
  InfStandaloneIo* io;
  InfStandaloneIoPrivate* priv;
  guint i;
  GList* item;
  InfStandaloneIoTimeout* timeout;

  io = INF_STANDALONE_IO(object);
  priv = INF_STANDALONE_IO_PRIVATE(io);

#ifdef G_OS_WIN32
  for(i = 0; i < priv->fd_size; ++ i)
  {
    WSAEventSelect(*priv->watches[i].socket, priv->events[i], 0);
    WSACloseEvent(priv->events[i]);
  }
#endif

  for(i = 0; i < priv->fd_size; ++ i)
    if(priv->watches[i].notify)
      priv->watches[i].notify(priv->watches[i].user_data);

  for(item = priv->timeouts; item != NULL; item = g_list_next(item))
  {
    timeout = (InfStandaloneIoTimeout*)item->data;
    if(timeout->notify)
      timeout->notify(timeout->user_data);
    g_slice_free(InfStandaloneIoTimeout, timeout);
  }

  g_free(priv->events);
  g_free(priv->watches);
  g_list_free(priv->timeouts);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_standalone_io_io_watch(InfIo* io,
                           InfNativeSocket* socket,
                           InfIoEvent events,
                           InfIoFunc func,
                           gpointer user_data,
                           GDestroyNotify notify)
{
  InfStandaloneIoPrivate* priv;
  long pevents;
  guint i;

#ifdef G_OS_WIN32
  gchar* error_message;
#endif

  priv = INF_STANDALONE_IO_PRIVATE(io);

#ifdef G_OS_WIN32
  pevents = 0;
  if(events & INF_IO_INCOMING)
    pevents |= (FD_READ | FD_ACCEPT | FD_CLOSE);
  if(events & INF_IO_OUTGOING)
    pevents |= (FD_WRITE | FD_CONNECT);
#else
  pevents = 0;
  if(events & INF_IO_INCOMING)
    pevents |= POLLIN;
  if(events & INF_IO_OUTGOING)
    pevents |= POLLOUT;
  if(events & INF_IO_ERROR)
    pevents |= (POLLERR | POLLHUP | POLLNVAL | POLLPRI);
#endif

  for(i = 0; i < priv->fd_size; ++ i)
  {
    if(priv->watches[i].socket == socket)
    {
      if(events == 0)
      {
#ifdef G_OS_WIN32
        WSAEventSelect(*priv->watches[i].socket, priv->events[i], 0);
        WSACloseEvent(priv->events[i]);
#endif
        /* Free user_data */
        if(priv->watches[i].notify)
          priv->watches[i].notify(priv->watches[i].user_data);

        /* Remove watch by replacing it by the last pollfd/watch */
        if(i != priv->fd_size - 1)
        {
          memcpy(
            &priv->events[i],
            &priv->events[priv->fd_size - 1],
            sizeof(InfStandaloneIoNativeEvent)
          );

          memcpy(
            &priv->watches[i],
            &priv->watches[priv->fd_size - 1],
            sizeof(InfStandaloneIoWatch)
          );

          priv->watches[i].event = &priv->events[i];
        }

        -- priv->fd_size;
      }
      else
      {
        /* Free userdata before update */
        if(priv->watches[i].notify)
          priv->watches[i].notify(priv->watches[i].user_data);

        /* Update */
#ifdef G_OS_WIN32
        WSAEventSelect(*priv->watches[i].socket, priv->events[i], pevents);
#else
        priv->events[i].events = pevents;
#endif

        priv->watches[i].func = func;
        priv->watches[i].user_data = user_data;
        priv->watches[i].notify = notify;
      }

      return;
    }
  }

  /* Socket is not already present, so create new watch */
  if(priv->fd_size == priv->fd_alloc)
  {
    priv->fd_alloc += 4;

    priv->events = g_realloc(
      priv->events,
      priv->fd_alloc * sizeof(InfStandaloneIoNativeEvent)
    );

    priv->watches = g_realloc(
      priv->watches,
      priv->fd_alloc * sizeof(InfStandaloneIoWatch)
    );

    /* Update event pointers, the location of the events in memory might have
     * changed after realloc. */
    for(i = 0; i < priv->fd_size; ++i)
      priv->watches[i].event = &priv->events[i];
  }

#ifdef G_OS_WIN32
  priv->events[priv->fd_size] = WSACreateEvent();
  if(priv->events[priv->fd_size] == WSA_INVALID_EVENT)
  {
    error_message = g_win32_error_message(WSAGetLastError());
    g_warning("WSACreateEvent() failed: %s", error_message);
    g_free(error_message);

    return;
  }

  WSAEventSelect(*socket, priv->events[priv->fd_size], pevents);
#else
  priv->events[priv->fd_size].fd = *socket;
  priv->events[priv->fd_size].events = pevents;
  priv->events[priv->fd_size].revents = 0;
#endif

  priv->watches[priv->fd_size].event = &priv->events[priv->fd_size];
  priv->watches[priv->fd_size].socket = socket;
  priv->watches[priv->fd_size].func = func;
  priv->watches[priv->fd_size].user_data = user_data;
  priv->watches[priv->fd_size].notify = notify;

  ++ priv->fd_size;
}

static gpointer
inf_standalone_io_io_add_timeout(InfIo* io,
                                 guint msecs,
                                 InfIoTimeoutFunc func,
                                 gpointer user_data,
                                 GDestroyNotify notify)
{
  InfStandaloneIoPrivate* priv;
  InfStandaloneIoTimeout* timeout;

  priv = INF_STANDALONE_IO_PRIVATE(io);
  timeout = g_slice_new(InfStandaloneIoTimeout);

  g_get_current_time(&timeout->begin);
  timeout->msecs = msecs;
  timeout->func = func;
  timeout->user_data = user_data;
  timeout->notify = notify;
  priv->timeouts = g_list_prepend(priv->timeouts, timeout);

  return timeout;
}

static void
inf_standalone_io_io_remove_timeout(InfIo* io,
                                    gpointer timeout_handle)
{
  InfStandaloneIoPrivate* priv;
  InfStandaloneIoTimeout* timeout;
  GList* item;

  priv = INF_STANDALONE_IO_PRIVATE(io);
  item = g_list_find(priv->timeouts, timeout_handle);
  g_assert(item != NULL);

  timeout = item->data;
  priv->timeouts = g_list_delete_link(priv->timeouts, item);

  if(timeout->notify)
    timeout->notify(timeout->user_data);

  g_slice_free(InfStandaloneIoTimeout, timeout);
}

static void
inf_standalone_io_class_init(gpointer g_class,
                             gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfStandaloneIoPrivate));

  object_class->finalize = inf_standalone_io_finalize;
}

static void
inf_standalone_io_io_init(gpointer g_iface,
                          gpointer iface_data)
{
  InfIoIface* iface;
  iface = (InfIoIface*)g_iface;

  iface->watch = inf_standalone_io_io_watch;
  iface->add_timeout = inf_standalone_io_io_add_timeout;
  iface->remove_timeout = inf_standalone_io_io_remove_timeout;
}

GType
inf_standalone_io_get_type(void)
{
  static GType standalone_io_type = 0;

  if(!standalone_io_type)
  {
    static const GTypeInfo standalone_io_type_info = {
      sizeof(InfStandaloneIoClass),   /* class_size */
      NULL,                           /* base_init */
      NULL,                           /* base_finalize */
      inf_standalone_io_class_init,   /* class_init */
      NULL,                           /* class_finalize */
      NULL,                           /* class_data */
      sizeof(InfStandaloneIo),        /* instance_size */
      0,                              /* n_preallocs */
      inf_standalone_io_init,         /* instance_init */
      NULL                            /* value_table */
    };

    static const GInterfaceInfo io_info = {
      inf_standalone_io_io_init,
      NULL,
      NULL
    };

    standalone_io_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfStandaloneIo",
      &standalone_io_type_info,
      0
    );

    g_type_add_interface_static(
      standalone_io_type,
      INF_TYPE_IO,
      &io_info
    );
  }

  return standalone_io_type;
}

/**
 * inf_standalone_io_new:
 *
 * Creates a new #InfStandaloneIo.
 *
 * Returns: A new #InfStandaloneIo. Free with g_object_unref() when no longer
 * needed.
 **/
InfStandaloneIo*
inf_standalone_io_new(void)
{
  GObject* object;
  object = g_object_new(INF_TYPE_STANDALONE_IO, NULL);
  return INF_STANDALONE_IO(object);
}

/**
 * inf_standalone_io_iteration:
 * @io: A #InfStandaloneIo.
 *
 * Performs a single iteration of @io. The call will block until a first
 * event has occured. Then, it will process that event and return.
 **/
void
inf_standalone_io_iteration(InfStandaloneIo* io)
{
  g_return_if_fail(INF_IS_STANDALONE_IO(io));
  inf_standalone_io_iteration_impl(io, INF_STANDALONE_IO_POLL_INFINITE);
}

/**
 * inf_standalone_io_iteration_timeout:
 * @io: A #InfStandaloneIo.
 * @timeout: Maximum number of milliseconds to block.
 *
 * Performs a single iteration of @io. The call will block until either an
 * event occured or @timeout milliseconds have elapsed. If an event occured,
 * the event will be processed before returning.
 **/
void
inf_standalone_io_iteration_timeout(InfStandaloneIo* io,
                                    guint timeout)
{
  g_return_if_fail(INF_IS_STANDALONE_IO(io));
  inf_standalone_io_iteration_impl(io, (int)timeout);
}

/**
 * inf_standalone_io_loop:
 * @io: A #InfStandaloneIo.
 *
 * This call will cause @io to wait for events and process them, but not
 * return until inf_standalone_io_loop_quit() is called.
 **/
void
inf_standalone_io_loop(InfStandaloneIo* io)
{
  InfStandaloneIoPrivate* priv;

  g_return_if_fail(INF_IS_STANDALONE_IO(io));
  priv = INF_STANDALONE_IO_PRIVATE(io);

  g_return_if_fail(priv->loop_running == FALSE);
  priv->loop_running = TRUE;

  while(priv->loop_running == TRUE)
    inf_standalone_io_iteration_impl(io, -1);
}

/**
 * inf_standalone_io_loop_quit:
 * @io: A #InfStandaloneIo.
 *
 * Exits a loop in which @io is running through a call to
 * inf_standalone_io_loop().
 **/
void
inf_standalone_io_loop_quit(InfStandaloneIo* io)
{
  InfStandaloneIoPrivate* priv;

  g_return_if_fail(INF_IS_STANDALONE_IO(io));
  priv = INF_STANDALONE_IO_PRIVATE(io);

  g_return_if_fail(priv->loop_running == TRUE);
  priv->loop_running = FALSE;
}

/**
 * inf_standalone_io_loop_running:
 * @io: A #InfStandaloneIo.
 *
 * Returns whether @io runs currently in a loop initiated with
 * inf_standalone_io_loop().
 *
 * Return Value: Whether @io runs in a loop.
 **/
gboolean
inf_standalone_io_loop_running(InfStandaloneIo* io)
{
  InfStandaloneIoPrivate* priv;

  g_return_val_if_fail(INF_IS_STANDALONE_IO(io), FALSE);
  priv = INF_STANDALONE_IO_PRIVATE(io);

  return priv->loop_running;
}

/* vim:set et sw=2 ts=2: */
