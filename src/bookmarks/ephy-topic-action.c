/*
 *  Copyright (C) 2003, 2004 Marco Pesenti Gritti
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

#include <gtk/gtktoolitem.h>
#include <glib/gi18n.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <string.h>

#include "ephy-topic-action.h"
#include "ephy-node-common.h"
#include "ephy-bookmarks.h"
#include "ephy-bookmarksbar.h"
#include "ephy-favicon-cache.h"
#include "ephy-shell.h"
#include "ephy-debug.h"
#include "ephy-dnd.h"
#include "ephy-gui.h"
#include "ephy-string.h"
#include "ephy-marshal.h"

static void ephy_topic_action_init       (EphyTopicAction *action);
static void ephy_topic_action_class_init (EphyTopicActionClass *class);

#define EPHY_TOPIC_ACTION_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE ((object), EPHY_TYPE_TOPIC_ACTION, EphyTopicActionPrivate))

static GtkTargetEntry drag_targets[] =
{
	{ EPHY_DND_TOPIC_TYPE, 0, 0 }
};
static int n_drag_targets = G_N_ELEMENTS (drag_targets);

struct EphyTopicActionPrivate
{
	guint topic_id;
	EphyNode *topic_node;

	guint motion_handler;
	guint release_handler;
	gint drag_x;
	gint drag_y;
};

enum
{
	PROP_0,
	PROP_TOPIC_ID
};

enum
{
	OPEN,
	OPEN_IN_TAB,
	OPEN_IN_TABS,
	LAST_SIGNAL
};

static GObjectClass *parent_class = NULL;

static guint ephy_topic_action_signals[LAST_SIGNAL] = { 0 };

GType
ephy_topic_action_get_type (void)
{
	static GType type = 0;

	if (!type)
	{
		static const GTypeInfo type_info =
		{
			sizeof (EphyTopicActionClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) ephy_topic_action_class_init,
			(GClassFinalizeFunc) NULL,
			NULL,
			sizeof (EphyTopicAction),
			0, /* n_preallocs */
			(GInstanceInitFunc) ephy_topic_action_init,
		};

		type = g_type_register_static (GTK_TYPE_ACTION,
					       "EphyTopicAction",
					       &type_info, 0);
	}
	return type;
}

static GtkWidget *
create_tool_item (GtkAction *action)
{
	GtkWidget *item;
	GtkWidget *button;
	GtkWidget *arrow;
	GtkWidget *hbox;
	GtkWidget *label;

	item = (* GTK_ACTION_CLASS (parent_class)->create_tool_item) (action);

	hbox = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hbox);
	gtk_container_add (GTK_CONTAINER (item), hbox);

	button = gtk_toggle_button_new ();
	gtk_widget_add_events (GTK_WIDGET (button), GDK_BUTTON1_MOTION_MASK);
	gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
	gtk_widget_show (button);
	gtk_container_add (GTK_CONTAINER (hbox), button);
	g_object_set_data (G_OBJECT (item), "button", button);

	arrow = gtk_arrow_new (GTK_ARROW_DOWN, GTK_SHADOW_NONE);
	gtk_widget_show (arrow);

	hbox = gtk_hbox_new (FALSE, 3);
	gtk_widget_show (hbox);
	gtk_container_add (GTK_CONTAINER (button), hbox);

	label = gtk_label_new (NULL);
	gtk_widget_show (label);
	gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), arrow, TRUE, TRUE, 0);
	g_object_set_data (G_OBJECT (item), "label", label);

	return item;
}

static void
menu_deactivate_cb (GtkMenuShell *ms, GtkWidget *button)
{
	g_object_set_data (G_OBJECT (button), "popup", NULL);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), FALSE);
	gtk_button_released (GTK_BUTTON (button));
}

