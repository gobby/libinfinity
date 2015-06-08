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
 * SECTION:inf-standalone-io
 * @title: InfStandaloneIo
 * @short_description: Simple event loop implementation
 * @include: libinfinity/common/inf-standalone-io.h
 * @see_also: #InfIo
 * @stability: Unstable
 *
 * #InfStandaloneIo is a simple implementation of the #InfIo interface. It
 * implements a basic application event loop with support for listening on
 * sockets, scheduling timeouts and inter-thread notifications. The class
 * is fully thread-safe.
 *
 * This class can be perfectly used for all functions in libinfinity that
 * require a #InfIo object to wait for events. If, on top of that more
 * functionality is required, or the main loop needs to be integrated with
 * another library such as a UI toolkit, a custom class should be created
 * instead which implements the #InfIo interface. For the GTK+ toolkit, there
 * is #InfGtkIo in the libinfgtk library, to integrate with the Glib main
 * loop.
 */

#include <libinfinity/common/inf-standalone-io.h>
#include <libinfinity/common/inf-io.h>

/* TODO: Modularize the FD handling, then add epoll support */

#ifdef G_OS_WIN32
# include <winsock2.h>
#else
# include <poll.h>
# include <errno.h>
# include <unistd.h>
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

struct _InfIoWatch {
  /* TODO: Do we actually need this? We can access the event by
   * priv->events[watchindex+1]. */
  InfStandaloneIoNativeEvent* event;

  InfNativeSocket* socket;
  InfIoWatchFunc func;
  gpointer user_data;
  GDestroyNotify notify;

  /* Protection flags to avoid freeing the watch object when running
   * the callback */
  gboolean executing;
  gboolean disposed;
};

struct _InfIoTimeout {
  GTimeVal begin;
  guint msecs;
  InfIoTimeoutFunc func;
  gpointer user_data;
  GDestroyNotify notify;
};

struct _InfIoDispatch {
  InfIoDispatchFunc func;
  gpointer user_data;
  GDestroyNotify notify;
};

typedef struct _InfStandaloneIoPrivate InfStandaloneIoPrivate;
struct _InfStandaloneIoPrivate {
  InfStandaloneIoNativeEvent* events;
  GMutex mutex;

  guint fd_size;
  guint fd_alloc;

  /* this array has fd_size-1 entries and fd_alloc-1 allocations: */
  InfIoWatch** watches;

  GList* timeouts;
  GList* dispatchs;

#ifndef G_OS_WIN32
  int wakeup_pipe[2];
#endif

  gboolean polling;
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

static void inf_standalone_io_io_iface_init(InfIoInterface* iface);
G_DEFINE_TYPE_WITH_CODE(InfStandaloneIo, inf_standalone_io, G_TYPE_OBJECT,
  G_ADD_PRIVATE(InfStandaloneIo)
  G_IMPLEMENT_INTERFACE(INF_TYPE_IO, inf_standalone_io_io_iface_init))

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

/* Run one iteration of the main loop. Call this only with the mutex locked
 * and a local reference added to io. */
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
  InfIoWatch* watch;
  InfIoTimeout* cur_timeout;
  InfIoDispatch* dispatch;
  guint elapsed;

#ifdef G_OS_WIN32
  gchar* error_message;
  WSANETWORKEVENTS wsa_events;
  const InfStandaloneIoEventTableEntry* entry;
#else
  ssize_t ret;
  char buf[1];
#endif

  priv = INF_STANDALONE_IO_PRIVATE(io);

  /* Find number of milliseconds to wait */
  if(priv->dispatchs != NULL)
  {
    /* TODO: Don't even poll */
    timeout = 0;
  }
  else
  {
    g_get_current_time(&current);
    for(item = priv->timeouts; item != NULL; item = g_list_next(item))
    {
      cur_timeout = (InfIoTimeout*)item->data;
      elapsed = inf_standalone_io_timeval_diff(&current, &cur_timeout->begin);

      if(elapsed >= cur_timeout->msecs)
      {
        /* already elapsed */
        /* TODO: Don't even poll */
        timeout = 0;
        /* no need to check other timeouts */
        break;
      }
      else
      {
        if(timeout == INF_STANDALONE_IO_POLL_INFINITE ||
           cur_timeout->msecs - elapsed < (guint)timeout)
        {
          timeout = cur_timeout->msecs - elapsed;
        }
      }
    }
  }

