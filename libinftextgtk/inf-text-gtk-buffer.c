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

#include <libinftextgtk/inf-text-gtk-buffer.h>
#include <libinftext/inf-text-buffer.h>

#include <string.h> /* for strlen() */

struct _InfTextBufferIter {
  GtkTextIter begin;
  GtkTextIter end;
};

typedef struct _InfTextGtkBufferTagRemove InfTextGtkBufferTagRemove;
struct _InfTextGtkBufferTagRemove {
  GtkTextBuffer* buffer;
  GtkTextIter begin_iter;
  GtkTextIter end_iter;
  GtkTextTag* ignore_tag;
};

typedef struct _InfTextGtkBufferPrivate InfTextGtkBufferPrivate;
struct _InfTextGtkBufferPrivate {
  GtkTextBuffer* buffer;
  InfUserTable* user_table;
  GHashTable* user_tags;
  InfTextUser* active_user;
};

enum {
  PROP_0,

  PROP_BUFFER,
  PROP_USER_TABLE,
  PROP_ACTIVE_USER
};

#define INF_TEXT_GTK_BUFFER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TEXT_GTK_TYPE_BUFFER, InfTextGtkBufferPrivate))

static GObjectClass* parent_class;
static GQuark inf_text_gtk_buffer_tag_user_quark;

/* This function is stolen from gtkhsv.c from GTK+ */
/* TODO: Use gtk_hsv_to_rgb from GTK+ 2.14 instead */
/* Converts from HSV to RGB */
static void
hsv_to_rgb (gdouble *h,
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
      break;
    }
  }
}

static void
inf_text_gtk_update_tag_color(InfTextGtkBuffer* buffer,
                              GtkTextTag* tag,
                              InfTextUser* user)
{
  gdouble hue;
  gdouble saturation;
  gdouble value;
  GdkColor color;

  hue = inf_text_user_get_hue(user);
  /* TODO: Choose these to also fit a dark theme. Perhaps make a property
   * out of them if we can't find out here. */
  saturation = 0.35;
  value = 1.0;

  hsv_to_rgb(&hue, &saturation, &value);

  color.red = hue * 0xffff;
  color.green = saturation * 0xffff;
  color.blue = value * 0xffff;

  g_object_set(G_OBJECT(tag), "background-gdk", &color, NULL);
}

static void
inf_text_gtk_user_notify_hue_cb(GObject* object,
                                GParamSpec* pspec,
                                gpointer user_data)
{
  InfTextGtkBuffer* buffer;
  InfTextGtkBufferPrivate* priv;
  guint user_id;
  GtkTextTag* tag;

  buffer = INF_TEXT_GTK_BUFFER(user_data);
  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);
  user_id = inf_user_get_id(INF_USER(object));
  tag = g_hash_table_lookup(priv->user_tags, GUINT_TO_POINTER(user_id));
  g_assert(tag != NULL);

  inf_text_gtk_update_tag_color(buffer, tag, INF_TEXT_USER(object));
}

static GtkTextTag*
inf_text_gtk_buffer_get_user_tag(InfTextGtkBuffer* buffer,
                                 guint user_id)
{
  InfTextGtkBufferPrivate* priv;
  GtkTextTagTable* table;
  GtkTextTag* tag;
  gchar* tag_name;
  InfTextUser* user;

  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  if(user_id == 0)
    return NULL;

  tag = g_hash_table_lookup(priv->user_tags, GUINT_TO_POINTER(user_id));
  if(tag != NULL)
  {
    return tag;
  }
  else
  {
    tag_name = g_strdup_printf("inftextgtk-user-%u", user_id);
    tag = gtk_text_tag_new(tag_name);
    g_free(tag_name);

    table = gtk_text_buffer_get_tag_table(priv->buffer);
    gtk_text_tag_table_add(table, tag);
    g_hash_table_insert(priv->user_tags, GUINT_TO_POINTER(user_id), tag);

    /* Set lowest priority for author tags, so GtkSourceView's bracket
     * matching highlight tags and highlight of FIXME and such in comments is
     * shown instead of the user color. */
    gtk_text_tag_set_priority(tag, 0);

    g_object_set_qdata(
      G_OBJECT(tag),
      inf_text_gtk_buffer_tag_user_quark,
      GUINT_TO_POINTER(user_id)
    );

    user = INF_TEXT_USER(
      inf_user_table_lookup_user_by_id(priv->user_table, user_id)
    );
    g_assert(user != NULL);

    /* TODO: Disconnect from that some time later */
    g_signal_connect(
      G_OBJECT(user),
      "notify::hue",
      G_CALLBACK(inf_text_gtk_user_notify_hue_cb),
      buffer
    );

    inf_text_gtk_update_tag_color(buffer, tag, user);

    return tag;
  }
}

static guint
inf_text_gtk_buffer_author_from_tag(GtkTextTag* tag)
{
  gpointer author_ptr;
  guint author_id;

  author_ptr = g_object_get_qdata(
    G_OBJECT(tag),
    inf_text_gtk_buffer_tag_user_quark
  );

  author_id = GPOINTER_TO_UINT(author_ptr);
  return author_id;
}

