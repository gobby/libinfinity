/* infinote - Collaborative notetaking application
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
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <libinftextgtk/inf-text-gtk-viewport.h>
#include <libinfinity/inf-signals.h>

#include <gtk/gtk.h>

typedef struct _InfTextGtkViewportUser InfTextGtkViewportUser;
struct _InfTextGtkViewportUser {
  InfTextGtkViewport* viewport;
  InfTextUser* user;
  GdkRectangle rectangle;
};

typedef struct _InfTextGtkViewportPrivate InfTextGtkViewportPrivate;
struct _InfTextGtkViewportPrivate {
  GtkScrolledWindow* scroll;
  InfUserTable* user_table;
  InfTextUser* active_user;
  GSList* users;

  gboolean show_user_markers;
};

enum {
  PROP_0,

  /* construct only */
  PROP_SCROLLED_WINDOW,
  PROP_USER_TABLE,

  /* read/write */
  PROP_ACTIVE_USER,
  PROP_SHOW_USER_MARKERS
};

#define INF_TEXT_GTK_VIEWPORT_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TEXT_GTK_TYPE_VIEWPORT, InfTextGtkViewportPrivate))

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

static InfTextGtkViewportUser*
inf_text_gtk_viewport_find_user(InfTextGtkViewport* viewport,
                                InfTextUser* user)
{
  InfTextGtkViewportPrivate* priv;
  GSList* item;

  priv = INF_TEXT_GTK_VIEWPORT_PRIVATE(viewport);

  for(item = priv->users; item != NULL; item = item->next)
    if( ((InfTextGtkViewportUser*)item->data)->user == user)
      return (InfTextGtkViewportUser*)item->data;

  return NULL;
}

static void
inf_text_gtk_viewport_user_compute_user_area(InfTextGtkViewportUser* user)
{
  InfTextGtkViewportPrivate* priv;
  GtkWidget* textview;
  GtkWidget* scrollbar;
  GtkTextIter iter;
  GdkRectangle rect;
  gint y;
  gint end_y;

  gint scroll_height;
  gint slider_size;
  gint stepper_size;
  gint stepper_spacing;
  gint border;
  GdkRectangle allocation;
  gint scroll_ox;
  gint scroll_oy;
  gint dy;

  priv = INF_TEXT_GTK_VIEWPORT_PRIVATE(user->viewport);

  /* TODO: We might want to skip this if show-user-markers is false. */

  textview = gtk_bin_get_child(GTK_BIN(priv->scroll));
  scrollbar = gtk_scrolled_window_get_vscrollbar(priv->scroll);
#if GTK_CHECK_VERSION(2,20,0)
  if(GTK_IS_TEXT_VIEW(textview) && scrollbar != NULL &&
     gtk_widget_get_realized(textview))
#else
  if(GTK_IS_TEXT_VIEW(textview) && scrollbar != NULL &&
     GTK_WIDGET_REALIZED(textview))
#endif
  {
    gtk_text_buffer_get_iter_at_offset(
      gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview)),
      &iter,
      inf_text_user_get_caret_position(user->user)
    );

    gtk_text_view_get_iter_location(GTK_TEXT_VIEW(textview), &iter, &rect);
    y = rect.y;

    gtk_text_buffer_get_end_iter(
      gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview)),
      &iter
    );

    gtk_text_view_get_iter_location(GTK_TEXT_VIEW(textview), &iter, &rect);
    end_y = rect.y;

    g_assert(end_y > 0 || y == 0);

    gtk_widget_style_get(
      scrollbar,
      "slider-width", &slider_size,
      "stepper-size", &stepper_size,
      "stepper-spacing", &stepper_spacing,
      "trough-border", &border,
      NULL
    );

#if GTK_CHECK_VERSION(2,18,0)
    gtk_widget_get_allocation(scrollbar, &allocation);
#else
    allocation = scrollbar->allocation;