static void
menu_activate_cb (GtkWidget *item, GtkAction *action)
{
	EphyNode *node;
	const char *location;

	node = g_object_get_data (G_OBJECT (item), "node");
	location = ephy_node_get_property_string
		(node, EPHY_NODE_BMK_PROP_LOCATION);

	if (ephy_gui_is_middle_click ())
	{
		g_signal_emit (action, ephy_topic_action_signals[OPEN_IN_TAB],
			       0, location, FALSE);
	}
	else
	{
		g_signal_emit (action, ephy_topic_action_signals[OPEN],
			       0, location);
	}
}

static void
ephy_topic_action_sync_label (GtkAction *action, GParamSpec *pspec, GtkWidget *proxy)
{
	GtkWidget *label = NULL;
	GValue value = { 0, };
	const char *label_text;

	g_value_init (&value, G_TYPE_STRING);
	g_object_get_property (G_OBJECT (action), "label", &value);

	label_text = g_value_get_string (&value);

	if (GTK_IS_TOOL_ITEM (proxy))
	{
		label = GTK_WIDGET (g_object_get_data (G_OBJECT (proxy), "label"));
	}
	else if (GTK_IS_MENU_ITEM (proxy))
	{
		label = GTK_BIN (proxy)->child;
	}
	else
	{
		g_warning ("Unknown widget");
		return;
	}

	g_return_if_fail (label != NULL);

	if (label_text)
	{
		gtk_label_set_label (GTK_LABEL (label), label_text);
	}

	g_value_unset (&value);
}

static int
sort_bookmarks (gconstpointer a, gconstpointer b)
{
	EphyNode *node_a = (EphyNode *)a;
	EphyNode *node_b = (EphyNode *)b;
	const char *title1, *title2;
	int retval;

	title1 = ephy_node_get_property_string (node_a, EPHY_NODE_BMK_PROP_TITLE);
	title2 = ephy_node_get_property_string (node_b, EPHY_NODE_BMK_PROP_TITLE);
                                                                                                                      
	if (title1 == NULL)
	{
		retval = -1;
	}
	else if (title2 == NULL)
	{
		retval = 1;
	}
	else
	{
		char *str_a, *str_b;

		str_a = g_utf8_casefold (title1, -1);
		str_b = g_utf8_casefold (title2, -1);
		retval = g_utf8_collate (str_a, str_b);
		g_free (str_a);
		g_free (str_b);
	}

	return retval;
}

#define MAX_LENGTH 32

static gboolean
can_open_in_tabs (EphyNode *node)
{
	GPtrArray *children;
	int priority;

	priority = ephy_node_get_property_int (node, EPHY_NODE_KEYWORD_PROP_PRIORITY);
	children = ephy_node_get_children (node);

	return (priority != EPHY_NODE_ALL_PRIORITY) && (children->len > 1);
}

static void
append_bookmarks_menu (EphyTopicAction *action, GtkWidget *menu, EphyNode *node, gboolean show_empty)
{
        EphyFaviconCache *cache;
	GtkWidget *item;
	GPtrArray *children;

        cache = EPHY_FAVICON_CACHE
		(ephy_embed_shell_get_favicon_cache (EPHY_EMBED_SHELL (ephy_shell)));

	children = ephy_node_get_children (node);

	if (children->len < 1 && show_empty)
	{
		/* This is the adjective, not the verb */
		item = gtk_menu_item_new_with_label (_("Empty"));
		gtk_widget_set_sensitive (item, FALSE);
		gtk_widget_show (item);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

		return;
	}
	else
	{
		GList *node_list = NULL, *l;
		int i;

		for (i = 0; i < children->len; ++i)
		{
			node_list = g_list_prepend (node_list,
						   g_ptr_array_index (children, i));
		}

		node_list = g_list_sort (node_list, (GCompareFunc)sort_bookmarks);

		for (l = g_list_first (node_list); l != NULL; l = g_list_next (l))
		{
			EphyNode *kid;
			const char *icon_location;
			const char *title;
			char *title_short;

			kid = (EphyNode*)l->data;

			icon_location = ephy_node_get_property_string
				(kid, EPHY_NODE_BMK_PROP_ICON);
			title = ephy_node_get_property_string
				(kid, EPHY_NODE_BMK_PROP_TITLE);
			if (title == NULL) continue;
			title_short = ephy_string_shorten (title, MAX_LENGTH);
			LOG ("Create menu for bookmark %s", title_short)

			item = gtk_image_menu_item_new_with_label (title_short);
			if (icon_location)
			{
				GdkPixbuf *icon;
				GtkWidget *image;

				icon = ephy_favicon_cache_get (cache, icon_location);
				if (icon != NULL)
				{
					image = gtk_image_new_from_pixbuf (icon);
					gtk_widget_show (image);
					gtk_image_menu_item_set_image
						(GTK_IMAGE_MENU_ITEM (item), image);
					g_object_unref (icon);
				}
			}

			g_object_set_data (G_OBJECT (item), "node", kid);
			g_signal_connect (item, "activate",
					  G_CALLBACK (menu_activate_cb), action);
			gtk_widget_show (item);
			gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

			g_free (title_short);
		}

		g_list_free (node_list);
	}
}