static guint
inf_text_gtk_buffer_iter_list_contains_author_tag(GSList* tag_list)
{
  GSList* item;
  guint author;

  for(item = tag_list; item != NULL; item = g_slist_next(item))
  {
    author = inf_text_gtk_buffer_author_from_tag(GTK_TEXT_TAG(item->data));
    if(author != 0) return author;
  }

  return 0;
}

static guint
inf_text_gtk_buffer_iter_get_author(GtkTextIter* location)
{
  GSList* tag_list;
  guint author;

  tag_list = gtk_text_iter_get_tags(location);
  author = inf_text_gtk_buffer_iter_list_contains_author_tag(tag_list);
  g_slist_free(tag_list);

  /* Author tag must always be set on text */
  return author;
}

static gboolean
inf_text_gtk_buffer_iter_is_author_toggle(GtkTextIter* iter)
{
  GSList* tag_list;
  guint author_id;

  tag_list = gtk_text_iter_get_toggled_tags(iter, TRUE);
  author_id = inf_text_gtk_buffer_iter_list_contains_author_tag(tag_list);
  g_slist_free(tag_list);

  /* We need to check both the tags that are toggled on and the tags that
   * are toggled off at this point, because text that is not written by
   * anyone specific (author NULL) does not count as author tag. */
  if(author_id == 0)
  {
    tag_list = gtk_text_iter_get_toggled_tags(iter, FALSE);
    author_id = inf_text_gtk_buffer_iter_list_contains_author_tag(tag_list);
    g_slist_free(tag_list);
  }

  if(author_id == 0) return FALSE;
  return TRUE;
}

static void
inf_text_gtk_buffer_iter_next_author_toggle(GtkTextIter* iter)
{
  do
  {
    /* We get endless loops without these. I am not sure why. */
    if(gtk_text_iter_is_end(iter)) return;

    if(gtk_text_iter_forward_to_tag_toggle(iter, NULL) == FALSE)
      return;
  } while(inf_text_gtk_buffer_iter_is_author_toggle(iter) == FALSE);
}

static void
inf_text_gtk_buffer_iter_prev_author_toggle(GtkTextIter* iter)
{
  do
  {
    /* We get endless loops without this. I am not sure why. */
    if(gtk_text_iter_is_start(iter)) return;

    if(gtk_text_iter_backward_to_tag_toggle(iter, NULL) == FALSE)
      return;
  } while(inf_text_gtk_buffer_iter_is_author_toggle(iter) == FALSE);
}

static void
inf_text_gtk_buffer_ensure_author_tags_priority_foreach_func(GtkTextTag* tag,
                                                             gpointer data)
{
  guint author;
  author = inf_text_gtk_buffer_author_from_tag(tag);

  if(author != 0)
    gtk_text_tag_set_priority(tag, 0);
}

static void
inf_text_gtk_buffer_apply_tag_cb(GtkTextBuffer* gtk_buffer,
                                 GtkTextTag* tag,
                                 GtkTextIter* start,
                                 GtkTextIter* end,
                                 gpointer user_data)
{
  /* Don't allow auhtor tags to be applied by default. GTK+ seems to do this
   * when copy+pasting text from the text buffer itself, but we want to make
   * sure that a given segment of text has always a unique author set. */
  if(inf_text_gtk_buffer_author_from_tag(tag) != 0)
    g_signal_stop_emission_by_name(G_OBJECT(gtk_buffer), "apply-tag");
}

static void
inf_text_gtk_buffer_insert_text_cb(GtkTextBuffer* gtk_buffer,
                                   GtkTextIter* location,
                                   gchar* text,
                                   gint len,
                                   gpointer user_data)
{
  InfTextGtkBuffer* buffer;
  InfTextGtkBufferPrivate* priv;
  guint location_offset;
  guint text_len;

  buffer = INF_TEXT_GTK_BUFFER(user_data);
  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  /* Text written by the active user */
  g_assert(priv->active_user != NULL);

  /* The default handler of the "insert-text" signal
   * (inf_text_gtk_buffer_buffer_insert_text) will re-emit the signal with
   * this handler being blocked. This is a bit of a hack since signal handlers
   * that ran already could rely on the default handler to run.
   *
   * However, it is required so that signal handlers of the "insert-text"
   * signal of InfTextGtkBuffer that connected with the AFTER flag find the
   * text already inserted into the buffer. */
  g_signal_stop_emission_by_name(G_OBJECT(gtk_buffer), "insert-text");

  location_offset = gtk_text_iter_get_offset(location);
  text_len = g_utf8_strlen(text, len);

  inf_text_buffer_insert_text(
    INF_TEXT_BUFFER(buffer),
    location_offset,
    text,
    len,
    text_len,
    INF_USER(priv->active_user)
  );

  /* Revalidate iterator */
  gtk_text_buffer_get_iter_at_offset(priv->buffer, location,
                                     location_offset + text_len);
}