#endif

    scroll_ox = border;
    scroll_oy = border + stepper_size + stepper_spacing;
    scroll_height = allocation.height - 2*scroll_oy;

    if(end_y > 0)
      y = y * scroll_height / end_y;

    user->rectangle.x = scroll_ox + allocation.x;
    user->rectangle.y = scroll_oy + allocation.y + y - slider_size/3;
    user->rectangle.width = slider_size;
    user->rectangle.height = slider_size*2/3;

    if(user->rectangle.y < scroll_oy + allocation.y)
    {
      dy = scroll_oy + allocation.y - user->rectangle.y;
      user->rectangle.y += dy;
      user->rectangle.height -= dy;
    }

    if(user->rectangle.y + user->rectangle.height >
       scroll_oy + allocation.y + scroll_height)
    {
      user->rectangle.height =
        scroll_oy + allocation.y + scroll_height - user->rectangle.y;
    }
  }
  else
  {
    user->rectangle.x = user->rectangle.y = 0;
    user->rectangle.width = user->rectangle.height = 0;
  }
}

static void
inf_text_gtk_viewport_user_invalidate_user_area(InfTextGtkViewportUser* user)
{
  InfTextGtkViewportPrivate* priv;
  priv = INF_TEXT_GTK_VIEWPORT_PRIVATE(user->viewport);

  if(priv->show_user_markers &&
     user->rectangle.width > 0 && user->rectangle.height > 0)
  {
    gtk_widget_queue_draw_area(
      gtk_scrolled_window_get_vscrollbar(priv->scroll),
      user->rectangle.x,
      user->rectangle.y,
      user->rectangle.width,
      user->rectangle.height
    );
  }
}

static gboolean
#if GTK_CHECK_VERSION(2, 91, 0)
inf_text_gtk_viewport_scrollbar_draw_cb(GtkWidget* scrollbar,
                                        cairo_t* cr,
                                        gpointer user_data)
#else
inf_text_gtk_viewport_scrollbar_expose_event_cb(GtkWidget* scrollbar,
                                                GdkEventExpose* event,
                                                gpointer user_data)
#endif
{
  InfTextGtkViewport* viewport;
  InfTextGtkViewportPrivate* priv;
  InfTextGtkViewportUser* viewport_user;
  GdkRectangle* rectangle;
  GdkColor* color;
  double h,s,v;
  double r,g,b;
  GSList* item;
  double line_width;

#if GTK_CHECK_VERSION(2, 91, 0)
  GdkRectangle clip_area;
#else
  cairo_t* cr;
#endif

  viewport = INF_TEXT_GTK_VIEWPORT(user_data);
  priv = INF_TEXT_GTK_VIEWPORT_PRIVATE(viewport);

  /* Can this happen? */
#if GTK_CHECK_VERSION(2, 91, 0)
  if(!gtk_cairo_should_draw_window(cr, gtk_widget_get_window(scrollbar)))
#elif GTK_CHECK_VERSION(2,14,0)
  if(event->window != gtk_widget_get_window(scrollbar))
#else
  if(event->window != GTK_WIDGET(scrollbar)->window)
#endif
    return FALSE;

  if(priv->show_user_markers)
  {
    color = &gtk_widget_get_style(scrollbar)->bg[GTK_STATE_NORMAL];
    h = color->red / 65535.0;
    s = color->green / 65535.0;
    v = color->blue / 65535.0;
    rgb_to_hsv(&h, &s, &v);
    s = MIN(MAX(s, 0.5), 0.8);
    v = MAX(v, 0.5);

#if GTK_CHECK_VERSION(2, 91, 0)
    gtk_cairo_transform_to_window(
      cr,
      GTK_WIDGET(scrollbar),
      gtk_widget_get_window(scrollbar)
    );

    gdk_cairo_get_clip_rectangle(cr, &clip_area);
#else
    cr = gdk_cairo_create(event->window);
#endif

    line_width = cairo_get_line_width(cr);
    for(item = priv->users; item != NULL; item = item->next)
    {
      viewport_user = (InfTextGtkViewportUser*)item->data;
      rectangle = &viewport_user->rectangle;

#if GTK_CHECK_VERSION(2, 91, 0)
      if(gdk_rectangle_intersect(&clip_area, rectangle, NULL))
#elif GTK_CHECK_VERSION(2,90,5)
      if(cairo_region_contains_rectangle(event->region, rectangle) !=
         CAIRO_REGION_OVERLAP_OUT)
#else
      if(gdk_region_rect_in(event->region, rectangle) !=
         GDK_OVERLAP_RECTANGLE_OUT)
#endif
      {
        h = inf_text_user_get_hue(viewport_user->user);

        cairo_rectangle(
          cr,
          rectangle->x + line_width/2,
          rectangle->y + line_width/2,
          rectangle->width - line_width,
          rectangle->height - line_width
        );

        r = h; g = s; b = v/2.0;
        hsv_to_rgb(&r, &g, &b);
        cairo_set_source_rgba(cr, r, g, b, 0.6);
        cairo_stroke_preserve(cr);

        r = h; g = s; b = v;
        hsv_to_rgb(&r, &g, &b);
        cairo_set_source_rgba(cr, r, g, b, 0.6);
        cairo_fill(cr);
      }
    }

#if ! GTK_CHECK_VERSION(2, 91, 0)
    cairo_destroy(cr);
#endif
  }

  return FALSE;
}

