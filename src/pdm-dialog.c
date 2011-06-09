/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright © 2002 Jorn Baayen
 *  Copyright © 2003 Marco Pesenti Gritti
 *  Copyright © 2003, 2004 Christian Persch
 *  Copyright © 2009 Igalia S.L.
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

#include "pdm-dialog.h"
#include "ephy-shell.h"
#include "ephy-file-helpers.h"
#include "ephy-gui.h"
#include "ephy-state.h"
#include "ephy-string.h"
#include "ephy-stock-icons.h"
#include "ephy-debug.h"
#include "ephy-time-helpers.h"
#include "ephy-embed-single.h"
#include "ephy-favicon-cache.h"
#include "ephy-history.h"
#include "ephy-password-info.h"

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <libsoup/soup.h>
#include <webkit/webkit.h>
#include <gnome-keyring.h>
#include <gnome-keyring-memory.h>

#include <string.h>
#include <time.h>

typedef struct PdmActionInfo PdmActionInfo;

struct PdmActionInfo
{
	/* Methods */
	void (* construct)	(PdmActionInfo *info);
	void (* destruct)	(PdmActionInfo *info);
	void (* fill)		(PdmActionInfo *info);
	void (* add)		(PdmActionInfo *info,
				 gpointer data);
	void (* remove)		(PdmActionInfo *info,
				 gpointer data);
	void (* scroll_to)	(PdmActionInfo *info);

	/* Data */
	PdmDialog *dialog;
	GtkTreeView *treeview;
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	const char *remove_id;
	int data_col;
	char *scroll_to_host;
	gboolean filled;
	gboolean delete_row_on_remove;
};

typedef struct PdmCallBackData PdmCallBackData;
struct PdmCallBackData
{
    guint key;
    GtkListStore *store;
};

#define EPHY_PDM_DIALOG_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE ((object), EPHY_TYPE_PDM_DIALOG, PdmDialogPrivate))

struct PdmDialogPrivate
{
	GtkWidget *notebook;
	GtkTreeModel *model;
	PdmActionInfo *cookies;
	PdmActionInfo *passwords;
};

enum
{
	COL_COOKIES_HOST,
	COL_COOKIES_HOST_KEY,
	COL_COOKIES_NAME,
	COL_COOKIES_DATA,
};

enum
{
	TV_COL_COOKIES_HOST,
	TV_COL_COOKIES_NAME
};

enum
{
	COL_PASSWORDS_HOST,
	COL_PASSWORDS_USER,
	COL_PASSWORDS_PASSWORD,
	COL_PASSWORDS_DATA,
};

enum
{
	PDM_DIALOG_RESPONSE_CLEAR = 1
};

static void pdm_dialog_class_init	(PdmDialogClass *klass);
static void pdm_dialog_init		(PdmDialog *dialog);
static void pdm_dialog_finalize		(GObject *object);
static void pdm_dialog_password_remove (PdmActionInfo *info, gpointer data);

G_DEFINE_TYPE (PdmDialog, pdm_dialog, EPHY_TYPE_DIALOG)

static void
pdm_dialog_class_init (PdmDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pdm_dialog_finalize;

	g_type_class_add_private (object_class, sizeof(PdmDialogPrivate));
}

static void
pdm_dialog_show_help (PdmDialog *pd)
{
	GtkWidget *notebook, *window;
	int id;

	static char * const help_preferences[] = {
		"managing-cookies",
		"managing-passwords"
	};

	ephy_dialog_get_controls (EPHY_DIALOG (pd),
				  "pdm_dialog", &window,
				  "pdm_notebook", &notebook,
				  NULL);

	id = gtk_notebook_get_current_page (GTK_NOTEBOOK (notebook));
	g_return_if_fail (id == 0 || id == 1);

	ephy_gui_help (window, help_preferences[id]);
}

typedef struct
{
	EphyDialog *dialog;
	GtkWidget *checkbutton_history;
	GtkWidget *checkbutton_cookies;
	GtkWidget *checkbutton_passwords;
	GtkWidget *checkbutton_cache;
	guint num_checked;
} PdmClearAllDialogButtons;

static SoupCookieJar*
get_cookie_jar ()
{
	SoupSession* session;

	session = webkit_get_default_session ();
	return (SoupCookieJar*)soup_session_get_feature (session, SOUP_TYPE_COOKIE_JAR);
}

static void
clear_all_dialog_release_cb (PdmClearAllDialogButtons *data)
{
	g_slice_free (PdmClearAllDialogButtons, data);
}

static void
clear_all_cookies (SoupCookieJar *jar)
{
	GSList *l, *p;

	l = soup_cookie_jar_all_cookies (jar);
	for (p = l; p; p = p->next)
		soup_cookie_jar_delete_cookie (jar, (SoupCookie*)p->data);

	soup_cookies_free (l);
}

static void
get_info_full_cb (GnomeKeyringResult result,
		  GnomeKeyringItemInfo *info,
		  gpointer data)
{
	if (result != GNOME_KEYRING_RESULT_OK)
		return;

	if (gnome_keyring_item_info_get_type (info) == GNOME_KEYRING_ITEM_NETWORK_PASSWORD)
		gnome_keyring_item_delete (GNOME_KEYRING_DEFAULT,
					   GPOINTER_TO_UINT (data),
					   NULL, NULL, NULL);
}

static void
got_network_passwords_list_cb (GnomeKeyringResult result,
			       GList *list,
			       gpointer data)
{
	GList *l;

	if (result != GNOME_KEYRING_RESULT_OK)
		return;

	for (l = list; l != NULL; l = l->next)
		gnome_keyring_item_get_info_full (GNOME_KEYRING_DEFAULT,
						  GPOINTER_TO_UINT (l->data),
						  GNOME_KEYRING_ITEM_INFO_BASICS,
						  (GnomeKeyringOperationGetItemInfoCallback) get_info_full_cb,
						  l->data,
						  NULL);
}

static void
_ephy_pdm_delete_all_passwords ()
{
	gnome_keyring_list_item_ids (GNOME_KEYRING_DEFAULT,
				     got_network_passwords_list_cb,
				     NULL, NULL);
}

