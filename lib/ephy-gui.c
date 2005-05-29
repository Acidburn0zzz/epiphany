/*
 *  Copyright (C) 2002 Marco Pesenti Gritti
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

#include "ephy-gui.h"
#include "eel-gconf-extensions.h"
#include "ephy-debug.h"

#include <ctype.h>
#include <string.h>
#include <glib/gi18n.h>
#include <gdk/gdkx.h>
#include <libgnome/gnome-help.h>
#include <gtk/gtktreemodel.h>
#include <gtk/gtkmessagedialog.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtkimage.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkstock.h>
#include <gtk/gtkmain.h>
#include <gtk/gtktreeselection.h>

#include <unistd.h>

void
ephy_gui_sanitise_popup_position (GtkMenu *menu,
				  GtkWidget *widget,
				  gint *x,
				  gint *y)
{
	GdkScreen *screen = gtk_widget_get_screen (widget);
	gint monitor_num;
	GdkRectangle monitor;
	GtkRequisition req;

	g_return_if_fail (widget != NULL);

	gtk_widget_size_request (GTK_WIDGET (menu), &req);

	monitor_num = gdk_screen_get_monitor_at_point (screen, *x, *y);
	gtk_menu_set_monitor (menu, monitor_num);
	gdk_screen_get_monitor_geometry (screen, monitor_num, &monitor);

	*x = CLAMP (*x, monitor.x, monitor.x + MAX (0, monitor.width - req.width));
	*y = CLAMP (*y, monitor.y, monitor.y + MAX (0, monitor.height - req.height));
}

void
ephy_gui_menu_position_tree_selection (GtkMenu   *menu,
				       gint      *x,
				       gint      *y,
				       gboolean  *push_in,
				       gpointer  user_data)
{
	GtkTreeSelection *selection;
	GList *selected_rows;
	GtkTreeModel *model;
	GtkTreeView *tree_view = GTK_TREE_VIEW (user_data);
	GtkWidget *widget = GTK_WIDGET (user_data);
	GtkRequisition req;
	GdkRectangle visible;

	gtk_widget_size_request (GTK_WIDGET (menu), &req);
	gdk_window_get_origin (widget->window, x, y);

	*x += (widget->allocation.width - req.width) / 2;

	/* Add on height for the treeview title */
	gtk_tree_view_get_visible_rect (tree_view, &visible);
	*y += widget->allocation.height - visible.height;

	selection = gtk_tree_view_get_selection (tree_view);
	selected_rows = gtk_tree_selection_get_selected_rows (selection, &model);
	if (selected_rows)
	{
		GdkRectangle cell_rect;

		gtk_tree_view_get_cell_area (tree_view, selected_rows->data,
					     NULL, &cell_rect);

		*y += CLAMP (cell_rect.y + cell_rect.height, 0, visible.height);

		g_list_foreach (selected_rows, (GFunc)gtk_tree_path_free, NULL);
		g_list_free (selected_rows);
	}

	ephy_gui_sanitise_popup_position (menu, widget, x, y);
}

/**
 * ephy_gui_menu_position_under_widget:
 */
void
ephy_gui_menu_position_under_widget (GtkMenu   *menu,
				     gint      *x,
				     gint      *y,
				     gboolean  *push_in,
				     gpointer	user_data)
{
	GtkWidget *w = GTK_WIDGET (user_data);
	GtkRequisition requisition;

	gdk_window_get_origin (w->window, x, y);
	gtk_widget_size_request (GTK_WIDGET (menu), &requisition);

	if (gtk_widget_get_direction (w) == GTK_TEXT_DIR_RTL)
	{
		*x += w->allocation.x + w->allocation.width - requisition.width;
	}
	else
	{
		*x += w->allocation.x;
	}

	*y += w->allocation.y + w->allocation.height;

	ephy_gui_sanitise_popup_position (menu, w, x, y);
}

void
ephy_gui_menu_position_on_panel (GtkMenu *menu,
				 gint      *x,
				 gint      *y,
				 gboolean  *push_in,
				 gpointer  user_data)
{
	GtkWidget *widget = GTK_WIDGET (user_data);
	GtkRequisition requisition;
	GdkScreen *screen;

	screen = gtk_widget_get_screen (widget);

	gdk_window_get_origin (widget->window, x, y);
	gtk_widget_size_request (GTK_WIDGET (menu), &requisition);

	if (GTK_WIDGET_NO_WINDOW (widget))
	{
		*x += widget->allocation.x;
		*y += widget->allocation.y;
	}

	/* FIXME: Adapt to vertical panels, but egg_tray_icon_get_orientation doesn't seem to work */
	if (*y > gdk_screen_get_height (screen) / 2)
	{
		*y -= requisition.height;
	}
	else
	{
		*y += widget->allocation.height;
	}

	*push_in = FALSE;

	ephy_gui_sanitise_popup_position (menu, widget, x, y);
}

GtkWindowGroup *
ephy_gui_ensure_window_group (GtkWindow *window)
{
	GtkWindowGroup *group;

	group = window->group;
	if (group == NULL)
	{
		group = gtk_window_group_new ();
		gtk_window_group_add_window (group, window);
		g_object_unref (group);
	}

	return group;
}

