/*
 *  Copyright (C) 2002 Christophe Fergeau
 *  Copyright (C) 2003, 2004 Marco Pesenti Gritti
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

#include "ephy-notebook.h"
#include "ephy-stock-icons.h"
#include "eel-gconf-extensions.h"
#include "ephy-prefs.h"
#include "ephy-marshal.h"
#include "ephy-file-helpers.h"
#include "ephy-dnd.h"
#include "ephy-embed.h"
#include "ephy-window.h"
#include "ephy-shell.h"
#include "ephy-favicon-cache.h"
#include "ephy-spinner.h"
#include "ephy-link.h"
#include "ephy-debug.h"

#include <glib-object.h>
#include <gtk/gtkeventbox.h>
#include <gtk/gtknotebook.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkwidget.h>
#include <gtk/gtktooltips.h>
#include <gtk/gtkmain.h>
#include <gtk/gtkstock.h>
#include <gtk/gtkimage.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtkiconfactory.h>
#include <glib/gi18n.h>
#include <libgnomevfs/gnome-vfs-uri.h>

#define TAB_WIDTH_N_CHARS 15

#define AFTER_ALL_TABS -1
#define NOT_IN_APP_WINDOWS -2

#define INSANE_NUMBER_OF_URLS 20

#define EPHY_NOTEBOOK_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE ((object), EPHY_TYPE_NOTEBOOK, EphyNotebookPrivate))

struct _EphyNotebookPrivate
{
	GList *focused_pages;
	GtkTooltips *title_tips;
	guint tabs_vis_notifier_id;
	gulong motion_notify_handler_id;
	gint x_start, y_start;
	gboolean drag_in_progress;
	gboolean show_tabs;
};

static void ephy_notebook_init           (EphyNotebook *notebook);
static void ephy_notebook_class_init     (EphyNotebookClass *klass);
static void ephy_notebook_finalize       (GObject *object);
static void move_tab_to_another_notebook (EphyNotebook *src,
			                  EphyNotebook *dest,
					  int dest_position);

/* Local variables */
static GdkCursor *cursor = NULL;

static GtkTargetEntry url_drag_types [] = 
{
        { EPHY_DND_URI_LIST_TYPE,   0, 0 },
        { EPHY_DND_URL_TYPE,        0, 1 }
};
static guint n_url_drag_types = G_N_ELEMENTS (url_drag_types);

enum
{
	TAB_ADDED,
	TAB_REMOVED,
	TABS_REORDERED,
	TAB_DETACHED,
	TAB_DELETE,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static GObjectClass *parent_class = NULL;

GType
ephy_notebook_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0))
	{
		static const GTypeInfo our_info =
			{
				sizeof (EphyNotebookClass),
				NULL, /* base_init */
				NULL, /* base_finalize */
				(GClassInitFunc) ephy_notebook_class_init,
				NULL,
				NULL, /* class_data */
				sizeof (EphyNotebook),
				0, /* n_preallocs */
				(GInstanceInitFunc) ephy_notebook_init
			};

		static const GInterfaceInfo link_info =
		{
			NULL,
			NULL,
			NULL
		};

	type = g_type_register_static (GTK_TYPE_NOTEBOOK,
				       "EphyNotebook",
				       &our_info, 0);
	g_type_add_interface_static (type,
				     EPHY_TYPE_LINK,
				     &link_info);
	}

	return type;
}

static void
ephy_notebook_class_init (EphyNotebookClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = ephy_notebook_finalize;

	signals[TAB_ADDED] =
		g_signal_new ("tab_added",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (EphyNotebookClass, tab_added),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1,
			      EPHY_TYPE_TAB);
	signals[TAB_REMOVED] =
		g_signal_new ("tab_removed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (EphyNotebookClass, tab_removed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1,
			      EPHY_TYPE_TAB);
	signals[TAB_DETACHED] =
		g_signal_new ("tab_detached",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (EphyNotebookClass, tab_detached),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1,
			      EPHY_TYPE_TAB);
	signals[TABS_REORDERED] =
		g_signal_new ("tabs_reordered",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (EphyNotebookClass, tabs_reordered),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	signals[TAB_DELETE] =
		g_signal_new ("tab_delete",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EphyNotebookClass, tab_delete),
			      g_signal_accumulator_true_handled, NULL,
			      ephy_marshal_BOOLEAN__OBJECT,
			      G_TYPE_BOOLEAN,
			      1,
			      EPHY_TYPE_TAB);

	g_type_class_add_private (object_class, sizeof(EphyNotebookPrivate));
}