static void
clear_all_dialog_response_cb (GtkDialog *dialog,
			      int response,
			      PdmClearAllDialogButtons *checkbuttons)
{
	if (response == GTK_RESPONSE_HELP)
	{
		/* Show help and return early */
		ephy_gui_help (GTK_WIDGET (dialog), "clearing-personal-data");
		return;
	}

	if (response == GTK_RESPONSE_OK)
	{
		if (gtk_toggle_button_get_active
			(GTK_TOGGLE_BUTTON (checkbuttons->checkbutton_history)))
		{
			EphyEmbedShell *shell;
			EphyHistory *history;

			shell = ephy_embed_shell_get_default ();
			history = EPHY_HISTORY (ephy_embed_shell_get_global_history (shell));
			ephy_history_clear (history);
		}
		if (gtk_toggle_button_get_active
			(GTK_TOGGLE_BUTTON (checkbuttons->checkbutton_cookies)))
		{
			SoupCookieJar *jar;

			jar = get_cookie_jar ();
			clear_all_cookies (jar);
		}
		if (gtk_toggle_button_get_active
			(GTK_TOGGLE_BUTTON (checkbuttons->checkbutton_passwords)))
		{
			/* Clear UI if we are the PDM dialog */
			if (EPHY_IS_PDM_DIALOG (checkbuttons->dialog))
			{
				PdmDialog *pdialog = EPHY_PDM_DIALOG (checkbuttons->dialog);
				PdmActionInfo *pinfo = pdialog->priv->passwords;
				gtk_list_store_clear (GTK_LIST_STORE (pinfo->model));
			}

			_ephy_pdm_delete_all_passwords ();
		}
		if (gtk_toggle_button_get_active
			(GTK_TOGGLE_BUTTON (checkbuttons->checkbutton_cache)))
		{
			EphyEmbedShell *shell;
			EphyEmbedSingle *single;
			EphyFaviconCache *cache;

			shell = ephy_embed_shell_get_default ();

			single = EPHY_EMBED_SINGLE (ephy_embed_shell_get_embed_single (shell));

			ephy_embed_single_clear_cache (single);

			cache = EPHY_FAVICON_CACHE (ephy_embed_shell_get_favicon_cache (shell));
			ephy_favicon_cache_clear (cache);
		}
	}
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
clear_all_dialog_checkbutton_toggled_cb (GtkToggleButton *toggle,
					 PdmClearAllDialogButtons *data)
{
	GtkWidget *dialog;
	dialog = gtk_widget_get_toplevel (GTK_WIDGET (toggle));

	if (gtk_toggle_button_get_active (toggle) == TRUE)
	{
		data->num_checked++;
	}
	else
	{
		data->num_checked--;
	}
	gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog), GTK_RESPONSE_OK,
					   data->num_checked != 0);
}

void
pdm_dialog_show_clear_all_dialog (EphyDialog *edialog,
				  GtkWidget *parent,
				  PdmClearAllDialogFlags flags)
{
	GtkWidget *dialog, *vbox;
	GtkWidget *check, *label, *content_area;
	PdmClearAllDialogButtons *checkbuttons;
	GtkWidget *button, *icon;

	dialog = gtk_message_dialog_new_with_markup (GTK_WINDOW (parent),
						     GTK_DIALOG_DESTROY_WITH_PARENT |
						     GTK_DIALOG_MODAL,
						     GTK_MESSAGE_QUESTION,
						     GTK_BUTTONS_NONE,
						     _("<b>Select the personal data "
						       "you want to clear</b>"));
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
						  _("You are about to clear personal data "
						    "that is stored about the web pages "
						    "you have visited. Before proceeding, "
						    "check the types of information that you "
						    "want to remove:"));
	gtk_window_set_title (GTK_WINDOW (dialog), _("Clear All Personal Data"));

	gtk_dialog_add_buttons (GTK_DIALOG (dialog),
				GTK_STOCK_HELP,
				GTK_RESPONSE_HELP,
				GTK_STOCK_CANCEL,
				GTK_RESPONSE_CANCEL,
				NULL);

	/* Clear button */
	button = gtk_dialog_add_button (GTK_DIALOG (dialog),
					_("Cl_ear"),
					GTK_RESPONSE_OK);
	icon = gtk_image_new_from_stock (GTK_STOCK_CLEAR, GTK_ICON_SIZE_BUTTON);
	gtk_button_set_image (GTK_BUTTON (button), icon);
	gtk_widget_show (button);

	gtk_dialog_set_default_response (GTK_DIALOG (dialog),
					 GTK_RESPONSE_CANCEL);
#if 0
	gtk_label_set_selectable (GTK_LABEL (GTK_MESSAGE_DIALOG (dialog)->label),
				  FALSE);
#endif

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	content_area = gtk_message_dialog_get_message_area (GTK_MESSAGE_DIALOG (dialog));
	gtk_box_pack_start (GTK_BOX (content_area),
			    vbox, FALSE, FALSE, 0);

	checkbuttons = g_slice_new0 (PdmClearAllDialogButtons);
	checkbuttons->dialog = edialog;
	checkbuttons->num_checked = 0;

	/* Cookies */
	check = gtk_check_button_new_with_mnemonic (_("C_ookies"));
	checkbuttons->checkbutton_cookies = check;
	gtk_box_pack_start (GTK_BOX (vbox), check,
			    FALSE, FALSE, 0);
	g_signal_connect (check, "toggled",
			  G_CALLBACK (clear_all_dialog_checkbutton_toggled_cb), checkbuttons);
	if (flags & CLEAR_ALL_COOKIES)
	{
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), TRUE);
	}

	/* Passwords */
	check = gtk_check_button_new_with_mnemonic (_("Saved _passwords"));
	checkbuttons->checkbutton_passwords = check;
	gtk_box_pack_start (GTK_BOX (vbox), check,
			    FALSE, FALSE, 0);
	g_signal_connect (check, "toggled",
			  G_CALLBACK (clear_all_dialog_checkbutton_toggled_cb), checkbuttons);
	if (flags & CLEAR_ALL_PASSWORDS)
	{
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), TRUE);
	}

	/* History */
	check = gtk_check_button_new_with_mnemonic (_("Hi_story"));
	checkbuttons->checkbutton_history = check;
	gtk_box_pack_start (GTK_BOX (vbox), check,
			    FALSE, FALSE, 0);
	g_signal_connect (check, "toggled",
			  G_CALLBACK (clear_all_dialog_checkbutton_toggled_cb), checkbuttons);
	if (flags & CLEAR_ALL_HISTORY)
	{
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), TRUE);
	}

	/* Cache */
	check = gtk_check_button_new_with_mnemonic (_("_Temporary files"));
	checkbuttons->checkbutton_cache = check;
	gtk_box_pack_start (GTK_BOX (vbox), check,
			    FALSE, FALSE, 0);
	g_signal_connect (check, "toggled",
			  G_CALLBACK (clear_all_dialog_checkbutton_toggled_cb), checkbuttons);
	if (flags & CLEAR_ALL_CACHE)
	{
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), TRUE);
	}

	/* Show vbox and all checkbuttons */
	gtk_widget_show_all (vbox);

	label = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (label),
			      _("<small><i><b>Note:</b> You cannot undo this action. "
				"The data you are choosing to clear "
				"will be deleted forever.</i></small>"));
	gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
			    label, FALSE, FALSE, 0);
	/* Need to do this or the label will wrap too early */
	gtk_widget_set_size_request (label, 330, -1);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_misc_set_alignment (GTK_MISC (label),
				0, 0);
	gtk_misc_set_padding (GTK_MISC (label),
			      6, 0);
	gtk_widget_show (label);

	gtk_window_present (GTK_WINDOW (dialog));
	g_signal_connect_data (dialog, "response",
			       G_CALLBACK (clear_all_dialog_response_cb),
			       checkbuttons, (GClosureNotify) clear_all_dialog_release_cb,
			       (GConnectFlags) 0);
}

