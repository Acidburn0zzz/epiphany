/*
 *  Copyright (C) 2003 Marco Pesenti Gritti <mpeseng@tin.it>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtktable.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkstock.h>
#include <gtk/gtkscrolledwindow.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtkactiongroup.h>
#include <gtk/gtktoggleaction.h>
#include <gtk/gtkcombobox.h>
#include <gtk/gtkuimanager.h>
#include <gdk/gdkkeysyms.h>
#include <glib/gi18n.h>
#include <libgnomeui/gnome-stock-icons.h>
#include <string.h>
#include <time.h>

#include "ephy-node-view.h"
#include "ephy-window.h"
#include "ephy-history-window.h"
#include "ephy-shell.h"
#include "ephy-dnd.h"
#include "ephy-state.h"
#include "window-commands.h"
#include "ephy-file-helpers.h"
#include "ephy-debug.h"
#include "ephy-new-bookmark.h"
#include "ephy-stock-icons.h"
#include "ephy-gui.h"
#include "toolbar.h"
#include "ephy-stock-icons.h"
#include "ephy-search-entry.h"
#include "ephy-session.h"
#include "ephy-favicon-cache.h"
#include "eel-gconf-extensions.h"

static GtkTargetEntry page_drag_types [] =
{
        { EPHY_DND_URI_LIST_TYPE,   0, 0 },
        { EPHY_DND_TEXT_TYPE,       0, 1 },
        { EPHY_DND_URL_TYPE,        0, 2 }
};
static int n_page_drag_types = G_N_ELEMENTS (page_drag_types);

static void ephy_history_window_class_init (EphyHistoryWindowClass *klass);
static void ephy_history_window_init (EphyHistoryWindow *editor);
static void ephy_history_window_finalize (GObject *object);
static void ephy_history_window_set_property (GObject *object,
		                              guint prop_id,
		                              const GValue *value,
		                              GParamSpec *pspec);
static void ephy_history_window_get_property (GObject *object,
					      guint prop_id,
					      GValue *value,
					      GParamSpec *pspec);
static void ephy_history_window_dispose      (GObject *object);

static void cmd_open_bookmarks_in_tabs    (GtkAction *action,
					   EphyHistoryWindow *editor);
static void cmd_open_bookmarks_in_browser (GtkAction *action,
					   EphyHistoryWindow *editor);
static void cmd_delete                    (GtkAction *action,
                                           EphyHistoryWindow *editor);
static void cmd_bookmark_link             (GtkAction *action,
                                           EphyHistoryWindow *editor);
static void cmd_clear			  (GtkAction *action,
				           EphyHistoryWindow *editor);
static void cmd_close			  (GtkAction *action,
					   EphyHistoryWindow *editor);
static void cmd_cut			  (GtkAction *action,
					   EphyHistoryWindow *editor);
static void cmd_copy			  (GtkAction *action,
					   EphyHistoryWindow *editor);
static void cmd_paste			  (GtkAction *action,
					   EphyHistoryWindow *editor);
static void cmd_select_all		  (GtkAction *action,
					   EphyHistoryWindow *editor);
static void cmd_help_contents		  (GtkAction *action,
					   EphyHistoryWindow *editor);

#define EPHY_HISTORY_WINDOW_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE ((object), EPHY_TYPE_HISTORY_WINDOW, EphyHistoryWindowPrivate))

#define CONF_HISTORY_DATE_FILTER "/apps/epiphany/dialogs/history_date_filter"
#define CONF_HISTORY_VIEW_DETAILS "/apps/epiphany/dialogs/history_view_details"

struct EphyHistoryWindowPrivate
{
	EphyHistory *history;
	GtkWidget *sites_view;
	GtkWidget *pages_view;
	EphyNodeFilter *pages_filter;
	EphyNodeFilter *sites_filter;
	GtkWidget *time_combo;
	GtkWidget *search_entry;
	GtkWidget *main_vbox;
	GtkWidget *window;
	GtkUIManager *ui_merge;
	GtkActionGroup *action_group;
	GtkWidget *confirmation_dialog;
	EphyNode *selected_site;
	GtkTreeViewColumn *title_col;
	GtkTreeViewColumn *address_col;
};

enum
{
	PROP_0,
	PROP_HISTORY
};

enum
{
	TIME_TODAY,
	TIME_LAST_TWO_DAYS,
	TIME_LAST_THREE_DAYS,
	TIME_EVER
};

#define TIME_EVER_STRING "ever"
#define TIME_TODAY_STRING "today"
#define TIME_LAST_TWO_DAYS_STRING "last_two_days"
#define TIME_LAST_THREE_DAYS_STRING "last_three_days"

static GObjectClass *parent_class = NULL;

static GtkActionEntry ephy_history_ui_entries [] = {
	/* Toplevel */
	{ "File", NULL, N_("_File") },
	{ "Edit", NULL, N_("_Edit") },
	{ "View", NULL, N_("_View") },
	{ "Help", NULL, N_("_Help") },
	{ "PopupAction", NULL, "" },

	/* File Menu */
	{ "OpenInWindow", GTK_STOCK_OPEN, N_("_Open in New Window"), "<control>O",
	  N_("Open the selected history link in a new window"),
	  G_CALLBACK (cmd_open_bookmarks_in_browser) },
	{ "OpenInTab", NULL, N_("Open in New _Tab"), "<shift><control>O",
	  N_("Open the selected history link in a new tab"),
	  G_CALLBACK (cmd_open_bookmarks_in_tabs) },
	{ "Delete", GTK_STOCK_DELETE, N_("_Delete"), NULL,
	  N_("Delete the selected history link"),
	  G_CALLBACK (cmd_delete) },
	{ "BookmarkLink", STOCK_ADD_BOOKMARK, N_("Boo_kmark Link..."), "<control>D",
	  N_("Bookmark the selected history link"),
	  G_CALLBACK (cmd_bookmark_link) },
	{ "Close", GTK_STOCK_CLOSE, N_("_Close"), "<control>W",
	  N_("Close the history window"),
	  G_CALLBACK (cmd_close) },

	/* Edit Menu */
	{ "Cut", GTK_STOCK_CUT, N_("Cu_t"), "<control>X",
	  N_("Cut the selection"),
	  G_CALLBACK (cmd_cut) },
	{ "Copy", GTK_STOCK_COPY, N_("_Copy"), "<control>C",
	  N_("Copy the selection"),
	  G_CALLBACK (cmd_copy) },
	{ "Paste", GTK_STOCK_PASTE, N_("_Paste"), "<control>V",
	  N_("Paste the clipboard"),
	  G_CALLBACK (cmd_paste) },
	{ "SelectAll", NULL, N_("Select _All"), "<control>A",
	  N_("Select all history links or text"),
	  G_CALLBACK (cmd_select_all) },
	{ "Clear", GTK_STOCK_CLEAR, N_("C_lear History"), NULL,
	  N_("Clear your browsing history"),
	  G_CALLBACK (cmd_clear) },

	/* Help Menu */
	{ "HelpContents", GTK_STOCK_HELP, N_("_Contents"), "F1",
	  N_("Display history help"),
	  G_CALLBACK (cmd_help_contents) },
	{ "HelpAbout", GNOME_STOCK_ABOUT, N_("_About"), NULL,
	  N_("Display credits for the web browser creators"),
	  G_CALLBACK (window_cmd_help_about) },
};
static guint ephy_history_ui_n_entries = G_N_ELEMENTS (ephy_history_ui_entries);

