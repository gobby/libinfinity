/* infinote - Collaborative notetaking application
 * Copyright (C) 2007-2010 Armin Burgmeier <armin@arbur.net>
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

#include <libinftextgtk/inf-text-gtk-view.h>
#include <libinfinity/inf-signals.h>

typedef struct _InfTextGtkViewUser InfTextGtkViewUser;
struct _InfTextGtkViewUser {
  InfTextGtkView* view;
  InfTextUser* user;
  gboolean cursor_visible;
  gpointer timeout_handle;

  GdkRectangle cursor_rect;
};

typedef struct _InfTextGtkViewPrivate InfTextGtkViewPrivate;
struct _InfTextGtkViewPrivate {
  InfIo* io;
  GtkTextView* textview;
  InfUserTable* user_table;
  InfTextUser* active_user;
  GSList* users;
};

enum {
  PROP_0,

  /* construct only */
  PROP_IO,
  PROP_VIEW,
  PROP_USER_TABLE,

  /* read/write */
  PROP_ACTIVE_USER
};

#define INF_TEXT_GTK_VIEW_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TEXT_GTK_TYPE_VIEW, InfTextGtkViewPrivate))

static GObjectClass* parent_class;

/* Converts from HSV to RGB */
/* TODO: Use gtk_hsv_to_rgb from GTK+ 2.14 instead */
static void
hsv_to_rgb(gdouble *h,
           gdouble *s,
           gdouble *v)
{
  gdouble hue, saturation, value;
  gdouble f, p, q, t;

  if (*s == 0.0)
  {
    *h = *v;
    *s = *v;
    *v = *v; /* heh */
  }
  else
  {
    hue = *h * 6.0;
    saturation = *s;
    value = *v;

    if (hue == 6.0)
      hue = 0.0;

    f = hue - (int) hue;
    p = value * (1.0 - saturation);
    q = value * (1.0 - saturation * f);
    t = value * (1.0 - saturation * (1.0 - f));

    switch ((int) hue)
    {
    case 0:
      *h = value;
      *s = t;
      *v = p;
      break;

    case 1:
      *h = q;
      *s = value;
      *v = p;
      break;

    case 2:
      *h = p;
      *s = value;
      *v = t;
      break;

    case 3:
      *h = p;
      *s = q;
      *v = value;
      break;

    case 4:
      *h = t;
      *s = p;
      *v = value;
      break;

    case 5:
      *h = value;
      *s = p;
      *v = q;
      break;

    default:
      g_assert_not_reached ();
    }
  }
}

/* Converts from RGB to HSV */
/* TODO: Use gtk_rgb_to_hsv from GTK+ 2.14 instead */
static void
rgb_to_hsv (gdouble *r,
            gdouble *g,
            gdouble *b)
{
  gdouble red, green, blue;
  gdouble h, s, v;
  gdouble min, max;
  gdouble delta;

  red = *r;
  green = *g;
  blue = *b;

  h = 0.0;

  if (red > green)
  {
    if (red > blue)
      max = red;
    else
      max = blue;

    if (green < blue)
      min = green;
    else
      min = blue;
  }
  else
  {
    if (green > blue)
      max = green;
    else
      max = blue;

    if (red < blue)
      min = red;
    else
      min = blue;
  }

  v = max;

  if (max != 0.0)
    s = (max - min) / max;
  else
    s = 0.0;

  if (s == 0.0)
    h = 0.0;
  else
  {
    delta = max - min;

    if (red == max)
      h = (green - blue) / delta;
    else if (green == max)
      h = 2 + (blue - red) / delta;
    else if (blue == max)
      h = 4 + (red - green) / delta;

    h /= 6.0;

    if (h < 0.0)
      h += 1.0;
    else if (h > 1.0)
      h -= 1.0;
  }

  *r = h;
  *g = s;
  *b = v;
}

