/* infinote - Collaborative notetaking application
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
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/**
 * SECTION:inf-text-gtk-view
 * @title: InfTextGtkView
 * @short_description: Drawing remote cursors and selections in a #GtkTextView
 * @include: libinftextgtk/inf-text-gtk-view.h
 * @see_also: #InfTextGtkBuffer, #InfTextGtkViewport
 * @stability: Unstable
 *
 * #InfTextGtkView is a helper object which, as long as it is alive, draws
 * the cursor position, selected text of remote users into a #GtkTextView
 * widget. It can also highlight the current line of a remote user in a
 * similar way the #GtkSourceView widget can highlight the current line of
 * the local user.
 *
 * This functionality was not implemented by subclassing #GtkTextView such
 * that it can also be used with existing subclasses, such as #GtkSourceView.
 *
 * With the function inf_text_gtk_view_set_active_user() the local
 * #InfTextUser who is editing the text can be set, if there is any. The
 * insertion position, seleceted text or current line are not drawn for this
 * user, since the normal #GtkTextView takes care of that already. This
 * assumes the local user cursor position and selection is synchronized to the
 * corresponding #InfTextUser properties, which is automatically the case when
 * a #InfTextGtkBuffer is used for the buffer the #GtkTextView is displaying.
 *
 * See #InfTextGtkViewport for drawing a marker at remote users' location into
 * the scrollbar.
 */	

#include <libinftextgtk/inf-text-gtk-view.h>
#include <libinfinity/inf-signals.h>
#include <gdk/gdk.h>

typedef struct _InfTextGtkViewUser InfTextGtkViewUser;
struct _InfTextGtkViewUser {
  InfTextGtkView* view;
  InfTextUser* user;
  gboolean cursor_visible;
  InfIoTimeout* timeout; /* TODO: Use glib for that; remove InfIo property */
  guint revalidate_idle;

  /* All in buffer coordinates: */

  /* The rectangular area occupied by the cursor */
  GdkRectangle cursor_rect;
  /* The position and height of the selection bound. width is ignored. */
  GdkRectangle selection_bound_rect;

  /* Current line */
  gint line_y;
  gint line_height;
};

/* Helper struct for redrawing selection area */
typedef struct _InfTextGtkViewUserToggle InfTextGtkViewUserToggle;
struct _InfTextGtkViewUserToggle {
  /* User toggled on or off at this point */
  InfTextGtkViewUser* user;
  /* Position of the toggle */
  guint pos;
  /* NULL if this toggles on, or the corresponding on
   * toggle if it toggles off. */
  InfTextGtkViewUserToggle* on_toggle;
  /* Position in textview, in window coordinates */
  gint x;
  gint y;
};

typedef struct _InfTextGtkViewPrivate InfTextGtkViewPrivate;
struct _InfTextGtkViewPrivate {
  InfIo* io;
  GtkTextView* textview;
  InfUserTable* user_table;
  InfTextUser* active_user;
  GSList* users;
  
  gboolean show_remote_cursors;
  gboolean show_remote_selections;
  gboolean show_remote_current_lines;
};

enum {
  PROP_0,

  /* construct only */
  PROP_IO,
  PROP_VIEW,
  PROP_USER_TABLE,

  /* read/write */
  PROP_ACTIVE_USER,
  
  PROP_SHOW_REMOTE_CURSORS,
  PROP_SHOW_REMOTE_SELECTIONS,
  PROP_SHOW_REMOTE_CURRENT_LINES
};

#define INF_TEXT_GTK_VIEW_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TEXT_GTK_TYPE_VIEW, InfTextGtkViewPrivate))

G_DEFINE_TYPE_WITH_CODE(InfTextGtkView, inf_text_gtk_view, G_TYPE_OBJECT,
  G_ADD_PRIVATE(InfTextGtkView))

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

/* Compute cursor_rect, selection_bound_rect */
static void
inf_text_gtk_view_user_compute_user_area(InfTextGtkViewUser* view_user)
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

  /* Find current line */
  gtk_text_view_get_line_yrange(
    priv->textview,
    &iter,
    &view_user->line_y,
    &view_user->line_height
  );

  /* TODO: We don't need the cursor rect for show-remote-current-lines, and
   * we don't need the selection rect for show-remote-cursors and
   * show-remote-current-lines. So we might not even want to compute them in
   * if those are disabled. */

  /* Find cursor position */
  gtk_text_view_get_iter_location(
    priv->textview,
    &iter,
    &view_user->cursor_rect
  );

  view_user->cursor_rect.width = MAX(
    (int)(view_user->cursor_rect.height * cursor_aspect_ratio),
    1
  );

  /* Find selection bound */
  gtk_text_iter_forward_chars(
    &iter,
    inf_text_user_get_selection_length(view_user->user)
  );

  gtk_text_view_get_iter_location(
    priv->textview,
    &iter,
    &view_user->selection_bound_rect
  );

  view_user->selection_bound_rect.width = MAX(
    (int)(view_user->selection_bound_rect.height * cursor_aspect_ratio),
    1
  );
}

static guint
inf_text_gtk_view_get_left_margin(GtkTextView* view)
{
  GtkAdjustment* hadjustment;
  gint margin;
  gint hadj;

  hadjustment = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(view));

  margin = gtk_text_view_get_left_margin(view);
  if(!hadjustment) return margin;

  hadj = gtk_adjustment_get_value(hadjustment);
  if(hadj < margin) return margin - hadj;

  return 0;
}