#define TOPIC_NODE_DATA_KEY	"TopicNode"

static void
open_in_tabs_activate_cb (GtkWidget *item, EphyTopicAction *action)
{
	EphyNode *node;
	GList *uri_list = NULL;
	GPtrArray *children;
	int i;

	node = g_object_get_data (G_OBJECT (item), TOPIC_NODE_DATA_KEY);
	g_return_if_fail (node != NULL);

	children = ephy_node_get_children (node);
	for (i = 0; i < children->len; i++)
	{
		const char *address;
		EphyNode *child;

		child = g_ptr_array_index (children, i);
		address = ephy_node_get_property_string
			(child, EPHY_NODE_BMK_PROP_LOCATION);
		uri_list = g_list_append (uri_list, g_strdup (address));
	}

	g_signal_emit (action, ephy_topic_action_signals[OPEN_IN_TABS],
		       0, uri_list);
	g_list_foreach (uri_list, (GFunc)g_free, NULL);
	g_list_free (uri_list);
}

static int
get_item_position (GtkWidget *widget, gboolean *last)
{
	GtkWidget *item, *toolbar;
	int index;

	item = gtk_widget_get_ancestor (widget, GTK_TYPE_TOOL_ITEM);
	g_return_val_if_fail (item != NULL, -1);

	toolbar = gtk_widget_get_ancestor (widget, GTK_TYPE_TOOLBAR);
	g_return_val_if_fail (toolbar != NULL, -1);

	index = gtk_toolbar_get_item_index (GTK_TOOLBAR (toolbar),
				            GTK_TOOL_ITEM (item));
	if (last)
	{
		int n_items;

		n_items = gtk_toolbar_get_n_items (GTK_TOOLBAR (toolbar));
		*last = (index == n_items - 1);
	}

	return index;
}

static void
remove_from_model (GtkWidget *widget)
{
	EphyBookmarks *bookmarks;
	EggToolbarsModel *model;
	int pos;

	pos = get_item_position (widget, NULL);

	bookmarks = ephy_shell_get_bookmarks (ephy_shell);
	model = EGG_TOOLBARS_MODEL (ephy_bookmarks_get_toolbars_model (bookmarks));

	egg_toolbars_model_remove_item (model, 0, pos);
}

static void
move_in_model (GtkWidget *widget, int direction)
{
	EphyBookmarks *bookmarks;
	EggToolbarsModel *model;
	int pos, new_pos;

	bookmarks = ephy_shell_get_bookmarks (ephy_shell);
	model = EGG_TOOLBARS_MODEL (ephy_bookmarks_get_toolbars_model (bookmarks));

	pos = get_item_position (widget, NULL);
	new_pos = MAX (0, pos + direction);

	egg_toolbars_model_move_item (model, 0, pos, 0, new_pos);
}