static InfTextGtkViewUser*
inf_text_gtk_view_find_user(InfTextGtkView* view,
                            InfTextUser* user)
{
  InfTextGtkViewPrivate* priv;
  GSList* item;

  priv = INF_TEXT_GTK_VIEW_PRIVATE(view);

  for(item = priv->users; item != NULL; item = item->next)
    if( ((InfTextGtkViewUser*)item->data)->user == user)
      return (InfTextGtkViewUser*)item->data;

  return NULL;
}

static void
inf_text_gtk_view_user_compute_cursor_rect(InfTextGtkViewUser* view_user)
{
  InfTextGtkViewPrivate* priv;
  GtkTextIter iter;
  gfloat cursor_aspect_ratio;

  priv = INF_TEXT_GTK_VIEW_PRIVATE(view_user->view);

  gtk_widget_style_get(
    GTK_WIDGET(priv->textview),
    "cursor-aspect-ratio", &cursor_aspect_ratio,
    NULL
  );

  gtk_text_buffer_get_iter_at_offset(
    gtk_text_view_get_buffer(priv->textview),
    &iter,
    inf_text_user_get_caret_position(view_user->user)
  );

  gtk_text_view_get_iter_location(
    priv->textview,
    &iter,
    &view_user->cursor_rect
  );

  view_user->cursor_rect.width =
    MAX((int)(view_user->cursor_rect.height * cursor_aspect_ratio), 1);
}

static void
inf_text_gtk_view_user_invalidate_cursor_rect(InfTextGtkViewUser* view_user)
{
  InfTextGtkViewPrivate* priv;
  GdkWindow* window;

  priv = INF_TEXT_GTK_VIEW_PRIVATE(view_user->view);

  if(GTK_WIDGET_REALIZED(priv->textview))
  {
    window = gtk_text_view_get_window(priv->textview, GTK_TEXT_WINDOW_TEXT);
    gdk_window_invalidate_rect(window, &view_user->cursor_rect, FALSE);
  }
}

static gboolean
inf_text_gtk_view_expose_event_after_cb(GtkWidget* widget,
                                        GdkEventExpose* event,
                                        gpointer user_data)
{
  InfTextGtkView* view;
  InfTextGtkViewPrivate* priv;
  GdkColor* cursor_color;
  double h,s,v;
  GSList* item;
  InfTextGtkViewUser* view_user;
  cairo_t* cr;
  double r,g,b;
  GdkRectangle rct;

  view = INF_TEXT_GTK_VIEW(user_data);
  priv = INF_TEXT_GTK_VIEW_PRIVATE(view);

  if(gtk_text_view_get_window_type(priv->textview, event->window) !=
     GTK_TEXT_WINDOW_TEXT)
  {
    return FALSE;
  }

  gtk_widget_style_get (widget, "cursor-color", &cursor_color, NULL);
  if(cursor_color != NULL)
  {
    h = cursor_color->red / 65535.0;
    s = cursor_color->green / 65535.0;
    v = cursor_color->blue / 65535.0;
    gdk_color_free(cursor_color);
  }
  else
  {
    cursor_color = &widget->style->text[GTK_STATE_NORMAL];
    h = cursor_color->red / 65535.0;
    s = cursor_color->green / 65535.0;
    v = cursor_color->blue / 65535.0;
  }

  rgb_to_hsv(&h, &s, &v);

  s = MIN(MAX(s, 0.3), 0.8);
  v = MAX(v, 0.7);

  cr = gdk_cairo_create(event->window);

  for(item = priv->users; item != NULL; item = item->next)
  {
    view_user = (InfTextGtkViewUser*)item->data;
    if(view_user->cursor_visible)
    {
      gtk_text_view_buffer_to_window_coords(
        priv->textview,
        GTK_TEXT_WINDOW_TEXT,
        view_user->cursor_rect.x, view_user->cursor_rect.y,
        &rct.x, &rct.y
      );

      rct.width = view_user->cursor_rect.width;
      rct.height = view_user->cursor_rect.height;

      if(gdk_region_rect_in(event->region, &rct) != GDK_OVERLAP_RECTANGLE_OUT)
      {
        h = inf_text_user_get_hue(view_user->user);

        r = h;
        g = s;
        b = v;
        hsv_to_rgb(&r, &g, &b);

        cairo_set_source_rgb(cr, r, g, b);
        gdk_cairo_rectangle(cr, &rct);
        cairo_fill(cr);
      }
    }
  }

  cairo_destroy(cr);
  return FALSE;
}

