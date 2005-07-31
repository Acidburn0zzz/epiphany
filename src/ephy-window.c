/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 2000, 2001, 2002, 2003, 2004 Marco Pesenti Gritti
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

#include "ephy-window.h"
#include "ephy-type-builtins.h"
#include "ephy-embed-type-builtins.h"
#include "ephy-command-manager.h"
#include "ephy-bookmarks-menu.h"
#include "ephy-favorites-menu.h"
#include "ephy-state.h"
#include "ppview-toolbar.h"
#include "window-commands.h"
#include "ephy-embed-shell.h"
#include "ephy-embed-single.h"
#include "ephy-shell.h"
#include "eel-gconf-extensions.h"
#include "ephy-prefs.h"
#include "ephy-embed-prefs.h"
#include "ephy-zoom.h"
#include "ephy-debug.h"
#include "ephy-file-helpers.h"
#include "ephy-statusbar.h"
#include "egg-editable-toolbar.h"
#include "ephy-toolbar.h"
#include "ephy-bookmarksbar.h"
#include "popup-commands.h"
#include "ephy-encoding-menu.h"
#include "ephy-tabs-menu.h"
#include "ephy-stock-icons.h"
#include "ephy-extension.h"
#include "ephy-favicon-cache.h"
#include "ephy-link.h"
#include "ephy-gui.h"
#include "ephy-notebook.h"
#include "ephy-fullscreen-popup.h"
#include "ephy-action-helper.h"
#include "ephy-find-toolbar.h"

#include <string.h>
#include <glib/gi18n.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <libgnomeui/gnome-stock-icons.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtkactiongroup.h>
#include <gtk/gtktoggleaction.h>
#include <gtk/gtkuimanager.h>
#include <gtk/gtktoggleaction.h>
#include <gtk/gtkdialog.h>
#include <gtk/gtkmessagedialog.h>

#ifdef HAVE_X11_XF86KEYSYM_H
#include <X11/XF86keysym.h>
#endif

static void ephy_window_class_init		(EphyWindowClass *klass);
static void ephy_window_link_iface_init		(EphyLinkIface *iface);
static void ephy_window_init			(EphyWindow *gs);
static GObject *ephy_window_constructor		(GType type,
						 guint n_construct_properties,
						 GObjectConstructParam *construct_params);
static void ephy_window_finalize		(GObject *object);
static void ephy_window_show			(GtkWidget *widget);
static EphyTab *ephy_window_open_link		(EphyLink *link,
						 const char *address,
						 EphyTab *tab,
						 EphyLinkFlags flags);
static void ephy_window_notebook_switch_page_cb	(GtkNotebook *notebook,
						 GtkNotebookPage *page,
						 guint page_num,
						 EphyWindow *window);
static void ephy_window_view_statusbar_cb       (GtkAction *action,
			                         EphyWindow *window);
static void ephy_window_view_toolbar_cb         (GtkAction *action,
			                         EphyWindow *window);
static void ephy_window_view_bookmarksbar_cb    (GtkAction *action,
			                         EphyWindow *window);
static void ephy_window_view_popup_windows_cb	(GtkAction *action,
						 EphyWindow *window);
static void sync_tab_load_status		(EphyTab *tab,
						 GParamSpec *pspec,
						 EphyWindow *window);
static void sync_tab_security			(EphyTab *tab,
						 GParamSpec *pspec,
						 EphyWindow *window);
static void sync_tab_zoom			(EphyTab *tab,
						 GParamSpec *pspec,
						 EphyWindow *window);

static const GtkActionEntry ephy_menu_entries [] = {

	/* Toplevel */

	{ "File", NULL, N_("_File") },
	{ "Edit", NULL, N_("_Edit") },
	{ "View", NULL, N_("_View") },
	{ "Bookmarks", NULL, N_("_Bookmarks") },
	{ "Go", NULL, N_("_Go") },
	{ "Tools", NULL, N_("T_ools") },
	{ "Tabs", NULL, N_("_Tabs") },
	{ "Help", NULL, N_("_Help") },
	{ "PopupAction", NULL, "" },
	{ "NotebookPopupAction", NULL, "" },

	/* File menu */

	{ "FileNewWindow", GTK_STOCK_NEW, N_("_New Window"), "<control>N",
	  N_("Open a new window"),
	  G_CALLBACK (window_cmd_file_new_window) },
	{ "FileNewTab", STOCK_NEW_TAB, N_("New _Tab"), "<control>T",
	  N_("Open a new tab"),
	  G_CALLBACK (window_cmd_file_new_tab) },
	{ "FileOpen", GTK_STOCK_OPEN, N_("_Open..."), "<control>O",
	  N_("Open a file"),
	  G_CALLBACK (window_cmd_file_open) },
	{ "FileSaveAs", GTK_STOCK_SAVE_AS, N_("Save _As..."), "<shift><control>S",
	  N_("Save the current page"),
	  G_CALLBACK (window_cmd_file_save_as) },
	{ "FileSave", GTK_STOCK_SAVE_AS, N_("Save _As..."), "<control>S",
	  N_("Save the current page"),
	  G_CALLBACK (window_cmd_file_save_as) },
	{ "FilePrintSetup", STOCK_PRINT_SETUP, N_("Print Set_up..."), NULL,
	  N_("Setup the page settings for printing"),
	  G_CALLBACK (window_cmd_file_print_setup) },
	{ "FilePrintPreview", GTK_STOCK_PRINT_PREVIEW, N_("Print Pre_view"),"<control><shift>P",
	  N_("Print preview"),
	  G_CALLBACK (window_cmd_file_print_preview) },
	{ "FilePrint", GTK_STOCK_PRINT, N_("_Print..."), "<control>P",
	  N_("Print the current page"),
	  G_CALLBACK (window_cmd_file_print) },
	{ "FileSendTo", STOCK_SEND_MAIL, N_("S_end To..."), "<control>M",
	  N_("Send a link of the current page"),
	  G_CALLBACK (window_cmd_file_send_to) },
	{ "FileCloseTab", GTK_STOCK_CLOSE, N_("_Close"), "<control>W",
	  N_("Close this tab"),
	  G_CALLBACK (window_cmd_file_close_window) },

	/* Edit menu */

	{ "EditUndo", GTK_STOCK_UNDO, N_("_Undo"), "<control>Z",
	  N_("Undo the last action"),
	  G_CALLBACK (window_cmd_edit_undo) },
	{ "EditRedo", GTK_STOCK_REDO, N_("Re_do"), "<shift><control>Z",
	  N_("Redo the last undone action"),
	  G_CALLBACK (window_cmd_edit_redo) },
	{ "EditCut", GTK_STOCK_CUT, N_("Cu_t"), "<control>X",
	  N_("Cut the selection"),
	  G_CALLBACK (window_cmd_edit_cut) },
	{ "EditCopy", GTK_STOCK_COPY, N_("_Copy"), "<control>C",
	  N_("Copy the selection"),
	  G_CALLBACK (window_cmd_edit_copy) },
	{ "EditPaste", GTK_STOCK_PASTE, N_("_Paste"), "<control>V",
	  N_("Paste clipboard"),
	  G_CALLBACK (window_cmd_edit_paste) },
	{ "EditSelectAll", NULL, N_("Select _All"), "<control>A",
	  N_("Select the entire page"),
	  G_CALLBACK (window_cmd_edit_select_all) },
	{ "EditFind", GTK_STOCK_FIND, N_("_Find..."), "<control>F",
	  N_("Find a word or phrase in the page"),
	  G_CALLBACK (window_cmd_edit_find) },
	{ "EditFindNext", NULL, N_("Find Ne_xt"), "<control>G",
	  N_("Find next occurrence of the word or phrase"),
	  G_CALLBACK (window_cmd_edit_find_next) },
	{ "EditFindPrev", NULL, N_("Find Pre_vious"), "<shift><control>G",
	  N_("Find previous occurrence of the word or phrase"),
	  G_CALLBACK (window_cmd_edit_find_prev) },
	{ "EditPersonalData", NULL, N_("P_ersonal Data"), NULL,
	  N_("View and remove cookies and passwords"),
	  G_CALLBACK (window_cmd_edit_personal_data) },
	{ "EditToolbar", NULL, N_("T_oolbars"), NULL,
	  N_("Customize toolbars"),
	  G_CALLBACK (window_cmd_edit_toolbar) },
	{ "EditPrefs", GTK_STOCK_PREFERENCES, N_("P_references"), NULL,
	  N_("Configure the web browser"),
	  G_CALLBACK (window_cmd_edit_prefs) },

	/* View menu */

	{ "ViewStop", GTK_STOCK_STOP, N_("_Stop"), "Escape",
	  N_("Stop current data transfer"),
	  G_CALLBACK (window_cmd_view_stop) },
	{ "ViewAlwaysStop", GTK_STOCK_STOP, N_("_Stop"), "Escape",
	  NULL, G_CALLBACK (window_cmd_view_stop) },
	{ "ViewReload", GTK_STOCK_REFRESH, N_("_Reload"), "<control>R",
	  N_("Display the latest content of the current page"),
	  G_CALLBACK (window_cmd_view_reload) },
	{ "ForceReload", NULL, "", "<shift><control>R",
	  NULL,
	  G_CALLBACK (window_cmd_view_reload) },
	{ "ViewZoomIn", GTK_STOCK_ZOOM_IN, N_("Zoom _In"), "<control>plus",
	  N_("Increase the text size"),
	  G_CALLBACK (window_cmd_view_zoom_in) },
	{ "ViewZoomOut", GTK_STOCK_ZOOM_OUT, N_("Zoom _Out"), "<control>minus",
	  N_("Decrease the text size"),
	  G_CALLBACK (window_cmd_view_zoom_out) },
	{ "ViewZoomNormal", GTK_STOCK_ZOOM_100, N_("_Normal Size"), "<control>0",
	  N_("Use the normal text size"),
	  G_CALLBACK (window_cmd_view_zoom_normal) },
	{ "ViewEncoding", NULL, N_("Text _Encoding"), NULL,
	  N_("Change the text encoding"),
	  NULL },
	{ "ViewPageSource", STOCK_VIEW_SOURCE, N_("_Page Source"), "<control>U",
	  N_("View the source code of the page"),
	  G_CALLBACK (window_cmd_view_page_source) },

	/* Bookmarks menu */

	{ "FileBookmarkPage", STOCK_ADD_BOOKMARK, N_("_Add Bookmark..."), "<control>D",
	  N_("Add a bookmark for the current page"),
	  G_CALLBACK (window_cmd_file_bookmark_page) },
	{ "GoBookmarks", EPHY_STOCK_BOOKMARKS, N_("_Edit Bookmarks"), "<control>B",
	  N_("Open the bookmarks window"),
	  G_CALLBACK (window_cmd_go_bookmarks) },

	/* Go menu */

	{ "GoBack", GTK_STOCK_GO_BACK, N_("_Back"), "<alt>Left",
	  N_("Go to the previous visited page"),
	  G_CALLBACK (window_cmd_go_back) },
	{ "GoForward", GTK_STOCK_GO_FORWARD, N_("_Forward"), "<alt>Right",
	  N_("Go to the next visited page"),
	  G_CALLBACK (window_cmd_go_forward) },
	{ "GoUp", GTK_STOCK_GO_UP, N_("_Up"), "<alt>Up",
	  N_("Go up one level"),
	  G_CALLBACK (window_cmd_go_up) },
	{ "GoLocation", NULL, N_("_Location..."), "<control>L",
	  N_("Go to a specified location"),
	  G_CALLBACK (window_cmd_go_location) },
	{ "GoHistory", EPHY_STOCK_HISTORY, N_("H_istory"), "<control>H",
	  N_("Open the history window"),
	  G_CALLBACK (window_cmd_go_history) },

	/* Tabs menu */

	{ "TabsPrevious", NULL, N_("_Previous Tab"), "<control>Page_Up",
	  N_("Activate previous tab"),
	  G_CALLBACK (window_cmd_tabs_previous) },
	{ "TabsNext", NULL, N_("_Next Tab"), "<control>Page_Down",
	  N_("Activate next tab"),
	  G_CALLBACK (window_cmd_tabs_next) },
	{ "TabsMoveLeft", NULL, N_("Move Tab _Left"), "<shift><control>Page_Up",
	  N_("Move current tab to left"),
	  G_CALLBACK (window_cmd_tabs_move_left) },
	{ "TabsMoveRight", NULL, N_("Move Tab _Right"), "<shift><control>Page_Down",
	  N_("Move current tab to right"),
	  G_CALLBACK (window_cmd_tabs_move_right) },
	{ "TabsDetach", NULL, N_("_Detach Tab"), "<shift><control>M",
	  N_("Detach current tab"),
	  G_CALLBACK (window_cmd_tabs_detach) },

	/* Help menu */

	{"HelpContents", GTK_STOCK_HELP, N_("_Contents"), "F1",
	 N_("Display web browser help"),
	 G_CALLBACK (window_cmd_help_contents) },
	{ "HelpAbout", GNOME_STOCK_ABOUT, N_("_About"), NULL,
	  N_("Display credits for the web browser creators"),
	  G_CALLBACK (window_cmd_help_about) },
};

static const GtkToggleActionEntry ephy_menu_toggle_entries [] =
{
	/* File Menu */

	{ "FileWorkOffline", NULL, N_("_Work Offline"), NULL,
	  N_("Switch to offline mode"),
	  G_CALLBACK (window_cmd_file_work_offline), FALSE },

	/* View Menu */

	{ "ViewToolbar", NULL, N_("_Toolbar"), "<shift><control>T",
	  N_("Show or hide toolbar"),
	  G_CALLBACK (ephy_window_view_toolbar_cb), TRUE },
	{ "ViewBookmarksBar", NULL, N_("_Bookmarks Bar"), NULL,
	  N_("Show or hide bookmarks bar"),
	  G_CALLBACK (ephy_window_view_bookmarksbar_cb), TRUE },
	{ "ViewStatusbar", NULL, N_("St_atusbar"), NULL,
	  N_("Show or hide statusbar"),
	  G_CALLBACK (ephy_window_view_statusbar_cb), TRUE },
	{ "ViewFullscreen", STOCK_FULLSCREEN, N_("_Fullscreen"), "F11",
	  N_("Browse at full screen"),
	  G_CALLBACK (window_cmd_view_fullscreen), FALSE },
	{ "ViewPopupWindows", EPHY_STOCK_POPUPS, N_("Popup _Windows"), NULL,
	  N_("Show or hide unrequested popup windows from this site"),
	  G_CALLBACK (ephy_window_view_popup_windows_cb), FALSE },
	{ "BrowseWithCaret", NULL, N_("Selection Caret"), "F7",
	  "",
	  G_CALLBACK (window_cmd_browse_with_caret), FALSE }
};

