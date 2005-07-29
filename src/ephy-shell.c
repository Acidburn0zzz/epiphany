/*
 *  Copyright (C) 2000-2004 Marco Pesenti Gritti
 *  Copyright (C) 2003, 2004 Christian Persch
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

#include "config.h"

#include "ephy-shell.h"
#include "ephy-type-builtins.h"
#include "ephy-embed-shell.h"
#include "ephy-embed-single.h"
#include "eel-gconf-extensions.h"
#include "ephy-prefs.h"
#include "ephy-file-helpers.h"
#include "ephy-favicon-cache.h"
#include "ephy-window.h"
#include "ephy-bookmarks-import.h"
#include "ephy-bookmarks-editor.h"
#include "ephy-history-window.h"
#include "pdm-dialog.h"
#include "prefs-dialog.h"
#include "ephy-debug.h"
#include "ephy-extensions-manager.h"
#include "ephy-session.h"
#include "ephy-lockdown.h"
#include "downloader-view.h"
#include "egg-toolbars-model.h"
#include "ephy-toolbars-model.h"
#include "ephy-toolbar.h"
#include "ephy-automation.h"
#include "print-dialog.h"
#include "ephy-prefs.h"
#include "ephy-gui.h"

#ifdef ENABLE_DBUS
#include "ephy-dbus.h"
#endif

#include <string.h>
#include <bonobo/bonobo-main.h>
#include <glib/gi18n.h>
#include <gtk/gtksignal.h>
#include <gtk/gtkmain.h>
#include <gtk/gtkmessagedialog.h>
#include <gtk/gtknotebook.h>
#include <dirent.h>
#include <unistd.h>
#include <libgnomeui/gnome-client.h>

#define AUTOMATION_IID "OAFIID:GNOME_Epiphany_Automation"

#define EPHY_SHELL_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE ((object), EPHY_TYPE_SHELL, EphyShellPrivate))

struct _EphyShellPrivate
{
	BonoboGenericFactory *automation_factory;
	EphySession *session;
	GObject *lockdown;
	EphyBookmarks *bookmarks;
	EggToolbarsModel *toolbars_model;
	EggToolbarsModel *fs_toolbars_model;
	EphyExtensionsManager *extensions_manager;
	GObject *dbus_service;
	GtkWidget *bme;
	GtkWidget *history_window;
	GObject *pdm_dialog;
	GObject *prefs_dialog;
	GObject *print_setup_dialog;
	GList *del_on_exit;

	gboolean embed_single_connected;
};

EphyShell *ephy_shell = NULL;

static void ephy_shell_class_init	(EphyShellClass *klass);
static void ephy_shell_init		(EphyShell *shell);
static void ephy_shell_finalize		(GObject *object);
static GObject *impl_get_embed_single   (EphyEmbedShell *embed_shell);

static GObjectClass *parent_class = NULL;

GQuark
ephy_shell_error_quark (void)
{
	static GQuark q = 0;

	if (q == 0)
	{
		q = g_quark_from_static_string ("ephy-shell-error-quark");
	}

	return q;
}

GType
ephy_shell_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0))
	{
		static const GTypeInfo our_info =
		{
			sizeof (EphyShellClass),
			NULL, /* base_init */
			NULL, /* base_finalize */
			(GClassInitFunc) ephy_shell_class_init,
			NULL,
			NULL, /* class_data */
			sizeof (EphyShell),
			0, /* n_preallocs */
			(GInstanceInitFunc) ephy_shell_init
		};

		type = g_type_register_static (EPHY_TYPE_EMBED_SHELL,
					       "EphyShell",
					       &our_info, 0);
	}

	return type;
}

static void
ephy_shell_class_init (EphyShellClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	EphyEmbedShellClass *embed_shell_class = EPHY_EMBED_SHELL_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = ephy_shell_finalize;

	embed_shell_class->get_embed_single = impl_get_embed_single;

	g_type_class_add_private (object_class, sizeof(EphyShellPrivate));
}