  priv->polling = TRUE;
  g_mutex_unlock(&priv->mutex);

  result = inf_standalone_io_poll(priv->events, priv->fd_size, timeout);

  g_mutex_lock(&priv->mutex);
  priv->polling = FALSE;

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

  if(result == INF_STANDALONE_IO_POLL_TIMEOUT)
  {
    /* No file descriptor is active, so check whether a timeout elapsed */
    g_get_current_time(&current);
    for(item = priv->timeouts; item != NULL; item = g_list_next(item))
    {
      cur_timeout = (InfIoTimeout*)item->data;
      elapsed = inf_standalone_io_timeval_diff(&current, &cur_timeout->begin);
      if(elapsed >= cur_timeout->msecs)
      {
        priv->timeouts = g_list_delete_link(priv->timeouts, item);
        g_mutex_unlock(&priv->mutex);

        cur_timeout->func(cur_timeout->user_data);
        if(cur_timeout->notify)
          cur_timeout->notify(cur_timeout->user_data);
        g_slice_free(InfIoTimeout, cur_timeout);

        g_mutex_lock(&priv->mutex);
        return;
      }
    }
  }
#ifdef G_OS_WIN32
  else if(result >= WSA_WAIT_EVENT_0 &&
          result < WSA_WAIT_EVENT_0 + priv->fd_size)
  {
    if(result == WSA_WAIT_EVENT_0)
    {
      /* wakeup call */
      WSAResetEvent(priv->events[0]);
    }
    else
    {
      watch = priv->watches[result - WSA_WAIT_EVENT_0 - 1];

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

      /* protect from removing the watch object via
       * inf_io_remove_watch() when running the callback. */
      watch->executing = TRUE;
      g_mutex_unlock(&priv->mutex);

      watch->func(watch->socket, events, watch->user_data);

      g_mutex_lock(&priv->mutex);
      watch->executing = FALSE;
      if(watch->disposed == TRUE)
      {
        g_mutex_unlock(&priv->mutex);
        if(watch->notify) watch->notify(watch->user_data);
        g_slice_free(InfIoWatch, watch);
        g_mutex_lock(&priv->mutex);
      }

      return;
    }
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

          if(i == 0)
          {
            /* wakeup call */

            /* we were not polling for outgoing */
            g_assert(~events & INF_IO_OUTGOING);
            if(events & INF_IO_ERROR)
            {
              /* TODO: Read error from FD? */
              g_warning("Error condition on wakeup pipe");
              /* TODO: Is there anything we could do here?
               * Try to re-establish pipe? */
            }
            else
            {
              ret = read(priv->events[0].fd, &buf, 1);
              if(ret == -1)
              {
                g_warning(
                  "read() on wakeup pipe failed: %s",
                  strerror(errno)
                );

                /* TODO: Is there anything we could do here?
                 * Try to re-establish pipe? */
              }
              else if(ret == 0)
              {
                g_warning("Wakeup pipe received EOF");
                /* TODO: Is there anything we could do here?
                 * Try to re-establish pipe? */
              }
              else
              {
                /* this is what we send as wakeup call */
                g_assert(buf[0] == 'c');
              }
            }
          }
          else
          {
            watch = priv->watches[i-1];

            /* protect from removing the watch object via
             * inf_io_remove_watch() when running the callback. */
            watch->executing = TRUE;
            g_mutex_unlock(&priv->mutex);

            watch->func(watch->socket, events, watch->user_data);

            g_mutex_lock(&priv->mutex);
            watch->executing = FALSE;
            if(watch->disposed == TRUE)
            {
              g_mutex_unlock(&priv->mutex);
              if(watch->notify) watch->notify(watch->user_data);
              g_slice_free(InfIoWatch, watch);
              g_mutex_lock(&priv->mutex);
            }

            return;
          }
        }
      }
    }
  }