static const GtkActionEntry ephy_popups_entries [] = {
	/* Document */

	{ "SaveBackgroundAs", NULL, N_("_Save Background As..."), NULL,
	  NULL, G_CALLBACK (popup_cmd_save_background_as) },
	{ "ContextBookmarkPage", STOCK_ADD_BOOKMARK, N_("Add Boo_kmark..."), "<control>D",
	  N_("Add a bookmark for the current page"),
	  G_CALLBACK (window_cmd_file_bookmark_page) },

	/* Framed document */

	{ "OpenFrame", NULL, N_("Show Only This _Frame"), NULL,
	  N_("Show only this frame in this window"),
	  G_CALLBACK (popup_cmd_open_frame) },

	/* Links */

	{ "OpenLink", GTK_STOCK_OPEN, N_("_Open Link"), NULL,
	  N_("Open link in this window"),
	  G_CALLBACK (popup_cmd_open_link) },
	{ "OpenLinkInNewWindow", NULL, N_("Open Link in New _Window"), NULL,
	  N_("Open link in a new window"),
	  G_CALLBACK (popup_cmd_link_in_new_window) },
	{ "OpenLinkInNewTab", NULL, N_("Open Link in New _Tab"), NULL,
	  N_("Open link in a new tab"),
	  G_CALLBACK (popup_cmd_link_in_new_tab) },
	{ "DownloadLink", EPHY_STOCK_DOWNLOAD, N_("_Download Link"), NULL,
	  NULL, G_CALLBACK (popup_cmd_download_link) },
	{ "DownloadLinkAs", GTK_STOCK_SAVE_AS, N_("_Save Link As..."), NULL,
	  N_("Save link with a different name"),
	  G_CALLBACK (popup_cmd_download_link_as) },
	{ "BookmarkLink", STOCK_ADD_BOOKMARK, N_("_Bookmark Link..."),
	  NULL, NULL, G_CALLBACK (popup_cmd_bookmark_link) },
	{ "CopyLinkAddress", NULL, N_("_Copy Link Address"), NULL,
	  NULL, G_CALLBACK (popup_cmd_copy_link_address) },

	/* Email links */

	/* This is on the context menu on a mailto: link and opens the mail program */
	{ "SendEmail", GTK_STOCK_OPEN, N_("_Send Email..."),
	  NULL, NULL, G_CALLBACK (popup_cmd_open_link) },
	{ "CopyEmailAddress", NULL, N_("_Copy Email Address"), NULL,
	  NULL, G_CALLBACK (popup_cmd_copy_link_address) },

	/* Images */

	{ "OpenImage", GTK_STOCK_OPEN, N_("Open _Image"), NULL,
	  NULL, G_CALLBACK (popup_cmd_open_image) },
	{ "SaveImageAs", GTK_STOCK_SAVE_AS, N_("_Save Image As..."), NULL,
	  NULL, G_CALLBACK (popup_cmd_save_image_as) },
	{ "SetImageAsBackground", NULL, N_("_Use Image As Background"), NULL,
	  NULL, G_CALLBACK (popup_cmd_set_image_as_background) },
	{ "CopyImageLocation", NULL, N_("Copy I_mage Address"), NULL,
	  NULL, G_CALLBACK (popup_cmd_copy_image_location) },
};

#ifdef HAVE_X11_XF86KEYSYM_H
static const struct
{
	const char *name;
	guint keyval;
	GCallback callback;
} xf_key_actions [] = {
	{ "XFKeyHomePage", 	XF86XK_HomePage,	G_CALLBACK (window_cmd_go_home ) },
	{ "XFKeyBack",		XF86XK_Back,		G_CALLBACK (window_cmd_go_back ) },
	{ "XFKeyForward",	XF86XK_Forward,		G_CALLBACK (window_cmd_go_forward ) },
	{ "XFKeyStop", 		XF86XK_Stop,		G_CALLBACK (window_cmd_view_stop ) },
	{ "XFKeyRefresh", 	XF86XK_Refresh, 	G_CALLBACK (window_cmd_view_reload ) },
	{ "XFKeyFavorites", 	XF86XK_Favorites,	G_CALLBACK (window_cmd_go_bookmarks ) },
	{ "XFKeyHistory", 	XF86XK_History, 	G_CALLBACK (window_cmd_go_history ) },
	{ "XFKeyOpenURL", 	XF86XK_OpenURL, 	G_CALLBACK (window_cmd_go_location ) },
	{ "XFKeyAddFavorite", 	XF86XK_AddFavorite, 	G_CALLBACK (window_cmd_file_bookmark_page ) },
	{ "XFKeyGo", 		XF86XK_Go,	 	G_CALLBACK (window_cmd_go_location ) },
	{ "XFKeyReload", 	XF86XK_Reload,	 	G_CALLBACK (window_cmd_view_reload ) },
	{ "XFKeySendTo", 	XF86XK_Send,	 	G_CALLBACK (window_cmd_file_send_to) },
	{ "XFKeyZoomIn", 	XF86XK_ZoomIn,	 	G_CALLBACK (window_cmd_view_zoom_in ) },
	{ "XFKeyZoomOut", 	XF86XK_ZoomOut,	 	G_CALLBACK (window_cmd_view_zoom_out) }
	/* FIXME: what about ScrollUp, ScrollDown, Menu*, Option, LogOff, Save,.. any others? */
};
#endif /* HAVE_X11_XF86KEYSYM_H */

#define CONF_LOCKDOWN_HIDE_MENUBAR "/apps/epiphany/lockdown/hide_menubar"
#define CONF_DESKTOP_BG_PICTURE "/desktop/gnome/background/picture_filename"

#define BOOKMARKS_MENU_PATH "/menubar/BookmarksMenu"

/* Until https://bugzilla.mozilla.org/show_bug.cgi?id=296002 is fixed */
#define KEEP_TAB_IN_SAME_TOPLEVEL

#define EPHY_WINDOW_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE ((object), EPHY_TYPE_WINDOW, EphyWindowPrivate))

struct _EphyWindowPrivate
{
	GtkWidget *main_vbox;
	GtkWidget *menu_dock;
	GtkWidget *fullscreen_popup;
	EphyToolbar *toolbar;
	GtkWidget *bookmarksbar;
	GtkWidget *statusbar;
	GtkUIManager *manager;
	GtkActionGroup *action_group;
	GtkActionGroup *popups_action_group;
	EphyFavoritesMenu *fav_menu;
	EphyEncodingMenu *enc_menu;
	EphyTabsMenu *tabs_menu;
	EphyBookmarksMenu *bmk_menu;
	PPViewToolbar *ppview_toolbar;
	GtkNotebook *notebook;
	EphyTab *active_tab;
	EphyFindToolbar *find_toolbar;
	guint num_tabs;
	guint tab_message_cid;
	guint help_message_cid;
	EphyEmbedChrome chrome;
	guint idle_resize_handler;

	guint browse_with_caret_notifier_id;
	guint allow_popups_notifier_id;

	guint closing : 1;
	guint has_size : 1;
	guint fullscreen_mode : 1;
	guint ppv_mode : 1;
	guint should_save_chrome : 1;
	guint is_popup : 1;
};

enum
{
	PROP_0,
	PROP_ACTIVE_TAB,
	PROP_CHROME,
	PROP_PPV_MODE,
	PROP_SINGLE_TAB_MODE
};

/* Make sure not to overlap with those in ephy-lockdown.c */
enum
{
	SENS_FLAG_CHROME	= 1 << 0,
	SENS_FLAG_CONTEXT	= 1 << 1,
	SENS_FLAG_DOCUMENT	= 1 << 2,
	SENS_FLAG_LOADING	= 1 << 3,
	SENS_FLAG_NAVIGATION	= 1 << 4
};

static GObjectClass *parent_class = NULL;

GType
ephy_window_get_type (void)
{
        static GType type = 0;

        if (G_UNLIKELY (type == 0))
        {
                static const GTypeInfo our_info =
                {
                        sizeof (EphyWindowClass),
                        NULL, /* base_init */
                        NULL, /* base_finalize */
                        (GClassInitFunc) ephy_window_class_init,
                        NULL,
                        NULL, /* class_data */
                        sizeof (EphyWindow),
                        0, /* n_preallocs */
                        (GInstanceInitFunc) ephy_window_init
                };
		static const GInterfaceInfo link_info = 
		{
			(GInterfaceInitFunc) ephy_window_link_iface_init,
			NULL,
			NULL
		};

                type = g_type_register_static (GTK_TYPE_WINDOW,
					       "EphyWindow",
					       &our_info, 0);

		g_type_add_interface_static (type,
					     EPHY_TYPE_LINK,
					     &link_info);
        }

        return type;
}

static void
destroy_fullscreen_popup (EphyWindow *window)
{
	if (window->priv->fullscreen_popup != NULL)
	{
		gtk_widget_destroy (window->priv->fullscreen_popup);
		window->priv->fullscreen_popup = NULL;
	}
}

static void
add_widget (GtkUIManager *manager,
	    GtkWidget *widget,
	    EphyWindow *window)
{
	gtk_box_pack_start (GTK_BOX (window->priv->menu_dock),
			    widget, FALSE, FALSE, 0);
}

static void
exit_fullscreen_clicked_cb (EphyWindow *window)
{
	GtkAction *action;

	action = gtk_action_group_get_action (window->priv->action_group, "ViewFullscreen");
	g_return_if_fail (action != NULL);

	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), FALSE);
}

static gboolean
get_toolbar_visibility (EphyWindow *window)
{
	return ((window->priv->chrome & EPHY_EMBED_CHROME_TOOLBAR) != 0) &&
	       !window->priv->ppv_mode;
}			

static void
get_chromes_visibility (EphyWindow *window,
			gboolean *show_menubar,
			gboolean *show_statusbar,
			gboolean *show_toolbar,
			gboolean *show_bookmarksbar,
			gboolean *show_tabsbar)
{
	EphyWindowPrivate *priv = window->priv;
	EphyEmbedChrome flags = priv->chrome;

	if (window->priv->ppv_mode)
	{
		*show_menubar = *show_statusbar
			      = *show_toolbar
			      = *show_bookmarksbar
			      = *show_tabsbar
			      = FALSE;
	}
	else if (window->priv->fullscreen_mode)
	{
		*show_toolbar = (flags & EPHY_EMBED_CHROME_TOOLBAR) != 0;
		*show_menubar = *show_statusbar = *show_bookmarksbar = FALSE;
		*show_tabsbar = !priv->is_popup;
	}
	else
	{
		*show_menubar = (flags & EPHY_EMBED_CHROME_MENUBAR) != 0;
		*show_statusbar = (flags & EPHY_EMBED_CHROME_STATUSBAR) != 0;
		*show_toolbar = (flags & EPHY_EMBED_CHROME_TOOLBAR) != 0;
		*show_bookmarksbar = (flags & EPHY_EMBED_CHROME_BOOKMARKSBAR) != 0;
		*show_tabsbar = !priv->is_popup;
	}
}

static void
sync_chromes_visibility (EphyWindow *window)
{
	EphyWindowPrivate *priv = window->priv;
	GtkWidget *menubar;
	gboolean show_statusbar, show_menubar, show_toolbar, show_bookmarksbar, show_tabsbar;

	if (priv->closing) return;

	get_chromes_visibility (window, &show_menubar,
				&show_statusbar, &show_toolbar,
				&show_bookmarksbar, &show_tabsbar);

	menubar = gtk_ui_manager_get_widget (window->priv->manager, "/menubar");
	g_assert (menubar != NULL);

	g_object_set (menubar, "visible", show_menubar, NULL);
	g_object_set (priv->toolbar, "visible", show_toolbar, NULL);
	g_object_set (priv->bookmarksbar, "visible", show_bookmarksbar, NULL);
	g_object_set (priv->statusbar, "visible", show_statusbar, NULL);

	ephy_toolbar_set_lock_visibility (priv->toolbar, !show_statusbar);

	ephy_notebook_set_show_tabs (EPHY_NOTEBOOK (priv->notebook), show_tabsbar);

	if (priv->fullscreen_popup != NULL)
	{
		g_object_set (priv->fullscreen_popup, "visible", !show_toolbar, NULL);
	}
}

static void
ephy_window_fullscreen (EphyWindow *window)
{
	GtkWidget *popup;
	EphyTab *tab;
	gboolean lockdown_fs;

	window->priv->fullscreen_mode = TRUE;

	lockdown_fs = eel_gconf_get_boolean (CONF_LOCKDOWN_FULLSCREEN);

	popup = ephy_fullscreen_popup_new (window);
	ephy_fullscreen_popup_set_show_leave
		(EPHY_FULLSCREEN_POPUP (popup), !lockdown_fs);
	window->priv->fullscreen_popup = popup;
	g_signal_connect_swapped (popup, "exit-clicked",
				  G_CALLBACK (exit_fullscreen_clicked_cb), window);

	/* sync status */
	tab = ephy_window_get_active_tab (window);
	sync_tab_load_status (tab, NULL, window);
	sync_tab_security (tab, NULL, window);

	egg_editable_toolbar_set_model
		(EGG_EDITABLE_TOOLBAR (window->priv->toolbar),
		 EGG_TOOLBARS_MODEL (
		 	ephy_shell_get_toolbars_model (ephy_shell, TRUE)));

	ephy_toolbar_set_show_leave_fullscreen (window->priv->toolbar,
						!lockdown_fs);

	sync_chromes_visibility (window);
}

static void
ephy_window_unfullscreen (EphyWindow *window)
{
	window->priv->fullscreen_mode = FALSE;

	destroy_fullscreen_popup (window);

	ephy_toolbar_set_show_leave_fullscreen (window->priv->toolbar, FALSE);

	egg_editable_toolbar_set_model
		(EGG_EDITABLE_TOOLBAR (window->priv->toolbar),
		 EGG_TOOLBARS_MODEL (
		 	ephy_shell_get_toolbars_model (ephy_shell, FALSE)));

	sync_chromes_visibility (window);
}

static gboolean
confirm_close_with_modified_forms (EphyWindow *window)
{
	GtkWidget *dialog;
	int response;

	dialog = gtk_message_dialog_new
		(GTK_WINDOW (window),
		 GTK_DIALOG_MODAL,
		 GTK_MESSAGE_WARNING,
		 GTK_BUTTONS_CANCEL,
		 _("There are unsubmitted changes to form elements"));

	gtk_message_dialog_format_secondary_text
		(GTK_MESSAGE_DIALOG (dialog),
		 _("If you close the document anyway, "
		   "you will lose that information."));
	
	gtk_dialog_add_button (GTK_DIALOG (dialog),
			       _("Close _Document"), GTK_RESPONSE_ACCEPT);

	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);

	/* FIXME set title */
	gtk_window_set_icon_name (GTK_WINDOW (dialog), "web-browser");

	gtk_window_group_add_window (GTK_WINDOW (window)->group, GTK_WINDOW (dialog));

	response = gtk_dialog_run (GTK_DIALOG (dialog));

	gtk_widget_destroy (dialog);

	return response == GTK_RESPONSE_ACCEPT;
}

