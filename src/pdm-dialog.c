/*
 *  Copyright (C) 2002 Jorn Baayen
 *  Copyright (C) 2003 Marco Pesenti Gritti
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pdm-dialog.h"
#include "ephy-shell.h"
#include "ephy-cookie-manager.h"
#include "ephy-file-helpers.h"
#include "ephy-password-manager.h"
#include "ephy-gui.h"
#include "ephy-ellipsizing-label.h"
#include "ephy-debug.h"
#include "ephy-state.h"

#include <gtk/gtktreeview.h>
#include <gtk/gtkliststore.h>
#include <gtk/gtkcellrenderertext.h>

#include <glib/gi18n.h>

#include <time.h>
#include <string.h>

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

	/* Data */
	PdmDialog *dialog;
	GtkTreeView *treeview;
	GtkTreeModel *model;
	int remove_id;
	int data_col;
	gboolean filled;
	gboolean delete_row_on_remove;
};

#define EPHY_PDM_DIALOG_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE ((object), EPHY_TYPE_PDM_DIALOG, PdmDialogPrivate))

struct PdmDialogPrivate
{
	GtkTreeModel *model;
	PdmActionInfo *cookies;
	PdmActionInfo *passwords;
};

enum
{
	COL_COOKIES_HOST,
	COL_COOKIES_NAME,
	COL_COOKIES_PATH,
	COL_COOKIES_DATA
};

enum
{
	COL_PASSWORDS_HOST,
	COL_PASSWORDS_USER,
	COL_PASSWORDS_DATA
};

enum
{
	PROP_WINDOW,
	PROP_NOTEBOOK,
	PROP_COOKIES_TREEVIEW,
	PROP_COOKIES_REMOVE,
	PROP_COOKIES_PROPERTIES,
	PROP_PASSWORDS_TREEVIEW,
	PROP_PASSWORDS_REMOVE
};

static const
EphyDialogProperty properties [] =
{
	{ "pdm_dialog",			NULL, PT_NORMAL, 0 },
	{ "pdm_notebook",		NULL, PT_NORMAL, 0 },

	{ "cookies_treeview",		NULL, PT_NORMAL, 0 },
	{ "cookies_remove_button",	NULL, PT_NORMAL, 0 },
	{ "cookies_properties_button",	NULL, PT_NORMAL, 0 },
	{ "passwords_treeview",		NULL, PT_NORMAL, 0 },
	{ "passwords_remove_button",	NULL, PT_NORMAL, 0 },

	{ NULL }
};

static void pdm_dialog_class_init	(PdmDialogClass *klass);
static void pdm_dialog_init		(PdmDialog *dialog);
static void pdm_dialog_finalize		(GObject *object);

/* Glade callbacks */
void pdm_dialog_close_button_clicked_cb 		(GtkWidget *button,
							 PdmDialog *dialog);
void pdm_dialog_cookies_properties_button_clicked_cb	(GtkWidget *button,
							 PdmDialog *dialog);
void pdm_dialog_cookies_treeview_selection_changed_cb	(GtkTreeSelection *selection,
							 PdmDialog *dialog);
void pdm_dialog_passwords_treeview_selection_changed_cb	(GtkTreeSelection *selection,
							 PdmDialog *dialog);
void pdm_dialog_response_cb (GtkDialog *widget,
			     int response,
			     PdmDialog *dialog);

static GObjectClass *parent_class = NULL;

GType 
pdm_dialog_get_type (void)
{
	static GType type = 0;

	if (type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (PdmDialogClass),
			NULL, /* base_init */
			NULL, /* base_finalize */
			(GClassInitFunc) pdm_dialog_class_init,
			NULL,
			NULL, /* class_data */
			sizeof (PdmDialog),
			0, /* n_preallocs */
			(GInstanceInitFunc) pdm_dialog_init
		};

		type = g_type_register_static (EPHY_TYPE_DIALOG,
					       "PdmDialog",
					       &our_info, 0);
	}

	return type;
}

static void
pdm_dialog_class_init (PdmDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = pdm_dialog_finalize;

	g_type_class_add_private (object_class, sizeof(PdmDialogPrivate));
}