static void
inf_text_gtk_buffer_delete_range_cb(GtkTextBuffer* gtk_buffer,
                                    GtkTextIter* begin,
                                    GtkTextIter* end,
                                    gpointer user_data)
{
  InfTextGtkBuffer* buffer;
  InfTextGtkBufferPrivate* priv;
  guint begin_offset;

  buffer = INF_TEXT_GTK_BUFFER(user_data);
  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  /* Text written by the active user */
  g_assert(priv->active_user != NULL);

  /* The default handler of the "erase-text" signal
   * (inf_text_gtk_buffer_buffer_erase_text) will re-emit the signal with
   * this handler being blocked. This is a bit of a hack since signal handlers
   * that ran already could rely on the default handler to run.
   *
   * However, it is required so that signal handlers of the "erase-text"
   * signal of InfTextGtkBuffer that connected with the AFTER flag find the
   * text already removed from buffer. */
  g_signal_stop_emission_by_name(G_OBJECT(gtk_buffer), "delete-range");

  begin_offset = gtk_text_iter_get_offset(begin);

  inf_text_buffer_erase_text(
    INF_TEXT_BUFFER(buffer),
    begin_offset,
    gtk_text_iter_get_offset(end) - begin_offset,
    INF_USER(priv->active_user)
  );

  /* Revalidate iterators */
  gtk_text_buffer_get_iter_at_offset(priv->buffer, begin, begin_offset);
  *end = *begin;
}

/* Required by inf_text_gtk_buffer_mark_set_cb() */
static void
inf_text_gtk_buffer_active_user_selection_changed_cb(InfTextUser* user,
                                                     guint position,
                                                     gint length,
                                                     gpointer user_data);

static void
inf_text_gtk_buffer_mark_set_cb(GtkTextBuffer* gtk_buffer,
                                GtkTextIter* location,
                                GtkTextMark* mark,
                                gpointer user_data)
{
  InfTextGtkBuffer* buffer;
  InfTextGtkBufferPrivate* priv;
  GtkTextMark* insert_mark;
  GtkTextMark* sel_mark;
  GtkTextIter insert_iter;
  GtkTextIter sel_iter;

  guint offset;
  int sel;

  buffer = INF_TEXT_GTK_BUFFER(user_data);
  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  insert_mark = gtk_text_buffer_get_insert(gtk_buffer);
  sel_mark = gtk_text_buffer_get_selection_bound(gtk_buffer);

  if( (mark == insert_mark || mark == sel_mark) && priv->active_user != NULL)
  {
    gtk_text_buffer_get_iter_at_mark(gtk_buffer, &insert_iter, insert_mark);
    gtk_text_buffer_get_iter_at_mark(gtk_buffer, &sel_iter, sel_mark);

    offset = gtk_text_iter_get_offset(&insert_iter);
    sel = gtk_text_iter_get_offset(&sel_iter) - offset;

    if(inf_text_user_get_caret_position(priv->active_user) != offset ||
       inf_text_user_get_selection_length(priv->active_user) != sel)
    {
      g_signal_handlers_block_by_func(
        G_OBJECT(priv->active_user),
        G_CALLBACK(inf_text_gtk_buffer_active_user_selection_changed_cb),
        buffer
      );

      inf_text_user_set_selection(priv->active_user, offset, sel);

      g_signal_handlers_unblock_by_func(
        G_OBJECT(priv->active_user),
        G_CALLBACK(inf_text_gtk_buffer_active_user_selection_changed_cb),
        buffer
      );
    }
  }
}

static void
inf_text_gtk_buffer_active_user_selection_changed_cb(InfTextUser* user,
                                                     guint position,
                                                     gint selection_length,
                                                     gpointer user_data)
{
  InfTextGtkBuffer* buffer;
  InfTextGtkBufferPrivate* priv;
  GtkTextIter insert;
  GtkTextIter selection_bound;

  buffer = INF_TEXT_GTK_BUFFER(user_data);
  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  g_signal_handlers_block_by_func(
    G_OBJECT(priv->buffer),
    G_CALLBACK(inf_text_gtk_buffer_mark_set_cb),
    buffer
  );

  gtk_text_buffer_get_iter_at_offset(priv->buffer, &insert, position);

  gtk_text_buffer_get_iter_at_offset(
    priv->buffer,
    &selection_bound,
    position + selection_length
  );

  gtk_text_buffer_select_range(priv->buffer, &insert, &selection_bound);

  g_signal_handlers_unblock_by_func(
    G_OBJECT(priv->buffer),
    G_CALLBACK(inf_text_gtk_buffer_mark_set_cb),
    buffer
  );
}