static void
inf_text_gtk_view_style_set_cb(GtkWidget* widget,
                               GtkStyle* previous_style,
                               gpointer user_data)
{
  InfTextGtkView* view;
  InfTextGtkViewPrivate* priv;
  GSList* item;
  InfTextGtkViewUser* view_user;

  view = INF_TEXT_GTK_VIEW(user_data);
  priv = INF_TEXT_GTK_VIEW_PRIVATE(view);

  for(item = priv->users; item != NULL; item = item->next)
  {
    view_user = (InfTextGtkViewUser*)item->data;
    inf_text_gtk_view_user_compute_cursor_rect(view_user);
  }
}

static void
inf_text_gtk_view_size_allocate_cb(GtkWidget* widget,
                                   GtkAllocation* allocation,
                                   gpointer user_data)
{
  InfTextGtkView* view;
  InfTextGtkViewPrivate* priv;
  GSList* item;
  InfTextGtkViewUser* view_user;

  view = INF_TEXT_GTK_VIEW(user_data);
  priv = INF_TEXT_GTK_VIEW_PRIVATE(view);

  for(item = priv->users; item != NULL; item = item->next)
  {
    view_user = (InfTextGtkViewUser*)item->data;
    inf_text_gtk_view_user_compute_cursor_rect(view_user);
  }
}

static void
inf_text_gtk_view_user_cursor_blink_timeout_func(gpointer user_data)
{
  InfTextGtkViewUser* view_user;
  InfTextGtkViewPrivate* priv;
  GtkSettings* settings;
  gboolean cursor_blink;
  gint cursor_blink_time;
  gint cursor_blink_timeout;

  view_user = (InfTextGtkViewUser*)user_data;
  priv = INF_TEXT_GTK_VIEW_PRIVATE(view_user->view);

  view_user->cursor_visible = !view_user->cursor_visible;
  inf_text_gtk_view_user_invalidate_cursor_rect(view_user);

  /* Schedule next cursor blink */
  settings = gtk_widget_get_settings(GTK_WIDGET(priv->textview));

  /* TODO: Handle gtk-cursor-blink-timeout */
  g_object_get(
    G_OBJECT(settings),
    "gtk-cursor-blink", &cursor_blink,
    "gtk-cursor-blink-time", &cursor_blink_time,
    NULL
  );

  if(cursor_blink)
  {
    if(!view_user->cursor_visible)
      cursor_blink_time = cursor_blink_time * 1 / 3;
    else
      cursor_blink_time = cursor_blink_time * 2 / 3;

    view_user->timeout_handle = inf_io_add_timeout(
      priv->io,
      cursor_blink_time,
      inf_text_gtk_view_user_cursor_blink_timeout_func,
      view_user,
      NULL
    );
  }
  else
  {
    view_user->timeout_handle = NULL;
  }
}

static void
inf_text_gtk_view_user_reset_timeout(InfTextGtkViewUser* view_user)
{
  InfTextGtkViewPrivate* priv;
  GtkSettings* settings;
  gboolean cursor_blink;
  gint cursor_blink_time;

  priv = INF_TEXT_GTK_VIEW_PRIVATE(view_user->view);

  if(view_user->timeout_handle)
  {
    inf_io_remove_timeout(priv->io, view_user->timeout_handle);
    view_user->timeout_handle = NULL;
  }

  if(!view_user->cursor_visible)
  {
    view_user->cursor_visible = TRUE;
    inf_text_gtk_view_user_invalidate_cursor_rect(view_user);
  }

  settings = gtk_widget_get_settings(GTK_WIDGET(priv->textview));

  /* TODO: Reschedule timeout if these settings change */
  g_object_get(
    G_OBJECT(settings),
    "gtk-cursor-blink", &cursor_blink,
    "gtk-cursor-blink-time", &cursor_blink_time,
    NULL
  );

  if(cursor_blink)
  {
    view_user->timeout_handle = inf_io_add_timeout(
      priv->io,
      cursor_blink_time,
      inf_text_gtk_view_user_cursor_blink_timeout_func,
      view_user,
      NULL
    );
  }
}