static void
action_treeview_selection_changed_cb (GtkTreeSelection *selection,
				      PdmActionInfo *action)
{
	GtkWidget *widget;
	EphyDialog *d = EPHY_DIALOG(action->dialog);
	gboolean has_selection;

	has_selection = gtk_tree_selection_count_selected_rows (selection) > 0;

	widget = ephy_dialog_get_control (d, action->remove_id);
	gtk_widget_set_sensitive (widget, has_selection);

}

static void
pdm_cmd_delete_selection (PdmActionInfo *action)
{

	GList *llist, *rlist = NULL, *l, *r;
	GtkTreeModel *model;
	GtkTreeSelection *selection;
	GtkTreePath *path;
	GtkTreeIter iter, iter2;
	GtkTreeRowReference *row_ref = NULL;

	selection = gtk_tree_view_get_selection
		(GTK_TREE_VIEW(action->treeview));
	llist = gtk_tree_selection_get_selected_rows (selection, &model);

	if (llist == NULL)
	{
		/* nothing to delete, return early */
		return;
	}

	for (l = llist;l != NULL; l = l->next)
	{
		rlist = g_list_prepend (rlist, gtk_tree_row_reference_new
					(model, (GtkTreePath *)l->data));
	}

	/* Intelligent selection logic, no actual selection yet */

	path = gtk_tree_row_reference_get_path
		((GtkTreeRowReference *) g_list_first (rlist)->data);

	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_path_free (path);
	iter2 = iter;

	if (gtk_tree_model_iter_next (GTK_TREE_MODEL (model), &iter))
	{
		path = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);
		row_ref = gtk_tree_row_reference_new (model, path);
	}
	else
	{
		path = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter2);
		if (gtk_tree_path_prev (path))
		{
			row_ref = gtk_tree_row_reference_new (model, path);
		}
	}
	gtk_tree_path_free (path);

	/* Removal */
	for (r = rlist; r != NULL; r = r->next)
	{
		GValue val = { 0, };

		path = gtk_tree_row_reference_get_path ((GtkTreeRowReference *)r->data);
		gtk_tree_model_get_iter (model, &iter, path);
		gtk_tree_model_get_value (model, &iter, action->data_col, &val);
		action->remove (action, g_value_get_boxed (&val));
		g_value_unset (&val);

		/* for cookies we delete from callback, for passwords right here */
		if (action->delete_row_on_remove)
		{
			gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
		}

		gtk_tree_row_reference_free ((GtkTreeRowReference *)r->data);
		gtk_tree_path_free (path);
	}

	g_list_foreach (llist, (GFunc)gtk_tree_path_free, NULL);
	g_list_free (llist);
	g_list_free (rlist);

	/* Selection */
	if (row_ref != NULL)
	{
		path = gtk_tree_row_reference_get_path (row_ref);

		if (path != NULL)
		{
			gtk_tree_view_set_cursor (GTK_TREE_VIEW (action->treeview), path, NULL, FALSE);
			gtk_tree_path_free (path);
		}

		gtk_tree_row_reference_free (row_ref);
	}
}

static gboolean
pdm_key_pressed_cb (GtkTreeView *treeview,
		    GdkEventKey *event,
		    PdmActionInfo *action)
{
	if (event->keyval == GDK_KEY_Delete || event->keyval == GDK_KEY_KP_Delete)
	{
		pdm_cmd_delete_selection (action);

		return TRUE;
	}

	return FALSE;
}

static void
pdm_dialog_remove_button_clicked_cb (GtkWidget *button,
				     PdmActionInfo *action)
{
	pdm_cmd_delete_selection (action);

	/* Restore the focus to the button */
	gtk_widget_grab_focus (button);
}

static void
setup_action (PdmActionInfo *action)
{
	GtkWidget *widget;
	GtkTreeSelection *selection;

	widget = ephy_dialog_get_control (EPHY_DIALOG(action->dialog),
					  action->remove_id);
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pdm_dialog_remove_button_clicked_cb),
			  action);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(action->treeview));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (action_treeview_selection_changed_cb),
			  action);

	g_signal_connect (G_OBJECT (action->treeview),
			  "key_press_event",
			  G_CALLBACK (pdm_key_pressed_cb),
			  action);
}

/* "Cookies" tab */