static void
inf_text_gtk_buffer_set_buffer(InfTextGtkBuffer* buffer,
                               GtkTextBuffer* gtk_buffer)
{
  InfTextGtkBufferPrivate* priv;
  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  if(priv->buffer != NULL)
  {
    g_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->buffer),
      G_CALLBACK(inf_text_gtk_buffer_apply_tag_cb),
      buffer
    );

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->buffer),
      G_CALLBACK(inf_text_gtk_buffer_insert_text_cb),
      buffer
    );

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->buffer),
      G_CALLBACK(inf_text_gtk_buffer_delete_range_cb),
      buffer
    );

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->buffer),
      G_CALLBACK(inf_text_gtk_buffer_mark_set_cb),
      buffer
    );

    g_object_unref(G_OBJECT(priv->buffer));
  }
  
  priv->buffer = gtk_buffer;

  if(gtk_buffer != NULL)
  {
    g_object_ref(G_OBJECT(gtk_buffer));
    
    g_signal_connect(
      G_OBJECT(gtk_buffer),
      "apply-tag",
      G_CALLBACK(inf_text_gtk_buffer_apply_tag_cb),
      buffer
    );

    g_signal_connect(
      G_OBJECT(gtk_buffer),
      "insert-text",
      G_CALLBACK(inf_text_gtk_buffer_insert_text_cb),
      buffer
    );

    g_signal_connect(
      G_OBJECT(gtk_buffer),
      "delete-range",
      G_CALLBACK(inf_text_gtk_buffer_delete_range_cb),
      buffer
    );

    g_signal_connect_after(
      G_OBJECT(gtk_buffer),
      "mark-set",
      G_CALLBACK(inf_text_gtk_buffer_mark_set_cb),
      buffer
    );
  }

  g_object_notify(G_OBJECT(buffer), "buffer");
}

static void
inf_text_gtk_buffer_init(GTypeInstance* instance,
                         gpointer g_class)
{
  InfTextGtkBuffer* buffer;
  InfTextGtkBufferPrivate* priv;

  buffer = INF_TEXT_GTK_BUFFER(instance);
  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  priv->buffer = NULL;
  priv->user_table = NULL;

  priv->user_tags = g_hash_table_new_full(
    NULL,
    NULL,
    NULL,
    (GDestroyNotify)g_object_unref
  );

  priv->active_user = NULL;
}