static void
pdm_dialog_show_help (PdmDialog *pd)
{
	GtkWidget *notebook, *window;
	int id;

	char *help_preferences[] = {
		"managing-cookies",
		"managing-passwords"
	};

	window = ephy_dialog_get_control (EPHY_DIALOG (pd), properties[PROP_WINDOW].id);
	g_return_if_fail (GTK_IS_WINDOW (window));

	notebook = ephy_dialog_get_control (EPHY_DIALOG (pd), properties[PROP_NOTEBOOK].id);
	g_return_if_fail (notebook != NULL);

	id = gtk_notebook_get_current_page (GTK_NOTEBOOK (notebook));
	g_assert (id == 0 || id == 1);

	ephy_gui_help (GTK_WINDOW (window), "epiphany", help_preferences[id]);
}

static void
action_treeview_selection_changed_cb (GtkTreeSelection *selection,
				      PdmActionInfo *action)
{
	GtkWidget *widget;
	EphyDialog *d = EPHY_DIALOG(action->dialog);
	gboolean has_selection;

	has_selection = gtk_tree_selection_count_selected_rows (selection) > 0;

	widget = ephy_dialog_get_control (d, properties[action->remove_id].id);
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

		path = gtk_tree_row_reference_get_path
			((GtkTreeRowReference *)r->data);

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
	if (event->keyval == GDK_Delete || event->keyval == GDK_KP_Delete)
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
}

static void
setup_action (PdmActionInfo *action)
{
	GtkWidget *widget;
	GtkTreeSelection *selection;

	widget = ephy_dialog_get_control (EPHY_DIALOG(action->dialog),
					  properties[action->remove_id].id);
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
cookies_treeview_selection_changed_cb (GtkTreeSelection *selection,
				       PdmDialog *dialog)
{
	GtkWidget *widget;
	EphyDialog *d = EPHY_DIALOG(dialog);
	gboolean has_selection;

	has_selection = gtk_tree_selection_count_selected_rows (selection) == 1;

	widget = ephy_dialog_get_control (d, properties[PROP_COOKIES_PROPERTIES].id);
	gtk_widget_set_sensitive (widget, has_selection);
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

	LOG ("pdm_dialog_cookies_construct")

	treeview = GTK_TREE_VIEW (ephy_dialog_get_control
			(EPHY_DIALOG (dialog), properties[PROP_COOKIES_TREEVIEW].id));

	/* set tree model */
	liststore = gtk_list_store_new (4,
					G_TYPE_STRING,
					G_TYPE_STRING,
					G_TYPE_STRING,
					EPHY_TYPE_COOKIE);
	gtk_tree_view_set_model (treeview, GTK_TREE_MODEL(liststore));
	gtk_tree_view_set_headers_visible (treeview, TRUE);
	selection = gtk_tree_view_get_selection (treeview);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (liststore),
					      COL_COOKIES_HOST,
					      GTK_SORT_ASCENDING);
	info->model = GTK_TREE_MODEL (liststore);
	g_object_unref (liststore);

	g_signal_connect (selection, "changed",
			  G_CALLBACK(cookies_treeview_selection_changed_cb),
			  dialog);

	renderer = gtk_cell_renderer_text_new ();

	gtk_tree_view_insert_column_with_attributes (treeview,
						     COL_COOKIES_HOST,
						     _("Domain"),
						     renderer,
						     "text", COL_COOKIES_HOST,
						     NULL);
	column = gtk_tree_view_get_column (treeview, COL_COOKIES_HOST);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_column_set_reorderable (column, TRUE);
	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_sort_column_id (column, COL_COOKIES_HOST);

	gtk_tree_view_insert_column_with_attributes (treeview,
						     COL_COOKIES_NAME,
						     _("Name"),
						     renderer,
						     "text", COL_COOKIES_NAME,
						     NULL);
	column = gtk_tree_view_get_column (treeview, COL_COOKIES_NAME);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_column_set_reorderable (column, TRUE);
	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_sort_column_id (column, COL_COOKIES_NAME);

	info->treeview = treeview;

	setup_action (info);
}

