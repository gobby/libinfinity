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

#include <libinftextgtk/inf-text-gtk-hue-chooser.h>
#include <libinfinity/inf-marshal.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include <math.h>

/* Based on gtkhsv.c from GTK+ */

typedef enum _InfTextGtkHueChooserDragMode {
  INF_TEXT_GTK_HUE_CHOOSER_DRAG_NONE,
  INF_TEXT_GTK_HUE_CHOOSER_DRAG_HUE
} InfTextGtkHueChooserDragMode;

typedef struct _InfTextGtkHueChooserPrivate InfTextGtkHueChooserPrivate;
struct _InfTextGtkHueChooserPrivate {
  gdouble hue;

  GdkWindow* window;

  guint ring_width;
  guint size;

  InfTextGtkHueChooserDragMode mode;
};

enum {
  PROP_0,

  PROP_HUE
};

enum {
  HUE_CHANGE,
  MOVE,

  LAST_SIGNAL
};

#define INF_TEXT_GTK_HUE_CHOOSER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TEXT_GTK_TYPE_HUE_CHOOSER, InfTextGtkHueChooserPrivate))

static GtkWidgetClass* parent_class;
static guint hue_chooser_signals[LAST_SIGNAL];

static const double INF_TEXT_GTK_HUE_CHOOSER_HUE_MOVE_DELTA = 0.002;

/*
 * Helper functions
 */

#define INTENSITY(r, g, b) ((r) * 0.30 + (g) * 0.59 + (b) * 0.11)

/* TODO: Use gtk_hsv_to_rgb from GTK+ 2.14 instead */
static void
inf_text_gtk_hue_chooser_hsv_to_rgb(gdouble* h,
                                    gdouble* s,
                                    gdouble* v)
{
  gdouble hue;
  gdouble saturation;
  gdouble value;
  gdouble f, p, q, t;

  if(*s == 0.0)
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

    if(hue == 6.0)
      hue = 0.0;

    f = hue - (int)hue;
    p = value * (1.0 - saturation);
    q = value * (1.0 - saturation * f);
    t = value * (1.0 - saturation * (1.0 - f));

    switch((int)hue)
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
      break;
    }
  }
}

static gboolean
inf_text_gtk_hue_chooser_is_in_ring(InfTextGtkHueChooser* chooser,
                                    gdouble x,
                                    gdouble y)
{
  InfTextGtkHueChooserPrivate* priv;
  GtkAllocation allocation;
  gdouble center_x;
  gdouble center_y;
  gdouble dx;
  gdouble dy;
  gdouble inner;
  gdouble outer;
  gdouble dist_sqr;

#if GTK_CHECK_VERSION(2,18,0)
  gtk_widget_get_allocation(GTK_WIDGET(chooser), &allocation);
#else
  allocation = GTK_WIDGET(chooser)->allocation;
#endif

  priv = INF_TEXT_GTK_HUE_CHOOSER_PRIVATE(chooser);
  center_x = allocation.width / 2.0;
  center_y = allocation.height / 2.0;
  outer = priv->size / 2.0;
  inner = outer - priv->ring_width;
  dx = x - center_x;
  dy = center_y - y;
  dist_sqr = dx * dx + dy * dy;

  return (dist_sqr >= inner * inner && dist_sqr <= outer * outer);
}

static gdouble
inf_text_gtk_hue_chooser_hue_by_coords(InfTextGtkHueChooser* chooser,
                                       gdouble x,
                                       gdouble y)
{
  GtkAllocation allocation;
  double center_x;
  double center_y;
  double dx;
  double dy;
  double angle;

#if GTK_CHECK_VERSION(2,18,0)
  gtk_widget_get_allocation(GTK_WIDGET(chooser), &allocation);
#else
  allocation = GTK_WIDGET(chooser)->allocation;
#endif

  center_x = allocation.width / 2.0;
  center_y = allocation.height / 2.0;

  dx = x - center_x;
  dy = center_y - y;

  angle = atan2(dy, dx);
  if(angle < 0.0) angle += 2.0 * G_PI;

  return angle / (2.0 * G_PI);
}