static guint
inf_text_gtk_view_get_right_margin(GtkTextView* view)
{
  GtkAdjustment* hadjustment;
  gint margin;
  gdouble hadj;
  gdouble hupper;
  gdouble hpage;

  hadjustment = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(view));

  margin = gtk_text_view_get_right_margin(view);
  if(!hadjustment) return margin;

  /* TODO: I am not exactly sure where this +1 comes from, but it is required
   * so that the selection is aligned with the local selection at the right
   * margin. */
  hadj = gtk_adjustment_get_value(hadjustment) + 1;

  g_object_get(
    G_OBJECT(hadjustment),
    "upper", &hupper,
    "page-size", &hpage,
    NULL
  );

  if(hadj > hupper - hpage - margin)
    return margin - (gint)(hupper - hpage - hadj);

  return 0;
}

/* Invalidate the whole area of the textview covered by the given user:
 * cursor, selection, current line */
static void
inf_text_gtk_view_user_invalidate_user_area(InfTextGtkViewUser* view_user)
{
  InfTextGtkViewPrivate* priv;
  GdkWindow* window;
  GdkRectangle invalidate_rect;
  gint selection_bound_x;
  gint selection_bound_y;
  gint window_width;

  priv = INF_TEXT_GTK_VIEW_PRIVATE(view_user->view);

  if(gtk_widget_get_realized(GTK_WIDGET(priv->textview)))
  {
    /* Invalidate cursors/selections */
    if(priv->show_remote_cursors || priv->show_remote_selections ||
       priv->show_remote_current_lines)
    {
      window = gtk_text_view_get_window(priv->textview, GTK_TEXT_WINDOW_TEXT);
      window_width = gdk_window_get_width(window);

      gtk_text_view_buffer_to_window_coords(
        priv->textview,
        GTK_TEXT_WINDOW_TEXT,
        view_user->cursor_rect.x, view_user->cursor_rect.y,
        &invalidate_rect.x, &invalidate_rect.y
      );

      invalidate_rect.width = view_user->cursor_rect.width;
      invalidate_rect.height = view_user->cursor_rect.height;

      /* Don't check for InfTextUser's selection length here so that clearing
       * a previous selection works. */
      if(priv->show_remote_selections &&
         (view_user->selection_bound_rect.x != view_user->cursor_rect.x ||
          view_user->selection_bound_rect.y != view_user->cursor_rect.y))
      {
        gtk_text_view_buffer_to_window_coords(
          priv->textview,
          GTK_TEXT_WINDOW_TEXT,
          view_user->selection_bound_rect.x,
          view_user->selection_bound_rect.y,
          &selection_bound_x, &selection_bound_y
        );

        /* Invalidate the whole area between cursor and selection bound */
        if(selection_bound_y == invalidate_rect.y)
        {
          /* Cursor and selection bound are on the same line */
          if(selection_bound_x > invalidate_rect.x)
          {
            /* Selection bound is to the right of cursor */
            invalidate_rect.width = MAX(
              selection_bound_x - invalidate_rect.x,
              invalidate_rect.width
            );
          }
          else
          {
            /* Selection bound is to the left of cursor */
            invalidate_rect.width += (invalidate_rect.x - selection_bound_x);
            invalidate_rect.x = selection_bound_x;
          }
        }
        else
        {
          /* Cursor and selection bound are on different lines. Could split
           * the actual area to be invalidated into three rectangles here,
           * but let's just do the union for simplicity reasons. */
          invalidate_rect.width = window_width;
          invalidate_rect.height = MAX(
            invalidate_rect.y + invalidate_rect.height,
            selection_bound_y + view_user->selection_bound_rect.height
          ) - MIN(invalidate_rect.y, selection_bound_y);

          invalidate_rect.x =
            inf_text_gtk_view_get_left_margin(priv->textview);
          invalidate_rect.y = MIN(invalidate_rect.y, selection_bound_y);

          invalidate_rect.width -=
            inf_text_gtk_view_get_left_margin(priv->textview) +
            inf_text_gtk_view_get_right_margin(priv->textview);
        }

        gdk_window_invalidate_rect(window, &invalidate_rect, FALSE);
      }

      /* Invalidate current lines */
      if(priv->show_remote_current_lines)
      {
        gtk_text_view_buffer_to_window_coords(
          priv->textview,
          GTK_TEXT_WINDOW_TEXT,
          0, view_user->line_y,
          NULL, &invalidate_rect.y
        );

        /* -1 to stay consistent with GtkSourceView */
        invalidate_rect.x =
          inf_text_gtk_view_get_left_margin(priv->textview) - 1;
        invalidate_rect.width = window_width - invalidate_rect.x;
        invalidate_rect.height = view_user->line_height;
      }

      gdk_window_invalidate_rect(window, &invalidate_rect, FALSE);
    }
  }
}

static gint
inf_text_gtk_view_user_line_position_cmp(gconstpointer first,
                                         gconstpointer second)
{
  const InfTextGtkViewUser* first_user;
  const InfTextGtkViewUser* second_user;

  first_user = (const InfTextGtkViewUser*)first;
  second_user = (const InfTextGtkViewUser*)second;

  if(second_user->line_y < first_user->line_y)
    return 1;
  else if(second_user->line_y > first_user->line_y)
    return -1;

  return 0;

}

static gint
inf_text_gtk_view_user_toggle_position_cmp(gconstpointer first,
                                           gconstpointer second,
                                           gpointer user_data)
{
  const InfTextGtkViewUserToggle* first_toggle;
  const InfTextGtkViewUserToggle* second_toggle;

  first_toggle = (const InfTextGtkViewUserToggle*)first;
  second_toggle = (const InfTextGtkViewUserToggle*)second;

  if(second_toggle->pos < first_toggle->pos)
    return 1;
  else if(second_toggle->pos > first_toggle->pos)
    return -1;

  return 0;
}

