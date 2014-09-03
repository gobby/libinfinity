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

#include <libinfgtk/inf-gtk-chat.h>
#include <libinfinity/inf-i18n.h>
#include <libinfinity/inf-signals.h>

#include <gdk/gdkkeysyms.h>

#include <string.h> /* For strlen() */

/**
 * SECTION:inf-gtk-chat
 * @title: InfGtkChat
 * @short_description: Gtk interface to #InfChatSession
 * @include: libinfgtk/inf-gtk-chat.h
 * @stability: Unstable
 *
 * #InfGtkChat is a widget showing a #InfChatSession conversation. Use
 * inf_gtk_chat_set_session() to set the session whose conversation to show
 * in the widget. If you have a local user in the session you can also call
 * inf_gtk_chat_set_active_user(). In this case the input text entry is made
 * available and messages are sent via that user.
 **/

/* This is a small hack to get the scrolling in the textview right */
typedef enum _InfGtkChatVMode {
  /* VMode is disabled, always keep bottom row constant */
  INF_GTK_CHAT_VMODE_DISABLED,
  /* VMode is enabled, keep top row constant for next line addition */
  INF_GTK_CHAT_VMODE_ENABLED,
  /* VMode is set, keep top row constant */
  INF_GTK_CHAT_VMODE_SET
} InfGtkChatVMode;

typedef struct _InfGtkChatEntryKeyPressEventCbForeachData
  InfGtkChatEntryKeyPressEventCbForeachData;
struct _InfGtkChatEntryKeyPressEventCbForeachData {
  gchar* match;

  guint count;
  guint index;

  InfUser* result;
  InfUser* first;
};

typedef struct _InfGtkChatPrivate InfGtkChatPrivate;
struct _InfGtkChatPrivate {
  GtkWidget* chat_view;
  GtkWidget* entry;
  GtkWidget* button;
  GtkAdjustment* vadj;
  gdouble voffset;
  InfGtkChatVMode vmode;

  InfChatSession* session;
  InfChatBuffer* buffer;
  InfUser* active_user;

  GtkTextTag* tag_normal;
  GtkTextTag* tag_system;
  GtkTextTag* tag_emote;
  GtkTextTag* tag_backlog;

  /* Tab completion */
  gchar* completion_text;
  guint completion_start;
  guint completion_end;
  guint completion_index;
};

enum {
  PROP_0,

  PROP_SESSION,
  PROP_ACTIVE_USER
};

#define INF_GTK_CHAT_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_GTK_TYPE_CHAT, InfGtkChatPrivate))

G_DEFINE_TYPE_WITH_CODE(InfGtkChat, inf_gtk_chat, GTK_TYPE_VBOX,
  G_ADD_PRIVATE(InfGtkChat))

/*
 * Message representation
 */

/* Copied from InfChatSession. TODO: Should go as a public helper function
 * in libinfinity. */
static gchar*
inf_gtk_chat_strdup_strftime(const char* format,
                             const struct tm* tm,
                             gsize* len)
{
  gsize alloc;
  gchar* str;
  size_t result;

  alloc = 64;
  str = g_malloc(alloc * sizeof(gchar));
  result = strftime(str, alloc, format, tm);

  while(result == 0 && alloc < 1024)
  {
    alloc *= 2;
    str = g_realloc(str, alloc * sizeof(gchar));
    result = strftime(str, alloc, format, tm);
  }

  if(result == 0)
  {
    g_free(str);
    return NULL;
  }

  if(len) *len = result;
  return str;
}