static void
inf_text_gtk_viewport_scrollbar_size_allocate_cb(GtkWidget* scrollbar,
                                                 GtkAllocation* allocation,
                                                 gpointer user_data)
{
  InfTextGtkViewport* viewport;
  InfTextGtkViewportPrivate* priv;
  GSList* item;
  InfTextGtkViewportUser* viewport_user;

  viewport = INF_TEXT_GTK_VIEWPORT(user_data);
  priv = INF_TEXT_GTK_VIEWPORT_PRIVATE(viewport);

  for(item = priv->users; item != NULL; item = item->next)
  {
    viewport_user = (InfTextGtkViewportUser*)item->data;
    inf_text_gtk_viewport_user_invalidate_user_area(viewport_user);
    inf_text_gtk_viewport_user_compute_user_area(viewport_user);
    inf_text_gtk_viewport_user_invalidate_user_area(viewport_user);
  }
}

static void
inf_text_gtk_viewport_adjustment_changed_cb(GtkAdjustment* adjustment,
                                            gpointer user_data)
{
  InfTextGtkViewport* viewport;
  InfTextGtkViewportPrivate* priv;
  GSList* item;
  InfTextGtkViewportUser* viewport_user;

  viewport = INF_TEXT_GTK_VIEWPORT(user_data);
  priv = INF_TEXT_GTK_VIEWPORT_PRIVATE(viewport);

  for(item = priv->users; item != NULL; item = item->next)
  {
    viewport_user = (InfTextGtkViewportUser*)item->data;
    inf_text_gtk_viewport_user_invalidate_user_area(viewport_user);
    inf_text_gtk_viewport_user_compute_user_area(viewport_user);
    inf_text_gtk_viewport_user_invalidate_user_area(viewport_user);
  }
}

static void
inf_text_gtk_viewport_scrollbar_style_set_cb(GtkWidget* scrollbar,
                                             GtkStyle* prev_style,
                                             gpointer user_data)
{
  InfTextGtkViewport* viewport;
  InfTextGtkViewportPrivate* priv;
  GSList* item;
  InfTextGtkViewportUser* viewport_user;

  viewport = INF_TEXT_GTK_VIEWPORT(user_data);
  priv = INF_TEXT_GTK_VIEWPORT_PRIVATE(viewport);

  for(item = priv->users; item != NULL; item = item->next)
  {
    viewport_user = (InfTextGtkViewportUser*)item->data;
    inf_text_gtk_viewport_user_invalidate_user_area(viewport_user);
    inf_text_gtk_viewport_user_compute_user_area(viewport_user);
    inf_text_gtk_viewport_user_invalidate_user_area(viewport_user);
  }
}

static void
inf_text_gtk_viewport_user_selection_changed_cb(InfTextUser* user,
                                                guint position,
                                                gint length,
                                                gboolean by_request,
                                                gpointer user_data)
{
  InfTextGtkViewportUser* viewport_user;
  InfTextGtkViewportPrivate* priv;

  viewport_user = (InfTextGtkViewportUser*)user_data;
  priv = INF_TEXT_GTK_VIEWPORT_PRIVATE(viewport_user->viewport);

  /* TODO: Just invalidate the region that really changed, by comparing
   * old and new rectangle's coordinates */
  inf_text_gtk_viewport_user_invalidate_user_area(viewport_user);

  /* Recompute and revalidate */
  inf_text_gtk_viewport_user_compute_user_area(viewport_user);
  inf_text_gtk_viewport_user_invalidate_user_area(viewport_user);
}