static EphyEmbed *
ephy_shell_new_window_cb (EphyEmbedSingle *single,
			  EphyEmbed *parent_embed,
			  EphyEmbedChrome chromemask,
			  EphyShell *shell)
{
	GtkWidget *parent = NULL;
	EphyTab *new_tab;
	gboolean is_popup;
	EphyNewTabFlags flags = EPHY_NEW_TAB_DONT_SHOW_WINDOW |
				EPHY_NEW_TAB_APPEND_LAST |
				EPHY_NEW_TAB_IN_NEW_WINDOW |
				EPHY_NEW_TAB_JUMP;

	LOG ("ephy_shell_new_window_cb tab chrome %d", chromemask);

	if (eel_gconf_get_boolean (CONF_LOCKDOWN_DISABLE_JAVASCRIPT_CHROME))
	{
		chromemask = EPHY_EMBED_CHROME_ALL;
	}

	if (parent_embed != NULL)
	{
		/* this will either be a EphyWindow, or the embed itself
		 * (in case it's about to be destroyed, which means it's already
		 * removed from its tab)
		 */
		parent = gtk_widget_get_toplevel (GTK_WIDGET (parent_embed));
	}

	/* what's a popup ? ATM, any window opened with menubar toggled on 
   	 * is *not* a popup 
	 */
	is_popup = (chromemask & EPHY_EMBED_CHROME_MENUBAR) == 0;

	new_tab = ephy_shell_new_tab_full
		(shell,
		 EPHY_IS_WINDOW (parent) ? EPHY_WINDOW (parent) : NULL,
		 NULL, NULL, flags, chromemask, is_popup, 0);

	return ephy_tab_get_embed (new_tab);
}


static gboolean
ephy_shell_add_sidebar_cb (EphyEmbedSingle *embed_single,
			   const char *url,
			   const char *title,
			   EphyShell *shell)
{
	EphySession *session;
	EphyWindow *window;
	GtkWidget *dialog;

	session = EPHY_SESSION (ephy_shell_get_session (shell));
	g_return_val_if_fail (EPHY_IS_SESSION (session), FALSE);

	window = ephy_session_get_active_window (session);

	dialog = gtk_message_dialog_new (GTK_WINDOW (window),
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_ERROR,
					 GTK_BUTTONS_OK,
					 _("Sidebar extension required"));

	gtk_window_set_title (GTK_WINDOW (dialog), _("Sidebar Extension Required"));
	gtk_window_set_icon_name (GTK_WINDOW (dialog), "web-browser");

	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
		  _("The link you clicked needs the sidebar extension to be "
		    "installed."));

	g_signal_connect_swapped (dialog, "response", 
				  G_CALLBACK (gtk_widget_destroy), dialog);

	gtk_widget_show (dialog);

	return TRUE;
}

static GObject*
impl_get_embed_single (EphyEmbedShell *embed_shell)
{
	EphyShell *shell = EPHY_SHELL (embed_shell);
	GObject *embed_single;

	embed_single = EPHY_EMBED_SHELL_CLASS (parent_class)->get_embed_single (embed_shell);

	if (embed_single != NULL && shell->priv->embed_single_connected == FALSE)
	{
		g_signal_connect_object (embed_single, "new-window",
					 G_CALLBACK (ephy_shell_new_window_cb),
					 shell, G_CONNECT_AFTER);

		g_signal_connect_object (embed_single, "add-sidebar",
					 G_CALLBACK (ephy_shell_add_sidebar_cb),
					 shell, G_CONNECT_AFTER);

		shell->priv->embed_single_connected = TRUE;
	}
	
	return embed_single;
}

static BonoboObject *
ephy_automation_factory_cb (BonoboGenericFactory *this_factory,
			    const char *iid,
			    EphyShell *shell)
{
	if (strcmp (iid, AUTOMATION_IID) == 0)
	{
		return BONOBO_OBJECT (g_object_new (EPHY_TYPE_AUTOMATION, NULL));
	}

	g_assert_not_reached ();

	return NULL;
}