static void
inf_gtk_chat_add_message(InfGtkChat* chat,
                         const InfChatBufferMessage* message)
{
  InfGtkChatPrivate* priv;
  GtkTextBuffer* buffer;
  GtkTextIter insert_pos;

  const gchar* formatter;
  time_t cur_time;
  struct tm cur_time_tm;
  struct tm given_time_tm;
  gchar* time_str;
  gchar* loc_text;
  gchar* text;
  GtkTextTag* tag;

  gdouble scroll_val;
  gdouble scroll_upper;
  gdouble scroll_page_size;

  priv = INF_GTK_CHAT_PRIVATE(chat);
  buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(priv->chat_view));

  cur_time = time(NULL);
  cur_time_tm = *localtime(&cur_time);
  given_time_tm = *localtime(&message->time);

  /* Show date if text was not logged today */
  if(cur_time_tm.tm_yday != given_time_tm.tm_yday ||
     cur_time_tm.tm_year != given_time_tm.tm_year)
  {
    formatter = "%x %X";
  }
  else
  {
    formatter = "%X";
  }

  time_str = inf_gtk_chat_strdup_strftime(formatter, &given_time_tm, NULL);
  switch(message->type)
  {
  case INF_CHAT_BUFFER_MESSAGE_NORMAL:
    tag = priv->tag_normal;
    text = g_strdup_printf(
      "[%s] <%s> %s",
      time_str,
      inf_user_get_name(message->user),
      message->text
    );
    break;
  case INF_CHAT_BUFFER_MESSAGE_EMOTE:
    tag = priv->tag_emote;
    text = g_strdup_printf(
      "[%s] * %s %s",
      time_str,
      inf_user_get_name(message->user),
      message->text
    );
    break;
  case INF_CHAT_BUFFER_MESSAGE_USERJOIN:
    tag = priv->tag_system;
    loc_text =
      g_strdup_printf(_("%s has joined"), inf_user_get_name(message->user));
    text = g_strdup_printf("[%s] %s", time_str, loc_text);
    g_free(loc_text);
    break;
  case INF_CHAT_BUFFER_MESSAGE_USERPART:
    tag = priv->tag_system;
    loc_text =
      g_strdup_printf(_("%s has left"), inf_user_get_name(message->user));
    text = g_strdup_printf("[%s] %s", time_str, loc_text);
    g_free(loc_text);
    break;
  default:
    g_assert_not_reached();
    break;
  }

  g_free(time_str);

  /* Always apply backlog tag if it's a backlog message */
  if(message->flags & INF_CHAT_BUFFER_MESSAGE_BACKLOG)
    tag = priv->tag_backlog;

  g_object_get( G_OBJECT(priv->vadj),
    "value", &scroll_val,
    "upper", &scroll_upper,
    "page-size", &scroll_page_size,
    NULL
  );

  gtk_text_buffer_get_end_iter(buffer, &insert_pos);
  gtk_text_buffer_insert_with_tags(buffer, &insert_pos, text, -1, tag, NULL);
  gtk_text_buffer_insert(buffer, &insert_pos, "\n", 1);

  if(scroll_val != scroll_upper - scroll_page_size &&
     scroll_upper - scroll_page_size > 0 &&
     priv->vmode == INF_GTK_CHAT_VMODE_ENABLED)
  {
    /* This is a kind of hack to keep the view where it is, otherwise
     * inf_gtk_chat_adjustment_changed_cb() would try to keep the distance
     * to the bottom row constant, moving the viewport by the newly
     * added row. */
    priv->vmode = INF_GTK_CHAT_VMODE_SET;
  }
}

/* like the g_utf8_next_char macro, but without the cast to char* at the end
 */
#define inf_utf8_next_char(p) ((p) + g_utf8_skip[*(const guchar *)(p)])

static void
inf_gtk_chat_commit_message(InfGtkChat* chat)
{
  InfGtkChatPrivate* priv;
  const gchar* text;

  priv = INF_GTK_CHAT_PRIVATE(chat);

  g_assert(priv->session != NULL);
  g_assert(priv->buffer != NULL);
  g_assert(priv->active_user != NULL);

  text = gtk_entry_get_text(GTK_ENTRY(priv->entry));

  if(g_str_has_prefix(text, "/me") &&
     (text[3] == '\0' || g_unichar_isspace(g_utf8_get_char(text+3))))
  {
    text += 3;
    while(g_unichar_isspace(g_utf8_get_char(text)))
      text = inf_utf8_next_char(text);

    inf_chat_buffer_add_emote_message(
      priv->buffer,
      priv->active_user,
      text,
      /* TODO: Use gtk_entry_get_text_length() - (text -
       * gtk_entry_get_text())) once we can use GTK+ 2.14. */
      strlen(text),
      time(NULL),
      0
    );
  }
  else
  {
    inf_chat_buffer_add_message(
      priv->buffer,
      priv->active_user,
      text,
      /* TODO: Use gtk_entry_get_text_length() once we can use GTK+ 2.14. */
      strlen(text),
      time(NULL),
      0
    );
  }

  gtk_entry_set_text(GTK_ENTRY(priv->entry), "");
}

