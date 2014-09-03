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
 * SECTION:inf-text-gtk-buffer
 * @title: InfTextGtkBuffer
 * @short_description: Synchronizing a text session with a #GtkTextBuffer
 * @include: libinftextgtk/inf-text-gtk-buffer.h
 * @see_also: #InfTextBuffer
 * @stability: Unstable
 *
 * #InfTextGtkBuffer is an implementation of the #InfTextBuffer interface. It
 * can therefore be used as a backend for #InfTextSession<!-- -->s to store
 * their text. The buffer is implemented by using a #GtkTextBuffer as
 * storage. This way the text document can be displayed using a #GtkTextView
 * such that only one copy of the text is held in memory, which is used both
 * by the user interface toolkit and the text synchronization functionality.
 *
 * If external changes are made to the #GtkTextBuffer, for example by a user
 * typing into a #GtkTextView, then the text is synchronized to other
 * participants of the session. For this purpose,
 * inf_text_gtk_buffer_set_active_user() should be called with a user that
 * was previously joined into the session using inf_session_proxy_join_user().
 * If there is no local user in the session, no modifications to the buffer
 * must be made because they cannot be synchronized to other participants.
 *
 * This class also takes care of setting background colors for the text to
 * indicate which user wrote what text, by adding corresponding
 * #GtkTextTag<!-- -->s to the document. The function
 * inf_text_gtk_buffer_set_show_user_colors() to turn on or off the colored
 * background. Even if background coloring is turned off, the text is still
 * tagged according to the authorship, so that coloring can be turned on at a
 * later point or so that the authorship can still be queried for other means,
 * such as in a "blame" kind of functionality.
 */

#include <libinftextgtk/inf-text-gtk-buffer.h>
#include <libinftext/inf-text-buffer.h>

#include <libinfinity/inf-signals.h>

#include <string.h> /* for strlen() */

struct _InfTextBufferIter {
  GtkTextIter begin;
  GtkTextIter end;
  InfTextUser* user;
};

typedef struct _InfTextGtkBufferRecord InfTextGtkBufferRecord;
struct _InfTextGtkBufferRecord {
  gboolean insert;
  guint char_count;
  guint position;
  InfTextChunk* chunk;
  gboolean applied;
  InfTextGtkBufferRecord* next;
};

typedef struct _InfTextGtkBufferUserTags InfTextGtkBufferUserTags;
struct _InfTextGtkBufferUserTags {
  InfTextGtkBuffer* buffer;
  InfTextUser* user;
  GtkTextTag* colored_tag;
  GtkTextTag* colorless_tag;
};

typedef struct _InfTextGtkBufferTagRemove InfTextGtkBufferTagRemove;
struct _InfTextGtkBufferTagRemove {
  GtkTextBuffer* buffer;
  GtkTextIter begin_iter;
  GtkTextIter end_iter;
  InfTextGtkBufferUserTags* ignore_tags;
};

typedef struct _InfTextGtkBufferPrivate InfTextGtkBufferPrivate;
struct _InfTextGtkBufferPrivate {
  GtkTextBuffer* buffer;
  InfUserTable* user_table;
  GHashTable* user_tags;

  InfTextGtkBufferRecord* record;

  gboolean show_user_colors;

  InfTextUser* active_user;
  gboolean wake_on_cursor_movement;

  gdouble saturation;
  gdouble value;
  gdouble alpha;
};

enum {
  PROP_0,

  PROP_BUFFER,
  PROP_USER_TABLE,
  PROP_ACTIVE_USER,
  PROP_WAKE_ON_CURSOR_MOVEMENT,
  PROP_SHOW_USER_COLORS,

  PROP_SATURATION,
  PROP_VALUE,
  PROP_ALPHA,

  /* overriden */
  PROP_MODIFIED
};

#define INF_TEXT_GTK_BUFFER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TEXT_GTK_TYPE_BUFFER, InfTextGtkBufferPrivate))

static GQuark inf_text_gtk_buffer_tag_user_quark;

static void inf_text_gtk_buffer_buffer_iface_init(InfBufferInterface* iface);
static void inf_text_gtk_buffer_text_buffer_iface_init(InfTextBufferInterface* iface);
G_DEFINE_TYPE_WITH_CODE(InfTextGtkBuffer, inf_text_gtk_buffer, G_TYPE_OBJECT,
  G_ADD_PRIVATE(InfTextGtkBuffer)
  G_IMPLEMENT_INTERFACE(INF_TYPE_BUFFER, inf_text_gtk_buffer_buffer_iface_init)
  G_IMPLEMENT_INTERFACE(INF_TEXT_TYPE_BUFFER, inf_text_gtk_buffer_text_buffer_iface_init))

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
  InfTextGtkBufferPrivate* priv;
  gdouble hue;
  gdouble saturation;
  gdouble value;
  GdkRGBA rgba;

  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  hue = inf_text_user_get_hue(user);
  saturation = priv->saturation;
  value = priv->value;
  hsv_to_rgb(&hue, &saturation, &value);

  rgba.red = hue;
  rgba.green = saturation;
  rgba.blue = value;
  rgba.alpha = priv->alpha;

  g_object_set(G_OBJECT(tag), "background-rgba", &rgba, NULL);
}

static void
inf_text_gtk_user_notify_hue_cb(GObject* object,
                                GParamSpec* pspec,
                                gpointer user_data)
{
  InfTextGtkBuffer* buffer;
  InfTextGtkBufferPrivate* priv;
  guint user_id;
  InfTextGtkBufferUserTags* tags;

  buffer = INF_TEXT_GTK_BUFFER(user_data);
  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);
  user_id = inf_user_get_id(INF_USER(object));
  tags = g_hash_table_lookup(priv->user_tags, GUINT_TO_POINTER(user_id));
  g_assert(tags != NULL && tags->colored_tag != NULL);

  inf_text_gtk_update_tag_color(
    buffer,
    tags->colored_tag,
    INF_TEXT_USER(object)
  );
}

static void
inf_text_gtk_buffer_user_tags_free(gpointer user_tags)
{
  InfTextGtkBufferUserTags* tags;
  tags = (InfTextGtkBufferUserTags*)user_tags;

  if(tags->colored_tag)
  {
    inf_signal_handlers_disconnect_by_func(
      tags->user,
      G_CALLBACK(inf_text_gtk_user_notify_hue_cb),
      tags->buffer
    );

    g_object_unref(tags->colored_tag);
  }

  if(tags->colorless_tag)
    g_object_unref(tags->colorless_tag);
  g_slice_free(InfTextGtkBufferUserTags, tags);
}

static InfTextGtkBufferUserTags*
inf_text_gtk_buffer_get_user_tags(InfTextGtkBuffer* buffer,
                                  guint user_id)
{
  InfTextGtkBufferPrivate* priv;
  InfTextGtkBufferUserTags* tags;
  InfUser* user;

  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  if(user_id == 0)
    return NULL;

  tags = g_hash_table_lookup(priv->user_tags, GUINT_TO_POINTER(user_id));

  if(tags != NULL)
  {
    return tags;
  }
  else
  {
    user = inf_user_table_lookup_user_by_id(priv->user_table, user_id);
    g_assert(INF_TEXT_IS_USER(user));

    tags = g_slice_new(InfTextGtkBufferUserTags);
    tags->buffer = buffer;
    tags->user = INF_TEXT_USER(user);
    tags->colored_tag = NULL;
    tags->colorless_tag = NULL;
    g_hash_table_insert(priv->user_tags, GUINT_TO_POINTER(user_id), tags);
    return tags;
  }
}

static GtkTextTag*
inf_text_gtk_buffer_get_user_tag(InfTextGtkBuffer* buffer,
                                 InfTextGtkBufferUserTags* user_tags,
                                 gboolean colored)
{
  InfTextGtkBufferPrivate* priv;
  GtkTextTagTable* table;
  GtkTextTag** tag;
  gchar* tag_name;
  guint user_id;
  const gchar* colorstr;

  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);
  tag = colored ? &user_tags->colored_tag : &user_tags->colorless_tag;
  if(*tag != NULL) return *tag;

  user_id = 0;
  if(user_tags->user != NULL)
    user_id = inf_user_get_id(INF_USER(user_tags->user));
  colorstr = colored ? "colored" : "colorless";

  tag_name = g_strdup_printf("inftextgtk-user-%s-%u", colorstr, user_id);
  *tag = gtk_text_tag_new(tag_name);
  g_free(tag_name);

  table = gtk_text_buffer_get_tag_table(priv->buffer);
  gtk_text_tag_table_add(table, *tag);

  /* Set lowest priority for author tags, so GtkSourceView's bracket
   * matching highlight tags and highlight of FIXME and such in comments is
   * shown instead of the user color. */
  gtk_text_tag_set_priority(*tag, 0);

  g_object_set_qdata(
    G_OBJECT(*tag),
    inf_text_gtk_buffer_tag_user_quark,
    user_tags->user
  );

  if(colored)
  {
    g_signal_connect(
      G_OBJECT(user_tags->user),
      "notify::hue",
      G_CALLBACK(inf_text_gtk_user_notify_hue_cb),
      buffer
    );

    inf_text_gtk_update_tag_color(buffer, *tag, user_tags->user);
  }

  return *tag;
}

