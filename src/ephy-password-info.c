/*
 *  Copyright © 2009 Holger Hans Peter Freyther
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
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */
#include "config.h"
#include "ephy-password-info.h"

#include <gnome-keyring-memory.h>

static EphyPasswordInfo*
password_info_copy (EphyPasswordInfo *info)
{
  EphyPasswordInfo *other = g_slice_new0 (EphyPasswordInfo);

  other->keyring_id = info->keyring_id;
  other->secret = gnome_keyring_memory_strdup (info->secret);
  return other;
}

static void
password_info_free (EphyPasswordInfo *info)
{
  gnome_keyring_memory_free (info->secret);
  g_slice_free (EphyPasswordInfo, info);
}

G_DEFINE_BOXED_TYPE (EphyPasswordInfo, ephy_password_info,
                     password_info_copy, password_info_free)

EphyPasswordInfo
*ephy_password_info_new (guint32 key_id)
{
  EphyPasswordInfo *info = g_slice_new0 (EphyPasswordInfo);

  info->keyring_id = key_id;
  return info;
}