/*
 * Signal handlers
 */

static void
inf_gtk_chat_entry_activate_cb(GtkEntry* entry,
                               gpointer user_data)
{
  const gchar* text;
  text = gtk_entry_get_text(entry);

  if(text != NULL && *text != '\0')
    inf_gtk_chat_commit_message(INF_GTK_CHAT(user_data));
}

static void
inf_gtk_chat_entry_changed_cb(GtkEntry* entry,
                              gpointer user_data)
{
  InfGtkChat* chat;
  InfGtkChatPrivate* priv;

  chat = INF_GTK_CHAT(user_data);
  priv = INF_GTK_CHAT_PRIVATE(chat);

  /* Reset tab completion */
  g_free(priv->completion_text);
  priv->completion_text = NULL;
}

static void
inf_gtk_chat_entry_key_press_event_cb_foreach_func(InfUser* user,
                                                   gpointer user_data)
{
  InfGtkChatEntryKeyPressEventCbForeachData* data;
  gchar* normalized;
  gchar* casefold;

  data = (InfGtkChatEntryKeyPressEventCbForeachData*)user_data;

  if(inf_user_get_status(user) != INF_USER_UNAVAILABLE)
  {
    normalized =
      g_utf8_normalize(inf_user_get_name(user), -1, G_NORMALIZE_ALL);
    casefold = g_utf8_casefold(normalized, -1);
    g_free(normalized);

    if(g_str_has_prefix(casefold, data->match))
    {
      if(data->count == data->index)
        data->result = user;
      ++data->count;

      if(data->first == NULL)
        data->first = user;
    }

    g_free(casefold);
  }
}

static gboolean
inf_gtk_chat_entry_key_press_event_cb(GtkWidget* widget,
                                      GdkEventKey* event,
                                      gpointer user_data)
{
  InfGtkChat* chat;
  InfGtkChatPrivate* priv;
  guint index;
  guint begin;
  const gchar* text;
  gchar* normalized;
  InfGtkChatEntryKeyPressEventCbForeachData foreach_data;
  InfUser* complete;
  int position;

  chat = INF_GTK_CHAT(user_data);
  priv = INF_GTK_CHAT_PRIVATE(chat);

  /* This must not be pressed for tab completion to be triggered: */
#define MASK (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK)

  if(event->keyval == GDK_KEY_Tab && (event->state & MASK) == 0)
  {
    index = gtk_editable_get_position(GTK_EDITABLE(widget));
    if(priv->completion_text != NULL && index != priv->completion_end)
    {
      /* Cursor was moved since last completion attempt: Reset completion */
      g_free(priv->completion_text);
      priv->completion_text = NULL;
    }
    else if(priv->completion_text != NULL)
    {
      /* Otherwise complete next in row */
      if(priv->completion_end > 0)
        ++priv->completion_index;
    }

    if(priv->completion_text == NULL)
    {
      /* No previous completion, so find completion text and completion
       * starting point. */
      begin = index;
      text = gtk_entry_get_text(GTK_ENTRY(priv->entry));
      while(begin > 0)
      {
        begin = g_utf8_prev_char(&text[begin]) - text;
        if(g_unichar_isspace(g_utf8_get_char(&text[begin])))
        {
          begin = inf_utf8_next_char(&text[begin]) - text;
          break;
        }
      }

      if(begin < index)
      {
        normalized = g_utf8_normalize(
          &text[begin],
          index - begin,
          G_NORMALIZE_ALL
        );
        priv->completion_text = g_utf8_casefold(normalized, -1);
        g_free(normalized);

        priv->completion_end = index;
        priv->completion_start = begin;
      }
    }

    if(priv->completion_text != NULL)
    {
      foreach_data.match = priv->completion_text;

      foreach_data.count = 0;
      foreach_data.index = priv->completion_index;
      foreach_data.result = NULL;
      foreach_data.first = NULL;

      inf_user_table_foreach_user(
        inf_session_get_user_table(INF_SESSION(priv->session)),
        inf_gtk_chat_entry_key_press_event_cb_foreach_func,
        &foreach_data
      );

      complete = foreach_data.result;
      if(complete == NULL)
      {
        if(foreach_data.first != NULL)
        {
          /* wrap around */
          complete = foreach_data.first;
          priv->completion_index = 0;
        }
      }

      if(complete == NULL)
      {
        /* No match: Reset completion, don't do anything */
        g_free(priv->completion_text);
        priv->completion_text = NULL;
      }
      else
      {
        inf_signal_handlers_block_by_func(
          G_OBJECT(priv->entry),
          G_CALLBACK(inf_gtk_chat_entry_changed_cb),
          chat
        );

        if(priv->completion_start != priv->completion_end)
        {
          gtk_editable_delete_text(
            GTK_EDITABLE(widget),
            priv->completion_start,
            priv->completion_end
          );
        }

        position = priv->completion_start;
        gtk_editable_insert_text(
          GTK_EDITABLE(priv->entry),
          inf_user_get_name(complete),
          -1,
          &position
        );

        if(priv->completion_start == 0)
        {
          gtk_editable_insert_text(
            GTK_EDITABLE(priv->entry),
            ": ",
            2,
            &position
          );
        }
        else
        {
          gtk_editable_insert_text(
            GTK_EDITABLE(priv->entry),
            " ",
            1,
            &position
          );
        }

        priv->completion_end = position;
        gtk_editable_set_position(GTK_EDITABLE(priv->entry), position);

        inf_signal_handlers_unblock_by_func(
          G_OBJECT(priv->entry),
          G_CALLBACK(inf_gtk_chat_entry_changed_cb),
          chat
        );
      }
    }

    return TRUE;
  }

  return FALSE;
}