static EphyNotebook *
find_notebook_at_pointer (gint abs_x, gint abs_y)
{
	GdkWindow *win_at_pointer, *toplevel_win;
	gpointer toplevel = NULL;
	gint x, y;

	/* FIXME multi-head */
	win_at_pointer = gdk_window_at_pointer (&x, &y);
	if (win_at_pointer == NULL)
	{
		/* We are outside all windows containing a notebook */
		return NULL;
	}

	toplevel_win = gdk_window_get_toplevel (win_at_pointer);

	/* get the GtkWidget which owns the toplevel GdkWindow */
	gdk_window_get_user_data (toplevel_win, &toplevel);

	/* toplevel should be an EphyWindow */
	if (toplevel != NULL && EPHY_IS_WINDOW (toplevel))
	{
		return EPHY_NOTEBOOK (ephy_window_get_notebook
					(EPHY_WINDOW (toplevel)));
	}

	return NULL;
}

static gboolean
is_in_notebook_window (EphyNotebook *notebook,
		       gint abs_x, gint abs_y)
{
	EphyNotebook *nb_at_pointer;

	nb_at_pointer = find_notebook_at_pointer (abs_x, abs_y);

	return nb_at_pointer == notebook;
}

static gint
find_tab_num_at_pos (EphyNotebook *notebook, gint abs_x, gint abs_y)
{
	GtkPositionType tab_pos;
	int page_num = 0;
	GtkNotebook *nb = GTK_NOTEBOOK (notebook);
	GtkWidget *page;

	tab_pos = gtk_notebook_get_tab_pos (GTK_NOTEBOOK (notebook));

	if (GTK_NOTEBOOK (notebook)->first_tab == NULL)
	{
		return AFTER_ALL_TABS;
	}

	/* For some reason unfullscreen + quick click can
	   cause a wrong click event to be reported to the tab */
	if (!is_in_notebook_window(notebook, abs_x, abs_y))
	{
		return NOT_IN_APP_WINDOWS;
	}

	while ((page = gtk_notebook_get_nth_page (nb, page_num)))
	{
		GtkWidget *tab;
		gint max_x, max_y;
		gint x_root, y_root;

		tab = gtk_notebook_get_tab_label (nb, page);
		g_return_val_if_fail (tab != NULL, -1);

		if (!GTK_WIDGET_MAPPED (GTK_WIDGET (tab)))
		{
			page_num++;
			continue;
		}

		gdk_window_get_origin (GDK_WINDOW (tab->window),
				       &x_root, &y_root);

		max_x = x_root + tab->allocation.x + tab->allocation.width;
		max_y = y_root + tab->allocation.y + tab->allocation.height;

		if (((tab_pos == GTK_POS_TOP)
		     || (tab_pos == GTK_POS_BOTTOM))
		    &&(abs_x<=max_x))
		{
			return page_num;
		}
		else if (((tab_pos == GTK_POS_LEFT)
			  || (tab_pos == GTK_POS_RIGHT))
			 && (abs_y<=max_y))
		{
			return page_num;
		}

		page_num++;
	}
	return AFTER_ALL_TABS;
}

static gint find_notebook_and_tab_at_pos (gint abs_x, gint abs_y,
					  EphyNotebook **notebook,
					  gint *page_num)
{
	*notebook = find_notebook_at_pointer (abs_x, abs_y);
	if (*notebook == NULL)
	{
		return NOT_IN_APP_WINDOWS;
	}
	*page_num = find_tab_num_at_pos (*notebook, abs_x, abs_y);

	if (*page_num < 0)
	{
		return *page_num;
	}
	else
	{
		return 0;
	}
}