static BonoboGenericFactory *
ephy_automation_factory_new (EphyShell *shell)
{
	BonoboGenericFactory *factory;
	GClosure *factory_closure;

	factory = g_object_new (bonobo_generic_factory_get_type (), NULL);

	factory_closure = g_cclosure_new
		(G_CALLBACK (ephy_automation_factory_cb), shell, NULL);

	bonobo_generic_factory_construct_noreg
		(factory, AUTOMATION_FACTORY_IID, factory_closure);

	return factory;
}

static void
ephy_shell_init (EphyShell *shell)
{
	EphyShell **ptr = &ephy_shell;

	shell->priv = EPHY_SHELL_GET_PRIVATE (shell);

	/* globally accessible singleton */
	g_assert (ephy_shell == NULL);
	ephy_shell = shell;
	g_object_add_weak_pointer (G_OBJECT(ephy_shell),
				   (gpointer *)ptr);

	/* Instantiate the automation factory */
	shell->priv->automation_factory = ephy_automation_factory_new (shell);
}

static char *
path_from_command_line_arg (const char *arg)
{
	char path[PATH_MAX];

	if (realpath (arg, path) != NULL)
	{
		return g_strdup (path);
	}
	else
	{
		return g_strdup (arg);
	}
}

static void
open_urls (GNOME_EphyAutomation automation,
	   guint32 user_time,
	   const char **args,
	   CORBA_Environment *ev,
	   gboolean new_tab,
	   gboolean existing_window,
	   gboolean fullscreen)
{
	int i;

	if (args == NULL)
	{
		/* Homepage or resume */
		GNOME_EphyAutomation_loadUrlWithStartupId
			(automation, "", fullscreen,
			 existing_window, new_tab, user_time, ev);
	}
	else
	{
		for (i = 0; args[i] != NULL; i++)
		{
			char *path;

			path = path_from_command_line_arg (args[i]);

			GNOME_EphyAutomation_loadUrlWithStartupId
				(automation, path, fullscreen,
				 existing_window, new_tab, user_time, ev);

			g_free (path);
		}
	}
}

static gboolean
save_yourself_cb (GnomeClient *client,
		  gint phase,
		  GnomeSaveStyle save_style,
		  gboolean shutdown,
		  GnomeInteractStyle interact_style,
		  gboolean fast,
		  EphyShell *shell)
{
	char *argv[] = { NULL, "--load-session", NULL };
	char *discard_argv[] = { "rm", "-f", NULL };
	EphySession *session;
	char *tmp, *save_to;

	LOG ("save_yourself_cb");

	tmp = g_build_filename (ephy_dot_dir (),
				"session_gnome-XXXXXX",
				NULL);
	save_to = ephy_file_tmp_filename (tmp, "xml");
	g_free (tmp);

	session = EPHY_SESSION (ephy_shell_get_session (shell));

	argv[0] = g_get_prgname ();
	argv[2] = save_to;
	gnome_client_set_restart_command
		(client, 3, argv);

	discard_argv[2] = save_to;
	gnome_client_set_discard_command (client, 3,
					  discard_argv);

	ephy_session_save (session, save_to);

	g_free (save_to);

	return TRUE;
}

static void
die_cb (GnomeClient* client,
	EphyShell *shell)
{
	EphySession *session;

	LOG ("die_cb");

	session = EPHY_SESSION (ephy_shell_get_session (shell));
	ephy_session_close (session);
}

static void
gnome_session_init (EphyShell *shell)
{
	GnomeClient *client;

	client = gnome_master_client ();

	g_signal_connect (G_OBJECT (client),
			  "save_yourself",
			  G_CALLBACK (save_yourself_cb),
			  shell);
	g_signal_connect (G_OBJECT (client),
			  "die",
			  G_CALLBACK (die_cb),
			  shell);
}