static void
show_cookies_properties (PdmDialog *dialog,
			 SoupCookie *info)
{
	GtkWidget *gdialog;
	GtkWidget *table;
	GtkWidget *label;
	GtkWidget *parent;
	GtkWidget *content_area;
	char *str;

	parent = ephy_dialog_get_control (EPHY_DIALOG(dialog), "pdm_dialog");

	gdialog = gtk_dialog_new_with_buttons
		 (_("Cookie Properties"),
		  GTK_WINDOW (parent),
		  GTK_DIALOG_DESTROY_WITH_PARENT,
		  GTK_STOCK_CLOSE, 0, NULL);
	ephy_state_add_window (GTK_WIDGET (gdialog), "cookie_properties",
			       -1, -1, FALSE,
			       EPHY_STATE_WINDOW_SAVE_SIZE | EPHY_STATE_WINDOW_SAVE_POSITION);
	gtk_container_set_border_width (GTK_CONTAINER (gdialog), 5);
	content_area = gtk_dialog_get_content_area (GTK_DIALOG (gdialog));
	gtk_box_set_spacing (GTK_BOX (content_area), 14); /* 24 = 2 * 5 + 14 */

	table = gtk_table_new (2, 4, FALSE);
	gtk_container_set_border_width (GTK_CONTAINER (table), 5);
	gtk_table_set_row_spacings (GTK_TABLE (table), 6);
	gtk_table_set_col_spacings (GTK_TABLE (table), 12);
	gtk_widget_show (table);

	str = g_strconcat ("<b>", _("Content:"), "</b>", NULL);
	label = gtk_label_new (str);
	g_free (str);
	gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
	gtk_widget_show (label);
	gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
			  GTK_FILL, GTK_FILL, 0, 0);

	label = gtk_label_new (info->value);
	gtk_label_set_selectable (GTK_LABEL (label), TRUE);
	gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
	gtk_widget_show (label);
	gtk_table_attach_defaults (GTK_TABLE (table), label, 1, 2, 0, 1);

	str = g_strconcat ("<b>", _("Path:"), "</b>", NULL);
	label = gtk_label_new (str);
	g_free (str);
	gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
	gtk_widget_show (label);
	gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2,
			  GTK_FILL, GTK_FILL, 0, 0);

	label = gtk_label_new (info->path);
	gtk_label_set_selectable (GTK_LABEL (label), TRUE);
	gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
	gtk_widget_show (label);
	gtk_table_attach_defaults (GTK_TABLE (table), label, 1, 2, 1, 2);

	str = g_strconcat ("<b>", _("Send for:"), "</b>", NULL);
	label = gtk_label_new (str);
	g_free (str);
	gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
	gtk_widget_show (label);
	gtk_table_attach (GTK_TABLE (table), label, 0, 1, 2, 3,
			  GTK_FILL, GTK_FILL, 0, 0);

	label = gtk_label_new (info->secure ? _("Encrypted connections only") : _("Any type of connection") );
	gtk_label_set_selectable (GTK_LABEL (label), TRUE);
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
	gtk_widget_show (label);
	gtk_table_attach_defaults (GTK_TABLE (table), label, 1, 2, 2, 3);

	str = g_strconcat ("<b>", _("Expires:"), "</b>", NULL);
	label = gtk_label_new (str);
	g_free (str);
	gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
	gtk_widget_show (label);
	gtk_table_attach (GTK_TABLE (table), label, 0, 1, 3, 4,
			  GTK_FILL, GTK_FILL, 0, 0);

	if (info->expires == NULL)
	{
		/* Session cookie */
		str = g_strdup (_("End of current session"));
	}
	else
	{
		struct tm t;
		time_t out = soup_date_to_time_t(info->expires);
		str = eel_strdup_strftime ("%c", localtime_r (&out, &t));
	}
	label = gtk_label_new (str);
	g_free (str);
	gtk_label_set_selectable (GTK_LABEL (label), TRUE);
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
	gtk_widget_show (label);
	gtk_table_attach_defaults (GTK_TABLE (table), label, 1, 2, 3, 4);

	gtk_box_pack_start (GTK_BOX (content_area),
			    table,
			    FALSE, FALSE, 0);

	gtk_window_group_add_window (ephy_gui_ensure_window_group (GTK_WINDOW (parent)),
				     GTK_WINDOW (gdialog));

	gtk_dialog_run (GTK_DIALOG (gdialog));

	gtk_widget_destroy (gdialog);
}

static void
cookies_properties_clicked_cb (GtkWidget *button,
			       PdmDialog *dialog)
{
	GtkTreeModel *model;
	GValue val = {0, };
	GtkTreeIter iter;
	GtkTreePath *path;
	SoupCookie *cookie;
	GList *l;
	GtkTreeSelection *selection;

	selection = gtk_tree_view_get_selection (dialog->priv->cookies->treeview);
	l = gtk_tree_selection_get_selected_rows (selection, &model);

	path = (GtkTreePath *)l->data;
	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get_value
		(model, &iter, COL_COOKIES_DATA, &val);
	cookie = (SoupCookie *) g_value_get_boxed (&val);

	show_cookies_properties (dialog, cookie);

	g_value_unset (&val);

	g_list_foreach (l, (GFunc)gtk_tree_path_free, NULL);
	g_list_free (l);
}

static void
cookies_treeview_selection_changed_cb (GtkTreeSelection *selection,
				       PdmDialog *dialog)
{
	GtkWidget *widget;
	EphyDialog *d = EPHY_DIALOG(dialog);
	gboolean has_selection;

	has_selection = gtk_tree_selection_count_selected_rows (selection) == 1;

	widget = ephy_dialog_get_control (d, "cookies_properties_button");
	gtk_widget_set_sensitive (widget, has_selection);
}

static gboolean
cookie_search_equal (GtkTreeModel *model,
		     int column,
		     const gchar *key,
		     GtkTreeIter *iter,
		     gpointer search_data)
{
	GValue value = { 0, };
	gboolean retval;

	/* Note that this is function has to return FALSE for a *match* ! */

	gtk_tree_model_get_value (model, iter, column, &value);
	retval = strstr (g_value_get_string (&value), key) == NULL;
	g_value_unset (&value);

	return retval;
}