static void
inf_text_gtk_view_user_selection_changed_cb(InfTextUser* user,
                                            guint position,
                                            gint length,
                                            gpointer user_data)
{
  InfTextGtkViewUser* view_user;
  InfTextGtkViewPrivate* priv;

  view_user = (InfTextGtkViewUser*)user_data;
  priv = INF_TEXT_GTK_VIEW_PRIVATE(view_user->view);

  inf_text_gtk_view_user_invalidate_cursor_rect(view_user);

  inf_text_gtk_view_user_compute_cursor_rect(view_user);
  inf_text_gtk_view_user_reset_timeout(view_user);

  inf_text_gtk_view_user_invalidate_cursor_rect(view_user);
}

static void
inf_text_gtk_view_user_notify_hue_cb(GObject* object,
                                     GParamSpec* pspec,
                                     gpointer user_data)
{
  InfTextGtkViewUser* view_user;
  InfTextGtkViewPrivate* priv;

  view_user = (InfTextGtkViewUser*)user_data;
  priv = INF_TEXT_GTK_VIEW_PRIVATE(view_user->view);

  /* TODO: Might restrict this on current lines,
   * cursor rects and selection rects */
  gtk_widget_queue_draw(GTK_WIDGET(priv->textview));
}

static void
inf_text_gtk_view_add_user(InfTextGtkView* view,
                           InfTextUser* user)
{
  InfTextGtkViewPrivate* priv;
  InfTextGtkViewUser* view_user;

  priv = INF_TEXT_GTK_VIEW_PRIVATE(view);
  view_user = g_slice_new(InfTextGtkViewUser);

  view_user->view = view;
  view_user->user = INF_TEXT_USER(user);
  view_user->cursor_visible = TRUE;
  view_user->timeout_handle = NULL;
  inf_text_gtk_view_user_compute_cursor_rect(view_user);
  inf_text_gtk_view_user_reset_timeout(view_user);
  priv->users = g_slist_prepend(priv->users, view_user);

  g_signal_connect_after(
    user,
    "selection-changed",
    G_CALLBACK(inf_text_gtk_view_user_selection_changed_cb),
    view_user
  );

  g_signal_connect(
    user,
    "notify::hue",
    G_CALLBACK(inf_text_gtk_view_user_notify_hue_cb),
    view_user
  );

  inf_text_gtk_view_user_invalidate_cursor_rect(view_user);
}

static void
inf_text_gtk_view_remove_user(InfTextGtkViewUser* view_user)
{
  InfTextGtkViewPrivate* priv;
  priv = INF_TEXT_GTK_VIEW_PRIVATE(view_user->view);

  inf_signal_handlers_disconnect_by_func(
    view_user->user,
    G_CALLBACK(inf_text_gtk_view_user_selection_changed_cb),
    view_user
  );

  inf_signal_handlers_disconnect_by_func(
    view_user->user,
    G_CALLBACK(inf_text_gtk_view_user_notify_hue_cb),
    view_user
  );

  if(view_user->timeout_handle != NULL)
    inf_io_remove_timeout(priv->io, view_user->timeout_handle);

  inf_text_gtk_view_user_invalidate_cursor_rect(view_user);

  priv->users = g_slist_remove(priv->users, view_user);
  g_slice_free(InfTextGtkViewUser, view_user);
}

