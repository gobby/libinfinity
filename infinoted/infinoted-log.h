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

#ifndef __INFINOTED_LOG_H__
#define __INFINOTED_LOG_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define INFINOTED_TYPE_LOG                 (infinoted_log_get_type())
#define INFINOTED_LOG(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INFINOTED_TYPE_LOG, InfinotedLog))
#define INFINOTED_LOG_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INFINOTED_TYPE_LOG, InfinotedLogClass))
#define INFINOTED_IS_LOG(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INFINOTED_TYPE_LOG))
#define INFINOTED_IS_LOG_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INFINOTED_TYPE_LOG))
#define INFINOTED_LOG_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INFINOTED_TYPE_LOG, InfinotedLogClass))

typedef struct _InfinotedLog InfinotedLog;
typedef struct _InfinotedLogClass InfinotedLogClass;

/**
 * InfinotedLogClass:
 * @log_message: Default signal handler for the #InfinotedLog::log-message
 * signal.
 *
 * This structure contains default signal handlers for #InfinotedLog.
 */
struct _InfinotedLogClass {
  /*< private >*/
  GObjectClass parent_class;

  /*< public >*/
  void (*log_message)(InfinotedLog* log,
                      guint prio,
                      guint depth,
                      const gchar* text);
};

/**
 * InfinotedLog:
 *
 * #InfinotedLog is an opaque data type. You should only access it via the
 * public API functions.
 */
struct _InfinotedLog {
  /*< private >*/
  GObject parent;
};

GType
infinoted_log_get_type(void) G_GNUC_CONST;

InfinotedLog*
infinoted_log_new(void);

gboolean
infinoted_log_open(InfinotedLog* log,
                   const gchar* path,
                   GError** error);

void
infinoted_log_close(InfinotedLog* log);

void
infinoted_log_log(InfinotedLog* log,
                  guint prio,
                  const char* fmt,
                  ...);

void
infinoted_log_error(InfinotedLog* log,
                    const char* fmt,
                    ...);

void
infinoted_log_warning(InfinotedLog* log,
                      const char* fmt,
                      ...);

void
infinoted_log_info(InfinotedLog* log,
                   const char* fmt,
                   ...);

G_END_DECLS

#endif /* __INFINOTED_LOG_H__ */

/* vim:set et sw=2 ts=2: */