enum
{
	VIEW_TITLE,
	VIEW_ADDRESS,
	VIEW_TITLE_AND_ADDRESS
};

static GtkRadioActionEntry ephy_history_radio_entries [] =
{
	/* View Menu */
	{ "ViewTitle", NULL, N_("_Title"), NULL,
	  N_("Show only the title column"), VIEW_TITLE },
	{ "ViewAddress", NULL, N_("_Address"), NULL,
	  N_("Show only the address column"), VIEW_ADDRESS },
	{ "ViewTitleAddress", NULL, N_("T_itle and Address"), NULL,
	  N_("Show both the title and address columns"),
	  VIEW_TITLE_AND_ADDRESS } 
};
static guint ephy_history_n_radio_entries = G_N_ELEMENTS (ephy_history_radio_entries);


static void
confirmation_dialog_response_cb (GtkDialog *dialog, gint response,
				 EphyHistoryWindow *editor)
{
	gtk_widget_destroy (GTK_WIDGET (dialog));

	if (response != GTK_RESPONSE_OK)
		return;

	ephy_history_clear (editor->priv->history);
}

static GtkWidget *
confirmation_dialog_construct (EphyHistoryWindow *editor)
{
	GtkWidget *dialog;
	GtkWidget *label;
	GtkWidget *vbox;
	GtkWidget *hbox;
	GtkWidget *image;
	GtkWidget *button;
	GtkWidget *align;
	char *str;

	dialog = gtk_dialog_new_with_buttons (_("Clear History"),
					     GTK_WINDOW (editor),
					     GTK_DIALOG_DESTROY_WITH_PARENT |
					     GTK_DIALOG_NO_SEPARATOR,
					     NULL);
	gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
	gtk_container_set_border_width (GTK_CONTAINER (dialog), 6);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 12);

	gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

	button = gtk_button_new ();
	gtk_widget_show (button);
	gtk_dialog_add_action_widget (GTK_DIALOG (dialog), button, GTK_RESPONSE_OK);
	GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);

	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

	align = gtk_alignment_new (0.5, 0.5, 0.0, 0.0);
	gtk_widget_show (align);
	gtk_container_add (GTK_CONTAINER (button), align);

	hbox = gtk_hbox_new (FALSE, 2);
	gtk_widget_show (hbox);
	gtk_container_add (GTK_CONTAINER (align), hbox);

	image = gtk_image_new_from_stock (GTK_STOCK_CLEAR, GTK_ICON_SIZE_BUTTON);
	gtk_widget_show (image);
	gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);

	label = gtk_label_new_with_mnemonic (_("C_lear"));
	gtk_widget_show (label);
	gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

	hbox = gtk_hbox_new (FALSE, 12);
	gtk_container_set_border_width (GTK_CONTAINER (hbox), 6);
	gtk_widget_show (hbox);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), hbox,
			    TRUE, TRUE, 0);

	image = gtk_image_new_from_stock (GTK_STOCK_DIALOG_WARNING,
					  GTK_ICON_SIZE_DIALOG);
	gtk_misc_set_alignment (GTK_MISC (image), 0.5, 0.0);
	gtk_widget_show (image);
	gtk_box_pack_start (GTK_BOX (hbox), image, TRUE, TRUE, 0);

	vbox = gtk_vbox_new (FALSE, 6);
	gtk_widget_show (vbox);
	gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, TRUE, 0);

	label = gtk_label_new (NULL);
	gtk_label_set_selectable (GTK_LABEL (label), TRUE);
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	str = g_strconcat ("<b><big>", _("Clear browsing history?"),
			   "</big></b>", NULL);
	gtk_label_set_markup (GTK_LABEL (label), str);
	g_free (str);
	gtk_box_pack_start (GTK_BOX (vbox), label, TRUE, TRUE, 0);
	gtk_widget_show (label);

	label = gtk_label_new (_("Clearing the browsing history will cause all"
				 " history links to be permanently deleted."));
	gtk_label_set_selectable (GTK_LABEL (label), TRUE);
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_box_pack_start (GTK_BOX (vbox), label, TRUE, TRUE, 0);
	gtk_widget_show (label);

	g_signal_connect (dialog, "response",
			  G_CALLBACK (confirmation_dialog_response_cb),
			  editor);

	return dialog;
}

static void
cmd_clear (GtkAction *action,
	   EphyHistoryWindow *editor)
{
	if (editor->priv->confirmation_dialog == NULL)
	{
		editor->priv->confirmation_dialog = confirmation_dialog_construct (editor);
		g_object_add_weak_pointer (G_OBJECT(editor->priv->confirmation_dialog),
					   (gpointer *)&editor->priv->confirmation_dialog);
	}

	gtk_widget_show (editor->priv->confirmation_dialog);
}

static void
cmd_close (GtkAction *action,
	   EphyHistoryWindow *editor)
{
	if (editor->priv->confirmation_dialog != NULL)
	{
		gtk_widget_destroy (editor->priv->confirmation_dialog);
	}
	gtk_widget_hide (GTK_WIDGET (editor));
}