static InfTextUser*
inf_text_gtk_buffer_author_from_tag(GtkTextTag* tag)
{
  gpointer author_ptr;

  author_ptr = g_object_get_qdata(
    G_OBJECT(tag),
    inf_text_gtk_buffer_tag_user_quark
  );

  return INF_TEXT_USER(author_ptr);
}

static InfTextUser*
inf_text_gtk_buffer_iter_list_contains_author_tag(GSList* tag_list)
{
  GSList* item;
  InfTextUser* author;

  for(item = tag_list; item != NULL; item = g_slist_next(item))
  {
    author = inf_text_gtk_buffer_author_from_tag(GTK_TEXT_TAG(item->data));
    if(author != NULL) return author;
  }

  return NULL;
}

static InfTextUser*
inf_text_gtk_buffer_iter_get_author(GtkTextIter* location)
{
  GSList* tag_list;
  InfTextUser* author;

  tag_list = gtk_text_iter_get_tags(location);
  author = inf_text_gtk_buffer_iter_list_contains_author_tag(tag_list);
  g_slist_free(tag_list);

  return author;
}

static gboolean
inf_text_gtk_buffer_iter_is_author_toggle(const GtkTextIter* iter,
                                          InfTextUser** toggled_on,
                                          InfTextUser** toggled_off)
{
  GSList* tag_list;
  InfTextUser* author_on;
  InfTextUser* author_off;

  tag_list = gtk_text_iter_get_toggled_tags(iter, TRUE);
  author_on = inf_text_gtk_buffer_iter_list_contains_author_tag(tag_list);
  g_slist_free(tag_list);

  /* We need to check both the tags that are toggled on and the tags that
   * are toggled off at this point, because text that is not written by
   * anyone specific (author NULL) does not count as author tag. */
  if(author_on == NULL || toggled_off != NULL)
  {
    tag_list = gtk_text_iter_get_toggled_tags(iter, FALSE);
    author_off = inf_text_gtk_buffer_iter_list_contains_author_tag(tag_list);
    g_slist_free(tag_list);
  }

  if(author_on == NULL && author_off == NULL)
    if(!gtk_text_iter_is_start(iter) && !gtk_text_iter_is_end(iter))
      return FALSE;

  if(toggled_on) *toggled_on = author_on;
  if(toggled_off) *toggled_off = author_off;
  return TRUE;
}

static void
inf_text_gtk_buffer_iter_next_author_toggle(GtkTextIter* iter,
                                            InfTextUser** user_on,
                                            InfTextUser** user_off)
{
  gboolean is_author_toggle;

  do
  {
    gtk_text_iter_forward_to_tag_toggle(iter, NULL);

    is_author_toggle = inf_text_gtk_buffer_iter_is_author_toggle(
      iter,
      user_on,
      user_off
    );
  } while(!is_author_toggle);
}

static void
inf_text_gtk_buffer_iter_prev_author_toggle(GtkTextIter* iter,
                                            InfTextUser** user_on,
                                            InfTextUser** user_off)
{
  gboolean is_author_toggle;

  do
  {
    gtk_text_iter_backward_to_tag_toggle(iter, NULL);

    is_author_toggle = inf_text_gtk_buffer_iter_is_author_toggle(
      iter,
      user_on,
      user_off
    );
  } while(!is_author_toggle);
}

static void
inf_text_gtk_buffer_ensure_author_tags_priority_foreach_func(GtkTextTag* tag,
                                                             gpointer data)
{
  InfTextUser* author;
  author = inf_text_gtk_buffer_author_from_tag(tag);

  if(author != NULL)
    gtk_text_tag_set_priority(tag, 0);
}

static void
inf_text_gtk_buffer_update_user_color_tag_table_foreach_func(GtkTextTag* tag,
                                                             gpointer data)
{
  InfTextGtkBuffer* buffer;
  InfTextGtkBufferPrivate* priv;
  InfTextUser* author;

  buffer = INF_TEXT_GTK_BUFFER(data);
  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);
  author = inf_text_gtk_buffer_author_from_tag(tag);

  if(author != NULL)
    inf_text_gtk_update_tag_color(buffer, tag, author);
}

/* Required by inf_text_gtk_buffer_record_signal() and
 * inf_text_gtk_buffer_mark_set_cb() */
static void
inf_text_gtk_buffer_active_user_selection_changed_cb(InfTextUser* user,
                                                     guint position,
                                                     gint length,
                                                     gboolean by_request,
                                                     gpointer user_data);

/* Required by inf_text_gtk_buffer_record_signal() and
 * inf_text_gtk_buffer_mark_set_cb() */
static void
inf_text_gtk_buffer_active_user_notify_status_cb(GObject* object,
                                                 GParamSpec* pspec,
                                                 gpointer user_data);

static void
inf_text_gtk_buffer_apply_tag_cb(GtkTextBuffer* gtk_buffer,
                                 GtkTextTag* tag,
                                 GtkTextIter* start,
                                 GtkTextIter* end,
                                 gpointer user_data)
{
  /* Don't allow author tags to be applied by default. GTK+ seems to do this
   * when copy+pasting text from the text buffer itself, but we want to make
   * sure that a given segment of text has always a unique author set. */
  if(inf_text_gtk_buffer_author_from_tag(tag) != NULL)
    g_signal_stop_emission_by_name(G_OBJECT(gtk_buffer), "apply-tag");
}

static void
inf_text_gtk_buffer_buffer_insert_text_tag_table_foreach_func(GtkTextTag* tag,
                                                              gpointer data)
{
  InfTextGtkBufferTagRemove* tag_remove;
  tag_remove = (InfTextGtkBufferTagRemove*)data;

  if(tag_remove->ignore_tags == NULL ||
     (tag != tag_remove->ignore_tags->colored_tag &&
      tag != tag_remove->ignore_tags->colorless_tag))
  {
    gtk_text_buffer_remove_tag(
      tag_remove->buffer,
      tag,
      &tag_remove->begin_iter,
      &tag_remove->end_iter
    );
  }
}

/* Record tracking:
 * This is to allow and correctly handle nested emissions of GtkTextBuffer's
 * insert-text/delete-range signals. The text-inserted and text-erased
 * signals of InfTextBuffer need to be emitted right after the operation was
 * applied to the buffer which is why we need some bookkeeping here. */

#ifndef G_DISABLE_ASSERT
/* Check whether the top record has been applied correctly to the buffer */
static gboolean
inf_text_gtk_buffer_record_check(InfTextGtkBuffer* buffer,
                                 InfTextGtkBufferRecord* record)
{
  InfTextGtkBufferPrivate* priv;
  InfTextChunk* chunk;
  guint text_len;
  guint buf_len;
  gpointer buf_text;
  gpointer chunk_text;
  gsize buf_bytes;
  gsize chunk_bytes;
  int result;

  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  text_len = inf_text_chunk_get_length(record->chunk);
  buf_len = gtk_text_buffer_get_char_count(priv->buffer);

  /* We can only check insertions */
  if(record->insert)
  {
    if(record->char_count + text_len != buf_len)
      return FALSE;
    if(record->position + text_len > buf_len)
      return FALSE;

    chunk = inf_text_buffer_get_slice(
      INF_TEXT_BUFFER(buffer),
      record->position,
      text_len
    );

    buf_text = inf_text_chunk_get_text(record->chunk, &buf_bytes);
    chunk_text = inf_text_chunk_get_text(chunk, &chunk_bytes);
    inf_text_chunk_free(chunk);

    if(buf_bytes == chunk_bytes)
      result = memcmp(buf_text, chunk_text, buf_bytes);
    else
      result = -1;

    g_free(buf_text);
    g_free(chunk_text);
    
    if(result != 0) return FALSE;
  }
  else
  {
    if(text_len > record->char_count)
      return FALSE;
    if(record->char_count - text_len != buf_len)
      return FALSE;
  }

  return TRUE;
}
#endif