static void
inf_text_gtk_viewport_user_notify_hue_cb(GObject* object,
                                         GParamSpec* pspec,
                                         gpointer user_data)
{
  InfTextGtkViewportUser* viewport_user;
  InfTextGtkViewportPrivate* priv;

  viewport_user = (InfTextGtkViewportUser*)user_data;
  priv = INF_TEXT_GTK_VIEWPORT_PRIVATE(viewport_user->viewport);

  inf_text_gtk_viewport_user_invalidate_user_area(viewport_user);
}

static void
inf_text_gtk_viewport_add_user(InfTextGtkViewport* viewport,
                              InfTextUser* user)
{
  InfTextGtkViewportPrivate* priv;
  InfTextGtkViewportUser* viewport_user;

  priv = INF_TEXT_GTK_VIEWPORT_PRIVATE(viewport);
  viewport_user = g_slice_new(InfTextGtkViewportUser);

  viewport_user->viewport = viewport;
  viewport_user->user = INF_TEXT_USER(user);
  priv->users = g_slist_prepend(priv->users, viewport_user);

  inf_text_gtk_viewport_user_compute_user_area(viewport_user);

  g_signal_connect_after(
    user,
    "selection-changed",
    G_CALLBACK(inf_text_gtk_viewport_user_selection_changed_cb),
    viewport_user
  );

  g_signal_connect(
    user,
    "notify::hue",
    G_CALLBACK(inf_text_gtk_viewport_user_notify_hue_cb),
    viewport_user
  );

  inf_text_gtk_viewport_user_invalidate_user_area(viewport_user);
}

static void
inf_text_gtk_viewport_remove_user(InfTextGtkViewportUser* viewport_user)
{
  InfTextGtkViewportPrivate* priv;
  priv = INF_TEXT_GTK_VIEWPORT_PRIVATE(viewport_user->viewport);

  inf_text_gtk_viewport_user_invalidate_user_area(viewport_user);

  inf_signal_handlers_disconnect_by_func(
    viewport_user->user,
    G_CALLBACK(inf_text_gtk_viewport_user_selection_changed_cb),
    viewport_user
  );

  inf_signal_handlers_disconnect_by_func(
    viewport_user->user,
    G_CALLBACK(inf_text_gtk_viewport_user_notify_hue_cb),
    viewport_user
  );

  priv->users = g_slist_remove(priv->users, viewport_user);
  g_slice_free(InfTextGtkViewportUser, viewport_user);
}

static void
inf_text_gtk_viewport_user_notify_status_cb(GObject* object,
                                            GParamSpec* pspec,
                                            gpointer user_data)
{
  InfTextGtkViewport* viewport;
  InfTextGtkViewportPrivate* priv;
  InfTextUser* user;
  InfTextGtkViewportUser* viewport_user;

  viewport = INF_TEXT_GTK_VIEWPORT(user_data);
  priv = INF_TEXT_GTK_VIEWPORT_PRIVATE(viewport);
  user = INF_TEXT_USER(object);

  g_assert(user != priv->active_user);
  viewport_user = inf_text_gtk_viewport_find_user(viewport, user);

  if(inf_user_get_status(INF_USER(user)) == INF_USER_ACTIVE)
  {
    if(!viewport_user)
      inf_text_gtk_viewport_add_user(viewport, user);
  }
  else
  {
    if(viewport_user)
      inf_text_gtk_viewport_remove_user(viewport_user);
  }
}

static void
inf_text_gtk_viewport_user_removed(InfTextGtkViewport* viewport,
                                   InfTextUser* user)
{
  InfTextGtkViewportPrivate* priv;
  InfTextGtkViewportUser* viewport_user;

  priv = INF_TEXT_GTK_VIEWPORT_PRIVATE(viewport);

  if(user == priv->active_user)
  {
    priv->active_user = NULL;
    g_object_notify(G_OBJECT(viewport), "active-user");
  }
  else
  {
    inf_signal_handlers_disconnect_by_func(
      user,
      G_CALLBACK(inf_text_gtk_viewport_user_notify_status_cb),
      viewport
    );

    if(inf_user_get_status(INF_USER(user)) == INF_USER_ACTIVE)
    {
      viewport_user = inf_text_gtk_viewport_find_user(viewport, user);
      g_assert(viewport_user != NULL);

      inf_text_gtk_viewport_remove_user(viewport_user);
    }
  }
}