#endif

  /* neither timeout nor IO fired, so try a dispatched message */
  if(priv->dispatchs != NULL)
  {
    dispatch = (InfIoDispatch*)priv->dispatchs->data;
    priv->dispatchs = g_list_delete_link(priv->dispatchs, priv->dispatchs);
    g_mutex_unlock(&priv->mutex);

    dispatch->func(dispatch->user_data);
    if(dispatch->notify)
      dispatch->notify(dispatch->user_data);
    g_slice_free(InfIoDispatch, dispatch);

    g_mutex_lock(&priv->mutex);
  }
}

static void
inf_standalone_io_init(InfStandaloneIo* io)
{
  InfStandaloneIoPrivate* priv;

#ifdef G_OS_WIN32
  gchar* error_message;
#endif

  priv = INF_STANDALONE_IO_PRIVATE(io);

  g_mutex_init(&priv->mutex);

  priv->fd_size = 0;
  priv->fd_alloc = 4;

  priv->events =
    g_malloc(sizeof(InfStandaloneIoNativeEvent) * priv->fd_alloc);

#ifdef G_OS_WIN32
  priv->events[0] = WSACreateEvent();
  if(priv->events[0] == WSA_INVALID_EVENT)
  {
    error_message = g_win32_error_message(WSAGetLastError());
    g_error("Failed to create wakeup event: %s", error_message);
    g_free(error_message); /* will not be called since g_error abort()s */
  }
  else
  {
    ++priv->fd_size;
  }
#else
  if(pipe(priv->wakeup_pipe) == -1)
  {
    g_error("Failed to create wakeup pipe: %s", strerror(errno));
  }
  else
  {
    priv->events[0].fd = priv->wakeup_pipe[0];
    priv->events[0].events = POLLIN | POLLERR;
    priv->events[0].revents = 0;
    ++priv->fd_size;
  }
#endif

  priv->watches = g_malloc(sizeof(InfIoWatch*) * (priv->fd_alloc - 1) );
  priv->timeouts = NULL;
  priv->dispatchs = NULL;

  priv->polling = FALSE;
  priv->loop_running = FALSE;
}

static void
inf_standalone_io_finalize(GObject* object)
{
  InfStandaloneIo* io;
  InfStandaloneIoPrivate* priv;
  guint i;
  GList* item;
  InfIoWatch* watch;
  InfIoTimeout* timeout;
  InfIoDispatch* dispatch;
#ifdef G_OS_WIN32
  gchar* error_message;
#endif

  io = INF_STANDALONE_IO(object);
  priv = INF_STANDALONE_IO_PRIVATE(io);

  g_mutex_lock(&priv->mutex);

  for(i = 1; i < priv->fd_size; ++i)
  {
    watch = priv->watches[i - 1];

    /* cannot dispose the IO while running a callback since the IO is
     * reffed on the stack. */
    g_assert(watch->executing == FALSE);

#ifdef G_OS_WIN32
    if(WSAEventSelect(*watch->socket, *watch->event, 0) ==
       SOCKET_ERROR)
    {
      error_message = g_win32_error_message(WSAGetLastError());
      g_warning("WSAEventSelect() failed: %s", error_message);
      g_free(error_message);
    }
#endif

    if(watch->notify)
      watch->notify(watch->user_data);
    g_slice_free(InfIoWatch, watch);
  }

  for(item = priv->timeouts; item != NULL; item = g_list_next(item))
  {
    timeout = (InfIoTimeout*)item->data;
    if(timeout->notify)
      timeout->notify(timeout->user_data);
    g_slice_free(InfIoTimeout, timeout);
  }

  for(item = priv->dispatchs; item != NULL; item = g_list_next(item))
  {
    dispatch = (InfIoDispatch*)item->data;
    if(dispatch->notify)
      dispatch->notify(dispatch->user_data);
    g_slice_free(InfIoDispatch, dispatch);
  }

#ifdef G_OS_WIN32
  for(i = 0; i < priv->fd_size; ++ i)
  {
    if(WSACloseEvent(priv->events[i]) == FALSE)
    {
      error_message = g_win32_error_message(WSAGetLastError());
      g_warning("WSACloseEvent() failed: %s", error_message);
      g_free(error_message);
    }
  }
#endif

  g_free(priv->events);
  g_free(priv->watches);
  g_list_free(priv->timeouts);
  g_list_free(priv->dispatchs);

#ifndef G_OS_WIN32
  if(close(priv->wakeup_pipe[0]) == -1)
  {
    g_warning(
      "Failed to close reading end of wakeup pipe: %s",
      strerror(errno)
    );
  }

  if(close(priv->wakeup_pipe[1]) == -1)
  {
    g_warning(
      "Failed to close writing end of wakeup pipe: %s",
      strerror(errno)
    );
  }
#endif

  g_mutex_unlock(&priv->mutex);
  g_mutex_clear(&priv->mutex);

  G_OBJECT_CLASS(inf_standalone_io_parent_class)->finalize(object);
}