static void
inf_text_gtk_buffer_dispose(GObject* object)
{
  InfTextGtkBuffer* buffer;
  InfTextGtkBufferPrivate* priv;

  buffer = INF_TEXT_GTK_BUFFER(object);
  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  g_hash_table_remove_all(priv->user_tags);

  inf_text_gtk_buffer_set_buffer(buffer, NULL);
  inf_text_gtk_buffer_set_active_user(buffer, NULL);
  g_object_unref(priv->user_table);

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_text_gtk_buffer_finalize(GObject* object)
{
  InfTextGtkBuffer* buffer;
  InfTextGtkBufferPrivate* priv;

  buffer = INF_TEXT_GTK_BUFFER(object);
  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  g_hash_table_unref(priv->user_tags);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_text_gtk_buffer_set_property(GObject* object,
                                 guint prop_id,
                                 const GValue* value,
                                 GParamSpec* pspec)
{
  InfTextGtkBuffer* buffer;
  InfTextGtkBufferPrivate* priv;

  buffer = INF_TEXT_GTK_BUFFER(object);
  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  switch(prop_id)
  {
  case PROP_BUFFER:
    g_assert(priv->buffer == NULL); /* construct only */
    inf_text_gtk_buffer_set_buffer(
      buffer,
      GTK_TEXT_BUFFER(g_value_get_object(value))
    );

    break;
  case PROP_USER_TABLE:
    g_assert(priv->user_table == NULL); /* construct/only */
    priv->user_table = INF_USER_TABLE(g_value_dup_object(value));
    break;
  case PROP_ACTIVE_USER:
    inf_text_gtk_buffer_set_active_user(
      buffer,
      INF_TEXT_USER(g_value_get_object(value))
    );

    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(value, prop_id, pspec);
    break;
  }
}

static void
inf_text_gtk_buffer_get_property(GObject* object,
                                 guint prop_id,
                                 GValue* value,
                                 GParamSpec* pspec)
{
  InfTextGtkBuffer* buffer;
  InfTextGtkBufferPrivate* priv;

  buffer = INF_TEXT_GTK_BUFFER(object);
  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  switch(prop_id)
  {
  case PROP_BUFFER:
    g_value_set_object(value, G_OBJECT(priv->buffer));
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

static const gchar*
inf_text_gtk_buffer_buffer_get_encoding(InfTextBuffer* buffer)
{
  return "UTF-8";
}

static guint
inf_text_gtk_buffer_get_length(InfTextBuffer* buffer)
{
  InfTextGtkBufferPrivate* priv;
  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);
  return gtk_text_buffer_get_char_count(priv->buffer);
}

static InfTextChunk*
inf_text_gtk_buffer_buffer_get_slice(InfTextBuffer* buffer,
                                     guint pos,
                                     guint len)
{
  InfTextGtkBufferPrivate* priv;
  GtkTextIter begin;
  GtkTextIter iter;
  InfTextChunk* result;
  guint remaining;

  guint size;
  guint author_id;
  gchar* text;

  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);
  gtk_text_buffer_get_iter_at_offset(priv->buffer, &iter, pos);
  result = inf_text_chunk_new("UTF-8");
  remaining = len;

  while(remaining > 0)
  {
    /* This indicates invalid length */
    g_assert(gtk_text_iter_is_end(&iter) == FALSE);

    begin = iter;
    inf_text_gtk_buffer_iter_next_author_toggle(&iter);

    size = gtk_text_iter_get_offset(&iter) - gtk_text_iter_get_offset(&begin);

    /* Not the whole segment if region to slice ends before segment end */
    if(size > remaining)
    {
      size = remaining;
      iter = begin;
      gtk_text_iter_forward_chars(&iter, size);
    }

    author_id = inf_text_gtk_buffer_iter_get_author(&begin);
    text = gtk_text_buffer_get_slice(priv->buffer, &begin, &iter, TRUE);

    /* TODO: Faster inf_text_chunk_append that optionally eats text */
    inf_text_chunk_insert_text(
      result,
      len - remaining,
      text,
      strlen(text), /* I hate strlen. GTK+ should tell us how many bytes. */
      size,
      author_id
    );

    remaining -= size;
    g_free(text);
  }

  return result;
}

static InfTextBufferIter*
inf_text_gtk_buffer_buffer_create_iter(InfTextBuffer* buffer)
{
  InfTextGtkBufferPrivate* priv;
  InfTextBufferIter* iter;

  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  if(gtk_text_buffer_get_char_count(priv->buffer) == 0)
  {
    return NULL;
  }
  else
  {
    iter = g_slice_new(InfTextBufferIter);
    gtk_text_buffer_get_start_iter(priv->buffer, &iter->begin);

    iter->end = iter->begin;
    inf_text_gtk_buffer_iter_next_author_toggle(&iter->end);

    return iter;
  }
}

static void
inf_text_gtk_buffer_buffer_destroy_iter(InfTextBuffer* buffer,
                                        InfTextBufferIter* iter)
{
  g_slice_free(InfTextBufferIter, iter);
}

static gboolean
inf_text_gtk_buffer_buffer_iter_next(InfTextBuffer* buffer,
                                     InfTextBufferIter* iter)
{
  if(gtk_text_iter_is_end(&iter->end))
    return FALSE;

  iter->begin = iter->end;
  inf_text_gtk_buffer_iter_next_author_toggle(&iter->end);
  return TRUE;
}

static gboolean
inf_text_gtk_buffer_buffer_iter_prev(InfTextBuffer* buffer,
                                     InfTextBufferIter* iter)
{
  if(gtk_text_iter_is_start(&iter->begin))
    return FALSE;

  iter->end = iter->begin;
  inf_text_gtk_buffer_iter_prev_author_toggle(&iter->begin);
  return TRUE;
}

static gpointer
inf_text_gtk_buffer_buffer_iter_get_text(InfTextBuffer* buffer,
                                         InfTextBufferIter* iter)
{
  InfTextGtkBufferPrivate* priv;
  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  return gtk_text_buffer_get_slice(
    priv->buffer,
    &iter->begin,
    &iter->end,
    TRUE
  );
}

static guint
inf_text_gtk_buffer_buffer_iter_get_length(InfTextBuffer* buffer,
                                           InfTextBufferIter* iter)
{
  return gtk_text_iter_get_offset(&iter->begin) -
    gtk_text_iter_get_offset(&iter->end);
}

static gsize
inf_text_gtk_buffer_buffer_iter_get_bytes(InfTextBuffer* buffer,
                                          InfTextBufferIter* iter)
{
  GtkTextIter walk;
  gsize bytes;
  guint remaining;
  guint end;

  guint line_chars;
  guint line_bytes;
  gboolean result;

  walk = iter->begin;
  bytes = 0;
  remaining = gtk_text_iter_get_offset(&iter->end) -
    gtk_text_iter_get_offset(&walk);
  end = gtk_text_iter_get_offset(&iter->end);

  while(remaining > 0)
  {
    line_chars = gtk_text_iter_get_chars_in_line(&walk) -
      gtk_text_iter_get_line_offset(&walk);

    if(line_chars + gtk_text_iter_get_offset(&walk) <= end)
    {
      /* Need whole line */
      line_bytes = gtk_text_iter_get_bytes_in_line(&walk) -
        gtk_text_iter_get_line_index(&walk);

      remaining -= line_chars;
      bytes += line_bytes;

      result = gtk_text_iter_forward_line(&walk);
      /* We cannot be in last line, because the end iterator would have to
       * be past the last line then. */
      g_assert(remaining == 0 || result == TRUE);
    }
    else
    {
      /* End iterator is in this line */
      line_bytes = gtk_text_iter_get_line_index(&iter->end) -
        gtk_text_iter_get_line_index(&walk);

      remaining = 0;
      bytes += line_bytes;
    }
  }

  return bytes;
}

static guint
inf_text_gtk_buffer_buffer_iter_get_author(InfTextBuffer* buffer,
                                           InfTextBufferIter* iter)
{
  /* TODO: Cache? */
  return inf_text_gtk_buffer_iter_get_author(&iter->begin);
}

static void
inf_text_gtk_buffer_buffer_insert_text_tag_table_foreach_func(GtkTextTag* tag,
                                                              gpointer data)
{
  InfTextGtkBufferTagRemove* tag_remove;
  tag_remove = (InfTextGtkBufferTagRemove*)data;

  if(tag != tag_remove->ignore_tag &&
     inf_text_gtk_buffer_author_from_tag(tag) != G_MAXUINT)
  {
    gtk_text_buffer_remove_tag(
      tag_remove->buffer,
      tag,
      &tag_remove->begin_iter,
      &tag_remove->end_iter
    );
  }
}

static void
inf_text_gtk_buffer_buffer_insert_text(InfTextBuffer* buffer,
                                       guint pos,
                                       InfTextChunk* chunk,
                                       InfUser* user)
{
  InfTextGtkBufferPrivate* priv;
  InfTextChunkIter chunk_iter;
  InfTextGtkBufferTagRemove tag_remove;

  GtkTextMark* mark;
  GtkTextIter insert_iter;
  gboolean insert_at_cursor;
  gboolean insert_at_selection_bound;

  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);
  tag_remove.buffer = priv->buffer;

  /* Allow author tag changes within this function: */
  g_signal_handlers_block_by_func(
    G_OBJECT(priv->buffer),
    G_CALLBACK(inf_text_gtk_buffer_apply_tag_cb),
    buffer
  );

  g_signal_handlers_block_by_func(
    G_OBJECT(priv->buffer),
    G_CALLBACK(inf_text_gtk_buffer_insert_text_cb),
    buffer
  );

  g_signal_handlers_block_by_func(
    G_OBJECT(priv->buffer),
    G_CALLBACK(inf_text_gtk_buffer_mark_set_cb),
    buffer
  );

  if(inf_text_chunk_iter_init(chunk, &chunk_iter))
  {
    gtk_text_buffer_get_iter_at_offset(
      priv->buffer,
      &tag_remove.end_iter,
      pos
    );

    do
    {
      tag_remove.ignore_tag = inf_text_gtk_buffer_get_user_tag(
        INF_TEXT_GTK_BUFFER(buffer),
        inf_text_chunk_iter_get_author(&chunk_iter)
      );

      gtk_text_buffer_insert_with_tags(
        tag_remove.buffer,
        &tag_remove.end_iter,
        inf_text_chunk_iter_get_text(&chunk_iter),
        inf_text_chunk_iter_get_bytes(&chunk_iter),
        tag_remove.ignore_tag,
        NULL
      );

      /* Remove other user tags. If we inserted the new text within another
       * user's text, GtkTextBuffer automatically applies that tag to the
       * new text. */

      /* TODO: We could probably look for the tag that we have to remove
       * before inserting text, to optimize this a bit. */
      tag_remove.begin_iter = tag_remove.end_iter;
      gtk_text_iter_backward_chars(
        &tag_remove.begin_iter,
        inf_text_chunk_iter_get_length(&chunk_iter)
      );

      gtk_text_tag_table_foreach(
        gtk_text_buffer_get_tag_table(tag_remove.buffer),
        inf_text_gtk_buffer_buffer_insert_text_tag_table_foreach_func,
        &tag_remove
      );
    } while(inf_text_chunk_iter_next(&chunk_iter));

    /* Fix left gravity of own cursor on remote insert */
    if(user != INF_USER(priv->active_user) || user == NULL)
    {
      mark = gtk_text_buffer_get_insert(priv->buffer);
      gtk_text_buffer_get_iter_at_mark(priv->buffer, &insert_iter, mark);

      if(gtk_text_iter_equal(&insert_iter, &tag_remove.end_iter))
        insert_at_cursor = TRUE;
      else
        insert_at_cursor = FALSE;

      mark = gtk_text_buffer_get_selection_bound(priv->buffer);
      gtk_text_buffer_get_iter_at_mark(priv->buffer, &insert_iter, mark);

      if(gtk_text_iter_equal(&insert_iter, &tag_remove.end_iter))
        insert_at_selection_bound = TRUE;
      else
        insert_at_selection_bound = FALSE;

      if(insert_at_cursor || insert_at_selection_bound)
      {
        gtk_text_iter_backward_chars(
          &tag_remove.end_iter,
          inf_text_chunk_get_length(chunk)
        );

        if(insert_at_cursor)
        {
          gtk_text_buffer_move_mark(
            priv->buffer,
            gtk_text_buffer_get_insert(priv->buffer),
            &tag_remove.end_iter
          );
        }

        if(insert_at_selection_bound)
        {
          gtk_text_buffer_move_mark(
            priv->buffer,
            gtk_text_buffer_get_selection_bound(priv->buffer),
            &tag_remove.end_iter
          );
        }
      }
    }
  }

  g_signal_handlers_unblock_by_func(
    G_OBJECT(priv->buffer),
    G_CALLBACK(inf_text_gtk_buffer_apply_tag_cb),
    buffer
  );

  g_signal_handlers_unblock_by_func(
    G_OBJECT(priv->buffer),
    G_CALLBACK(inf_text_gtk_buffer_insert_text_cb),
    buffer
  );

  g_signal_handlers_unblock_by_func(
    G_OBJECT(priv->buffer),
    G_CALLBACK(inf_text_gtk_buffer_mark_set_cb),
    buffer
  );
}

static void
inf_text_gtk_buffer_buffer_erase_text(InfTextBuffer* buffer,
                                      guint pos,
                                      guint len,
                                      InfUser* user)
{
  InfTextGtkBufferPrivate* priv;

  GtkTextIter begin;
  GtkTextIter end;

  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  gtk_text_buffer_get_iter_at_offset(priv->buffer, &begin, pos);

  /* TODO: Is it faster to call gtk_text_iter_forward_chars on begin? */
  gtk_text_buffer_get_iter_at_offset(priv->buffer, &end, pos + len);

  g_signal_handlers_block_by_func(
    G_OBJECT(priv->buffer),
    G_CALLBACK(inf_text_gtk_buffer_delete_range_cb),
    buffer
  );

  gtk_text_buffer_delete(priv->buffer, &begin, &end);

  g_signal_handlers_unblock_by_func(
    G_OBJECT(priv->buffer),
    G_CALLBACK(inf_text_gtk_buffer_delete_range_cb),
    buffer
  );
}

static void
inf_text_gtk_buffer_class_init(gpointer g_class,
                               gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfTextGtkBufferPrivate));

  object_class->dispose = inf_text_gtk_buffer_dispose;
  object_class->finalize = inf_text_gtk_buffer_finalize;
  object_class->set_property = inf_text_gtk_buffer_set_property;
  object_class->get_property = inf_text_gtk_buffer_get_property;

  inf_text_gtk_buffer_tag_user_quark = g_quark_from_static_string(
    "inf-text-gtk-buffer-tag-user"
  );

  g_object_class_install_property(
    object_class,
    PROP_BUFFER,
    g_param_spec_object(
      "buffer",
      "Buffer",
      "The underlaying GtkTextBuffer",
      GTK_TYPE_TEXT_BUFFER,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_USER_TABLE,
    g_param_spec_object(
      "user-table",
      "User table",
      "A user table of the participating users",
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
      "The user currently inserting text locally",
      INF_TEXT_TYPE_USER,
      G_PARAM_READWRITE
    )
  );
}

