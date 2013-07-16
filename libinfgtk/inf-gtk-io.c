/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2013 Armin Burgmeier <armin@arbur.net>
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

#include <libinfgtk/inf-gtk-io.h>
#include <libinfinity/common/inf-io.h>

struct _InfIoWatch {
  InfGtkIo* io;
  InfNativeSocket* socket;
  guint id;
  InfIoWatchFunc func;
  gpointer user_data;
  GDestroyNotify notify;

  /* Additional state to avoid freeing the userdata while running
   * the callback */
  gboolean executing;
  gboolean disposed;
};

struct _InfIoTimeout {
  InfGtkIo* io;
  guint id;
  InfIoTimeoutFunc func;
  gpointer user_data;
  GDestroyNotify notify;
};

struct _InfIoDispatch {
  InfGtkIo* io;
  guint id;
  InfIoDispatchFunc func;
  gpointer user_data;
  GDestroyNotify notify;
};

typedef struct _InfGtkIoUserdata InfGtkIoUserdata;
struct _InfGtkIoUserdata {
  union {
    InfIoWatch* watch;
    InfIoTimeout* timeout;
    InfIoDispatch* dispatch;
  } shared;

  GMutex* mutex;
  int* mutexref;
};

typedef struct _InfGtkIoPrivate InfGtkIoPrivate;
struct _InfGtkIoPrivate {
  /* TODO: GMainContext */

  GMutex* mutex;
  int* mutexref; /* reference counter for the mutex */

  GSList* watches;
  GSList* timeouts;
  GSList* dispatchs;
};

#define INF_GTK_IO_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_GTK_TYPE_IO, InfGtkIoPrivate))

static GObjectClass* parent_class;

static void
inf_gtk_io_userdata_free(gpointer data)
{
  InfGtkIoUserdata* userdata;
  userdata = (InfGtkIoUserdata*)data;

  /* Note that the shared members may already be invalid at this point */

  /* Note also that we cannot lock the mutex here, because this function
   * might or might not be called with it being locked already. However in
   * this case we can use an atomic operation instead. */

  if(g_atomic_int_dec_and_test(userdata->mutexref) == TRUE)
  {
    g_mutex_free(userdata->mutex);
    g_free(userdata->mutexref);
  }

  g_slice_free(InfGtkIoUserdata, userdata);
}

static InfIoWatch*
inf_gtk_io_watch_new(InfGtkIo* io,
                     InfNativeSocket* socket,
                     InfIoWatchFunc func,
                     gpointer user_data,
                     GDestroyNotify notify)
{
  InfIoWatch* watch;
  watch = g_slice_new(InfIoWatch);
  watch->io = io;
  watch->socket = socket;
  watch->id = 0;
  watch->func = func;
  watch->user_data = user_data;
  watch->notify = notify;
  watch->executing = FALSE;
  watch->disposed = FALSE;
  return watch;
}

static void
inf_gtk_io_watch_free(InfIoWatch* watch)
{
  if(watch->notify)
    watch->notify(watch->user_data);

  g_slice_free(InfIoWatch, watch);
}

static InfIoTimeout*
inf_gtk_io_timeout_new(InfGtkIo* io,
                       InfIoTimeoutFunc func,
                       gpointer user_data,
                       GDestroyNotify notify)
{
  InfIoTimeout* timeout;
  timeout = g_slice_new(InfIoTimeout);

  timeout->io = io;
  timeout->id = 0;
  timeout->func = func;
  timeout->user_data = user_data;
  timeout->notify = notify;
  return timeout;
}

static void
inf_gtk_io_timeout_free(InfIoTimeout* timeout)
{
  if(timeout->notify)
    timeout->notify(timeout->user_data);

  g_slice_free(InfIoTimeout, timeout);
}

static InfIoDispatch*
inf_gtk_io_dispatch_new(InfGtkIo* io,
                        InfIoDispatchFunc func,
                        gpointer user_data,
                        GDestroyNotify notify)
{
  InfIoDispatch* dispatch;
  dispatch = g_slice_new(InfIoDispatch);

  dispatch->io = io;
  dispatch->id = 0;
  dispatch->func = func;
  dispatch->user_data = user_data;
  dispatch->notify = notify;
  return dispatch;
}

static void
inf_gtk_io_dispatch_free(InfIoDispatch* dispatch)
{
  if(dispatch->notify)
    dispatch->notify(dispatch->user_data);

  g_slice_free(InfIoDispatch, dispatch);
}