static void
inf_gtk_chat_entry_button_clicked_cb(GtkButton* button,
                                     gpointer user_data)
{
  inf_gtk_chat_commit_message(INF_GTK_CHAT(user_data));
}

static void
inf_gtk_chat_buffer_add_message_cb(InfChatBuffer* buffer,
                                   const InfChatBufferMessage* message,
                                   gpointer user_data)
{
  inf_gtk_chat_add_message(INF_GTK_CHAT(user_data), message);
}

static void
inf_gtk_chat_user_notify_status_cb(GObject* object,
                                   GParamSpec* pspec,
                                   gpointer user_data)
{
  if(inf_user_get_status(INF_USER(object)) == INF_USER_UNAVAILABLE)
    inf_gtk_chat_set_active_user(INF_GTK_CHAT(user_data), NULL);
}

static void
inf_gtk_chat_user_notify_flags_cb(GObject* object,
                                  GParamSpec* pspec,
                                  gpointer user_data)
{
  if( (inf_user_get_flags(INF_USER(object)) & INF_USER_LOCAL) == 0)
    inf_gtk_chat_set_active_user(INF_GTK_CHAT(user_data), NULL);
}

static void
inf_gtk_chat_adjustment_changed_cb(GObject* object,
                                   gpointer user_data)
{
  InfGtkChatPrivate* priv;
  InfGtkChatVMode prev_mode;
  gdouble value;
  gdouble new_value;
  gdouble upper;
  gdouble page_size;
  gdouble max;

  priv = INF_GTK_CHAT_PRIVATE(user_data);

  g_object_get(
    object,
    "value", &value,
    "upper", &upper,
    "page-size", &page_size,
    NULL
  );

  max = (upper > page_size) ? (upper - page_size) : 0.0;
  if(priv->vmode != INF_GTK_CHAT_VMODE_SET)
  {
    prev_mode = priv->vmode;
    new_value = (max > priv->voffset) ? (max - priv->voffset) : 0.0;
    if(value != new_value)
    {
      gtk_adjustment_set_value(GTK_ADJUSTMENT(object), new_value);

      /* Undo effect of signal handler: We only enable vmode operation if
       * the adjustment value was changed independently, for example by the
       * user moving the scrollbar. */
      priv->vmode = prev_mode;
    }
  }
  else
  {
    priv->voffset = (max > value) ? (max - value) : 0.0;
    priv->vmode = INF_GTK_CHAT_VMODE_ENABLED;
  }
}