static gboolean
compare_cookies (const EphyCookie *cookie1,
		 const EphyCookie *cookie2)
{
	g_return_val_if_fail (cookie1 != NULL || cookie2 != NULL, FALSE);

	return (strcmp (cookie1->domain, cookie2->domain) == 0
		&& strcmp (cookie1->path, cookie2->path) == 0
		&& strcmp (cookie1->name, cookie2->name) == 0);
}

static gboolean
cookie_to_iter (GtkTreeModel *model,
		const EphyCookie *cookie,
		GtkTreeIter *iter)
{
	gboolean valid;
	gboolean found = FALSE;

	valid = gtk_tree_model_get_iter_first (model, iter);

	while (valid)
	{
		EphyCookie *data;

		gtk_tree_model_get (model, iter,
				    COL_COOKIES_DATA, &data,
				    -1);

		found = compare_cookies (cookie, data);

		ephy_cookie_free (data);

		if (found) break;

		valid = gtk_tree_model_iter_next (model, iter);
	}

	return found;
}

static void
cookie_added_cb (EphyCookieManager *manager,
		 const EphyCookie *cookie,
		 PdmDialog *dialog)
{
	PdmActionInfo *info = dialog->priv->cookies;
	
	LOG ("cookie_added_cb")

	info->add (info, (gpointer) ephy_cookie_copy (cookie));
}

static void
cookie_changed_cb (EphyCookieManager *manager,
		   const EphyCookie *cookie,
		   PdmDialog *dialog)
{
	PdmActionInfo *info = dialog->priv->cookies;
	GtkTreeIter iter;

	LOG ("cookie_changed_cb")

	if (cookie_to_iter (info->model, cookie, &iter))
	{
		gtk_list_store_remove (GTK_LIST_STORE (info->model), &iter);		
		info->add (info, (gpointer) ephy_cookie_copy (cookie));
	}
	else
	{
		g_warning ("Unable to find changed cookie in list!\n");
	}
}

static void
cookie_deleted_cb (EphyCookieManager *manager,
		   const EphyCookie *cookie,
		   PdmDialog *dialog)
{
	PdmActionInfo *info = dialog->priv->cookies;
	GtkTreeIter iter;

	LOG ("cookie_deleted_cb")

	if (cookie_to_iter (info->model, cookie, &iter))
	{
		gtk_list_store_remove (GTK_LIST_STORE (info->model), &iter);		
	}
	else
	{
		g_warning ("Unable to find deleted cookie in list!\n");
	}
}

static void
cookies_cleared_cb (EphyCookieManager *manager,
		    PdmDialog *dialog)
{
	PdmActionInfo *info = dialog->priv->cookies;
	GtkTreeIter iter;
	gboolean valid;

	LOG ("cookies_cleared_cb")

	valid = gtk_tree_model_get_iter_first (info->model, &iter);

	while (valid)
	{
		gtk_list_store_remove (GTK_LIST_STORE (info->model), &iter);		

		valid = gtk_tree_model_iter_next (info->model, &iter);
	}
}

static void
pdm_dialog_fill_cookies_list (PdmActionInfo *info)
{
	EphyCookieManager *manager;
	GList *list, *l;

	g_assert (info->filled == FALSE);

	manager = EPHY_COOKIE_MANAGER (ephy_embed_shell_get_embed_single
			(EPHY_EMBED_SHELL (ephy_shell)));

	list = ephy_cookie_manager_list_cookies (manager);

	for (l = list; l != NULL; l = l->next)
	{
		info->add (info, l->data);
	}

	/* the element data has been consumed, so we need only to free the list */
	g_list_free (list);

	info->filled = TRUE;

	/* Now connect the callbacks on the EphyCookieManager */
	g_signal_connect_object (manager, "cookie-added",
				 G_CALLBACK (cookie_added_cb), info->dialog, 0);
	g_signal_connect_object (manager, "cookie-changed",
				 G_CALLBACK (cookie_changed_cb), info->dialog, 0);
	g_signal_connect_object (manager, "cookie-deleted",
				 G_CALLBACK (cookie_deleted_cb), info->dialog, 0);
	g_signal_connect_object (manager, "cookies-cleared",
			  	 G_CALLBACK (cookies_cleared_cb), info->dialog, 0);
}