static void
inf_text_gtk_buffer_record_transform(InfTextGtkBufferRecord* record,
                                     InfTextGtkBufferRecord* against)
{
  guint record_len;
  guint against_len;

  /* What we do here is common sense; in fact this depends on how
   * insert-text/delete-range signal handlers do revalidation of iters if
   * they insert/erase text themselves. We rely on them doing it exactly
   * this way currently, otherwise we cannot identify new/erased text to
   * emit text-inserted/text-erased for, resulting in new/erased text not
   * being transmitted to remote users, in turn resulting in lost session
   * consistency. This is why the inf_text_gtk_buffer_record_check()
   * check will fail if this happens. */
  g_assert(record->applied == FALSE);
  g_assert(against->applied == TRUE);

  record_len = inf_text_chunk_get_length(record->chunk);
  against_len = inf_text_chunk_get_length(against->chunk);

  if(record->insert && against->insert)
  {
    if(record->position >= against->position)
      record->position += against_len;
  }
  else if(record->insert && !against->insert)
  {
    if(record->position >= against->position + against_len)
      record->position -= against_len;
    else if(record->position >= against->position)
      record->position = against->position;
  }
  else if(!record->insert && against->insert)
  {
    if(record->position >= against->position)
    {
      record->position += against->position;
    }
    else if(record->position < against->position &&
            record->position + record_len > against->position)
    {
      /* Add text right into deletion range... */
      inf_text_chunk_insert_chunk(
        record->chunk,
        against->position - record->position,
        against->chunk
      );
    }
  }
  else if(!record->insert && !against->insert)
  {
    if(against->position + against_len <= record->position + record_len)
    {
      record->position -= against_len;
    }
    else if(against->position + against_len > record->position &&
            against->position + against_len <= record->position + record_len)
    {
      record->position = against->position;
      inf_text_chunk_erase(
        record->chunk,
        0,
        against->position + against_len - record->position
      );
    }
    else if(against->position <= record->position &&
            against->position + against_len >= record->position + record_len)
    {
      record->position = against->position;
      inf_text_chunk_erase(
        record->chunk,
        0,
        inf_text_chunk_get_length(record->chunk)
      );
    }
    else if(against->position >= record->position &&
            against->position + against_len <= record->position + record_len)
    {
      inf_text_chunk_erase(
        record->chunk,
        against->position - record->position,
        inf_text_chunk_get_length(against->chunk)
      );
    }
    else if(against->position >= record->position &&
            against->position + against_len >= record->position + record_len)
    {
      inf_text_chunk_erase(
        record->chunk,
        against->position - record->position,
        record->position + record_len - against->position
      );
    }
  }

  /* Revalidate char count */
  if(against->insert)
  {
    record->char_count += against_len;
  }
  else
  {
    g_assert(record->char_count >= against_len);
    record->char_count -= against_len;
  }
}

static void
inf_text_gtk_buffer_record_signal(InfTextGtkBuffer* buffer,
                                  InfTextGtkBufferRecord* record)
{
  InfTextGtkBufferPrivate* priv;
  InfTextGtkBufferRecord* rec;
  InfTextGtkBufferTagRemove tag_remove;
  GtkTextTag* tag;

  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  g_assert(priv->active_user != NULL);
  g_assert(record->applied == FALSE);

  g_assert(inf_text_gtk_buffer_record_check(buffer, record));

  record->applied = TRUE;
  for(rec = record->next; rec != NULL; rec = rec->next)
    if(!rec->applied)
      inf_text_gtk_buffer_record_transform(rec->next, record);

  if(record->insert)
  {
    /* Allow author tag changes within this function: */
    inf_signal_handlers_block_by_func(
      G_OBJECT(priv->buffer),
      G_CALLBACK(inf_text_gtk_buffer_apply_tag_cb),
      buffer
    );

    /* Tag the inserted text with the user's color */
    tag_remove.buffer = priv->buffer;

    tag_remove.ignore_tags = inf_text_gtk_buffer_get_user_tags(
      buffer,
      inf_user_get_id(INF_USER(priv->active_user))
    );
    g_assert(tag_remove.ignore_tags != NULL);

    tag = inf_text_gtk_buffer_get_user_tag(
      buffer,
      tag_remove.ignore_tags,
      priv->show_user_colors
    );

    /* Remove other user tags, if any */
    gtk_text_buffer_get_iter_at_offset(
      priv->buffer,
      &tag_remove.begin_iter,
      record->position
    );

    gtk_text_buffer_get_iter_at_offset(
      priv->buffer,
      &tag_remove.end_iter,
      record->position + inf_text_chunk_get_length(record->chunk)
    );

    gtk_text_tag_table_foreach(
      gtk_text_buffer_get_tag_table(tag_remove.buffer),
      inf_text_gtk_buffer_buffer_insert_text_tag_table_foreach_func,
      &tag_remove
    );

    /* Apply tag for this particular user */
    gtk_text_buffer_apply_tag(
      priv->buffer,
      tag,
      &tag_remove.begin_iter,
      &tag_remove.end_iter
    );

    /* Allow author tag changes within this function: */
    inf_signal_handlers_unblock_by_func(
      G_OBJECT(priv->buffer),
      G_CALLBACK(inf_text_gtk_buffer_apply_tag_cb),
      buffer
    );
  }

  /* Block the notify_status signal handler of the active user. That signal
   * handler syncs the cursor position of the user to the insertion mark of
   * the TextBuffer when the user becomes active again. However, when we
   * insert or erase text, then this will be updated anyway. */
  inf_signal_handlers_block_by_func(
    G_OBJECT(priv->active_user),
    G_CALLBACK(inf_text_gtk_buffer_active_user_notify_status_cb),
    buffer
  );

  /* Block selection-changed of active user. This would try to resync the 
   * buffer markers, but GtkTextBuffer already did this for us. */
  inf_signal_handlers_block_by_func(
    G_OBJECT(priv->active_user),
    G_CALLBACK(inf_text_gtk_buffer_active_user_selection_changed_cb),
    buffer
  );

  if(record->insert)
  {
    inf_text_buffer_text_inserted(
      INF_TEXT_BUFFER(buffer),
      record->position,
      record->chunk,
      INF_USER(priv->active_user)
    );
  }
  else
  {
    inf_text_buffer_text_erased(
      INF_TEXT_BUFFER(buffer),
      record->position,
      record->chunk,
      INF_USER(priv->active_user)
    );
  }

  inf_signal_handlers_unblock_by_func(
    G_OBJECT(priv->active_user),
    G_CALLBACK(inf_text_gtk_buffer_active_user_notify_status_cb),
    buffer
  );

  inf_signal_handlers_unblock_by_func(
    G_OBJECT(priv->active_user),
    G_CALLBACK(inf_text_gtk_buffer_active_user_selection_changed_cb),
    buffer
  );
}

static void
inf_text_gtk_buffer_push_record(InfTextGtkBuffer* buffer,
                                gboolean insert,
                                guint position,
                                InfTextChunk* chunk)
{
  InfTextGtkBufferPrivate* priv;
  InfTextGtkBufferRecord* rec;

  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  rec = priv->record;

  priv->record = g_slice_new(InfTextGtkBufferRecord);
  priv->record->insert = insert;
  priv->record->char_count = gtk_text_buffer_get_char_count(priv->buffer);
  priv->record->position = position;
  priv->record->chunk = chunk;
  priv->record->applied = FALSE;
  priv->record->next = rec;

  /* It is enough to check whether the top record was applied to the buffer,
   * since, for previous records we would have been notified in a previous
   * callback already. */
  if(rec != NULL && rec->applied == FALSE)
  {
    /* If char count differs then the previous record has already been applied
     * (that is the default handler ran but not our after handler, so
     * probably another after handler inserted new text). */
    /* TODO: This does not work if length of record is zero */
    if(rec->char_count != (guint)gtk_text_buffer_get_char_count(priv->buffer))
    {
      /* This record has been applied already, so signal. */
      inf_text_gtk_buffer_record_signal(buffer, rec);

#ifndef G_ASSERT_DISABLED
      /* Outer records would already have been signalled by previous signal
       * handler invocations if they were applied. */
      for(; rec != NULL; rec = rec->next)
      {
        g_assert(
          rec->applied == TRUE ||
          rec->char_count ==
            (guint)gtk_text_buffer_get_char_count(priv->buffer)
        );
      }
#endif
    }
  }
}

static void
inf_text_gtk_buffer_pop_record(InfTextGtkBuffer* buffer)
{
  InfTextGtkBufferPrivate* priv;
  InfTextGtkBufferRecord* rec;
  guint char_count;
  guint length;

  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  g_assert(priv->record != NULL);
  if(!priv->record->applied)
  {
    length = inf_text_chunk_get_length(priv->record->chunk);
    char_count = gtk_text_buffer_get_char_count(priv->buffer);

    if(priv->record->insert)
    {
      g_assert(priv->record->char_count + length == char_count);
    }
    else
    {
      g_assert(priv->record->char_count >= length);
      g_assert(priv->record->char_count - length == char_count);
    }

    /* Signal application */
    inf_text_gtk_buffer_record_signal(buffer, priv->record);
  }

  rec = priv->record;
  priv->record = rec->next;

  inf_text_chunk_free(rec->chunk);
  g_slice_free(InfTextGtkBufferRecord, rec);
}