gboolean
ephy_shell_startup (EphyShell *shell,
		    EphyShellStartupFlags flags,
		    guint32 user_time,
		    const char **args,
		    const char *string_arg,
		    GError **error)
{
	CORBA_Environment ev;
	GNOME_EphyAutomation automation;
	Bonobo_RegistrationResult result;

	ephy_ensure_dir_exists (ephy_dot_dir ());

	CORBA_exception_init (&ev);

	result = bonobo_activation_register_active_server
		(AUTOMATION_FACTORY_IID, BONOBO_OBJREF (shell->priv->automation_factory), NULL);

	switch (result)
	{
		case Bonobo_ACTIVATION_REG_SUCCESS:
			break;
		case Bonobo_ACTIVATION_REG_ALREADY_ACTIVE:
			break;
		case Bonobo_ACTIVATION_REG_NOT_LISTED:
			g_set_error (error, EPHY_SHELL_ERROR,
				     EPHY_SHELL_ERROR_MISSING_SERVER,
				     _("Bonobo couldn't locate the GNOME_Epiphany_Automation.server "
				       "file. You can use bonobo-activation-sysconf to configure "
				       "the search path for bonobo server files."));
			break;
		case Bonobo_ACTIVATION_REG_ERROR:
			g_set_error (error, EPHY_SHELL_ERROR,
				     EPHY_SHELL_ERROR_FACTORY_REG_FAILED,
				     _("Epiphany can't be used now, due to an unexpected error "
				       "from Bonobo when attempting to register the automation "
				       "server"));
			break;
		default:
			g_assert_not_reached ();
	}

	if (result == Bonobo_ACTIVATION_REG_SUCCESS ||
		 result == Bonobo_ACTIVATION_REG_ALREADY_ACTIVE)
	{
		automation = bonobo_activation_activate_from_id (AUTOMATION_IID,
								 0, NULL, &ev);
		if (CORBA_Object_is_nil (automation, &ev))
		{
			g_set_error (error, EPHY_SHELL_ERROR,
				     EPHY_SHELL_ERROR_OBJECT_REG_FAILED,
				     _("Epiphany can't be used now, due to an unexpected error "
				       "from Bonobo when attempting to locate the automation "
				       "object."));
			automation = NULL;
			goto done;
		}

		/* init the session manager up here so we can quit while the resume dialogue is on */
		gnome_session_init (shell);

		if (flags & EPHY_SHELL_STARTUP_BOOKMARKS_EDITOR)
		{
			GNOME_EphyAutomation_openBookmarksEditorWithStartupId
				(automation, user_time, &ev);
		}
		else if (flags & EPHY_SHELL_STARTUP_SESSION)
		{
			GNOME_EphyAutomation_loadSessionWithStartupId
				(automation, string_arg, user_time, &ev);
		}
		else if (flags & EPHY_SHELL_STARTUP_IMPORT_BOOKMARKS)
		{
			GNOME_EphyAutomation_importBookmarks
				(automation, string_arg, &ev);
		}
		else if (flags & EPHY_SHELL_STARTUP_ADD_BOOKMARK)
		{
			GNOME_EphyAutomation_addBookmark
				(automation, string_arg, &ev);
		}
		else
		{
			open_urls (automation, user_time, args, &ev,
				   flags & EPHY_SHELL_STARTUP_TABS,
				   flags & EPHY_SHELL_STARTUP_EXISTING_WINDOW,
				   flags & EPHY_SHELL_STARTUP_FULLSCREEN);
		}

		bonobo_object_release_unref (automation, &ev);
	}

done:
	CORBA_exception_free (&ev);
	gdk_notify_startup_complete ();

	return !(result == Bonobo_ACTIVATION_REG_ALREADY_ACTIVE);
}