void
ephy_notebook_move_tab (EphyNotebook *src,
			EphyNotebook *dest,
			EphyTab *tab,
			int dest_position)
{
	if (dest == NULL || src == dest)
	{
		gtk_notebook_reorder_child
			(GTK_NOTEBOOK (src), GTK_WIDGET (tab), dest_position);
		
		if (src->priv->drag_in_progress == FALSE)
		{
			g_signal_emit (G_OBJECT (src), signals[TABS_REORDERED], 0);
		}
	}
	else
	{
		/* make sure the tab isn't destroyed while we move it */
		g_object_ref (tab);
		ephy_notebook_remove_tab (src, tab);
		ephy_notebook_add_tab (dest, tab, dest_position, TRUE);
		g_object_unref (tab);
	}
}

static void
drag_start (EphyNotebook *notebook)
{
	notebook->priv->drag_in_progress = TRUE;

	/* get a new cursor, if necessary */
	if (!cursor) cursor = gdk_cursor_new (GDK_FLEUR);

	/* grab the pointer */
	gtk_grab_add (GTK_WIDGET (notebook));
	if (!gdk_pointer_is_grabbed ()) {
		gdk_pointer_grab (GDK_WINDOW(GTK_WIDGET (notebook)->window),
				  FALSE,
				  GDK_BUTTON1_MOTION_MASK | GDK_BUTTON_RELEASE_MASK,
				  NULL, cursor, GDK_CURRENT_TIME);
	}
}

static void
drag_stop (EphyNotebook *notebook)
{
	if (notebook->priv->drag_in_progress)
	{
		g_signal_emit (G_OBJECT (notebook), signals[TABS_REORDERED], 0);
	}

	notebook->priv->drag_in_progress = FALSE;
	if (notebook->priv->motion_notify_handler_id != 0)
	{
		g_signal_handler_disconnect (G_OBJECT (notebook),
					     notebook->priv->motion_notify_handler_id);
		notebook->priv->motion_notify_handler_id = 0;
	}
}

/* this function is only called during dnd, we don't need to emit TABS_REORDERED
 * here, instead we do it on drag_stop
 */
static void
move_tab (EphyNotebook *notebook,
	  int dest_position)
{
	gint cur_page_num;

	cur_page_num = gtk_notebook_get_current_page (GTK_NOTEBOOK (notebook));

	if (dest_position != cur_page_num)
	{
		GtkWidget *cur_tab;
		
		cur_tab = gtk_notebook_get_nth_page (GTK_NOTEBOOK (notebook),
						     cur_page_num);
		ephy_notebook_move_tab (EPHY_NOTEBOOK (notebook), NULL,
					EPHY_TAB (cur_tab),
					dest_position);
	}
}

static gboolean
motion_notify_cb (EphyNotebook *notebook,
		  GdkEventMotion *event,
		  gpointer data)
{
	EphyNotebook *dest;
	gint page_num;
	gint result;

	if (notebook->priv->drag_in_progress == FALSE)
	{
		if (gtk_drag_check_threshold (GTK_WIDGET (notebook),
					      notebook->priv->x_start,
					      notebook->priv->y_start,
					      event->x_root, event->y_root))
		{
			drag_start (notebook);
		}
		else
		{
			return FALSE;
		}
	}

	result = find_notebook_and_tab_at_pos ((gint)event->x_root,
					       (gint)event->y_root,
					       &dest, &page_num);

	if (result != NOT_IN_APP_WINDOWS)
	{
		if (dest != notebook)
		{
			move_tab_to_another_notebook (notebook, dest,
						      page_num);
		}
		else
		{
			g_assert (page_num >= -1);
			move_tab (notebook, page_num);
		}
	}

	return FALSE;
}

static void
move_tab_to_another_notebook (EphyNotebook *src,
			      EphyNotebook *dest,
			      int dest_position)
{
	EphyTab *tab;
	int cur_page;

	/* This is getting tricky, the tab was dragged in a notebook
	 * in another window of the same app, we move the tab
	 * to that new notebook, and let this notebook handle the
	 * drag
	*/
	g_assert (EPHY_IS_NOTEBOOK (dest));
	g_assert (dest != src);

	cur_page = gtk_notebook_get_current_page (GTK_NOTEBOOK (src));
	tab = EPHY_TAB (gtk_notebook_get_nth_page (GTK_NOTEBOOK (src), cur_page));

	/* stop drag in origin window */
	drag_stop (src);
	gtk_grab_remove (GTK_WIDGET (src));

	ephy_notebook_move_tab (src, dest, tab, dest_position);

	/* start drag handling in dest notebook */
	drag_start (dest);

	dest->priv->motion_notify_handler_id =
		g_signal_connect (G_OBJECT (dest),
				  "motion-notify-event",
				  G_CALLBACK (motion_notify_cb),
				  NULL);
}