static gint
inf_text_gtk_view_user_toggle_user_cmp(gconstpointer first,
                                       gconstpointer second)
{
  const InfTextGtkViewUserToggle* first_toggle;
  const InfTextGtkViewUserToggle* second_toggle;
  guint first_id;
  guint second_id;

  first_toggle = (const InfTextGtkViewUserToggle*)first;
  second_toggle = (const InfTextGtkViewUserToggle*)second;
  first_id = inf_user_get_id(INF_USER(first_toggle->user->user));
  second_id = inf_user_get_id(INF_USER(second_toggle->user->user));

  if(second_id < first_id)
    return 1;
  else if(second_id > first_id)
    return -1;

  return 0;
}

static void
inf_text_gtk_view_user_toggle_free(gpointer data)
{
  g_slice_free(InfTextGtkViewUserToggle, data);
}

static InfTextGtkViewUserToggle*
inf_text_gtk_view_add_user_toggle(GSequence* sequence,
                                  guint position,
                                  InfTextGtkViewUser* user,
                                  InfTextGtkViewUserToggle* on_toggle,
                                  gint x,
                                  gint y)
{
  InfTextGtkViewUserToggle* toggle;
  toggle = g_slice_new(InfTextGtkViewUserToggle);
  toggle->user = user;
  toggle->pos = position;
  toggle->on_toggle = on_toggle;
  toggle->x = x;
  toggle->y = y;

  g_sequence_insert_sorted(
    sequence,
    toggle,
    inf_text_gtk_view_user_toggle_position_cmp,
    NULL
  );

  return toggle;
}

static void
inf_text_gtk_view_add_user_toggle_pair(GSequence* sequence,
                                       guint begin,
                                       guint end,
                                       InfTextGtkViewUser* user,
                                       gint begin_x,
                                       gint begin_y,
                                       gint end_x,
                                       gint end_y)
{
  InfTextGtkViewUserToggle* begin_toggle;

  g_assert(end > begin);

  begin_toggle = inf_text_gtk_view_add_user_toggle(
    sequence,
    begin,
    user,
    NULL,
    begin_x,
    begin_y
  );

  inf_text_gtk_view_add_user_toggle(
    sequence,
    end,
    user,
    begin_toggle,
    end_x,
    end_y
  );
}

static gboolean
inf_text_gtk_view_draw_before_cb(GtkWidget* widget,
                                 cairo_t* cr,
                                 gpointer user_data)
{
  InfTextGtkView* view;
  InfTextGtkViewPrivate* priv;
  GSList* item;
  GSList* prev_item;
  InfTextGtkViewUser* prev_user;
  InfTextGtkViewUser* view_user;
  GtkAdjustment* hadjustment;
  GtkAdjustment* vadjustment;
  GdkWindow *text_window;

  GtkStyleContext* style;
  GdkRGBA bg;
  double h, s, v;
  double r, g, b;

  GSList* sort_users;
  GdkRectangle rect;
  gint window_width;
  gint rx, ry;
  GdkRectangle clip_area;
  cairo_pattern_t* pattern;
  double n, n_users;
  cairo_matrix_t matrix;

  view = INF_TEXT_GTK_VIEW(user_data);
  priv = INF_TEXT_GTK_VIEW_PRIVATE(view);

  text_window = gtk_text_view_get_window(priv->textview, GTK_TEXT_WINDOW_TEXT);

  if(!gtk_cairo_should_draw_window(cr, text_window))
  {
    return FALSE;
  }

  if(priv->show_remote_current_lines)
  {
    gtk_cairo_transform_to_window(cr, GTK_WIDGET(priv->textview), text_window);

    gdk_cairo_get_clip_rectangle(cr, &clip_area);

    window_width = gdk_window_get_width(text_window);

    /* Make current line color depend on background. */
    style = gtk_widget_get_style_context(GTK_WIDGET(priv->textview));
    gtk_style_context_save(style);
    gtk_style_context_add_class(style, GTK_STYLE_CLASS_VIEW);
    gtk_style_context_get_background_color(style, GTK_STATE_FLAG_NORMAL, &bg);
    gtk_style_context_restore(style);

    gtk_rgb_to_hsv(bg.red, bg.green, bg.blue, &h, &s, &v);
    v = MAX(v, 0.3);
    s = MAX(s, 0.1 + 0.3*(1 - v));

    sort_users = g_slist_copy(priv->users);
    sort_users =
      g_slist_sort(sort_users, inf_text_gtk_view_user_line_position_cmp);

    prev_item = sort_users;
    if(prev_item) prev_user = (InfTextGtkViewUser*)prev_item->data;
    n_users = 1.0;

    for(item = sort_users; item != NULL; item = item->next, n_users += 1.0)
    {
      if(item->next == NULL ||
         ((InfTextGtkViewUser*)item->next->data)->line_y != prev_user->line_y)
      {
        gtk_text_view_buffer_to_window_coords(
          priv->textview,
          GTK_TEXT_WINDOW_TEXT,
          0, prev_user->line_y,
          NULL, &rect.y
        );

        /* -1 to stay consistent with GtkSourceView */
        rect.x = inf_text_gtk_view_get_left_margin(priv->textview) - 1;
        rect.width = window_width - rect.x;
        rect.height = prev_user->line_height;

        if(gdk_rectangle_intersect(&clip_area, &rect, NULL))
        {
          hadjustment =
            gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(priv->textview));
          vadjustment =
            gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(priv->textview));

          /* Construct pattern */
          rx = gtk_adjustment_get_value(vadjustment);
          ry = gtk_adjustment_get_value(hadjustment);
          pattern =
            cairo_pattern_create_linear(0, 0, 3.5*n_users, 3.5*n_users);
          cairo_matrix_init_translate(&matrix, rx, ry);
          cairo_pattern_set_matrix(pattern, &matrix);
          cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);

          for(n = 0.0;
              prev_item != item->next;
              prev_item = prev_item->next, n += 1.0)
          {
            view_user = (InfTextGtkViewUser*)prev_item->data;
            h = inf_text_user_get_hue(view_user->user);
            gtk_hsv_to_rgb(h, s, v, &r, &g, &b);

            cairo_pattern_add_color_stop_rgb(
              pattern,
              n/n_users,
              r, g, b
            );

            cairo_pattern_add_color_stop_rgb(
              pattern,
              (n+1.0)/n_users,
              r, g, b
            );
          }

          cairo_set_source(cr, pattern);
          gdk_cairo_rectangle(cr, &rect);
          cairo_fill(cr);
          cairo_pattern_destroy(pattern);
        }

        prev_item = item->next;
        if(prev_item) prev_user = (InfTextGtkViewUser*)prev_item->data;
        n_users = 0.0;
      }
    }

    g_slist_free(sort_users);
  }

  return FALSE;
}