static void
inf_text_gtk_viewport_user_added(InfTextGtkViewport* viewport,
                                 InfTextUser* user)
{
  InfTextGtkViewportPrivate* priv;
  priv = INF_TEXT_GTK_VIEWPORT_PRIVATE(viewport);

  /* Active user is guaranteed to be contained in user table, so if user was
   * just added then it can't be set as active user already. */
  g_assert(user != priv->active_user);

  g_signal_connect(
    user,
    "notify::status",
    G_CALLBACK(inf_text_gtk_viewport_user_notify_status_cb),
    viewport
  );

  if(inf_user_get_status(INF_USER(user)) == INF_USER_ACTIVE)
    inf_text_gtk_viewport_add_user(viewport, user);
}

static void
inf_text_gtk_viewport_add_user_cb(InfUserTable* user_table,
                                  InfUser* user,
                                  gpointer user_data)
{
  InfTextGtkViewport* viewport = INF_TEXT_GTK_VIEWPORT(user_data);
  g_assert(INF_TEXT_IS_USER(user));
  inf_text_gtk_viewport_user_added(viewport, INF_TEXT_USER(user));
}

static void
inf_text_gtk_viewport_remove_user_cb(InfUserTable* user_table,
                                     InfUser* user,
                                     gpointer user_data)
{
  InfTextGtkViewport* viewport = INF_TEXT_GTK_VIEWPORT(user_data);
  g_assert(INF_TEXT_IS_USER(user));
  inf_text_gtk_viewport_user_removed(viewport, INF_TEXT_USER(user));
}

static void
inf_text_gtk_viewport_set_user_table_foreach_new_user_func(InfUser* user,
                                                           gpointer user_data)
{
  InfTextGtkViewport* viewport = INF_TEXT_GTK_VIEWPORT(user_data);
  g_assert(INF_TEXT_IS_USER(user));
  inf_text_gtk_viewport_user_added(viewport, INF_TEXT_USER(user));
}

static void
inf_text_gtk_viewport_set_user_table_foreach_old_user_func(InfUser* user,
                                                           gpointer user_data)
{
  InfTextGtkViewport* viewport = INF_TEXT_GTK_VIEWPORT(user_data);
  g_assert(INF_TEXT_IS_USER(user));
  inf_text_gtk_viewport_user_removed(viewport, INF_TEXT_USER(user));
}

static void
inf_text_gtk_viewport_set_scrolled_window(InfTextGtkViewport* viewport,
                                          GtkScrolledWindow* scroll)
{
  InfTextGtkViewportPrivate* priv;
  GtkWidget* scrollbar;
  GtkAdjustment* adjustment;

  priv = INF_TEXT_GTK_VIEWPORT_PRIVATE(viewport);

  if(priv->scroll != NULL)
  {
    scrollbar = gtk_scrolled_window_get_vscrollbar(priv->scroll);

    /* Can already be unset at this point */
    /* TODO: Should we catch that and unregister our signals before? OTOH it
     * is most likely going to be freed anyway... */
    if(scrollbar != NULL)
    {
      adjustment = gtk_range_get_adjustment(GTK_RANGE(scrollbar));
      g_assert(adjustment != NULL);

      inf_signal_handlers_disconnect_by_func(
        G_OBJECT(adjustment),
        G_CALLBACK(inf_text_gtk_viewport_adjustment_changed_cb),
        viewport
      );

      inf_signal_handlers_disconnect_by_func(
        G_OBJECT(scrollbar),
        G_CALLBACK(inf_text_gtk_viewport_scrollbar_size_allocate_cb),
        viewport
      );

      inf_signal_handlers_disconnect_by_func(
        G_OBJECT(scrollbar),
        G_CALLBACK(inf_text_gtk_viewport_scrollbar_style_set_cb),
        viewport
      );

      inf_signal_handlers_disconnect_by_func(
        G_OBJECT(scrollbar),
#if GTK_CHECK_VERSION(2, 91, 0)
        G_CALLBACK(inf_text_gtk_viewport_scrollbar_draw_cb),
#else
        G_CALLBACK(inf_text_gtk_viewport_scrollbar_expose_event_cb),
#endif
        viewport
      );
    }

    g_object_unref(priv->scroll);
  }

  priv->scroll = scroll;

  if(scroll != NULL)
  {
    scrollbar = gtk_scrolled_window_get_vscrollbar(priv->scroll);
    /* TODO: can this happen? maybe for GTK_POLICY_NEVER? */
    g_assert(scrollbar != NULL);

    adjustment = gtk_range_get_adjustment(GTK_RANGE(scrollbar));
    g_assert(adjustment != NULL);

    g_object_ref(scroll);

    g_signal_connect_after(
      G_OBJECT(adjustment),
      "changed",
      G_CALLBACK(inf_text_gtk_viewport_adjustment_changed_cb),
      viewport
    );

    g_signal_connect_after(
      G_OBJECT(scrollbar),
      "size-allocate",
      G_CALLBACK(inf_text_gtk_viewport_scrollbar_size_allocate_cb),
      viewport
    );

    g_signal_connect_after(
      G_OBJECT(scrollbar),
      "style-set",
      G_CALLBACK(inf_text_gtk_viewport_scrollbar_style_set_cb),
      viewport
    );

#if GTK_CHECK_VERSION(2, 91, 0)
    g_signal_connect_after(
      G_OBJECT(scrollbar),
      "draw",
      G_CALLBACK(inf_text_gtk_viewport_scrollbar_draw_cb),
      viewport
    );
#else
    g_signal_connect_after(
      G_OBJECT(scrollbar),
      "expose-event",
      G_CALLBACK(inf_text_gtk_viewport_scrollbar_expose_event_cb),
      viewport
    );
#endif
  }

  g_object_notify(G_OBJECT(viewport), "scrolled-window");
}

