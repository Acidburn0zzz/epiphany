/*
 *  Copyright (C) 2002  Ricardo Fernández Pascual
 *  Copyright (C) 2003  Marco Pesenti Gritti
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

#ifndef EPHY_FAVORITES_MENU_H
#define EPHY_FAVORITES_MENU_H

#include "ephy-window.h"

G_BEGIN_DECLS

#define EPHY_TYPE_FAVORITES_MENU		(ephy_favorites_menu_get_type())
#define EPHY_FAVORITES_MENU(object)		(G_TYPE_CHECK_INSTANCE_CAST((object), EPHY_TYPE_FAVORITES_MENU, EphyFavoritesMenu))
#define EPHY_FAVORITES_MENU_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST((klass), EPHY_TYPE_FAVORITES_MENU, EphyFavoritesMenuClass))
#define EPHY_IS_FAVORITES_MENU(object)		(G_TYPE_CHECK_INSTANCE_TYPE((object), EPHY_TYPE_FAVORITES_MENU))
#define EPHY_IS_FAVORITES_MENU_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass), EPHY_TYPE_FAVORITES_MENU))
#define EPHY_FAVORITES_MENU_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS((obj), EPHY_TYPE_FAVORITES_MENU, EphyFavoritesMenuClass))

typedef struct _EphyFavoritesMenu EphyFavoritesMenu;
typedef struct _EphyFavoritesMenuClass EphyFavoritesMenuClass;
typedef struct _EphyFavoritesMenuPrivate EphyFavoritesMenuPrivate;

struct _EphyFavoritesMenuClass
{
	GObjectClass parent_class;
};

struct _EphyFavoritesMenu
{
	GObject parent_object;

	/*< private >*/
	EphyFavoritesMenuPrivate *priv;
};

GType		   ephy_favorites_menu_get_type		(void);

EphyFavoritesMenu *ephy_favorites_menu_new		(EphyWindow *window);

G_END_DECLS

#endif