static gboolean
button_release_cb (EphyNotebook *notebook,
		   GdkEventButton *event,
		   gpointer data)
{
	if (notebook->priv->drag_in_progress)
	{
		gint cur_page_num;
		GtkWidget *cur_page;

		cur_page_num =
			gtk_notebook_get_current_page (GTK_NOTEBOOK (notebook));
		cur_page = gtk_notebook_get_nth_page (GTK_NOTEBOOK (notebook),
						      cur_page_num);

		if (!is_in_notebook_window (notebook, event->x_root, event->y_root)
		    && gtk_notebook_get_n_pages (GTK_NOTEBOOK (notebook)) > 1)
		{
			/* Tab was detached */
			g_signal_emit (G_OBJECT (notebook),
				       signals[TAB_DETACHED], 0, cur_page);
		}

		/* ungrab the pointer if it's grabbed */
		if (gdk_pointer_is_grabbed ())
		{
			gdk_pointer_ungrab (GDK_CURRENT_TIME);
			gtk_grab_remove (GTK_WIDGET (notebook));
		}
	}
	/* This must be called even if a drag isn't happening */
	drag_stop (notebook);
	return FALSE;
}

static gboolean
button_press_cb (EphyNotebook *notebook,
		 GdkEventButton *event,
		 gpointer data)
{
	gint tab_clicked = find_tab_num_at_pos (notebook,
						event->x_root,
						event->y_root);

	if (notebook->priv->drag_in_progress)
	{
		return TRUE;
	}

	if ((event->button == 1) && (event->type == GDK_BUTTON_PRESS)
	    && (tab_clicked >= 0))
	{
		notebook->priv->x_start = event->x_root;
		notebook->priv->y_start = event->y_root;
		notebook->priv->motion_notify_handler_id =
			g_signal_connect (G_OBJECT (notebook),
					  "motion-notify-event",
					  G_CALLBACK (motion_notify_cb), NULL);
	}
	else if (GDK_BUTTON_PRESS == event->type && 3 == event->button)
	{
		if (tab_clicked == -1)
		{
			/* consume event, so that we don't pop up the context menu when
			 * the mouse if not over a tab label
			 */
			return TRUE;
		}
		else
		{
			/* switch to the page the mouse is over, but don't consume the event */
			gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), tab_clicked);
		}
	}

	return FALSE;
}

GtkWidget *
ephy_notebook_new (void)
{
	return GTK_WIDGET (g_object_new (EPHY_TYPE_NOTEBOOK, NULL));
}

static void
ephy_notebook_switch_page_cb (GtkNotebook *notebook,
                              GtkNotebookPage *page,
                              guint page_num,
                              gpointer data)
{
	EphyNotebook *nb = EPHY_NOTEBOOK (notebook);
	GtkWidget *child;

	child = gtk_notebook_get_nth_page (notebook, page_num);

	/* Remove the old page, we dont want to grow unnecessarily
	 * the list */
	if (nb->priv->focused_pages)
	{
		nb->priv->focused_pages =
			g_list_remove (nb->priv->focused_pages, child);
	}

	nb->priv->focused_pages = g_list_append (nb->priv->focused_pages,
						 child);
}

