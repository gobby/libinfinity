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
 * SECTION:inf-text-gtk-hue-chooser
 * @title: InfTextGtkHueChooser
 * @short_description: A GTK+ widget for selecting a hue value
 * @include: libinftextgtk/inf-text-gtk-hue-chooser.h
 * @see_also: #InfTextGtkHueChooser
 * @stability: Unstable
 *
 * #InfTextGtkHueChooser is a widget which allows the user to select a hue
 * value without selecting also saturation and lightness at the same time. It
 * only presents the hue circle without the inner triangle.
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

static guint hue_chooser_signals[LAST_SIGNAL];

G_DEFINE_TYPE_WITH_CODE(InfTextGtkHueChooser, inf_text_gtk_hue_chooser, GTK_TYPE_WIDGET,
  G_ADD_PRIVATE(InfTextGtkHueChooser))

static const double INF_TEXT_GTK_HUE_CHOOSER_HUE_MOVE_DELTA = 0.002;

/*
 * Helper functions
 */

#define INTENSITY(r, g, b) ((r) * 0.30 + (g) * 0.59 + (b) * 0.11)

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

  gtk_widget_get_allocation(GTK_WIDGET(chooser), &allocation);

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

  gtk_widget_get_allocation(GTK_WIDGET(chooser), &allocation);

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

  gtk_widget_get_allocation(GTK_WIDGET(chooser), &allocation);

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

      gtk_hsv_to_rgb(hue, 1.0, 1.0, &r, &g, &b);

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
   * will get properly clipped at the edges of the ring */
  source_cr = cairo_create(source);

  gtk_hsv_to_rgb(priv->hue, 1.0, 1.0, &r, &g, &b);

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
inf_text_gtk_hue_chooser_init(InfTextGtkHueChooser* hue_chooser)
{
  InfTextGtkHueChooserPrivate* priv;
  priv = INF_TEXT_GTK_HUE_CHOOSER_PRIVATE(hue_chooser);

  gtk_widget_set_has_window(GTK_WIDGET(hue_chooser), FALSE);
  gtk_widget_set_can_focus(GTK_WIDGET(hue_chooser), TRUE);

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

  G_OBJECT_CLASS(inf_text_gtk_hue_chooser_parent_class)->dispose(object);
}