gboolean
ephy_gui_confirm_overwrite_file (GtkWidget *parent,
				 const char *filename)
{
	GtkWidget *dialog;
	char *display_name;
	gboolean retval;

	if (filename == NULL) return FALSE;

	if (!g_file_test (filename, G_FILE_TEST_EXISTS))
	{
		char *path = g_path_get_dirname (filename);
		gboolean writable = access (path, W_OK) == 0;

		/* check if path is writable to */
		if (!writable)
		{
			dialog = gtk_message_dialog_new (
					parent ? GTK_WINDOW(parent) : NULL,
					GTK_DIALOG_DESTROY_WITH_PARENT,
					GTK_MESSAGE_ERROR,
					GTK_BUTTONS_CLOSE,
					_("Directory %s is not writable"), path);

			gtk_message_dialog_format_secondary_text (
					GTK_MESSAGE_DIALOG (dialog),
					_("You do not have permission to "
						"create files in this directory."));

			gtk_window_set_title (GTK_WINDOW (dialog), _("Directory not writable"));
			gtk_window_set_icon_name (GTK_WINDOW (dialog), "web-browser");

			if (parent != NULL)
			{
				gtk_window_group_add_window (
						ephy_gui_ensure_window_group (GTK_WINDOW (parent)),
						GTK_WINDOW (dialog));
			}

			gtk_dialog_run (GTK_DIALOG (dialog));

			gtk_widget_destroy (dialog);		
		}

		g_free (path);

		return writable;
	}

	display_name = g_filename_display_basename (filename);

	/* check if file is writable */
	if (access (filename, W_OK) != 0)
	{
		dialog = gtk_message_dialog_new (
				parent ? GTK_WINDOW(parent) : NULL,
				GTK_DIALOG_DESTROY_WITH_PARENT,
				GTK_MESSAGE_ERROR,
				GTK_BUTTONS_CLOSE,
				_("File %s is not writable"), display_name);

		gtk_message_dialog_format_secondary_text (
				GTK_MESSAGE_DIALOG (dialog),
				_("You do not have permission to overwrite this file."));

		gtk_window_set_title (GTK_WINDOW (dialog), _("File not writable"));
		gtk_window_set_icon_name (GTK_WINDOW (dialog), "web-browser");

		if (parent != NULL)
		{
			gtk_window_group_add_window (
					ephy_gui_ensure_window_group (GTK_WINDOW (parent)),
					GTK_WINDOW (dialog));
		}

		gtk_dialog_run (GTK_DIALOG (dialog));

		gtk_widget_destroy (dialog);		

		return FALSE;
	}

	dialog = gtk_message_dialog_new
		(parent ? GTK_WINDOW (parent) : NULL,
		 GTK_DIALOG_DESTROY_WITH_PARENT,
		 GTK_MESSAGE_QUESTION,
		 GTK_BUTTONS_CANCEL,
		 _("Overwrite \"%s\"?"), display_name);

	gtk_message_dialog_format_secondary_text
		(GTK_MESSAGE_DIALOG (dialog),
		 _("A file with this name already exists. If you choose to "
		   "overwrite this file, the contents will be lost."));

	gtk_dialog_add_button (GTK_DIALOG (dialog),
			       _("_Overwrite"), GTK_RESPONSE_ACCEPT);

	gtk_window_set_title (GTK_WINDOW (dialog), _("Overwrite File?"));
	gtk_window_set_icon_name (GTK_WINDOW (dialog), "web-browser");

	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);

	if (parent != NULL)
	{
		gtk_window_group_add_window (ephy_gui_ensure_window_group (GTK_WINDOW (parent)),
					     GTK_WINDOW (dialog));
	}

	retval = (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT);

	gtk_widget_destroy (dialog);

	g_free (display_name);

	return retval;
}

void
ephy_gui_help (GtkWindow *parent,
	       const char *file_name,
               const char *link_id)
{
	GError *err = NULL;

	gnome_help_display (file_name, link_id, &err);

	if (err != NULL)
	{
		GtkWidget *dialog;
		dialog = gtk_message_dialog_new (parent,
						 GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_ERROR,
						 GTK_BUTTONS_OK,
						 _("Could not display help: %s"), err->message);
		g_signal_connect (G_OBJECT (dialog), "response",
				  G_CALLBACK (gtk_widget_destroy),
				  NULL);
		gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
		gtk_widget_show (dialog);
		g_error_free (err);
	}
}

gboolean
ephy_gui_is_middle_click (void)
{
	gboolean is_middle_click = FALSE;
	GdkEvent *event;

	event = gtk_get_current_event ();
	if (event != NULL)
	{
		if (event->type == GDK_BUTTON_RELEASE)
		{
			guint modifiers, button, state;

			modifiers = gtk_accelerator_get_default_mod_mask ();
			button = event->button.button;
			state = event->button.state;

			/* middle-click or control-click */
			if ((button == 1 && ((state & modifiers) == GDK_CONTROL_MASK)) ||
			    (button == 2))
			{
				is_middle_click = TRUE;
			}
		}

		gdk_event_free (event);
	}

	return is_middle_click;
}

void
ephy_gui_window_update_user_time (GtkWidget *window,
				  guint32 user_time)
{
	LOG ("updating user time on window %p to %d", window, user_time);

	if (user_time != 0)
	{
		gtk_widget_realize (window);
		gdk_x11_window_set_user_time (window->window,
					      user_time);
	}

}

/* gtk+ bug 166379 */
/* adapted from gtk+/gtk/gtkwindow.c */
void
ephy_gui_window_present (GtkWindow *window,
			 guint32 user_time)
{
	GtkWidget *widget;
	
	g_return_if_fail (GTK_IS_WINDOW (window));
	
	widget = GTK_WIDGET (window);
	
	if (GTK_WIDGET_VISIBLE (window))
	{
		g_assert (widget->window != NULL);

		gdk_window_show (widget->window);

		/* note that gdk_window_focus() will also move the window to
		* the current desktop, for WM spec compliant window managers.
		*/
		gdk_window_focus (widget->window, user_time);
	}
	else
	{
		gtk_widget_show (widget);
	}
}