static void
inf_text_gtk_buffer_buffer_init(gpointer g_iface,
                                gpointer iface_data)
{
  InfBufferIface* iface;
  iface = (InfBufferIface*)g_iface;
}

static void
inf_text_gtk_buffer_text_buffer_init(gpointer g_iface,
                                     gpointer iface_data)
{
  InfTextBufferIface* iface;
  iface = (InfTextBufferIface*)g_iface;

  iface->get_encoding = inf_text_gtk_buffer_buffer_get_encoding;
  iface->get_length = inf_text_gtk_buffer_get_length;
  iface->get_slice = inf_text_gtk_buffer_buffer_get_slice;
  iface->create_iter = inf_text_gtk_buffer_buffer_create_iter;
  iface->destroy_iter = inf_text_gtk_buffer_buffer_destroy_iter;
  iface->iter_next = inf_text_gtk_buffer_buffer_iter_next;
  iface->iter_prev = inf_text_gtk_buffer_buffer_iter_prev;
  iface->iter_get_text = inf_text_gtk_buffer_buffer_iter_get_text;
  iface->iter_get_length = inf_text_gtk_buffer_buffer_iter_get_length;
  iface->iter_get_bytes = inf_text_gtk_buffer_buffer_iter_get_bytes;
  iface->iter_get_author = inf_text_gtk_buffer_buffer_iter_get_author;
  iface->insert_text = inf_text_gtk_buffer_buffer_insert_text;
  iface->erase_text = inf_text_gtk_buffer_buffer_erase_text;
}

