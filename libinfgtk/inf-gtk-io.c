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

#include <libinfgtk/inf-gtk-io.h>
#include <libinfinity/common/inf-io.h>

typedef struct _InfGtkIoWatch InfGtkIoWatch;
struct _InfGtkIoWatch {
  InfNativeSocket* socket;
  guint id;
  InfIoFunc func;
  gpointer user_data;
};

typedef struct _InfGtkIoTimeout InfGtkIoTimeout;
struct _InfGtkIoTimeout {
  InfGtkIo* io;
  guint id;
  InfIoTimeoutFunc func;
  gpointer user_data;
};

typedef struct _InfGtkIoPrivate InfGtkIoPrivate;
struct _InfGtkIoPrivate {
  /* TODO: GMainContext */

  GSList* watches;
  GSList* timeouts;
};

#define INF_GTK_IO_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_GTK_TYPE_IO, InfGtkIoPrivate))

static GObjectClass* parent_class;

static InfGtkIoWatch*
inf_gtk_io_watch_new(InfNativeSocket* socket,
                     InfIoFunc func,
                     gpointer user_data)
{
  InfGtkIoWatch* watch;
  watch = g_slice_new(InfGtkIoWatch);
  watch->socket = socket;
  watch->id = 0;
  watch->func = func;
  watch->user_data = user_data;
  return watch;
}

static void
inf_gtk_io_watch_free(InfGtkIoWatch* watch)
{
  if(watch->id != 0)
    g_source_remove(watch->id);

  g_slice_free(InfGtkIoWatch, watch);
}

static InfGtkIoTimeout*
inf_gtk_io_timeout_new(InfGtkIo* io,
                       InfIoTimeoutFunc func,
                       gpointer user_data)
{
  InfGtkIoTimeout* timeout;
  timeout = g_slice_new(InfGtkIoTimeout);

  timeout->io = io;
  timeout->id = 0;
  timeout->func = func;
  timeout->user_data = user_data;
  return timeout;
}

static void
inf_gtk_io_timeout_free(InfGtkIoTimeout* timeout)
{
  if(timeout->id != 0)
    g_source_remove(timeout->id);

  g_slice_free(InfGtkIoTimeout, timeout);
}

static InfGtkIoWatch*
inf_gtk_io_watch_lookup(InfGtkIo* io,
                        InfNativeSocket* socket)
{
  InfGtkIoPrivate* priv;
  GSList* item;

  priv = INF_GTK_IO_PRIVATE(io);

  for(item = priv->watches; item != NULL; item = g_slist_next(item))
    if( ((InfGtkIoWatch*)item->data)->socket == socket)
      return (InfGtkIoWatch*)item->data;

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

  priv->watches = NULL;
}

static void
inf_gtk_io_finalize(GObject* object)
{
  InfGtkIo* io;
  InfGtkIoPrivate* priv;
  GSList* item;

  io = INF_GTK_IO(object);
  priv = INF_GTK_IO_PRIVATE(io);

  for(item = priv->watches; item != NULL; item = g_slist_next(item))
    inf_gtk_io_watch_free((InfGtkIoWatch*)item->data);
  g_slist_free(priv->watches);

  for(item = priv->timeouts; item != NULL; item = g_slist_next(item))
    inf_gtk_io_timeout_free((InfGtkIoTimeout*)item->data);
  g_slist_free(priv->timeouts);

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
    cond |= G_IO_ERR;

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
  InfGtkIoWatch* watch;
  watch = (InfGtkIoWatch*)user_data;

  watch->func(
    watch->socket,
    inf_gtk_io_inf_events_from_glib_events(condition),
    watch->user_data
  );

  return TRUE;
}

static gboolean
inf_gtk_io_timeout_func(gpointer user_data)
{
  InfGtkIoTimeout* timeout;
  InfGtkIoPrivate* priv;

  timeout = (InfGtkIoTimeout*)user_data;
  priv = INF_GTK_IO_PRIVATE(timeout->io);
  timeout->id = 0; /* we return FALSE to stop the glib timeout */

  priv->timeouts = g_slist_remove(priv->timeouts, timeout); 

  timeout->func(timeout->user_data);
  inf_gtk_io_timeout_free(timeout);
  return FALSE;
}

static void
inf_gtk_io_io_watch(InfIo* io,
                    InfNativeSocket* socket,
                    InfIoEvent events,
                    InfIoFunc func,
                    gpointer user_data)
{
  InfGtkIoPrivate* priv;
  InfGtkIoWatch* watch;
  GIOChannel* channel;

  priv = INF_GTK_IO_PRIVATE(io);
  watch = inf_gtk_io_watch_lookup(INF_GTK_IO(io), socket);

  if(watch == NULL)
  {
    if(events != 0)
    {
      watch = inf_gtk_io_watch_new(socket, func, user_data);
      priv->watches = g_slist_prepend(priv->watches, watch);
    }
  }
  else
  {
    if(events != 0)
    {
      g_source_remove(watch->id);
      watch->func = func;
      watch->user_data = user_data;
    }
    else
    {
      inf_gtk_io_watch_free(watch);
      priv->watches = g_slist_remove(priv->watches, watch);
    }
  }

  if(events != 0)
  {
    channel = g_io_channel_unix_new(*socket);

    watch->id = g_io_add_watch(
      channel,
      inf_gtk_io_inf_events_to_glib_events(events),
      inf_gtk_io_watch_func,
      watch
    );

    g_io_channel_unref(channel);
  }
}

static gpointer
inf_gtk_io_io_add_timeout(InfIo* io,
                          guint msecs,
                          InfIoTimeoutFunc func,
                          gpointer user_data)
{
  InfGtkIoPrivate* priv;
  InfGtkIoTimeout* timeout;

  priv = INF_GTK_IO_PRIVATE(io);
  timeout = inf_gtk_io_timeout_new(INF_GTK_IO(io), func, user_data);
  timeout->id = g_timeout_add(msecs, inf_gtk_io_timeout_func, timeout);
  priv->timeouts = g_slist_prepend(priv->timeouts, timeout);

  return timeout;
}

static void
inf_gtk_io_io_remove_timeout(InfIo* io,
                             gpointer timeout_handle)
{
  InfGtkIoPrivate* priv;
  InfGtkIoTimeout* timeout;

  priv = INF_GTK_IO_PRIVATE(io);
  timeout = (InfGtkIoTimeout*)timeout_handle;
  g_assert(g_slist_find(priv->timeouts, timeout) != NULL);

  priv->timeouts = g_slist_remove(priv->timeouts, timeout);

  inf_gtk_io_timeout_free(timeout);
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

  iface->watch = inf_gtk_io_io_watch;
  iface->add_timeout = inf_gtk_io_io_add_timeout;
  iface->remove_timeout = inf_gtk_io_io_remove_timeout;
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

/** inf_gtk_io_new:
 *
 * Creates a new #InfGtkIo.
 **/
InfGtkIo*
inf_gtk_io_new(void)
{
  GObject* object;
  object = g_object_new(INF_GTK_TYPE_IO, NULL);
  return INF_GTK_IO(object);
}

/* vim:set et sw=2 ts=2: */