static void
inf_gtk_chat_adjustment_value_changed_cb(GObject* object,
                                         gpointer user_data)
{
  InfGtkChatPrivate* priv;
  gdouble value;
  gdouble upper;
  gdouble page_size;
  gdouble max;

  priv = INF_GTK_CHAT_PRIVATE(user_data);

  g_object_get(
    object,
    "value", &value,
    "upper", &upper,
    "page-size", &page_size,
    NULL
  );

  max = (upper > page_size) ? (upper - page_size) : 0.0;
  priv->voffset = (max > value) ? (max - value) : 0.0;

  /* Enable vmode as soon as we scroll away from the bottom of the textview.
   * This keeps the viewport constant when adding new rows but the scroll
   * position not being at the bottom of the view. Due to some strange GTK+
   * weirdness this does not work when initially populating the buffer with
   * backlog messages, so we enable this explicitely after the scrollbar is
   * moved away from the very bottom of the view. */
  if(priv->vmode == INF_GTK_CHAT_VMODE_DISABLED)
    priv->vmode = INF_GTK_CHAT_VMODE_ENABLED;
}

/*
 * GObject overrides
 */

static void
inf_gtk_chat_init(InfGtkChat* chat)
{
  InfGtkChatPrivate* priv;

  GtkWidget* scroll;
  GtkWidget* image;
  GtkWidget* hbox;

  priv = INF_GTK_CHAT_PRIVATE(chat);

  priv->session = NULL;
  priv->buffer = NULL;
  priv->active_user = NULL;
  priv->voffset = 0.0;
  priv->vmode = INF_GTK_CHAT_VMODE_DISABLED;

  /* Actually they are invalid as long as completion_text is NULL, but
   * let's be sure */
  priv->completion_text = NULL;
  priv->completion_start = 0;
  priv->completion_end = 0;
  priv->completion_index = 0;

  priv->chat_view = gtk_text_view_new();
  gtk_text_view_set_editable(GTK_TEXT_VIEW(priv->chat_view), FALSE);
  gtk_text_view_set_wrap_mode(
    GTK_TEXT_VIEW(priv->chat_view),
    GTK_WRAP_WORD_CHAR
  );
  gtk_text_view_set_indent(GTK_TEXT_VIEW(priv->chat_view), -12);
  /* TODO: this prevents copying via ctrl+c - maybe the entry ctrl+c
   * should catch this and copy from the textview instead: */
  gtk_widget_set_can_focus(priv->chat_view, FALSE);
  gtk_widget_show(priv->chat_view);

  /* TODO: These should probably be style properties: */

  priv->tag_normal = gtk_text_buffer_create_tag(
    GTK_TEXT_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(priv->chat_view))),
    "normal",
    NULL
  );

  priv->tag_system = gtk_text_buffer_create_tag(
    GTK_TEXT_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(priv->chat_view))),
    "system",
    "foreground", "#0000ff",
    NULL
  );

  priv->tag_emote = gtk_text_buffer_create_tag(
    GTK_TEXT_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(priv->chat_view))),
    "emote",
    "foreground", "#113322",
    NULL
  );

  priv->tag_backlog = gtk_text_buffer_create_tag(
    GTK_TEXT_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(priv->chat_view))),
    "backlog",
    "foreground", "#606060",
    NULL
  );

  scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_shadow_type(
    GTK_SCROLLED_WINDOW(scroll),
    GTK_SHADOW_IN
  );
  gtk_scrolled_window_set_policy(
    GTK_SCROLLED_WINDOW(scroll),
    GTK_POLICY_AUTOMATIC,
    GTK_POLICY_AUTOMATIC
  );
  gtk_container_add(GTK_CONTAINER(scroll), priv->chat_view);
  priv->vadj =
    gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll));
  gtk_widget_show(scroll);

  g_signal_connect(
    G_OBJECT(priv->vadj),
    "changed",
    G_CALLBACK(inf_gtk_chat_adjustment_changed_cb),
    chat
  );

  g_signal_connect(
    G_OBJECT(priv->vadj),
    "value-changed",
    G_CALLBACK(inf_gtk_chat_adjustment_value_changed_cb),
    chat
  );

  priv->entry = gtk_entry_new();
  g_object_set(G_OBJECT(priv->entry), "truncate-multiline", TRUE, NULL);

  g_signal_connect(
    G_OBJECT(priv->entry),
    "activate",
    G_CALLBACK(inf_gtk_chat_entry_activate_cb),
    chat
  );

  g_signal_connect_after(
    G_OBJECT(priv->entry),
    "changed",
    G_CALLBACK(inf_gtk_chat_entry_changed_cb),
    chat
  );

  g_signal_connect(
    G_OBJECT(priv->entry),
    "key-press-event",
    G_CALLBACK(inf_gtk_chat_entry_key_press_event_cb),
    chat
  );

  gtk_widget_set_sensitive(priv->entry, FALSE);
  gtk_widget_show(priv->entry);

  image = gtk_image_new_from_icon_name("go-jump", GTK_ICON_SIZE_BUTTON);
  priv->button = gtk_button_new_with_label(_("Send"));
  gtk_button_set_image(GTK_BUTTON(priv->button), image);
  g_signal_connect(
    G_OBJECT(priv->button),
    "clicked",
    G_CALLBACK(inf_gtk_chat_entry_button_clicked_cb),
    chat
  );
  /*gtk_widget_show(priv->button);*/

  hbox = gtk_hbox_new(FALSE, 12);
  gtk_box_pack_start(GTK_BOX(hbox), priv->entry, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(hbox), priv->button, FALSE, TRUE, 0);
  gtk_widget_show(hbox);

  gtk_box_pack_start(GTK_BOX(chat), scroll, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(chat), hbox, FALSE, TRUE, 0);
  gtk_box_set_spacing(GTK_BOX(chat), 6);
}

