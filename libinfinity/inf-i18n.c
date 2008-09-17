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

#include <libinfinity/inf-i18n.h>

#include <glib.h>

#include "config.h"

const char*
_inf_gettext(const char* msgid)
{
	/* I don't want to introduce a generic inf_init() function just because
	 * of gettext. However, if we need one some day, then we can move
	 * gettext initialization there. */
	static gboolean gettext_initialized = FALSE;
	if(G_UNLIKELY(!gettext_initialized))
	{
		gettext_initialized = TRUE;

		bindtextdomain(GETTEXT_PACKAGE, INF_LOCALEDIR);
		bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
	}

	return dgettext(GETTEXT_PACKAGE, msgid);
}