static void
remove_activate_cb (GtkWidget *menu, GtkWidget *proxy)
{
	remove_from_model (proxy);
}

static void
move_left_activate_cb (GtkWidget *menu, GtkWidget *proxy)
{
	move_in_model (proxy, -1);
}

static void
move_right_activate_cb (GtkWidget *menu, GtkWidget *proxy)
{
	move_in_model (proxy, +1);
}

static GtkWidget *
add_open_in_tabs_menu (EphyTopicAction *action,
		       GtkWidget *menu,
		       EphyNode *node)
{
	GtkWidget *item;

	item = gtk_menu_item_new_with_mnemonic (_("_Open in Tabs"));
	gtk_widget_show (item);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	g_object_set_data (G_OBJECT (item), TOPIC_NODE_DATA_KEY, node);

	g_signal_connect (item, "activate",
			  G_CALLBACK (open_in_tabs_activate_cb), action);

	return item;
}

static GtkWidget *
build_bookmarks_menu (EphyTopicAction *action, EphyNode *node)
{
	GtkWidget *menu;

	menu = gtk_menu_new ();

	append_bookmarks_menu (action, menu, node, TRUE);

	if (can_open_in_tabs (node))
	{
		GtkWidget *item;

		item = gtk_separator_menu_item_new ();
		gtk_widget_show (item);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

		add_open_in_tabs_menu (action, menu, node);
	}

	return menu;
}

static int
sort_topics (gconstpointer a, gconstpointer b)
{
	EphyNode *node_a = (EphyNode *)a;
	EphyNode *node_b = (EphyNode *)b;
	const char *title1, *title2;
	int retval;

	title1 = ephy_node_get_property_string (node_a, EPHY_NODE_KEYWORD_PROP_NAME);
	title2 = ephy_node_get_property_string (node_b, EPHY_NODE_KEYWORD_PROP_NAME);
                                                                                                                      
	if (title1 == NULL)
	{
		retval = -1;
	}
	else if (title2 == NULL)
	{
		retval = 1;
	}
	else
	{
		char *str_a, *str_b;

		str_a = g_utf8_casefold (title1, -1);
		str_b = g_utf8_casefold (title2, -1);
		retval = g_utf8_collate (str_a, str_b);
		g_free (str_a);
		g_free (str_b);
	}

	return retval;
}

static GtkWidget *
build_topics_menu (EphyTopicAction *action)
{
	GtkWidget *menu, *item;
	GPtrArray *children;
	int i;
	EphyBookmarks *bookmarks;
	EphyNode *all, *uncategorized, *node;
	EphyNodePriority priority;
	GList *node_list = NULL, *l = NULL;

	bookmarks = ephy_shell_get_bookmarks (ephy_shell);
	all = ephy_bookmarks_get_bookmarks (bookmarks);
	node = ephy_bookmarks_get_keywords (bookmarks);

	menu = gtk_menu_new ();

	children = ephy_node_get_children (node);

	for (i = 0; i < children->len; ++i)
	{
		EphyNode *kid;

		kid = g_ptr_array_index (children, i);
		priority = ephy_node_get_property_int
			(kid, EPHY_NODE_KEYWORD_PROP_PRIORITY);
		if (priority == EPHY_NODE_NORMAL_PRIORITY)
		{
			node_list = g_list_prepend (node_list, kid);
		}
	}

	node_list = g_list_sort (node_list, (GCompareFunc)sort_topics);

	for (l = g_list_first (node_list); l != NULL; l = g_list_next (l))
	{
		EphyNode *kid;
		const char *title;
		GtkWidget *bmk_menu;

		kid = (EphyNode*)l->data;
		if (kid == all) continue;

		title = ephy_node_get_property_string
			(kid, EPHY_NODE_KEYWORD_PROP_NAME);
		LOG ("Create menu for topic %s", title);

		item = gtk_image_menu_item_new_with_label (title);

		gtk_widget_show (item);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

		bmk_menu = build_bookmarks_menu (action, kid);
		gtk_widget_show (bmk_menu);
		gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), bmk_menu);
	}
	g_list_free (node_list);

	uncategorized = ephy_bookmarks_get_not_categorized (bookmarks);
	append_bookmarks_menu (action, menu, uncategorized, FALSE);

	return menu;
}