static void
inf_text_gtk_viewport_set_user_table(InfTextGtkViewport* viewport,
                                     InfUserTable* user_table)
{
  InfTextGtkViewportPrivate* priv;
  priv = INF_TEXT_GTK_VIEWPORT_PRIVATE(viewport);

  if(priv->user_table != NULL)
  {
    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->user_table),
      G_CALLBACK(inf_text_gtk_viewport_add_user_cb),
      viewport
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->user_table),
      G_CALLBACK(inf_text_gtk_viewport_remove_user_cb),
      viewport
    );

    inf_user_table_foreach_user(
      priv->user_table,
      inf_text_gtk_viewport_set_user_table_foreach_old_user_func,
      viewport
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
      G_CALLBACK(inf_text_gtk_viewport_add_user_cb),
      viewport
    );

    g_signal_connect(
      G_OBJECT(user_table),
      "remove-user",
      G_CALLBACK(inf_text_gtk_viewport_remove_user_cb),
      viewport
    );

    inf_user_table_foreach_user(
      user_table,
      inf_text_gtk_viewport_set_user_table_foreach_new_user_func,
      viewport
    );
  }

  g_object_notify(G_OBJECT(viewport), "user-table");
}

static void
inf_text_gtk_viewport_init(GTypeInstance* instance,
                           gpointer g_class)
{
  InfTextGtkViewport* viewport;
  InfTextGtkViewportPrivate* priv;

  viewport = INF_TEXT_GTK_VIEWPORT(instance);
  priv = INF_TEXT_GTK_VIEWPORT_PRIVATE(viewport);

  priv->scroll = NULL;
  priv->user_table = NULL;
  priv->active_user = NULL;
  priv->users = NULL;

  priv->show_user_markers = TRUE;
}