static void
ephy_shell_finalize (GObject *object)
{
	EphyShell *shell = EPHY_SHELL (object);

	g_assert (ephy_shell == NULL);

	/* this will unload the extensions */
	LOG ("Unref extension manager");
	if (shell->priv->extensions_manager)
	{
		g_object_unref (shell->priv->extensions_manager);
	}

#ifdef ENABLE_DBUS
	LOG ("Shutting down DBUS service");
	if (shell->priv->dbus_service)
	{
		g_object_unref (shell->priv->dbus_service);
	}
#endif

	LOG ("Unref session manager");
	if (shell->priv->session)
	{
		g_object_unref (shell->priv->session);
	}

	LOG ("Unref lockdown controller");
	if (shell->priv->lockdown)
	{
		g_object_unref (shell->priv->lockdown);
	}

	LOG ("Unref toolbars model");
	if (shell->priv->toolbars_model)
	{
		g_object_unref (shell->priv->toolbars_model);
	}

	LOG ("Unref fullscreen toolbars model");
	if (shell->priv->fs_toolbars_model)
	{
		g_object_unref (shell->priv->fs_toolbars_model);
	}

	LOG ("Unref Bookmarks Editor");
	if (shell->priv->bme)
	{
		gtk_widget_destroy (GTK_WIDGET (shell->priv->bme));
	}

	LOG ("Unref History Window");
	if (shell->priv->history_window)
	{
		gtk_widget_destroy (GTK_WIDGET (shell->priv->history_window));
	}

	LOG ("Unref PDM Dialog");
	if (shell->priv->pdm_dialog)
	{
		g_object_unref (shell->priv->pdm_dialog);
	}

	LOG ("Unref prefs dialog");
	if (shell->priv->prefs_dialog)
	{
		g_object_unref (shell->priv->prefs_dialog);
	}

	LOG ("Unref print setup dialog");
	if (shell->priv->print_setup_dialog)
	{
		g_object_unref (shell->priv->print_setup_dialog);
	}

	LOG ("Unref bookmarks");
	if (shell->priv->bookmarks)
	{
		g_object_unref (shell->priv->bookmarks);
	}

	G_OBJECT_CLASS (parent_class)->finalize (object);

	if (shell->priv->automation_factory)
	{
		bonobo_activation_unregister_active_server
			(AUTOMATION_FACTORY_IID, BONOBO_OBJREF (shell->priv->automation_factory));

		bonobo_object_unref (shell->priv->automation_factory);
	}

	LOG ("Ephy shell finalized");
}


/**
 * ephy_shell_get_default:
 *
 * Retrieve the default #EphyShell object
 *
 * ReturnValue: the default #EphyShell
 **/
EphyShell *
ephy_shell_get_default (void)
{
   return ephy_shell;
}

EphyShell *
ephy_shell_new (void)
{
	return EPHY_SHELL (g_object_new (EPHY_TYPE_SHELL, NULL));
}

static gboolean
url_is_empty (const char *location)
{
	gboolean is_empty = FALSE;

        if (location == NULL || location[0] == '\0' ||
            strcmp (location, "about:blank") == 0)
        {
                is_empty = TRUE;
        }

        return is_empty;
}

static gboolean
load_homepage (EphyEmbed *embed)
{
	char *home;
	gboolean is_empty;

	home = eel_gconf_get_string(CONF_GENERAL_HOMEPAGE);

	if (home == NULL || home[0] == '\0')
	{
		g_free (home);

		home = g_strdup ("about:blank");
	}

	is_empty = url_is_empty (home);

	ephy_embed_load_url (embed, home);

	g_free (home);

	return is_empty;
}

/**
 * ephy_shell_new_tab_full:
 * @shell: a #EphyShell
 * @parent_window: the target #EphyWindow or %NULL
 * @previous_tab: the referrer tab or %NULL
 * @url: an url to load or %NULL
 * @chrome: a #EphyEmbedChrome mask to use if creating a new window
 * @is_popup: whether the new window is a popup
 * @user_time: a timestamp, or 0
 *
 * Create a new tab and the parent window when necessary.
 * Use this function to open urls in new window/tabs.
 *
 * ReturnValue: the created #EphyTab
 **/