static gboolean
inf_text_gtk_view_draw_after_cb(GtkWidget* widget,
                                cairo_t* cr,
                                gpointer user_data)
{
  InfTextGtkView* view;
  InfTextGtkViewPrivate* priv;
  gint window_width;
  GtkStyleContext* style;
  GdkColor* cursor_color;
  GdkRGBA fg;
  double hc,sc,vc;
  double hs,ss,vs;
  GSList* item;
  InfTextGtkViewUser* view_user;
  double rc,gc,bc;
  double rs,gs,bs;

  GdkRectangle clip_area;

  gint ax, ay;
  GtkTextIter begin_iter;
  GtkTextIter end_iter;
  guint area_begin;
  guint area_end;

  guint own_sel_begin;
  guint own_sel_end;
  gint osbx, osby;
  gint osex, osey;

  gint sel;
  guint begin;
  guint end;
  GSequence* toggles;

  GSequenceIter* tog_iter;
  InfTextGtkViewUserToggle* cur_toggle;
  InfTextGtkViewUserToggle* prev_toggle;
  guint n_users;
  GSList* users;
  cairo_pattern_t* pattern;
  GtkAdjustment* hadjustment;
  GtkAdjustment* vadjustment;
  double n;
  cairo_matrix_t matrix;

  GdkRectangle rct;
  gint rx, ry;
  GdkWindow *text_window;

  view = INF_TEXT_GTK_VIEW(user_data);
  priv = INF_TEXT_GTK_VIEW_PRIVATE(view);

  text_window = gtk_text_view_get_window(priv->textview, GTK_TEXT_WINDOW_TEXT);

  if(!gtk_cairo_should_draw_window(cr, text_window))
  {
    return FALSE;
  }

  gtk_cairo_transform_to_window(cr, GTK_WIDGET(priv->textview), text_window);
  gdk_cairo_get_clip_rectangle(cr, &clip_area);

  style = gtk_widget_get_style_context(GTK_WIDGET(priv->textview));
  gtk_style_context_save(style);
  gtk_style_context_add_class(style, GTK_STYLE_CLASS_VIEW);
  gtk_style_context_get_color(style, GTK_STATE_FLAG_NORMAL, &fg);
  gtk_style_context_restore(style);

  if(priv->show_remote_selections)
  {
    window_width = gdk_window_get_width(text_window);

    /* Make selection color based on text color: If text is dark, selection
     * is dark, if text is bright selection is bright. Note that we draw with
     * 50% alpha only, so text remains readable. */
    gtk_rgb_to_hsv(fg.red, fg.green, fg.blue, &hs, &ss, &vs);
    vs = MAX(vs, 0.5);
    ss = 1.0 - 0.4*(vs);

    /* Find range of text to be updated */
    gtk_text_view_window_to_buffer_coords(
      priv->textview,
      GTK_TEXT_WINDOW_TEXT,
      clip_area.x, clip_area.y,
      &ax, &ay
    );

    gtk_text_view_get_iter_at_location(
      priv->textview,
      &begin_iter,
      ax,
      ay
    );

    gtk_text_view_get_iter_at_location(
      priv->textview,
      &end_iter,
      ax + clip_area.width,
      ay + clip_area.height
    );

    area_begin = gtk_text_iter_get_offset(&begin_iter);
    area_end = gtk_text_iter_get_offset(&end_iter);
    g_assert(area_end >= area_begin);

    /* Find own selection (we don't draw remote
     * selections over own selection). */
    gtk_text_buffer_get_selection_bounds(
      gtk_text_view_get_buffer(priv->textview),
      &begin_iter,
      &end_iter
    );

    own_sel_begin = gtk_text_iter_get_offset(&begin_iter);
    own_sel_end = gtk_text_iter_get_offset(&end_iter);
    if(own_sel_begin != own_sel_end)
    {
      gtk_text_view_get_iter_location(priv->textview, &begin_iter, &rct);
      gtk_text_view_buffer_to_window_coords(
        priv->textview,
        GTK_TEXT_WINDOW_TEXT,
        rct.x, rct.y,
        &osbx, &osby
      );

      gtk_text_view_get_iter_location(priv->textview, &end_iter, &rct);
      gtk_text_view_buffer_to_window_coords(
        priv->textview,
        GTK_TEXT_WINDOW_TEXT,
        rct.x, rct.y,
        &osex, &osey
      );
    }

    /* Build toggle list */
    toggles = g_sequence_new(inf_text_gtk_view_user_toggle_free);
    for(item = priv->users; item != NULL; item = item->next)
    {
      view_user = (InfTextGtkViewUser*)item->data;
      if(inf_text_user_get_selection_length(view_user->user) != 0)
      {
        begin = inf_text_user_get_caret_position(view_user->user);
        sel = inf_text_user_get_selection_length(view_user->user);

        if(sel > 0)
        {
          end = begin + sel;
        }
        else
        {
          g_assert(begin >= (unsigned int)-sel);

          end = begin;
          begin += sel;
        }

        /* This can happen if the document is not yet fully loaded, i.e. synchronization
         * is still in progress. */
        if(begin > gtk_text_buffer_get_char_count(gtk_text_view_get_buffer(priv->textview)))
          begin = gtk_text_buffer_get_char_count(gtk_text_view_get_buffer(priv->textview));
        if(end > gtk_text_buffer_get_char_count(gtk_text_view_get_buffer(priv->textview)))
          end = gtk_text_buffer_get_char_count(gtk_text_view_get_buffer(priv->textview));

        begin = MIN(MAX(begin, area_begin), area_end);
        end = MIN(MAX(end, area_begin), area_end);
        g_assert(end >= begin);

        if(begin != end)
        {
          if(sel > 0)
          {
            gtk_text_view_buffer_to_window_coords(
              priv->textview,
              GTK_TEXT_WINDOW_TEXT,
              view_user->cursor_rect.x,
              view_user->cursor_rect.y,
              &rx, &ry
            );

            gtk_text_view_buffer_to_window_coords(
              priv->textview,
              GTK_TEXT_WINDOW_TEXT,
              view_user->selection_bound_rect.x,
              view_user->selection_bound_rect.y,
              &ax, &ay
            );
          }
          else
          {
            gtk_text_view_buffer_to_window_coords(
              priv->textview,
              GTK_TEXT_WINDOW_TEXT,
              view_user->selection_bound_rect.x,
              view_user->selection_bound_rect.y,
              &rx, &ry
            );

            gtk_text_view_buffer_to_window_coords(
              priv->textview,
              GTK_TEXT_WINDOW_TEXT,
              view_user->cursor_rect.x,
              view_user->cursor_rect.y,
              &ax, &ay
            );
          }

          if(own_sel_begin == own_sel_end ||
             own_sel_end <= begin || own_sel_begin >= end)
          {
            /* Local selection and remote selection do not overlap */
            inf_text_gtk_view_add_user_toggle_pair(
              toggles,
              begin, end,
              view_user,
              rx, ry,
              ax, ay
            );
          }
          else if(own_sel_begin <= begin && own_sel_end >= end)
          {
            /* Whole remote selection is covered by local selection */
          }
          else if(own_sel_begin > begin && own_sel_end >= end)
          {
            /* Last part of remote selection is covered by local selection */
            inf_text_gtk_view_add_user_toggle_pair(
              toggles,
              begin, own_sel_begin,
              view_user,
              rx, ry,
              osbx, osby
            );
          }
          else if(own_sel_begin <= begin && own_sel_end < end)
          {
            /* First part of remote selection is covered by local selection */
            inf_text_gtk_view_add_user_toggle_pair(
              toggles,
              own_sel_end, end,
              view_user,
              osex, osey,
              ax, ay
            );
          }
          else if(own_sel_begin > begin && own_sel_end < end)
          {
            /* Local selection is in middle of remote selection */
            inf_text_gtk_view_add_user_toggle_pair(
              toggles,
              begin, own_sel_begin,
              view_user,
              rx, ry,
              osbx, osby
            );

            inf_text_gtk_view_add_user_toggle_pair(
              toggles,
              own_sel_end, end,
              view_user,
              osex, osey,
              ax, ay
            );
          }
          else
          {
            g_assert_not_reached();
          }
        }
      }
    }

    /* Walk toggle list, draw selections */
    tog_iter = g_sequence_get_begin_iter(toggles);
    cur_toggle = NULL;
    prev_toggle = NULL;
    users = NULL;
    n_users = 0;

    tog_iter = g_sequence_get_begin_iter(toggles);
    while(!g_sequence_iter_is_end(tog_iter))
    {
      cur_toggle = (InfTextGtkViewUserToggle*)g_sequence_get(tog_iter);

      /* Draw users from prev_toggle to cur_toggle */
      if(users != NULL)
      {
        g_assert(prev_toggle != NULL);
        g_assert(n_users > 0);

        hadjustment =
          gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(priv->textview));
        vadjustment =
          gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(priv->textview));

        /* Construct pattern */
        rx = gtk_adjustment_get_value(hadjustment);
        ry = gtk_adjustment_get_value(vadjustment);
        pattern =
          cairo_pattern_create_linear(0, 0, 3.5*n_users, 3.5*n_users);
        cairo_matrix_init_translate(&matrix, rx, ry);
        cairo_pattern_set_matrix(pattern, &matrix);
        cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
        for(item = users, n = 0.0; item != NULL; item = item->next, n += 1.0)
        {
          view_user = ((InfTextGtkViewUserToggle*)item->data)->user;
          hs = inf_text_user_get_hue(view_user->user);
          gtk_hsv_to_rgb(hs, ss, vs, &rs, &gs, &bs);

          cairo_pattern_add_color_stop_rgba(
            pattern,
            n/n_users,
            rs, gs, bs, 0.5
          );

          cairo_pattern_add_color_stop_rgba(
            pattern,
            (n+1.0)/n_users,
            rs, gs, bs, 0.5
          );
        }

        cairo_set_source(cr, pattern);
        if(prev_toggle->y == cur_toggle->y)
        {
          /* same line */
          g_assert(prev_toggle->x < cur_toggle->x);

          rct.x = prev_toggle->x;
          rct.y = prev_toggle->y;
          rct.width = cur_toggle->x - prev_toggle->x;
          rct.height = cur_toggle->user->selection_bound_rect.height;
          gdk_cairo_rectangle(cr, &rct);
        }
        else
        {
          g_assert(
            cur_toggle->y - prev_toggle->y >=
            cur_toggle->user->selection_bound_rect.height
          );

          /* multiple lines */
          if(window_width > prev_toggle->x)
          {
            /* first line */
            rct.x = prev_toggle->x;
            rct.y = prev_toggle->y;
            rct.width = window_width - prev_toggle->x -
              inf_text_gtk_view_get_right_margin(priv->textview);
            rct.height = prev_toggle->user->selection_bound_rect.height;
            gdk_cairo_rectangle(cr, &rct);
          }

          if(cur_toggle->x > 0)
          {
            /* last line */
            rct.x = inf_text_gtk_view_get_left_margin(priv->textview);
            rct.y = cur_toggle->y;
            rct.width = cur_toggle->x - rct.x;
            rct.height = cur_toggle->user->selection_bound_rect.height;
            gdk_cairo_rectangle(cr, &rct);
          }

          if(cur_toggle->y - prev_toggle->y >
             cur_toggle->user->selection_bound_rect.height)
          {
            /* intermediate */
            rct.x = inf_text_gtk_view_get_left_margin(priv->textview);
            rct.y = prev_toggle->y +
              prev_toggle->user->selection_bound_rect.height;
            rct.width = window_width - rct.x -
              inf_text_gtk_view_get_right_margin(priv->textview);
            rct.height = cur_toggle->y - prev_toggle->y -
              cur_toggle->user->selection_bound_rect.height;
            gdk_cairo_rectangle(cr, &rct);
          }
        }

        cairo_fill(cr);
        cairo_pattern_destroy(pattern);
      }

      prev_toggle = cur_toggle;

      /* advance to next position, toggle users on/off while doing so */
      do
      {
        if(cur_toggle->on_toggle == NULL)
        {
          /* Keep toggles in user list sorted by user ID, so that the same
           * users generate the same pattern */
          users = g_slist_insert_sorted(
            users,
            cur_toggle,
            inf_text_gtk_view_user_toggle_user_cmp
          );

          ++n_users;
        }
        else
        {
          g_assert(n_users > 0);
          users = g_slist_remove(users, cur_toggle->on_toggle);
          --n_users;
        }

        tog_iter = g_sequence_iter_next(tog_iter);
        if(g_sequence_iter_is_end(tog_iter))
          break;

        cur_toggle = (InfTextGtkViewUserToggle*)g_sequence_get(tog_iter);
      } while(cur_toggle->pos == prev_toggle->pos);
    }

    g_assert(n_users == 0);
    g_assert(users == NULL);
    g_sequence_free(toggles);
  }

  if(priv->show_remote_cursors)
  {
    gtk_widget_style_get (widget, "cursor-color", &cursor_color, NULL);
    if(cursor_color != NULL)
    {
      rc = cursor_color->red / 65535.0;
      bc = cursor_color->green / 65535.0;
      gc = cursor_color->blue / 65535.0;
      gdk_color_free(cursor_color);
    }
    else
    {
      rc = fg.red;
      bc = fg.green;
      gc = fg.blue;
    }

    gtk_rgb_to_hsv(rc, bc, gc, &hc, &sc, &vc);
    sc = MIN(MAX(sc, 0.3), 0.8);
    vc = MAX(vc, 0.7);

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

        if(gdk_rectangle_intersect(&clip_area, &rct, NULL))
        {
          hc = inf_text_user_get_hue(view_user->user);
          gtk_hsv_to_rgb(hc, sc, vc, &rc, &gc, &bc);

          cairo_set_source_rgb(cr, rc, gc, bc);
          gdk_cairo_rectangle(cr, &rct);
          cairo_fill(cr);
        }
      }
    }
  }

  return FALSE;
}

