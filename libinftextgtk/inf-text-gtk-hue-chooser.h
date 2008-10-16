/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INF_TEXT_GTK_HUE_CHOOSER_H__
#define __INF_TEXT_GTK_HUE_CHOOSER_H__

#include <gtk/gtkwidget.h>

G_BEGIN_DECLS

#define INF_TEXT_GTK_TYPE_HUE_CHOOSER                 (inf_text_gtk_hue_chooser_get_type())
#define INF_TEXT_GTK_HUE_CHOOSER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TEXT_GTK_TYPE_HUE_CHOOSER, InfTextGtkHueChooser))
#define INF_TEXT_GTK_HUE_CHOOSER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TEXT_GTK_TYPE_HUE_CHOOSER, InfTextGtkHueChooserClass))
#define INF_TEXT_GTK_IS_HUE_CHOOSER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TEXT_GTK_TYPE_HUE_CHOOSER))
#define INF_TEXT_GTK_IS_HUE_CHOOSER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TEXT_GTK_TYPE_HUE_CHOOSER))
#define INF_TEXT_GTK_HUE_CHOOSER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TEXT_GTK_TYPE_HUE_CHOOSER, InfTextGtkHueChooserClass))

typedef struct _InfTextGtkHueChooser InfTextGtkHueChooser;
typedef struct _InfTextGtkHueChooserClass InfTextGtkHueChooserClass;

struct _InfTextGtkHueChooserClass {
  GtkWidgetClass parent_class;

  void(*hue_change)(InfTextGtkHueChooser* chooser,
                    gdouble hue);

  void(*move)(InfTextGtkHueChooser* chooser,
              GtkDirectionType direction);
};

struct _InfTextGtkHueChooser {
  GtkWidget parent;
};

GType
inf_text_gtk_hue_chooser_get_type(void) G_GNUC_CONST;

GtkWidget*
inf_text_gtk_hue_chooser_new(void);

GtkWidget*
inf_text_gtk_hue_chooser_new_with_hue(gdouble hue);

void
inf_text_gtk_hue_chooser_set_hue(InfTextGtkHueChooser* chooser,
                                 gdouble hue);

gdouble
inf_text_gtk_hue_chooser_get_hue(InfTextGtkHueChooser* chooser);

G_END_DECLS

#endif /* __INF_TEXT_GTK_HUE_CHOOSER_H__ */

/* vim:set et sw=2 ts=2: */
