/* libinfinity/inf-config.h.  Generated from inf-config.h.in by configure.  */
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

#ifndef __INF_I18N_H__
#define __INF_I18N_H__

#include <libintl.h>

#define _(String) _inf_gettext(String)
#ifdef gettext_noop
# define N_(String) gettext_noop(String)
#else
# define N_(String) (String)
#endif

const char* _inf_gettext(const char* msgid);

#endif /* __INF_I18N_H__ */