static void
inf_text_gtk_view_style_updated_cb(GtkWidget* widget,
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
    inf_text_gtk_view_user_compute_user_area(view_user);
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
    inf_text_gtk_view_user_compute_user_area(view_user);
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

  view_user = (InfTextGtkViewUser*)user_data;
  priv = INF_TEXT_GTK_VIEW_PRIVATE(view_user->view);

  view_user->cursor_visible = !view_user->cursor_visible;
  inf_text_gtk_view_user_invalidate_user_area(view_user);

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

    view_user->timeout = inf_io_add_timeout(
      priv->io,
      cursor_blink_time,
      inf_text_gtk_view_user_cursor_blink_timeout_func,
      view_user,
      NULL
    );
  }
  else
  {
    view_user->timeout = NULL;
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

  if(view_user->timeout)
  {
    inf_io_remove_timeout(priv->io, view_user->timeout);
    view_user->timeout = NULL;
  }

  if(!view_user->cursor_visible)
  {
    view_user->cursor_visible = TRUE;

    /* TODO: Only need to invalidate cursor rect, not whole user area */
    inf_text_gtk_view_user_invalidate_user_area(view_user);
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
    view_user->timeout = inf_io_add_timeout(
      priv->io,
      cursor_blink_time,
      inf_text_gtk_view_user_cursor_blink_timeout_func,
      view_user,
      NULL
    );
  }
}