static GtkWidget *
get_target_window (EphyHistoryWindow *editor)
{
	if (editor->priv->window)
	{
		return editor->priv->window;
	}
	else
	{
		EphySession *session;

		session = EPHY_SESSION (ephy_shell_get_session (ephy_shell));
		return GTK_WIDGET (ephy_session_get_active_window (session));
	}
}

static void
cmd_open_bookmarks_in_tabs (GtkAction *action,
			    EphyHistoryWindow *editor)
{
	EphyWindow *window;
	GList *selection;
	GList *l;

	window = EPHY_WINDOW (get_target_window (editor));
	selection = ephy_node_view_get_selection (EPHY_NODE_VIEW (editor->priv->pages_view));

	for (l = selection; l; l = l->next)
	{
		EphyNode *node = l->data;
		const char *location;

		location = ephy_node_get_property_string (node,
						EPHY_NODE_PAGE_PROP_LOCATION);

		ephy_shell_new_tab (ephy_shell, window, NULL, location,
			EPHY_NEW_TAB_OPEN_PAGE | EPHY_NEW_TAB_IN_EXISTING_WINDOW);
	}

	g_list_free (selection);
}

static void
cmd_open_bookmarks_in_browser (GtkAction *action,
			       EphyHistoryWindow *editor)
{
	EphyWindow *window;
	GList *selection;
	GList *l;

	window = EPHY_WINDOW (get_target_window (editor));
	selection = ephy_node_view_get_selection (EPHY_NODE_VIEW (editor->priv->pages_view));

	for (l = selection; l; l = l->next)
	{
		EphyNode *node = l->data;
		const char *location;

		location = ephy_node_get_property_string (node,
						EPHY_NODE_PAGE_PROP_LOCATION);

		ephy_shell_new_tab (ephy_shell, window, NULL, location,
				    EPHY_NEW_TAB_OPEN_PAGE |
				    EPHY_NEW_TAB_IN_NEW_WINDOW);
	}

	g_list_free (selection);
}

static void
cmd_cut (GtkAction *action,
	 EphyHistoryWindow *editor)
{
	GtkWidget *widget = gtk_window_get_focus (GTK_WINDOW (editor));

	if (GTK_IS_EDITABLE (widget))
	{
		gtk_editable_cut_clipboard (GTK_EDITABLE (widget));
	}
}

static void
cmd_copy (GtkAction *action,
	  EphyHistoryWindow *editor)
{
	GtkWidget *widget = gtk_window_get_focus (GTK_WINDOW (editor));

	if (GTK_IS_EDITABLE (widget))
	{
		gtk_editable_copy_clipboard (GTK_EDITABLE (widget));
	}

	else if (ephy_node_view_is_target (EPHY_NODE_VIEW (editor->priv->pages_view)))
	{
		GList *selection;

		selection = ephy_node_view_get_selection (EPHY_NODE_VIEW (editor->priv->pages_view));

		if (g_list_length (selection) == 1)
		{
			const char *tmp;
			EphyNode *node = selection->data;
			tmp = ephy_node_get_property_string (node, EPHY_NODE_PAGE_PROP_LOCATION);
			gtk_clipboard_set_text (gtk_clipboard_get (GDK_SELECTION_CLIPBOARD), tmp, -1);
		}

		g_list_free (selection);
	}
}

static void
cmd_paste (GtkAction *action,
	   EphyHistoryWindow *editor)
{
	GtkWidget *widget = gtk_window_get_focus (GTK_WINDOW (editor));

	if (GTK_IS_EDITABLE (widget))
	{
		gtk_editable_paste_clipboard (GTK_EDITABLE (widget));
	}
}

static void
cmd_select_all (GtkAction *action,
		EphyHistoryWindow *editor)
{
	GtkWidget *widget = gtk_window_get_focus (GTK_WINDOW (editor));
	GtkWidget *pages_view = editor->priv->pages_view;

	if (GTK_IS_EDITABLE (widget))
	{
		gtk_editable_select_region (GTK_EDITABLE (widget), 0, -1);
	}
	else if (ephy_node_view_is_target (EPHY_NODE_VIEW (pages_view)))
	{
		GtkTreeSelection *sel;

		sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (pages_view));
		gtk_tree_selection_select_all (sel);
	}
}

static void
cmd_delete (GtkAction *action,
            EphyHistoryWindow *editor)
{
	if (ephy_node_view_is_target (EPHY_NODE_VIEW (editor->priv->pages_view)))
	{
		ephy_node_view_remove (EPHY_NODE_VIEW (editor->priv->pages_view));
	}
}

static void
cmd_bookmark_link (GtkAction *action,
                   EphyHistoryWindow *editor)
{
        GtkWindow *window;
        EphyBookmarks *bookmarks;
        GtkWidget *new_bookmark;
        GList *selection;

        window = GTK_WINDOW(get_target_window (editor));
        bookmarks = ephy_shell_get_bookmarks (ephy_shell);

        selection = ephy_node_view_get_selection (EPHY_NODE_VIEW (editor->priv->pages_view));

	if (g_list_length (selection) == 1)
	{
		const char *location;
		const char *title;
		EphyNode *node;

		node = selection->data;
		location = ephy_node_get_property_string (node, EPHY_NODE_PAGE_PROP_LOCATION);
		title = ephy_node_get_property_string (node, EPHY_NODE_PAGE_PROP_TITLE);
		if (ephy_new_bookmark_is_unique (bookmarks, GTK_WINDOW (window),
						 location))
		{
			new_bookmark = ephy_new_bookmark_new
				(bookmarks, window, location);
			ephy_new_bookmark_set_title
				(EPHY_NEW_BOOKMARK (new_bookmark), title);
			gtk_widget_show (new_bookmark);
		}
	}
	g_list_free (selection);
}

static void
cmd_help_contents (GtkAction *action,
		   EphyHistoryWindow *editor)
{
	ephy_gui_help (GTK_WINDOW (editor),
		       "epiphany", 
		       "ephy-managing-history");
}