static void
inf_text_gtk_hue_chooser_finalize(GObject* object)
{
  InfTextGtkHueChooser* hue_chooser;
  InfTextGtkHueChooserPrivate* priv;

  hue_chooser = INF_TEXT_GTK_HUE_CHOOSER(object);
  priv = INF_TEXT_GTK_HUE_CHOOSER_PRIVATE(hue_chooser);

  G_OBJECT_CLASS(inf_text_gtk_hue_chooser_parent_class)->finalize(object);
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

  GTK_WIDGET_CLASS(inf_text_gtk_hue_chooser_parent_class)->map(widget);
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
  GTK_WIDGET_CLASS(inf_text_gtk_hue_chooser_parent_class)->unmap(widget);
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

  gtk_widget_set_realized(widget, TRUE);
  gtk_widget_get_allocation(widget, &allocation);

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

  gtk_widget_set_window(widget, parent_window);

  priv->window = gdk_window_new(parent_window, &attr, GDK_WA_X | GDK_WA_Y);
  gdk_window_set_user_data(priv->window, chooser);
  gdk_window_show(priv->window);
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

  GTK_WIDGET_CLASS(inf_text_gtk_hue_chooser_parent_class)->unrealize(widget);
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

static void
inf_text_gtk_hue_chooser_size_allocate(GtkWidget* widget,
                                       GtkAllocation* allocation)
{
  InfTextGtkHueChooser* chooser;
  InfTextGtkHueChooserPrivate* priv;

  chooser = INF_TEXT_GTK_HUE_CHOOSER(widget);
  priv = INF_TEXT_GTK_HUE_CHOOSER_PRIVATE(chooser);

  gtk_widget_set_allocation(widget, allocation);

  if(gtk_widget_get_realized(widget))
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

    g_object_unref(cursor);
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

  gdk_device_ungrab(
    gdk_event_get_device((GdkEvent*)event),
    event->time
  );

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

static gboolean
inf_text_gtk_hue_chooser_focus(GtkWidget* widget,
                               GtkDirectionType dir)
{
  InfTextGtkHueChooser* chooser;
  InfTextGtkHueChooserPrivate* priv;

  chooser = INF_TEXT_GTK_HUE_CHOOSER(widget);
  priv = INF_TEXT_GTK_HUE_CHOOSER_PRIVATE(chooser);

  if(!gtk_widget_has_focus(widget))
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
inf_text_gtk_hue_chooser_class_init(
  InfTextGtkHueChooserClass* hue_chooser_class)
{
  GObjectClass* object_class;
  GtkWidgetClass* widget_class;
  GtkBindingSet* binding_set;

  object_class = G_OBJECT_CLASS(hue_chooser_class);
  widget_class = GTK_WIDGET_CLASS(hue_chooser_class);

  object_class->dispose = inf_text_gtk_hue_chooser_dispose;
  object_class->finalize = inf_text_gtk_hue_chooser_finalize;
  object_class->set_property = inf_text_gtk_hue_chooser_set_property;
  object_class->get_property = inf_text_gtk_hue_chooser_get_property;

  widget_class->map = inf_text_gtk_hue_chooser_map;
  widget_class->unmap = inf_text_gtk_hue_chooser_unmap;
  widget_class->realize = inf_text_gtk_hue_chooser_realize;
  widget_class->unrealize = inf_text_gtk_hue_chooser_unrealize;
  widget_class->get_preferred_height =
    inf_text_gtk_hue_chooser_get_preferred_height;
  widget_class->get_preferred_width =
    inf_text_gtk_hue_chooser_get_preferred_width;
  widget_class->size_allocate = inf_text_gtk_hue_chooser_size_allocate;
  widget_class->button_press_event =
    inf_text_gtk_hue_chooser_button_press_event;
  widget_class->button_release_event =
    inf_text_gtk_hue_chooser_button_release_event;
  widget_class->motion_notify_event =
    inf_text_gtk_hue_chooser_motion_notify_event;
  widget_class->draw = inf_text_gtk_hue_chooser_draw;
  widget_class->focus = inf_text_gtk_hue_chooser_focus;
  widget_class->grab_broken_event = inf_text_gtk_hue_chooser_grab_broken_event;

  hue_chooser_class->hue_change = inf_text_gtk_hue_chooser_hue_change;
  hue_chooser_class->move = inf_text_gtk_hue_chooser_move;

  /**
   * InfTextGtkHueChooser::hue-change:
   * @chooser: The #InfTextGtkHueChooser emitting the signal.
   * @hue: The new hue value.
   *
   * This signal is emitted whenever the hue value is changed.
   */
  hue_chooser_signals[HUE_CHANGE] = g_signal_new(
    "hue-change",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfTextGtkHueChooserClass, hue_change),
    NULL, NULL,
    inf_marshal_VOID__DOUBLE,
    G_TYPE_NONE,
    1,
    G_TYPE_DOUBLE
  );

  /**
   * InfTextGtkHueChooser::move:
   * @chooser: The #InfTextGtkHueChooser emitting the signal.
   * @direction: The direction in which the move was mode.
   *
   * This is an action signal emitted when the selection is moved by the user.
   */
  hue_chooser_signals[MOVE] = g_signal_new(
    "move",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
    G_STRUCT_OFFSET(InfTextGtkHueChooserClass, move),
    NULL, NULL,
    inf_marshal_VOID__ENUM,
    G_TYPE_NONE,
    1,
    GTK_TYPE_DIRECTION_TYPE
  );

  g_object_class_install_property(
    object_class,
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

  binding_set = gtk_binding_set_by_class(object_class);

  gtk_binding_entry_add_signal(
    binding_set,
    GDK_KEY_Up,
    0,
    "move",
    1,
    G_TYPE_ENUM,
    GTK_DIR_UP
  );

  gtk_binding_entry_add_signal(
    binding_set,
    GDK_KEY_KP_Up,
    0,
    "move",
    1,
    G_TYPE_ENUM,
    GTK_DIR_UP
  );

  gtk_binding_entry_add_signal(
    binding_set,
    GDK_KEY_Down,
    0,
    "move",
    1,
    G_TYPE_ENUM,
    GTK_DIR_DOWN
  );

  gtk_binding_entry_add_signal(
    binding_set,
    GDK_KEY_KP_Down,
    0,
    "move",
    1,
    G_TYPE_ENUM,
    GTK_DIR_DOWN
  );

  gtk_binding_entry_add_signal(
    binding_set,
    GDK_KEY_Right,
    0,
    "move",
    1,
    G_TYPE_ENUM,
    GTK_DIR_RIGHT
  );

  gtk_binding_entry_add_signal(
    binding_set,
    GDK_KEY_KP_Right,
    0,
    "move",
    1,
    G_TYPE_ENUM,
    GTK_DIR_RIGHT
  );

  gtk_binding_entry_add_signal(
    binding_set,
    GDK_KEY_Left,
    0,
    "move",
    1,
    G_TYPE_ENUM,
    GTK_DIR_LEFT
  );

  gtk_binding_entry_add_signal(
    binding_set,
    GDK_KEY_KP_Left,
    0,
    "move",
    1,
    G_TYPE_ENUM,
    GTK_DIR_LEFT
  );
}

/*
 * Public API
 */

/**
 * inf_text_gtk_hue_chooser_new: (constructor)
 *
 * Creates a new #InfTextGtkHueChooser widget with the initial hue set to 0.0
 * (red).
 *
 * Returns: (transfer floating): A newly created #InfTextGtkHueChooser.
 **/
GtkWidget*
inf_text_gtk_hue_chooser_new(void)
{
  GObject* object;
  object = g_object_new(INF_TEXT_GTK_TYPE_HUE_CHOOSER, NULL);
  return GTK_WIDGET(object);
}

/**
 * inf_text_gtk_hue_chooser_new_with_hue: (constructor)
 * @hue: Initial hue value
 *
 * Creates a new #InfTextGtkHueChooser widget with the given hue as initial
 * value. @hue must be between 0.0 and 1.0.
 *
 * Returns: (transfer floating): A newly created #InfTextGtkHueChooser.
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