static void
pdm_dialog_cookies_destruct (PdmActionInfo *info)
{
}

static void
pdm_dialog_cookie_add (PdmActionInfo *info,
		       gpointer data)
{
	EphyCookie *cookie = (EphyCookie *) data;
	GtkListStore *store;
	GtkTreeIter iter;
	GValue value = { 0, };

	store = GTK_LIST_STORE(info->model);

	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter,
			    COL_COOKIES_HOST, cookie->domain,
			    COL_COOKIES_NAME, cookie->name,
			    COL_COOKIES_PATH, cookie->path,
			    -1);

	g_value_init (&value, EPHY_TYPE_COOKIE);
	g_value_take_boxed (&value, cookie);
	gtk_list_store_set_value (store, &iter, COL_COOKIES_DATA, &value);
	g_value_unset (&value);
}

static void
pdm_dialog_cookie_remove (PdmActionInfo *info,
			  gpointer data)
{
	EphyCookie *cookie = (EphyCookie *) data;
	EphyCookieManager *manager;

	manager = EPHY_COOKIE_MANAGER (ephy_embed_shell_get_embed_single
			(EPHY_EMBED_SHELL (ephy_shell)));

	ephy_cookie_manager_remove_cookie (manager, cookie);
}

/* "Passwords" tab */

static void
pdm_dialog_passwords_construct (PdmActionInfo *info)
{
	PdmDialog *dialog = info->dialog;
	GtkTreeView *treeview;
	GtkListStore *liststore;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeSelection *selection;

	LOG ("pdm_dialog_passwords_construct")

	treeview = GTK_TREE_VIEW (ephy_dialog_get_control
			(EPHY_DIALOG(dialog), properties[PROP_PASSWORDS_TREEVIEW].id));

	/* set tree model */
	liststore = gtk_list_store_new (3,
					G_TYPE_STRING,
					G_TYPE_STRING,
					EPHY_TYPE_PASSWORD_INFO);
	gtk_tree_view_set_model (treeview, GTK_TREE_MODEL(liststore));
	gtk_tree_view_set_headers_visible (treeview, TRUE);
	selection = gtk_tree_view_get_selection (treeview);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (liststore),
					      COL_PASSWORDS_HOST,
					      GTK_SORT_ASCENDING);
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

	info->treeview = treeview;

	setup_action (info);
}

static void
pdm_dialog_fill_passwords_list (PdmActionInfo *info)
{
	EphyPasswordManager *manager;
	GList *list, *l;

	g_assert (info->filled == FALSE);

	manager = EPHY_PASSWORD_MANAGER (ephy_embed_shell_get_embed_single
			(EPHY_EMBED_SHELL (ephy_shell)));

	list = ephy_password_manager_list (manager);

	for (l = list; l != NULL; l = l->next)
	{
		info->add (info, l->data);
	}

	/* the element data has been consumed, so we need only to free the list */
	g_list_free (list);

	info->filled = TRUE;
}

static void
pdm_dialog_passwords_destruct (PdmActionInfo *info)
{
}

static void
pdm_dialog_password_add (PdmActionInfo *info,
			 gpointer data)
{
	EphyPasswordInfo *pinfo = (EphyPasswordInfo *) data;
	GtkListStore *store;
	GtkTreeIter iter;
	GValue value = { 0, };

	store = GTK_LIST_STORE (info->model);

	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store,
			    &iter,
			    COL_PASSWORDS_HOST, pinfo->host,
			    COL_PASSWORDS_USER, pinfo->username,
			    -1);

	g_value_init (&value, EPHY_TYPE_PASSWORD_INFO);
	g_value_take_boxed (&value, pinfo);
	gtk_list_store_set_value (store, &iter, COL_PASSWORDS_DATA, &value);
	g_value_unset (&value);
}

static void
pdm_dialog_password_remove (PdmActionInfo *info,
			    gpointer data)
{
	EphyPasswordInfo *pinfo = (EphyPasswordInfo *) data;
	EphyPasswordManager *manager;

	manager = EPHY_PASSWORD_MANAGER (ephy_embed_shell_get_embed_single
			(EPHY_EMBED_SHELL (ephy_shell)));

	ephy_password_manager_remove (manager, pinfo);
}