EphyTab *
ephy_shell_new_tab_full (EphyShell *shell,
			 EphyWindow *parent_window,
			 EphyTab *previous_tab,
			 const char *url,
			 EphyNewTabFlags flags,
			 EphyEmbedChrome chrome,
			 gboolean is_popup,
			 guint32 user_time)
{
	EphyWindow *window;
	EphyTab *tab;
	EphyEmbed *embed;
	gboolean in_new_window = TRUE;
	gboolean jump_to;
	EphyEmbed *previous_embed = NULL;
	GtkWidget *nb;
	int position = -1;
	gboolean is_empty = FALSE;
	EphyToolbar *toolbar;

	if (flags & EPHY_NEW_TAB_IN_NEW_WINDOW) in_new_window = TRUE;
	if (flags & EPHY_NEW_TAB_IN_EXISTING_WINDOW) in_new_window = FALSE;

	in_new_window = in_new_window && !eel_gconf_get_boolean (CONF_LOCKDOWN_FULLSCREEN);
	g_return_val_if_fail (in_new_window || !is_popup, NULL);

	jump_to = (flags & EPHY_NEW_TAB_JUMP) != 0;

	LOG ("Opening new tab parent-window %p parent-tab %p in-new-window:%s jump-to:%s",
	     parent_window, previous_tab, in_new_window ? "t" : "f", jump_to ? "t" : "f");

	if (!in_new_window && parent_window != NULL)
	{
		window = parent_window;
	}
	else
	{
		window = ephy_window_new_with_chrome (chrome, is_popup);
	}

	toolbar = EPHY_TOOLBAR (ephy_window_get_toolbar (window));

	if (previous_tab != NULL)
	{
		previous_embed = ephy_tab_get_embed (previous_tab);
	}

	if ((flags & EPHY_NEW_TAB_APPEND_AFTER) && previous_tab != NULL)
	{
		nb = ephy_window_get_notebook (window);
		position = gtk_notebook_page_num (GTK_NOTEBOOK (nb),
						  GTK_WIDGET (previous_tab)) + 1;
	}

	tab = ephy_tab_new ();
	gtk_widget_show (GTK_WIDGET (tab));
	embed = ephy_tab_get_embed (tab);

	ephy_window_add_tab (window, tab, position, jump_to);

	ephy_gui_window_update_user_time (GTK_WIDGET (window), user_time);

	if ((flags & EPHY_NEW_TAB_DONT_SHOW_WINDOW) == 0)
	{
		gtk_widget_show (GTK_WIDGET (window));
	}

	if (flags & EPHY_NEW_TAB_FULLSCREEN_MODE)
	{
		gtk_window_fullscreen (GTK_WINDOW (window));
	}

	if (flags & EPHY_NEW_TAB_HOME_PAGE ||
	    flags & EPHY_NEW_TAB_NEW_PAGE)
	{
		ephy_toolbar_activate_location (toolbar);
		is_empty = load_homepage (embed);
	}
	else if (flags & EPHY_NEW_TAB_OPEN_PAGE)
	{
		g_assert (url != NULL);
		ephy_embed_load_url (embed, url);
		is_empty = url_is_empty (url);
	}

        /* Make sure the initial focus is somewhere sensible and not, for
         * example, on the reload button.
         */
        if (in_new_window || jump_to)
        {
                /* If the location entry is blank, focus that, except if the
                 * page was a copy */
                if (is_empty)
                {
                        /* empty page, focus location entry */
			toolbar = EPHY_TOOLBAR (ephy_window_get_toolbar (window));
			ephy_toolbar_activate_location (toolbar);
                }
                else if (embed != NULL) 
                {
			/* non-empty page, focus the page. but make sure the widget is realised first! */
			gtk_widget_realize (GTK_WIDGET (embed));
			ephy_embed_activate (embed);
                }
        }

	return tab;
}

/**
 * ephy_shell_new_tab:
 * @shell: a #EphyShell
 * @parent_window: the target #EphyWindow or %NULL
 * @previous_tab: the referrer tab or %NULL
 * @url: an url to load or %NULL
 *
 * Create a new tab and the parent window when necessary.
 * Use this function to open urls in new window/tabs.
 *
 * ReturnValue: the created #EphyTab
 **/
EphyTab *
ephy_shell_new_tab (EphyShell *shell,
		    EphyWindow *parent_window,
		    EphyTab *previous_tab,
		    const char *url,
		    EphyNewTabFlags flags)
{
	return ephy_shell_new_tab_full (shell, parent_window,
					previous_tab, url, flags,
					EPHY_EMBED_CHROME_ALL, FALSE, 0);
}