static void
menubar_deactivate_cb (GtkWidget *menubar,
		       EphyWindow *window)
{
	g_signal_handlers_disconnect_by_func
		(menubar, G_CALLBACK (menubar_deactivate_cb), window);

	gtk_menu_shell_deselect (GTK_MENU_SHELL (menubar));

	sync_chromes_visibility (window);
}

static gboolean
ephy_window_key_press_event (GtkWidget *widget,
			     GdkEventKey *event)
{
	EphyWindow *window = EPHY_WINDOW (widget);
	GtkWidget *menubar;
	guint keyval = GDK_F10;
	guint modifier = 0;
	guint mask = gtk_accelerator_get_default_mod_mask ();
	char *accel = NULL;

	/* Handle ESC here instead of an action callback, so we can
	 * handle it differently in the location entry, and we can
	 * stop even when the page is not loading (to stop animations).
	 */
	if (event->keyval == GDK_Escape && (event->state & mask) == 0)
	{
		GtkWidget *widget;
		EphyEmbed *embed;
		gboolean handled = FALSE;

		widget = gtk_window_get_focus (GTK_WINDOW (window));

		if (GTK_IS_WIDGET (widget))
		{
			handled = gtk_widget_event (widget, (GdkEvent*)event);
		}

		embed = ephy_window_get_active_embed (window);
		if (handled == FALSE && embed != NULL)
		{
			gtk_widget_grab_focus (GTK_WIDGET (embed));
			ephy_embed_stop_load (embed);

			handled = TRUE;
		}

		return handled;
	}
		
	/* Don't activate menubar in ppv mode, or in lockdown mode */
	if (window->priv->ppv_mode || eel_gconf_get_boolean (CONF_LOCKDOWN_HIDE_MENUBAR))
	{
		return GTK_WIDGET_CLASS (parent_class)->key_press_event (widget, event);
	}

	g_object_get (gtk_widget_get_settings (widget),
		      "gtk-menu-bar-accel", &accel,
		      NULL);

	if (accel != NULL)
	{
		gtk_accelerator_parse (accel, &keyval, &modifier);

		g_free (accel);
	}

	/* Show and activate the menubar, if it isn't visible */
        if (event->keyval == keyval && (event->state & mask) == (modifier & mask))
	{
		menubar = gtk_ui_manager_get_widget (window->priv->manager, "/menubar");
		g_return_val_if_fail (menubar != NULL , FALSE);

		if (!GTK_WIDGET_VISIBLE (menubar))
		{
			g_signal_connect (menubar, "deactivate",
					  G_CALLBACK (menubar_deactivate_cb), window);

			gtk_widget_show (menubar);
			gtk_menu_shell_select_first (GTK_MENU_SHELL (menubar), FALSE);

			return TRUE;
		}
        }

	return GTK_WIDGET_CLASS (parent_class)->key_press_event (widget, event);
}

static gboolean
ephy_window_delete_event_cb (GtkWidget *widget, GdkEvent *event, EphyWindow *window)
{
	EphyTab *modified_tab = NULL;
	GList *tabs, *l;
	gboolean modified = FALSE;

	/* Workaround a crash when closing a window while in print preview mode. See
	 * mozilla bug #241809
	 */
	if (window->priv->ppv_mode)
	{
		EphyEmbed *embed;

		embed = ephy_window_get_active_embed (window);
		ephy_embed_set_print_preview_mode (embed, FALSE);

		ephy_window_set_print_preview (window, FALSE);
	}

	tabs = ephy_window_get_tabs (window);
	for (l = tabs; l != NULL; l = l->next)
	{
		EphyTab *tab = (EphyTab *) l->data;
		EphyEmbed *embed;

		g_return_val_if_fail (EPHY_IS_TAB (tab), FALSE);

		embed = ephy_tab_get_embed (tab);
		g_return_val_if_fail (EPHY_IS_EMBED (embed), FALSE);

		if (ephy_embed_has_modified_forms (embed))
		{
			modified = TRUE;
			modified_tab = tab;
			break;
		}
	}
	g_list_free (tabs);

	if (modified)
	{
		/* jump to the first tab with modified forms */
		ephy_window_jump_to_tab (window, modified_tab);

		if (confirm_close_with_modified_forms (window) == FALSE)
		{
			/* stop window close */
			return TRUE;
		}
	}
	
	/* See bug #114689 */
	gtk_widget_hide (widget);

	/* proceed with window close */
	return FALSE;
}

static void
update_edit_actions_sensitivity (EphyWindow *window, gboolean hide)
{
	GtkWidget *widget = gtk_window_get_focus (GTK_WINDOW (window));
	GtkActionGroup *action_group;
	GtkAction *action;
	gboolean can_copy, can_cut, can_undo, can_redo, can_paste;

	if (GTK_IS_EDITABLE (widget))
	{
		gboolean has_selection;

		has_selection = gtk_editable_get_selection_bounds
			(GTK_EDITABLE (widget), NULL, NULL);

		can_copy = has_selection;
		can_cut = has_selection;
		can_paste = TRUE;
		can_undo = FALSE;
		can_redo = FALSE;
	}
	else
	{
		EphyEmbed *embed;

		embed = ephy_window_get_active_embed (window);
		g_return_if_fail (embed != NULL);

		can_copy = ephy_command_manager_can_do_command
				(EPHY_COMMAND_MANAGER (embed), "cmd_copy");
		can_cut = ephy_command_manager_can_do_command
				(EPHY_COMMAND_MANAGER (embed), "cmd_cut");
		can_paste = ephy_command_manager_can_do_command
				(EPHY_COMMAND_MANAGER (embed), "cmd_paste");
		can_undo = ephy_command_manager_can_do_command
				(EPHY_COMMAND_MANAGER (embed), "cmd_undo");
		can_redo = ephy_command_manager_can_do_command
				(EPHY_COMMAND_MANAGER (embed), "cmd_redo");
	}

	action_group = window->priv->action_group;

	action = gtk_action_group_get_action (action_group, "EditCopy");
	gtk_action_set_sensitive (action, can_copy);
	gtk_action_set_visible (action, !hide || can_copy);
	action = gtk_action_group_get_action (action_group, "EditCut");
	gtk_action_set_sensitive (action, can_cut);
	gtk_action_set_visible (action, !hide || can_cut);
	action = gtk_action_group_get_action (action_group, "EditPaste");
	gtk_action_set_sensitive (action, can_paste);
	gtk_action_set_visible (action,  !hide || can_paste);
	action = gtk_action_group_get_action (action_group, "EditUndo");
	gtk_action_set_sensitive (action, can_undo);
	gtk_action_set_visible (action,  !hide || can_undo);
	action = gtk_action_group_get_action (action_group, "EditRedo");
	gtk_action_set_sensitive (action, can_redo);
	gtk_action_set_visible (action, !hide || can_redo);
}

static void
enable_edit_actions_sensitivity (EphyWindow *window)
{
	GtkActionGroup *action_group;
	GtkAction *action;

	action_group = window->priv->action_group;

	action = gtk_action_group_get_action (action_group, "EditCopy");
	gtk_action_set_sensitive (action, TRUE);
	gtk_action_set_visible (action, TRUE);
	action = gtk_action_group_get_action (action_group, "EditCut");
	gtk_action_set_sensitive (action, TRUE);
	gtk_action_set_visible (action, TRUE);
	action = gtk_action_group_get_action (action_group, "EditPaste");
	gtk_action_set_sensitive (action, TRUE);
	gtk_action_set_visible (action, TRUE);
	action = gtk_action_group_get_action (action_group, "EditUndo");
	gtk_action_set_sensitive (action, TRUE);
	gtk_action_set_visible (action, TRUE);
	action = gtk_action_group_get_action (action_group, "EditRedo");
	gtk_action_set_sensitive (action, TRUE);
	gtk_action_set_visible (action, TRUE);
}

static void
edit_menu_show_cb (GtkWidget *menu,
		   EphyWindow *window)
{
	update_edit_actions_sensitivity (window, FALSE);
}

static void
edit_menu_hide_cb (GtkWidget *menu,
		   EphyWindow *window)
{
	enable_edit_actions_sensitivity (window);
}

static void
init_menu_updaters (EphyWindow *window)
{
	GtkWidget *edit_menu_item, *edit_menu;

	edit_menu_item = gtk_ui_manager_get_widget
		(window->priv->manager, "/menubar/EditMenu");
	edit_menu = gtk_menu_item_get_submenu (GTK_MENU_ITEM (edit_menu_item));

	g_signal_connect (edit_menu, "show",
			  G_CALLBACK (edit_menu_show_cb), window);
	g_signal_connect (edit_menu, "hide",
			  G_CALLBACK (edit_menu_hide_cb), window);
}

static void
menu_item_select_cb (GtkMenuItem *proxy,
		     EphyWindow *window)
{
	GtkAction *action;
	char *message;

	action = g_object_get_data (G_OBJECT (proxy),  "gtk-action");
	g_return_if_fail (action != NULL);
	
	g_object_get (G_OBJECT (action), "tooltip", &message, NULL);
	if (message)
	{
		gtk_statusbar_push (GTK_STATUSBAR (window->priv->statusbar),
				    window->priv->help_message_cid, message);
		g_free (message);
	}
}

static void
menu_item_deselect_cb (GtkMenuItem *proxy,
		       EphyWindow *window)
{
	gtk_statusbar_pop (GTK_STATUSBAR (window->priv->statusbar),
			   window->priv->help_message_cid);
}

static void
disconnect_proxy_cb (GtkUIManager *manager,
		     GtkAction *action,
		     GtkWidget *proxy,
		     EphyWindow *window)
{
	if (GTK_IS_MENU_ITEM (proxy))
	{
		g_signal_handlers_disconnect_by_func
			(proxy, G_CALLBACK (menu_item_select_cb), window);
		g_signal_handlers_disconnect_by_func
			(proxy, G_CALLBACK (menu_item_deselect_cb), window);
	}
}

static void
connect_proxy_cb (GtkUIManager *manager,
		  GtkAction *action,
		  GtkWidget *proxy,
		  EphyWindow *window)
{
	if (GTK_IS_MENU_ITEM (proxy))
	{
		g_signal_connect (proxy, "select",
				  G_CALLBACK (menu_item_select_cb), window);
		g_signal_connect (proxy, "deselect",
				  G_CALLBACK (menu_item_deselect_cb), window);
	}
}

static void
update_chromes_actions (EphyWindow *window)
{
	GtkActionGroup *action_group = window->priv->action_group;
	GtkAction *action;
	gboolean show_statusbar, show_menubar, show_toolbar, show_bookmarksbar, show_tabsbar;

	get_chromes_visibility (window, &show_menubar,
				&show_statusbar, &show_toolbar,
				&show_bookmarksbar, &show_tabsbar);

	action = gtk_action_group_get_action (action_group, "ViewToolbar");
	g_signal_handlers_block_by_func (G_OBJECT (action),
		 			 G_CALLBACK (ephy_window_view_toolbar_cb),
		 			 window);
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), show_toolbar);
	g_signal_handlers_unblock_by_func (G_OBJECT (action),
		 			   G_CALLBACK (ephy_window_view_toolbar_cb),
		 			   window);

	action = gtk_action_group_get_action (action_group, "ViewBookmarksBar");
	g_signal_handlers_block_by_func (G_OBJECT (action),
		 			 G_CALLBACK (ephy_window_view_bookmarksbar_cb),
		 			 window);
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), show_bookmarksbar);
	g_signal_handlers_unblock_by_func (G_OBJECT (action),
		 			   G_CALLBACK (ephy_window_view_bookmarksbar_cb),
		 			   window);

	action = gtk_action_group_get_action (action_group, "ViewStatusbar");
	g_signal_handlers_block_by_func (G_OBJECT (action),
		 			 G_CALLBACK (ephy_window_view_statusbar_cb),
		 			 window);
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), show_statusbar);
	g_signal_handlers_unblock_by_func (G_OBJECT (action),
		 			   G_CALLBACK (ephy_window_view_statusbar_cb),
		 			   window);
}

#ifdef HAVE_X11_XF86KEYSYM_H
static void
setup_multimedia_key_actions (EphyWindow *window)
{
	GtkActionGroup *action_group = window->priv->action_group;
	GtkAction *action;
	const char *agname, *name;
	char *accel_path;
	guint i;

	agname = gtk_action_group_get_name (action_group);

	for (i = 0; i < G_N_ELEMENTS (xf_key_actions); i++)
	{
		name = xf_key_actions[i].name;

		action = g_object_new (GTK_TYPE_ACTION, "name", name, NULL);
		g_signal_connect (action, "activate",
				  xf_key_actions[i].callback, window);

		accel_path = g_strconcat ("<Actions>/", agname, "/", name, NULL);
		gtk_action_set_accel_path (action, accel_path);
		gtk_accel_map_add_entry (accel_path, xf_key_actions[i].keyval, 0);
		gtk_action_group_add_action (action_group, action);
		g_free (accel_path);
	}
}
#endif /* HAVE_X11_XF86KEYSYM_H */

static void
setup_ui_manager (EphyWindow *window)
{
	GtkActionGroup *action_group;
	GtkAction *action;
	GtkUIManager *manager;

	window->priv->main_vbox = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (window->priv->main_vbox);
	gtk_container_add (GTK_CONTAINER (window),
			   window->priv->main_vbox);

	window->priv->menu_dock = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (window->priv->menu_dock);
	gtk_box_pack_start (GTK_BOX (window->priv->main_vbox),
			    GTK_WIDGET (window->priv->menu_dock),
			    FALSE, TRUE, 0);

	manager = gtk_ui_manager_new ();

	g_signal_connect (manager, "connect_proxy",
			  G_CALLBACK (connect_proxy_cb), window);
	g_signal_connect (manager, "disconnect_proxy",
			  G_CALLBACK (disconnect_proxy_cb), window);

	action_group = gtk_action_group_new ("WindowActions");
	gtk_action_group_set_translation_domain (action_group, NULL);
	gtk_action_group_add_actions (action_group, ephy_menu_entries,
				      G_N_ELEMENTS (ephy_menu_entries), window);
	gtk_action_group_add_toggle_actions (action_group,
					     ephy_menu_toggle_entries,
					     G_N_ELEMENTS (ephy_menu_toggle_entries),
					     window);
	gtk_ui_manager_insert_action_group (manager, action_group, 0);
	window->priv->action_group = action_group;
	action = gtk_action_group_get_action (action_group, "FileOpen");
	g_object_set (action, "short_label", _("Open"), NULL);
	action = gtk_action_group_get_action (action_group, "FileSaveAs");
	g_object_set (action, "short_label", _("Save As"), NULL);
	action = gtk_action_group_get_action (action_group, "FilePrint");
	g_object_set (action, "short_label", _("Print"), NULL);
	action = gtk_action_group_get_action (action_group, "FileBookmarkPage");
	g_object_set (action, "short_label", _("Bookmark"), NULL);
	action = gtk_action_group_get_action (action_group, "EditFind");
	g_object_set (action, "short_label", _("Find"), NULL);
	action = gtk_action_group_get_action (action_group, "GoBookmarks");
	g_object_set (action, "short_label", _("Bookmarks"), NULL);

	action = gtk_action_group_get_action (action_group, "EditFind");
	g_object_set (action, "is_important", TRUE, NULL);
	action = gtk_action_group_get_action (action_group, "GoBookmarks");
	g_object_set (action, "is_important", TRUE, NULL);

	action = gtk_action_group_get_action (action_group, "ViewEncoding");
	g_object_set (action, "hide_if_empty", FALSE, NULL);

	action_group = gtk_action_group_new ("PopupsActions");
	gtk_action_group_set_translation_domain (action_group, NULL);
	gtk_action_group_add_actions (action_group, ephy_popups_entries,
				      G_N_ELEMENTS (ephy_popups_entries), window);
	gtk_ui_manager_insert_action_group (manager, action_group, 0);
	window->priv->popups_action_group = action_group;

	window->priv->manager = manager;
	g_signal_connect (manager, "add_widget", G_CALLBACK (add_widget), window);
	gtk_window_add_accel_group (GTK_WINDOW (window),
				    gtk_ui_manager_get_accel_group (manager));

#ifdef HAVE_X11_XF86KEYSYM_H
	setup_multimedia_key_actions (window);
#endif				    
}