/* common routines */

static void
sync_notebook_tab (GtkWidget *notebook,
		   GtkNotebookPage *page,
		   int page_num,
		   PdmDialog *dialog)
{
	/* Lazily fill the list store */
	if (page_num == 0 && dialog->priv->cookies->filled == FALSE)
	{
		dialog->priv->cookies->fill (dialog->priv->cookies);
	}
	else if (page_num == 1 && dialog->priv->passwords->filled == FALSE)
	{
		dialog->priv->passwords->fill (dialog->priv->passwords);
	}
}

static void
pdm_dialog_init (PdmDialog *dialog)
{
	PdmActionInfo *cookies, *passwords;
	GtkWidget *notebook;

	dialog->priv = EPHY_PDM_DIALOG_GET_PRIVATE (dialog);

	dialog->priv->cookies = NULL;
	dialog->priv->passwords = NULL;

	ephy_dialog_construct (EPHY_DIALOG(dialog),
			       properties,
			       ephy_file ("epiphany.glade"),
			       "pdm_dialog",
			       NULL);

	/**
	 * Group all Properties and Remove buttons in the same size group to
	 * avoid the little jerk you get otherwise when switching pages because
	 * one set of buttons is wider than another.
	 */
	ephy_dialog_set_size_group (EPHY_DIALOG (dialog),
				    properties[PROP_COOKIES_REMOVE].id,
				    properties[PROP_COOKIES_PROPERTIES].id,
				    properties[PROP_PASSWORDS_REMOVE].id,
				    NULL);

	cookies = g_new0 (PdmActionInfo, 1);
	cookies->construct = pdm_dialog_cookies_construct;
	cookies->destruct = pdm_dialog_cookies_destruct;
	cookies->fill = pdm_dialog_fill_cookies_list;
	cookies->add = pdm_dialog_cookie_add;
	cookies->remove = pdm_dialog_cookie_remove;
	cookies->dialog = dialog;
	cookies->remove_id = PROP_COOKIES_REMOVE;
	cookies->data_col = COL_COOKIES_DATA;
	cookies->filled = FALSE;
	cookies->delete_row_on_remove = FALSE;

	passwords = g_new0 (PdmActionInfo, 1);
	passwords->construct = pdm_dialog_passwords_construct;
	passwords->destruct = pdm_dialog_passwords_destruct;
	passwords->fill = pdm_dialog_fill_passwords_list;
	passwords->add = pdm_dialog_password_add;
	passwords->remove = pdm_dialog_password_remove;
	passwords->dialog = dialog;
	passwords->remove_id = PROP_PASSWORDS_REMOVE;
	passwords->data_col = COL_PASSWORDS_DATA;
	passwords->filled = FALSE;
	passwords->delete_row_on_remove = TRUE;

	dialog->priv->cookies = cookies;
	dialog->priv->passwords = passwords;

	cookies->construct (cookies);
	passwords->construct (passwords);

	notebook = ephy_dialog_get_control (EPHY_DIALOG (dialog), properties[PROP_NOTEBOOK].id);
	sync_notebook_tab (notebook, NULL, 0, dialog);
	g_signal_connect (G_OBJECT (notebook), "switch_page",
			  G_CALLBACK (sync_notebook_tab), dialog);
}