static void
set_columns_visibility (EphyHistoryWindow *view, int value)
{
	switch (value)
	{
		case VIEW_TITLE:
			gtk_tree_view_column_set_visible (view->priv->title_col, TRUE);
			gtk_tree_view_column_set_visible (view->priv->address_col, FALSE);
			break;
		case VIEW_ADDRESS:
			gtk_tree_view_column_set_visible (view->priv->title_col, FALSE);
			gtk_tree_view_column_set_visible (view->priv->address_col, TRUE);
			break;
		case VIEW_TITLE_AND_ADDRESS:
			gtk_tree_view_column_set_visible (view->priv->title_col, TRUE);
			gtk_tree_view_column_set_visible (view->priv->address_col, TRUE);
			break;
	}
}

static void
cmd_view_columns (GtkAction *action,
		  GtkRadioAction *current,
		  EphyHistoryWindow *view)
{
	int value;
	GSList *svalues = NULL;

	value = gtk_radio_action_get_current_value (current);
	set_columns_visibility (view, value);

	switch (value)
	{
		case VIEW_TITLE:
			svalues = g_slist_append (svalues, (gpointer)"title");
			break;
		case VIEW_TITLE_AND_ADDRESS:
			svalues = g_slist_append (svalues, (gpointer)"title");
			svalues = g_slist_append (svalues, (gpointer)"address");
			break;
	}

	eel_gconf_set_string_list (CONF_HISTORY_VIEW_DETAILS, svalues);
	g_slist_free (svalues);
}

GType
ephy_history_window_get_type (void)
{
	static GType ephy_history_window_type = 0;

	if (ephy_history_window_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (EphyHistoryWindowClass),
			NULL,
			NULL,
			(GClassInitFunc) ephy_history_window_class_init,
			NULL,
			NULL,
			sizeof (EphyHistoryWindow),
			0,
			(GInstanceInitFunc) ephy_history_window_init
		};

		ephy_history_window_type = g_type_register_static (GTK_TYPE_WINDOW,
							             "EphyHistoryWindow",
							             &our_info, 0);
	}

	return ephy_history_window_type;
}

static void
ephy_history_window_show (GtkWidget *widget)
{
	EphyHistoryWindow *window = EPHY_HISTORY_WINDOW (widget);

	gtk_widget_grab_focus (window->priv->search_entry);

	GTK_WIDGET_CLASS (parent_class)->show (widget);
}