static gboolean
inf_text_gtk_view_user_selection_changed_cb_idle_func(gpointer user_data)
{
  InfTextGtkViewUser* view_user;
  view_user = (InfTextGtkViewUser*)user_data;

  g_assert(view_user->revalidate_idle != 0);
  view_user->revalidate_idle = 0;

  /* Revalidate */
  inf_text_gtk_view_user_invalidate_user_area(view_user);

  return FALSE;
}

static void
inf_text_gtk_view_user_selection_changed_cb(InfTextUser* user,
                                            guint position,
                                            gint length,
                                            gboolean by_request,
                                            gpointer user_data)
{
  InfTextGtkViewUser* view_user;
  InfTextGtkViewPrivate* priv;

  view_user = (InfTextGtkViewUser*)user_data;
  priv = INF_TEXT_GTK_VIEW_PRIVATE(view_user->view);

  /* We don't need to invalidate areas if the change was not made by a user
   * request. So for example if someone's cursor moved because another user
   * has inserted text somewhere before it, then we don't need to redraw that
   * cursor since it either:
   * a) was shifted to the right, in which case the underlying text was also
   * shifted and is therefore invalidated anyway.
   * b) Both text and cursor have not been shifted, no redraw necessary.
   * Note that we need to recompute the user area though because it might
   * have moved. */
  if(by_request)
  {
    /* Invalidate current user area, e.g. to get rid of cursor at previous
     * location if it moved, or if the selection area was shrinked. */
    /* TODO: Not sure whether this also needs to go to the idle handler...
     * if so make sure it is executed even if the viewuser is deleted in the
     * meanwhile. */
    inf_text_gtk_view_user_invalidate_user_area(view_user);
  }

  inf_text_gtk_view_user_compute_user_area(view_user);

  if(by_request)
  {
    inf_text_gtk_view_user_reset_timeout(view_user);

    /* We can't invalidate here because
     * gtk_text_view_buffer_to_window_coords() does not give correct
     * coordinates at this point. We need to wait for the textview to
     * revalidate onscreen lines first (which it does in an idle handler,
     * note higher numbers indicate less priority). */
    if(view_user->revalidate_idle == 0)
    {
      view_user->revalidate_idle = g_idle_add_full(
        GTK_TEXT_VIEW_PRIORITY_VALIDATE + 1,
        inf_text_gtk_view_user_selection_changed_cb_idle_func,
        view_user,
        NULL
      );
    }
  }
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
  view_user->timeout = NULL;
  view_user->revalidate_idle = 0;
  inf_text_gtk_view_user_compute_user_area(view_user);
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

  inf_text_gtk_view_user_invalidate_user_area(view_user);
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

  if(view_user->timeout != NULL)
    inf_io_remove_timeout(priv->io, view_user->timeout);

  if(view_user->revalidate_idle != 0)
    g_source_remove(view_user->revalidate_idle);

  inf_text_gtk_view_user_invalidate_user_area(view_user);

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
      G_CALLBACK(inf_text_gtk_view_draw_before_cb),
      view
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->textview),
      G_CALLBACK(inf_text_gtk_view_draw_after_cb),
      view
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->textview),
      G_CALLBACK(inf_text_gtk_view_style_updated_cb),
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

    g_signal_connect(
      G_OBJECT(gtk_view),
      "draw",
      G_CALLBACK(inf_text_gtk_view_draw_before_cb),
      view
    );

    g_signal_connect_after(
      G_OBJECT(gtk_view),
      "draw",
      G_CALLBACK(inf_text_gtk_view_draw_after_cb),
      view
    );

    g_signal_connect_after(
      G_OBJECT(gtk_view),
      "style-updated",
      G_CALLBACK(inf_text_gtk_view_style_updated_cb),
      view
    );

    /* This is required for the remote cursors showing up at the correct
     * position initially. Maybe gtk_text_view_get_iter_location() seems to
     * return junk before. Note that also style-updated is not enough. */
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
inf_text_gtk_view_init(InfTextGtkView* view)
{
  InfTextGtkViewPrivate* priv;
  priv = INF_TEXT_GTK_VIEW_PRIVATE(view);

  priv->io = NULL;
  priv->textview = NULL;
  priv->user_table = NULL;
  priv->active_user = NULL;
  priv->users = NULL;

  priv->show_remote_cursors = TRUE;
  priv->show_remote_selections = TRUE;
  priv->show_remote_current_lines = TRUE;
}