/**
 * ephy_shell_get_session:
 * @shell: the #EphyShell
 *
 * Returns current session.
 *
 * Return value: the current session.
 **/
GObject *
ephy_shell_get_session (EphyShell *shell)
{
	g_return_val_if_fail (EPHY_IS_SHELL (shell), NULL);

	if (shell->priv->session == NULL)
	{
		EphyExtensionsManager *manager;

		shell->priv->session = g_object_new (EPHY_TYPE_SESSION, NULL);

		manager = EPHY_EXTENSIONS_MANAGER
			(ephy_shell_get_extensions_manager (shell));
		ephy_extensions_manager_register (manager,
						  G_OBJECT (shell->priv->session));
	}

	return G_OBJECT (shell->priv->session);
}

/**
 * ephy_shell_get_lockdown:
 * @shell: the #EphyShell
 *
 * Returns the lockdown controller.
 *
 * Return value: the lockdown controller
 **/
static GObject *
ephy_shell_get_lockdown (EphyShell *shell)
{
	g_return_val_if_fail (EPHY_IS_SHELL (shell), NULL);

	if (shell->priv->lockdown == NULL)
	{
		EphyExtensionsManager *manager;

		shell->priv->lockdown = g_object_new (EPHY_TYPE_LOCKDOWN, NULL);

		manager = EPHY_EXTENSIONS_MANAGER
			(ephy_shell_get_extensions_manager (shell));
		ephy_extensions_manager_register (manager,
						  G_OBJECT (shell->priv->lockdown));
	}

	return G_OBJECT (shell->priv->session);
}

EphyBookmarks *
ephy_shell_get_bookmarks (EphyShell *shell)
{
	if (shell->priv->bookmarks == NULL)
	{
		shell->priv->bookmarks = ephy_bookmarks_new ();
	}

	return shell->priv->bookmarks;
}

GObject *
ephy_shell_get_toolbars_model (EphyShell *shell, gboolean fullscreen)
{
	LOG ("ephy_shell_get_toolbars_model fs=%d", fullscreen);

	if (fullscreen)
	{
		if (shell->priv->fs_toolbars_model == NULL)
		{
			gboolean success;
			const char *xml;

			shell->priv->fs_toolbars_model = egg_toolbars_model_new ();
			xml = ephy_file ("epiphany-fs-toolbar.xml");
			g_return_val_if_fail (xml != NULL, NULL);

			success = egg_toolbars_model_load
				(shell->priv->fs_toolbars_model, xml);
			g_return_val_if_fail (success, NULL);
		}

		return G_OBJECT (shell->priv->fs_toolbars_model);
	}
	else
	{
		if (shell->priv->toolbars_model == NULL)
		{
			EphyBookmarks *bookmarks;
			GObject *bookmarksbar_model;

			shell->priv->toolbars_model = ephy_toolbars_model_new ();

			/* get the bookmarks toolbars model. we have to do this
			 * before loading the toolbars model from disk, since
			 * this will connect the get_item_* signals
			 */
			bookmarks = ephy_shell_get_bookmarks (shell);
			bookmarksbar_model = ephy_bookmarks_get_toolbars_model (bookmarks);

			/* ok, now we can load the model */
			ephy_toolbars_model_load
				(EPHY_TOOLBARS_MODEL (shell->priv->toolbars_model));
		}

		return G_OBJECT (shell->priv->toolbars_model);
	}
}

GObject *
ephy_shell_get_extensions_manager (EphyShell *es)
{
	g_return_val_if_fail (EPHY_IS_SHELL (es), NULL);

	if (es->priv->extensions_manager == NULL)
	{
		/* Instantiate extensions manager */
		es->priv->extensions_manager =
			g_object_new (EPHY_TYPE_EXTENSIONS_MANAGER, NULL);

		ephy_extensions_manager_startup (es->priv->extensions_manager);

		/* FIXME */
		ephy_shell_get_lockdown (es);
	}

	return G_OBJECT (es->priv->extensions_manager);
}