static void
inf_text_gtk_view_user_notify_status_cb(GObject* object,
                                        GParamSpec* pspec,
                                        gpointer user_data)
{
  InfTextGtkView* view;
  InfTextGtkViewPrivate* priv;
  InfTextUser* user;
  InfTextGtkViewUser* view_user;

  view = INF_TEXT_GTK_VIEW(user_data);
  priv = INF_TEXT_GTK_VIEW_PRIVATE(view);
  user = INF_TEXT_USER(object);

  g_assert(user != priv->active_user);
  view_user = inf_text_gtk_view_find_user(view, user);

  if(inf_user_get_status(INF_USER(user)) == INF_USER_ACTIVE)
  {
    if(!view_user)
      inf_text_gtk_view_add_user(view, user);
  }
  else
  {
    if(view_user)
      inf_text_gtk_view_remove_user(view_user);
  }
}

static void
inf_text_gtk_view_user_removed(InfTextGtkView* view,
                               InfTextUser* user)
{
  InfTextGtkViewPrivate* priv;
  InfTextGtkViewUser* view_user;

  priv = INF_TEXT_GTK_VIEW_PRIVATE(view);

  if(user == priv->active_user)
  {
    priv->active_user = NULL;
    g_object_notify(G_OBJECT(view), "active-user");
  }
  else
  {
    inf_signal_handlers_disconnect_by_func(
      user,
      G_CALLBACK(inf_text_gtk_view_user_notify_status_cb),
      view
    );

    if(inf_user_get_status(INF_USER(user)) == INF_USER_ACTIVE)
    {
      view_user = inf_text_gtk_view_find_user(view, user);
      g_assert(view_user != NULL);

      inf_text_gtk_view_remove_user(view_user);
    }
  }
}

static void
inf_text_gtk_view_user_added(InfTextGtkView* view,
                             InfTextUser* user)
{
  InfTextGtkViewPrivate* priv;
  priv = INF_TEXT_GTK_VIEW_PRIVATE(view);

  /* Active user is guaranteed to be contained in user table, so if user was
   * just added then it can't be set as active user already. */
  g_assert(user != priv->active_user);

  g_signal_connect(
    user,
    "notify::status",
    G_CALLBACK(inf_text_gtk_view_user_notify_status_cb),
    view
  );

  if(inf_user_get_status(INF_USER(user)) == INF_USER_ACTIVE)
    inf_text_gtk_view_add_user(view, user);
}

static void
inf_text_gtk_view_add_user_cb(InfUserTable* user_table,
                              InfUser* user,
                              gpointer user_data)
{
  InfTextGtkView* view = INF_TEXT_GTK_VIEW(user_data);
  g_assert(INF_TEXT_IS_USER(user));
  inf_text_gtk_view_user_added(view, INF_TEXT_USER(user));
}

static void
inf_text_gtk_view_remove_user_cb(InfUserTable* user_table,
                                 InfUser* user,
                                 gpointer user_data)
{
  InfTextGtkView* view = INF_TEXT_GTK_VIEW(user_data);
  g_assert(INF_TEXT_IS_USER(user));
  inf_text_gtk_view_user_removed(view, INF_TEXT_USER(user));
}

static void
inf_text_gtk_view_set_user_table_foreach_new_user_func(InfUser* user,
                                                       gpointer user_data)
{
  InfTextGtkView* view = INF_TEXT_GTK_VIEW(user_data);
  g_assert(INF_TEXT_IS_USER(user));
  inf_text_gtk_view_user_added(view, INF_TEXT_USER(user));
}

static void
inf_text_gtk_view_set_user_table_foreach_old_user_func(InfUser* user,
                                                       gpointer user_data)
{
  InfTextGtkView* view = INF_TEXT_GTK_VIEW(user_data);
  g_assert(INF_TEXT_IS_USER(user));
  inf_text_gtk_view_user_removed(view, INF_TEXT_USER(user));
}