static InfIoWatch*
inf_gtk_io_watch_lookup(InfGtkIo* io,
                        InfNativeSocket* socket)
{
  InfGtkIoPrivate* priv;
  GSList* item;

  priv = INF_GTK_IO_PRIVATE(io);

  for(item = priv->watches; item != NULL; item = g_slist_next(item))
    if( ((InfIoWatch*)item->data)->socket == socket)
      return (InfIoWatch*)item->data;

  return NULL;
}

static void
inf_gtk_io_init(GTypeInstance* instance,
                gpointer g_class)
{
  InfGtkIo* io;
  InfGtkIoPrivate* priv;

  io = INF_GTK_IO(instance);
  priv = INF_GTK_IO_PRIVATE(io);

  priv->mutex = g_mutex_new();
  priv->mutexref = g_malloc(sizeof(int));
  *priv->mutexref = 1;

  priv->watches = NULL;
  priv->timeouts = NULL;
  priv->dispatchs = NULL;
}

static void
inf_gtk_io_finalize(GObject* object)
{
  InfGtkIo* io;
  InfGtkIoPrivate* priv;
  GSList* item;
  int mutexref;

  io = INF_GTK_IO(object);
  priv = INF_GTK_IO_PRIVATE(io);

  g_mutex_lock(priv->mutex);

  for(item = priv->watches; item != NULL; item = g_slist_next(item))
  {
    /* We have a stack-ref on the InfGtkIo when exeucting the callback */
    g_assert( ((InfIoWatch*)item->data)->executing == FALSE);

    if( ((InfIoWatch*)item->data)->id != 0)
      g_source_remove( ((InfIoWatch*)item->data)->id);
    inf_gtk_io_watch_free((InfIoWatch*)item->data);
  }
  g_slist_free(priv->watches);

  for(item = priv->timeouts; item != NULL; item = g_slist_next(item))
  {
    if( ((InfIoTimeout*)item->data)->id != 0)
      g_source_remove( ((InfIoTimeout*)item->data)->id);
    inf_gtk_io_timeout_free((InfIoTimeout*)item->data);
  }
  g_slist_free(priv->timeouts);

  for(item = priv->dispatchs; item != NULL; item = g_slist_next(item))
  {
    if( ((InfIoDispatch*)item->data)->id != 0)
      g_source_remove( ((InfIoDispatch*)item->data)->id);
    inf_gtk_io_dispatch_free((InfIoDispatch*)item->data);
  }
  g_slist_free(priv->dispatchs);
  g_mutex_unlock(priv->mutex);

  /* some callback userdata might still have a reference to the mutex, and
   * wait for the callback function to be called until it is released. The
   * callback function will do nothing since g_source_is_destroyed() will
   * return FALSE since we removed all sources above. But we need to keep
   * the mutex alive so that the callbacks can check. */
  if(g_atomic_int_dec_and_test(priv->mutexref))
  {
    g_mutex_free(priv->mutex);
    g_free(priv->mutexref);
  }

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static GIOCondition
inf_gtk_io_inf_events_to_glib_events(InfIoEvent events)
{
  GIOCondition cond;

  cond = 0;
  if(events & INF_IO_INCOMING)
    cond |= G_IO_IN;
  if(events & INF_IO_OUTGOING)
    cond |= G_IO_OUT;
  if(events & INF_IO_ERROR)
    cond |= (G_IO_ERR | G_IO_HUP);

  return cond;
}

static InfIoEvent
inf_gtk_io_inf_events_from_glib_events(GIOCondition cond)
{
  InfIoEvent event;

  event = 0;
  if(cond & G_IO_IN)
    event |= INF_IO_INCOMING;
  if(cond & G_IO_OUT)
    event |= INF_IO_OUTGOING;
  if(cond & (G_IO_ERR | G_IO_HUP))
    event |= INF_IO_ERROR;

  return event;
}

static gboolean
inf_gtk_io_watch_func(GIOChannel* channel,
                      GIOCondition condition,
                      gpointer user_data)
{
  InfGtkIoUserdata* userdata;
  InfIoWatch* watch;
  InfGtkIoPrivate* priv;

  userdata = (InfGtkIoUserdata*)user_data;
  g_mutex_lock(userdata->mutex);
  if(!g_source_is_destroyed(g_main_current_source()))
  {
    /* At this point we now that InfGtkIo is still alive because otherwise
     * the source would have been destroyed in _finalize. */
    watch = userdata->shared.watch;

    g_object_ref(watch->io);
    priv = INF_GTK_IO_PRIVATE(watch->io);

    g_assert(*priv->mutexref > 1); /* Both InfGtkIo and we have a reference */
    g_assert(g_slist_find(priv->watches, watch) != NULL);

    watch->executing = TRUE;
    g_mutex_unlock(userdata->mutex);

    /* Note that at this point the watch object could be removed from the
     * list, but, since executing is set to TRUE, it is not freed. */

    watch->func(
      watch->socket,
      inf_gtk_io_inf_events_from_glib_events(condition),
      watch->user_data
    );

    g_mutex_lock(userdata->mutex);
    watch->executing = FALSE;
    g_object_unref(watch->io);

    if(watch->disposed == TRUE)
    {
      g_mutex_unlock(userdata->mutex);
      inf_gtk_io_watch_free(watch);
    }
    else
    {
      g_mutex_unlock(userdata->mutex);
    }
  }
  else
  {
    g_mutex_unlock(userdata->mutex);
  }

  return TRUE;
}

static gboolean
inf_gtk_io_timeout_func(gpointer user_data)
{
  InfIoTimeout* timeout;
  InfGtkIoPrivate* priv;
  InfGtkIoUserdata* userdata;

  userdata = (InfGtkIoUserdata*)user_data;
  g_mutex_lock(userdata->mutex);
  if(!g_source_is_destroyed(g_main_current_source()))
  {
    /* At this point we now that InfGtkIo is still alive because otherwise
     * the source would have been destroyed in _finalize. */
    timeout = userdata->shared.timeout;
    priv = INF_GTK_IO_PRIVATE(timeout->io);

    g_assert(*priv->mutexref > 1); /* Both InfGtkIo and we have a reference */
    g_assert(g_slist_find(priv->timeouts, timeout) != NULL);
    priv->timeouts = g_slist_remove(priv->timeouts, timeout);
    g_mutex_unlock(userdata->mutex);

    timeout->func(timeout->user_data);
    inf_gtk_io_timeout_free(timeout);
  }
  else
  {
    g_mutex_unlock(userdata->mutex);
  }

  return FALSE;
}

static gboolean
inf_gtk_io_dispatch_func(gpointer user_data)
{
  InfIoDispatch* dispatch;
  InfGtkIoPrivate* priv;
  InfGtkIoUserdata* userdata;

  userdata = (InfGtkIoUserdata*)user_data;
  g_mutex_lock(userdata->mutex);
  if(!g_source_is_destroyed(g_main_current_source()))
  {
    /* At this point we now that InfGtkIo is still alive because otherwise
     * the source would have been destroyed in _finalize. */
    dispatch = userdata->shared.dispatch;
    priv = INF_GTK_IO_PRIVATE(dispatch->io);

    g_assert(*priv->mutexref > 1); /* Both InfGtkIo and we have a reference */
    g_assert(g_slist_find(priv->dispatchs, dispatch) != NULL);
    priv->dispatchs = g_slist_remove(priv->dispatchs, dispatch);
    g_mutex_unlock(userdata->mutex);

    dispatch->func(dispatch->user_data);
    inf_gtk_io_dispatch_free(dispatch);
  }
  else
  {
    g_mutex_unlock(userdata->mutex);
  }

  return FALSE;
}

static InfIoWatch*
inf_gtk_io_io_add_watch(InfIo* io,
                        InfNativeSocket* socket,
                        InfIoEvent events,
                        InfIoWatchFunc func,
                        gpointer user_data,
                        GDestroyNotify notify)
{
  InfGtkIoPrivate* priv;
  InfIoWatch* watch;
  InfGtkIoUserdata* data;
  GIOChannel* channel;

  priv = INF_GTK_IO_PRIVATE(io);

  g_mutex_lock(priv->mutex);
  watch = inf_gtk_io_watch_lookup(INF_GTK_IO(io), socket);
  if(watch != NULL)
  {
    g_mutex_unlock(priv->mutex);
    return NULL;
  }

  watch = inf_gtk_io_watch_new(
    INF_GTK_IO(io),
    socket,
    func,
    user_data,
    notify
  );

  data = g_slice_new(InfGtkIoUserdata);
  data->shared.watch = watch;
  data->mutex = priv->mutex;
  data->mutexref = priv->mutexref;
  g_atomic_int_inc(data->mutexref);

#ifdef G_OS_WIN32
  channel = g_io_channel_win32_new_socket(*socket);
#else
  channel = g_io_channel_unix_new(*socket);
#endif

  watch->id = g_io_add_watch_full(
    channel,
    G_PRIORITY_DEFAULT,
    inf_gtk_io_inf_events_to_glib_events(events),
    inf_gtk_io_watch_func,
    data,
    inf_gtk_io_userdata_free
  );

  g_io_channel_unref(channel);

  priv->watches = g_slist_prepend(priv->watches, watch);
  g_mutex_unlock(priv->mutex);

  return watch;
}

static void
inf_gtk_io_io_update_watch(InfIo* io,
                           InfIoWatch* watch,
                           InfIoEvent events)
{
  InfGtkIoPrivate* priv;
  InfGtkIoUserdata* data;
  GIOChannel* channel;

  priv = INF_GTK_IO_PRIVATE(io);
  g_mutex_lock(priv->mutex);

  g_assert(g_slist_find(priv->watches, watch) != NULL);

  data = g_slice_new(InfGtkIoUserdata);
  data->shared.watch = watch;
  data->mutex = priv->mutex;
  data->mutexref = priv->mutexref;
  g_atomic_int_inc(data->mutexref);
  g_mutex_unlock(priv->mutex);

  g_source_remove(watch->id);

#ifdef G_OS_WIN32
  channel = g_io_channel_win32_new_socket(*watch->socket);
#else
  channel = g_io_channel_unix_new(*watch->socket);
#endif

  watch->id = g_io_add_watch_full(
    channel,
    G_PRIORITY_DEFAULT,
    inf_gtk_io_inf_events_to_glib_events(events),
    inf_gtk_io_watch_func,
    data,
    inf_gtk_io_userdata_free
  );

  g_io_channel_unref(channel);
}

static void
inf_gtk_io_io_remove_watch(InfIo* io,
                           InfIoWatch* watch)
{
  InfGtkIoPrivate* priv;

  priv = INF_GTK_IO_PRIVATE(io);
  g_mutex_lock(priv->mutex);

  g_assert(g_slist_find(priv->watches, watch) != NULL);
  priv->watches = g_slist_remove(priv->watches, watch);

  if(watch->executing)
  {
    /* If we are currently running the watch callback then don't free the
     * watch object right now, because this would destroy the userdata before
     * the callback finished running. Instead, remember that the watch is
     * going to be disposed and remove it in the watch func right after
     * having called the callback. */
    watch->disposed = TRUE;
    g_mutex_unlock(priv->mutex);
  }
  else
  {
    g_mutex_unlock(priv->mutex);
    inf_gtk_io_watch_free(watch);
  }

  /* Note that we can do this safely without having locked the mutex because
   * if the callback function is currently being invoked then its user_data
   * will not be destroyed immediately. */
  g_source_remove(watch->id);
}

static InfIoTimeout*
inf_gtk_io_io_add_timeout(InfIo* io,
                          guint msecs,
                          InfIoTimeoutFunc func,
                          gpointer user_data,
                          GDestroyNotify notify)
{
  InfGtkIoPrivate* priv;
  InfIoTimeout* timeout;
  InfGtkIoUserdata* data;

  priv = INF_GTK_IO_PRIVATE(io);
  timeout = inf_gtk_io_timeout_new(INF_GTK_IO(io), func, user_data, notify);

  data = g_slice_new(InfGtkIoUserdata);
  data->shared.timeout = timeout;
  data->mutex = priv->mutex;
  data->mutexref = priv->mutexref;

  g_mutex_lock(priv->mutex);
  g_atomic_int_inc(data->mutexref);

  timeout->id = g_timeout_add_full(
    G_PRIORITY_DEFAULT,
    msecs,
    inf_gtk_io_timeout_func,
    data,
    inf_gtk_io_userdata_free
  );

  priv->timeouts = g_slist_prepend(priv->timeouts, timeout);
  g_mutex_unlock(priv->mutex);

  return timeout;
}

static void
inf_gtk_io_io_remove_timeout(InfIo* io,
                             InfIoTimeout* timeout)
{
  InfGtkIoPrivate* priv;

  priv = INF_GTK_IO_PRIVATE(io);

  g_mutex_lock(priv->mutex);
  g_assert(g_slist_find(priv->timeouts, timeout) != NULL);
  priv->timeouts = g_slist_remove(priv->timeouts, timeout);
  g_mutex_unlock(priv->mutex);

  /* Note that we can do this safely without having locked the mutex because
   * if the callback function is currently being invoked then its user_data
   * will not be destroyed immediately. */
  g_source_remove(timeout->id);

  inf_gtk_io_timeout_free(timeout);
}

static InfIoDispatch*
inf_gtk_io_io_add_dispatch(InfIo* io,
                           InfIoDispatchFunc func,
                           gpointer user_data,
                           GDestroyNotify notify)
{
  InfGtkIoPrivate* priv;
  InfIoDispatch* dispatch;
  InfGtkIoUserdata* data;

  priv = INF_GTK_IO_PRIVATE(io);
  dispatch = inf_gtk_io_dispatch_new(INF_GTK_IO(io), func, user_data, notify);

  data = g_slice_new(InfGtkIoUserdata);
  data->shared.dispatch = dispatch;
  data->mutex = priv->mutex;
  data->mutexref = priv->mutexref;

  g_mutex_lock(priv->mutex);
  g_atomic_int_inc(data->mutexref);

  dispatch->id = g_idle_add_full(
    G_PRIORITY_DEFAULT_IDLE,
    inf_gtk_io_dispatch_func,
    data,
    inf_gtk_io_userdata_free
  );

  priv->dispatchs = g_slist_prepend(priv->dispatchs, dispatch);
  g_mutex_unlock(priv->mutex);

  return dispatch;
}

static void
inf_gtk_io_io_remove_dispatch(InfIo* io,
                              InfIoDispatch* dispatch)
{
  InfGtkIoPrivate* priv;

  priv = INF_GTK_IO_PRIVATE(io);

  g_mutex_lock(priv->mutex);
  g_assert(g_slist_find(priv->dispatchs, dispatch) != NULL);
  priv->dispatchs = g_slist_remove(priv->dispatchs, dispatch);
  g_mutex_unlock(priv->mutex);

  /* Note that we can do this safely without having locked the mutex because
   * if the callback function is currently being invoked then its user_data
   * will not be destroyed immediately. */
  g_source_remove(dispatch->id);

  inf_gtk_io_dispatch_free(dispatch);
}

static void
inf_gtk_io_class_init(gpointer g_class,
                      gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfGtkIoPrivate));

  object_class->finalize = inf_gtk_io_finalize;
}