static void
notebook_drag_data_received_cb (GtkWidget* widget, GdkDragContext *context,
				gint x, gint y, GtkSelectionData *selection_data,
				guint info, guint time, EphyTab *tab)
{
	EphyWindow *window;
	GtkWidget *notebook;

	g_signal_stop_emission_by_name (widget, "drag_data_received");

	if (eel_gconf_get_boolean (CONF_LOCKDOWN_DISABLE_ARBITRARY_URL)) return;

	if (selection_data->length <= 0 || selection_data->data == NULL) return;

	window = EPHY_WINDOW (gtk_widget_get_toplevel (widget));
	notebook = ephy_window_get_notebook (window);

	if (selection_data->target == gdk_atom_intern (EPHY_DND_URL_TYPE, FALSE))
	{
		char **split;

		/* URL_TYPE has format: url \n title */
		split = g_strsplit (selection_data->data, "\n", 2);
		if (split != NULL && split[0] != NULL && split[0][0] != '\0')
		{
			ephy_link_open (EPHY_LINK (notebook), split[0], tab,
					tab ? 0 : EPHY_LINK_NEW_TAB);
		}
		g_strfreev (split);
	}
	else if (selection_data->target == gdk_atom_intern (EPHY_DND_URI_LIST_TYPE, FALSE))
	{
		char **uris;
		int i;

		uris = gtk_selection_data_get_uris (selection_data);
		if (uris == NULL) return;

		for (i = 0; uris[i] != NULL && i < INSANE_NUMBER_OF_URLS; i++)
		{
			tab = ephy_link_open (EPHY_LINK (notebook), uris[i],
					      tab, i == 0 ? 0 : EPHY_LINK_NEW_TAB);
		}

		g_strfreev (uris);
	}
	else
	{
		g_return_if_reached ();
	}
}

/*
 * update_tabs_visibility: Hide tabs if there is only one tab
 * and the pref is not set.
 */
static void
update_tabs_visibility (EphyNotebook *nb, gboolean before_inserting)
{
	gboolean show_tabs;
	guint num;

	num = gtk_notebook_get_n_pages (GTK_NOTEBOOK (nb));

	if (before_inserting) num++;

	show_tabs = (eel_gconf_get_boolean (CONF_ALWAYS_SHOW_TABS_BAR) || num > 1) &&
		    nb->priv->show_tabs == TRUE;

	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (nb), show_tabs);
}

static void
tabs_visibility_notifier (GConfClient *client,
			  guint cnxn_id,
			  GConfEntry *entry,
			  EphyNotebook *nb)
{
	update_tabs_visibility (nb, FALSE);
}

static void
ephy_notebook_init (EphyNotebook *notebook)
{
	notebook->priv = EPHY_NOTEBOOK_GET_PRIVATE (notebook);

	gtk_notebook_set_scrollable (GTK_NOTEBOOK (notebook), TRUE);
	gtk_notebook_set_show_border (GTK_NOTEBOOK (notebook), FALSE);
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (notebook), FALSE);

	notebook->priv->title_tips = gtk_tooltips_new ();
	g_object_ref (G_OBJECT (notebook->priv->title_tips));
	gtk_object_sink (GTK_OBJECT (notebook->priv->title_tips));

	notebook->priv->show_tabs = TRUE;

	g_signal_connect (notebook, "button-press-event",
			  (GCallback)button_press_cb, NULL);
	g_signal_connect (notebook, "button-release-event",
			  (GCallback)button_release_cb, NULL);
	gtk_widget_add_events (GTK_WIDGET (notebook), GDK_BUTTON1_MOTION_MASK);

	g_signal_connect_after (G_OBJECT (notebook), "switch_page",
                                G_CALLBACK (ephy_notebook_switch_page_cb),
                                NULL);

	/* Set up drag-and-drop target */
	g_signal_connect (G_OBJECT(notebook), "drag_data_received",
			  G_CALLBACK(notebook_drag_data_received_cb),
			  NULL);
        gtk_drag_dest_set (GTK_WIDGET(notebook), GTK_DEST_DEFAULT_MOTION |
			   GTK_DEST_DEFAULT_DROP,
                           url_drag_types,n_url_drag_types,
                           GDK_ACTION_MOVE | GDK_ACTION_COPY);

	notebook->priv->tabs_vis_notifier_id = eel_gconf_notification_add
		(CONF_ALWAYS_SHOW_TABS_BAR,
		 (GConfClientNotifyFunc)tabs_visibility_notifier, notebook);
}