static void
inf_text_gtk_view_set_view(InfTextGtkView* view,
                           GtkTextView* gtk_view)
{
  InfTextGtkViewPrivate* priv;
  priv = INF_TEXT_GTK_VIEW_PRIVATE(view);

  if(priv->textview != NULL)
  {
    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->textview),
      G_CALLBACK(inf_text_gtk_view_expose_event_after_cb),
      view
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->textview),
      G_CALLBACK(inf_text_gtk_view_style_set_cb),
      view
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->textview),
      G_CALLBACK(inf_text_gtk_view_size_allocate_cb),
      view
    );

    g_object_unref(priv->textview);
  }

  priv->textview = gtk_view;

  if(gtk_view != NULL)
  {
    g_object_ref(gtk_view);

    g_signal_connect_after(
      G_OBJECT(gtk_view),
      "expose-event",
      G_CALLBACK(inf_text_gtk_view_expose_event_after_cb),
      view
    );

    g_signal_connect_after(
      G_OBJECT(gtk_view),
      "style-set",
      G_CALLBACK(inf_text_gtk_view_style_set_cb),
      view
    );

    /* This is required for the remote cursors showing up at the correct
     * position initially. Maybe gtk_text_view_get_iter_location() seems to
     * return junk before. Note that also style-set is not enough. */
    g_signal_connect_after(
      G_OBJECT(gtk_view),
      "size-allocate",
      G_CALLBACK(inf_text_gtk_view_size_allocate_cb),
      view
    );
  }

  g_object_notify(G_OBJECT(view), "view");
}

static void
inf_text_gtk_view_set_user_table(InfTextGtkView* view,
                                 InfUserTable* user_table)
{
  InfTextGtkViewPrivate* priv;
  priv = INF_TEXT_GTK_VIEW_PRIVATE(view);

  if(priv->user_table != NULL)
  {
    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->user_table),
      G_CALLBACK(inf_text_gtk_view_add_user_cb),
      view
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->user_table),
      G_CALLBACK(inf_text_gtk_view_remove_user_cb),
      view
    );

    inf_user_table_foreach_user(
      priv->user_table,
      inf_text_gtk_view_set_user_table_foreach_old_user_func,
      view
    );

    g_object_unref(priv->user_table);
  }

  priv->user_table = user_table;

  if(user_table != NULL)
  {
    g_object_ref(user_table);

    g_signal_connect(
      G_OBJECT(user_table),
      "add-user",
      G_CALLBACK(inf_text_gtk_view_add_user_cb),
      view
    );

    g_signal_connect(
      G_OBJECT(user_table),
      "remove-user",
      G_CALLBACK(inf_text_gtk_view_remove_user_cb),
      view
    );

    inf_user_table_foreach_user(
      user_table,
      inf_text_gtk_view_set_user_table_foreach_new_user_func,
      view
    );
  }

  g_object_notify(G_OBJECT(view), "user-table");
}

static void
inf_text_gtk_view_init(GTypeInstance* instance,
                         gpointer g_class)
{
  InfTextGtkView* view;
  InfTextGtkViewPrivate* priv;

  view = INF_TEXT_GTK_VIEW(instance);
  priv = INF_TEXT_GTK_VIEW_PRIVATE(view);

  priv->io = NULL;
  priv->textview = NULL;
  priv->user_table = NULL;
  priv->active_user = NULL;
  priv->users = NULL;
}

