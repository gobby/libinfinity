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

#include <libinfinity/common/inf-standalone-io.h>
#include <libinfinity/common/inf-io.h>

#include <poll.h>
#include <errno.h>
#include <string.h>

typedef struct _InfStandaloneIoWatch InfStandaloneIoWatch;
struct _InfStandaloneIoWatch {
  struct pollfd* pfd;
  InfNativeSocket* socket;
  InfIoFunc func;
  gpointer user_data;
};

typedef struct _InfStandaloneIoPrivate InfStandaloneIoPrivate;
struct _InfStandaloneIoPrivate {
  struct pollfd* pfds;
  InfStandaloneIoWatch* watches;

  guint fd_size;
  guint fd_alloc;

  gboolean loop_running;
};

#define INF_STANDALONE_IO_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TYPE_STANDALONE_IO, InfStandaloneIoPrivate))

static GObjectClass* parent_class;

static void
inf_standalone_io_iteration_impl(InfStandaloneIo* io,
                                 int timeout)
{
  InfStandaloneIoPrivate* priv;
  InfIoEvent events;
  int result;
  guint i;

  priv = INF_STANDALONE_IO_PRIVATE(io);

  do
  {
    result = poll(priv->pfds, (nfds_t)priv->fd_size, timeout);
  } while(result == -1 && errno == EINTR);

  if(result == -1)
  {
    g_warning("poll() failed: %s\n", strerror(errno));
    return;
  }

  g_object_ref(G_OBJECT(io));

  while(result--)
  {
    for(i = 0; i < priv->fd_size; ++ i)
    {
      if(priv->pfds[i].revents != 0)
      {
        events = 0;
        if(priv->pfds[i].revents & POLLIN)
          events |= INF_IO_INCOMING;
        if(priv->pfds[i].revents & POLLOUT)
          events |= INF_IO_OUTGOING;
        /* We treat POLLPRI as error because it should not occur in
         * infinote. */
        if(priv->pfds[i].revents & (POLLERR | POLLPRI | POLLHUP | POLLNVAL))
          events |= INF_IO_ERROR;
        
        priv->pfds[i].revents = 0;

        priv->watches[i].func(
          priv->watches[i].socket,
          events,
          priv->watches[i].user_data
        );

        /* The callback might have done everything, including completly
         * screwed up the array of file descriptors. This is why we break
         * here and iterate from the beginning to find the next event. */
        break;
      }
    }
  }

  g_object_unref(G_OBJECT(io));
}

static void
inf_standalone_io_init(GTypeInstance* instance,
                       gpointer g_class)
{
  InfStandaloneIo* io;
  InfStandaloneIoPrivate* priv;

  io = INF_STANDALONE_IO(instance);
  priv = INF_STANDALONE_IO_PRIVATE(io);

  priv->pfds = g_malloc(sizeof(struct pollfd) * 4);
  priv->watches = g_malloc(sizeof(InfStandaloneIoWatch) * 4);

  priv->fd_size = 0;
  priv->fd_alloc = 4;

  priv->loop_running = FALSE;
}

static void
inf_standalone_io_finalize(GObject* object)
{
  InfStandaloneIo* io;
  InfStandaloneIoPrivate* priv;

  io = INF_STANDALONE_IO(object);
  priv = INF_STANDALONE_IO_PRIVATE(io);

  g_free(priv->pfds);
  g_free(priv->watches);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_standalone_io_io_watch(InfIo* io,
                           InfNativeSocket* socket,
                           InfIoEvent events,
                           InfIoFunc func,
                           gpointer user_data)
{
  InfStandaloneIoPrivate* priv;
  int pevents;
  guint i;

  priv = INF_STANDALONE_IO_PRIVATE(io);

  pevents = 0;
  if(events & INF_IO_INCOMING)
    pevents |= POLLIN;
  if(events & INF_IO_OUTGOING)
    pevents |= POLLOUT;
  if(events & INF_IO_ERROR)
    pevents |= (POLLERR | POLLHUP | POLLNVAL | POLLPRI);

  for(i = 0; i < priv->fd_size; ++ i)
  {
    if(priv->watches[i].socket == socket)
    {
      if(events == 0)
      {
        /* Remove watch by replacing it by the last pollfd/watch */
        if(i != priv->fd_size - 1)
        {
          memcpy(
            &priv->pfds[i],
            &priv->pfds[priv->fd_size - 1],
            sizeof(struct pollfd)
          );

          memcpy(
            &priv->watches[i],
            &priv->watches[priv->fd_size - 1],
            sizeof(InfStandaloneIoWatch)
          );

          priv->watches[i].pfd = &priv->pfds[i];
        }

        -- priv->fd_size;
      }
      else
      {
        /* Update */
        priv->pfds[i].events = pevents;

        priv->watches[i].func = func;
        priv->watches[i].user_data = user_data;
      }

      return;
    }
  }

  /* Socket is not already present, so create new watch */
  if(priv->fd_size == priv->fd_alloc)
  {
    priv->fd_alloc += 4;

    priv->pfds = g_realloc(
      priv->pfds,
      priv->fd_alloc * sizeof(struct pollfd)
    );

    priv->watches = g_realloc(
      priv->watches,
      priv->fd_alloc * sizeof(InfStandaloneIoWatch)
    );
  }

  priv->pfds[priv->fd_size].fd = *socket;
  priv->pfds[priv->fd_size].events = pevents;
  priv->pfds[priv->fd_size].revents = 0;

  priv->watches[priv->fd_size].pfd = &priv->pfds[priv->fd_size];
  priv->watches[priv->fd_size].socket = socket;
  priv->watches[priv->fd_size].func = func;
  priv->watches[priv->fd_size].user_data = user_data;

  ++ priv->fd_size;
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

/** inf_standalone_io_new:
 *
 * Creates a new #InfStandaloneIo.
 **/
InfStandaloneIo*
inf_standalone_io_new(void)
{
  GObject* object;
  object = g_object_new(INF_TYPE_STANDALONE_IO, NULL);
  return INF_STANDALONE_IO(object);
}

/** inf_standalone_io_iteration:
 *
 * @io: A #InfStandaloneIo.
 *
 * Performs a single iteration of @io. The call will block until a first
 * event has occured. Then, it will process that event and return.
 **/
void
inf_standalone_io_iteration(InfStandaloneIo* io)
{
  g_return_if_fail(INF_IS_STANDALONE_IO(io));
  inf_standalone_io_iteration_impl(io, -1);
}

/** inf_standalone_io_iteration_timeout:
 *
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

/** inf_standalone_io_loop:
 *
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

/** inf_standalone_io_loop_quit:
 *
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

/** inf_standalone_io_loop_running:
 *
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