static GtkWidget *
build_menu (EphyTopicAction *action)
{
	if (action->priv->topic_id == BOOKMARKS_NODE_ID)
	{
		return build_topics_menu (action);
	}
	else
	{
		return build_bookmarks_menu (action, action->priv->topic_node);
	}
}

static void
drag_data_get_cb (GtkWidget *widget, GdkDragContext *context,
		  GtkSelectionData *selection_data, guint info,
		  guint32 time, EphyTopicAction *action)
{
	EphyBookmarks *bookmarks;
	char *uri;

	bookmarks = ephy_shell_get_bookmarks (ephy_shell);
	g_return_if_fail (bookmarks != NULL);

	uri = ephy_bookmarks_get_topic_uri (bookmarks, action->priv->topic_node);
	g_return_if_fail (uri != NULL);

	gtk_selection_data_set (selection_data, selection_data->target, 8,
				(unsigned char *) uri, strlen (uri));

	g_free (uri);
}

static void
drag_data_delete_cb (GtkWidget *widget, GdkDragContext *context,
		     EphyTopicAction *action)
{
	remove_from_model (widget);
}

static void
stop_drag_check (EphyTopicAction *action, GtkWidget *widget)
{
	if (action->priv->motion_handler)
	{
		g_signal_handler_disconnect (widget, action->priv->motion_handler);
		action->priv->motion_handler = 0;

		g_signal_handler_disconnect (widget, action->priv->release_handler);
		action->priv->release_handler = 0;
	}
}

static gboolean
check_horizontal_threshold (GtkWidget *widget, gint start_x, gint start_y,
			    gint current_x, gint current_y)
{
	gint drag_threshold;

	g_return_val_if_fail (GTK_IS_WIDGET (widget), FALSE);

	g_object_get (gtk_widget_get_settings (widget),
		      "gtk-dnd-drag-threshold", &drag_threshold,
		      NULL);

	return (ABS (current_x - start_x) > drag_threshold &&
		ABS (current_y - start_y) < drag_threshold);
}

static gboolean
drag_motion_cb (GtkWidget *widget, GdkEventMotion *event, EphyTopicAction *action)
{
	GtkWidget *button, *event_widget;

	event_widget = gtk_get_event_widget ((GdkEvent*) event);
	button = GTK_WIDGET (g_object_get_data (G_OBJECT (widget), "button"));

	if (!gtk_widget_is_ancestor (event_widget, widget) &&
	    check_horizontal_threshold (widget, action->priv->drag_x,
				        action->priv->drag_y, event->x, event->y))
	{
		GtkTargetList *target_list;

		target_list = gtk_target_list_new (drag_targets, n_drag_targets);

		stop_drag_check (action, widget);
		gtk_menu_popdown (GTK_MENU (widget));
		gtk_drag_begin (button, target_list, GDK_ACTION_MOVE |
				GDK_ACTION_COPY, 1, (GdkEvent*)event);

		gtk_target_list_unref (target_list);
	}

	return TRUE;
}