static void
sync_tab_address (EphyTab *tab, GParamSpec *pspec, EphyWindow *window)
{
	if (window->priv->closing) return;

	ephy_toolbar_set_location (window->priv->toolbar,
				   ephy_tab_get_address (tab),
				   ephy_tab_get_typed_address (tab));
}

static void
sync_tab_document_type (EphyTab *tab,
			GParamSpec *pspec,
			EphyWindow *window)
{
	EphyWindowPrivate *priv = window->priv;
	GtkActionGroup *action_group = priv->action_group;
	GtkAction *action;
	EphyEmbedDocumentType type;
	gboolean can_find, disable, is_image;

	if (priv->closing) return;

	/* update zoom actions */
	sync_tab_zoom (tab, NULL, window);
	
	type = ephy_tab_get_document_type (tab);
	can_find = (type != EPHY_EMBED_DOCUMENT_IMAGE);
	is_image = type == EPHY_EMBED_DOCUMENT_IMAGE;
	disable = (type != EPHY_EMBED_DOCUMENT_HTML);

	action = gtk_action_group_get_action (action_group, "ViewEncoding");
	ephy_action_change_sensitivity_flags (action, SENS_FLAG_DOCUMENT, disable);
	action = gtk_action_group_get_action (action_group, "ViewPageSource");
	ephy_action_change_sensitivity_flags (action, SENS_FLAG_DOCUMENT, is_image);
	action = gtk_action_group_get_action (action_group, "EditFind");
	ephy_action_change_sensitivity_flags (action, SENS_FLAG_DOCUMENT, !can_find);
	action = gtk_action_group_get_action (action_group, "EditFindNext");
	ephy_action_change_sensitivity_flags (action, SENS_FLAG_DOCUMENT, !can_find);
	action = gtk_action_group_get_action (action_group, "EditFindPrev");
	ephy_action_change_sensitivity_flags (action, SENS_FLAG_DOCUMENT, !can_find);

	if (!can_find)
	{
		ephy_find_toolbar_close (priv->find_toolbar);
	}
}

static void
sync_tab_icon (EphyTab *tab, GParamSpec *pspec, EphyWindow *window)
{
	const char *address;
	EphyFaviconCache *cache;
	GdkPixbuf *pixbuf = NULL;

	if (window->priv->closing) return;

	cache = EPHY_FAVICON_CACHE
		(ephy_embed_shell_get_favicon_cache
			(EPHY_EMBED_SHELL (ephy_shell)));

	address = ephy_tab_get_icon_address (tab);

	if (address)
	{
		pixbuf = ephy_favicon_cache_get (cache, address);
	}

	gtk_window_set_icon (GTK_WINDOW (window), pixbuf);

	ephy_toolbar_set_favicon (window->priv->toolbar, address);

	if (pixbuf)
	{
		g_object_unref (pixbuf);
	}
}

static void
sync_tab_load_progress (EphyTab *tab, GParamSpec *pspec, EphyWindow *window)
{
	if (window->priv->closing) return;

	ephy_statusbar_set_progress (EPHY_STATUSBAR (window->priv->statusbar),
				     ephy_tab_get_load_percent (tab));
}

static void
sync_tab_message (EphyTab *tab, GParamSpec *pspec, EphyWindow *window)
{
	GtkStatusbar *s = GTK_STATUSBAR (window->priv->statusbar);
	const char *message;

	if (window->priv->closing) return;

	message = ephy_tab_get_status_message (tab);

	gtk_statusbar_pop (s, window->priv->tab_message_cid);

	if (message)
	{
		gtk_statusbar_push (s, window->priv->tab_message_cid, message);
	}
}

static void
sync_tab_navigation (EphyTab *tab,
		     GParamSpec *pspec,
		     EphyWindow *window)
{
	EphyTabNavigationFlags flags;
	GtkActionGroup *action_group;
	GtkAction *action;
	gboolean up = FALSE, back = FALSE, forward = FALSE;

	if (window->priv->closing) return;

	flags = ephy_tab_get_navigation_flags (tab);

	if (flags & EPHY_TAB_NAV_UP)
	{
		up = TRUE;
	}
	if (flags & EPHY_TAB_NAV_BACK)
	{
		back = TRUE;
	}
	if (flags & EPHY_TAB_NAV_FORWARD)
	{
		forward = TRUE;
	}

	action_group = window->priv->action_group;
	action = gtk_action_group_get_action (action_group, "GoUp");
	ephy_action_change_sensitivity_flags (action, SENS_FLAG_NAVIGATION, !up);
	action = gtk_action_group_get_action (action_group, "GoBack");
	ephy_action_change_sensitivity_flags (action, SENS_FLAG_NAVIGATION, !back);
	action = gtk_action_group_get_action (action_group, "GoForward");
	ephy_action_change_sensitivity_flags (action, SENS_FLAG_NAVIGATION, !forward);

	ephy_toolbar_set_navigation_actions (window->priv->toolbar,
					     back, forward, up);
}

static void
sync_tab_security (EphyTab *tab, GParamSpec *pspec, EphyWindow *window)
{
	EphyEmbed *embed;
	EphyEmbedSecurityLevel level;
	char *description = NULL;
	char *state = NULL;
	char *tooltip;
	const char *stock_id = STOCK_LOCK_INSECURE;
	gboolean show_lock = FALSE;

	if (window->priv->closing) return;

	embed = ephy_tab_get_embed (tab);

	ephy_embed_get_security_level (embed, &level, &description);

	switch (level)
	{
		case EPHY_EMBED_STATE_IS_UNKNOWN:
			state = _("Unknown");
			break;
		case EPHY_EMBED_STATE_IS_INSECURE:
			state = _("Insecure");
			g_free (description);
			description = NULL;
			break;
		case EPHY_EMBED_STATE_IS_BROKEN:
			state = _("Broken");
			stock_id = STOCK_LOCK_BROKEN;
			show_lock = TRUE;
			g_free (description);
			description = NULL;
			break;
		case EPHY_EMBED_STATE_IS_SECURE_LOW:
		case EPHY_EMBED_STATE_IS_SECURE_MED:
			state = _("Low");
			/* We deliberately don't show the 'secure' icon
			 * for low & medium secure sites; see bug #151709.
			 */
			stock_id = STOCK_LOCK_INSECURE;
			break;
		case EPHY_EMBED_STATE_IS_SECURE_HIGH:
			state = _("High");
			stock_id = STOCK_LOCK_SECURE;
			show_lock = TRUE;
			break;
		default:
			g_assert_not_reached ();
			break;
	}

	tooltip = g_strdup_printf (_("Security level: %s"), state);
	if (description != NULL)
	{
		char *tmp = tooltip;

		tooltip = g_strconcat (tmp, "\n", description, NULL);
		g_free (description);
		g_free (tmp);
	}

	ephy_statusbar_set_security_state (EPHY_STATUSBAR (window->priv->statusbar),
					   stock_id, tooltip);

	ephy_toolbar_set_security_state (window->priv->toolbar,
					 show_lock, stock_id, tooltip);

	if (window->priv->fullscreen_popup != NULL)
	{
		ephy_fullscreen_popup_set_security_state
			(EPHY_FULLSCREEN_POPUP (window->priv->fullscreen_popup),
			 show_lock, stock_id, tooltip);
	}

	g_free (tooltip);
}

static void
sync_tab_popup_windows (EphyTab *tab,
			GParamSpec *pspec,
			EphyWindow *window)
{
	guint num_popups = 0;
	char *tooltip = NULL;

	g_object_get (G_OBJECT (tab),
		      "hidden-popup-count", &num_popups,
		      NULL);

	if (num_popups > 0)
	{
		tooltip = g_strdup_printf (ngettext ("%d hidden popup window",
						     "%d hidden popup windows",
						     num_popups),
					   num_popups);
	}

	ephy_statusbar_set_popups_state
		(EPHY_STATUSBAR (window->priv->statusbar),
		 tooltip == NULL,
		 tooltip);

	g_free (tooltip);
}

static void
sync_tab_popups_allowed (EphyTab *tab,
			 GParamSpec *pspec,
			 EphyWindow *window)
{
	GtkAction *action;
	gboolean allow;

	g_return_if_fail (EPHY_IS_TAB (tab));
	g_return_if_fail (EPHY_IS_WINDOW (window));

	action = gtk_action_group_get_action (window->priv->action_group,
					      "ViewPopupWindows");
	g_return_if_fail (GTK_IS_ACTION (action));

	g_object_get (G_OBJECT (tab), "popups-allowed", &allow, NULL);

	g_signal_handlers_block_by_func
		(G_OBJECT (action),
		 G_CALLBACK (ephy_window_view_popup_windows_cb),
		 window);

	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), allow);

	g_signal_handlers_unblock_by_func
		(G_OBJECT (action),
		 G_CALLBACK (ephy_window_view_popup_windows_cb),
		 window);
}

static void
sync_tab_load_status (EphyTab *tab,
		      GParamSpec *pspec,
		      EphyWindow *window)
{
	EphyWindowPrivate *priv = window->priv;
	GtkActionGroup *action_group = priv->action_group;
	GtkAction *action;
	gboolean loading;

	if (window->priv->closing) return;

	loading = ephy_tab_get_load_status (tab);

	action = gtk_action_group_get_action (action_group, "ViewStop");
	gtk_action_set_sensitive (action, loading);

	/* disable print while loading, see bug #116344 */
	action = gtk_action_group_get_action (action_group, "FilePrintPreview");
	ephy_action_change_sensitivity_flags (action, SENS_FLAG_LOADING, loading);
	action = gtk_action_group_get_action (action_group, "FilePrint");
	ephy_action_change_sensitivity_flags (action, SENS_FLAG_LOADING, loading);

	ephy_toolbar_set_spinning (priv->toolbar, loading);

	if (priv->fullscreen_popup)
	{
		ephy_fullscreen_popup_set_spinning
			 (EPHY_FULLSCREEN_POPUP (priv->fullscreen_popup),
			  loading);
	}
}

static void
sync_tab_title (EphyTab *tab,
		GParamSpec *pspec,
		EphyWindow *window)
{
	EphyWindowPrivate *priv = window->priv;

	if (priv->closing) return;

	gtk_window_set_title (GTK_WINDOW(window), ephy_tab_get_title (tab));
}

static void
sync_tab_visibility (EphyTab *tab,
		     GParamSpec *pspec,
		     EphyWindow *window)
{
	EphyWindowPrivate *priv = window->priv;
	GList *l, *tabs;
	gboolean visible = FALSE;

	if (priv->closing) return;

	tabs = ephy_window_get_tabs (window);
	for (l = tabs; l != NULL; l = l->next)
	{
		EphyTab *tab = EPHY_TAB(l->data);
		g_return_if_fail (EPHY_IS_TAB(tab));

		if (ephy_tab_get_visibility (tab))
		{
			visible = TRUE;
			break;
		}
	}
	g_list_free (tabs);

	g_object_set (window, "visible", visible, NULL);
}

static void
sync_tab_zoom (EphyTab *tab, GParamSpec *pspec, EphyWindow *window)
{
	GtkActionGroup *action_group;
	GtkAction *action;
	EphyEmbedDocumentType type;
	gboolean can_zoom_in = TRUE, can_zoom_out = TRUE, can_zoom_normal = FALSE, can_zoom;
	float zoom;

	if (window->priv->closing) return;

	zoom = ephy_tab_get_zoom (tab);
	type = ephy_tab_get_document_type (tab);
	can_zoom = (type != EPHY_EMBED_DOCUMENT_IMAGE);

	if (zoom >= ZOOM_MAXIMAL)
	{
		can_zoom_in = FALSE;
	}
	if (zoom <= ZOOM_MINIMAL)
	{
		can_zoom_out = FALSE;
	}
	if (zoom != 1.0)
	{
		can_zoom_normal = TRUE;
	}

	ephy_toolbar_set_zoom (window->priv->toolbar, can_zoom, zoom);

	action_group = window->priv->action_group;
	action = gtk_action_group_get_action (action_group, "ViewZoomIn");
	gtk_action_set_sensitive (action, can_zoom_in && can_zoom);
	action = gtk_action_group_get_action (action_group, "ViewZoomOut");
	gtk_action_set_sensitive (action, can_zoom_out && can_zoom);
	action = gtk_action_group_get_action (action_group, "ViewZoomNormal");
	gtk_action_set_sensitive (action, can_zoom_normal && can_zoom);
}

static void
network_status_changed (EphyEmbedSingle *single,
			gboolean offline,
			EphyWindow *window)
{
	GtkAction *action;

	action = gtk_action_group_get_action (window->priv->action_group,
					      "FileWorkOffline");
	g_signal_handlers_block_by_func
		(action, G_CALLBACK (window_cmd_file_work_offline), window);
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), offline);
	g_signal_handlers_unblock_by_func
		(action, G_CALLBACK (window_cmd_file_work_offline), window);	
}

static void
popup_menu_at_coords (GtkMenu *menu, gint *x, gint *y, gboolean *push_in,
		      gpointer user_data)
{
	GtkWidget *window = GTK_WIDGET (user_data);
	EphyEmbedEvent *event;
	guint ux, uy;

	event = g_object_get_data (G_OBJECT (window), "context_event");
	g_return_if_fail (event != NULL);

	ephy_embed_event_get_coords (event, &ux, &uy);
	*x = ux; *y = uy;

	/* FIXME: better position the popup within the window bounds? */
	ephy_gui_sanitise_popup_position (menu, window, x, y);

	*push_in = TRUE;
}