static void
ephy_history_window_class_init (EphyHistoryWindowClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = ephy_history_window_finalize;

	object_class->set_property = ephy_history_window_set_property;
	object_class->get_property = ephy_history_window_get_property;
	object_class->dispose  = ephy_history_window_dispose;

	widget_class->show = ephy_history_window_show;

	g_object_class_install_property (object_class,
					 PROP_HISTORY,
					 g_param_spec_object ("history",
							      "Global history",
							      "Global History",
							      EPHY_TYPE_HISTORY,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (object_class, sizeof(EphyHistoryWindowPrivate));
}

static void
ephy_history_window_finalize (GObject *object)
{
	EphyHistoryWindow *editor = EPHY_HISTORY_WINDOW (object);

	g_object_unref (G_OBJECT (editor->priv->pages_filter));
	g_object_unref (G_OBJECT (editor->priv->sites_filter));

	g_object_unref (editor->priv->action_group);
	g_object_unref (editor->priv->ui_merge);

	if (editor->priv->window)
	{
		g_object_remove_weak_pointer
                        (G_OBJECT(editor->priv->window),
                         (gpointer *)&editor->priv->window);
	}

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
ephy_history_window_node_activated_cb (GtkWidget *view,
				         EphyNode *node,
					 EphyHistoryWindow *editor)
{
	const char *location;

	location = ephy_node_get_property_string
		(node, EPHY_NODE_PAGE_PROP_LOCATION);
	g_return_if_fail (location != NULL);

	ephy_shell_new_tab (ephy_shell, NULL, NULL, location,
			    EPHY_NEW_TAB_OPEN_PAGE);
}

static void
ephy_history_window_update_menu (EphyHistoryWindow *editor)
{
	gboolean open_in_window, open_in_tab;
	gboolean cut, copy, paste, select_all;
	gboolean pages_focus, pages_selection;
	gboolean pages_multiple_selection;
	gboolean delete, bookmark_page;
	GtkActionGroup *action_group;
	GtkAction *action;
	char *open_in_window_label, *open_in_tab_label, *copy_label;
	GtkWidget *focus_widget;

	pages_focus = ephy_node_view_is_target
		(EPHY_NODE_VIEW (editor->priv->pages_view));
	pages_selection = ephy_node_view_has_selection
		(EPHY_NODE_VIEW (editor->priv->pages_view),
		 &pages_multiple_selection);
	focus_widget = gtk_window_get_focus (GTK_WINDOW (editor));

	if (GTK_IS_EDITABLE (focus_widget))
	{
		gboolean has_selection;

		has_selection = gtk_editable_get_selection_bounds
			(GTK_EDITABLE (focus_widget), NULL, NULL);

		cut = has_selection;
		copy = has_selection;
		paste = TRUE;
		select_all = TRUE;
	}
	else
	{
		cut = FALSE;
		copy = (pages_focus && !pages_multiple_selection && pages_selection);
		paste = FALSE;
		select_all = pages_focus;
	}

	if (pages_multiple_selection)
	{
		open_in_window_label = N_("_Open in New Windows");
		open_in_tab_label = N_("Open in New _Tabs");
	}
	else
	{
		open_in_window_label = _("_Open in New Window");
		open_in_tab_label = _("Open in New _Tab");
	}

	if (pages_focus)
	{
		copy_label = _("_Copy Address");
	}
	else
	{
		copy_label = _("_Copy");
	}


	open_in_window = (pages_focus && pages_selection);
	open_in_tab = (pages_focus && pages_selection);
        delete = (pages_focus && pages_selection);
	bookmark_page = (pages_focus && pages_selection && !pages_multiple_selection);

	action_group = editor->priv->action_group;
	action = gtk_action_group_get_action (action_group, "OpenInWindow");
	g_object_set (action, "sensitive", open_in_window, NULL);
	g_object_set (action, "label", open_in_window_label, NULL);
	action = gtk_action_group_get_action (action_group, "OpenInTab");
	g_object_set (action, "sensitive", open_in_tab, NULL);
	g_object_set (action, "label", open_in_tab_label, NULL);
	action = gtk_action_group_get_action (action_group, "Cut");
	g_object_set (action, "sensitive", cut, NULL);
	action = gtk_action_group_get_action (action_group, "Copy");
	g_object_set (action, "sensitive", copy, NULL);
	g_object_set (action, "label", copy_label, NULL);
	action = gtk_action_group_get_action (action_group, "Paste");
	g_object_set (action, "sensitive", paste, NULL);
	action = gtk_action_group_get_action (action_group, "SelectAll");
	g_object_set (action, "sensitive", select_all, NULL);
	action = gtk_action_group_get_action (action_group, "Delete");
	g_object_set (action, "sensitive", delete, NULL);
	action = gtk_action_group_get_action (action_group, "BookmarkLink");
	g_object_set (action, "sensitive", bookmark_page, NULL);
}

static void
entry_selection_changed_cb (GtkWidget *widget, GParamSpec *pspec, EphyHistoryWindow *editor)
{
	ephy_history_window_update_menu (editor);
}

static void
add_entry_monitor (EphyHistoryWindow *editor, GtkWidget *entry)
{
        g_signal_connect (G_OBJECT (entry),
                          "notify::selection-bound",
                          G_CALLBACK (entry_selection_changed_cb),
                          editor);
        g_signal_connect (G_OBJECT (entry),
                          "notify::cursor-position",
                          G_CALLBACK (entry_selection_changed_cb),
                          editor);
}

static gboolean
view_focus_cb (EphyNodeView *view,
               GdkEventFocus *event,
               EphyHistoryWindow *editor)
{
       ephy_history_window_update_menu (editor);

       return FALSE;
}

static void
add_focus_monitor (EphyHistoryWindow *editor, GtkWidget *widget)
{
       g_signal_connect (G_OBJECT (widget),
                         "focus_in_event",
                         G_CALLBACK (view_focus_cb),
                         editor);
       g_signal_connect (G_OBJECT (widget),
                         "focus_out_event",
                         G_CALLBACK (view_focus_cb),
                         editor);
}

static void
remove_focus_monitor (EphyHistoryWindow *editor, GtkWidget *widget)
{
       g_signal_handlers_disconnect_by_func (G_OBJECT (widget),
                                             G_CALLBACK (view_focus_cb),
                                             editor);
}

static gboolean
ephy_history_window_show_popup_cb (GtkWidget *view,
				   EphyHistoryWindow *editor)
{
	GtkWidget *widget;

	widget = gtk_ui_manager_get_widget (editor->priv->ui_merge,
					    "/EphyHistoryWindowPopup");
	gtk_menu_popup (GTK_MENU (widget), NULL, NULL, NULL, NULL, 2,
			gtk_get_current_event_time ());

	return TRUE;
}

static gboolean
key_pressed_cb (EphyNodeView *view,
		GdkEventKey *event,
		EphyHistoryWindow *editor)
{
	switch (event->keyval)
	{
	case GDK_Delete:
	case GDK_KP_Delete:
		cmd_delete (NULL, editor);
		return TRUE;

	default:
		break;
	}

	return FALSE;
}

static void
add_by_site_filter (EphyHistoryWindow *editor, EphyNodeFilter *filter, int level)
{
	if (editor->priv->selected_site == NULL) return;

	ephy_node_filter_add_expression
		(filter, ephy_node_filter_expression_new
				(EPHY_NODE_FILTER_EXPRESSION_HAS_PARENT,
				 editor->priv->selected_site),
		 level);
}

#define SEC_PER_DAY (60 * 60 * 24)

static void
add_by_date_filter (EphyHistoryWindow *editor, EphyNodeFilter *filter, int level,
		    EphyNode *equals)
{
	GTime now, cmp_time, days;
	int time_range;

	/* FIXME this is probably wrong for timezones */

	time_range = gtk_combo_box_get_active
		(GTK_COMBO_BOX (editor->priv->time_combo));

	now = time (NULL);
	days = now / SEC_PER_DAY;

	switch (time_range)
	{
		case TIME_EVER:
			return;
		case TIME_TODAY:
			break;
		case TIME_LAST_TWO_DAYS:
			days -= 1;
			break;
		case TIME_LAST_THREE_DAYS:
			days -= 2;
			break;
	}

	cmp_time = days * SEC_PER_DAY;

	ephy_node_filter_add_expression
		(filter, ephy_node_filter_expression_new
				(EPHY_NODE_FILTER_EXPRESSION_INT_PROP_BIGGER_THAN,
				 EPHY_NODE_PAGE_PROP_LAST_VISIT, cmp_time),
		 level);

	if (equals == NULL) return;

	ephy_node_filter_add_expression
		(filter, ephy_node_filter_expression_new
				(EPHY_NODE_FILTER_EXPRESSION_EQUALS, equals),
		 0);
}

static void
add_by_word_filter (EphyHistoryWindow *editor, EphyNodeFilter *filter, int level)
{
	const char *search_text;

	search_text = gtk_entry_get_text (GTK_ENTRY (editor->priv->search_entry));
	if (search_text == NULL) return;

	ephy_node_filter_add_expression
		(filter, ephy_node_filter_expression_new
				(EPHY_NODE_FILTER_EXPRESSION_STRING_PROP_CONTAINS,
				 EPHY_NODE_PAGE_PROP_TITLE, search_text),
		 level);

	ephy_node_filter_add_expression
		(filter, ephy_node_filter_expression_new
				(EPHY_NODE_FILTER_EXPRESSION_STRING_PROP_CONTAINS,
				 EPHY_NODE_PAGE_PROP_LOCATION, search_text),
		 level);
}

static void
setup_filters (EphyHistoryWindow *editor,
	       gboolean pages, gboolean sites)
{
	LOG ("Setup filters for pages %d and sites %d", pages, sites);

	if (pages)
	{
		ephy_node_filter_empty (editor->priv->pages_filter);

		add_by_date_filter (editor, editor->priv->pages_filter, 0, NULL);
		add_by_word_filter (editor, editor->priv->pages_filter, 1);
		add_by_site_filter (editor, editor->priv->pages_filter, 2);

		ephy_node_filter_done_changing (editor->priv->pages_filter);
	}

	if (sites)
	{
		ephy_node_filter_empty (editor->priv->sites_filter);

		add_by_date_filter (editor, editor->priv->sites_filter, 0,
				    ephy_history_get_pages (editor->priv->history));

		ephy_node_filter_done_changing (editor->priv->sites_filter);
	}
}

static void
site_node_selected_cb (EphyNodeView *view,
		       EphyNode *node,
		       EphyHistoryWindow *editor)
{
	EphyNode *pages;

	if (editor->priv->selected_site == node) return;

	editor->priv->selected_site = node;

	if (node == NULL)
	{
		pages = ephy_history_get_pages (editor->priv->history);
		ephy_node_view_select_node (EPHY_NODE_VIEW (editor->priv->sites_view), pages);
	}
	else
	{
		ephy_search_entry_clear (EPHY_SEARCH_ENTRY (editor->priv->search_entry));
		setup_filters (editor, TRUE, FALSE);
	}
}

static void
search_entry_search_cb (GtkWidget *entry, char *search_text, EphyHistoryWindow *editor)
{
	EphyNode *all;

	g_signal_handlers_block_by_func
		(G_OBJECT (editor->priv->sites_view),
		 G_CALLBACK (site_node_selected_cb),
		 editor);
	all = ephy_history_get_pages (editor->priv->history);
	editor->priv->selected_site = all;
	ephy_node_view_select_node (EPHY_NODE_VIEW (editor->priv->sites_view),
				    all);
	g_signal_handlers_unblock_by_func
		(G_OBJECT (editor->priv->sites_view),
		 G_CALLBACK (site_node_selected_cb),
		 editor);

	setup_filters (editor, TRUE, FALSE);
}

static void
time_combo_changed_cb (GtkWidget *combo, EphyHistoryWindow *editor)
{
	setup_filters (editor, TRUE, TRUE);
}

static GtkWidget *
build_search_box (EphyHistoryWindow *editor)
{
	GtkWidget *box, *label, *entry;
	GtkWidget *combo;
	char *str;
	int time_range;

	box = gtk_hbox_new (FALSE, 6);
	gtk_container_set_border_width (GTK_CONTAINER (box), 6);
	gtk_widget_show (box);

	entry = ephy_search_entry_new ();
	add_focus_monitor (editor, entry);
	add_entry_monitor (editor, entry);
	editor->priv->search_entry = entry;
	gtk_widget_show (entry);

	label = gtk_label_new (NULL);
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	str = g_strconcat ("<b>", _("_Search:"), "</b>", NULL);
	gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), str);
	g_free (str);
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);
	gtk_widget_show (label);

	combo = gtk_combo_box_new_text ();
	gtk_widget_show (combo);

	gtk_combo_box_append_text (GTK_COMBO_BOX (combo), _("Today"));
	str = g_strdup_printf (ngettext ("Last %d day", "Last %d days", 2), 2);
	gtk_combo_box_append_text (GTK_COMBO_BOX (combo), str);
	g_free (str);
	str = g_strdup_printf (ngettext ("Last %d day", "Last %d days", 3), 3);
	gtk_combo_box_append_text (GTK_COMBO_BOX (combo), str);
	g_free (str);
	/* keep this in sync with embed/ephy-history.c's HISTORY_PAGE_OBSOLETE_DAYS */
	str = g_strdup_printf (ngettext ("Last %d day", "Last %d days", 10), 10);
	gtk_combo_box_append_text (GTK_COMBO_BOX (combo), str);
	g_free (str);

	str = eel_gconf_get_string (CONF_HISTORY_DATE_FILTER);
	if (str && strcmp (TIME_TODAY_STRING, str) == 0)
	{
		time_range = TIME_TODAY;
	}
	else if (str && strcmp (TIME_LAST_TWO_DAYS_STRING, str) == 0)
	{
		time_range = TIME_LAST_TWO_DAYS;
	}
	else if (str && strcmp (TIME_LAST_THREE_DAYS_STRING, str) == 0)
	{
		time_range = TIME_LAST_THREE_DAYS;
	}
	else
	{
		time_range = TIME_EVER;
	}
	g_free (str);

	gtk_combo_box_set_active (GTK_COMBO_BOX (combo),
				  time_range);

	editor->priv->time_combo = combo;

	gtk_box_pack_start (GTK_BOX (box),
			    label, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (box),
			    entry, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (box),
			    combo, FALSE, TRUE, 0);

	g_signal_connect (combo, "changed",
			  G_CALLBACK (time_combo_changed_cb),
			  editor);
	g_signal_connect (G_OBJECT (entry), "search",
			  G_CALLBACK (search_entry_search_cb),
			  editor);

	return box;
}