static void
button_toggled_cb (GtkWidget *button,
		   EphyTopicAction *action)
{
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)))
	{
		GtkWidget *menu;
		GdkEvent *event;
		guint32 event_time = 0;
		guint event_button = 0;

		menu = build_menu (action);
		g_signal_connect (menu, "deactivate",
				  G_CALLBACK (menu_deactivate_cb), button);

		event = gtk_get_current_event ();
		if (event != NULL)
		{
			if (event->type == GDK_BUTTON_PRESS)
			{
				event_button = ((GdkEventButton *) event)->button;
				event_time = ((GdkEventButton *) event)->time;
			}

			gdk_event_free (event);
		}

		if (event_button == 0)
		{
			gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
					ephy_gui_menu_position_under_widget,
					button, 0 , gtk_get_current_event_time ());
			gtk_menu_shell_select_first (GTK_MENU_SHELL (menu), FALSE);
		}
		else
		{
			gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
					ephy_gui_menu_position_under_widget,
					button, event_button, event_time);
		}

		g_object_set_data (G_OBJECT (button), "popup", menu);
	}
}

static GtkWidget *
create_menu_item (GtkAction *action)
{
	GtkWidget *menu, *menu_item;
	GValue value = { 0, };
	const char *tmp;
	char *label_text;

	g_value_init (&value, G_TYPE_STRING);
	g_object_get_property (G_OBJECT (action), "label", &value);

	tmp = g_value_get_string (&value);
	label_text = ephy_string_double_underscores (tmp);

	LOG ("create_menu_item action %p", action)

	menu_item = gtk_menu_item_new_with_label (label_text);

	g_value_unset (&value);
	g_free (label_text);

	menu = build_menu (EPHY_TOPIC_ACTION (action));
	gtk_widget_show (menu);

	gtk_menu_item_set_submenu (GTK_MENU_ITEM (menu_item), menu);

	return menu_item;
}

static void
show_context_menu (EphyTopicAction *action,
		   GtkWidget *proxy,
		   GdkEventButton *event,
		   GtkMenuPositionFunc func)
{
	GtkWidget *menu, *item;
	gboolean last;

	menu = gtk_menu_new ();

	item = add_open_in_tabs_menu (action, menu, action->priv->topic_node);
	gtk_widget_set_sensitive (item, can_open_in_tabs (action->priv->topic_node));

	item = gtk_separator_menu_item_new ();
	gtk_widget_show (item);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	item = gtk_image_menu_item_new_from_stock (GTK_STOCK_REMOVE, NULL);
	gtk_widget_show (item);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	g_signal_connect (item, "activate",
			  G_CALLBACK (remove_activate_cb), proxy);

	item = gtk_separator_menu_item_new ();
	gtk_widget_show (item);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	item = gtk_menu_item_new_with_mnemonic (_("Move _Left"));
	gtk_widget_set_sensitive (item, get_item_position (proxy, NULL) > 0);
	gtk_widget_show (item);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	g_signal_connect (item, "activate",
			  G_CALLBACK (move_left_activate_cb), proxy);

	item = gtk_menu_item_new_with_mnemonic (_("Move Ri_ght"));
	get_item_position (proxy, &last);
	gtk_widget_set_sensitive (item, !last);
	gtk_widget_show (item);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	g_signal_connect (item, "activate",
			  G_CALLBACK (move_right_activate_cb), proxy);

	if (event != NULL)
	{
		gtk_menu_popup (GTK_MENU (menu), NULL, NULL, func, proxy,
				event->button, event->time);
	}
	else
	{
		gtk_menu_popup (GTK_MENU (menu), NULL, NULL, func, proxy, 0,
				gtk_get_current_event_time ());
		gtk_menu_shell_select_first (GTK_MENU_SHELL (menu), FALSE);
	}
}

static gboolean
popup_menu_cb (GtkWidget *widget, EphyTopicAction *action)
{
	if (gtk_widget_get_ancestor (widget, EPHY_TYPE_BOOKMARKSBAR))
        {
                show_context_menu (action, widget, NULL,
				   ephy_gui_menu_position_under_widget);
		return TRUE;
        }

	return FALSE;
}

static gboolean
button_release_cb (GtkWidget *widget,
                   GdkEventButton *event,
		   EphyTopicAction *action)
{
	if (event->button == 1)
	{
		stop_drag_check (action, widget);

		if (GTK_IS_TOGGLE_BUTTON (widget))
		{
			gtk_toggle_button_set_active
				(GTK_TOGGLE_BUTTON (widget), FALSE);
		}
	}

	return FALSE;
}