static void
pdm_dialog_finalize (GObject *object)
{
	PdmDialog *dialog = EPHY_PDM_DIALOG (object);

	dialog->priv->cookies->destruct (dialog->priv->cookies);
	dialog->priv->passwords->destruct (dialog->priv->passwords);

	g_free (dialog->priv->passwords);
	g_free (dialog->priv->cookies);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
show_cookies_properties (PdmDialog *dialog,
			 EphyCookie *info)
{
	GtkWidget *gdialog;
	GtkWidget *table;
	GtkWidget *label;
	GtkWidget *parent;
	GtkWidget *dialog_vbox;
	char *str;

	parent = ephy_dialog_get_control (EPHY_DIALOG(dialog),
					  properties[PROP_WINDOW].id);

	gdialog = gtk_dialog_new_with_buttons
		 (_("Cookie Properties"),
		  GTK_WINDOW(parent),
		  GTK_DIALOG_MODAL,
		  GTK_STOCK_CLOSE, 0, NULL);
	ephy_state_add_window (GTK_WIDGET (gdialog), "cookie_properties", 
			       -1, -1, EPHY_STATE_WINDOW_SAVE_SIZE | EPHY_STATE_WINDOW_SAVE_POSITION);
	gtk_dialog_set_has_separator (GTK_DIALOG(gdialog), FALSE);
	gtk_container_set_border_width (GTK_CONTAINER(gdialog), 6);

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

	label = ephy_ellipsizing_label_new (info->value);
	gtk_label_set_selectable (GTK_LABEL (label), TRUE);
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
	gtk_widget_show (label);
	gtk_table_attach_defaults (GTK_TABLE (table), label, 1, 2, 0, 1);

	str = g_strconcat ("<b>", _("Path:"), "</b>", NULL);
	label = ephy_ellipsizing_label_new (str);
	g_free (str);
	gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
	gtk_widget_show (label);
	gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2,
			  GTK_FILL, GTK_FILL, 0, 0);

	label = gtk_label_new (info->path);
	gtk_label_set_selectable (GTK_LABEL (label), TRUE);
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
	gtk_widget_show (label);
	gtk_table_attach_defaults (GTK_TABLE (table), label, 1, 2, 1, 2);

	str = g_strconcat ("<b>", _("Secure:"), "</b>", NULL);
	label = gtk_label_new (str);
	g_free (str);
	gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
	gtk_widget_show (label);
	gtk_table_attach (GTK_TABLE (table), label, 0, 1, 2, 3,
			  GTK_FILL, GTK_FILL, 0, 0);

	label = gtk_label_new (info->is_secure ? _("Yes") : _("No") );
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

	if (info->is_session)
	{
		str = g_strdup (_("End of current session"));
	}
	else
	{
		struct tm t;
		char s[128];
		const char *fmt_hack = "%c";
		strftime (s, sizeof(s), fmt_hack, localtime_r (&info->expires, &t));
		str = g_locale_to_utf8 (s, -1, NULL, NULL, NULL);
	}
	label = gtk_label_new (str);
	g_free (str);
	gtk_label_set_selectable (GTK_LABEL (label), TRUE);
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
	gtk_widget_show (label);
	gtk_table_attach_defaults (GTK_TABLE (table), label, 1, 2, 3, 4);

	dialog_vbox = GTK_DIALOG(gdialog)->vbox;
	gtk_box_pack_start (GTK_BOX(dialog_vbox),
			    table,
			    FALSE, FALSE, 0);

	gtk_dialog_run (GTK_DIALOG(gdialog));

	gtk_widget_destroy (gdialog);
}

void
pdm_dialog_cookies_properties_button_clicked_cb (GtkWidget *button,
						 PdmDialog *dialog)
{
	GtkTreeModel *model;
	GValue val = {0, };
	GtkTreeIter iter;
	GtkTreePath *path;
	EphyCookie *cookie;
	GList *l;
	GtkTreeSelection *selection;

	selection = gtk_tree_view_get_selection (dialog->priv->cookies->treeview);
	l = gtk_tree_selection_get_selected_rows
		(selection, &model);

	path = (GtkTreePath *)l->data;
	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get_value
		(model, &iter, COL_COOKIES_DATA, &val);
	cookie = (EphyCookie *) g_value_get_boxed (&val);

	show_cookies_properties (dialog, cookie);

	g_value_unset (&val);

	g_list_foreach (l, (GFunc)gtk_tree_path_free, NULL);
	g_list_free (l);
}

void
pdm_dialog_response_cb (GtkDialog *widget,
			gint response,
			PdmDialog *dialog)
{
	switch (response)
	{
		case GTK_RESPONSE_CLOSE:
			g_object_unref (dialog);
			break;
		case GTK_RESPONSE_HELP:
			pdm_dialog_show_help (dialog);
			break;
		default:
			break;
	}
}
