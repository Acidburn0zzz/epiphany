/*
 *  Copyright (C) 2002  Ricardo Fernández Pascual
 *  Copyright (C) 2003  Marco Pesenti Gritti
 *  Copyright (C) 2003  Christian Persch
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

#include "ephy-encoding-menu.h"
#include "ephy-encoding-dialog.h"
#include "ephy-encodings.h"
#include "ephy-embed.h"
#include "ephy-embed-shell.h"
#include "ephy-shell.h"
#include "ephy-string.h"
#include "ephy-debug.h"

#include <gtk/gtkaction.h>
#include <gtk/gtktoggleaction.h>
#include <gtk/gtkradioaction.h>
#include <gtk/gtkuimanager.h>
#include <glib/gi18n.h>
#include <string.h>

#define EPHY_ENCODING_MENU_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE ((object), EPHY_TYPE_ENCODING_MENU, EphyEncodingMenuPrivate))

struct _EphyEncodingMenuPrivate
{
	EphyEncodings *encodings;
	EphyWindow *window;
	GtkUIManager *manager;
	GtkActionGroup *action_group;
	gboolean update_tag;
	guint merge_id;
	GSList *encodings_radio_group;
	EphyEncodingDialog *dialog;
};

#define ENCODING_PLACEHOLDER_PATH	"/menubar/ViewMenu/ViewEncodingMenu/ViewEncodingPlaceholder"

static void	ephy_encoding_menu_class_init	  (EphyEncodingMenuClass *klass);
static void	ephy_encoding_menu_init		  (EphyEncodingMenu *menu);

enum
{
	PROP_0,
	PROP_WINDOW
};

static GObjectClass *parent_class = NULL;

GType
ephy_encoding_menu_get_type (void)
{
	static GType ephy_encoding_menu_type = 0;

	if (ephy_encoding_menu_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (EphyEncodingMenuClass),
			NULL,
			NULL,
			(GClassInitFunc) ephy_encoding_menu_class_init,
			NULL,
			NULL,
			sizeof (EphyEncodingMenu),
			0,
			(GInstanceInitFunc) ephy_encoding_menu_init
		};

		ephy_encoding_menu_type = g_type_register_static (G_TYPE_OBJECT,
								  "EphyEncodingMenu",
								  &our_info, 0);
	}

	return ephy_encoding_menu_type;
}

static void
ephy_encoding_menu_init (EphyEncodingMenu *menu)
{
	menu->priv = EPHY_ENCODING_MENU_GET_PRIVATE (menu);

	menu->priv->encodings =
		EPHY_ENCODINGS (ephy_embed_shell_get_encodings
				(EPHY_EMBED_SHELL (ephy_shell)));

	menu->priv->update_tag = FALSE;
	menu->priv->action_group = NULL;
	menu->priv->merge_id = 0;
	menu->priv->encodings_radio_group = NULL;
	menu->priv->dialog = NULL;
}

static int
sort_encodings (gconstpointer a, gconstpointer b)
{
	EphyNode *node1 = (EphyNode *) a;
	EphyNode *node2 = (EphyNode *) b;
	const char *key1, *key2;

	key1 = ephy_node_get_property_string
			(node1, EPHY_NODE_ENCODING_PROP_COLLATION_KEY);
	key2 = ephy_node_get_property_string
			(node2, EPHY_NODE_ENCODING_PROP_COLLATION_KEY);

	return strcmp (key1, key2);
}

static void
add_menu_item (EphyNode *node, EphyEncodingMenu *menu)
{
	const char *code;
	char action[128], name[128];

	code = ephy_node_get_property_string
				(node, EPHY_NODE_ENCODING_PROP_ENCODING);

	g_snprintf (action, sizeof (action), "Encoding%s", code);
	g_snprintf (name, sizeof (name), "%sItem", action);

	gtk_ui_manager_add_ui (menu->priv->manager, menu->priv->merge_id,
			       ENCODING_PLACEHOLDER_PATH,
			       name, action,
			       GTK_UI_MANAGER_MENUITEM, FALSE);
}

static void
update_encoding_menu_cb (GtkAction *dummy, EphyEncodingMenu *menu)
{
	EphyEncodingMenuPrivate *p = menu->priv;
	EphyEmbed *embed;
	GtkAction *action;
	EphyEncodingInfo *info;
	char name[128];
	const char *encoding;
	EphyNode *enc_node;
	GList *recent, *related = NULL, *l;
	EphyLanguageGroup groups;
	gboolean is_automatic;

	START_PROFILER ("Rebuilding encoding menu")

	/* FIXME: block the "activate" signal on the actions instead; needs to 
	 * wait until g_signal_handlers_block_matched supports blocking
	 * by signal id alone.
	 */
	menu->priv->update_tag = TRUE;

	/* get most recently used encodings */
	recent = ephy_encodings_get_recent (p->encodings);

	embed = ephy_window_get_active_embed (p->window);
	info = ephy_embed_get_encoding_info (embed);
	if (info == NULL) goto build_menu;

	LOG ("encoding information\n enc='%s' default='%s' hint='%s' "
	     "prev_doc='%s' forced='%s' parent='%s' source=%d "
	     "hint_source=%d parent_source=%d", info->encoding,
		info->default_encoding, info->hint_encoding,
		info->prev_doc_encoding, info->forced_encoding,
		info->parent_encoding, info->encoding_source,
		info->hint_encoding_source, info->parent_encoding_source)

	encoding = info->encoding;

	enc_node = ephy_encodings_get_node (p->encodings, encoding);
	if (!EPHY_IS_NODE (enc_node))
	{
		goto build_menu;
	}

	/* set the encodings group's active member */
	g_snprintf (name, sizeof (name), "Encoding%s", encoding);
	action = gtk_action_group_get_action (p->action_group, name);
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), TRUE);

	/* get encodings related to the current encoding */
	groups = ephy_node_get_property_int
			(enc_node, EPHY_NODE_ENCODING_PROP_LANGUAGE_GROUPS);

	related = ephy_encodings_get_encodings (p->encodings, groups);
	related = g_list_sort (related, (GCompareFunc) sort_encodings);

	/* add the current encoding to the list of
	 * things to display, making sure we don't add it more than once
	 */
	if (g_list_find (related, enc_node) == NULL
	    && g_list_find (recent, enc_node) == NULL)
	{
		recent = g_list_prepend (recent, enc_node);
	}

	/* make sure related and recent are disjoint so we don't display twice */
	for (l = related; l != NULL; l = l->next)
	{
		recent = g_list_remove (recent, l->data);
	}

	recent = g_list_sort (recent, (GCompareFunc) sort_encodings);