static void
inf_text_gtk_buffer_insert_text_cb_before(GtkTextBuffer* gtk_buffer,
                                          GtkTextIter* location,
                                          gchar* text,
                                          gint len,
                                          gpointer user_data)
{
  InfTextGtkBuffer* buffer;
  InfTextGtkBufferPrivate* priv;
  InfTextChunk* chunk;

  buffer = INF_TEXT_GTK_BUFFER(user_data);
  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  g_assert(priv->active_user != NULL);
  chunk = inf_text_chunk_new("UTF-8");

  inf_text_chunk_insert_text(
    chunk,
    0,
    text,
    len,
    g_utf8_strlen(text, len),
    inf_user_get_id(INF_USER(priv->active_user))
  );

  inf_text_gtk_buffer_push_record(
    buffer,
    TRUE,
    gtk_text_iter_get_offset(location),
    chunk
  );
}

static void
inf_text_gtk_buffer_insert_text_cb_after(GtkTextBuffer* gtk_buffer,
                                         GtkTextIter* location,
                                         gchar* text,
                                         gint len,
                                         gpointer user_data)
{
  InfTextGtkBuffer* buffer;
  InfTextGtkBufferPrivate* priv;
  gpointer rec_text;
  gsize bytes;

  buffer = INF_TEXT_GTK_BUFFER(user_data);
  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  g_assert(priv->record != NULL);
  g_assert(priv->record->insert == TRUE);

#ifndef G_ASSERT_DISABLED
  if(priv->record->applied == FALSE)
  {
    g_assert(
      priv->record->position +
        inf_text_chunk_get_length(priv->record->chunk) ==
      (guint)gtk_text_iter_get_offset(location)
    );

    rec_text = inf_text_chunk_get_text(priv->record->chunk, &bytes);
    g_assert(bytes == (gsize)len);
    g_assert(memcmp(text, rec_text, bytes) == 0);
    g_free(rec_text);
  }
#endif

  inf_text_gtk_buffer_pop_record(buffer);
}

static void
inf_text_gtk_buffer_delete_range_cb_before(GtkTextBuffer* gtk_buffer,
                                           GtkTextIter* begin,
                                           GtkTextIter* end,
                                           gpointer user_data)
{
  InfTextGtkBuffer* buffer;
  InfTextGtkBufferPrivate* priv;
  guint begin_offset;
  guint end_offset;
  InfTextChunk* chunk;

  buffer = INF_TEXT_GTK_BUFFER(user_data);
  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  begin_offset = gtk_text_iter_get_offset(begin);
  end_offset = gtk_text_iter_get_offset(end);

  chunk = inf_text_buffer_get_slice(
    INF_TEXT_BUFFER(buffer),
    begin_offset,
    end_offset - begin_offset
  );

  inf_text_gtk_buffer_push_record(buffer, FALSE, begin_offset, chunk);
}

static void
inf_text_gtk_buffer_delete_range_cb_after(GtkTextBuffer* gtk_buffer,
                                          GtkTextIter* begin,
                                          GtkTextIter* end,
                                          gpointer user_data)
{
  InfTextGtkBuffer* buffer;
  InfTextGtkBufferPrivate* priv;

  buffer = INF_TEXT_GTK_BUFFER(user_data);
  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);
  
  g_assert(priv->record != NULL);
  g_assert(priv->record->insert == FALSE);
  
  g_assert(priv->record->applied == TRUE ||
           priv->record->position == (guint)gtk_text_iter_get_offset(begin));

  inf_text_gtk_buffer_pop_record(buffer);
}

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
    /* Don't send status updates for inactive users as these would make it
     * active. Instead, we send one update when the user becomes active
     * again. */
    if(inf_user_get_status(INF_USER(priv->active_user)) == INF_USER_ACTIVE ||
       priv->wake_on_cursor_movement == TRUE)
    {
      gtk_text_buffer_get_iter_at_mark(gtk_buffer, &insert_iter, insert_mark);
      gtk_text_buffer_get_iter_at_mark(gtk_buffer, &sel_iter, sel_mark);

      offset = gtk_text_iter_get_offset(&insert_iter);
      sel = gtk_text_iter_get_offset(&sel_iter) - offset;

      if(inf_text_user_get_caret_position(priv->active_user) != offset ||
         inf_text_user_get_selection_length(priv->active_user) != sel)
      {
        /* Block the notify_status signal handler of the active user. That
         * signal handler syncs the cursor position of the user to the
         * insertion mark of the TextBuffer when the user becomes active
         * again. However, when we move the cursor, then this will be updated
         * anyway. */
        inf_signal_handlers_block_by_func(
          G_OBJECT(priv->active_user),
          G_CALLBACK(inf_text_gtk_buffer_active_user_notify_status_cb),
          buffer
        );

        inf_signal_handlers_block_by_func(
          G_OBJECT(priv->active_user),
          G_CALLBACK(inf_text_gtk_buffer_active_user_selection_changed_cb),
          buffer
        );

        inf_text_user_set_selection(priv->active_user, offset, sel, TRUE);

        inf_signal_handlers_unblock_by_func(
          G_OBJECT(priv->active_user),
          G_CALLBACK(inf_text_gtk_buffer_active_user_notify_status_cb),
          buffer
        );

        inf_signal_handlers_unblock_by_func(
          G_OBJECT(priv->active_user),
          G_CALLBACK(inf_text_gtk_buffer_active_user_selection_changed_cb),
          buffer
        );
      }
    }
  }
}

static void
inf_text_gtk_buffer_active_user_notify_status_cb(GObject* object,
                                                 GParamSpec* pspec,
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

  g_assert(INF_TEXT_USER(object) == priv->active_user);

  switch(inf_user_get_status(INF_USER(object)))
  {
  case INF_USER_ACTIVE:
    /* User became active: Sync user selection and the insertion mark of the
     * TextBuffer. They can get out of sync while the user is inactive, and
     * wake-on-cursor-movement is FALSE. For example text can be selected in
     * an inactive document, and then the user decides to select something
     * else, erasing the previous selection. */

    insert_mark = gtk_text_buffer_get_insert(priv->buffer);
    sel_mark = gtk_text_buffer_get_selection_bound(priv->buffer);

    gtk_text_buffer_get_iter_at_mark(priv->buffer, &insert_iter, insert_mark);
    gtk_text_buffer_get_iter_at_mark(priv->buffer, &sel_iter, sel_mark);

    offset = gtk_text_iter_get_offset(&insert_iter);
    sel = gtk_text_iter_get_offset(&sel_iter) - offset;

    if(inf_text_user_get_caret_position(priv->active_user) != offset ||
       inf_text_user_get_selection_length(priv->active_user) != sel)
    {
      inf_signal_handlers_block_by_func(
        G_OBJECT(priv->active_user),
        G_CALLBACK(inf_text_gtk_buffer_active_user_selection_changed_cb),
        buffer
      );

      inf_text_user_set_selection(priv->active_user, offset, sel, TRUE);

      inf_signal_handlers_unblock_by_func(
        G_OBJECT(priv->active_user),
        G_CALLBACK(inf_text_gtk_buffer_active_user_selection_changed_cb),
        buffer
      );
    }

    break;
  case INF_USER_UNAVAILABLE:
    /* TODO: Do we want to unset the active-user automatically here? */
    break;
  default:
    /* Not of interest. */
    break;
  }
}