static void
inf_text_gtk_viewport_dispose(GObject* object)
{
  InfTextGtkViewport* viewport;
  InfTextGtkViewportPrivate* priv;

  viewport = INF_TEXT_GTK_VIEWPORT(object);
  priv = INF_TEXT_GTK_VIEWPORT_PRIVATE(viewport);

  inf_text_gtk_viewport_set_scrolled_window(viewport, NULL);
  inf_text_gtk_viewport_set_user_table(viewport, NULL);

  g_assert(priv->active_user == NULL);
  g_assert(priv->users == NULL);

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_text_gtk_viewport_set_property(GObject* object,
                                   guint prop_id,
                                   const GValue* value,
                                   GParamSpec* pspec)
{
  InfTextGtkViewport* viewport;
  InfTextGtkViewportPrivate* priv;

  viewport = INF_TEXT_GTK_VIEWPORT(object);
  priv = INF_TEXT_GTK_VIEWPORT_PRIVATE(viewport);

  switch(prop_id)
  {
  case PROP_SCROLLED_WINDOW:
    g_assert(priv->scroll == NULL); /* construct only */

    inf_text_gtk_viewport_set_scrolled_window(
      viewport,
      GTK_SCROLLED_WINDOW(g_value_get_object(value))
    );

    break;
  case PROP_USER_TABLE:
    g_assert(priv->user_table == NULL); /* construct/only */

    inf_text_gtk_viewport_set_user_table(
      viewport,
      INF_USER_TABLE(g_value_get_object(value))
    );

    break;
  case PROP_ACTIVE_USER:
    inf_text_gtk_viewport_set_active_user(
      viewport,
      INF_TEXT_USER(g_value_get_object(value))
    );

    break;
  case PROP_SHOW_USER_MARKERS:
    inf_text_gtk_viewport_set_show_user_markers(
      viewport,
      g_value_get_boolean(value)
    );

    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(value, prop_id, pspec);
    break;
  }
}

static void
inf_text_gtk_viewport_get_property(GObject* object,
                                   guint prop_id,
                                   GValue* value,
                                   GParamSpec* pspec)
{
  InfTextGtkViewport* viewport;
  InfTextGtkViewportPrivate* priv;

  viewport = INF_TEXT_GTK_VIEWPORT(object);
  priv = INF_TEXT_GTK_VIEWPORT_PRIVATE(viewport);

  switch(prop_id)
  {
  case PROP_SCROLLED_WINDOW:
    g_value_set_object(value, G_OBJECT(priv->scroll));
    break;
  case PROP_USER_TABLE:
    g_value_set_object(value, G_OBJECT(priv->user_table));
    break;
  case PROP_ACTIVE_USER:
    g_value_set_object(value, G_OBJECT(priv->active_user));
    break;
  case PROP_SHOW_USER_MARKERS:
    g_value_set_boolean(value, priv->show_user_markers);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_text_gtk_viewport_class_init(gpointer g_class,
                                 gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfTextGtkViewportPrivate));

  object_class->dispose = inf_text_gtk_viewport_dispose;
  object_class->set_property = inf_text_gtk_viewport_set_property;
  object_class->get_property = inf_text_gtk_viewport_get_property;

  g_object_class_install_property(
    object_class,
    PROP_SCROLLED_WINDOW,
    g_param_spec_object(
      "scrolled-window",
      "Scrolled Window",
      "The underlying GtkScrolledWindow",
      GTK_TYPE_SCROLLED_WINDOW,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_USER_TABLE,
    g_param_spec_object(
      "user-table",
      "User table",
      "The user table containing the users of the session shown in the "
      "viewport",
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
      "The user for which to show the viewport",
      INF_TEXT_TYPE_USER,
      G_PARAM_READWRITE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_SHOW_USER_MARKERS,
    g_param_spec_boolean(
      "show-user-markers",
      "Show user markers",
      "Whether to indicate the position of non-local user's cursors in the "
      "scrollbar",
      TRUE,
      G_PARAM_READWRITE
    )
  );
}

GType
inf_text_gtk_viewport_get_type(void)
{
  static GType viewport_type = 0;

  if(!viewport_type)
  {
    static const GTypeInfo viewport_type_info = {
      sizeof(InfTextGtkViewportClass),    /* class_size */
      NULL,                               /* base_init */
      NULL,                               /* base_finalize */
      inf_text_gtk_viewport_class_init,   /* class_init */
      NULL,                               /* class_finalize */
      NULL,                               /* class_data */
      sizeof(InfTextGtkViewport),         /* instance_size */
      0,                                  /* n_preallocs */
      inf_text_gtk_viewport_init,         /* instance_init */
      NULL                                /* value_table */
    };

    viewport_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfTextGtkViewport",
      &viewport_type_info,
      0
    );
  }

  return viewport_type;
}

/**
 * inf_text_gtk_viewport_new:
 * @scroll: A #GtkScrolledWindow.
 * @user_table: The #InfUserTable for the text session displayed in @viewport.
 *
 * Creates a new #InfTextGtkViewport for @scroll. This draws the position of
 * remote user's cursors into the scrollbars of @scroll.
 *
 * Returns: A new #InfTextGtkViewport.
 */
InfTextGtkViewport*
inf_text_gtk_viewport_new(GtkScrolledWindow* scroll,
                          InfUserTable* user_table)
{
  GObject* object;

  g_return_val_if_fail(GTK_IS_SCROLLED_WINDOW(scroll), NULL);
  g_return_val_if_fail(INF_IS_USER_TABLE(user_table), NULL);

  object = g_object_new(
    INF_TEXT_GTK_TYPE_VIEWPORT,
    "scrolled-window", scroll,
    "user-table", user_table,
    NULL
  );

  return INF_TEXT_GTK_VIEWPORT(object);
}

/**
 * inf_text_gtk_viewport_get_scrolled_window:
 * @viewport: A #InfTextGtkViewport.
 *
 * Returns the underlying #GtkScrolledWindow.
 *
 * Return Value: The #InfTextGtkViewport's #GtkScrolledWindow.
 **/
GtkScrolledWindow*
inf_text_gtk_viewport_get_scrolled_window(InfTextGtkViewport* viewport)
{
  g_return_val_if_fail(INF_TEXT_GTK_IS_VIEWPORT(viewport), NULL);
  return INF_TEXT_GTK_VIEWPORT_PRIVATE(viewport)->scroll;
}

/**
 * inf_text_gtk_viewport_get_user_table:
 * @viewport: A #InfTextGtkViewport.
 *
 * Returns the #InfUserTable containing the users of the session the
 * #InfTextGtkViewport's #GtkScrolledWindow is displaying.
 *
 * Returns: The #InfTextGtkViewport's #InfUserTable.
 */
InfUserTable*
inf_text_gtk_viewport_get_user_table(InfTextGtkViewport* viewport)
{
  g_return_val_if_fail(INF_TEXT_GTK_IS_VIEWPORT(viewport), NULL);
  return INF_TEXT_GTK_VIEWPORT_PRIVATE(viewport)->user_table;
}

/**
 * inf_text_gtk_viewport_set_active_user:
 * @viewport: A #InfTextGtkViewport.
 * @user: A user from @viewport's user table, or %NULL.
 *
 * Sets the user for which perspective to draw the viewport. The cursor
 * position for teh active user is not draws since it is assumed that the
 * viewport's "real" scrollbars match the active user's position.
 */
void
inf_text_gtk_viewport_set_active_user(InfTextGtkViewport* viewport,
                                      InfTextUser* user)
{
  InfTextGtkViewportPrivate* priv;
  InfTextUser* active_user;

  g_return_if_fail(INF_TEXT_GTK_IS_VIEWPORT(viewport));
  g_return_if_fail(user == NULL || INF_TEXT_IS_USER(user));

  priv = INF_TEXT_GTK_VIEWPORT_PRIVATE(viewport);
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

    inf_text_gtk_viewport_user_added(viewport, active_user);
  }

  if(user != NULL)
  {
    inf_text_gtk_viewport_user_removed(viewport, user);
  }

  priv->active_user = user;
  g_object_notify(G_OBJECT(viewport), "active-user");
}

/**
 * inf_text_gtk_viewport_get_active_user:
 * @viewport: A #InfTextGtkViewport.
 *
 * Returns the active user of @viewport. See
 * inf_text_gtk_viewport_set_active_user().
 *
 * Returns: The active user of @viewport.
 */
InfTextUser*
inf_text_gtk_viewport_get_active_user(InfTextGtkViewport* viewport)
{
  g_return_val_if_fail(INF_TEXT_GTK_IS_VIEWPORT(viewport), NULL);
  return INF_TEXT_GTK_VIEWPORT_PRIVATE(viewport)->active_user;
}

/**
 * inf_text_gtk_viewport_set_show_user_markers:
 * @viewport: A #InfTextGtkViewport.
 * @show: Whether to show the position of non-local users.
 *
 * If @show is %TRUE then draw a marker indicating the cursor position of all
 * non-local users with status %INF_USER_ACTIVE in the scrollbar of the
 * scrolled window. If @show is %FALSE then do not draw user markers into the
 * scrollbar.
 */
void
inf_text_gtk_viewport_set_show_user_markers(InfTextGtkViewport* viewport,
                                            gboolean show)
{
  InfTextGtkViewportPrivate* priv;

  g_return_if_fail(INF_TEXT_GTK_IS_VIEWPORT(viewport));
  priv = INF_TEXT_GTK_VIEWPORT_PRIVATE(viewport);

  if(priv->show_user_markers != show)
  {
    gtk_widget_queue_draw(gtk_scrolled_window_get_vscrollbar(priv->scroll));

    priv->show_user_markers = show;
    g_object_notify(G_OBJECT(viewport), "show-user-markers");
  }
}

/* vim:set et sw=2 ts=2: */