build_menu:
	/* check if encoding was overridden */
	is_automatic = ephy_encoding_info_is_automatic (info);

	action = gtk_action_group_get_action (p->action_group,
					      "ViewEncodingAutomatic");
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), is_automatic);
	g_object_set (G_OBJECT (action), "sensitive", !is_automatic, NULL);


	/* clear the menu */
	if (p->merge_id > 0)
	{
		gtk_ui_manager_remove_ui (p->manager, p->merge_id);
		gtk_ui_manager_ensure_update (p->manager);
	}

	/* build the new menu */
	p->merge_id = gtk_ui_manager_new_merge_id (p->manager);

	gtk_ui_manager_add_ui (p->manager, p->merge_id,
			       ENCODING_PLACEHOLDER_PATH,
			       "ViewEncodingAutomaticItem",
			       "ViewEncodingAutomatic",
			       GTK_UI_MANAGER_MENUITEM, FALSE);

	gtk_ui_manager_add_ui (p->manager, p->merge_id,
			       ENCODING_PLACEHOLDER_PATH,
			       "Sep1Item", "Sep1",
			       GTK_UI_MANAGER_SEPARATOR, FALSE);

	g_list_foreach (recent, (GFunc) add_menu_item, menu);

	gtk_ui_manager_add_ui (p->manager, p->merge_id,
			       ENCODING_PLACEHOLDER_PATH,
			       "Sep2Item", "Sep2",
			       GTK_UI_MANAGER_SEPARATOR, FALSE);

	g_list_foreach (related, (GFunc) add_menu_item, menu);

	gtk_ui_manager_add_ui (p->manager, p->merge_id,
			       ENCODING_PLACEHOLDER_PATH,
			       "Sep3Item", "Sep3",
			       GTK_UI_MANAGER_SEPARATOR, FALSE);

	gtk_ui_manager_add_ui (p->manager, p->merge_id,
			       ENCODING_PLACEHOLDER_PATH,
			       "ViewEncodingOtherItem",
			       "ViewEncodingOther",
			       GTK_UI_MANAGER_MENUITEM, FALSE);

	/* cleanup */
	g_list_free (related);
	g_list_free (recent);

	ephy_encoding_info_free (info);

	menu->priv->update_tag = FALSE;

	STOP_PROFILER ("Rebuilding encoding menu")
}