static void
ephy_notebook_finalize (GObject *object)
{
	EphyNotebook *notebook = EPHY_NOTEBOOK (object);

	eel_gconf_notification_remove (notebook->priv->tabs_vis_notifier_id);

	if (notebook->priv->focused_pages)
	{
		g_list_free (notebook->priv->focused_pages);
	}
	g_object_unref (notebook->priv->title_tips);

	LOG ("EphyNotebook finalised %p", object)

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
sync_load_status (EphyTab *tab, GParamSpec *pspec, GtkWidget *proxy)
{
	GtkWidget *spinner, *icon;

	spinner = GTK_WIDGET (g_object_get_data (G_OBJECT (proxy), "spinner"));
	icon = GTK_WIDGET (g_object_get_data (G_OBJECT (proxy), "icon"));
	g_return_if_fail (spinner != NULL && icon != NULL);

	if (ephy_tab_get_load_status (tab))
	{
		gtk_widget_hide (icon);
		gtk_widget_show (spinner);
		ephy_spinner_start (EPHY_SPINNER (spinner));
	}
	else
	{
		ephy_spinner_stop (EPHY_SPINNER (spinner));
		gtk_widget_hide (spinner);
		gtk_widget_show (icon);
	}
}

static void
sync_icon (EphyTab *tab, GParamSpec *pspec, GtkWidget *proxy)
{
	EphyFaviconCache *cache;
	GdkPixbuf *pixbuf = NULL;
	GtkImage *icon = NULL;
	const char *address;

	cache = EPHY_FAVICON_CACHE
		(ephy_embed_shell_get_favicon_cache (EPHY_EMBED_SHELL (ephy_shell)));
	address = ephy_tab_get_icon_address (tab);

	if (address)
	{
		pixbuf = ephy_favicon_cache_get (cache, address);
	}

	icon = GTK_IMAGE (g_object_get_data (G_OBJECT (proxy), "icon"));
	if (icon)
	{
		gtk_image_set_from_pixbuf (icon, pixbuf);
	}

	if (pixbuf)
	{
		g_object_unref (pixbuf);
	}
}

static void
sync_label (EphyTab *tab, GParamSpec *pspec, GtkWidget *proxy)
{
	GtkWidget *label, *ebox;
	GtkTooltips *tips;	
	const char *title;

	ebox = GTK_WIDGET (g_object_get_data (G_OBJECT (proxy), "label-ebox"));
	tips = GTK_TOOLTIPS (g_object_get_data (G_OBJECT (proxy), "tooltips"));
	label = GTK_WIDGET (g_object_get_data (G_OBJECT (proxy), "label"));

	g_return_if_fail (ebox != NULL && tips != NULL && label != NULL);

	title = ephy_tab_get_title (tab);

	if (title)
	{
		gtk_label_set_text (GTK_LABEL (label), title);
		gtk_tooltips_set_tip (tips, ebox, title, NULL);
	}
}

static void
close_button_clicked_cb (GtkWidget *widget, GtkWidget *tab)
{
	EphyNotebook *notebook;
	gboolean inhibited = FALSE;

	notebook = EPHY_NOTEBOOK (gtk_widget_get_parent (tab));
	g_signal_emit (G_OBJECT (notebook), signals[TAB_DELETE], 0, tab, &inhibited);

	if (inhibited == FALSE)
	{
		ephy_notebook_remove_tab (notebook, EPHY_TAB (tab));
	}
}

static void
tab_label_style_set_cb (GtkWidget *label,
			GtkStyle *previous_style,
			GtkWidget *hbox)
{
	PangoFontMetrics *metrics;
	PangoContext *context;
	int char_width, h, w;

	context = gtk_widget_get_pango_context (label);
	metrics = pango_context_get_metrics (context,
			                     label->style->font_desc,
					     pango_context_get_language (context));

	char_width = pango_font_metrics_get_approximate_digit_width (metrics);
	pango_font_metrics_unref (metrics);

	gtk_icon_size_lookup_for_settings (gtk_widget_get_settings (label),
					   GTK_ICON_SIZE_MENU, &w, &h);

	gtk_widget_set_size_request
		(hbox, TAB_WIDTH_N_CHARS * PANGO_PIXELS(char_width) + 2 * w, -1);
}

static GtkWidget *
build_tab_label (EphyNotebook *nb, EphyTab *tab)
{
	GtkWidget *hbox, *label_hbox, *label_ebox;
	GtkWidget *label, *close_button, *image, *spinner, *icon;
	GtkIconSize close_icon_size;

	/* set hbox spacing and label padding (see below) so that there's an
	 * equal amount of space around the label */
	hbox = gtk_hbox_new (FALSE, 4);

	label_ebox = gtk_event_box_new ();
	gtk_event_box_set_visible_window (GTK_EVENT_BOX (label_ebox), FALSE);
	gtk_box_pack_start (GTK_BOX (hbox), label_ebox, TRUE, TRUE, 0);

	label_hbox = gtk_hbox_new (FALSE, 4);
	gtk_container_add (GTK_CONTAINER (label_ebox), label_hbox);

	/* setup close button */
	close_button = gtk_button_new ();
	gtk_button_set_relief (GTK_BUTTON (close_button),
			       GTK_RELIEF_NONE);
	/* don't allow focus on the close button */
	gtk_button_set_focus_on_click (GTK_BUTTON (close_button), FALSE);

	close_icon_size = gtk_icon_size_from_name (EPHY_ICON_SIZE_TAB_BUTTON);
	image = gtk_image_new_from_stock (EPHY_STOCK_CLOSE_TAB, close_icon_size);
	gtk_container_add (GTK_CONTAINER (close_button), image);
	gtk_box_pack_start (GTK_BOX (hbox), close_button, FALSE, FALSE, 0);

	gtk_tooltips_set_tip (nb->priv->title_tips, close_button,
			      _("Close tab"), NULL);

	g_signal_connect (G_OBJECT (close_button), "clicked",
                          G_CALLBACK (close_button_clicked_cb),
                          tab);

	/* setup load feedback */
	spinner = ephy_spinner_new ();
	ephy_spinner_set_size (EPHY_SPINNER (spinner), GTK_ICON_SIZE_MENU);
	gtk_box_pack_start (GTK_BOX (label_hbox), spinner, FALSE, FALSE, 0);

	/* setup site icon, empty by default */
	icon = gtk_image_new ();
	gtk_box_pack_start (GTK_BOX (label_hbox), icon, FALSE, FALSE, 0);

	/* setup label */
        label = gtk_label_new ("");
	gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_misc_set_padding (GTK_MISC (label), 0, 0);
	gtk_box_pack_start (GTK_BOX (label_hbox), label, TRUE, TRUE, 0);

	/* Set minimal size */
	g_signal_connect (label, "style-set",
			  G_CALLBACK (tab_label_style_set_cb), hbox);

	gtk_widget_show (hbox);
	gtk_widget_show (label_ebox);
	gtk_widget_show (label_hbox);
	gtk_widget_show (label);
	gtk_widget_show (image);
	gtk_widget_show (close_button);
	
	g_object_set_data (G_OBJECT (hbox), "label", label);
	g_object_set_data (G_OBJECT (hbox), "label-ebox", label_ebox);
	g_object_set_data (G_OBJECT (hbox), "spinner", spinner);
	g_object_set_data (G_OBJECT (hbox), "icon", icon);
	g_object_set_data (G_OBJECT (hbox), "close-button", close_button);
	g_object_set_data (G_OBJECT (hbox), "tooltips", nb->priv->title_tips);

	return hbox;
}

void
ephy_notebook_set_show_tabs (EphyNotebook *nb, gboolean show_tabs)
{
	nb->priv->show_tabs = show_tabs;

	update_tabs_visibility (nb, FALSE);
}

void
ephy_notebook_add_tab (EphyNotebook *nb,
		       EphyTab *tab,
		       int position,
		       gboolean jump_to)
{
	GtkWidget *label;

	g_return_if_fail (EPHY_IS_TAB (tab));

	label = build_tab_label (nb, tab);

	update_tabs_visibility (nb, TRUE);

	gtk_notebook_insert_page (GTK_NOTEBOOK (nb), GTK_WIDGET (tab),
				  label, position);

	/* Set up drag-and-drop target */
	g_signal_connect (G_OBJECT(label), "drag_data_received",
			  G_CALLBACK(notebook_drag_data_received_cb), tab);
	gtk_drag_dest_set (label, GTK_DEST_DEFAULT_ALL,
			   url_drag_types,n_url_drag_types,
			   GDK_ACTION_MOVE | GDK_ACTION_COPY);

	sync_icon (tab, NULL, label);
	sync_label (tab, NULL, label);
	sync_load_status (tab, NULL, label);

	g_signal_connect_object (tab, "notify::icon",
			         G_CALLBACK (sync_icon), label, 0);
	g_signal_connect_object (tab, "notify::title",
			         G_CALLBACK (sync_label), label, 0);
	g_signal_connect_object (tab, "notify::load-status",
				 G_CALLBACK (sync_load_status), label, 0);

	g_signal_emit (G_OBJECT (nb), signals[TAB_ADDED], 0, tab);

	/* The signal handler may have reordered the tabs */
	position = gtk_notebook_page_num (GTK_NOTEBOOK (nb), GTK_WIDGET (tab));

	if (jump_to)
	{
		gtk_notebook_set_current_page (GTK_NOTEBOOK (nb), position);
		g_object_set_data (G_OBJECT (tab), "jump_to",
				   GINT_TO_POINTER (jump_to));

	}
}

static void
smart_tab_switching_on_closure (EphyNotebook *nb,
				EphyTab *tab)
{
	gboolean jump_to;

	jump_to = GPOINTER_TO_INT (g_object_get_data
				   (G_OBJECT (tab), "jump_to"));

	if (!jump_to || !nb->priv->focused_pages)
	{
		gtk_notebook_next_page (GTK_NOTEBOOK (nb));
	}
	else
	{
		GList *l;
		GtkWidget *child;
		int page_num;

		/* activate the last focused tab */
		l = g_list_last (nb->priv->focused_pages);
		child = GTK_WIDGET (l->data);
		page_num = gtk_notebook_page_num (GTK_NOTEBOOK (nb),
						  child);
		gtk_notebook_set_current_page
			(GTK_NOTEBOOK (nb), page_num);
	}
}

void
ephy_notebook_remove_tab (EphyNotebook *nb,
			  EphyTab *tab)
{
	int position, curr;
	GtkWidget *label, *ebox;

	g_return_if_fail (EPHY_IS_NOTEBOOK (nb));
	g_return_if_fail (EPHY_IS_TAB (tab));

	/* Remove the page from the focused pages list */
	nb->priv->focused_pages =  g_list_remove (nb->priv->focused_pages,
						  tab);

	position = gtk_notebook_page_num (GTK_NOTEBOOK (nb), GTK_WIDGET (tab));
	curr = gtk_notebook_get_current_page (GTK_NOTEBOOK (nb));

	if (position == curr)
	{
		smart_tab_switching_on_closure (nb, tab);
	}

	label = gtk_notebook_get_tab_label (GTK_NOTEBOOK (nb), GTK_WIDGET (tab));
	ebox = GTK_WIDGET (g_object_get_data (G_OBJECT (label), "label-ebox"));
	gtk_tooltips_set_tip (GTK_TOOLTIPS (nb->priv->title_tips), ebox, NULL, NULL);

	g_signal_handlers_disconnect_by_func (tab,
					      G_CALLBACK (sync_icon), label);
	g_signal_handlers_disconnect_by_func (tab,
					      G_CALLBACK (sync_label), label);
	g_signal_handlers_disconnect_by_func (tab,
					      G_CALLBACK (sync_load_status), label);

	/**
	 * we ref the tab so that it's still alive while the tabs_removed
	 * signal is processed.
	 */
	g_object_ref (tab);

	gtk_notebook_remove_page (GTK_NOTEBOOK (nb), position);

	update_tabs_visibility (nb, FALSE);

	g_signal_emit (G_OBJECT (nb), signals[TAB_REMOVED], 0, tab);

	g_object_unref (tab);

	/* if that was the last tab, destroy the window */
	if (gtk_notebook_get_n_pages (GTK_NOTEBOOK (nb)) == 0)
	{
		gtk_widget_destroy (gtk_widget_get_toplevel (GTK_WIDGET (nb)));
	}
}