static void
inf_gtk_io_io_init(gpointer g_iface,
                   gpointer iface_data)
{
  InfIoIface* iface;
  iface = (InfIoIface*)g_iface;

  iface->add_watch = inf_gtk_io_io_add_watch;
  iface->update_watch = inf_gtk_io_io_update_watch;
  iface->remove_watch = inf_gtk_io_io_remove_watch;
  iface->add_timeout = inf_gtk_io_io_add_timeout;
  iface->remove_timeout = inf_gtk_io_io_remove_timeout;
  iface->add_dispatch = inf_gtk_io_io_add_dispatch;
  iface->remove_dispatch = inf_gtk_io_io_remove_dispatch;
}

GType
inf_gtk_io_get_type(void)
{
  static GType io_type = 0;

  if(!io_type)
  {
    static const GTypeInfo io_type_info = {
      sizeof(InfGtkIoClass),   /* class_size */
      NULL,                    /* base_init */
      NULL,                    /* base_finalize */
      inf_gtk_io_class_init,   /* class_init */
      NULL,                    /* class_finalize */
      NULL,                    /* class_data */
      sizeof(InfGtkIo),        /* instance_size */
      0,                       /* n_preallocs */
      inf_gtk_io_init,         /* instance_init */
      NULL                     /* value_table */
    };

    static const GInterfaceInfo io_info = {
      inf_gtk_io_io_init,
      NULL,
      NULL
    };

    io_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfGtkIo",
      &io_type_info,
      0
    );

    g_type_add_interface_static(
      io_type,
      INF_TYPE_IO,
      &io_info
    );
  }

  return io_type;
}

/**
 * inf_gtk_io_new:
 *
 * Creates a new #InfGtkIo.
 *
 * Returns: A new #InfGtkIo. Free with g_object_unref() when no longer needed.
 **/
InfGtkIo*
inf_gtk_io_new(void)
{
  GObject* object;
  object = g_object_new(INF_GTK_TYPE_IO, NULL);
  return INF_GTK_IO(object);
}

/* vim:set et sw=2 ts=2: */