GType
inf_text_gtk_buffer_get_type(void)
{
  static GType buffer_type = 0;

  if(!buffer_type)
  {
    static const GTypeInfo buffer_type_info = {
      sizeof(InfTextGtkBufferClass),  /* class_size */
      NULL,                           /* base_init */
      NULL,                           /* base_finalize */
      inf_text_gtk_buffer_class_init, /* class_init */
      NULL,                           /* class_finalize */
      NULL,                           /* class_data */
      sizeof(InfTextGtkBuffer),       /* instance_size */
      0,                              /* n_preallocs */
      inf_text_gtk_buffer_init,       /* instance_init */
      NULL                            /* value_table */
    };

    static const GInterfaceInfo buffer_info = {
      inf_text_gtk_buffer_buffer_init,
      NULL,
      NULL
    };

    static const GInterfaceInfo text_buffer_info = {
      inf_text_gtk_buffer_text_buffer_init,
      NULL,
      NULL
    };

    buffer_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfTextGtkBuffer",
      &buffer_type_info,
      0
    );

    g_type_add_interface_static(
      buffer_type,
      INF_TYPE_BUFFER,
      &buffer_info
    );

    g_type_add_interface_static(
      buffer_type,
      INF_TEXT_TYPE_BUFFER,
      &text_buffer_info
    );
  }

  return buffer_type;
}