static void
toolwindow_show_cb (GtkWidget *widget, EphyShell *es)
{
	EphySession *session;

	LOG ("Ref shell for %s", G_OBJECT_TYPE_NAME (widget));

	session = EPHY_SESSION (ephy_shell_get_session (es));
	ephy_session_add_window (ephy_shell->priv->session, GTK_WINDOW (widget));
	g_object_ref (ephy_shell);
}

static void
toolwindow_hide_cb (GtkWidget *widget, EphyShell *es)
{
	EphySession *session;

	LOG ("Unref shell for %s", G_OBJECT_TYPE_NAME (widget));

	session = EPHY_SESSION (ephy_shell_get_session (es));
	ephy_session_remove_window (ephy_shell->priv->session, GTK_WINDOW (widget));
	g_object_unref (ephy_shell);
}

GtkWidget *
ephy_shell_get_bookmarks_editor (EphyShell *shell)
{
	EphyBookmarks *bookmarks;

	if (shell->priv->bme == NULL)
	{
		bookmarks = ephy_shell_get_bookmarks (ephy_shell);
		g_assert (bookmarks != NULL);
		shell->priv->bme = ephy_bookmarks_editor_new (bookmarks);

		g_signal_connect (shell->priv->bme, "show", 
				  G_CALLBACK (toolwindow_show_cb), shell);
		g_signal_connect (shell->priv->bme, "hide", 
				  G_CALLBACK (toolwindow_hide_cb), shell);
	}

	return shell->priv->bme;
}

GtkWidget *
ephy_shell_get_history_window (EphyShell *shell)
{
	EphyHistory *history;

	if (shell->priv->history_window == NULL)
	{
		history = EPHY_HISTORY
			(ephy_embed_shell_get_global_history (embed_shell));
		g_assert (history != NULL);
		shell->priv->history_window = ephy_history_window_new (history);

		g_signal_connect (shell->priv->history_window, "show",
				  G_CALLBACK (toolwindow_show_cb), shell);
		g_signal_connect (shell->priv->history_window, "hide",
				  G_CALLBACK (toolwindow_hide_cb), shell);
	}

	return shell->priv->history_window;
}

GObject *
ephy_shell_get_pdm_dialog (EphyShell *shell)
{
	if (shell->priv->pdm_dialog == NULL)
	{
		shell->priv->pdm_dialog = g_object_new (EPHY_TYPE_PDM_DIALOG, NULL);

		g_object_add_weak_pointer (shell->priv->pdm_dialog,
					   (gpointer *) &shell->priv->pdm_dialog);
	}

	return shell->priv->pdm_dialog;
}

GObject *
ephy_shell_get_prefs_dialog (EphyShell *shell)
{
	if (shell->priv->prefs_dialog == NULL)
	{
		shell->priv->prefs_dialog = g_object_new (EPHY_TYPE_PREFS_DIALOG, NULL);

		g_object_add_weak_pointer (shell->priv->prefs_dialog,
					   (gpointer *) &shell->priv->prefs_dialog);
	}

	return shell->priv->prefs_dialog;
}

GObject *
ephy_shell_get_print_setup_dialog (EphyShell *shell)
{
	if (shell->priv->print_setup_dialog == NULL)
	{
		shell->priv->print_setup_dialog = G_OBJECT (ephy_print_setup_dialog_new ());

		g_object_add_weak_pointer (shell->priv->print_setup_dialog,
					   (gpointer *) &shell->priv->print_setup_dialog);
	}

	return shell->priv->print_setup_dialog;
}

#ifdef ENABLE_DBUS
GObject	*
ephy_shell_get_dbus_service (EphyShell *shell)
{
	g_return_val_if_fail (EPHY_IS_SHELL (shell), NULL);

	if (shell->priv->dbus_service == NULL)
	{
		shell->priv->dbus_service = g_object_new (EPHY_TYPE_DBUS, NULL);
		ephy_dbus_startup (EPHY_DBUS (shell->priv->dbus_service));
	}

	return G_OBJECT (shell->priv->dbus_service);
}
#endif