static void
pdm_dialog_cookies_construct (PdmActionInfo *info)
{
	PdmDialog *dialog = info->dialog;
	GtkTreeView *treeview;
	GtkListStore *liststore;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeSelection *selection;
	GtkWidget *button;

	LOG ("pdm_dialog_cookies_construct");

	ephy_dialog_get_controls (EPHY_DIALOG (dialog),
				  "cookies_treeview", &treeview,
				  "cookies_properties_button", &button,
				  NULL);

	g_signal_connect (button, "clicked",
			  G_CALLBACK (cookies_properties_clicked_cb), dialog);

	/* set tree model */
	liststore = gtk_list_store_new (4,
					G_TYPE_STRING,
					G_TYPE_STRING,
					G_TYPE_STRING,
					SOUP_TYPE_COOKIE);
	gtk_tree_view_set_model (treeview, GTK_TREE_MODEL(liststore));
	gtk_tree_view_set_headers_visible (treeview, TRUE);
	selection = gtk_tree_view_get_selection (treeview);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);

	info->model = GTK_TREE_MODEL (liststore);
	g_object_unref (liststore);

	g_signal_connect (selection, "changed",
			  G_CALLBACK(cookies_treeview_selection_changed_cb),
			  dialog);

	renderer = gtk_cell_renderer_text_new ();

	gtk_tree_view_insert_column_with_attributes (treeview,
						     TV_COL_COOKIES_HOST,
						     _("Domain"),
						     renderer,
						     "text", COL_COOKIES_HOST,
						     NULL);
	column = gtk_tree_view_get_column (treeview, TV_COL_COOKIES_HOST);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_column_set_reorderable (column, TRUE);
	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_sort_column_id (column, COL_COOKIES_HOST_KEY);

	gtk_tree_view_insert_column_with_attributes (treeview,
						     TV_COL_COOKIES_NAME,
						     _("Name"),
						     renderer,
						     "text", COL_COOKIES_NAME,
						     NULL);
	column = gtk_tree_view_get_column (treeview, TV_COL_COOKIES_NAME);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_column_set_reorderable (column, TRUE);
	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_sort_column_id (column, COL_COOKIES_NAME);

	gtk_tree_view_set_enable_search (treeview, TRUE);
	gtk_tree_view_set_search_column (treeview, COL_COOKIES_HOST);
	gtk_tree_view_set_search_equal_func (treeview,
					     (GtkTreeViewSearchEqualFunc) cookie_search_equal,
					     dialog, NULL);

	info->treeview = treeview;
	info->selection = selection;

	setup_action (info);
}

static gboolean
cookie_to_iter (GtkTreeModel *model,
		const SoupCookie *cookie,
		GtkTreeIter *iter)
{
	gboolean valid;
	gboolean found = FALSE;

	valid = gtk_tree_model_get_iter_first (model, iter);

	while (valid)
	{
		SoupCookie *data;

		gtk_tree_model_get (model, iter,
				    COL_COOKIES_DATA, &data,
				    -1);

		found = soup_cookie_equal ((SoupCookie*)cookie, data);

		soup_cookie_free (data);

		if (found) break;

		valid = gtk_tree_model_iter_next (model, iter);
	}

	return found;
}

static void
cookie_changed_cb (SoupCookieJar *jar,
		   const SoupCookie *old_cookie,
		   const SoupCookie *new_cookie,
		   PdmDialog *dialog)
{
	PdmActionInfo *info;
	GtkTreeIter iter;

	g_return_if_fail (EPHY_IS_PDM_DIALOG (dialog));
	info = dialog->priv->cookies;

	LOG ("cookie_changed_cb");

	g_return_if_fail (info);

	if (old_cookie)
	{
		/* Cookie changed or deleted, let's get rid of the old one
		   in any case */
		if (cookie_to_iter (info->model, old_cookie, &iter))
		{
			gtk_list_store_remove (GTK_LIST_STORE (info->model), &iter);
		}
		else
		{
			g_warning ("Unable to find changed cookie in list!\n");
		}
	}

	if (new_cookie)
	{
		/* Cookie changed or added, let's add the new cookie in
		   any case */
		info->add (info, (gpointer) soup_cookie_copy ((SoupCookie*)new_cookie));
	}
}

static gboolean
cookie_host_to_iter (GtkTreeModel *model,
		     const char *key1,
		     GtkTreeIter *iter)
{
	GtkTreeIter iter2;
	gboolean valid;
	gssize len;
	int max = 0;

	len = strlen (key1);

	valid = gtk_tree_model_get_iter_first (model, &iter2);

	while (valid)
	{
		const char *p, *q;
		char *key2;
		int n = 0;

		gtk_tree_model_get (model, &iter2, COL_COOKIES_HOST, &key2, -1);

		/* Count the segments (string between successive dots)
		 * that key1 and key2 share.
		 */

		/* Start on the \0 */
		p = key1 + len;
		q = key2 + strlen (key2);

		do
		{
			if (*p == '.') ++n;
			--p;
			--q;
		}
		while (p >= key1 && q >= key2 && *p == *q);

		if ((p < key1 && q < key2 && *key1 != '.' && *key2 != '.') ||
		    (p < key1 && q >= key2 && *q == '.') ||
		    (q < key2 && p >= key1 && *p == '.'))
		{
			++n;
		}

		g_free (key2);

		/* Complete match */
		if (p < key1 && q < key2)
		{
			*iter = iter2;
			return TRUE;
		}

		if (n > max)
		{
			max = n;
			*iter = iter2;
		}

		valid = gtk_tree_model_iter_next (model, &iter2);
	}

	return max > 0;
}

static int
compare_cookie_host_keys (GtkTreeModel *model,
			  GtkTreeIter  *a,
			  GtkTreeIter  *b,
			  gpointer user_data)
{
	GValue a_value = {0, };
	GValue b_value = {0, };
	int retval;

	gtk_tree_model_get_value (model, a, COL_COOKIES_HOST_KEY, &a_value);
	gtk_tree_model_get_value (model, b, COL_COOKIES_HOST_KEY, &b_value);

	retval = strcmp (g_value_get_string (&a_value),
			 g_value_get_string (&b_value));

	g_value_unset (&a_value);
	g_value_unset (&b_value);

	return retval;
}

static void
pdm_dialog_fill_cookies_list (PdmActionInfo *info)
{
	SoupCookieJar *jar;
	GSList *list, *l;

	g_assert (info->filled == FALSE);

	jar = get_cookie_jar ();
	list = soup_cookie_jar_all_cookies (jar);

	for (l = list; l != NULL; l = l->next)
	{
		info->add (info, l->data);
	}

	/* the element data has been consumed, so we need only to free the list */
	g_slist_free (list);

	/* Now turn on sorting */
	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (info->model),
					 COL_COOKIES_HOST_KEY,
					 (GtkTreeIterCompareFunc) compare_cookie_host_keys,
					 NULL, NULL);
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (info->model),
					      COL_COOKIES_HOST_KEY,
					      GTK_SORT_ASCENDING);

	info->filled = TRUE;

	g_signal_connect (jar, "changed",
			  G_CALLBACK (cookie_changed_cb), info->dialog);

	info->scroll_to (info);
}

static void
pdm_dialog_cookies_destruct (PdmActionInfo *info)
{
	g_free (info->scroll_to_host);
	info->scroll_to_host = NULL;
}