static void
hide_embed_popup_cb (GtkWidget *popup,
		     EphyWindow *window)
{
	enable_edit_actions_sensitivity (window);

	g_signal_handlers_disconnect_by_func
		(popup, G_CALLBACK (hide_embed_popup_cb), window);
}

static char *
get_name_from_address_value (const GValue *value)
{
	GnomeVFSURI *uri;
	char *name = NULL;

	uri = gnome_vfs_uri_new (g_value_get_string (value));
	if (uri)
	{
		name = gnome_vfs_uri_extract_short_name (uri);
		gnome_vfs_uri_unref (uri);
	}

	return name;
}

static void
update_popups_tooltips (EphyWindow *window, EphyEmbedEvent *event)
{
	EphyEmbedEventContext context;
	GtkActionGroup *group = window->priv->popups_action_group;
	const GValue *value;
	GtkAction *action;
	char *tooltip, *name;

	if (ephy_embed_event_has_property (event, "background_image"))
	{
		ephy_embed_event_get_property (event, "background_image", &value);

		action = gtk_action_group_get_action (group, "SaveBackgroundAs");
		name = get_name_from_address_value (value);
		tooltip = g_strdup_printf (_("Save background image '%s'"), name);
		g_object_set (action, "tooltip", tooltip, NULL);
		g_free (name);
		g_free (tooltip);
	}

	context = ephy_embed_event_get_context (event);

	if (context & EPHY_EMBED_CONTEXT_IMAGE)
	{
		ephy_embed_event_get_property (event, "image", &value);
		name = get_name_from_address_value (value);

		action = gtk_action_group_get_action (group, "OpenImage");
		tooltip = g_strdup_printf (_("Open image '%s'"), name);
		g_object_set (action, "tooltip", tooltip, NULL);
		g_free (tooltip);

		action = gtk_action_group_get_action (group, "SetImageAsBackground");
		tooltip = g_strdup_printf (_("Use as desktop background '%s'"), name);
		g_object_set (action, "tooltip", tooltip, NULL);
		g_free (tooltip);

		action = gtk_action_group_get_action (group, "SaveImageAs");
		tooltip = g_strdup_printf (_("Save image '%s'"), name);
		g_object_set (action, "tooltip", tooltip, NULL);
		g_free (tooltip);

		action = gtk_action_group_get_action (group, "CopyImageLocation");
		tooltip = g_strdup_printf (_("Copy image address '%s'"),
					   g_value_get_string (value));
		g_object_set (action, "tooltip", tooltip, NULL);
		g_free (tooltip);		

		g_free (name);
	}

	if (context & EPHY_EMBED_CONTEXT_EMAIL_LINK)
	{
		ephy_embed_event_get_property (event, "link", &value);

		action = gtk_action_group_get_action (group, "SendEmail");
		tooltip = g_strdup_printf (_("Send email to address '%s'"),
					   g_value_get_string (value));
		g_object_set (action, "tooltip", tooltip, NULL);
		g_free (tooltip);

		action = gtk_action_group_get_action (group, "CopyEmailAddress");
		tooltip = g_strdup_printf (_("Copy email address '%s'"),
					   g_value_get_string (value));
		g_object_set (action, "tooltip", tooltip, NULL);
		g_free (tooltip);
	}

	if (context & EPHY_EMBED_CONTEXT_LINK)
	{
		ephy_embed_event_get_property (event, "link", &value);

		action = gtk_action_group_get_action (group, "DownloadLink");
		name = get_name_from_address_value (value);
		tooltip = g_strdup_printf (_("Save link '%s'"), name);
		g_object_set (action, "tooltip", tooltip, NULL);
		g_free (name);
		g_free (tooltip);

		action = gtk_action_group_get_action (group, "BookmarkLink");
		tooltip = g_strdup_printf (_("Bookmark link '%s'"),
					   g_value_get_string (value));
		g_object_set (action, "tooltip", tooltip, NULL);
		g_free (tooltip);

		action = gtk_action_group_get_action (group, "CopyLinkAddress");
		tooltip = g_strdup_printf (_("Copy link's address '%s'"),
					   g_value_get_string (value));
		g_object_set (action, "tooltip", tooltip, NULL);
		g_free (tooltip);
	}
}

static void
show_embed_popup (EphyWindow *window,
		  EphyTab *tab,
		  EphyEmbedEvent *event)
{
	EphyWindowPrivate *priv = window->priv;
	GtkActionGroup *action_group;
	GtkAction *action;
	EphyEmbedEventContext context;
	const char *popup;
	const GValue *value;
	gboolean framed, has_background, can_open_in_new;
	GtkWidget *widget;
	guint button;

	/* Do not show the menu in print preview mode */
	if (priv->ppv_mode)
	{
		return;
	}

	ephy_embed_event_get_property (event, "framed_page", &value);
	framed = g_value_get_int (value);

	has_background = ephy_embed_event_has_property (event, "background_image");
	can_open_in_new = ephy_embed_event_has_property (event, "link-has-web-scheme");

	context = ephy_embed_event_get_context (event);

	LOG ("show_embed_popup context %x", context);

	if ((context & EPHY_EMBED_CONTEXT_EMAIL_LINK) &&
	    (context & EPHY_EMBED_CONTEXT_IMAGE))
	{
		popup = "/EphyImageEmailLinkPopup";
	}
	else if (context & EPHY_EMBED_CONTEXT_EMAIL_LINK)
	{
		popup = "/EphyEmailLinkPopup";
		update_edit_actions_sensitivity (window, TRUE);
	}
	else if ((context & EPHY_EMBED_CONTEXT_LINK) &&
		 (context & EPHY_EMBED_CONTEXT_IMAGE))
	{
		popup = "/EphyImageLinkPopup";
	}
	else if (context & EPHY_EMBED_CONTEXT_LINK)
	{
		popup = "/EphyLinkPopup";
		update_edit_actions_sensitivity (window, TRUE);
	}
	else if (context & EPHY_EMBED_CONTEXT_IMAGE)
	{
		popup = "/EphyImagePopup";
	}
	else if (context & EPHY_EMBED_CONTEXT_INPUT)
	{
		popup = "/EphyInputPopup";
		update_edit_actions_sensitivity (window, FALSE);
	}
	else if (window->priv->fullscreen_mode)
	{
		popup = framed ? "/EphyFullscreenFramedDocumentPopup" :
				 "/EphyFullscreenDocumentPopup";
		update_edit_actions_sensitivity (window, TRUE);
	}
	else
	{
		popup = framed ? "/EphyFramedDocumentPopup" :
				 "/EphyDocumentPopup";
		update_edit_actions_sensitivity (window, TRUE);
	}

	update_popups_tooltips (window, event);

	widget = gtk_ui_manager_get_widget (priv->manager, popup);
	g_return_if_fail (widget != NULL);

	action_group = window->priv->popups_action_group;
	action = gtk_action_group_get_action (action_group, "SaveBackgroundAs");
	ephy_action_change_sensitivity_flags (action, SENS_FLAG_CONTEXT, !has_background);
	gtk_action_set_visible (action, has_background);

	action = gtk_action_group_get_action (action_group, "OpenLinkInNewWindow");
	gtk_action_set_sensitive (action, can_open_in_new);

	action = gtk_action_group_get_action (action_group, "OpenLinkInNewTab");
	ephy_action_change_sensitivity_flags (action, SENS_FLAG_CONTEXT, !can_open_in_new);

	g_object_set_data_full (G_OBJECT (window), "context_event",
				g_object_ref (event),
				(GDestroyNotify)g_object_unref);

	g_signal_connect (widget, "hide",
			  G_CALLBACK (hide_embed_popup_cb), window);

	button = ephy_embed_event_get_button (event);
	if (button == 0)
	{
		gtk_menu_popup (GTK_MENU (widget), NULL, NULL,
				popup_menu_at_coords, window, 0,
				gtk_get_current_event_time ());
		gtk_menu_shell_select_first (GTK_MENU_SHELL (widget), FALSE);
	}
	else
	{
		gtk_menu_popup (GTK_MENU (widget), NULL, NULL,
				NULL, NULL, button,
				gtk_get_current_event_time ());
	}
}

static gboolean
tab_context_menu_cb (EphyEmbed *embed,
		     EphyEmbedEvent *event,
		     EphyWindow *window)
{
	EphyTab *tab;

	tab = ephy_tab_for_embed (embed);
	g_return_val_if_fail (EPHY_IS_TAB (tab), FALSE);
	g_return_val_if_fail (window->priv->active_tab == tab, FALSE);

	show_embed_popup (window, tab, event);

	return TRUE;
}

static gboolean
let_me_resize_hack (EphyWindow *window)
{
	EphyWindowPrivate *priv = window->priv;

	gtk_window_set_resizable (GTK_WINDOW (window), TRUE);

	priv->idle_resize_handler = 0;
	return FALSE;
}

static void
tab_size_to_cb (EphyEmbed *embed,
		int width,
		int height,
		EphyWindow *window)
{
	EphyWindowPrivate *priv = window->priv;
	GtkWidget *widget = GTK_WIDGET (window);
	GtkWidget *embed_widget = GTK_WIDGET (embed);
	GdkScreen *screen;
	EphyTab *tab;
	GdkRectangle rect;
	int monitor;
	int ww, wh, ew, eh, dw, dh;

	LOG ("tab_size_to_cb window %p embed %p width %d height %d", window, embed, width, height);

	tab = ephy_tab_for_embed (embed);
	g_return_if_fail (tab != NULL);

	/* FIXME: allow sizing also for non-popup single-tab windows? */
	if (tab != ephy_window_get_active_tab (window) || !priv->is_popup) return;

	/* contrain size so that the window will be fully contained within the screen */
	screen = gtk_widget_get_screen (widget);
	monitor = gdk_screen_get_monitor_at_window (screen, widget->window);
	gdk_screen_get_monitor_geometry (screen, monitor, &rect);
	/* FIXME: get and subtract the panel size */

	gtk_window_get_size (GTK_WINDOW (window), &ww, &wh);

	ew = embed_widget->allocation.width;
	eh = embed_widget->allocation.height;

	/* This should approximate the chrome extent */
	dw = ww - ew; dw = MAX (dw, 0); dw = MIN (dw, rect.width - 1);
	dh = wh - eh; dh = MAX (dh, 0); dh = MIN (dh, rect.height - 1);

	width = MIN (rect.width - dw, width);
	height = MIN (rect.height - dh, height);

	/* FIXME: move window if this will place it partially outside the screen rect? */

	gtk_window_set_resizable (GTK_WINDOW (window), FALSE);
	ephy_tab_set_size (tab, width, height);

	if (priv->idle_resize_handler == 0)
	{
		priv->idle_resize_handler =
			g_idle_add ((GSourceFunc) let_me_resize_hack, window);
	}
}

static void
ephy_window_set_active_tab (EphyWindow *window, EphyTab *new_tab)
{
	EphyTab *old_tab;
	EphyEmbed *embed;

	g_return_if_fail (EPHY_IS_WINDOW (window));
	g_return_if_fail (gtk_widget_get_toplevel (GTK_WIDGET (new_tab)) == GTK_WIDGET (window));

	old_tab = window->priv->active_tab;

	if (old_tab == new_tab) return;

	if (old_tab != NULL)
	{
		g_signal_handlers_disconnect_by_func (old_tab,
						      G_CALLBACK (sync_tab_address),
						      window);
		g_signal_handlers_disconnect_by_func (old_tab,
						      G_CALLBACK (sync_tab_document_type),
						      window);
		g_signal_handlers_disconnect_by_func (old_tab,
						      G_CALLBACK (sync_tab_icon),
						      window);
		g_signal_handlers_disconnect_by_func (old_tab,
						      G_CALLBACK (sync_tab_load_progress),
						      window);
		g_signal_handlers_disconnect_by_func (old_tab,
						      G_CALLBACK (sync_tab_load_status),
						      window);
		g_signal_handlers_disconnect_by_func (old_tab,
						      G_CALLBACK (sync_tab_message),
						      window);
		g_signal_handlers_disconnect_by_func (old_tab,
						      G_CALLBACK (sync_tab_navigation),
						      window);
		g_signal_handlers_disconnect_by_func (old_tab,
						      G_CALLBACK (sync_tab_security),
						      window);
		g_signal_handlers_disconnect_by_func (old_tab,
						      G_CALLBACK (sync_tab_popup_windows),
						      window);
		g_signal_handlers_disconnect_by_func (old_tab,
						      G_CALLBACK (sync_tab_popups_allowed),
						      window);
		g_signal_handlers_disconnect_by_func (old_tab,
						      G_CALLBACK (sync_tab_title),
						      window);
		g_signal_handlers_disconnect_by_func (old_tab,
						      G_CALLBACK (sync_tab_zoom),
						      window);

		embed = ephy_tab_get_embed (old_tab);
		g_signal_handlers_disconnect_by_func
			(embed, G_CALLBACK (tab_context_menu_cb), window);
		g_signal_handlers_disconnect_by_func
			(embed, G_CALLBACK (tab_size_to_cb), window);

	}

	window->priv->active_tab = new_tab;

	if (new_tab != NULL)
	{
		sync_tab_address	(new_tab, NULL, window);
		sync_tab_document_type	(new_tab, NULL, window);
		sync_tab_icon		(new_tab, NULL, window);
		sync_tab_load_progress	(new_tab, NULL, window);
		sync_tab_load_status	(new_tab, NULL, window);
		sync_tab_message	(new_tab, NULL, window);
		sync_tab_navigation	(new_tab, NULL, window);
		sync_tab_security	(new_tab, NULL, window);
		sync_tab_popup_windows	(new_tab, NULL, window);
		sync_tab_popups_allowed	(new_tab, NULL, window);
		sync_tab_title		(new_tab, NULL, window);
		sync_tab_zoom		(new_tab, NULL, window);

		g_signal_connect_object (new_tab, "notify::address",
					 G_CALLBACK (sync_tab_address),
					 window, 0);
		g_signal_connect_object (new_tab, "notify::document-type",
					 G_CALLBACK (sync_tab_document_type),
					 window, 0);
		g_signal_connect_object (new_tab, "notify::icon",
					 G_CALLBACK (sync_tab_icon),
					 window, 0);
		g_signal_connect_object (new_tab, "notify::load-progress",
					 G_CALLBACK (sync_tab_load_progress),
					 window, 0);
		g_signal_connect_object (new_tab, "notify::load-status",
					 G_CALLBACK (sync_tab_load_status),
					 window, 0);
		g_signal_connect_object (new_tab, "notify::message",
					 G_CALLBACK (sync_tab_message),
					 window, 0);
		g_signal_connect_object (new_tab, "notify::navigation",
					 G_CALLBACK (sync_tab_navigation),
					 window, 0);
		g_signal_connect_object (new_tab, "notify::security-level",
					 G_CALLBACK (sync_tab_security),
					 window, 0);
		g_signal_connect_object (new_tab, "notify::hidden-popup-count",
					 G_CALLBACK (sync_tab_popup_windows),
					 window, 0);
		g_signal_connect_object (new_tab, "notify::popups-allowed",
					 G_CALLBACK (sync_tab_popups_allowed),
					 window, 0);
		g_signal_connect_object (new_tab, "notify::title",
					 G_CALLBACK (sync_tab_title),
					 window, 0);
		g_signal_connect_object (new_tab, "notify::zoom",
					 G_CALLBACK (sync_tab_zoom),
					 window, 0);

		embed = ephy_tab_get_embed (new_tab);
		g_signal_connect_object (embed, "ge-context-menu",
					 G_CALLBACK (tab_context_menu_cb),
					 window, G_CONNECT_AFTER);
		g_signal_connect_object (embed, "size-to",
					 G_CALLBACK (tab_size_to_cb),
					 window, 0);

		g_object_notify (G_OBJECT (window), "active-tab");
	}
}