static void
encoding_activate_cb (GtkAction *action, EphyEncodingMenu *menu)
{
	EphyEmbed *embed;
	EphyEncodingInfo *info;
	const char *name, *encoding;

	if (gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)) == FALSE
	    || menu->priv->update_tag)
	{
		return;
	}

	name = gtk_action_get_name (GTK_ACTION (action));
	encoding = name + strlen("Encoding");

	embed = ephy_window_get_active_embed (menu->priv->window);

	info = ephy_embed_get_encoding_info (embed);
	if (info == NULL) return;

	/* Force only when different from current encoding */
	if (info->encoding && strcmp (info->encoding, encoding) != 0)
	{
		ephy_embed_set_encoding (embed, encoding);
	}

	ephy_encoding_info_free (info);

	ephy_encodings_add_recent (menu->priv->encodings, encoding);
}

static void
add_action (EphyNode *node, EphyEncodingMenu *menu)
{
	GtkAction *action;
	char name[128];
	const char *encoding, *title;

	encoding = ephy_node_get_property_string
			(node, EPHY_NODE_ENCODING_PROP_ENCODING);
	title = ephy_node_get_property_string
			(node, EPHY_NODE_ENCODING_PROP_TITLE);

	g_snprintf (name, sizeof (name), "Encoding%s", encoding);

	action = g_object_new (GTK_TYPE_RADIO_ACTION,
			       "name", name,
			       "label", title,
			       NULL);

	gtk_radio_action_set_group (GTK_RADIO_ACTION (action),
				    menu->priv->encodings_radio_group);
	menu->priv->encodings_radio_group = gtk_radio_action_get_group
						(GTK_RADIO_ACTION (action));

	g_signal_connect (action, "activate",
			  G_CALLBACK (encoding_activate_cb),
			  menu);

	gtk_action_group_add_action (menu->priv->action_group, action);
	g_object_unref (action);
}

static void
ephy_encoding_menu_view_dialog_cb (GtkAction *action, EphyEncodingMenu *menu)
{
	if (menu->priv->dialog == NULL)
	{
		menu->priv->dialog = ephy_encoding_dialog_new
					(menu->priv->window);

		g_object_add_weak_pointer(G_OBJECT (menu->priv->dialog),
					  (gpointer *) &menu->priv->dialog);
	}

	ephy_dialog_show (EPHY_DIALOG (menu->priv->dialog));
}

static void
ephy_encoding_menu_automatic_cb (GtkAction *action, EphyEncodingMenu *menu)
{
	EphyEmbed *embed;

	if (gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)) == FALSE
	    || menu->priv->update_tag)
	{
		return;
	}

	embed = ephy_window_get_active_embed (menu->priv->window);

	/* setting "" will clear the forced encoding */
	ephy_embed_set_encoding (embed, "");
}