static void
inf_text_gtk_view_dispose(GObject* object)
{
  InfTextGtkView* view;
  InfTextGtkViewPrivate* priv;

  view = INF_TEXT_GTK_VIEW(object);
  priv = INF_TEXT_GTK_VIEW_PRIVATE(view);

  inf_text_gtk_view_set_view(view, NULL);
  inf_text_gtk_view_set_user_table(view, NULL);

  g_assert(priv->active_user == NULL);
  g_assert(priv->users == NULL);

  if(priv->io != NULL)
  {
    g_object_unref(priv->io);
    priv->io = NULL;
  }

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_text_gtk_view_set_property(GObject* object,
                                 guint prop_id,
                                 const GValue* value,
                                 GParamSpec* pspec)
{
  InfTextGtkView* view;
  InfTextGtkViewPrivate* priv;

  view = INF_TEXT_GTK_VIEW(object);
  priv = INF_TEXT_GTK_VIEW_PRIVATE(view);

  switch(prop_id)
  {
  case PROP_IO:
    g_assert(priv->io == NULL); /* construct only */
    priv->io = INF_IO(g_value_dup_object(value));
    break;
  case PROP_VIEW:
    g_assert(priv->textview == NULL); /* construct only */

    inf_text_gtk_view_set_view(
      view,
      GTK_TEXT_VIEW(g_value_get_object(value))
    );

    break;
  case PROP_USER_TABLE:
    g_assert(priv->user_table == NULL); /* construct/only */

    inf_text_gtk_view_set_user_table(
      view,
      INF_USER_TABLE(g_value_get_object(value))
    );

    break;
  case PROP_ACTIVE_USER:
    inf_text_gtk_view_set_active_user(
      view,
      INF_TEXT_USER(g_value_get_object(value))
    );

    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(value, prop_id, pspec);
    break;
  }
}

static void
inf_text_gtk_view_get_property(GObject* object,
                                 guint prop_id,
                                 GValue* value,
                                 GParamSpec* pspec)
{
  InfTextGtkView* view;
  InfTextGtkViewPrivate* priv;

  view = INF_TEXT_GTK_VIEW(object);
  priv = INF_TEXT_GTK_VIEW_PRIVATE(view);

  switch(prop_id)
  {
  case PROP_IO:
    g_value_set_object(value, G_OBJECT(priv->io));
    break;
  case PROP_VIEW:
    g_value_set_object(value, G_OBJECT(priv->textview));
    break;
  case PROP_USER_TABLE:
    g_value_set_object(value, G_OBJECT(priv->user_table));
    break;
  case PROP_ACTIVE_USER:
    g_value_set_object(value, G_OBJECT(priv->active_user));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_text_gtk_view_class_init(gpointer g_class,
                               gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfTextGtkViewPrivate));

  object_class->dispose = inf_text_gtk_view_dispose;
  object_class->set_property = inf_text_gtk_view_set_property;
  object_class->get_property = inf_text_gtk_view_get_property;

  g_object_class_install_property(
    object_class,
    PROP_IO,
    g_param_spec_object(
      "io",
      "IO",
      "The IO object to schedule timeouts",
      INF_TYPE_IO,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_VIEW,
    g_param_spec_object(
      "view",
      "View",
      "The underlying GtkTextView",
      GTK_TYPE_TEXT_VIEW,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_USER_TABLE,
    g_param_spec_object(
      "user-table",
      "User table",
      "The user table containing the users of the session shown in the view",
      INF_TYPE_USER_TABLE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_ACTIVE_USER,
    g_param_spec_object(
      "active-user",
      "Active user",
      "The user for which to show the view",
      INF_TEXT_TYPE_USER,
      G_PARAM_READWRITE
    )
  );
}

GType
inf_text_gtk_view_get_type(void)
{
  static GType view_type = 0;

  if(!view_type)
  {
    static const GTypeInfo view_type_info = {
      sizeof(InfTextGtkViewClass),    /* class_size */
      NULL,                           /* base_init */
      NULL,                           /* base_finalize */
      inf_text_gtk_view_class_init,   /* class_init */
      NULL,                           /* class_finalize */
      NULL,                           /* class_data */
      sizeof(InfTextGtkView),         /* instance_size */
      0,                              /* n_preallocs */
      inf_text_gtk_view_init,         /* instance_init */
      NULL                            /* value_table */
    };

    view_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfTextGtkView",
      &view_type_info,
      0
    );
  }

  return view_type;
}

/**
 * inf_text_gtk_view_new:
 * @io: A #InfIo.
 * @view: A #GtkTextView.
 * @user_table: The #InfUserTable for the text session displayed in @view.
 *
 * Creates a new #InfTextGtkView for @view. This draws remote user's cursors
 * and selections into the text view.
 *
 * Returns: A new #InfTextGtkView.
 */
InfTextGtkView*
inf_text_gtk_view_new(InfIo* io,
                      GtkTextView* view,
                      InfUserTable* user_table)
{
  GObject* object;

  g_return_val_if_fail(INF_IS_IO(io), NULL);
  g_return_val_if_fail(GTK_IS_TEXT_VIEW(view), NULL);
  g_return_val_if_fail(INF_IS_USER_TABLE(user_table), NULL);

  object = g_object_new(
    INF_TEXT_GTK_TYPE_VIEW,
    "io", io,
    "view", view,
    "user-table", user_table,
    NULL
  );

  return INF_TEXT_GTK_VIEW(object);
}

/**
 * inf_text_gtk_view_get_text_view:
 * @view: A #InfTextGtkView.
 *
 * Returns the underlying #GtkTextView.
 *
 * Return Value: The #InfTextGtkView's #GtkTextView.
 **/
GtkTextView*
inf_text_gtk_view_get_text_view(InfTextGtkView* view)
{
  g_return_val_if_fail(INF_TEXT_GTK_IS_VIEW(view), NULL);
  return INF_TEXT_GTK_VIEW_PRIVATE(view)->textview;
}

/**
 * inf_text_gtk_view_get_user_table:
 * @view: A #InfTextGtkView.
 *
 * Returns the #InfUserTable containing the users of the session the
 * #InfTextGtkView's #GtkTextView is displaying.
 *
 * Returns: The #InfGtkTextView's #InfUserTable.
 */
InfUserTable*
inf_text_gtk_view_get_user_table(InfTextGtkView* view)
{
  g_return_val_if_fail(INF_TEXT_GTK_IS_VIEW(view), NULL);
  return INF_TEXT_GTK_VIEW_PRIVATE(view)->user_table;
}

/**
 * inf_text_gtk_view_set_active_user:
 * @view: A #InfTextGtkView.
 * @user: A user from @view's user table, or %NULL.
 *
 * Sets the user for which perspective to draw the view. The selection and
 * cursor position is not drawn for this user since it is assumed that the
 * view's buffer cursor position and selection match the active user ones
 * (which is automatically the case if the buffer is managed by a
 * #InfTextGtkBuffer).
 */
void
inf_text_gtk_view_set_active_user(InfTextGtkView* view,
                                  InfTextUser* user)
{
  InfTextGtkViewPrivate* priv;
  InfTextUser* active_user;
  InfTextGtkViewUser* view_user;

  g_return_if_fail(INF_TEXT_GTK_IS_VIEW(view));
  g_return_if_fail(user == NULL || INF_TEXT_IS_USER(user));

  priv = INF_TEXT_GTK_VIEW_PRIVATE(view);
  g_return_if_fail(
    user == NULL ||
    inf_user_table_lookup_user_by_id(
      priv->user_table,
      inf_user_get_id(INF_USER(user))
    ) == INF_USER(user)
  );

  if(priv->active_user != NULL)
  {
    active_user = priv->active_user;
    priv->active_user = NULL;

    inf_text_gtk_view_user_added(view, active_user);
  }

  if(user != NULL)
  {
    inf_text_gtk_view_user_removed(view, user);
  }

  priv->active_user = user;
  g_object_notify(G_OBJECT(view), "active-user");
}

/**
 * inf_text_gtk_view_get_active_user:
 * @view: A #InfTextGtkView.
 *
 * Returns the active user of @view. See inf_text_gtk_view_set_active_user().
 *
 * Returns: The active user of @view.
 */
InfTextUser*
inf_text_gtk_view_get_active_user(InfTextGtkView* view)
{
  g_return_val_if_fail(INF_TEXT_GTK_IS_VIEW(view), NULL);
  return INF_TEXT_GTK_VIEW_PRIVATE(view)->active_user;
}

/* vim:set et sw=2 ts=2: */