static void
update_tabs_menu_sensitivity (EphyWindow *window)
{
	gboolean prev_tab, next_tab, move_left, move_right, detach;
	GtkActionGroup *action_group;
	GtkAction *action;
	int current;
	int last;

	current = gtk_notebook_get_current_page
		(GTK_NOTEBOOK (window->priv->notebook));
	last = gtk_notebook_get_n_pages
		(GTK_NOTEBOOK (window->priv->notebook)) - 1;
	prev_tab = move_left = (current > 0);
	next_tab = move_right = (current < last);
	detach = gtk_notebook_get_n_pages
		(GTK_NOTEBOOK (window->priv->notebook)) > 1;

	action_group = window->priv->action_group;
	action = gtk_action_group_get_action (action_group, "TabsPrevious");
	gtk_action_set_sensitive (action, prev_tab);
	action = gtk_action_group_get_action (action_group, "TabsNext");
	gtk_action_set_sensitive (action, next_tab);
	action = gtk_action_group_get_action (action_group, "TabsMoveLeft");
	gtk_action_set_sensitive (action, move_left);
	action = gtk_action_group_get_action (action_group, "TabsMoveRight");
	gtk_action_set_sensitive (action, move_right);
	action = gtk_action_group_get_action (action_group, "TabsDetach");
	ephy_action_change_sensitivity_flags (action, SENS_FLAG_CHROME, !detach);
#ifdef KEEP_TAB_IN_SAME_TOPLEVEL
	gtk_action_set_visible (action, FALSE);
#endif
}

static gboolean
modal_alert_cb (EphyEmbed *embed,
		EphyWindow *window)
{
	EphyWindowPrivate *priv = window->priv;
	EphyTab *tab;
	char *address;

	tab = ephy_tab_for_embed (embed);
	g_return_val_if_fail (tab != NULL, FALSE);

	/* if we're in ppv mode, we cannot switch tabs, so inhibit the alert */
	if (priv->ppv_mode) return TRUE;

	/* switch the window to the tab, and bring the window to the foreground
	 * (since the alert is modal, the user won't be able to do anything
	 * with his current window anyway :|)
	 */
	ephy_window_jump_to_tab (window, tab);
	gtk_window_present (GTK_WINDOW (window));

	/* make sure the location entry shows the real URL of the tab's page */
	address = ephy_embed_get_location (embed, TRUE);
	ephy_toolbar_set_location (priv->toolbar, address, NULL);
	g_free (address);

	/* don't suppress alert */
	return FALSE;
}

static gboolean
show_notebook_popup_menu (GtkNotebook *notebook,
			  EphyWindow *window,
			  GdkEventButton *event)
{
	GtkWidget *menu, *tab, *tab_label;
	GtkAction *action;

	menu = gtk_ui_manager_get_widget (window->priv->manager, "/EphyNotebookPopup");
	g_return_val_if_fail (menu != NULL, FALSE);

	/* allow extensions to sync when showing the popup */
	action = gtk_action_group_get_action (window->priv->action_group,
					      "NotebookPopupAction");
	g_return_val_if_fail (action != NULL, FALSE);
	gtk_action_activate (action);

	if (event != NULL)
	{
		gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
				NULL, NULL,
				event->button, event->time);
	}
	else
	{
		tab = GTK_WIDGET (ephy_window_get_active_tab (window));
		tab_label = gtk_notebook_get_tab_label (notebook, tab);

		gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
				ephy_gui_menu_position_under_widget, tab_label,
				0, gtk_get_current_event_time ());
		gtk_menu_shell_select_first (GTK_MENU_SHELL (menu), FALSE);
	}

	return TRUE;
}

static gboolean
notebook_button_press_cb (GtkNotebook *notebook,
			  GdkEventButton *event,
			  EphyWindow *window)
{
	if (GDK_BUTTON_PRESS == event->type && 3 == event->button)
	{
		return show_notebook_popup_menu (notebook, window, event);
	}

	return FALSE;
}

static gboolean
notebook_popup_menu_cb (GtkNotebook *notebook,
			EphyWindow *window)
{
	/* Only respond if the notebook is the actual focus */
	if (EPHY_IS_NOTEBOOK (gtk_window_get_focus (GTK_WINDOW (window))))
	{
		return show_notebook_popup_menu (notebook, window, NULL);
	}

	return FALSE;
}

static void
tab_added_cb (EphyNotebook *notebook,
	      EphyTab *tab,
	      EphyWindow *window)
{
	EphyExtension *manager;
	EphyEmbed *embed;

        g_return_if_fail (EPHY_IS_TAB (tab));

	window->priv->num_tabs++;

	update_tabs_menu_sensitivity (window);

	g_signal_connect_object (G_OBJECT (tab), "notify::visibility",
				 G_CALLBACK (sync_tab_visibility), window, 0);
	g_signal_connect_object (G_OBJECT (tab), "open-link",
				 G_CALLBACK (ephy_link_open), window,
				 G_CONNECT_SWAPPED);

	embed = ephy_tab_get_embed (tab);
	g_return_if_fail (embed != NULL);

	g_signal_connect_after (embed, "ge-modal-alert",
				G_CALLBACK (modal_alert_cb), window);

	/* Let the extensions attach themselves to the tab */
	manager = EPHY_EXTENSION (ephy_shell_get_extensions_manager (ephy_shell));
	ephy_extension_attach_tab (manager, window, tab);
}

static void
tab_removed_cb (EphyNotebook *notebook,
		EphyTab *tab,
		EphyWindow *window)
{
	EphyExtension *manager;
	EphyEmbed *embed;

        g_return_if_fail (EPHY_IS_TAB (tab));

	/* Let the extensions remove themselves from the tab */
	manager = EPHY_EXTENSION (ephy_shell_get_extensions_manager (ephy_shell));
	ephy_extension_detach_tab (manager, window, tab);

	g_signal_handlers_disconnect_by_func (G_OBJECT (tab),
					      G_CALLBACK (sync_tab_visibility),
					      window);
	g_signal_handlers_disconnect_by_func (G_OBJECT (tab),
					      G_CALLBACK (ephy_link_open),
					      window);	

	window->priv->num_tabs--;

	if (window->priv->num_tabs > 0)
	{
		update_tabs_menu_sensitivity (window);
	}

	embed = ephy_tab_get_embed (tab);
	g_return_if_fail (embed != NULL);

	g_signal_handlers_disconnect_by_func
		(embed, G_CALLBACK (modal_alert_cb), window);
}

static void
tab_detached_cb (EphyNotebook *notebook,
		 EphyTab *tab,
		 gpointer data)
{
	EphyWindow *window;

	g_return_if_fail (EPHY_IS_TAB (tab));

	if (eel_gconf_get_boolean (CONF_LOCKDOWN_FULLSCREEN))
	{
		return;
	}

	window = ephy_window_new ();
	ephy_notebook_move_tab (notebook,
				EPHY_NOTEBOOK (ephy_window_get_notebook (window)),
				tab, 0);
	gtk_widget_show (GTK_WIDGET (window));
}

static void
tabs_reordered_cb (EphyNotebook *notebook, EphyWindow *window)
{
	update_tabs_menu_sensitivity (window);
}

static gboolean
tab_delete_cb (EphyNotebook *notebook,
	       EphyTab *tab,
	       EphyWindow *window)
{
	g_return_val_if_fail (EPHY_IS_TAB (tab), FALSE);

	if (eel_gconf_get_boolean (CONF_LOCKDOWN_DISABLE_QUIT) &&
	    gtk_notebook_get_n_pages (GTK_NOTEBOOK (window->priv->notebook)) == 1)
	{
		return TRUE;
	}
	else if (ephy_embed_has_modified_forms (ephy_tab_get_embed (tab)))
	{
		return !confirm_close_with_modified_forms (window);
	}

	return FALSE;
}

static GtkNotebook *
setup_notebook (EphyWindow *window)
{
	GtkNotebook *notebook;

	notebook = GTK_NOTEBOOK (ephy_notebook_new ());

	g_signal_connect_after (G_OBJECT (notebook), "switch_page",
				G_CALLBACK (
				ephy_window_notebook_switch_page_cb),
				window);

	g_signal_connect (notebook, "popup-menu",
			  G_CALLBACK (notebook_popup_menu_cb), window);
	g_signal_connect (notebook, "button-press-event",
			  G_CALLBACK(notebook_button_press_cb), window);

	g_signal_connect (G_OBJECT (notebook), "tab_added",
			  G_CALLBACK (tab_added_cb), window);
	g_signal_connect (G_OBJECT (notebook), "tab_removed",
			  G_CALLBACK (tab_removed_cb), window);
	g_signal_connect (G_OBJECT (notebook), "tab_detached",
			  G_CALLBACK (tab_detached_cb), NULL);
	g_signal_connect (G_OBJECT (notebook), "tabs_reordered",
			  G_CALLBACK (tabs_reordered_cb), window);
	g_signal_connect (G_OBJECT (notebook), "tab_delete",
			  G_CALLBACK (tab_delete_cb), window);

	return notebook;
}

static void
ephy_window_set_chrome (EphyWindow *window, EphyEmbedChrome mask)
{
	EphyEmbedChrome chrome_mask = mask;

	if (mask == EPHY_EMBED_CHROME_ALL)
	{
		window->priv->should_save_chrome = TRUE;
	}

	if (!eel_gconf_get_boolean (CONF_WINDOWS_SHOW_TOOLBARS))
	{
		chrome_mask &= ~EPHY_EMBED_CHROME_TOOLBAR;
	}

	if (!eel_gconf_get_boolean (CONF_WINDOWS_SHOW_STATUSBAR))
	{
		chrome_mask &= ~EPHY_EMBED_CHROME_STATUSBAR;
	}

	if (!eel_gconf_get_boolean (CONF_WINDOWS_SHOW_BOOKMARKS_BAR))
	{
		chrome_mask &= ~EPHY_EMBED_CHROME_BOOKMARKSBAR;
	}

	if (eel_gconf_get_boolean (CONF_LOCKDOWN_HIDE_MENUBAR))
	{
		chrome_mask &= ~EPHY_EMBED_CHROME_MENUBAR;
	}

	window->priv->chrome = chrome_mask;

	update_chromes_actions (window);
}

static void
ephy_window_set_is_popup (EphyWindow *window,
			  gboolean is_popup)
{
	EphyWindowPrivate *priv = window->priv;
	GtkAction *action;

	priv->is_popup = is_popup;
	ephy_notebook_set_dnd_enabled (EPHY_NOTEBOOK (priv->notebook), !is_popup);

	action = gtk_action_group_get_action (priv->action_group, "FileNewTab");
	ephy_action_change_sensitivity_flags (action, SENS_FLAG_CHROME, is_popup);

	action = gtk_action_group_get_action (priv->popups_action_group, "OpenLinkInNewTab");
	ephy_action_change_sensitivity_flags (action, SENS_FLAG_CHROME, is_popup);

	g_object_notify (G_OBJECT (window), "is-popup");
}

static void
ephy_window_dispose (GObject *object)
{
	EphyWindow *window = EPHY_WINDOW (object);
	EphyWindowPrivate *priv = window->priv;
	GObject *single;
	GSList *popups;

	LOG ("EphyWindow dispose %p", window);

	/* Only do these once */
	if (window->priv->closing == FALSE)
	{
		EphyExtension *manager;

		window->priv->closing = TRUE;

		/* Let the extensions detach themselves from the window */
		manager = EPHY_EXTENSION (ephy_shell_get_extensions_manager (ephy_shell));
		ephy_extension_detach_window (manager, window);

		/* Deactivate menus */
		popups = gtk_ui_manager_get_toplevels (window->priv->manager, GTK_UI_MANAGER_POPUP);
		g_slist_foreach (popups, (GFunc) gtk_menu_shell_deactivate, NULL);
		g_slist_free (popups);
	
		single = ephy_embed_shell_get_embed_single (embed_shell);
		g_signal_handlers_disconnect_by_func
			(single, G_CALLBACK (network_status_changed), window);
	
		eel_gconf_notification_remove (priv->browse_with_caret_notifier_id);
		eel_gconf_notification_remove (priv->allow_popups_notifier_id);

		if (priv->idle_resize_handler != 0)
		{
			g_source_remove (priv->idle_resize_handler);
			priv->idle_resize_handler = 0;
		}

		g_object_unref (priv->fav_menu);
		priv->fav_menu = NULL;

		g_object_unref (priv->enc_menu);
		priv->enc_menu = NULL;

		g_object_unref (priv->tabs_menu);
		priv->tabs_menu = NULL;

		g_object_unref (priv->bmk_menu);
		priv->bmk_menu = NULL;
	
		if (priv->ppview_toolbar)
		{
			g_object_unref (priv->ppview_toolbar);
			priv->ppview_toolbar = NULL;
		}
	
		g_object_unref (priv->action_group);
		priv->action_group = NULL;

		g_object_unref (priv->manager);
		priv->manager = NULL;
	}

	destroy_fullscreen_popup (window);

        G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
ephy_window_set_property (GObject *object,
			  guint prop_id,
			  const GValue *value,
			  GParamSpec *pspec)
{
	EphyWindow *window = EPHY_WINDOW (object);

	switch (prop_id)
	{
		case PROP_ACTIVE_TAB:
			ephy_window_set_active_tab (window, g_value_get_object (value));
			break;
		case PROP_CHROME:
			ephy_window_set_chrome (window, g_value_get_flags (value));
			break;
		case PROP_PPV_MODE:
			ephy_window_set_print_preview (window, g_value_get_boolean (value));
			break;
		case PROP_SINGLE_TAB_MODE:
			ephy_window_set_is_popup (window, g_value_get_boolean (value));
			break;
	}
}

static void
ephy_window_get_property (GObject *object,
			  guint prop_id,
			  GValue *value,
			  GParamSpec *pspec)
{
	EphyWindow *window = EPHY_WINDOW (object);

	switch (prop_id)
	{
		case PROP_ACTIVE_TAB:
			g_value_set_object (value, window->priv->active_tab);
			break;
		case PROP_CHROME:
			g_value_set_flags (value, window->priv->chrome);
			break;
		case PROP_PPV_MODE:
			g_value_set_boolean (value, window->priv->ppv_mode);
			break;
		case PROP_SINGLE_TAB_MODE:
			g_value_set_boolean (value, window->priv->is_popup);
			break;
	}
}

static gboolean
ephy_window_focus_in_event (GtkWidget *widget,
			    GdkEventFocus *event)
{
	EphyWindow *window = EPHY_WINDOW (widget);
	EphyWindowPrivate *priv = window->priv;

	if (priv->fullscreen_popup && !get_toolbar_visibility (window))
	{
		gtk_widget_show (priv->fullscreen_popup);
	}

	return GTK_WIDGET_CLASS (parent_class)->focus_in_event (widget, event);
}

static gboolean
ephy_window_focus_out_event (GtkWidget *widget,
			     GdkEventFocus *event)
{
	EphyWindow *window = EPHY_WINDOW (widget);
	EphyWindowPrivate *priv = window->priv;

	if (priv->fullscreen_popup)
	{
		gtk_widget_hide (priv->fullscreen_popup);
	}

	return GTK_WIDGET_CLASS (parent_class)->focus_out_event (widget, event);
}

static gboolean
ephy_window_state_event (GtkWidget *widget,
			 GdkEventWindowState *event)
{
	EphyWindow *window = EPHY_WINDOW (widget);
	EphyWindowPrivate *priv = window->priv;
	gboolean (* window_state_event) (GtkWidget *, GdkEventWindowState *);

	window_state_event = GTK_WIDGET_CLASS (parent_class)->window_state_event;
	if (window_state_event)
	{
		window_state_event (widget, event);
	}

	if (event->changed_mask & (GDK_WINDOW_STATE_MAXIMIZED))
	{
		gboolean show;

		show = (event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) == 0;

		gtk_statusbar_set_has_resize_grip (GTK_STATUSBAR (priv->statusbar), show);
	}

	if (event->changed_mask & GDK_WINDOW_STATE_FULLSCREEN)
	{
		GtkActionGroup *action_group;
		GtkAction *action;
		gboolean fullscreen;

		fullscreen = event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN;

		if (fullscreen)
		{
			ephy_window_fullscreen (window);
		}
		else
		{
			ephy_window_unfullscreen (window);
		}

		action_group = priv->action_group;

		action = gtk_action_group_get_action (action_group, "ViewFullscreen");
		g_signal_handlers_block_by_func
			(action, G_CALLBACK (window_cmd_view_fullscreen), window);
		gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), fullscreen);
		g_signal_handlers_unblock_by_func
			(action, G_CALLBACK (window_cmd_view_fullscreen), window);

		action = gtk_action_group_get_action (action_group, "EditToolbar");
		ephy_action_change_sensitivity_flags (action, SENS_FLAG_CHROME, fullscreen);
	}

	return FALSE;
}