static InfIoWatch**
inf_standalone_io_find_watch(InfStandaloneIo* io,
                             InfIoWatch* watch)
{
  InfStandaloneIoPrivate* priv;
  guint i;

  priv = INF_STANDALONE_IO_PRIVATE(io);
  for(i = 1; i < priv->fd_size; ++i)
    if(priv->watches[i-1] == watch)
      return &priv->watches[i-1];

  return NULL;

}

static InfIoWatch**
inf_standalone_io_find_watch_by_socket(InfStandaloneIo* io,
                                       InfNativeSocket* socket)
{
  InfStandaloneIoPrivate* priv;
  guint i;

  priv = INF_STANDALONE_IO_PRIVATE(io);
  for(i = 1; i < priv->fd_size; ++i)
    if(priv->watches[i-1]->socket == socket)
      return &priv->watches[i-1];

  return NULL;
}

static void
inf_standalone_io_wakeup(InfStandaloneIo* io)
{
  /* Wake up the main loop in case it is currently sleeping. This function is
   * called whenever a watch changes or a timeout or dispatch is added, so
   * that the new event is taken into account. */
  /* Should only ever be called with the IO's mutex being locked. */
  /* TODO: Turn this into a noop if called from the same thread the loop
   * runs in? */

  InfStandaloneIoPrivate* priv;
#ifndef G_OS_WIN32
  char c;
  ssize_t ret;
#else
  gchar* error_message;
#endif
  priv = INF_STANDALONE_IO_PRIVATE(io);

  if(priv->polling)
  {
#ifdef G_OS_WIN32
    if(WSASetEvent(priv->events[0]) == FALSE)
    {
      error_message = g_win32_error_message(WSAGetLastError());

      g_warning(
        "WSASetEvent() failed when attempting to wake up the main loop: %s",
        error_message
      );

      g_free(error_message);
    }
#else
    c = 'c';
    ret = write(priv->wakeup_pipe[1], &c, 1);
    if(ret == -1)
    {
      g_warning(
        "write() failed when attempting to wake up the main loop: %s",
        strerror(errno)
      );

      /* TODO: Is there anything we could do here?
       * Try to re-establish pipe? */
    }
    else if(ret == 0)
    {
      g_warning(
        "Received EOF from weakup pipe when attempting to wake "
        "up the main loop"
      );

      /* TODO: Is there anything we could do here?
       * Try to re-establish pipe? */
    }
#endif
  }
}

static InfIoWatch*
inf_standalone_io_io_add_watch(InfIo* io,
                               InfNativeSocket* socket,
                               InfIoEvent events,
                               InfIoWatchFunc func,
                               gpointer user_data,
                               GDestroyNotify notify)
{
  InfStandaloneIoPrivate* priv;
  InfIoWatch* watch;
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

  g_mutex_lock(&priv->mutex);

  /* Watching the same socket for different events at least won't work on
   * Windows since WSAEventSelect cancels the effect of previous
   * WSAEventSelect calls for the same socket. */
  if(inf_standalone_io_find_watch_by_socket(INF_STANDALONE_IO(io), socket))
  {
    g_mutex_unlock(&priv->mutex);
    return NULL;
  }

  /* TODO: If we are currently polling we should not modify the fds array
   * array but do this after wakeup directly after the poll call. */

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
      (priv->fd_alloc - 1) * sizeof(InfIoWatch*)
    );

    /* Update event pointers, the location of the events in memory might have
     * changed after realloc. */
    for(i = 1; i < priv->fd_size; ++i)
      priv->watches[i-1]->event = &priv->events[i];
  }