static void
inf_text_gtk_view_dispose(GObject* object)
{
  InfTextGtkView* view;
  InfTextGtkViewPrivate* priv;

  view = INF_TEXT_GTK_VIEW(object);
  priv = INF_TEXT_GTK_VIEW_PRIVATE(view);

  inf_text_gtk_view_set_user_table(view, NULL);
  inf_text_gtk_view_set_view(view, NULL);

  g_assert(priv->active_user == NULL);
  g_assert(priv->users == NULL);

  if(priv->io != NULL)
  {
    g_object_unref(priv->io);
    priv->io = NULL;
  }

  G_OBJECT_CLASS(inf_text_gtk_view_parent_class)->dispose(object);
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
  case PROP_SHOW_REMOTE_CURSORS:
    inf_text_gtk_view_set_show_remote_cursors(
      view,
      g_value_get_boolean(value)
    );

    break;
  case PROP_SHOW_REMOTE_SELECTIONS:
    inf_text_gtk_view_set_show_remote_selections(
      view,
      g_value_get_boolean(value)
    );

    break;
  case PROP_SHOW_REMOTE_CURRENT_LINES:
    inf_text_gtk_view_set_show_remote_current_lines(
      view,
      g_value_get_boolean(value)
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
  case PROP_SHOW_REMOTE_CURSORS:
    g_value_set_boolean(value, priv->show_remote_cursors);
    break;
  case PROP_SHOW_REMOTE_SELECTIONS:
    g_value_set_boolean(value, priv->show_remote_selections);
    break;
  case PROP_SHOW_REMOTE_CURRENT_LINES:
    g_value_set_boolean(value, priv->show_remote_current_lines);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_text_gtk_view_class_init(InfTextGtkViewClass* view_class)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(view_class);

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

  g_object_class_install_property(
    object_class,
    PROP_SHOW_REMOTE_CURSORS,
    g_param_spec_boolean(
      "show-remote-cursors",
      "Show remote cursors",
      "Whether to show cursors of non-local users",
      TRUE,
      G_PARAM_READWRITE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_SHOW_REMOTE_SELECTIONS,
    g_param_spec_boolean(
      "show-remote-selections",
      "Show remote selections",
      "Whether to highlight text selected by non-local users",
      TRUE,
      G_PARAM_READWRITE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_SHOW_REMOTE_CURRENT_LINES,
    g_param_spec_boolean(
      "show-remote-current-lines",
      "Show remote current lines",
      "Whether to highlight the line in which the cursor of non-local users is",
      TRUE,
      G_PARAM_READWRITE
    )
  );
}

/**
 * inf_text_gtk_view_new: (constructor)
 * @io: A #InfIo.
 * @view: A #GtkTextView.
 * @user_table: The #InfUserTable for the text session displayed in @view.
 *
 * Creates a new #InfTextGtkView for @view. This draws remote user's cursors
 * and selections into the text view.
 *
 * Returns: (transfer full): A new #InfTextGtkView.
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
 * Returns: (transfer none): The #InfTextGtkView's #GtkTextView.
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
 * Returns: (transfer none): The #InfTextGtkView's #InfUserTable.
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
 * @user: (allow-none): A user from @view's user table, or %NULL.
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
 * Returns: (transfer none) (allow-none): The active user of @view.
 */
InfTextUser*
inf_text_gtk_view_get_active_user(InfTextGtkView* view)
{
  g_return_val_if_fail(INF_TEXT_GTK_IS_VIEW(view), NULL);
  return INF_TEXT_GTK_VIEW_PRIVATE(view)->active_user;
}

/**
 * inf_text_gtk_view_set_show_remote_cursors:
 * @view: A #InfTextGtkView.
 * @show: Whether to show cursors of non-local users.
 *
 * If @show is %TRUE then @view draws a cursor for each non-local user in
 * %INF_USER_ACTIVE status in that user's color into its underlying
 * #GtkTextView. If it is %FALSE then remote cursors are not drawn.
 */
void
inf_text_gtk_view_set_show_remote_cursors(InfTextGtkView* view,
                                          gboolean show)
{
  InfTextGtkViewPrivate* priv;
  
  g_return_if_fail(INF_TEXT_GTK_IS_VIEW(view));
  priv = INF_TEXT_GTK_VIEW_PRIVATE(view);

  if(priv->show_remote_cursors != show)
  {
    gtk_widget_queue_draw(GTK_WIDGET(priv->textview));

    priv->show_remote_cursors = show;
    g_object_notify(G_OBJECT(view), "show-remote-cursors");
  }
}

/**
 * inf_text_gtk_view_set_show_remote_selections:
 * @view: A #InfTextGtkView.
 * @show: Whether to show selections of non-local users.
 *
 * If @show is %TRUE then @view draws the selection ranges for each non-local
 * user in %INF_USER_ACTIVE status. The selection range is drawn shaded in
 * that user's color on top of the author color which indicates who wrote the
 * selected text. If more than one user has a given piece of text selected
 * then an alternating stripe pattern with each of the user's colors is drawn.
 * If @show is %FALSE then selection ranges of remote users are not drawn.
 */
void
inf_text_gtk_view_set_show_remote_selections(InfTextGtkView* view,
                                             gboolean show)
{
  InfTextGtkViewPrivate* priv;
  
  g_return_if_fail(INF_TEXT_GTK_IS_VIEW(view));
  priv = INF_TEXT_GTK_VIEW_PRIVATE(view);

  if(priv->show_remote_selections != show)
  {
    gtk_widget_queue_draw(GTK_WIDGET(priv->textview));

    priv->show_remote_selections = show;
    g_object_notify(G_OBJECT(view), "show-remote-selections");
  }
}

/**
 * inf_text_gtk_view_set_show_remote_current_lines:
 * @view: A #InfTextGtkView.
 * @show: Whether to highlight the current line of non-local users.
 *
 * If @show is %TRUE then all lines in which the cursor of a non-local user
 * in %INF_USER_ACTIVE status is is highlighted with that user's color, similar
 * to GtkSourceView's "highlight current line" functionality. If it is %FALSE
 * then the current line of non-local users is not highlighted.
 */
void
inf_text_gtk_view_set_show_remote_current_lines(InfTextGtkView* view,
                                                gboolean show)
{
  InfTextGtkViewPrivate* priv;

  g_return_if_fail(INF_TEXT_GTK_IS_VIEW(view));
  priv = INF_TEXT_GTK_VIEW_PRIVATE(view);

  if(priv->show_remote_current_lines != show)
  {
    gtk_widget_queue_draw(GTK_WIDGET(priv->textview));

    priv->show_remote_current_lines = show;
    g_object_notify(G_OBJECT(view), "show-remote-current-lines");
  }
}

/* vim:set et sw=2 ts=2: */