static GtkActionEntry menu_entries [] =
{
	{ "ViewEncodingOther", NULL, N_("_Other..."), NULL,
	  N_("Other encodings"),
	  G_CALLBACK (ephy_encoding_menu_view_dialog_cb) }
};
static guint n_menu_entries = G_N_ELEMENTS (menu_entries);

static GtkToggleActionEntry toggle_menu_entries [] =
{
	{ "ViewEncodingAutomatic", NULL, N_("_Automatic"), NULL,
	  N_("Use the encoding specified by the document"),
	  G_CALLBACK (ephy_encoding_menu_automatic_cb), FALSE }
};
static const guint n_toggle_menu_entries = G_N_ELEMENTS (toggle_menu_entries);

static void
ephy_encoding_menu_set_window (EphyEncodingMenu *menu, EphyWindow *window)
{
	GtkActionGroup *action_group;
	GtkAction *action;
	GList *encodings;

	g_return_if_fail (EPHY_IS_WINDOW (window));

	menu->priv->window = window;
	menu->priv->manager = GTK_UI_MANAGER (window->ui_merge);

	action_group = gtk_action_group_new ("EncodingActions");
	gtk_action_group_set_translation_domain (action_group, NULL);
	menu->priv->action_group = action_group;

	gtk_action_group_add_actions (action_group, menu_entries,
				      n_menu_entries, menu);
	gtk_action_group_add_toggle_actions (action_group, toggle_menu_entries,
                                    	     n_toggle_menu_entries, menu);

	encodings = ephy_encodings_get_encodings (menu->priv->encodings, LG_ALL);
	g_list_foreach (encodings, (GFunc) add_action, menu);
	g_list_free (encodings);

	gtk_ui_manager_insert_action_group (menu->priv->manager,
					    action_group, 0);
	g_object_unref (action_group);

	action = gtk_ui_manager_get_action (menu->priv->manager,
					    "/menubar/ViewMenu");
	g_signal_connect_object (action, "activate",
				 G_CALLBACK (update_encoding_menu_cb),
				 menu, 0);
}

static void
ephy_encoding_menu_set_property (GObject *object,
				 guint prop_id,
				 const GValue *value,
				 GParamSpec *pspec)
{
	EphyEncodingMenu *menu = EPHY_ENCODING_MENU (object);

	switch (prop_id)
	{
		case PROP_WINDOW:
			ephy_encoding_menu_set_window (menu, g_value_get_object (value));
			break;
	}
}

static void
ephy_encoding_menu_get_property (GObject *object,
				 guint prop_id,
				 GValue *value,
				 GParamSpec *pspec)
{
	EphyEncodingMenu *menu = EPHY_ENCODING_MENU (object);

	switch (prop_id)
	{
		case PROP_WINDOW:
			g_value_set_object (value, menu->priv->window);
			break;
	}
}

static void
ephy_encoding_menu_finalize (GObject *object)
{
	EphyEncodingMenu *menu = EPHY_ENCODING_MENU (object); 

	if (menu->priv->dialog)
	{
		g_object_unref (menu->priv->dialog);
	}

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
ephy_encoding_menu_class_init (EphyEncodingMenuClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = ephy_encoding_menu_finalize;
	object_class->set_property = ephy_encoding_menu_set_property;
	object_class->get_property = ephy_encoding_menu_get_property;

	g_object_class_install_property (object_class,
					 PROP_WINDOW,
					 g_param_spec_object ("window",
							      "Window",
							      "Parent window",
							      EPHY_TYPE_WINDOW,
							      G_PARAM_READWRITE |
							      G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (object_class, sizeof(EphyEncodingMenuPrivate));
}

EphyEncodingMenu *
ephy_encoding_menu_new (EphyWindow *window)
{
	return g_object_new (EPHY_TYPE_ENCODING_MENU,
			     "window", window,
			     NULL);
}