static void
pdm_dialog_cookie_add (PdmActionInfo *info,
		       gpointer data)
{
	SoupCookie *cookie = (SoupCookie *) data;
	GtkListStore *store;
	GtkTreeIter iter;
	int column[4] = { COL_COOKIES_HOST, COL_COOKIES_HOST_KEY, COL_COOKIES_NAME, COL_COOKIES_DATA };
	GValue value[4] = { { 0, }, { 0, }, { 0, }, { 0, } };

	store = GTK_LIST_STORE(info->model);

	/* NOTE: We use this strange method to insert the row, because
	 * we want to use g_value_take_string but all the row data needs to
	 * be inserted in one call as it's needed when the new row is sorted
	 * into the model.
	 */

	g_value_init (&value[0], G_TYPE_STRING);
	g_value_init (&value[1], G_TYPE_STRING);
	g_value_init (&value[2], G_TYPE_STRING);
	g_value_init (&value[3], SOUP_TYPE_COOKIE);

	g_value_set_static_string (&value[0], cookie->domain);
	g_value_take_string (&value[1], ephy_string_collate_key_for_domain (cookie->domain, -1));
	g_value_set_static_string (&value[2], cookie->name);
	g_value_take_boxed (&value[3], cookie);

	gtk_list_store_insert_with_valuesv (store, &iter, -1,
					    column, value,
					    G_N_ELEMENTS (value));

	g_value_unset (&value[0]);
	g_value_unset (&value[1]);
	g_value_unset (&value[2]);
	g_value_unset (&value[3]);
}

static void
pdm_dialog_cookie_remove (PdmActionInfo *info,
			  gpointer data)
{
	SoupCookie *cookie = (SoupCookie *) data;

	soup_cookie_jar_delete_cookie (get_cookie_jar(), cookie);
}

static void
pdm_dialog_cookie_scroll_to (PdmActionInfo *info)
{
	GtkTreeIter iter;
	GtkTreePath *path;

	if (info->scroll_to_host == NULL || !info->filled) return;

	if (cookie_host_to_iter (info->model, info->scroll_to_host, &iter))
	{
		gtk_tree_selection_unselect_all (info->selection);

		path = gtk_tree_model_get_path (info->model, &iter);
		gtk_tree_view_scroll_to_cell (info->treeview,
					      path, NULL, TRUE,
					      0.5, 0.0);
		gtk_tree_path_free (path);
	}

	g_free (info->scroll_to_host);
	info->scroll_to_host = NULL;
}

/* "Passwords" tab */
static void
passwords_data_func_get_item_cb (GnomeKeyringResult result,
				 GnomeKeyringItemInfo *info,
				 gpointer data)
{
	GtkTreeRowReference *rowref = (GtkTreeRowReference *)data;

	if (result == GNOME_KEYRING_RESULT_OK) {
		GtkTreeIter iter;
		GtkTreePath *path;
		GtkTreeModel *model;

		if (!gtk_tree_row_reference_valid (rowref))
			return;

		path = gtk_tree_row_reference_get_path (rowref);
		model = gtk_tree_row_reference_get_model (rowref);

		if (path != NULL && gtk_tree_model_get_iter (model, &iter, path)) {
			EphyPasswordInfo *epinfo;
			GValue val = {0, };

			gtk_tree_model_get_value (model, &iter, COL_PASSWORDS_DATA, &val);
			epinfo = g_value_get_boxed (&val);
			epinfo->secret = gnome_keyring_memory_strdup (gnome_keyring_item_info_get_secret (info));

			gtk_list_store_set (GTK_LIST_STORE (model), &iter,
					    COL_PASSWORDS_DATA, epinfo,
					    COL_PASSWORDS_PASSWORD, epinfo->secret, -1);
			g_value_unset (&val);
		}
	}
}

static void
passwords_data_func (GtkTreeViewColumn *tree_column, GtkCellRenderer *cell,
		     GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
	GValue val = { 0, };
	EphyPasswordInfo *info;

	if (!gtk_tree_view_column_get_visible (tree_column))
		return;

	gtk_tree_model_get_value (model, iter, COL_PASSWORDS_DATA, &val);
	info = g_value_get_boxed (&val);

	if (info->secret == NULL) {
		GtkTreePath *path;
		GtkTreeRowReference *rowref;

		path = gtk_tree_model_get_path (model, iter);
		rowref = gtk_tree_row_reference_new (model, path);

		gnome_keyring_item_get_info_full (GNOME_KEYRING_DEFAULT,
						  info->keyring_id,
						  GNOME_KEYRING_ITEM_INFO_SECRET,
						  (GnomeKeyringOperationGetItemInfoCallback) passwords_data_func_get_item_cb,
						  rowref,
						  (GDestroyNotify) gtk_tree_row_reference_free);
		gtk_tree_path_free (path);
	}
	g_value_unset (&val);
}

static void
passwords_show_toggled_cb (GtkWidget *button,
			   PdmDialog *dialog)
{
	GtkTreeView *treeview;
	GtkTreeViewColumn *column;
	gboolean active;

	treeview = GTK_TREE_VIEW (ephy_dialog_get_control (EPHY_DIALOG(dialog),
							   "passwords_treeview"));
	column = gtk_tree_view_get_column (treeview, COL_PASSWORDS_PASSWORD);

	active = gtk_toggle_button_get_active ((GTK_TOGGLE_BUTTON (button)));

	gtk_tree_view_column_set_visible (column, active);
}