#ifdef G_OS_WIN32
  priv->events[priv->fd_size] = WSACreateEvent();
  if(priv->events[priv->fd_size] == WSA_INVALID_EVENT)
  {
    error_message = g_win32_error_message(WSAGetLastError());
    g_warning("WSACreateEvent() failed: %s", error_message);
    g_free(error_message);

    g_mutex_unlock(&priv->mutex);
    return NULL;
  }

  if(WSAEventSelect(*socket, priv->events[priv->fd_size], pevents) ==
     SOCKET_ERROR)
  {
    error_message = g_win32_error_message(WSAGetLastError());
    g_warning("WSAEventSelect() failed: %s", error_message);
    g_free(error_message);

    WSACloseEvent(priv->events[priv->fd_size]);
    g_mutex_unlock(&priv->mutex);
    return NULL;
  }
#else
  priv->events[priv->fd_size].fd = *socket;
  priv->events[priv->fd_size].events = pevents;
  priv->events[priv->fd_size].revents = 0;
#endif

  watch = g_slice_new(InfIoWatch);
  watch->event = &priv->events[priv->fd_size];
  watch->socket = socket;
  watch->func = func;
  watch->user_data = user_data;
  watch->notify = notify;
  watch->executing = FALSE;
  watch->disposed = FALSE;

  priv->watches[priv->fd_size-1] = watch;
  ++priv->fd_size;

  inf_standalone_io_wakeup(INF_STANDALONE_IO(io));
  g_mutex_unlock(&priv->mutex);

  return watch;
}

static void
inf_standalone_io_io_update_watch(InfIo* io,
                                  InfIoWatch* watch,
                                  InfIoEvent events)
{
  InfStandaloneIoPrivate* priv;
  InfIoWatch** watch_iter;
  long pevents;

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

  g_mutex_lock(&priv->mutex);

  watch_iter = inf_standalone_io_find_watch(INF_STANDALONE_IO(io), watch);
  if(watch_iter != NULL)
  {
    /* TODO: If we are currently polling we should not modify the fds array
     * array but do this after wakeup directly after the poll call. */

    /* Update */
#ifdef G_OS_WIN32
    if(WSAEventSelect(*watch->socket, *watch->event, pevents) == SOCKET_ERROR)
    {
      error_message = g_win32_error_message(WSAGetLastError());
      g_warning("WSAEventSelect() failed: %s", error_message);
      g_free(error_message);
    }
#else
    watch->event->events = pevents;
#endif

    inf_standalone_io_wakeup(INF_STANDALONE_IO(io));
  }

  g_mutex_unlock(&priv->mutex);
}