static gboolean
button_press_cb (GtkWidget *widget,
		 GdkEventButton *event,
		 EphyTopicAction *action)
{
	if (event->button == 1 &&
	    gtk_widget_get_ancestor (widget, EPHY_TYPE_BOOKMARKSBAR))
	{
		if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
		{
			GtkWidget *menu;

			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
			menu = g_object_get_data (G_OBJECT (widget), "popup");
			g_return_val_if_fail (menu != NULL, FALSE);

			g_object_set_data (G_OBJECT (menu), "button", widget);

			action->priv->drag_x = event->x;
			action->priv->drag_y = event->y;
			action->priv->motion_handler = g_signal_connect
				(menu, "motion_notify_event",
				 G_CALLBACK (drag_motion_cb), action);
			action->priv->release_handler = g_signal_connect
				(menu, "button_release_event",
				 G_CALLBACK (button_release_cb), action);
		
			return TRUE;
		}
	}
	else if (event->button == 3 &&
	         gtk_widget_get_ancestor (widget, EPHY_TYPE_BOOKMARKSBAR))	
	{
		show_context_menu (action, widget, event, NULL);
		return TRUE;
	}

	return FALSE;
}

static void
connect_proxy (GtkAction *action, GtkWidget *proxy)
{
	GtkWidget *button;

	LOG ("connect_proxy action %p, proxy %p", action, proxy)

	(* GTK_ACTION_CLASS (parent_class)->connect_proxy) (action, proxy);

	ephy_topic_action_sync_label (action, NULL, proxy);
	g_signal_connect_object (action, "notify::label",
			         G_CALLBACK (ephy_topic_action_sync_label), proxy, 0);

	if (GTK_IS_TOOL_ITEM (proxy))
	{
		button = GTK_WIDGET (g_object_get_data (G_OBJECT (proxy), "button"));
		g_signal_connect (button, "toggled",
				  G_CALLBACK (button_toggled_cb), action);
		g_signal_connect (button, "popup_menu",
				  G_CALLBACK (popup_menu_cb), action);
		g_signal_connect (button, "button-press-event",
				  G_CALLBACK (button_press_cb), action);
		g_signal_connect (button, "button-release-event",
				  G_CALLBACK (button_release_cb), action);
		g_signal_connect (button, "drag_data_get",
				  G_CALLBACK (drag_data_get_cb), action);
		g_signal_connect (button, "drag_data_delete",
				  G_CALLBACK (drag_data_delete_cb), action);
	}
}

static void
ephy_topic_action_set_topic_id (EphyTopicAction *action, guint id)
{
	EphyBookmarks *bookmarks;
	EphyNode *node;

	bookmarks = ephy_shell_get_bookmarks (ephy_shell);
	node = ephy_bookmarks_get_from_id (bookmarks, id);

	action->priv->topic_node = node;
	action->priv->topic_id = id;
}

static void
ephy_topic_action_set_property (GObject *object,
                                guint prop_id,
                                const GValue *value,
                                GParamSpec *pspec)
{
	EphyTopicAction *topic;

	topic = EPHY_TOPIC_ACTION (object);

	switch (prop_id)
	{
		case PROP_TOPIC_ID:
			ephy_topic_action_set_topic_id (topic, g_value_get_int (value));
			break;
	}
}

static void
ephy_topic_action_get_property (GObject *object,
                                   guint prop_id,
                                   GValue *value,
                                   GParamSpec *pspec)
{
	EphyTopicAction *bmk;

	bmk = EPHY_TOPIC_ACTION (object);

	switch (prop_id)
	{
		case PROP_TOPIC_ID:
			g_value_set_boolean (value, bmk->priv->topic_id);
			break;
	}
}