static void
ephy_window_link_iface_init (EphyLinkIface *iface)
{
	iface->open_link = ephy_window_open_link;
}

static void
ephy_window_class_init (EphyWindowClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

        parent_class = g_type_class_peek_parent (klass);

	object_class->constructor = ephy_window_constructor;
	object_class->dispose = ephy_window_dispose;
        object_class->finalize = ephy_window_finalize;
	object_class->get_property = ephy_window_get_property;
	object_class->set_property = ephy_window_set_property;

	widget_class->show = ephy_window_show;
	widget_class->key_press_event = ephy_window_key_press_event;
	widget_class->focus_in_event = ephy_window_focus_in_event;
	widget_class->focus_out_event = ephy_window_focus_out_event;
	widget_class->window_state_event = ephy_window_state_event;

	g_object_class_install_property (object_class,
					 PROP_ACTIVE_TAB,
					 g_param_spec_object ("active-tab",
							      "active-tab",
							      "Active tab",
							      EPHY_TYPE_TAB,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class,
					 PROP_CHROME,
					 g_param_spec_flags ("chrome",
							     "chrome",
							     "Window chrome",
							     EPHY_TYPE_EMBED_CHROME,
							     EPHY_EMBED_CHROME_ALL,
							     G_PARAM_CONSTRUCT_ONLY |
							     G_PARAM_READWRITE));

	g_object_class_install_property (object_class,
					 PROP_PPV_MODE,
					 g_param_spec_boolean ("print-preview-mode",
							       "Print preview mode",
							       "Whether the window is in print preview mode",
							       FALSE,
							       G_PARAM_READWRITE));

	g_object_class_install_property (object_class,
					 PROP_SINGLE_TAB_MODE,
					 g_param_spec_boolean ("is-popup",
							       "Is Popup",
							       "Whether the window is a popup",
							       FALSE,
							       G_PARAM_READWRITE |
							       G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (object_class, sizeof (EphyWindowPrivate));
}

static void
browse_with_caret_notifier (GConfClient *client,
			    guint cnxn_id,
			    GConfEntry *entry,
			    EphyWindow *window)
{
	GtkAction *action;

	action = gtk_action_group_get_action (window->priv->action_group,
					      "BrowseWithCaret");
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action),
				      eel_gconf_get_boolean (CONF_BROWSE_WITH_CARET));
}

static void
allow_popups_notifier (GConfClient *client,
		       guint cnxn_id,
		       GConfEntry *entry,
		       EphyWindow *window)
{
	GList *tabs;
	EphyTab *tab;

	g_return_if_fail (EPHY_IS_WINDOW (window));

	tabs = ephy_window_get_tabs (window);

	for (; tabs; tabs = g_list_next (tabs))
	{
		tab = EPHY_TAB (tabs->data);
		g_return_if_fail (EPHY_IS_TAB (tab));

		g_object_notify (G_OBJECT (tab), "popups-allowed");
	}
}

static void
action_request_forward_cb (GObject *toolbar,
			   const char *name,
                	   GObject *bookmarksbar)
{
	g_signal_emit_by_name (bookmarksbar, "action_request", name);
}

static EphyTab *
ephy_window_open_link (EphyLink *link,
		       const char *address,
		       EphyTab *tab,
		       EphyLinkFlags flags)
{
	EphyWindow *window = EPHY_WINDOW (link);
	EphyWindowPrivate *priv = window->priv;
	EphyTab *new_tab;

	g_return_val_if_fail (address != NULL, NULL);

	/* don't do anything in ppv mode */
	if (window->priv->ppv_mode) return NULL;

	if (tab == NULL)
	{
		tab = ephy_window_get_active_tab (window);
	}

	if (flags != 0)
	{
		EphyNewTabFlags ntflags = EPHY_NEW_TAB_OPEN_PAGE;

		if (flags & EPHY_LINK_JUMP_TO)
		{
			ntflags |= EPHY_NEW_TAB_JUMP;
		}
		if (flags & EPHY_LINK_NEW_WINDOW ||
		    (flags & EPHY_LINK_NEW_TAB && priv->is_popup))
		{
			ntflags |= EPHY_NEW_TAB_IN_NEW_WINDOW;
		}
		else
		{
			ntflags |= EPHY_NEW_TAB_IN_EXISTING_WINDOW;
		}

		new_tab = ephy_shell_new_tab
				(ephy_shell,
				 EPHY_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (tab))),
				 tab, address, ntflags);
	}
	else
	{
		EphyEmbed *embed;
		
		embed = ephy_tab_get_embed (tab);

		ephy_embed_load_url (embed, address);

		if (address == NULL || address[0] == '\0' || strcmp (address, "about:blank") == 0)
		{
			ephy_toolbar_activate_location (priv->toolbar);
		}
		else
		{
			gtk_widget_grab_focus (GTK_WIDGET (embed));
		}

		new_tab = tab;
	}

	return new_tab;
}

static void
find_toolbar_close_cb (EphyFindToolbar *toolbar,
		       EphyWindow *window)
{
	gtk_widget_hide (GTK_WIDGET (toolbar));
}

static void
ephy_window_init (EphyWindow *window)
{
	EphyWindowPrivate *priv;
	EphyExtension *manager;
	EphyEmbedSingle *single;
	EggToolbarsModel *model;
	GError *error = NULL;

	LOG ("EphyWindow initialising %p", window);

	g_object_ref (ephy_shell);

	priv = window->priv = EPHY_WINDOW_GET_PRIVATE (window);

	window->priv->chrome = EPHY_EMBED_CHROME_ALL;

	ephy_gui_ensure_window_group (GTK_WINDOW (window));

	/* Setup the UI manager and connect verbs */
	setup_ui_manager (window);

	window->priv->notebook = setup_notebook (window);
	g_signal_connect_swapped (window->priv->notebook, "open-link",
				  G_CALLBACK (ephy_link_open), window);
	gtk_box_pack_start (GTK_BOX (window->priv->main_vbox),
			    GTK_WIDGET (window->priv->notebook),
			    TRUE, TRUE, 0);
	gtk_widget_show (GTK_WIDGET (window->priv->notebook));

	priv->find_toolbar = ephy_find_toolbar_new (window);
	g_signal_connect (priv->find_toolbar, "close",
			  G_CALLBACK (find_toolbar_close_cb), window);
	gtk_box_pack_start (GTK_BOX (window->priv->main_vbox),
			    GTK_WIDGET (priv->find_toolbar), FALSE, FALSE, 0);
	/* don't show the find toolbar here! */
	
	window->priv->statusbar = ephy_statusbar_new ();
	gtk_box_pack_end (GTK_BOX (window->priv->main_vbox),
			  GTK_WIDGET (window->priv->statusbar),
			  FALSE, TRUE, 0);
	window->priv->tab_message_cid = gtk_statusbar_get_context_id
		(GTK_STATUSBAR (window->priv->statusbar), "tab_message");
	window->priv->help_message_cid = gtk_statusbar_get_context_id
		(GTK_STATUSBAR (window->priv->statusbar), "help_message");

	/* get the toolbars model *before* getting the bookmarksbar model
	 * (via ephy_bookmarsbar_new()), so that the toolbars model is
	 * instantiated *before* the bookmarksbarmodel, to make forwarding
	 * works. See bug #151267.
	 */
	model= EGG_TOOLBARS_MODEL (ephy_shell_get_toolbars_model (ephy_shell, FALSE));

	/* create the toolbars */
	window->priv->toolbar = ephy_toolbar_new (window);
	g_signal_connect_swapped (window->priv->toolbar, "open-link",
				  G_CALLBACK (ephy_link_open), window);
	g_signal_connect_swapped (window->priv->toolbar, "exit-clicked",
				  G_CALLBACK (exit_fullscreen_clicked_cb), window);
	window->priv->bookmarksbar = ephy_bookmarksbar_new (window);
	g_signal_connect_swapped (window->priv->bookmarksbar, "open-link",
				  G_CALLBACK (ephy_link_open), window);

	g_signal_connect_swapped (window->priv->toolbar, "activation-finished",
				  G_CALLBACK (sync_chromes_visibility), window);

	/* now load the UI definition */
	gtk_ui_manager_add_ui_from_file
		(window->priv->manager, ephy_file ("epiphany-ui.xml"), &error);
	if (error != NULL)
	{
		g_warning ("Could not merge epiphany-ui.xml: %s", error->message);
		g_error_free (error);
        }

	/* Initialize the menus */
	window->priv->tabs_menu = ephy_tabs_menu_new (window);
	window->priv->enc_menu = ephy_encoding_menu_new (window);
	window->priv->fav_menu = ephy_favorites_menu_new (window);
	g_signal_connect_swapped (window->priv->fav_menu, "open-link",
				  G_CALLBACK (ephy_link_open), window);
	window->priv->bmk_menu = ephy_bookmarks_menu_new (window->priv->manager,
							  BOOKMARKS_MENU_PATH);
	g_signal_connect_swapped (window->priv->bmk_menu, "open-link",
				  G_CALLBACK (ephy_link_open), window);

	/* forward the toolbar's action_request signal to the bookmarks toolbar,
	 * so the user can also have bookmarks on the normal toolbar
	 */
	g_signal_connect (window->priv->toolbar, "action_request",
			  G_CALLBACK (action_request_forward_cb),
			  window->priv->bookmarksbar);

	/* Add the toolbars to the window */
	gtk_box_pack_end (GTK_BOX (window->priv->menu_dock),
			  window->priv->bookmarksbar,
			  FALSE, FALSE, 0);
	gtk_box_pack_end (GTK_BOX (window->priv->menu_dock),
			  GTK_WIDGET (window->priv->toolbar),
			  FALSE, FALSE, 0);

	/* Once the window is sufficiently created let the extensions attach to it */
	manager = EPHY_EXTENSION (ephy_shell_get_extensions_manager (ephy_shell));
	ephy_extension_attach_window (manager, window);

	/* We only set the model now after attaching the extensions, so that
	 * extensions already have created their actions which may be on
	 * the toolbar
	 */
	egg_editable_toolbar_set_model
		(EGG_EDITABLE_TOOLBAR (window->priv->toolbar), model);

	g_signal_connect (window, "delete-event",
			  G_CALLBACK (ephy_window_delete_event_cb),
			  window);

	/* other notifiers */
	browse_with_caret_notifier (NULL, 0, NULL, window);
	window->priv->browse_with_caret_notifier_id = eel_gconf_notification_add
		(CONF_BROWSE_WITH_CARET,
		 (GConfClientNotifyFunc)browse_with_caret_notifier, window);

	window->priv->allow_popups_notifier_id = eel_gconf_notification_add
		(CONF_SECURITY_ALLOW_POPUPS,
		 (GConfClientNotifyFunc)allow_popups_notifier, window);

	/* network status */
	single = EPHY_EMBED_SINGLE (ephy_embed_shell_get_embed_single (embed_shell));
	network_status_changed (single,
			        ephy_embed_single_get_offline_mode (single),
				window);
	g_signal_connect (single, "network-status",
			  G_CALLBACK (network_status_changed), window);

	/* ensure the UI is updated */
	gtk_ui_manager_ensure_update (window->priv->manager);

	init_menu_updaters (window);
}

static GObject *
ephy_window_constructor (GType type,
			 guint n_construct_properties,
			 GObjectConstructParam *construct_params)
{
	GObject *object;
	EphyWindow *window;

	object = parent_class->constructor (type, n_construct_properties,
					    construct_params);

	window = EPHY_WINDOW (object);

	sync_chromes_visibility (window);

	return object;
}

static void
ephy_window_finalize (GObject *object)
{
        G_OBJECT_CLASS (parent_class)->finalize (object);

	LOG ("Ephy Window finalized %p", object);

	g_object_unref (ephy_shell);
}

/**
 * ephy_window_new:
 *
 * Equivalent to g_object_new() but returns an #EphyWindow so you don't have
 * to cast it.
 *
 * Return value: a new #EphyWindow
 **/
EphyWindow *
ephy_window_new (void)
{
	return EPHY_WINDOW (g_object_new (EPHY_TYPE_WINDOW, NULL));
}