static void
add_widget (GtkUIManager *merge, GtkWidget *widget, EphyHistoryWindow *editor)
{
	gtk_box_pack_start (GTK_BOX (editor->priv->main_vbox),
			    widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);
}

static gboolean
delete_event_cb (EphyHistoryWindow *editor)
{
	gtk_widget_hide (GTK_WIDGET (editor));

	return TRUE;
}

static void
provide_favicon (EphyNode *node, GValue *value, gpointer user_data)
{
	EphyFaviconCache *cache;
	const char *icon_location;
	GdkPixbuf *pixbuf = NULL;

	cache = EPHY_FAVICON_CACHE
		(ephy_embed_shell_get_favicon_cache (EPHY_EMBED_SHELL (ephy_shell)));
	icon_location = ephy_node_get_property_string
		(node, EPHY_NODE_PAGE_PROP_ICON);

	LOG ("Get favicon for %s", icon_location ? icon_location : "None")

	if (icon_location)
	{
		pixbuf = ephy_favicon_cache_get (cache, icon_location);
	}

	g_value_init (value, GDK_TYPE_PIXBUF);
	g_value_take_object (value, pixbuf);
}

static void
view_selection_changed_cb (GtkWidget *view, EphyHistoryWindow *editor)
{
	ephy_history_window_update_menu (editor);
}