static void
pdm_dialog_passwords_construct (PdmActionInfo *info)
{
	PdmDialog *dialog = info->dialog;
	GtkTreeView *treeview;
	GtkListStore *liststore;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeSelection *selection;
	GtkWidget *button;

	LOG ("pdm_dialog_passwords_construct");

	ephy_dialog_get_controls (EPHY_DIALOG (dialog),
				  "passwords_treeview", &treeview,
				  "passwords_show_button", &button,
				  NULL);

	g_signal_connect (button, "toggled",
			  G_CALLBACK (passwords_show_toggled_cb), dialog);

	/* set tree model */
	liststore = gtk_list_store_new (4,
					G_TYPE_STRING,
					G_TYPE_STRING,
					G_TYPE_STRING,
					EPHY_TYPE_PASSWORD_INFO);

	gtk_tree_view_set_model (treeview, GTK_TREE_MODEL(liststore));
	gtk_tree_view_set_headers_visible (treeview, TRUE);
	selection = gtk_tree_view_get_selection (treeview);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);

	info->model = GTK_TREE_MODEL (liststore);
	g_object_unref (liststore);

	renderer = gtk_cell_renderer_text_new ();

	gtk_tree_view_insert_column_with_attributes (treeview,
						     COL_PASSWORDS_HOST,
						     _("Host"),
						     renderer,
						     "text", COL_PASSWORDS_HOST,
						     NULL);
	column = gtk_tree_view_get_column (treeview, COL_PASSWORDS_HOST);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_column_set_reorderable (column, TRUE);
	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_sort_column_id (column, COL_PASSWORDS_HOST);

	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_insert_column_with_attributes (treeview,
						     COL_PASSWORDS_USER,
						     _("User Name"),
						     renderer,
						     "text", COL_PASSWORDS_USER,
						     NULL);
	column = gtk_tree_view_get_column (treeview, COL_PASSWORDS_USER);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_column_set_reorderable (column, TRUE);
	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_sort_column_id (column, COL_PASSWORDS_USER);

	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_insert_column_with_attributes (treeview,
						     COL_PASSWORDS_PASSWORD,
						     _("User Password"),
						     renderer,
						     "text", COL_PASSWORDS_PASSWORD,
						     NULL);
	column = gtk_tree_view_get_column (treeview, COL_PASSWORDS_PASSWORD);
	gtk_tree_view_column_set_cell_data_func (column,
						 renderer,
						 passwords_data_func,
						 info,
						 NULL);
	/* Initially shown as hidden colum */
	gtk_tree_view_column_set_visible (column, FALSE);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_column_set_reorderable (column, TRUE);
	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);

	info->treeview = treeview;
	setup_action (info);
}

static void
pdm_dialog_fill_passwords_list_async_cb (GnomeKeyringResult result,
					 GList *list,
					 gpointer data)
{
	GList *l;
	PdmActionInfo *info = (PdmActionInfo *)data;

	if (result != GNOME_KEYRING_RESULT_OK)
		return;

	for (l = list; l != NULL; l = l->next)
		info->add (info, l->data);

	info->filled = TRUE;
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (info->model),
					      COL_PASSWORDS_HOST,
					      GTK_SORT_ASCENDING);
}

static void
pdm_dialog_fill_passwords_list (PdmActionInfo *info)
{
	gnome_keyring_list_item_ids (GNOME_KEYRING_DEFAULT,
				     pdm_dialog_fill_passwords_list_async_cb,
				     info,
				     NULL);
}

static void
pdm_dialog_fill_passwords_list_from_soupsession_dummy (PdmActionInfo *info)
{
	/* Do nothing, no HTTP password will be added to PDM (in private mode) because we cant delete them.

	   See https://bugzilla.gnome.org/show_bug.cgi?id=591395
	*/
}

static void
pdm_dialog_passwords_destruct (PdmActionInfo *info)
{
}

static void
pdm_dialog_password_add_item_attrs_cb (GnomeKeyringResult result,
				       GnomeKeyringAttributeList *attributes,
				       gpointer data)
{
	EphyPasswordInfo *pinfo;
	PdmCallBackData *cbdata;
	GnomeKeyringAttribute *attribute;
	gchar *user, *host, *protocol;
	GtkTreeIter iter;
	int i;

	if (result != GNOME_KEYRING_RESULT_OK)
		return;

	cbdata = (PdmCallBackData *)data;

	user = host = protocol = NULL;
	attribute = (GnomeKeyringAttribute *) attributes->data;
	for (i = 0; i < attributes->len; ++i) {
		if (attribute[i].type == GNOME_KEYRING_ATTRIBUTE_TYPE_STRING) {
			if (strcmp (attribute[i].name, "server") == 0)
				host = g_strdup (attribute[i].value.string);
			else if (strcmp (attribute[i].name, "user") == 0)
				user = g_strdup (attribute[i].value.string);
			else if (strcmp (attribute[i].name, "protocol") == 0)
				protocol = attribute[i].value.string;
			}
		}
	if (!protocol || strncmp("http", protocol, 4) != 0)
		return;

	pinfo = ephy_password_info_new (cbdata->key);
	if (!pinfo)
		return;

	gtk_list_store_append (cbdata->store, &iter);
	gtk_list_store_set (cbdata->store, &iter,
			    COL_PASSWORDS_HOST, host,
			    COL_PASSWORDS_USER, user,
			    COL_PASSWORDS_PASSWORD, NULL,
			    COL_PASSWORDS_DATA, pinfo,
			    -1);
}

static void
pdm_dialog_password_add_item_info_cb (GnomeKeyringResult result,
				      GnomeKeyringItemInfo *info,
				      gpointer data)
{
	if (result != GNOME_KEYRING_RESULT_OK)
		return;

	if (gnome_keyring_item_info_get_type (info) == GNOME_KEYRING_ITEM_NETWORK_PASSWORD) {
		PdmCallBackData *cbdata = (PdmCallBackData *)data;
		gnome_keyring_item_get_attributes (GNOME_KEYRING_DEFAULT,
						   cbdata->key,
						   (GnomeKeyringOperationGetAttributesCallback) pdm_dialog_password_add_item_attrs_cb,
						   g_memdup (cbdata, sizeof (PdmCallBackData)),
						   (GDestroyNotify) g_free);

	}

}

static void
pdm_dialog_password_add (PdmActionInfo *info,
			 gpointer data)
{
	PdmCallBackData *cbdata;
	guint key_id = GPOINTER_TO_UINT(data);

	/*
	 * We have the item id of the password. We will have to check if this
	 * password entry is of the right type and then can proceed to get the
	 * the private information. Seahorse is treating every protocol that
	 * starts with http as Web Access and we will do the same here.
	 */

	cbdata = g_malloc (sizeof (PdmCallBackData));
	cbdata->key = key_id;
	cbdata->store = GTK_LIST_STORE (info->model);

	/* Get the type of the key_id */
	gnome_keyring_item_get_info_full (GNOME_KEYRING_DEFAULT,
					  key_id,
					  GNOME_KEYRING_ITEM_INFO_BASICS,
					  (GnomeKeyringOperationGetItemInfoCallback) pdm_dialog_password_add_item_info_cb,
					  cbdata,
					  (GDestroyNotify) g_free);
}

static void
pdm_dialog_password_remove_dummy_cb (GnomeKeyringResult result,
				     gpointer data)
{
}