static void
inf_text_gtk_hue_chooser_paint(InfTextGtkHueChooser* chooser,
                               cairo_t* cr,
                               gint x,
                               gint y,
                               gint width,
                               gint height)
{
  InfTextGtkHueChooserPrivate* priv;
  GtkAllocation allocation;
  int xx, yy;
  gdouble dx, dy, dist;
  gdouble center_x;
  gdouble center_y;
  gdouble inner, outer;
  guint32* buf;
  guint32* p;
  gdouble angle;
  gdouble hue;
  gdouble r;
  gdouble g;
  gdouble b;
  cairo_surface_t* source;
  cairo_t* source_cr;
  gint focus_width;
  gint focus_pad;
  gint r_, g_, b_;

  priv = INF_TEXT_GTK_HUE_CHOOSER_PRIVATE(chooser);

  gtk_widget_style_get(
    GTK_WIDGET(chooser),
    "focus-line-width", &focus_width,
    "focus-padding", &focus_pad,
    NULL
  );

#if GTK_CHECK_VERSION(2,18,0)
  gtk_widget_get_allocation(GTK_WIDGET(chooser), &allocation);
#else
  allocation = GTK_WIDGET(chooser)->allocation;
#endif

  center_x = allocation.width / 2.0;
  center_y = allocation.height / 2.0;

  outer = priv->size / 2.0;
  inner = outer - priv->ring_width;

  /* Create an image initialized with the ring colors */

  buf = g_new(guint32, width * height);

  for(yy = 0; yy < height; yy++)
  {
    p = buf + yy * width;

    dy = -(yy + y - center_y);

    for (xx = 0; xx < width; xx++)
    {
      dx = xx + x - center_x;

      dist = dx * dx + dy * dy;
      if(dist < ((inner-1) * (inner-1)) || dist > ((outer+1) * (outer+1)))
      {
        *p++ = 0;
        continue;
      }

      angle = atan2 (dy, dx);
      if (angle < 0.0)
        angle += 2.0 * G_PI;

      hue = angle / (2.0 * G_PI);

      r = hue;
      g = 1.0;
      b = 1.0;
      inf_text_gtk_hue_chooser_hsv_to_rgb(&r, &g, &b);

      r_ = floor (r * 255 + 0.5);
      g_ = floor (g * 255 + 0.5);
      b_ = floor (b * 255 + 0.5);

      *p++ = ((r_ << 16) |
              (g_ <<  8) |
               b_      );
    }
  }

  source = cairo_image_surface_create_for_data(
    (unsigned char *)buf,
    CAIRO_FORMAT_RGB24,
    width,
    height,
    4 * width
  );

  /* Now draw the value marker onto the source image, so that it
   * will get properly clipped at the edges of the ring
   */
  source_cr = cairo_create(source);

  r = priv->hue;
  g = 1.0;
  b = 1.0;
  inf_text_gtk_hue_chooser_hsv_to_rgb(&r, &g, &b);

  if(INTENSITY(r, g, b) > 0.5)
    cairo_set_source_rgb(source_cr, 0.0, 0.0, 0.0);
  else
    cairo_set_source_rgb(source_cr, 1.0, 1.0, 1.0);

  cairo_move_to(source_cr, -x + center_x, - y + center_y);
  cairo_line_to(
    source_cr,
    -x + center_x + cos(priv->hue * 2.0 * G_PI) * priv->size / 2,
    -y + center_y - sin(priv->hue * 2.0 * G_PI) * priv->size / 2
  );
  cairo_stroke(source_cr);
  cairo_destroy(source_cr);

  /* Draw the ring using the source image */

  cairo_save(cr);

  cairo_set_source_surface(cr, source, x, y);
  cairo_surface_destroy(source);

  cairo_set_line_width(cr, priv->ring_width);
  cairo_new_path(cr);

  cairo_arc(
    cr,
    center_x,
    center_y,
    priv->size / 2.0 - priv->ring_width / 2.0,
    0,
    2 * G_PI
  );

  cairo_stroke(cr);

  cairo_restore(cr);

  g_free (buf);
}

/*
 * GObject overrides
 */

static void
inf_text_gtk_hue_chooser_init(GTypeInstance* instance,
                              gpointer g_class)
{
  InfTextGtkHueChooser* hue_chooser;
  InfTextGtkHueChooserPrivate* priv;

  hue_chooser = INF_TEXT_GTK_HUE_CHOOSER(instance);
  priv = INF_TEXT_GTK_HUE_CHOOSER_PRIVATE(hue_chooser);

#if GTK_CHECK_VERSION(2,18,0)
  gtk_widget_set_has_window(GTK_WIDGET(hue_chooser), FALSE);
  gtk_widget_set_can_focus(GTK_WIDGET(hue_chooser), TRUE);
#else
  GTK_WIDGET_SET_FLAGS(hue_chooser, GTK_NO_WINDOW);
  GTK_WIDGET_SET_FLAGS(hue_chooser, GTK_CAN_FOCUS);
#endif

  priv->hue = 0.0;
  priv->window = NULL;
  priv->ring_width = 50;
  priv->size = 240;
  priv->mode = INF_TEXT_GTK_HUE_CHOOSER_DRAG_NONE;
}