static void
inf_gtk_chat_dispose(GObject* object)
{
  InfGtkChat* chat;
  InfGtkChatPrivate* priv;

  chat = INF_GTK_CHAT(object);
  priv = INF_GTK_CHAT_PRIVATE(chat);

  if(priv->session != NULL)
    inf_gtk_chat_set_session(chat, NULL);

  G_OBJECT_CLASS(inf_gtk_chat_parent_class)->dispose(object);
}

static void
inf_gtk_chat_finalize(GObject* object)
{
  InfGtkChat* chat;
  InfGtkChatPrivate* priv;

  chat = INF_GTK_CHAT(object);
  priv = INF_GTK_CHAT_PRIVATE(chat);

  g_free(priv->completion_text);

  G_OBJECT_CLASS(inf_gtk_chat_parent_class)->finalize(object);
}

static void
inf_gtk_chat_set_property(GObject* object,
                          guint prop_id,
                          const GValue* value,
                          GParamSpec* pspec)
{
  InfGtkChat* chat;
  InfGtkChatPrivate* priv;

  chat = INF_GTK_CHAT(object);
  priv = INF_GTK_CHAT_PRIVATE(chat);

  switch(prop_id)
  {
  case PROP_SESSION:
    inf_gtk_chat_set_session(
      chat,
      INF_CHAT_SESSION(g_value_get_object(value))
    );

    break;
  case PROP_ACTIVE_USER:
    inf_gtk_chat_set_active_user(chat, INF_USER(g_value_get_object(value)));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_gtk_chat_get_property(GObject* object,
                          guint prop_id,
                          GValue* value,
                          GParamSpec* pspec)
{
  InfGtkChat* chat;
  InfGtkChatPrivate* priv;

  chat = INF_GTK_CHAT(object);
  priv = INF_GTK_CHAT_PRIVATE(chat);

  switch(prop_id)
  {
  case PROP_SESSION:
    g_value_set_object(value, priv->session);
    break;
  case PROP_ACTIVE_USER:
    g_value_set_object(value, priv->active_user);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

/*
 * GType registration
 */

static void
inf_gtk_chat_class_init(InfGtkChatClass* chat_class)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(chat_class);

  object_class->dispose = inf_gtk_chat_dispose;
  object_class->finalize = inf_gtk_chat_finalize;
  object_class->set_property = inf_gtk_chat_set_property;
  object_class->get_property = inf_gtk_chat_get_property;

  g_object_class_install_property(
    object_class,
    PROP_SESSION,
    g_param_spec_object(
      "session",
      "Session",
      "The chat session this widget is displaying",
      INF_TYPE_CHAT_SESSION,
      G_PARAM_READWRITE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_ACTIVE_USER,
    g_param_spec_object(
      "active-user",
      "Active user",
      "The user outgoing messages come from",
      INF_TYPE_USER,
      G_PARAM_READWRITE
    )
  );
}

/*
 * Public API.
 */

/**
 * inf_gtk_chat_new:
 *
 * Creates a new #InfGtkChat. To show a chat conversation set a session to
 * show via inf_gtk_chat_set_session().
 *
 * Returns: A new #InfGtkChat.
 */
GtkWidget*
inf_gtk_chat_new(void)
{
  GObject* object;
  object = g_object_new(INF_GTK_TYPE_CHAT, NULL);
  return GTK_WIDGET(object);
}

/**
 * inf_gtk_chat_set_session:
 * @chat: A #InfGtkChat.
 * @session: The #InfChatSession to set.
 *
 * Sets the chat session to show in the chat widget. If there is a previous
 * session set the chat view will be cleared before showing the new session.
 * If the previous session had an active user set it will be unset. If
 * @session is %NULL this function just clears the chat view and unsets the
 * active user, if any.
 */
void
inf_gtk_chat_set_session(InfGtkChat* chat,
                         InfChatSession* session)
{
  InfGtkChatPrivate* priv;
  guint i;

  g_return_if_fail(INF_GTK_IS_CHAT(chat));
  g_return_if_fail(session == NULL || INF_IS_CHAT_SESSION(session));

  priv = INF_GTK_CHAT_PRIVATE(chat);

  /* Notify "active-user" and "session" simultanously */
  g_object_freeze_notify(G_OBJECT(chat));

  if(priv->session != NULL)
  {
    g_assert(priv->buffer != NULL);

    /* Remove active user, if any */
    if(priv->active_user != NULL)
      inf_gtk_chat_set_active_user(chat, NULL);

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->buffer),
      G_CALLBACK(inf_gtk_chat_buffer_add_message_cb),
      chat
    );

    g_object_unref(priv->session);
    g_object_unref(priv->buffer);

    gtk_text_buffer_set_text(
      gtk_text_view_get_buffer(GTK_TEXT_VIEW(priv->chat_view)),
      "",
      0
    );
  }

  priv->session = session;

  if(session != NULL)
  {
    g_object_ref(session);

    priv->buffer =
      INF_CHAT_BUFFER(inf_session_get_buffer(INF_SESSION(session)));
    g_object_ref(priv->buffer);

    g_signal_connect_after(
      G_OBJECT(priv->buffer),
      "add-message",
      G_CALLBACK(inf_gtk_chat_buffer_add_message_cb),
      chat
    );

    /* Show messages from oldest to newest */
    for(i = 0; i < inf_chat_buffer_get_n_messages(priv->buffer); ++i)
    {
      inf_gtk_chat_add_message(
        chat,
        inf_chat_buffer_get_message(priv->buffer, i)
      );
    }
  }
  else
  {
    priv->buffer = NULL;
  }

  g_object_notify(G_OBJECT(chat), "session");
  g_object_thaw_notify(G_OBJECT(chat));
}

/**
 * inf_gtk_chat_set_active_user:
 * @chat: A #InfGtkChat.
 * @user: A local #InfUser which joined chat's session.
 *
 * Sets the active user for the chat. This must be a user in the chat's
 * session's user table and it must have the %INF_USER_LOCAL flag set, i.e.
 * you need to have it joined before using inf_session_proxy_join_user().
 *
 * If an active user is set the chat's text entry is made sensitive and the
 * user can type chat messages. They are sent to the session as originated by
 * @user. If @user's status changes to %INF_USER_UNAVAILABLE or the
 * %INF_USER_LOCAL flag is removed the active user will be unset
 * automatically.
 *
 * This cannot be called when the chat has no session set yet.
 * Use inf_gtk_chat_set_session() first.
 */
void
inf_gtk_chat_set_active_user(InfGtkChat* chat,
                             InfUser* user)
{
  InfGtkChatPrivate* priv;
  const gchar* text;

  g_return_if_fail(INF_GTK_IS_CHAT(chat));
  g_return_if_fail(user == NULL || INF_IS_USER(user));

  priv = INF_GTK_CHAT_PRIVATE(chat);
  g_return_if_fail(priv->session != NULL);

  g_return_if_fail(
    user == NULL || inf_user_get_status(user) != INF_USER_UNAVAILABLE
  );
  g_return_if_fail(
    user == NULL || (inf_user_get_flags(user) & INF_USER_LOCAL) != 0
  );
  g_return_if_fail(
    user == NULL ||
    inf_user_table_lookup_user_by_id(
      inf_session_get_user_table(INF_SESSION(priv->session)),
      inf_user_get_id(user)
    ) == user
  );

  if(priv->active_user != NULL)
  {
    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->active_user),
      G_CALLBACK(inf_gtk_chat_user_notify_status_cb),
      chat
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->active_user),
      G_CALLBACK(inf_gtk_chat_user_notify_flags_cb),
      chat
    );

    g_object_unref(priv->active_user);
  }

  priv->active_user = user;

  if(user != NULL)
  {
    g_object_ref(user);

    g_signal_connect(
      G_OBJECT(user),
      "notify::status",
      G_CALLBACK(inf_gtk_chat_user_notify_status_cb),
      chat
    );

    g_signal_connect(
      G_OBJECT(user),
      "notify::flags",
      G_CALLBACK(inf_gtk_chat_user_notify_flags_cb),
      chat
    );

    gtk_widget_set_sensitive(priv->entry, TRUE);

    text = gtk_entry_get_text(GTK_ENTRY(priv->entry));
    gtk_widget_set_sensitive(priv->button, text != NULL && *text != '\0');

    /* TODO: Only do this when there currently is no focus child: */
    /* TODO: Doesn't work anyway: */
#if 0
    gtk_container_set_focus_child(
      GTK_CONTAINER(
        gtk_widget_get_parent(gtk_widget_get_parent(priv->entry))
      ),
      gtk_widget_get_parent(priv->entry)
    );

    gtk_container_set_focus_child(
      GTK_CONTAINER(gtk_widget_get_parent(priv->entry)),
      priv->entry
    );
#endif
  }
  else
  {
    gtk_widget_set_sensitive(priv->entry, FALSE);
    gtk_widget_set_sensitive(priv->button, FALSE);
  }

  g_object_notify(G_OBJECT(chat), "active-user");
}