static int
get_details_value (void)
{
	int value;
	GSList *svalues;

	svalues = eel_gconf_get_string_list (CONF_HISTORY_VIEW_DETAILS);
	if (svalues == NULL) return VIEW_TITLE_AND_ADDRESS;

	if (g_slist_find_custom (svalues, "title", (GCompareFunc)strcmp) &&
	    g_slist_find_custom (svalues, "address", (GCompareFunc)strcmp))
	{
		value = VIEW_TITLE_AND_ADDRESS;
	}
	else if (g_slist_find_custom (svalues, "title", (GCompareFunc)strcmp))
	{
		value = VIEW_TITLE;
	}
	else if (g_slist_find_custom (svalues, "address", (GCompareFunc)strcmp))
	{
		value = VIEW_ADDRESS;
	}
	else
	{
		value = VIEW_TITLE_AND_ADDRESS;
	}

	g_slist_foreach (svalues, (GFunc) g_free, NULL);
	g_slist_free (svalues);

	return value;
}

static void
ephy_history_window_construct (EphyHistoryWindow *editor)
{
	GtkTreeViewColumn *col;
	GtkTreeSelection *selection;
	GtkWidget *vbox, *hpaned;
	GtkWidget *pages_view, *sites_view;
	GtkWidget *scrolled_window;
	EphyNode *node;
	GtkUIManager *ui_merge;
	GtkActionGroup *action_group;
	GdkPixbuf *icon;
	int col_id, details_value;

	gtk_window_set_title (GTK_WINDOW (editor), _("History"));

	icon = gtk_widget_render_icon (GTK_WIDGET (editor), 
				       EPHY_STOCK_HISTORY,
				       GTK_ICON_SIZE_MENU,
				       NULL);
	gtk_window_set_icon (GTK_WINDOW(editor), icon);

	g_signal_connect (editor, "delete_event",
			  G_CALLBACK (delete_event_cb), NULL);

	editor->priv->main_vbox = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (editor->priv->main_vbox);
	gtk_container_add (GTK_CONTAINER (editor), editor->priv->main_vbox);

	ui_merge = gtk_ui_manager_new ();
	g_signal_connect (ui_merge, "add_widget", G_CALLBACK (add_widget), editor);
	gtk_window_add_accel_group (GTK_WINDOW (editor),
				    gtk_ui_manager_get_accel_group (ui_merge));
	action_group = gtk_action_group_new ("PopupActions");
	gtk_action_group_set_translation_domain (action_group, NULL);
	gtk_action_group_add_actions (action_group, ephy_history_ui_entries,
				      ephy_history_ui_n_entries, editor);

	details_value = get_details_value ();
	gtk_action_group_add_radio_actions (action_group,
					    ephy_history_radio_entries,
					    ephy_history_n_radio_entries,
					    details_value,
					    G_CALLBACK (cmd_view_columns), 
					    editor);

	gtk_ui_manager_insert_action_group (ui_merge,
					    action_group, 0);
	gtk_ui_manager_add_ui_from_file (ui_merge,
				         ephy_file ("epiphany-history-window-ui.xml"),
				         NULL);
	gtk_ui_manager_ensure_update (ui_merge);
	editor->priv->ui_merge = ui_merge;
	editor->priv->action_group = action_group;

	hpaned = gtk_hpaned_new ();
	gtk_container_set_border_width (GTK_CONTAINER (hpaned), 0);
	gtk_box_pack_end (GTK_BOX (editor->priv->main_vbox), hpaned,
			  TRUE, TRUE, 0);
	gtk_widget_show (hpaned);

	g_assert (editor->priv->history);

	/* Sites View */
	node = ephy_history_get_hosts (editor->priv->history);
	scrolled_window = g_object_new (GTK_TYPE_SCROLLED_WINDOW,
					"hadjustment", NULL,
					"vadjustment", NULL,
					"hscrollbar_policy", GTK_POLICY_AUTOMATIC,
					"vscrollbar_policy", GTK_POLICY_AUTOMATIC,
					"shadow_type", GTK_SHADOW_IN,
					NULL);
	gtk_paned_add1 (GTK_PANED (hpaned), scrolled_window);
	gtk_widget_show (scrolled_window);
	editor->priv->sites_filter = ephy_node_filter_new ();
	sites_view = ephy_node_view_new (node, editor->priv->sites_filter);
	add_focus_monitor (editor, sites_view);
	col_id = ephy_node_view_add_data_column (EPHY_NODE_VIEW (sites_view),
					         G_TYPE_STRING,
					         EPHY_NODE_PAGE_PROP_LOCATION,
						 NULL, NULL);
	ephy_node_view_enable_drag_source (EPHY_NODE_VIEW (sites_view),
					   page_drag_types,
				           n_page_drag_types, col_id);
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (sites_view));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);
	ephy_node_view_add_column (EPHY_NODE_VIEW (sites_view), _("Sites"),
				   G_TYPE_STRING,
				   EPHY_NODE_PAGE_PROP_TITLE,
				   EPHY_NODE_PAGE_PROP_PRIORITY,
				   EPHY_NODE_VIEW_AUTO_SORT |
				   EPHY_NODE_VIEW_SEARCHABLE,
				   provide_favicon);
	gtk_container_add (GTK_CONTAINER (scrolled_window), sites_view);
	gtk_widget_show (sites_view);
	editor->priv->sites_view = sites_view;
	editor->priv->selected_site = ephy_history_get_pages (editor->priv->history);
	ephy_node_view_select_node (EPHY_NODE_VIEW (sites_view),
				    editor->priv->selected_site);

	g_signal_connect (G_OBJECT (sites_view),
			  "node_selected",
			  G_CALLBACK (site_node_selected_cb),
			  editor);
	g_signal_connect (G_OBJECT (selection),
			  "changed",
			  G_CALLBACK (view_selection_changed_cb),
			  editor);

	vbox = gtk_vbox_new (FALSE, 0);
	gtk_paned_add2 (GTK_PANED (hpaned), vbox);
	gtk_widget_show (vbox);

	gtk_box_pack_start (GTK_BOX (vbox),
			    build_search_box (editor),
			    FALSE, FALSE, 0);

	/* Pages View */
	scrolled_window = g_object_new (GTK_TYPE_SCROLLED_WINDOW,
					"hadjustment", NULL,
					"vadjustment", NULL,
					"hscrollbar_policy", GTK_POLICY_AUTOMATIC,
					"vscrollbar_policy", GTK_POLICY_AUTOMATIC,
					"shadow_type", GTK_SHADOW_IN,
					NULL);
	gtk_box_pack_start (GTK_BOX (vbox), scrolled_window, TRUE, TRUE, 0);
	gtk_widget_show (scrolled_window);
	node = ephy_history_get_pages (editor->priv->history);
	editor->priv->pages_filter = ephy_node_filter_new ();
	pages_view = ephy_node_view_new (node, editor->priv->pages_filter);
	add_focus_monitor (editor, pages_view);
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (pages_view));
	gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (pages_view), TRUE);
	col_id = ephy_node_view_add_data_column (EPHY_NODE_VIEW (pages_view),
					         G_TYPE_STRING,
					         EPHY_NODE_PAGE_PROP_LOCATION,
						 NULL, NULL);
	ephy_node_view_enable_drag_source (EPHY_NODE_VIEW (pages_view),
					   page_drag_types,
				           n_page_drag_types, col_id);
	col = ephy_node_view_add_column (EPHY_NODE_VIEW (pages_view), _("Title"),
				         G_TYPE_STRING, EPHY_NODE_PAGE_PROP_TITLE,
				         -1, EPHY_NODE_VIEW_USER_SORT |
					 EPHY_NODE_VIEW_SEARCHABLE, NULL);
	gtk_tree_view_column_set_max_width (col, 250);
	editor->priv->title_col = col;
	col = ephy_node_view_add_column (EPHY_NODE_VIEW (pages_view), _("Address"),
				         G_TYPE_STRING, EPHY_NODE_PAGE_PROP_LOCATION,
				         -1, EPHY_NODE_VIEW_USER_SORT, NULL);
	gtk_tree_view_column_set_max_width (col, 200);
	editor->priv->address_col = col;
	gtk_container_add (GTK_CONTAINER (scrolled_window), pages_view);
	gtk_widget_show (pages_view);
	editor->priv->pages_view = pages_view;

	g_signal_connect (G_OBJECT (pages_view),
			  "node_activated",
			  G_CALLBACK (ephy_history_window_node_activated_cb),
			  editor);
	g_signal_connect (G_OBJECT (pages_view),
			  "popup_menu",
			  G_CALLBACK (ephy_history_window_show_popup_cb),
			  editor);
	g_signal_connect (G_OBJECT (pages_view),
			  "key_press_event",
			  G_CALLBACK (key_pressed_cb),
			  editor);
	g_signal_connect (G_OBJECT (selection),
			  "changed",
			  G_CALLBACK (view_selection_changed_cb),
			  editor);

	ephy_state_add_window (GTK_WIDGET (editor),
			       "history_window",
		               450, 400,
			       EPHY_STATE_WINDOW_SAVE_SIZE | EPHY_STATE_WINDOW_SAVE_POSITION);
	ephy_state_add_paned  (GTK_WIDGET (hpaned),
			       "history_paned",
		               130);

	set_columns_visibility (editor, details_value);
	setup_filters (editor, TRUE, TRUE);
}

