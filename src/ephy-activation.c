/*
 *  Copyright © 2005 Gustavo Gama
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

#include "ephy-activation.h"

#include "ephy-settings.h"
#include "ephy-shell.h"
#include "ephy-session.h"
#include "ephy-prefs.h"
#include "ephy-debug.h"

static gboolean
session_queue_command (EphySessionCommand command,
		       char *arg,
		       char **args,
		       guint startup_id,
		       GError **error)
{
	EphyShell *shell;
	EphySession *session;

	shell = ephy_shell_get_default ();
	if (shell == NULL)
	{
		g_set_error (error,
			     g_quark_from_static_string ("ephy-activation-error"),
			     0, 
			     "Shutting down." /* FIXME i18n & better string */);
		return FALSE;
	}

	session = EPHY_SESSION (ephy_shell_get_session (ephy_shell_get_default()));
	g_assert (session != NULL);

	ephy_session_queue_command (session, command, arg, args,
				    (guint32) startup_id, FALSE);

	return TRUE;
}

gboolean
ephy_activation_load_uri_list (EphyDbus *ephy_dbus,
			       char **uris,
			       char *options,
			       guint startup_id,
			       GError **error)
{
	char *new_options;
	
	new_options = g_strconcat (options, "external,", NULL);
	return session_queue_command (EPHY_SESSION_CMD_OPEN_URIS,
				      new_options, uris, startup_id, error);
	g_free (new_options);
}

gboolean
ephy_activation_load_session (EphyDbus *ephy_dbus,
			      char *session_name,
			      guint startup_id,
			      GError **error)
{
	return session_queue_command (EPHY_SESSION_CMD_LOAD_SESSION,
				      session_name, NULL, startup_id, error);
}

gboolean
ephy_activation_open_bookmarks_editor (EphyDbus *ephy_dbus,
				       guint startup_id,
				       GError **error)
{
	if (g_settings_get_boolean (EPHY_SETTINGS_LOCKDOWN,
				    EPHY_PREFS_LOCKDOWN_BOOKMARK_EDITING))
	{
		g_set_error (error,
			     g_quark_from_static_string ("ephy-activation-error"),
			     0,
			     "Bookmarks editing is locked down.");

		return FALSE;
	}

	return session_queue_command (EPHY_SESSION_CMD_OPEN_BOOKMARKS_EDITOR,
				      NULL, NULL, startup_id, error);
}