/**
 * inf_gtk_chat_get_active_user:
 * @chat: A #InfGtkChat.
 *
 * Returns the active user for @chat as set with
 * inf_gtk_chat_set_active_user().
 *
 * Returns: The chat's active user, or %NULL if there is none.
 */
InfUser*
inf_gtk_chat_get_active_user(InfGtkChat* chat)
{
  g_return_val_if_fail(INF_GTK_IS_CHAT(chat), NULL);
  return INF_GTK_CHAT_PRIVATE(chat)->active_user;
}

/* TODO: I don't like this API because it allows users to do crap with the
 * entry such as setting it to be sensitive when it shouldn't be. It's
 * currently used to set the focus on the entry widget after setting the 
 * active user for which I did not find another way... IMHO doing
 * gtk_container_set_focus_child() should be enough, but it isn't. I'm not
 * sure what the problem is... maybe we should replace it by
 * inf_gtk_chat_entry_grab_focus() or something. */

/**
 * inf_gtk_chat_get_entry:
 * @chat: A #InfGtkChat.
 *
 * Returns the chat's text input entry.
 *
 * Returns: The chat's #GtkEntry. This is owned by the chat, so you don't
 * need to free it.
 */
GtkWidget*
inf_gtk_chat_get_entry(InfGtkChat* chat)
{
  g_return_val_if_fail(INF_GTK_IS_CHAT(chat), NULL);
  return INF_GTK_CHAT_PRIVATE(chat)->entry;
}

/* vim:set et sw=2 ts=2: */