/**
 * inf_text_gtk_buffer_new:
 * @buffer: The underlaying #GtkTextBuffer.
 * @user_table: The #InfUserTable containing the participating users.
 *
 * Creates a new #InfTextGtkBuffer wrapping @buffer. It implements the
 * #InfTextBuffer interface by using @buffer to store the text. User colors
 * are read from the users from @user_table.
 *
 * Return Value: A #InfTextGtkBuffer.
 **/
InfTextGtkBuffer*
inf_text_gtk_buffer_new(GtkTextBuffer* buffer,
                        InfUserTable* user_table)
{
  GObject* object;

  g_return_val_if_fail(GTK_IS_TEXT_BUFFER(buffer), NULL);
  g_return_val_if_fail(INF_IS_USER_TABLE(user_table), NULL);

  object = g_object_new(
    INF_TEXT_GTK_TYPE_BUFFER,
    "buffer", buffer,
    "user-table", user_table,
    NULL
  );

  return INF_TEXT_GTK_BUFFER(object);
}

/**
 * inf_text_gtk_buffer_get_text_buffer:
 * @buffer: A #InfTextGtkBuffer.
 *
 * Returns the underlaying #GtkTextBuffer.
 *
 * Return Value: A #GtkTextBuffer.
 **/
GtkTextBuffer*
inf_text_gtk_buffer_get_text_buffer(InfTextGtkBuffer* buffer)
{
  g_return_val_if_fail(INF_TEXT_GTK_IS_BUFFER(buffer), NULL);
  return INF_TEXT_GTK_BUFFER_PRIVATE(buffer)->buffer;
}

/**
 * inf_text_gtk_buffer_set_active_user:
 * @buffer: A #InfTextGtkBuffer.
 * @user: A #InfTextUser, or %NULL.
 *
 * Sets the active user for @buffer. The active user is the user by which
 * edits not issued through the #InfTextBuffer interface are performed (for
 * example, edits by the user when the underlaying buffer is displayed in
 * a #GtkTextView).
 *
 * Note that such modifications should not be performed when no active user is
 * set. Note also the active user must be available and have the
 * %INF_USER_LOCAL flag set.
 **/
void
inf_text_gtk_buffer_set_active_user(InfTextGtkBuffer* buffer,
                                    InfTextUser* user)
{
  InfTextGtkBufferPrivate* priv;

  g_return_if_fail(INF_TEXT_GTK_IS_BUFFER(buffer));
  g_return_if_fail(user == NULL || INF_TEXT_IS_USER(user));
  
  g_return_if_fail(
    user == NULL ||
    (inf_user_get_flags(INF_USER(user)) & INF_USER_LOCAL) != 0
  );

  g_return_if_fail(
    user == NULL ||
    inf_user_get_status(INF_USER(user)) != INF_USER_UNAVAILABLE
  );

  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  if(priv->active_user != NULL)
  {
    g_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->active_user),
      G_CALLBACK(inf_text_gtk_buffer_active_user_selection_changed_cb),
      buffer
    );

    g_object_unref(G_OBJECT(priv->active_user));
  }

  priv->active_user = user;

  if(user != NULL)
  {
    /* TODO: Set cursor and selection of new user */

    g_object_ref(G_OBJECT(user));

    g_signal_connect(
      G_OBJECT(user),
      "selection-changed",
      G_CALLBACK(inf_text_gtk_buffer_active_user_selection_changed_cb),
      buffer
    );
  }

  g_object_notify(G_OBJECT(buffer), "active-user");
}

/**
 * inf_text_gtk_buffer_get_active_user:
 * @buffer: A #InfTextGtkBuffer.
 *
 * Returns the current active user for @buffer.
 *
 * Return Value: A #InfTextUser.
 **/
InfTextUser*
inf_text_gtk_buffer_get_active_user(InfTextGtkBuffer* buffer)
{
  g_return_val_if_fail(INF_TEXT_GTK_IS_BUFFER(buffer), NULL);
  return INF_TEXT_GTK_BUFFER_PRIVATE(buffer)->active_user;
}

/**
 * inf_text_gtk_buffer_ensure_author_tags_priority:
 * @buffer: A #InfTextGtkBuffer.
 *
 * Ensures that all author tags have the lowest priority of all tags in the
 * underlying #GtkTextBuffer's tag table. Normally you do not need to use
 * this function if you do not set the priority for your tags explicitely.
 * However, if you do (or are forced to do, because you are using a library
 * that does this, like GtkSourceView), then you can call this function
 * afterwards to make sure all the user tags have the lowest priority.
 */
void
inf_text_gtk_buffer_ensure_author_tags_priority(InfTextGtkBuffer* buffer)
{
  InfTextGtkBufferPrivate* priv;
  GtkTextTagTable* tag_table;

  g_return_if_fail(INF_TEXT_GTK_IS_BUFFER(buffer));

  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);
  tag_table = gtk_text_buffer_get_tag_table(priv->buffer);
  gtk_text_tag_table_foreach(
    tag_table,
    inf_text_gtk_buffer_ensure_author_tags_priority_foreach_func,
    buffer
  );
}

/* vim:set et sw=2 ts=2: */