static void
inf_standalone_io_io_remove_watch(InfIo* io,
                                  InfIoWatch* watch)
{
  InfStandaloneIoPrivate* priv;
  InfIoWatch** watch_iter;
  guint index;

#ifdef G_OS_WIN32
  gchar* error_message;
#endif

  priv = INF_STANDALONE_IO_PRIVATE(io);

  g_mutex_lock(&priv->mutex);

  watch_iter = inf_standalone_io_find_watch(INF_STANDALONE_IO(io), watch);
  if(watch_iter != NULL)
  {
#ifdef G_OS_WIN32
    if(WSAEventSelect(*watch->socket, *watch->event, 0) == SOCKET_ERROR)
    {
      error_message = g_win32_error_message(WSAGetLastError());
      g_warning("WSAEventSelect() failed: %s", error_message);
      g_free(error_message);
    }

    if(WSACloseEvent(*watch->event) == FALSE)
    {
      error_message = g_win32_error_message(WSAGetLastError());
      g_warning("WSACloseEvent() failed: %s", error_message);
      g_free(error_message);
    }
#endif

    /* TODO: If we are currently polling we should not modify the fds array
     * array but do this after wakeup directly after the poll call. */
    if(watch->executing)
    {
      /* The callback of the watch is currently running. We don't want to
       * destroy the user data while it is, so we just remove it from the
       * watches array, wait for the callback to return and then free the
       * user_data and the InfIoWatch struct. */
      watch->disposed = TRUE;
    }
    else
    {
      /* Free user_data */
      if(watch->notify)
        watch->notify(watch->user_data);
      g_slice_free(InfIoWatch, watch);
    }

    /* Remove watch by replacing it by the last pollfd/watch */
    index = 1 + (watch_iter - priv->watches);
    if(index != priv->fd_size - 1)
    {
      memcpy(
        &priv->events[index],
        &priv->events[priv->fd_size - 1],
        sizeof(InfStandaloneIoNativeEvent)
      );

      memcpy(
        &priv->watches[index - 1],
        &priv->watches[priv->fd_size - 2],
        sizeof(InfIoWatch*)
      );

      priv->watches[index - 1]->event = &priv->events[index];
    }

    --priv->fd_size;

    inf_standalone_io_wakeup(INF_STANDALONE_IO(io));
  }

  g_mutex_unlock(&priv->mutex);
}

static InfIoTimeout*
inf_standalone_io_io_add_timeout(InfIo* io,
                                 guint msecs,
                                 InfIoTimeoutFunc func,
                                 gpointer user_data,
                                 GDestroyNotify notify)
{
  InfStandaloneIoPrivate* priv;
  InfIoTimeout* timeout;

  priv = INF_STANDALONE_IO_PRIVATE(io);
  timeout = g_slice_new(InfIoTimeout);

  g_get_current_time(&timeout->begin);
  timeout->msecs = msecs;
  timeout->func = func;
  timeout->user_data = user_data;
  timeout->notify = notify;

  g_mutex_lock(&priv->mutex);
  priv->timeouts = g_list_prepend(priv->timeouts, timeout);
  inf_standalone_io_wakeup(INF_STANDALONE_IO(io));
  g_mutex_unlock(&priv->mutex);

  return timeout;
}

static void
inf_standalone_io_io_remove_timeout(InfIo* io,
                                    InfIoTimeout* timeout)
{
  InfStandaloneIoPrivate* priv;
  GList* item;

  priv = INF_STANDALONE_IO_PRIVATE(io);

  g_mutex_lock(&priv->mutex);

  item = g_list_find(priv->timeouts, timeout);
  if(item != NULL)
  {
    priv->timeouts = g_list_delete_link(priv->timeouts, item);
    g_mutex_unlock(&priv->mutex);

    if(timeout->notify)
      timeout->notify(timeout->user_data);

    g_slice_free(InfIoTimeout, timeout);

    /* No need to wake up the main loop; it might run into its timeout sooner
     * than necessary now, but that's OK. */
  }
  else
  {
    g_mutex_unlock(&priv->mutex);
  }
}

static InfIoDispatch*
inf_standalone_io_io_add_dispatch(InfIo* io,
                                  InfIoDispatchFunc func,
                                  gpointer user_data,
                                  GDestroyNotify notify)
{
  InfStandaloneIoPrivate* priv;
  InfIoDispatch* dispatch;

  priv = INF_STANDALONE_IO_PRIVATE(io);
  dispatch = g_slice_new(InfIoDispatch);

  dispatch->func = func;
  dispatch->user_data = user_data;
  dispatch->notify = notify;

  g_mutex_lock(&priv->mutex);
  priv->dispatchs = g_list_prepend(priv->dispatchs, dispatch);
  inf_standalone_io_wakeup(INF_STANDALONE_IO(io));
  g_mutex_unlock(&priv->mutex);

  return dispatch;
}