void
ephy_history_window_set_parent (EphyHistoryWindow *ebe,
				GtkWidget *window)
{
	if (ebe->priv->window)
	{
		g_object_remove_weak_pointer
                        (G_OBJECT(ebe->priv->window),
                         (gpointer *)&ebe->priv->window);
	}

	ebe->priv->window = window;

	g_object_add_weak_pointer
                        (G_OBJECT(ebe->priv->window),
                         (gpointer *)&ebe->priv->window);

}

GtkWidget *
ephy_history_window_new (EphyHistory *history)
{
	EphyHistoryWindow *editor;

	g_assert (history != NULL);

	editor = EPHY_HISTORY_WINDOW (g_object_new
			(EPHY_TYPE_HISTORY_WINDOW,
			 "history", history,
			 NULL));

	ephy_history_window_construct (editor);

	return GTK_WIDGET (editor);
}

static void
ephy_history_window_set_property (GObject *object,
		                  guint prop_id,
		                  const GValue *value,
		                  GParamSpec *pspec)
{
	EphyHistoryWindow *editor = EPHY_HISTORY_WINDOW (object);

	switch (prop_id)
	{
	case PROP_HISTORY:
		editor->priv->history = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
ephy_history_window_get_property (GObject *object,
		                  guint prop_id,
		                  GValue *value,
		                  GParamSpec *pspec)
{
	EphyHistoryWindow *editor = EPHY_HISTORY_WINDOW (object);

	switch (prop_id)
	{
	case PROP_HISTORY:
		g_value_set_object (value, editor->priv->history);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
ephy_history_window_init (EphyHistoryWindow *editor)
{
	editor->priv = EPHY_HISTORY_WINDOW_GET_PRIVATE (editor);
}

static void
save_date_filter (EphyHistoryWindow *editor)
{
	const char *time_string = NULL;
	int time_range;

	time_range = gtk_combo_box_get_active
		(GTK_COMBO_BOX (editor->priv->time_combo));

	switch (time_range)
	{
		case TIME_EVER:
			time_string = TIME_EVER_STRING;
			break;
		case TIME_TODAY:
			time_string = TIME_TODAY_STRING;
			break;
		case TIME_LAST_TWO_DAYS:
			time_string = TIME_LAST_TWO_DAYS_STRING;
			break;
		case TIME_LAST_THREE_DAYS:
			time_string = TIME_LAST_THREE_DAYS_STRING;
			break;
	}

	eel_gconf_set_string (CONF_HISTORY_DATE_FILTER, time_string);
}

static void
ephy_history_window_dispose (GObject *object)
{
	EphyHistoryWindow *editor;

	g_return_if_fail (object != NULL);
	g_return_if_fail (EPHY_IS_HISTORY_WINDOW (object));

	editor = EPHY_HISTORY_WINDOW (object);

	if (editor->priv->sites_view != NULL)
	{
		remove_focus_monitor (editor, editor->priv->pages_view);
		remove_focus_monitor (editor, editor->priv->sites_view);
		remove_focus_monitor (editor, editor->priv->search_entry);

		editor->priv->sites_view = NULL;

		save_date_filter (editor);
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}