static void
inf_text_gtk_hue_chooser_dispose(GObject* object)
{
  InfTextGtkHueChooser* hue_chooser;
  InfTextGtkHueChooserPrivate* priv;

  hue_chooser = INF_TEXT_GTK_HUE_CHOOSER(object);
  priv = INF_TEXT_GTK_HUE_CHOOSER_PRIVATE(hue_chooser);

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_text_gtk_hue_chooser_finalize(GObject* object)
{
  InfTextGtkHueChooser* hue_chooser;
  InfTextGtkHueChooserPrivate* priv;

  hue_chooser = INF_TEXT_GTK_HUE_CHOOSER(object);
  priv = INF_TEXT_GTK_HUE_CHOOSER_PRIVATE(hue_chooser);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_text_gtk_hue_chooser_set_property(GObject* object,
                                      guint prop_id,
                                      const GValue* value,
                                      GParamSpec* pspec)
{
  InfTextGtkHueChooser* hue_chooser;
  InfTextGtkHueChooserPrivate* priv;

  hue_chooser = INF_TEXT_GTK_HUE_CHOOSER(object);
  priv = INF_TEXT_GTK_HUE_CHOOSER_PRIVATE(hue_chooser);

  switch(prop_id)
  {
  case PROP_HUE:
    inf_text_gtk_hue_chooser_set_hue(hue_chooser, g_value_get_double(value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_text_gtk_hue_chooser_get_property(GObject* object,
                                      guint prop_id,
                                      GValue* value,
                                      GParamSpec* pspec)
{
  InfTextGtkHueChooser* hue_chooser;
  InfTextGtkHueChooserPrivate* priv;

  hue_chooser = INF_TEXT_GTK_HUE_CHOOSER(object);
  priv = INF_TEXT_GTK_HUE_CHOOSER_PRIVATE(hue_chooser);

  switch(prop_id)
  {
  case PROP_HUE:
    g_value_set_double(value, priv->hue);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

/*
 * GtkWidget overrides
 */

static void
inf_text_gtk_hue_chooser_map(GtkWidget* widget)
{
  InfTextGtkHueChooser* chooser;
  InfTextGtkHueChooserPrivate* priv;

  chooser = INF_TEXT_GTK_HUE_CHOOSER(widget);
  priv = INF_TEXT_GTK_HUE_CHOOSER_PRIVATE(chooser);

  GTK_WIDGET_CLASS(parent_class)->map(widget);
  gdk_window_show(priv->window);
}

static void
inf_text_gtk_hue_chooser_unmap(GtkWidget* widget)
{
  InfTextGtkHueChooser* chooser;
  InfTextGtkHueChooserPrivate* priv;

  chooser = INF_TEXT_GTK_HUE_CHOOSER(widget);
  priv = INF_TEXT_GTK_HUE_CHOOSER_PRIVATE(chooser);

  gdk_window_hide(priv->window);
  GTK_WIDGET_CLASS(parent_class)->unmap(widget);
}

static void
inf_text_gtk_hue_chooser_realize(GtkWidget* widget)
{
  InfTextGtkHueChooser* chooser;
  InfTextGtkHueChooserPrivate* priv;
  GtkAllocation allocation;
  GdkWindowAttr attr;
  GdkWindow* parent_window;

  chooser = INF_TEXT_GTK_HUE_CHOOSER(widget);
  priv = INF_TEXT_GTK_HUE_CHOOSER_PRIVATE(chooser);

#if GTK_CHECK_VERSION(2,20,0)
  gtk_widget_set_realized(widget, TRUE);
#else
  GTK_WIDGET_SET_FLAGS(widget, GTK_REALIZED);
#endif

#if GTK_CHECK_VERSION(2,18,0)
  gtk_widget_get_allocation(widget, &allocation);
#else
  allocation = widget->allocation;
#endif

  attr.window_type = GDK_WINDOW_CHILD;
  attr.x = allocation.x;
  attr.y = allocation.y;
  attr.width = allocation.width;
  attr.height = allocation.height;
  attr.wclass = GDK_INPUT_ONLY;
  attr.event_mask = gtk_widget_get_events(widget);
  attr.event_mask |= (
    GDK_KEY_PRESS_MASK |
    GDK_BUTTON_PRESS_MASK |
    GDK_BUTTON_RELEASE_MASK |
    GDK_POINTER_MOTION_MASK |
    GDK_ENTER_NOTIFY_MASK |
    GDK_LEAVE_NOTIFY_MASK
  );

  parent_window = gtk_widget_get_parent_window(widget);
  g_object_ref(parent_window);

#if GTK_CHECK_VERSION(2,18,0)
  gtk_widget_set_window(widget, parent_window);
#else
  widget->window = parent_window;
#endif

  priv->window = gdk_window_new(parent_window, &attr, GDK_WA_X | GDK_WA_Y);
  gdk_window_set_user_data(priv->window, chooser);
  gtk_widget_set_style(
    widget,
    gtk_style_attach(gtk_widget_get_style(widget), parent_window)
  );
}

static void
inf_text_gtk_hue_chooser_unrealize(GtkWidget* widget)
{
  InfTextGtkHueChooser* chooser;
  InfTextGtkHueChooserPrivate* priv;

  chooser = INF_TEXT_GTK_HUE_CHOOSER(widget);
  priv = INF_TEXT_GTK_HUE_CHOOSER_PRIVATE(chooser);

  gdk_window_set_user_data(priv->window, NULL);
  gdk_window_destroy(priv->window);
  priv->window = NULL;

  GTK_WIDGET_CLASS(parent_class)->unrealize(widget);
}

static gint
inf_text_gtk_hue_chooser_calculate_width_and_height(GtkWidget *widget)
{
  InfTextGtkHueChooser* chooser;
  InfTextGtkHueChooserPrivate* priv;
  gint focus_width;
  gint focus_pad;

  chooser = INF_TEXT_GTK_HUE_CHOOSER(widget);
  priv = INF_TEXT_GTK_HUE_CHOOSER_PRIVATE(chooser);

  gtk_widget_style_get(
    widget,
    "focus-line-width", &focus_width,
    "focus-padding", &focus_pad,
    NULL
  );

  return priv->size + 2 * (focus_width + focus_pad);
}

#if GTK_CHECK_VERSION(2,91,6)
static void
inf_text_gtk_hue_chooser_get_preferred_width(GtkWidget *widget,
                                             gint *minimum,
                                             gint *natural)
{
  *minimum = *natural =
    inf_text_gtk_hue_chooser_calculate_width_and_height(widget);
}

static void
inf_text_gtk_hue_chooser_get_preferred_height(GtkWidget *widget,
                                              gint *minimum,
                                              gint *natural)
{
  *minimum = *natural =
    inf_text_gtk_hue_chooser_calculate_width_and_height(widget);
}

#else

static void
inf_text_gtk_hue_chooser_size_request(GtkWidget* widget,
                                      GtkRequisition* requisition)
{
  requisition->width = requisition->height =
    inf_text_gtk_hue_chooser_calculate_width_and_height(widget);
}
#endif

static void
inf_text_gtk_hue_chooser_size_allocate(GtkWidget* widget,
                                       GtkAllocation* allocation)
{
  InfTextGtkHueChooser* chooser;
  InfTextGtkHueChooserPrivate* priv;

  chooser = INF_TEXT_GTK_HUE_CHOOSER(widget);
  priv = INF_TEXT_GTK_HUE_CHOOSER_PRIVATE(chooser);

#if GTK_CHECK_VERSION(2,18,0)
  gtk_widget_set_allocation(widget, allocation);
#else
  widget->allocation = *allocation;
#endif

#if GTK_CHECK_VERSION(2,20,0)
  if(gtk_widget_get_realized(widget))
#else
  if(GTK_WIDGET_REALIZED(widget))
#endif
  {
    /* TODO: Keep 1:1 aspect ratio, center within allocation, set size
     * accordingly, always request only ring_width * 2. */
    gdk_window_move_resize(
      priv->window,
      allocation->x,
      allocation->y,
      allocation->width,
      allocation->height
    );
  }
}

static gboolean
inf_text_gtk_hue_chooser_button_press_event(GtkWidget* widget,
                                            GdkEventButton* event)
{
  InfTextGtkHueChooser* chooser;
  InfTextGtkHueChooserPrivate* priv;
  GdkCursor* cursor;
  double hue;

  chooser = INF_TEXT_GTK_HUE_CHOOSER(widget);
  priv = INF_TEXT_GTK_HUE_CHOOSER_PRIVATE(chooser);

  gtk_widget_queue_draw(widget);

  if(priv->mode != INF_TEXT_GTK_HUE_CHOOSER_DRAG_NONE || event->button != 1)
    return FALSE;

  if(inf_text_gtk_hue_chooser_is_in_ring(chooser, event->x, event->y))
  {
    /* Start drag */
    priv->mode = INF_TEXT_GTK_HUE_CHOOSER_DRAG_HUE;

    hue = inf_text_gtk_hue_chooser_hue_by_coords(chooser, event->x, event->y);
    inf_text_gtk_hue_chooser_set_hue(chooser, hue);

    cursor = gdk_cursor_new_for_display(
      gtk_widget_get_display(widget),
      GDK_CROSSHAIR
    );

#if GTK_CHECK_VERSION(2, 91, 0)
    gdk_device_grab(
      gdk_event_get_device((GdkEvent*)event),
      priv->window,
      GDK_OWNERSHIP_NONE,
      FALSE,
      GDK_POINTER_MOTION_MASK |
      GDK_POINTER_MOTION_HINT_MASK |
      GDK_BUTTON_RELEASE_MASK,
      cursor,
      event->time
    );
#else
    gdk_pointer_grab(
      priv->window,
      FALSE,
      GDK_POINTER_MOTION_MASK |
      GDK_POINTER_MOTION_HINT_MASK |
      GDK_BUTTON_RELEASE_MASK,
      NULL,
      cursor,
      event->time
    );
#endif
    gdk_cursor_unref(cursor);
    gtk_widget_grab_focus(widget);
  }

  return TRUE;
}

static gboolean
inf_text_gtk_hue_chooser_button_release_event(GtkWidget* widget,
                                              GdkEventButton* event)
{
  InfTextGtkHueChooser* chooser;
  InfTextGtkHueChooserPrivate* priv;
  double hue;

  chooser = INF_TEXT_GTK_HUE_CHOOSER(widget);
  priv = INF_TEXT_GTK_HUE_CHOOSER_PRIVATE(chooser);

  if(priv->mode == INF_TEXT_GTK_HUE_CHOOSER_DRAG_NONE || event->button != 1)
    return FALSE;

  priv->mode = INF_TEXT_GTK_HUE_CHOOSER_DRAG_NONE;

  hue = inf_text_gtk_hue_chooser_hue_by_coords(chooser, event->x, event->y);
  inf_text_gtk_hue_chooser_set_hue(chooser, hue);

#if GTK_CHECK_VERSION(2, 91, 0)
  gdk_device_ungrab(
    gdk_event_get_device((GdkEvent*)event),
    event->time
  );
#else
  gdk_display_pointer_ungrab(
    gdk_drawable_get_display(event->window),
    event->time
  );
#endif
  return TRUE;
}

static gboolean
inf_text_gtk_hue_chooser_motion_notify_event(GtkWidget* widget,
                                             GdkEventMotion* event)
{
  InfTextGtkHueChooser* chooser;
  InfTextGtkHueChooserPrivate* priv;
  double hue;

  chooser = INF_TEXT_GTK_HUE_CHOOSER(widget);
  priv = INF_TEXT_GTK_HUE_CHOOSER_PRIVATE(chooser);

  if(priv->mode == INF_TEXT_GTK_HUE_CHOOSER_DRAG_NONE)
    return FALSE;

  gdk_event_request_motions(event);

  hue = inf_text_gtk_hue_chooser_hue_by_coords(chooser, event->x, event->y);
  inf_text_gtk_hue_chooser_set_hue(chooser, hue);

  return TRUE;
}

#if GTK_CHECK_VERSION(2,91,0)
static gboolean
inf_text_gtk_hue_chooser_draw(GtkWidget* widget,
                              cairo_t* cr)
{
  InfTextGtkHueChooser* chooser;
  InfTextGtkHueChooserPrivate* priv;

  chooser = INF_TEXT_GTK_HUE_CHOOSER(widget);
  priv = INF_TEXT_GTK_HUE_CHOOSER_PRIVATE(chooser);

  inf_text_gtk_hue_chooser_paint(
    chooser,
    cr,
    0,
    0,
    gtk_widget_get_allocated_width(widget),
    gtk_widget_get_allocated_height(widget)
  );

  return FALSE;
}
#else
static gboolean
inf_text_gtk_hue_chooser_expose_event(GtkWidget* widget,
                                      GdkEventExpose* event)
{
  InfTextGtkHueChooser* chooser;
  InfTextGtkHueChooserPrivate* priv;
  GtkAllocation allocation;
  GdkRectangle rect;
  GdkRectangle dest;
  cairo_t* cr;

  chooser = INF_TEXT_GTK_HUE_CHOOSER(widget);
  priv = INF_TEXT_GTK_HUE_CHOOSER_PRIVATE(chooser);

#if GTK_CHECK_VERSION(2,18,0)
  if(!(gtk_widget_is_drawable(widget) &&
       event->window == gtk_widget_get_window(widget)))
  {
    return FALSE;
  }
#else
  if(!(GTK_WIDGET_DRAWABLE(widget) && event->window == widget->window))
    return FALSE;
#endif

#if GTK_CHECK_VERSION(2,18,0)
  gtk_widget_get_allocation(widget, &allocation);
#else
  allocation = widget->allocation;
#endif

  rect.x = allocation.x;
  rect.y = allocation.y;
  rect.width = allocation.width;
  rect.height = allocation.height;

  if(!gdk_rectangle_intersect(&event->area, &rect, &dest))
    return FALSE;

#if GTK_CHECK_VERSION(2,14,0)
  cr = gdk_cairo_create(gtk_widget_get_window(widget));
#else
  cr = gdk_cairo_create(widget->window);
#endif
  cairo_translate(cr, allocation.x, allocation.y);

  inf_text_gtk_hue_chooser_paint(
    chooser,
    cr,
    dest.x - allocation.x,
    dest.y - allocation.y,
    dest.width,
    dest.height
  );

  cairo_destroy(cr);

#if GTK_CHECK_VERSION(2,18,0)
  if(gtk_widget_has_focus(widget))
#else
  if(GTK_WIDGET_HAS_FOCUS(widget))
#endif
  {
    /* This looks irritating: */
#if 0
    gtk_paint_focus(
      widget->style,
      widget->window,
      GTK_WIDGET_STATE(widget),
      &event->area,
      widget,
      NULL,
      widget->allocation.x,
      widget->allocation.y,
      widget->allocation.width,
      widget->allocation.height
    );
#endif
  }

  return FALSE;
}
#endif

static gboolean
inf_text_gtk_hue_chooser_focus(GtkWidget* widget,
                               GtkDirectionType dir)
{
  InfTextGtkHueChooser* chooser;
  InfTextGtkHueChooserPrivate* priv;

  chooser = INF_TEXT_GTK_HUE_CHOOSER(widget);
  priv = INF_TEXT_GTK_HUE_CHOOSER_PRIVATE(chooser);

#if GTK_CHECK_VERSION(2,18,0)
  if(!gtk_widget_has_focus(widget))
#else
  if(!GTK_WIDGET_HAS_FOCUS(widget))
#endif
  {
    gtk_widget_grab_focus(GTK_WIDGET(widget));
    return TRUE;
  }

  return FALSE;
}

static gboolean
inf_text_gtk_hue_chooser_grab_broken_event(GtkWidget* widget,
                                           GdkEventGrabBroken* event)
{
  InfTextGtkHueChooser* chooser;
  InfTextGtkHueChooserPrivate* priv;

  chooser = INF_TEXT_GTK_HUE_CHOOSER(widget);
  priv = INF_TEXT_GTK_HUE_CHOOSER_PRIVATE(chooser);

  priv->mode = INF_TEXT_GTK_HUE_CHOOSER_DRAG_NONE;
  return TRUE;
}

/*
 * Default signal handlers
 */

static void
inf_text_gtk_hue_chooser_hue_change(InfTextGtkHueChooser* chooser,
                                    gdouble hue)
{
  InfTextGtkHueChooserPrivate* priv;
  priv = INF_TEXT_GTK_HUE_CHOOSER_PRIVATE(chooser);

  priv->hue = hue;
  g_object_notify(G_OBJECT(chooser), "hue");
  gtk_widget_queue_draw(GTK_WIDGET(chooser));
}

static void
inf_text_gtk_hue_chooser_move(InfTextGtkHueChooser* chooser,
                              GtkDirectionType direction)
{
  InfTextGtkHueChooserPrivate* priv;
  gdouble hue;

  priv = INF_TEXT_GTK_HUE_CHOOSER_PRIVATE(chooser);
  hue = priv->hue;

  switch(direction)
  {
  case GTK_DIR_UP:
  case GTK_DIR_LEFT:
    hue += INF_TEXT_GTK_HUE_CHOOSER_HUE_MOVE_DELTA;
    break;
  case GTK_DIR_DOWN:
  case GTK_DIR_RIGHT:
    hue -= INF_TEXT_GTK_HUE_CHOOSER_HUE_MOVE_DELTA;
    break;
  default:
    /* we don't care about the tab directions */
    break;
  }

  /* wrap */
  if(hue < 0.0) hue += 1.0;
  if(hue > 1.0) hue -= 1.0;

  inf_text_gtk_hue_chooser_set_hue(chooser, hue);
}

/*
 * Type registration
 */

static void
inf_text_gtk_hue_chooser_class_init(gpointer g_class,
                                    gpointer class_data)
{
  GObjectClass* gobject_class;
  GtkWidgetClass* widget_class;
  InfTextGtkHueChooserClass* hue_chooser_class;
  GtkBindingSet* binding_set;

  gobject_class = G_OBJECT_CLASS(g_class);
  widget_class = GTK_WIDGET_CLASS(g_class);
  hue_chooser_class = INF_TEXT_GTK_HUE_CHOOSER_CLASS(g_class);

  parent_class = GTK_WIDGET_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfTextGtkHueChooserPrivate));

  gobject_class->dispose = inf_text_gtk_hue_chooser_dispose;
  gobject_class->finalize = inf_text_gtk_hue_chooser_finalize;
  gobject_class->set_property = inf_text_gtk_hue_chooser_set_property;
  gobject_class->get_property = inf_text_gtk_hue_chooser_get_property;

  /*object_class->destroy = inf_text_gtk_hue_chooser_destroy;*/

  widget_class->map = inf_text_gtk_hue_chooser_map;
  widget_class->unmap = inf_text_gtk_hue_chooser_unmap;
  widget_class->realize = inf_text_gtk_hue_chooser_realize;
  widget_class->unrealize = inf_text_gtk_hue_chooser_unrealize;
#if GTK_CHECK_VERSION(2,91,6)
  widget_class->get_preferred_height =
    inf_text_gtk_hue_chooser_get_preferred_height;
  widget_class->get_preferred_width =
    inf_text_gtk_hue_chooser_get_preferred_width;
#else
  widget_class->size_request = inf_text_gtk_hue_chooser_size_request;
#endif
  widget_class->size_allocate = inf_text_gtk_hue_chooser_size_allocate;
  widget_class->button_press_event =
    inf_text_gtk_hue_chooser_button_press_event;
  widget_class->button_release_event =
    inf_text_gtk_hue_chooser_button_release_event;
  widget_class->motion_notify_event =
    inf_text_gtk_hue_chooser_motion_notify_event;
#if GTK_CHECK_VERSION(2,91,0)
  widget_class->draw = inf_text_gtk_hue_chooser_draw;
#else
  widget_class->expose_event = inf_text_gtk_hue_chooser_expose_event;
#endif
  widget_class->focus = inf_text_gtk_hue_chooser_focus;
  widget_class->grab_broken_event = inf_text_gtk_hue_chooser_grab_broken_event;

  hue_chooser_class->hue_change = inf_text_gtk_hue_chooser_hue_change;
  hue_chooser_class->move = inf_text_gtk_hue_chooser_move;

  hue_chooser_signals[HUE_CHANGE] = g_signal_new(
    "hue-change",
    G_OBJECT_CLASS_TYPE(gobject_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfTextGtkHueChooserClass, hue_change),
    NULL, NULL,
    inf_marshal_VOID__DOUBLE,
    G_TYPE_NONE,
    1,
    G_TYPE_DOUBLE
  );

  hue_chooser_signals[MOVE] = g_signal_new(
    "move",
    G_OBJECT_CLASS_TYPE(gobject_class),
    G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
    G_STRUCT_OFFSET(InfTextGtkHueChooserClass, move),
    NULL, NULL,
    inf_marshal_VOID__ENUM,
    G_TYPE_NONE,
    1,
    GTK_TYPE_DIRECTION_TYPE
  );

  g_object_class_install_property(
    gobject_class,
    PROP_HUE,
    g_param_spec_double(
      "hue",
      "Hue",
      "The current hue value",
      0.0,
      1.0,
      0.0,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT
    )
  );

  binding_set = gtk_binding_set_by_class(g_class);

  gtk_binding_entry_add_signal(
    binding_set,
#if GTK_CHECK_VERSION(2,90,7)
    GDK_KEY_Up,
#else
    GDK_Up,
#endif
    0,
    "move",
    1,
    G_TYPE_ENUM,
    GTK_DIR_UP
  );

  gtk_binding_entry_add_signal(
    binding_set,
#if GTK_CHECK_VERSION(2,90,7)
    GDK_KEY_KP_Up,
#else
    GDK_KP_Up,
#endif
    0,
    "move",
    1,
    G_TYPE_ENUM,
    GTK_DIR_UP
  );

  gtk_binding_entry_add_signal(
    binding_set,
#if GTK_CHECK_VERSION(2,90,7)
    GDK_KEY_Down,
#else
    GDK_Down,
#endif
    0,
    "move",
    1,
    G_TYPE_ENUM,
    GTK_DIR_DOWN
  );

  gtk_binding_entry_add_signal(
    binding_set,
#if GTK_CHECK_VERSION(2,90,7)
    GDK_KEY_KP_Down,
#else
    GDK_KP_Down,
#endif
    0,
    "move",
    1,
    G_TYPE_ENUM,
    GTK_DIR_DOWN
  );

  gtk_binding_entry_add_signal(
    binding_set,
#if GTK_CHECK_VERSION(2,90,7)
    GDK_KEY_Right,
#else
    GDK_Right,
#endif
    0,
    "move",
    1,
    G_TYPE_ENUM,
    GTK_DIR_RIGHT
  );

  gtk_binding_entry_add_signal(
    binding_set,
#if GTK_CHECK_VERSION(2,90,7)
    GDK_KEY_KP_Right,
#else
    GDK_KP_Right,
#endif
    0,
    "move",
    1,
    G_TYPE_ENUM,
    GTK_DIR_RIGHT
  );

  gtk_binding_entry_add_signal(
    binding_set,
#if GTK_CHECK_VERSION(2,90,7)
    GDK_KEY_Left,
#else
    GDK_Left,
#endif
    0,
    "move",
    1,
    G_TYPE_ENUM,
    GTK_DIR_LEFT
  );

  gtk_binding_entry_add_signal(
    binding_set,
#if GTK_CHECK_VERSION(2,90,7)
    GDK_KEY_KP_Left,
#else
    GDK_KP_Left,
#endif
    0,
    "move",
    1,
    G_TYPE_ENUM,
    GTK_DIR_LEFT
  );
}

GType
inf_text_gtk_hue_chooser_get_type(void)
{
  static GType hue_chooser_type = 0;

  if(!hue_chooser_type)
  {
    static const GTypeInfo hue_chooser_type_info = {
      sizeof(InfTextGtkHueChooserClass),   /* class_size */
      NULL,                                /* base_init */
      NULL,                                /* base_finalize */
      inf_text_gtk_hue_chooser_class_init, /* class_init */
      NULL,                                /* class_finalize */
      NULL,                                /* class_data */
      sizeof(InfTextGtkHueChooser),        /* instance_size */
      0,                                   /* n_preallocs */
      inf_text_gtk_hue_chooser_init,       /* instance_init */
      NULL                                 /* value_table */
    };

    hue_chooser_type = g_type_register_static(
      GTK_TYPE_WIDGET,
      "InfTextGtkHueChooser",
      &hue_chooser_type_info,
      0
    );
  }

  return hue_chooser_type;
}

/*
 * Public API
 */

/**
 * inf_text_gtk_hue_chooser_new:
 *
 * Creates a new #InfTextGtkHueChooser widget with the initial hue set to 0.0
 * (red).
 *
 * Return Value: A newly created #InfTextGtkHueChooser.
 **/
GtkWidget*
inf_text_gtk_hue_chooser_new(void)
{
  GObject* object;
  object = g_object_new(INF_TEXT_GTK_TYPE_HUE_CHOOSER, NULL);
  return GTK_WIDGET(object);
}

/**
 * inf_text_gtk_hue_chooser_new_with_hue:
 * @hue: Initial hue value
 *
 * Creates a new #InfTextGtkHueChooser widget with the given hue as initial
 * value. @hue must be between 0.0 and 1.0.
 *
 * Return Value: A newly created #InfTextGtkHueChooser.
 **/
GtkWidget*
inf_text_gtk_hue_chooser_new_with_hue(gdouble hue)
{
  GObject* object;
  g_return_val_if_fail(hue >= 0.0 && hue <= 1.0, NULL);
  object = g_object_new(INF_TEXT_GTK_TYPE_HUE_CHOOSER, "hue", hue, NULL);
  return GTK_WIDGET(object);
}

/**
 * inf_text_gtk_hue_chooser_set_hue:
 * @chooser: A #InfTextGtkHueChooser.
 * @hue: New hue value.
 *
 * Sets the current hue value of @chooser to @hue. @hue must be between 0.0
 * and 1.0.
 */
void
inf_text_gtk_hue_chooser_set_hue(InfTextGtkHueChooser* chooser,
                                 gdouble hue)
{
  InfTextGtkHueChooserPrivate* priv;

  g_return_if_fail(INF_TEXT_GTK_IS_HUE_CHOOSER(chooser));
  g_return_if_fail(hue >= 0.0 && hue <= 1.0);

  priv = INF_TEXT_GTK_HUE_CHOOSER_PRIVATE(chooser);

  g_signal_emit(G_OBJECT(chooser), hue_chooser_signals[HUE_CHANGE], 0, hue);
}

/**
 * inf_text_gtk_hue_chooser_get_hue:
 * @chooser: A #InfTextGtkHueChooser.
 *
 * Returns the currently selected hue value of @chooser.
 *
 * Returns: The current hue value, a number between 0.0 and 1.0.
 */
gdouble
inf_text_gtk_hue_chooser_get_hue(InfTextGtkHueChooser* chooser)
{
  g_return_val_if_fail(INF_TEXT_GTK_IS_HUE_CHOOSER(chooser), 0.0);
  return INF_TEXT_GTK_HUE_CHOOSER_PRIVATE(chooser)->hue;
}

/* vim:set et sw=2 ts=2: */