static void
inf_text_gtk_buffer_active_user_selection_changed_cb(InfTextUser* user,
                                                     guint position,
                                                     gint selection_length,
                                                     gboolean by_request,
                                                     gpointer user_data)
{
  InfTextGtkBuffer* buffer;
  InfTextGtkBufferPrivate* priv;
  GtkTextIter insert;
  GtkTextIter selection_bound;

  buffer = INF_TEXT_GTK_BUFFER(user_data);
  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  inf_signal_handlers_block_by_func(
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

  inf_signal_handlers_unblock_by_func(
    G_OBJECT(priv->buffer),
    G_CALLBACK(inf_text_gtk_buffer_mark_set_cb),
    buffer
  );
}

static void
inf_text_gtk_buffer_modified_changed_cb(GtkTextBuffer* buffer,
                                        gpointer user_data)
{
  g_object_notify(G_OBJECT(user_data), "modified");
}

static void
inf_text_gtk_buffer_set_modified(InfTextGtkBuffer* buffer,
                                 gboolean modified)
{
  InfTextGtkBufferPrivate* priv;
  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  if(priv->buffer != NULL)
  {
    inf_signal_handlers_block_by_func(
      G_OBJECT(priv->buffer),
      G_CALLBACK(inf_text_gtk_buffer_modified_changed_cb),
      buffer
    );

    gtk_text_buffer_set_modified(priv->buffer, modified);

    inf_signal_handlers_unblock_by_func(
      G_OBJECT(priv->buffer),
      G_CALLBACK(inf_text_gtk_buffer_modified_changed_cb),
      buffer
    );

    g_object_notify(G_OBJECT(buffer), "modified");
  }
}

static void
inf_text_gtk_buffer_set_buffer(InfTextGtkBuffer* buffer,
                               GtkTextBuffer* gtk_buffer)
{
  InfTextGtkBufferPrivate* priv;
  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  if(priv->buffer != NULL)
  {
    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->buffer),
      G_CALLBACK(inf_text_gtk_buffer_apply_tag_cb),
      buffer
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->buffer),
      G_CALLBACK(inf_text_gtk_buffer_insert_text_cb_before),
      buffer
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->buffer),
      G_CALLBACK(inf_text_gtk_buffer_insert_text_cb_after),
      buffer
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->buffer),
      G_CALLBACK(inf_text_gtk_buffer_delete_range_cb_before),
      buffer
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->buffer),
      G_CALLBACK(inf_text_gtk_buffer_delete_range_cb_after),
      buffer
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->buffer),
      G_CALLBACK(inf_text_gtk_buffer_mark_set_cb),
      buffer
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->buffer),
      G_CALLBACK(inf_text_gtk_buffer_modified_changed_cb),
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
      G_CALLBACK(inf_text_gtk_buffer_insert_text_cb_before),
      buffer
    );

    g_signal_connect_after(
      G_OBJECT(gtk_buffer),
      "insert-text",
      G_CALLBACK(inf_text_gtk_buffer_insert_text_cb_after),
      buffer
    );

    g_signal_connect(
      G_OBJECT(gtk_buffer),
      "delete-range",
      G_CALLBACK(inf_text_gtk_buffer_delete_range_cb_before),
      buffer
    );

    g_signal_connect_after(
      G_OBJECT(gtk_buffer),
      "delete-range",
      G_CALLBACK(inf_text_gtk_buffer_delete_range_cb_after),
      buffer
    );

    g_signal_connect_after(
      G_OBJECT(gtk_buffer),
      "mark-set",
      G_CALLBACK(inf_text_gtk_buffer_mark_set_cb),
      buffer
    );

    g_signal_connect_after(
      G_OBJECT(gtk_buffer),
      "modified-changed",
      G_CALLBACK(inf_text_gtk_buffer_modified_changed_cb),
      buffer
    );
  }

  g_object_notify(G_OBJECT(buffer), "buffer");

  /* TODO: Notify modified, if it changed */
}

static void
inf_text_gtk_buffer_init(InfTextGtkBuffer* buffer)
{
  InfTextGtkBufferPrivate* priv;
  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  priv->buffer = NULL;
  priv->user_table = NULL;

  priv->user_tags = g_hash_table_new_full(
    NULL,
    NULL,
    NULL,
    inf_text_gtk_buffer_user_tags_free
  );

  priv->show_user_colors = TRUE;

  priv->active_user = NULL;
  priv->wake_on_cursor_movement = FALSE;

  priv->saturation = 0.35;
  priv->value = 1.0;
  priv->alpha = 1.0;
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

  G_OBJECT_CLASS(inf_text_gtk_buffer_parent_class)->dispose(object);
}

static void
inf_text_gtk_buffer_finalize(GObject* object)
{
  InfTextGtkBuffer* buffer;
  InfTextGtkBufferPrivate* priv;

  buffer = INF_TEXT_GTK_BUFFER(object);
  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  g_hash_table_unref(priv->user_tags);

  G_OBJECT_CLASS(inf_text_gtk_buffer_parent_class)->finalize(object);
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
  case PROP_WAKE_ON_CURSOR_MOVEMENT:
    priv->wake_on_cursor_movement = g_value_get_boolean(value);
    break;
  case PROP_SHOW_USER_COLORS:
    priv->show_user_colors = g_value_get_boolean(value);
    break;
  case PROP_MODIFIED:
    inf_text_gtk_buffer_set_modified(buffer, g_value_get_boolean(value));
    break;
  case PROP_SATURATION:
    inf_text_gtk_buffer_set_saturation_value(
      buffer,
      g_value_get_double(value),
      priv->value
    );
    break;
  case PROP_VALUE:
    inf_text_gtk_buffer_set_saturation_value(
      buffer,
      priv->saturation,
      g_value_get_double(value)
    );
    break;
  case PROP_ALPHA:
    inf_text_gtk_buffer_set_fade(buffer, g_value_get_double(value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
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
  case PROP_WAKE_ON_CURSOR_MOVEMENT:
    g_value_set_boolean(value, priv->wake_on_cursor_movement);
    break;
  case PROP_SHOW_USER_COLORS:
    g_value_set_boolean(value, priv->show_user_colors);
    break;
  case PROP_MODIFIED:
    if(priv->buffer != NULL)
      g_value_set_boolean(value, gtk_text_buffer_get_modified(priv->buffer));
    else
      g_value_set_boolean(value, FALSE);

    break;
  case PROP_SATURATION:
    g_value_set_double(value, priv->saturation);
    break;
  case PROP_VALUE:
    g_value_set_double(value, priv->value);
    break;
  case PROP_ALPHA:
    g_value_set_double(value, priv->alpha);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static gboolean
inf_text_gtk_buffer_buffer_get_modified(InfBuffer* buffer)
{
  InfTextGtkBuffer* gtk_buffer;
  InfTextGtkBufferPrivate* priv;

  gtk_buffer = INF_TEXT_GTK_BUFFER(buffer);
  priv = INF_TEXT_GTK_BUFFER_PRIVATE(gtk_buffer);

  if(priv->buffer != NULL)
    return gtk_text_buffer_get_modified(priv->buffer);
  else
    return FALSE;
}

static void
inf_text_gtk_buffer_buffer_set_modified(InfBuffer* buffer,
                                        gboolean modified)
{
  inf_text_gtk_buffer_set_modified(INF_TEXT_GTK_BUFFER(buffer), modified);
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
  InfTextUser* author;
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
    inf_text_gtk_buffer_iter_next_author_toggle(&iter, NULL, &author);

    size = gtk_text_iter_get_offset(&iter) - gtk_text_iter_get_offset(&begin);

    /* Not the whole segment if region to slice ends before segment end */
    if(size > remaining)
    {
      size = remaining;
      iter = begin;
      gtk_text_iter_forward_chars(&iter, size);
    }

    text = gtk_text_buffer_get_slice(priv->buffer, &begin, &iter, TRUE);

    /* TODO: Faster inf_text_chunk_append that optionally eats text */
    inf_text_chunk_insert_text(
      result,
      len - remaining,
      text,
      strlen(text), /* I hate strlen. GTK+ should tell us how many bytes. */
      size,
      (author == NULL) ? 0 : inf_user_get_id(INF_USER(author))
    );

    remaining -= size;
    g_free(text);
  }

  return result;
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
  GtkTextTag* tag;

  GtkTextMark* mark;
  GtkTextIter insert_iter;
  gboolean insert_at_cursor;
  gboolean insert_at_selection_bound;

  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);
  tag_remove.buffer = priv->buffer;

  /* This would have to be handled separately, but I think this is unlikely
   * to happen anyway. If it does happen then we would again need to rely on
   * iterator revalidation to happen in the way we expect it. */
  g_assert(priv->record == NULL);

  /* Allow author tag changes within this function: */
  inf_signal_handlers_block_by_func(
    G_OBJECT(priv->buffer),
    G_CALLBACK(inf_text_gtk_buffer_apply_tag_cb),
    buffer
  );

  inf_signal_handlers_block_by_func(
    G_OBJECT(priv->buffer),
    G_CALLBACK(inf_text_gtk_buffer_insert_text_cb_before),
    buffer
  );

  inf_signal_handlers_block_by_func(
    G_OBJECT(priv->buffer),
    G_CALLBACK(inf_text_gtk_buffer_insert_text_cb_after),
    buffer
  );

  if(inf_text_chunk_iter_init_begin(chunk, &chunk_iter))
  {
    gtk_text_buffer_get_iter_at_offset(
      priv->buffer,
      &tag_remove.end_iter,
      pos
    );

    do
    {
      tag_remove.ignore_tags = inf_text_gtk_buffer_get_user_tags(
        INF_TEXT_GTK_BUFFER(buffer),
        inf_text_chunk_iter_get_author(&chunk_iter)
      );

      if(tag_remove.ignore_tags)
      {
        tag = inf_text_gtk_buffer_get_user_tag(
          INF_TEXT_GTK_BUFFER(buffer),
          tag_remove.ignore_tags,
          priv->show_user_colors
        );
      }
      else
      {
        tag = NULL;
      }

      gtk_text_buffer_insert_with_tags(
        tag_remove.buffer,
        &tag_remove.end_iter,
        inf_text_chunk_iter_get_text(&chunk_iter),
        inf_text_chunk_iter_get_bytes(&chunk_iter),
        tag,
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

    /* TODO: We could also do this by simply resyncing the text buffer marks
     * to the active user's caret and selection properties. But then we
     * wouldn't have left gravtiy if no active user was present. */
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
        inf_signal_handlers_block_by_func(
          G_OBJECT(priv->buffer),
          G_CALLBACK(inf_text_gtk_buffer_mark_set_cb),
          buffer
        );

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

        inf_signal_handlers_unblock_by_func(
          G_OBJECT(priv->buffer),
          G_CALLBACK(inf_text_gtk_buffer_mark_set_cb),
          buffer
        );
      }
    }
  }

  inf_signal_handlers_unblock_by_func(
    G_OBJECT(priv->buffer),
    G_CALLBACK(inf_text_gtk_buffer_apply_tag_cb),
    buffer
  );

  inf_signal_handlers_unblock_by_func(
    G_OBJECT(priv->buffer),
    G_CALLBACK(inf_text_gtk_buffer_insert_text_cb_before),
    buffer
  );

  inf_signal_handlers_unblock_by_func(
    G_OBJECT(priv->buffer),
    G_CALLBACK(inf_text_gtk_buffer_insert_text_cb_after),
    buffer
  );

  inf_text_buffer_text_inserted(buffer, pos, chunk, user);
}

static void
inf_text_gtk_buffer_buffer_erase_text(InfTextBuffer* buffer,
                                      guint pos,
                                      guint len,
                                      InfUser* user)
{
  InfTextGtkBufferPrivate* priv;
  InfTextChunk* chunk;

  GtkTextIter begin;
  GtkTextIter end;

  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  /* This would have to be handled separately, but I think this is unlikely
   * to happen anyway. If it does happen then we would again need to rely on
   * iterator revalidation to happen in the way we expect it. */
  g_assert(priv->record == NULL);

  chunk = inf_text_buffer_get_slice(buffer, pos, len);

  gtk_text_buffer_get_iter_at_offset(priv->buffer, &begin, pos);
  gtk_text_buffer_get_iter_at_offset(priv->buffer, &end, pos + len);

  inf_signal_handlers_block_by_func(
    G_OBJECT(priv->buffer),
    G_CALLBACK(inf_text_gtk_buffer_delete_range_cb_before),
    buffer
  );

  inf_signal_handlers_block_by_func(
    G_OBJECT(priv->buffer),
    G_CALLBACK(inf_text_gtk_buffer_delete_range_cb_after),
    buffer
  );

  gtk_text_buffer_delete(priv->buffer, &begin, &end);

  inf_signal_handlers_unblock_by_func(
    G_OBJECT(priv->buffer),
    G_CALLBACK(inf_text_gtk_buffer_delete_range_cb_before),
    buffer
  );

  inf_signal_handlers_unblock_by_func(
    G_OBJECT(priv->buffer),
    G_CALLBACK(inf_text_gtk_buffer_delete_range_cb_after),
    buffer
  );

  inf_text_buffer_text_erased(buffer, pos, chunk, user);
  inf_text_chunk_free(chunk);
}

static InfTextBufferIter*
inf_text_gtk_buffer_buffer_create_begin_iter(InfTextBuffer* buffer)
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
    inf_text_gtk_buffer_iter_next_author_toggle(
      &iter->end,
      NULL,
      &iter->user
    );

    return iter;
  }
}