static void
pdm_dialog_password_remove (PdmActionInfo *info,
			    gpointer data)
{
	EphyPasswordInfo *pinfo = (EphyPasswordInfo *) data;

	gnome_keyring_item_delete (GNOME_KEYRING_DEFAULT,
				   pinfo->keyring_id,
				   (GnomeKeyringOperationDoneCallback) pdm_dialog_password_remove_dummy_cb,
				   NULL,
				   NULL);
}

/* common routines */

static void
sync_notebook_tab (GtkWidget *notebook,
		   GtkWidget *page,
		   int page_num,
		   PdmDialog *dialog)
{
	PdmDialogPrivate *priv = dialog->priv;

	/* Lazily fill the list store */
	if (page_num == 0 && priv->cookies->filled == FALSE)
	{
		priv->cookies->fill (priv->cookies);
		priv->cookies->scroll_to (priv->cookies);
	}
	else if (page_num == 1 && priv->passwords->filled == FALSE)
	{
		priv->passwords->fill (priv->passwords);
	}
}

static void
pdm_dialog_response_cb (GtkDialog *widget,
			int response,
			PdmDialog *dialog)
{
	if (response == GTK_RESPONSE_HELP)
	{
		pdm_dialog_show_help (dialog);
		return;
	}
	if (response == PDM_DIALOG_RESPONSE_CLEAR)
	{
		int page;
		GtkWidget *parent;

		parent = ephy_dialog_get_control (EPHY_DIALOG (dialog),
						  "pdm_dialog");

		page = gtk_notebook_get_current_page (GTK_NOTEBOOK (dialog->priv->notebook));
		if (page == 0)
		{
			/* Cookies */
			pdm_dialog_show_clear_all_dialog (EPHY_DIALOG (dialog),
							  parent, CLEAR_ALL_COOKIES);
		}
		if (page == 1)
		{
			/* Passwords */
			pdm_dialog_show_clear_all_dialog (EPHY_DIALOG (dialog),
							  parent, CLEAR_ALL_PASSWORDS);
		}
		return;
	}

	g_object_unref (dialog);
}

static void
pdm_dialog_init (PdmDialog *dialog)
{
	PdmDialogPrivate *priv;
	PdmActionInfo *cookies, *passwords;
	GtkWidget *window;
	gboolean has_private_profile = ephy_has_private_profile ();


	priv = dialog->priv = EPHY_PDM_DIALOG_GET_PRIVATE (dialog);

	ephy_dialog_construct (EPHY_DIALOG(dialog),
			       ephy_file ("epiphany.ui"),
			       "pdm_dialog",
			       NULL);

	ephy_dialog_get_controls (EPHY_DIALOG (dialog),
				  "pdm_dialog", &window,
				  "pdm_notebook", &priv->notebook,
				  NULL);

	ephy_gui_ensure_window_group (GTK_WINDOW (window));

	gtk_window_set_icon_name (GTK_WINDOW (window), EPHY_STOCK_EPHY);

	g_signal_connect (window, "response",
			  G_CALLBACK (pdm_dialog_response_cb), dialog);
	/*
	 * Group all Properties and Remove buttons in the same size group to
	 * avoid the little jerk you get otherwise when switching pages because
	 * one set of buttons is wider than another.
	 */
	ephy_dialog_set_size_group (EPHY_DIALOG (dialog),
				    "cookies_remove_button",
				    "cookies_properties_button",
				    "passwords_remove_button",
				    NULL);

	cookies = g_new0 (PdmActionInfo, 1);
	cookies->construct = pdm_dialog_cookies_construct;
	cookies->destruct = pdm_dialog_cookies_destruct;
	cookies->fill = pdm_dialog_fill_cookies_list;
	cookies->add = pdm_dialog_cookie_add;
	cookies->remove = pdm_dialog_cookie_remove;
	cookies->scroll_to = pdm_dialog_cookie_scroll_to;
	cookies->dialog = dialog;
	cookies->remove_id = "cookies_remove_button";
	cookies->data_col = COL_COOKIES_DATA;
	cookies->scroll_to_host = NULL;
	cookies->filled = FALSE;
	cookies->delete_row_on_remove = FALSE;

	passwords = g_new0 (PdmActionInfo, 1);
	passwords->construct = pdm_dialog_passwords_construct;
	passwords->destruct = pdm_dialog_passwords_destruct;
	/* Bug 591395 : we dont show HTTP auth in private mode because we can delete or use it anyway.  */
	passwords->fill = has_private_profile ? pdm_dialog_fill_passwords_list_from_soupsession_dummy : pdm_dialog_fill_passwords_list;
	passwords->add = pdm_dialog_password_add;
	passwords->remove = pdm_dialog_password_remove;
	passwords->dialog = dialog;
	passwords->remove_id = "passwords_remove_button";
	passwords->data_col = COL_PASSWORDS_DATA;
	passwords->scroll_to_host = NULL;
	passwords->filled = FALSE;
	passwords->delete_row_on_remove = TRUE;

	priv->cookies = cookies;
	priv->passwords = passwords;

	cookies->construct (cookies);
	passwords->construct (passwords);

	sync_notebook_tab (priv->notebook, NULL, 0, dialog);
	g_signal_connect (G_OBJECT (priv->notebook), "switch_page",
			  G_CALLBACK (sync_notebook_tab), dialog);
}

static void
pdm_dialog_finalize (GObject *object)
{
	PdmDialog *dialog = EPHY_PDM_DIALOG (object);
	GObject *single;

	single = ephy_embed_shell_get_embed_single (embed_shell);

	g_signal_handlers_disconnect_matched
		(single, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, object);

	g_signal_handlers_disconnect_by_func (get_cookie_jar (), cookie_changed_cb, object);

	dialog->priv->cookies->destruct (dialog->priv->cookies);
	dialog->priv->passwords->destruct (dialog->priv->passwords);

	g_free (dialog->priv->passwords);
	g_free (dialog->priv->cookies);

	G_OBJECT_CLASS (pdm_dialog_parent_class)->finalize (object);
}

void
pdm_dialog_open (PdmDialog *dialog,
		 const char *host)
{
	PdmDialogPrivate *priv = dialog->priv;

	/* Switch to cookies tab */
	gtk_notebook_set_current_page (GTK_NOTEBOOK (priv->notebook), 0);

	g_free (priv->cookies->scroll_to_host);
	priv->cookies->scroll_to_host = g_strdup (host);

	priv->cookies->scroll_to (priv->cookies);
	gtk_widget_grab_focus (GTK_WIDGET (priv->cookies->treeview));

	ephy_dialog_show (EPHY_DIALOG (dialog));
}