/**
 * ephy_window_new_with_chrome:
 * @chrome: an #EphyEmbedChrome
 * @is_popup: whether the new window is a popup window
 *
 * Identical to ephy_window_new(), but allows you to specify a chrome.
 *
 * Return value: a new #EphyWindow
 **/
EphyWindow *
ephy_window_new_with_chrome (EphyEmbedChrome chrome,
			     gboolean is_popup)
{
	return EPHY_WINDOW (g_object_new (EPHY_TYPE_WINDOW,
					  "chrome", chrome,
					  "is-popup", is_popup,
					  NULL));
}

/**
 * ephy_window_set_print_preview:
 * @window: an #EphyWindow
 * @enabled: %TRUE to enable print preview mode
 *
 * Sets whether the window is in print preview mode.
 **/
void
ephy_window_set_print_preview (EphyWindow *window,
			       gboolean enabled)
{
	EphyWindowPrivate *priv = window->priv;
	GtkAccelGroup *accel_group;

	accel_group = gtk_ui_manager_get_accel_group (window->priv->manager);

	if (priv->ppv_mode == enabled) return;

	priv->ppv_mode = enabled;

	sync_chromes_visibility (window);

	if (enabled)
	{
		g_return_if_fail (priv->ppview_toolbar == NULL);

		priv->ppview_toolbar = ppview_toolbar_new (window);
		gtk_window_remove_accel_group (GTK_WINDOW (window), accel_group);
	}
	else
	{
		g_return_if_fail (priv->ppview_toolbar != NULL);

		g_object_unref (priv->ppview_toolbar);
		priv->ppview_toolbar = NULL;
		gtk_window_add_accel_group (GTK_WINDOW (window), accel_group);
	}

	g_object_notify (G_OBJECT (window), "print-preview-mode");
}

/**
 * ephy_window_get_ui_manager:
 * @window: an #EphyWindow
 *
 * Returns this window's UI manager.
 *
 * Return value: an #GtkUIManager
 **/
GObject *
ephy_window_get_ui_manager (EphyWindow *window)
{
	g_return_val_if_fail (EPHY_IS_WINDOW (window), NULL);

	return G_OBJECT (window->priv->manager);
}

/**
 * ephy_window_get_toolbar:
 * @window: an #EphyWindow
 *
 * Returns this window's toolbar as an #EggEditableToolbar.
 *
 * Return value: an #EggEditableToolbar
 **/
GtkWidget *
ephy_window_get_toolbar (EphyWindow *window)
{
	g_return_val_if_fail (EPHY_IS_WINDOW (window), NULL);

	return GTK_WIDGET (window->priv->toolbar);
}

/**
 * ephy_window_get_bookmarksbar:
 * @window: an #EphyWindow
 *
 * Returns this window's bookmarks toolbar, which is an #EggEditableToolbar.
 *
 * Return value: an #EggEditableToolbar
 **/
GtkWidget *
ephy_window_get_bookmarksbar (EphyWindow *window)
{
	g_return_val_if_fail (EPHY_IS_WINDOW (window), NULL);

	return GTK_WIDGET (window->priv->bookmarksbar);
}

/**
 * ephy_window_get_notebook:
 * @window: an #EphyWindow
 *
 * Returns the #GtkNotebook used by this window.
 *
 * Return value: the @window's #GtkNotebook
 **/
GtkWidget *
ephy_window_get_notebook (EphyWindow *window)
{
	g_return_val_if_fail (EPHY_IS_WINDOW (window), NULL);

	return GTK_WIDGET (window->priv->notebook);
}

/**
 * ephy_window_get_find_toolbar:
 * @window: an #EphyWindow
 *
 * Returns the #EphyFindToolbar used by this window.
 *
 * Return value: the @window's #EphyFindToolbar
 **/
GtkWidget *
ephy_window_get_find_toolbar (EphyWindow *window)
{
	g_return_val_if_fail (EPHY_IS_WINDOW (window), NULL);

	return GTK_WIDGET (window->priv->find_toolbar);
}

/**
 * ephy_window_get_statusbar:
 * @window: an #EphyWindow
 *
 * Returns this window's statusbar as an #EphyStatusbar.
 *
 * Return value: This window's statusbar
 **/
GtkWidget *
ephy_window_get_statusbar (EphyWindow *window)
{
	g_return_val_if_fail (EPHY_IS_WINDOW (window), NULL);

	return GTK_WIDGET (window->priv->statusbar);
}

/**
 * ephy_window_add_tab:
 * @window: an #EphyWindow
 * @tab: an #EphyTab
 * @position: the position in @window's #GtkNotebook
 * @jump_to: %TRUE to switch to @tab's new notebook page after insertion
 *
 * Inserts @tab into @window.
 **/
void
ephy_window_add_tab (EphyWindow *window,
		     EphyTab *tab,
		     gint position,
		     gboolean jump_to)
{
	GtkWidget *widget;

	g_return_if_fail (EPHY_IS_WINDOW (window));
	g_return_if_fail (EPHY_IS_TAB (tab));
	g_return_if_fail (!window->priv->is_popup ||
			  gtk_notebook_get_n_pages (GTK_NOTEBOOK (window->priv->notebook)) < 1);

	widget = GTK_WIDGET(ephy_tab_get_embed (tab));

	ephy_notebook_add_tab (EPHY_NOTEBOOK (window->priv->notebook),
			       tab, position, jump_to);
}

/**
 * ephy_window_jump_to_tab:
 * @window: an #EphyWindow
 * @tab: an #EphyTab inside @window
 *
 * Switches @window's #GtkNotebook to open @tab as its current page.
 **/
void
ephy_window_jump_to_tab (EphyWindow *window,
			 EphyTab *tab)
{
	int page;

	page = gtk_notebook_page_num
		(window->priv->notebook, GTK_WIDGET (tab));
	gtk_notebook_set_current_page
		(window->priv->notebook, page);
}

static EphyTab *
real_get_active_tab (EphyWindow *window, int page_num)
{
	GtkWidget *tab;

	if (page_num == -1)
	{
		page_num = gtk_notebook_get_current_page (window->priv->notebook);
	}
	tab = gtk_notebook_get_nth_page (window->priv->notebook, page_num);

	g_return_val_if_fail (EPHY_IS_TAB (tab), NULL);

	return EPHY_TAB (tab);
}

/**
 * ephy_window_remove_tab:
 * @window: an #EphyWindow
 * @tab: an #EphyTab
 *
 * Removes @tab from @window.
 **/
void
ephy_window_remove_tab (EphyWindow *window,
		        EphyTab *tab)
{
	EphyEmbed *embed;
	gboolean modified;

	g_return_if_fail (EPHY_IS_WINDOW (window));
	g_return_if_fail (EPHY_IS_TAB (tab));

	embed = ephy_tab_get_embed (tab);
	g_return_if_fail (EPHY_IS_EMBED (embed));

	modified = ephy_embed_has_modified_forms (embed);
	if (ephy_embed_has_modified_forms (embed)
	    && confirm_close_with_modified_forms (window) == FALSE)
	{
		/* don't close the tab */
		return;
	}

	ephy_notebook_remove_tab (EPHY_NOTEBOOK (window->priv->notebook), tab);
}

/**
 * ephy_window_load_url:
 * @window: a #EphyWindow
 * @url: the url to load
 *
 * Loads a new url in the active tab of @window.
 * Unlike ephy_embed_load_url(), this function activates
 * the embed.
 *
 **/
void
ephy_window_load_url (EphyWindow *window,
		      const char *url)
{
	g_return_if_fail (url != NULL);

	ephy_link_open (EPHY_LINK (window), url, NULL, 0);
}

/**
 * ephy_window_activate_location:
 * @window: an #EphyWindow
 *
 * Activates the location entry on @window's toolbar.
 **/
void
ephy_window_activate_location (EphyWindow *window)
{
	if (window->priv->fullscreen_popup)
	{
		gtk_widget_hide (window->priv->fullscreen_popup);
	}

	ephy_toolbar_activate_location (window->priv->toolbar);
}

static void
ephy_window_show (GtkWidget *widget)
{
	EphyWindow *window = EPHY_WINDOW(widget);
	EphyWindowPrivate *priv = window->priv;

	if (!priv->has_size)
	{
		EphyTab *tab;
		int width, height;

		tab = ephy_window_get_active_tab (EPHY_WINDOW (window));
		g_return_if_fail (tab != NULL);

		ephy_tab_get_size (tab, &width, &height);
		if (width == -1 && height == -1)
		{
			ephy_state_add_window (widget, "main_window", 600, 500,
					       TRUE, EPHY_STATE_WINDOW_SAVE_SIZE);
		}

		priv->has_size = TRUE;
	}

	GTK_WIDGET_CLASS (parent_class)->show (widget);
}

/**
 * ephy_window_get_active_tab:
 * @window: an #EphyWindow
 *
 * Returns @window's active #EphyTab.
 *
 * Return value: @window's active tab
 **/
EphyTab *
ephy_window_get_active_tab (EphyWindow *window)
{
	g_return_val_if_fail (EPHY_IS_WINDOW (window), NULL);

	return window->priv->active_tab;
}

/**
 * ephy_window_get_active_embed:
 * @window: an #EphyWindow
 *
 * Return @window's active #EphyEmbed. This is identical to calling
 * ephy_window_get_active_tab() followed by ephy_tab_get_embed().
 *
 * Return value: @window's active embed
 **/
EphyEmbed *
ephy_window_get_active_embed (EphyWindow *window)
{
	EphyTab *tab;

	g_return_val_if_fail (EPHY_IS_WINDOW (window), NULL);

	tab = ephy_window_get_active_tab (window);
	if (tab == NULL) return NULL;

	return ephy_tab_get_embed (tab);
}

/**
 * ephy_window_get_tabs:
 * @window: a #EphyWindow
 *
 * Returns the list of #EphyTab:s in the window.
 *
 * Return value: a newly-allocated list of #EphyTab:s
 */
GList *
ephy_window_get_tabs (EphyWindow *window)
{
	g_return_val_if_fail (EPHY_IS_WINDOW (window), NULL);

	return gtk_container_get_children (GTK_CONTAINER (window->priv->notebook));
}

static void
ephy_window_notebook_switch_page_cb (GtkNotebook *notebook,
				     GtkNotebookPage *page,
				     guint page_num,
				     EphyWindow *window)
{
	EphyWindowPrivate *priv = window->priv;
	EphyTab *tab;
	EphyEmbed *embed;

	if (priv->closing) return;

	/* get the new tab */
	tab = real_get_active_tab (window, page_num);
	embed = ephy_tab_get_embed (tab);

	/* update new tab */
	ephy_window_set_active_tab (window, tab);

	ephy_find_toolbar_set_embed (priv->find_toolbar, embed);

	/* update window controls */
	update_tabs_menu_sensitivity (window);
}

/**
 * ephy_window_set_zoom:
 * @window: an #EphyWindow
 * @zoom: the desired zoom level
 *
 * Sets the zoom on @window's active #EphyEmbed. A @zoom of 1.0 corresponds to
 * 100% zoom (normal size).
 **/
void
ephy_window_set_zoom (EphyWindow *window,
		      float zoom)
{
	EphyEmbed *embed;
	float current_zoom = 1.0;

        g_return_if_fail (EPHY_IS_WINDOW (window));

	embed = ephy_window_get_active_embed (window);
        g_return_if_fail (embed != NULL);

	current_zoom = ephy_embed_get_zoom (embed);

	if (zoom == ZOOM_IN)
	{
		zoom = ephy_zoom_get_changed_zoom_level (current_zoom, 1);
	}
	else if (zoom == ZOOM_OUT)
	{
		zoom = ephy_zoom_get_changed_zoom_level (current_zoom, -1);
	}

	if (zoom != current_zoom)
	{
		ephy_embed_set_zoom (embed, zoom);
	}
}

static void
sync_prefs_with_chrome (EphyWindow *window)
{
	EphyEmbedChrome flags = window->priv->chrome;

	if (window->priv->should_save_chrome)
	{
		eel_gconf_set_boolean (CONF_WINDOWS_SHOW_BOOKMARKS_BAR,
				       flags & EPHY_EMBED_CHROME_BOOKMARKSBAR);
		eel_gconf_set_boolean (CONF_WINDOWS_SHOW_TOOLBARS,
				       flags & EPHY_EMBED_CHROME_TOOLBAR);
		eel_gconf_set_boolean (CONF_WINDOWS_SHOW_STATUSBAR,
				       flags & EPHY_EMBED_CHROME_STATUSBAR);
	}
}

static void
sync_chrome_with_view_toggle (GtkAction *action, EphyWindow *window,
			      EphyEmbedChrome chrome_flag)
{
	gboolean active;

	active = gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action));
	window->priv->chrome = active ? window->priv->chrome | chrome_flag :
					window->priv->chrome & (~chrome_flag);

	sync_chromes_visibility (window);
	sync_prefs_with_chrome (window);
}

static void
ephy_window_view_statusbar_cb (GtkAction *action,
			       EphyWindow *window)
{
	sync_chrome_with_view_toggle (action, window,
				      EPHY_EMBED_CHROME_STATUSBAR);
}

static void
ephy_window_view_toolbar_cb (GtkAction *action,
			     EphyWindow *window)
{
	sync_chrome_with_view_toggle (action, window,
				      EPHY_EMBED_CHROME_TOOLBAR);
}

static void
ephy_window_view_bookmarksbar_cb (GtkAction *action,
			          EphyWindow *window)
{
	sync_chrome_with_view_toggle (action, window,
				      EPHY_EMBED_CHROME_BOOKMARKSBAR);
}

static void
ephy_window_view_popup_windows_cb (GtkAction *action,
				   EphyWindow *window)
{
	EphyTab *tab;
	gboolean allow;

	g_return_if_fail (EPHY_IS_WINDOW (window));

	tab = ephy_window_get_active_tab (window);
	g_return_if_fail (EPHY_IS_TAB (tab));

	if (gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)))
	{
		allow = TRUE;
	}
	else
	{
		allow = FALSE;
	}

	g_object_set (G_OBJECT (tab), "popups-allowed", allow, NULL);
}

/**
 * ephy_window_get_is_popup:
 * @window: an #EphyWindow
 *
 * Returns whether this window is a popup window.
 *
 * Return value: %TRUE if it is a popup window
 **/
gboolean
ephy_window_get_is_popup (EphyWindow *window)
{
	g_return_val_if_fail (EPHY_IS_WINDOW (window), FALSE);

	return window->priv->is_popup;
}

/**
 * ephy_window_get_is_print_preview:
 * @window: an #EphyWindow
 *
 * Returns whether this window is in print preview mode.
 *
 * Return value: %TRUE if it is in print preview mode
 **/
gboolean
ephy_window_get_is_print_preview (EphyWindow *window)
{
	g_return_val_if_fail (EPHY_IS_WINDOW (window), FALSE);

	return window->priv->ppv_mode;
}