static InfTextBufferIter*
inf_text_gtk_buffer_buffer_create_end_iter(InfTextBuffer* buffer)
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
    gtk_text_buffer_get_end_iter(priv->buffer, &iter->end);

    iter->begin = iter->end;
    inf_text_gtk_buffer_iter_prev_author_toggle(
      &iter->begin,
      &iter->user,
      NULL
    );

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
  inf_text_gtk_buffer_iter_next_author_toggle(&iter->end, NULL, &iter->user);
  return TRUE;
}

static gboolean
inf_text_gtk_buffer_buffer_iter_prev(InfTextBuffer* buffer,
                                     InfTextBufferIter* iter)
{
  if(gtk_text_iter_is_start(&iter->begin))
    return FALSE;

  iter->end = iter->begin;
  inf_text_gtk_buffer_iter_prev_author_toggle(
    &iter->begin,
    &iter->user,
    NULL
  );

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
inf_text_gtk_buffer_buffer_iter_get_offset(InfTextBuffer* buffer,
                                           InfTextBufferIter* iter)
{
  return gtk_text_iter_get_offset(&iter->begin);
}

static guint
inf_text_gtk_buffer_buffer_iter_get_length(InfTextBuffer* buffer,
                                           InfTextBufferIter* iter)
{
  return gtk_text_iter_get_offset(&iter->end) -
    gtk_text_iter_get_offset(&iter->begin);
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
  return (iter->user == NULL) ? 0 : inf_user_get_id(INF_USER(iter->user));
}

static void
inf_text_gtk_buffer_class_init(InfTextGtkBufferClass* text_gtk_buffer_class)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(text_gtk_buffer_class);

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

  g_object_class_install_property(
    object_class,
    PROP_WAKE_ON_CURSOR_MOVEMENT,
    g_param_spec_boolean(
      "wake-on-cursor-movement",
      "Wake on cursor movement",
      "Whether to make inactive users active when the insertion mark in the "
      "TextBuffer moves",
      FALSE,
      G_PARAM_READWRITE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_SHOW_USER_COLORS,
    g_param_spec_boolean(
      "show-user-colors",
      "Show user colors",
      "Whether to show user colors initially for newly written text",
      TRUE,
      G_PARAM_READWRITE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_SATURATION,
    g_param_spec_double(
      "saturation",
      "Saturation",
      "Saturation of user colors in a HSV color model",
      0.0,
      1.0,
      0.35,
      G_PARAM_READWRITE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_VALUE,
    g_param_spec_double(
      "value",
      "Value",
      "Value of user colors in a HSV color model",
      0.0,
      1.0,
      1.0,
      G_PARAM_READWRITE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_VALUE,
    g_param_spec_double(
      "alpha",
      "Alpha",
      "The translucency of the user color",
      0.0,
      1.0,
      1.0,
      G_PARAM_READWRITE
    )
  );

  g_object_class_override_property(object_class, PROP_MODIFIED, "modified");
}

static void
inf_text_gtk_buffer_buffer_iface_init(InfBufferInterface* iface)
{
  iface->get_modified = inf_text_gtk_buffer_buffer_get_modified;
  iface->set_modified = inf_text_gtk_buffer_buffer_set_modified;
}

static void
inf_text_gtk_buffer_text_buffer_iface_init(InfTextBufferInterface* iface)
{
  iface->get_encoding = inf_text_gtk_buffer_buffer_get_encoding;
  iface->get_length = inf_text_gtk_buffer_get_length;
  iface->get_slice = inf_text_gtk_buffer_buffer_get_slice;
  iface->insert_text = inf_text_gtk_buffer_buffer_insert_text;
  iface->erase_text = inf_text_gtk_buffer_buffer_erase_text;
  iface->create_begin_iter = inf_text_gtk_buffer_buffer_create_begin_iter;
  iface->create_end_iter = inf_text_gtk_buffer_buffer_create_end_iter;
  iface->destroy_iter = inf_text_gtk_buffer_buffer_destroy_iter;
  iface->iter_next = inf_text_gtk_buffer_buffer_iter_next;
  iface->iter_prev = inf_text_gtk_buffer_buffer_iter_prev;
  iface->iter_get_text = inf_text_gtk_buffer_buffer_iter_get_text;
  iface->iter_get_offset = inf_text_gtk_buffer_buffer_iter_get_offset;
  iface->iter_get_length = inf_text_gtk_buffer_buffer_iter_get_length;
  iface->iter_get_bytes = inf_text_gtk_buffer_buffer_iter_get_bytes;
  iface->iter_get_author = inf_text_gtk_buffer_buffer_iter_get_author;
  iface->text_inserted = NULL;
  iface->text_erased = NULL;
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
    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->active_user),
      G_CALLBACK(inf_text_gtk_buffer_active_user_notify_status_cb),
      buffer
    );

    inf_signal_handlers_disconnect_by_func(
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
      "notify::status",
      G_CALLBACK(inf_text_gtk_buffer_active_user_notify_status_cb),
      buffer
    );

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
 * inf_text_gtk_buffer_get_author:
 * @buffer: A #InfTextGtkBuffer.
 * @location: A #GtkTextIter which is not the end iterator.
 *
 * Returns the #InfTextUser which wrote the character at @location. If there
 * is no such user, then %NULL is returned.
 *
 * Return Value: A #InfTextUser, or %NULL.
 */
InfTextUser*
inf_text_gtk_buffer_get_author(InfTextGtkBuffer* buffer,
                               GtkTextIter* location)
{
  InfTextGtkBufferPrivate* priv;

  g_return_val_if_fail(INF_TEXT_GTK_IS_BUFFER(buffer), NULL);

  g_return_val_if_fail(
    location != NULL && !gtk_text_iter_is_end(location),
    NULL
  );

  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);
  return inf_text_gtk_buffer_iter_get_author(location);
}

/**
 * inf_text_gtk_buffer_get_user_for_tag:
 * @buffer: A #InfTextGtkBuffer.
 * @tag: A #GtkTextTag from @buffer's underlying #GtkTextBuffer's tag table.
 *
 * If @tag is an author tag, i.e. used by @buffer to mark text that a certain
 * user has written, then this function returns the #InfTextUser whose text is
 * marked by @tag. If @tag is not an author tag then the function returns
 * %NULL.
 *
 * Returns: A #InfTextUser, or %NULL.
 */
InfTextUser*
inf_text_gtk_buffer_get_user_for_tag(InfTextGtkBuffer* buffer,
                                     GtkTextTag* tag)
{
  g_return_val_if_fail(INF_TEXT_GTK_IS_BUFFER(buffer), NULL);
  g_return_val_if_fail(GTK_IS_TEXT_TAG(tag), NULL);

  return inf_text_gtk_buffer_author_from_tag(tag);
}

/**
 * inf_text_gtk_buffer_is_author_toggle:
 * @buffer: A #InfTextGtkBuffer.
 * @iter: A #GtkTextIter pointing into @buffer's underlying #GtkTextBuffer.
 * @user_on: A location to store a #InfTextUser, or %NULL.
 * @user_off: Another location to store a #InfTextUser, or %NULL.
 *
 * This function returns %TRUE if the author of the text in @buffer changes
 * at @iter, or %FALSE otherwise. If it returns %TRUE, then the user who
 * authored the text to the right hand side of @iter is stored in @user_on (if
 * non-%NULL) and the author of the text to the left hand side of @iter is
 * stored in @user_off (if non-%NULL). Both can also be set to %NULL if there
 * is unowned text in the buffer or if @iter is at the start or end of the
 * buffer.
 *
 * Returns: Whether text attribution changes at @iter.
 */
gboolean
inf_text_gtk_buffer_is_author_toggle(InfTextGtkBuffer* buffer,
                                     const GtkTextIter* iter,
                                     InfTextUser** user_on,
                                     InfTextUser** user_off)
{
  g_return_val_if_fail(INF_TEXT_GTK_IS_BUFFER(buffer), FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);

  return inf_text_gtk_buffer_iter_is_author_toggle(
    iter,
    user_on,
    user_off
  );
}

/**
 * inf_text_gtk_buffer_forward_to_author_toggle:
 * @buffer: A #InfTextGtkBuffer.
 * @iter: A #GtkTextIter pointing into @buffer's underlying #GtkTextBuffer.
 * @user_on: A location to store a #InfTextUser, or %NULL.
 * @user_off: Another location to store a #InfTextUser, or %NULL.
 *
 * Moves @iter to the next point in @buffer's underlying #GtkTextBuffer where
 * the text has been written by another user. If @iter points to the end of
 * the buffer, then the function does nothing and returns %FALSE. Otherwise
 * it returns %TRUE and sets @user_on to the user which has written the text
 * on the right hand side of the location @iter has been moved to (if
 * non-%NULL) and @user_off to the user which has written the left hand side
 * of the location @iter has been moved to.
 *
 * Returns: %TRUE if @iter was moved, or %FALSE otherwise.
 */
gboolean
inf_text_gtk_buffer_forward_to_author_toggle(InfTextGtkBuffer* buffer,
                                             GtkTextIter* iter,
                                             InfTextUser** user_on,
                                             InfTextUser** user_off)
{
  g_return_val_if_fail(INF_TEXT_GTK_IS_BUFFER(buffer), FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);

  if(gtk_text_iter_is_end(iter))
    return FALSE;

  inf_text_gtk_buffer_iter_next_author_toggle(iter, user_on, user_off);
  return TRUE;
}

/**
 * inf_text_gtk_buffer_backward_to_author_toggle:
 * @buffer: A #InfTextGtkBuffer.
 * @iter: A #GtkTextIter pointing into @buffer's underlying #GtkTextBuffer.
 * @user_on: A location to store a #InfTextUser, or %NULL.
 * @user_off: Another location to store a #InfTextUser, or %NULL.
 *
 * Moves @iter to the previous point in @buffer's underlying #GtkTextBuffer
 * where the text has been written by another user. If @iter points to the
 * beginning of the buffer, then the function does nothing and returns %FALSE.
 * Otherwise it returns %TRUE and sets @user_on to the user which has written
 * the text on the right hand side of the location @iter has been moved to (if
 * non-%NULL) and @user_off to the user which has written the left hand side
 * of the location @iter has been moved to.
 *
 * Returns: %TRUE if @iter was moved, or %FALSE otherwise.
 */
gboolean
inf_text_gtk_buffer_backward_to_author_toggle(InfTextGtkBuffer* buffer,
                                              GtkTextIter* iter,
                                              InfTextUser** user_on,
                                              InfTextUser** user_off)
{
  g_return_val_if_fail(INF_TEXT_GTK_IS_BUFFER(buffer), FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);

  if(gtk_text_iter_is_start(iter))
    return FALSE;

  inf_text_gtk_buffer_iter_prev_author_toggle(iter, user_on, user_off);
  return TRUE;
}

/**
 * inf_text_gtk_buffer_set_wake_on_cursor_movement:
 * @buffer: A #InfTextGtkBuffer.
 * @wake: Whether to make inactive users active on cursor movement.
 *
 * This function spcecifies whether movement of the insertion point or
 * selection bound of the underlying text buffer causes the active user
 * (see inf_text_gtk_buffer_set_active_user()) to become active when its
 * status is %INF_USER_INACTIVE.
 *
 * If @wake is %TRUE, then the user status changes to %INF_USER_ACTIVE
 * in that case. If @wake is %FALSE, then the user status stays
 * %INF_USER_INACTIVE, and its caret-position and selection-length
 * properties will be no longer be synchronized to the buffer marks until
 * the user is set active again.
 */

void
inf_text_gtk_buffer_set_wake_on_cursor_movement(InfTextGtkBuffer* buffer,
                                                gboolean wake)
{
  g_return_if_fail(INF_TEXT_GTK_IS_BUFFER(buffer));
  INF_TEXT_GTK_BUFFER_PRIVATE(buffer)->wake_on_cursor_movement = wake;
  g_object_notify(G_OBJECT(buffer), "wake-on-cursor-movement");
}

/**
 * inf_text_gtk_buffer_get_wake_on_cursor_movement:
 * @buffer: A #InfTextGtkBuffer.
 *
 * Returns whether movement of the insertion point or selection bound of the
 * underlying text buffer causes whether the active user (see
 * inf_text_gtk_buffer_set_active_user()) to become active when its status
 * is %INF_USER_INACTIVE. See also
 * inf_text_gtk_buffer_set_wake_on_cursor_movement().
 *
 * Returns: Whether to make inactive users active when the insertion mark
 * is moved.
 */
gboolean
inf_text_gtk_buffer_get_wake_on_cursor_movement(InfTextGtkBuffer* buffer)
{
  g_return_val_if_fail(INF_TEXT_GTK_IS_BUFFER(buffer), FALSE);
  return INF_TEXT_GTK_BUFFER_PRIVATE(buffer)->wake_on_cursor_movement;
}

/**
 * inf_text_gtk_buffer_ensure_author_tags_priority:
 * @buffer: A #InfTextGtkBuffer.
 *
 * Ensures that all author tags have the lowest priority of all tags in the
 * underlying #GtkTextBuffer's tag table. Normally you do not need to use
 * this function if you do not set the priority for your tags explicitely.
 * However, if you do (or are forced to do, because you are using a library
 * that does this, such as GtkSourceView), then you can call this function
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

/**
 * inf_text_gtk_buffer_set_saturation_value:
 * @buffer: A #InfTextGtkBuffer.
 * @saturation: Saturation to use for user colors.
 * @value: Value to use for user colors.
 *
 * Sets the saturation and value to use for user colors in a HSV color model.
 * The hue is defined by each user's individual color. The reason why S and V
 * are set locally the same for all users is that they can be adjusted
 * depending on one's theme: Dark themes want dark user colors, bright themes
 * want bright ones.
 */
void
inf_text_gtk_buffer_set_saturation_value(InfTextGtkBuffer* buffer,
                                         gdouble saturation,
                                         gdouble value)
{
  InfTextGtkBufferPrivate* priv;
  GtkTextTagTable* tag_table;

  g_return_if_fail(INF_TEXT_GTK_IS_BUFFER(buffer));
  g_return_if_fail(saturation >= 0.0 && saturation <= 1.0);
  g_return_if_fail(value >= 0.0 && value <= 1.0);

  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  if(saturation == priv->saturation && value == priv->value)
    return;

  g_object_freeze_notify(G_OBJECT(buffer));
  if(saturation != priv->saturation)
  {
    priv->saturation = saturation;
    g_object_notify(G_OBJECT(buffer), "saturation");
  }

  if(value != priv->value)
  {
    priv->value = value;
    g_object_notify(G_OBJECT(buffer), "value");
  }

  tag_table = gtk_text_buffer_get_tag_table(priv->buffer);
  gtk_text_tag_table_foreach(
    tag_table,
    inf_text_gtk_buffer_update_user_color_tag_table_foreach_func,
    buffer
  );
  g_object_thaw_notify(G_OBJECT(buffer));
}

/**
 * inf_text_gtk_buffer_set_fade:
 * @buffer: A #InfTextGtkBuffer.
 * @alpha: An alpha value between 0.0 and 1.0.
 *
 * This functions can be used to show the user background color with limited
 * intensity, such that the background of the #GtkTextView showing the buffer
 * partly shines through.
 *
 * An @alpha value of 1.0 means to fully show the user background color, a
 * value of 0.0 means to show the given background color. Values inbetween
 * interpolate linearly between the two colors in RGB color space.
 *
 * The default value for @alpha is 1.0.
 */
void
inf_text_gtk_buffer_set_fade(InfTextGtkBuffer* buffer,
                             gdouble alpha)
{
  InfTextGtkBufferPrivate* priv;
  GtkTextTagTable* tag_table;

  g_return_if_fail(INF_TEXT_GTK_IS_BUFFER(buffer));
  g_return_if_fail(alpha >= 0.0 && alpha <= 1.0);

  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);

  g_object_freeze_notify(G_OBJECT(buffer));
  if(alpha != priv->alpha)
  {
    priv->alpha = alpha;
    g_object_notify(G_OBJECT(buffer), "alpha");
  }

  tag_table = gtk_text_buffer_get_tag_table(priv->buffer);

  gtk_text_tag_table_foreach(
    tag_table,
    inf_text_gtk_buffer_update_user_color_tag_table_foreach_func,
    buffer
  );

  g_object_thaw_notify(G_OBJECT(buffer));
}

/**
 * inf_text_gtk_buffer_get_saturation:
 * @buffer: A #InfTextGtkBuffer.
 *
 * Returns the saturation part of the HSV user color.
 *
 * Returns: The saturation used for user colors.
 */
gdouble
inf_text_gtk_buffer_get_saturation(InfTextGtkBuffer* buffer)
{
  g_return_val_if_fail(INF_TEXT_GTK_IS_BUFFER(buffer), 0.0);
  return INF_TEXT_GTK_BUFFER_PRIVATE(buffer)->saturation;
}

/**
 * inf_text_gtk_buffer_get_value:
 * @buffer: A #InfTextGtkBuffer.
 *
 * Returns the value part of the HSV user color.
 *
 * Returns: The value used for user colors.
 */
gdouble
inf_text_gtk_buffer_get_value(InfTextGtkBuffer* buffer)
{
  g_return_val_if_fail(INF_TEXT_GTK_IS_BUFFER(buffer), 0.0);
  return INF_TEXT_GTK_BUFFER_PRIVATE(buffer)->value;
}

/**
 * inf_text_gtk_buffer_set_show_user_colors:
 * @buffer: A #InfTextGtkBuffer.
 * @show: Whether to show user colors or not.
 *
 * If @show is %TRUE (the default), then the user color is used as background
 * for newly written text by that user. Otherwise, newly written text has no
 * background color.
 *
 * Note that this setting is for newly written text only. If you want to show
 * or hide user colors for existing text use
 * inf_text_gtk_buffer_show_user_colors().
 */
void
inf_text_gtk_buffer_set_show_user_colors(InfTextGtkBuffer* buffer,
                                         gboolean show)
{
  g_return_if_fail(INF_TEXT_GTK_IS_BUFFER(buffer));
  INF_TEXT_GTK_BUFFER_PRIVATE(buffer)->show_user_colors = show;
  g_object_notify(G_OBJECT(buffer), "show-user-colors");
}

/**
 * inf_text_gtk_buffer_get_show_user_colors:
 * @buffer: A #InfTextGtkBuffer.
 *
 * Returns whether newly written text is attributed with the author's user
 * color or not.
 *
 * Returns: %TRUE if user color is applied to newly written text, or %FALSE
 * otherwise.
 */
gboolean
inf_text_gtk_buffer_get_show_user_colors(InfTextGtkBuffer* buffer)
{
  g_return_val_if_fail(INF_TEXT_GTK_IS_BUFFER(buffer), FALSE);
  return INF_TEXT_GTK_BUFFER_PRIVATE(buffer)->show_user_colors;
}

/**
 * inf_text_gtk_buffer_show_user_colors:
 * @buffer: A #InfTextGtkBuffer.
 * @show: Whether to show or hide user colors.
 * @start: Beginning of the range for which to show or hide user colors.
 * @end: End of the range for which to show or hide user colors.
 *
 * If @show is %FALSE, then don't show user colors (which user wrote what
 * text) as the background of the text, in the range from @start to @end.
 * If @show is %TRUE, show user colors if they have previously been hidden
 * via a call to this function with @show being %FALSE.
 */
void
inf_text_gtk_buffer_show_user_colors(InfTextGtkBuffer* buffer,
                                     gboolean show,
                                     GtkTextIter* start,
                                     GtkTextIter* end)
{
  InfTextGtkBufferPrivate* priv;
  GtkTextIter iter;
  GtkTextIter prev;
  InfTextUser* user;
  InfTextGtkBufferUserTags* tags;
  GtkTextTag* hide_tag;
  GtkTextTag* show_tag;

  g_return_if_fail(INF_TEXT_GTK_IS_BUFFER(buffer));
  g_return_if_fail(start != NULL);
  g_return_if_fail(end != NULL);

  priv = INF_TEXT_GTK_BUFFER_PRIVATE(buffer);
  iter = *start;
  prev = iter;

  while(!gtk_text_iter_equal(&iter, end))
  {
    inf_text_gtk_buffer_iter_next_author_toggle(&iter, NULL, &user);
    if(gtk_text_iter_compare(&iter, end) > 0)
      iter = *end;

    if(user != NULL)
    {
      tags = g_hash_table_lookup(
        priv->user_tags,
        GUINT_TO_POINTER(inf_user_get_id(INF_USER(user)))
      );
      g_assert(tags != NULL);

      if(show)
      {
        hide_tag = inf_text_gtk_buffer_get_user_tag(buffer, tags, FALSE);
        show_tag = inf_text_gtk_buffer_get_user_tag(buffer, tags, TRUE);
      }
      else
      {
        hide_tag = inf_text_gtk_buffer_get_user_tag(buffer, tags, TRUE);
        show_tag = inf_text_gtk_buffer_get_user_tag(buffer, tags, FALSE);
      }

      inf_signal_handlers_block_by_func(
        priv->buffer,
        G_CALLBACK(inf_text_gtk_buffer_apply_tag_cb),
        buffer
      );

      gtk_text_buffer_remove_tag(priv->buffer, hide_tag, &prev, &iter);
      gtk_text_buffer_apply_tag(priv->buffer, show_tag, &prev, &iter);

      inf_signal_handlers_unblock_by_func(
        priv->buffer,
        G_CALLBACK(inf_text_gtk_buffer_apply_tag_cb),
        buffer
      );
    }

    prev = iter;
  }
}

/* vim:set et sw=2 ts=2: */