static void
inf_standalone_io_io_remove_dispatch(InfIo* io,
                                     InfIoDispatch* dispatch)
{
  InfStandaloneIoPrivate* priv;
  GList* item;

  priv = INF_STANDALONE_IO_PRIVATE(io);

  g_mutex_lock(&priv->mutex);

  item = g_list_find(priv->dispatchs, dispatch);
  if(item != NULL)
  {
    priv->dispatchs = g_list_delete_link(priv->dispatchs, item);
    g_mutex_unlock(&priv->mutex);

    if(dispatch->notify)
      dispatch->notify(dispatch->user_data);

    g_slice_free(InfIoDispatch, dispatch);

    /* No need to wake up the main loop; it might run into its timeout sooner
     * than necessary now, but that's OK. */
  }
  else
  {
    g_mutex_unlock(&priv->mutex);
  }
}

static void
inf_standalone_io_class_init(InfStandaloneIoClass* io_class)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(io_class);

  object_class->finalize = inf_standalone_io_finalize;
}

static void
inf_standalone_io_io_iface_init(InfIoInterface* iface)
{
  iface->add_watch = inf_standalone_io_io_add_watch;
  iface->update_watch = inf_standalone_io_io_update_watch;
  iface->remove_watch = inf_standalone_io_io_remove_watch;
  iface->add_timeout = inf_standalone_io_io_add_timeout;
  iface->remove_timeout = inf_standalone_io_io_remove_timeout;
  iface->add_dispatch = inf_standalone_io_io_add_dispatch;
  iface->remove_dispatch = inf_standalone_io_io_remove_dispatch;
}

/**
 * inf_standalone_io_new: (constructor)
 *
 * Creates a new #InfStandaloneIo.
 *
 * Returns: (transfer full): A new #InfStandaloneIo. Free with
 * g_object_unref() when no longer needed.
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
  InfStandaloneIoPrivate* priv;

  g_return_if_fail(INF_IS_STANDALONE_IO(io));
  priv = INF_STANDALONE_IO_PRIVATE(io);

  g_object_ref(io);
  g_mutex_lock(&priv->mutex);

  g_return_val_if_fail(priv->polling == FALSE, g_mutex_unlock(&priv->mutex));

  inf_standalone_io_iteration_impl(io, INF_STANDALONE_IO_POLL_INFINITE);

  g_mutex_unlock(&priv->mutex);
  g_object_unref(io);
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
  InfStandaloneIoPrivate* priv;
  g_return_if_fail(INF_IS_STANDALONE_IO(io));
  priv = INF_STANDALONE_IO_PRIVATE(io);

  g_object_ref(io);
  g_mutex_lock(&priv->mutex);

  g_return_val_if_fail(priv->polling == FALSE, g_mutex_unlock(&priv->mutex));

  inf_standalone_io_iteration_impl(io, (int)timeout);

  g_mutex_unlock(&priv->mutex);
  g_object_unref(io);
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

  g_object_ref(io);
  g_mutex_lock(&priv->mutex);

  g_return_val_if_fail(
    priv->loop_running == FALSE,
    g_mutex_unlock(&priv->mutex)
  );

  g_return_val_if_fail(
    priv->polling == FALSE,
    g_mutex_unlock(&priv->mutex)
  );

  /* TODO: Actually we need to make sure that a previous loop() call in
   * another thread has exited the loop below, otherwise we will end up with
   * two loops running one of which is supposed to have quit. */
  priv->loop_running = TRUE;

  while(priv->loop_running == TRUE)
    inf_standalone_io_iteration_impl(io, -1);

  g_mutex_unlock(&priv->mutex);
  g_object_unref(io);
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

  g_mutex_lock(&priv->mutex);

  g_return_if_fail(priv->loop_running == TRUE);
  priv->loop_running = FALSE;

  inf_standalone_io_wakeup(io);
  g_mutex_unlock(&priv->mutex);
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
  gboolean running;

  g_return_val_if_fail(INF_IS_STANDALONE_IO(io), FALSE);
  priv = INF_STANDALONE_IO_PRIVATE(io);

  g_mutex_lock(&priv->mutex);
  running = priv->loop_running;
  g_mutex_unlock(&priv->mutex);

  return running;
}

/* vim:set et sw=2 ts=2: */
