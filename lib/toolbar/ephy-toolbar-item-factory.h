/*
 *  Copyright (C) 2002  Ricardo Fern�ndez Pascual
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
 */

#ifndef EPHY_TOOLBAR_ITEM_FACTORY_H
#define EPHY_TOOLBAR_ITEM_FACTORY_H

#include "ephy-toolbar-item.h"

G_BEGIN_DECLS

typedef EphyTbItem *(*EphyTbItemConstructor) (void);

EphyTbItem *	ephy_toolbar_item_create_from_string	(const gchar *str);
void            ephy_toolbar_item_register_type         (const gchar *type, EphyTbItemConstructor constructor);

G_END_DECLS

#endif