static void
ephy_topic_action_class_init (EphyTopicActionClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);
	GtkActionClass *action_class = GTK_ACTION_CLASS (class);

	parent_class = g_type_class_peek_parent (class);

	action_class->toolbar_item_type = GTK_TYPE_TOOL_ITEM;
	action_class->create_tool_item = create_tool_item;
	action_class->create_menu_item = create_menu_item;
	action_class->connect_proxy = connect_proxy;

	object_class->set_property = ephy_topic_action_set_property;
	object_class->get_property = ephy_topic_action_get_property;

	ephy_topic_action_signals[OPEN] =
                g_signal_new ("open",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (EphyTopicActionClass, open),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
			      G_TYPE_STRING);

	ephy_topic_action_signals[OPEN_IN_TAB] =
                g_signal_new ("open_in_tab",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (EphyTopicActionClass, open_in_tab),
                              NULL, NULL,
                              ephy_marshal_VOID__STRING_BOOLEAN,
                              G_TYPE_NONE,
                              2,
			      G_TYPE_STRING,
			      G_TYPE_BOOLEAN);

	ephy_topic_action_signals[OPEN_IN_TABS] =
                g_signal_new ("open_in_tabs",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (EphyTopicActionClass, open_in_tabs),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__POINTER,
                              G_TYPE_NONE,
                              1,
			      G_TYPE_POINTER);

	g_object_class_install_property (object_class,
                                         PROP_TOPIC_ID,
                                         g_param_spec_int ("topic_id",
                                                           "topic_id",
                                                           "topic_id",
							   0,
							   G_MAXINT,
                                                           0,
                                                           G_PARAM_READWRITE));

	g_type_class_add_private (object_class, sizeof(EphyTopicActionPrivate));
}

static void
sync_topic_properties (GtkAction *action, EphyNode *bmk)
{
	const char *tmp;
	char *title;
	int priority;

	priority = ephy_node_get_property_int 
		(bmk, EPHY_NODE_KEYWORD_PROP_PRIORITY);

	if (priority == EPHY_NODE_ALL_PRIORITY)
	{
		tmp = _("Bookmarks");
	}
	else
	{
		tmp = ephy_node_get_property_string
        	        (bmk, EPHY_NODE_KEYWORD_PROP_NAME);
	}

	title = ephy_string_double_underscores (tmp);

	g_object_set (action, "label", title, NULL);

	g_free (title);
}

static void
topic_child_changed_cb (EphyNode *node,
			EphyNode *child,
			guint property_id,
			GtkAction *action)
{
	guint id;

	id = EPHY_TOPIC_ACTION (action)->priv->topic_id;

	if (id == ephy_node_get_id (child))
	{
		sync_topic_properties (action, child);
	}
}

static void
ephy_topic_action_init (EphyTopicAction *action)
{
	EphyBookmarks *bookmarks;
	EphyNode *node;

	action->priv = EPHY_TOPIC_ACTION_GET_PRIVATE (action);

	bookmarks = ephy_shell_get_bookmarks (ephy_shell);
	node = ephy_bookmarks_get_keywords (bookmarks);
	ephy_node_signal_connect_object (node, EPHY_NODE_CHILD_CHANGED,
				         (EphyNodeCallback) topic_child_changed_cb,
				         G_OBJECT (action));
}

GtkAction *
ephy_topic_action_new (const char *name, guint id)
{
	EphyNode *bmk;
	EphyBookmarks *bookmarks;
	GtkAction *action;

	bookmarks = ephy_shell_get_bookmarks (ephy_shell);

	bmk = ephy_bookmarks_get_from_id (bookmarks, id);
	g_return_val_if_fail (bmk != NULL, NULL);

	action = GTK_ACTION (g_object_new (EPHY_TYPE_TOPIC_ACTION,
					   "topic_id", id,
					   "name", name,
					   NULL));

	sync_topic_properties (action, bmk);

	return action;
}
