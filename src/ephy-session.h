/*
 *  Copyright (C) 2002 Jorn Baayen
 *  Copyright (C) 2003 Marco Pesenti Gritti
 *  Copyright (C) 2003 Christian Persch
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *  $Id$
 */

#ifndef EPHY_SESSION_H
#define EPHY_SESSION_H

#include "ephy-extension.h"

#include "ephy-window.h"

#include <gtk/gtkwindow.h>

G_BEGIN_DECLS

#define EPHY_TYPE_SESSION		(ephy_session_get_type ())
#define EPHY_SESSION(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), EPHY_TYPE_SESSION, EphySession))
#define EPHY_SESSION_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), EPHY_TYPE_SESSION, EphySessionClass))
#define EPHY_IS_SESSION(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), EPHY_TYPE_SESSION))
#define EPHY_IS_SESSION_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), EPHY_TYPE_SESSION))
#define EPHY_SESSION_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), EPHY_TYPE_SESSION, EphySessionClass))

typedef struct _EphySession		EphySession;
typedef struct _EphySessionClass	EphySessionClass;
typedef struct _EphySessionPrivate	EphySessionPrivate;

struct _EphySession
{
        GObject parent;

	/*< private >*/
        EphySessionPrivate *priv;
};

struct _EphySessionClass
{
        GObjectClass parent_class;
};

GType		 ephy_session_get_type		(void);

EphyWindow	*ephy_session_get_active_window	(EphySession *session);

gboolean	 ephy_session_save		(EphySession *session,
						 const char *filename);

gboolean	 ephy_session_load		(EphySession *session,
						 const char *filename);

gboolean	 ephy_session_autoresume	(EphySession *session);

void		 ephy_session_close		(EphySession *session);

GList		*ephy_session_get_windows	(EphySession *session);

void		 ephy_session_add_window	(EphySession *session,
						 GtkWindow *window);

void		ephy_session_remove_window	(EphySession *session,
						 GtkWindow *window);

G_END_DECLS

#endif
