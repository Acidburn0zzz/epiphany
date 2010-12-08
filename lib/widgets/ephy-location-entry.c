/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright © 2002  Ricardo Fernández Pascual
 *  Copyright © 2003, 2004  Marco Pesenti Gritti
 *  Copyright © 2003, 2004, 2005  Christian Persch
 *  Copyright © 2008  Xan López
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

#include "ephy-tree-model-node.h"
#include "ephy-location-entry.h"
#include "ephy-marshal.h"
#include "ephy-signal-accumulator.h"
#include "ephy-dnd.h"
#include "egg-editable-toolbar.h"
#include "ephy-stock-icons.h"
#include "ephy-debug.h"
#include "ephy-gui.h"

#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include <string.h>

/**
 * SECTION:ephy-location-entry
 * @short_description: A location entry widget
 * @see_also: #GtkEntry
 *
 * #EphyLocationEntry implements the location bar in the main Epiphany window.
 */

#define EPHY_LOCATION_ENTRY_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE ((object), EPHY_TYPE_LOCATION_ENTRY, EphyLocationEntryPrivate))

struct _EphyLocationEntryPrivate
{
	GtkWidget *entry;
	char *lock_stock_id;
	GdkPixbuf *favicon;
	GdkColor secure_bg_colour;
	GdkColor secure_fg_colour;

	GSList *search_terms;

	char *before_completion;
	char *saved_text;

	guint text_col;
	guint action_col;
	guint keywords_col;
	guint relevance_col;
	guint url_col;
	guint extra_col;
	guint favicon_col;

	guint hash;

	guint user_changed : 1;
	guint can_redo : 1;
	guint block_update : 1;
	guint original_address : 1;
	guint secure : 1;
	guint apply_colours : 1;
	guint needs_reset : 1;
	guint show_lock : 1;
};

static const GtkTargetEntry url_drag_types [] =
{
	{ EPHY_DND_URL_TYPE,        0, 0 },
	{ EPHY_DND_URI_LIST_TYPE,   0, 1 },
	{ EPHY_DND_TEXT_TYPE,       0, 2 }
};

static const GdkColor fallback_bg_colour = { 0, 0xf7f7, 0xf7f7, 0xbebe }; /* yellow-ish */
static const GdkColor fallback_fg_colour = { 0, 0, 0, 0 }; /* black */

static void ephy_location_entry_class_init (EphyLocationEntryClass *klass);
static void ephy_location_entry_init (EphyLocationEntry *le);
static gboolean ephy_location_entry_reset_internal (EphyLocationEntry *, gboolean);

static void textcell_data_func (GtkCellLayout *cell_layout,
				GtkCellRenderer *cell,
				GtkTreeModel *tree_model,
				GtkTreeIter *iter,
				gpointer data);
static void extracell_data_func (GtkCellLayout *cell_layout,
				 GtkCellRenderer *cell,
				 GtkTreeModel *tree_model,
				 GtkTreeIter *iter,
				 gpointer data);

enum signalsEnum
{
	USER_CHANGED,
	LOCK_CLICKED,
	GET_LOCATION,
	GET_TITLE,
	LAST_SIGNAL
};
static gint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (EphyLocationEntry, ephy_location_entry, GTK_TYPE_TOOL_ITEM)

static void
ephy_location_entry_style_set (GtkWidget *widget,
			       GtkStyle *previous_style)
{
	EphyLocationEntry *entry = EPHY_LOCATION_ENTRY (widget);
	EphyLocationEntryPrivate *priv = entry->priv;
	GtkSettings *settings;
	GdkColor *bg_colour = NULL, *fg_colour = NULL;
	GdkColor title_fg_colour;
	char *theme;
	gboolean is_a11y_theme;

	if (GTK_WIDGET_CLASS (ephy_location_entry_parent_class)->style_set)
	{
		GTK_WIDGET_CLASS (ephy_location_entry_parent_class)->style_set (widget, previous_style);
	}

	title_fg_colour = gtk_widget_get_style (widget)->text[GTK_STATE_INSENSITIVE];

	settings = gtk_settings_get_for_screen (gtk_widget_get_screen (widget));
	g_object_get (settings, "gtk-theme-name", &theme, NULL);
	is_a11y_theme = strstr (theme, "HighContrast") != NULL ||
			strstr (theme, "LowContrast") != NULL;
	g_free (theme);

	gtk_widget_style_get (widget,
			      "secure-fg-color", &fg_colour,
			      "secure-bg-color", &bg_colour,
			      NULL);

	/* We only use the fallback colours when we don't have an a11y theme */
	priv->apply_colours = !is_a11y_theme || (fg_colour != NULL && bg_colour != NULL);

	if (fg_colour != NULL)
	{
		priv->secure_fg_colour = *fg_colour;
		gdk_color_free (fg_colour);
	}
	else
	{
		priv->secure_fg_colour = fallback_fg_colour;
	}

	if (bg_colour != NULL)
	{
		priv->secure_bg_colour = *bg_colour;
		gdk_color_free (bg_colour);
	}
	else
	{
		priv->secure_bg_colour = fallback_bg_colour;
	}

	/* Apply the new style */
	ephy_location_entry_set_secure (entry, priv->secure);
}

inline static void
free_search_terms (GSList *search_terms)
{
	GSList *iter;
	
	for (iter = search_terms; iter != NULL; iter = iter->next)
		g_regex_unref ((GRegex*)iter->data);
	
	g_slist_free (search_terms);
}

static void
ephy_location_entry_finalize (GObject *object)
{
	EphyLocationEntry *entry = EPHY_LOCATION_ENTRY (object);
	EphyLocationEntryPrivate *priv = entry->priv;
	
	g_free (priv->saved_text);
	g_free (priv->lock_stock_id);

	if (priv->favicon != NULL)
	{
		g_object_unref (priv->favicon);
	}
	
	if (priv->search_terms)
	{
		free_search_terms (priv->search_terms);
		priv->search_terms = NULL;
	}

	G_OBJECT_CLASS (ephy_location_entry_parent_class)->finalize (object);
}

static void
ephy_location_entry_class_init (EphyLocationEntryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->finalize = ephy_location_entry_finalize;

	widget_class->style_set = ephy_location_entry_style_set;

       /**
	* EphyLocationEntry::user-changed:
	* @entry: the object on which the signal is emitted
	*
	* Emitted when the user changes the contents of the internal #GtkEntry
	*
	*/
	signals[USER_CHANGED] = g_signal_new (
		"user_changed", G_OBJECT_CLASS_TYPE (klass),
		G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EphyLocationEntryClass, user_changed),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE,
		0,
		G_TYPE_NONE);

       /**
	* EphyLocationEntry::lock-clicked:
	* @entry: the object on which the signal is emitted
	*
	* Emitted when the user clicks the security icon inside the
	* #EphyLocationEntry.
	*
	*/
	signals[LOCK_CLICKED] = g_signal_new (
		"lock-clicked",
		EPHY_TYPE_LOCATION_ENTRY,
		G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EphyLocationEntryClass, lock_clicked),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE,
		0);

       /**
	* EphyLocationEntry::get-location:
	* @entry: the object on which the signal is emitted
	* Returns: the current page address as a string
	*
	* For drag and drop purposes, the location bar will request you the
	* real address of where it is pointing to. The signal handler for this
	* function should return the address of the currently loaded site.
	*
	*/
	signals[GET_LOCATION] = g_signal_new (
		"get-location", G_OBJECT_CLASS_TYPE (klass),
		G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EphyLocationEntryClass, get_location),
		ephy_signal_accumulator_string, NULL,
		ephy_marshal_STRING__VOID,
		G_TYPE_STRING,
		0,
		G_TYPE_NONE);

       /**
	* EphyLocationEntry::get-title:
	* @entry: the object on which the signal is emitted
	* Returns: the current page title as a string
	*
	* For drag and drop purposes, the location bar will request you the
	* title of where it is pointing to. The signal handler for this
	* function should return the title of the currently loaded site.
	*
	*/
	signals[GET_TITLE] = g_signal_new (
		"get-title", G_OBJECT_CLASS_TYPE (klass),
		G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EphyLocationEntryClass, get_title),
		ephy_signal_accumulator_string, NULL,
		ephy_marshal_STRING__VOID,
		G_TYPE_STRING,
		0,
		G_TYPE_NONE);

	gtk_widget_class_install_style_property (widget_class,
						 g_param_spec_boxed ("secure-bg-color",
								     "Secure background colour",
								     "Background colour to use for secure sites",
								     GDK_TYPE_COLOR,
								     G_PARAM_READABLE | G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB));

	gtk_widget_class_install_style_property (widget_class,
						 g_param_spec_boxed ("secure-fg-color",
								     "Secure foreground Colour",
								     "Foreground colour to use for secure sites",
								     GDK_TYPE_COLOR,
								     G_PARAM_READABLE | G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB));

	g_type_class_add_private (object_class, sizeof (EphyLocationEntryPrivate));
}

static void
update_address_state (EphyLocationEntry *entry)
{
	EphyLocationEntryPrivate *priv = entry->priv;
	const char *text;

	text = gtk_entry_get_text (GTK_ENTRY (priv->entry));
	priv->original_address = text != NULL &&
				 g_str_hash (text) == priv->hash;
}

static void
update_favicon (EphyLocationEntry *lentry)
{
	EphyLocationEntryPrivate *priv = lentry->priv;
	GtkEntry *entry = GTK_ENTRY (priv->entry);

	/* Only show the favicon if the entry's text is the
	 * address of the current page.
	 */
	if (priv->favicon != NULL && priv->original_address)
	{
		gtk_entry_set_icon_from_pixbuf (entry,
						GTK_ENTRY_ICON_PRIMARY,
						priv->favicon);
	}
	else
	{
		/* Here we could consider using fallback favicon that matches
		 * the page MIME type, though text/html should be good enough
		 * most of the time. See #337140
		 */
		gtk_entry_set_icon_from_icon_name (entry,
						   GTK_ENTRY_ICON_PRIMARY,
						   "text-html");
	}
}

static void
editable_changed_cb (GtkEditable *editable,
		     EphyLocationEntry *entry)
{
	EphyLocationEntryPrivate *priv = entry->priv;
	const char *text;
	char *pattern;

	update_address_state (entry);

	if (priv->block_update == TRUE) 
		return;
	else
	{
		priv->user_changed = TRUE;
		priv->can_redo = FALSE;
	}	
	
	if (priv->search_terms)
	{
		free_search_terms (priv->search_terms);
		priv->search_terms = NULL;
	}

	text = gtk_entry_get_text (GTK_ENTRY (editable));

	/*
	 * user is specifying a regular expression, so we will
	 * have only one search term
	 */
	if (g_str_has_prefix (text, "re:"))
	{
		GRegex *regex;
		pattern = g_strdup (text+3);
		regex = g_regex_new (pattern,
				     G_REGEX_CASELESS | G_REGEX_OPTIMIZE,
				     G_REGEX_MATCH_NOTEMPTY, NULL);
		priv->search_terms = g_slist_append (priv->search_terms, regex);
		g_free (pattern);
	}
	else
	{
		const char *current;
		const char *ptr;
		char *tmp;
		char *term;
		GRegex *term_regex;
		GRegex *quote_regex;
		gint count;
		gboolean inside_quotes = FALSE;

		quote_regex = g_regex_new ("\"", G_REGEX_OPTIMIZE,
					   G_REGEX_MATCH_NOTEMPTY, NULL);
		
		/*
		 * This code loops through the string using pointer arythmetics.
		 * Although the string we are handling may contain UTF-8 chars
		 * this works because only ASCII chars affect what is actually
		 * copied from the string as a search term.
		 */
		for (count = 0, current = ptr = text; ptr[0] != '\0'; ptr++, count++)
		{
			/*
			 * If we found a double quote character; we will 
			 * consume bytes up until the next quote, or
			 * end of line;
			 */
			if (ptr[0] == '"')
				inside_quotes = !inside_quotes;

			/*
			 * If we found a space, and we are not looking for a
			 * closing double quote, or if the next char is the
			 * end of the string, append what we have already as
			 * a search term.
			 */
			if (((ptr[0] == ' ') && (!inside_quotes)) || ptr[1] == '\0')
			{
				/*
				 * We special-case the end of the line because
				 * we would otherwise not copy the last character
				 * of the search string, since the for loop will
				 * stop before that.
				 */
				if (ptr[1] == '\0')
					count++;
				
				/*
				 * remove quotes, and quote any regex-sensitive
				 * characters
				 */
				tmp = g_regex_escape_string (current, count);
				term = g_regex_replace (quote_regex, tmp, -1, 0,
							"", G_REGEX_MATCH_NOTEMPTY, NULL);
				g_strstrip (term);
				g_free (tmp);

				/* we don't want empty search terms */
				if (term[0] != '\0')
				{
					term_regex = g_regex_new (term,
								  G_REGEX_CASELESS | G_REGEX_OPTIMIZE,
								  G_REGEX_MATCH_NOTEMPTY, NULL);
					priv->search_terms = g_slist_append (priv->search_terms, term_regex);
				}
				g_free (term);

				 /* count will be incremented by the for loop */
				count = -1;
				current = ptr + 1;
			}
		}

		g_regex_unref (quote_regex);
	}

	g_signal_emit (entry, signals[USER_CHANGED], 0);
}

static gboolean
entry_key_press_cb (GtkEntry *entry,
		    GdkEventKey *event,
		    EphyLocationEntry *lentry)
{
	guint state = event->state & gtk_accelerator_get_default_mod_mask ();

	if (event->keyval == GDK_KEY_Escape && state == 0)
	{
		ephy_location_entry_reset_internal (lentry, TRUE);
		/* don't return TRUE since we want to cancel the autocompletion popup too */
	}

	return FALSE;
}

static gboolean
entry_key_press_after_cb (GtkEntry *entry,
			  GdkEventKey *event,
			  EphyLocationEntry *lentry)
{
	EphyLocationEntryPrivate *priv = lentry->priv;

	guint state = event->state & gtk_accelerator_get_default_mod_mask ();

	if ((event->keyval == GDK_KEY_Return ||
	     event->keyval == GDK_KEY_KP_Enter ||
	     event->keyval == GDK_KEY_ISO_Enter) &&
	    (state == GDK_CONTROL_MASK ||
	     state == (GDK_CONTROL_MASK | GDK_SHIFT_MASK)))
	{
		//gtk_im_context_reset (entry->im_context);

		priv->needs_reset = TRUE;
		g_signal_emit_by_name (entry, "activate");

		return TRUE;
	}
	
	if ((event->keyval == GDK_KEY_Down || event->keyval == GDK_KEY_KP_Down)
	    && state == 0)
	{
		/* If we are focusing the entry, with the cursor at the end of it
		 * we emit the changed signal, so that the completion popup appears */
		const char *string;
		
		string = gtk_entry_get_text (entry);
		if (gtk_editable_get_position (GTK_EDITABLE (entry)) == strlen (string))
		{
			g_signal_emit_by_name (entry, "changed", 0);
			return TRUE;
		}
	}

	return FALSE;
}

static void
entry_activate_after_cb (GtkEntry *entry,
			 EphyLocationEntry *lentry)
{
	EphyLocationEntryPrivate *priv = lentry->priv;
	
	priv->user_changed = FALSE;

	if (priv->needs_reset)
	{
		ephy_location_entry_reset_internal (lentry, TRUE);
		priv->needs_reset = FALSE;
	}
}

static gboolean
match_selected_cb (GtkEntryCompletion *completion,
		   GtkTreeModel *model,
		   GtkTreeIter *iter,
		   EphyLocationEntry *entry)
{
	EphyLocationEntryPrivate *priv = entry->priv;
	char *item = NULL;
	guint state;

	gtk_tree_model_get (model, iter,
			    priv->action_col, &item, -1);
	if (item == NULL) return FALSE;

	ephy_gui_get_current_event (NULL, &state, NULL);

	priv->needs_reset = (state == GDK_CONTROL_MASK ||
			     state == (GDK_CONTROL_MASK | GDK_SHIFT_MASK));

	ephy_location_entry_set_location (entry, item);
	//gtk_im_context_reset (GTK_ENTRY (entry->priv->entry)->im_context);
	g_signal_emit_by_name (priv->entry, "activate");

	g_free (item);

	return TRUE;
}

static void
action_activated_after_cb (GtkEntryCompletion *completion,
			   gint index,
			   EphyLocationEntry *lentry)
{
	guint state, button;

	ephy_gui_get_current_event (NULL, &state, &button);
	if ((state == GDK_CONTROL_MASK ||
	     state == (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) ||
	    button == 2)
	{
		ephy_location_entry_reset_internal (lentry, TRUE);
	}
}

static gboolean
toolbar_is_editable (GtkWidget *widget)
{
	GtkWidget *etoolbar;

	etoolbar = gtk_widget_get_ancestor (widget, EGG_TYPE_EDITABLE_TOOLBAR);

	if (etoolbar)
	{
		return egg_editable_toolbar_get_edit_mode
			(EGG_EDITABLE_TOOLBAR (etoolbar));
	}

	return FALSE;
}

static gboolean
entry_drag_motion_cb (GtkWidget        *widget,
		      GdkDragContext   *context,
		      gint              x,
		      gint              y,
		      guint             time)
{
	if (toolbar_is_editable (widget))
	{
		g_signal_stop_emission_by_name (widget, "drag_motion");
	}

	return FALSE;
}

static gboolean
entry_drag_drop_cb (GtkWidget          *widget,
		    GdkDragContext     *context,
		    gint                x,
		    gint                y,
		    guint               time)
{
	if (toolbar_is_editable (widget))
	{
		g_signal_stop_emission_by_name (widget, "drag_drop");
	}

	return FALSE;
}

static void
entry_clear_activate_cb (GtkMenuItem *item,
			 EphyLocationEntry *entry)
{
	EphyLocationEntryPrivate *priv = entry->priv;

	priv->block_update = TRUE;
	gtk_entry_set_text (GTK_ENTRY (priv->entry), "");
	priv->block_update = FALSE;
	priv->user_changed = TRUE;
}

static void
entry_redo_activate_cb (GtkMenuItem *item,
			EphyLocationEntry *entry)
{
	ephy_location_entry_undo_reset (entry);
}

static void
entry_undo_activate_cb (GtkMenuItem *item,
			EphyLocationEntry *entry)
{
	ephy_location_entry_reset_internal (entry, FALSE);
}

static void
entry_populate_popup_cb (GtkEntry *entry,
			 GtkMenu *menu,
			 EphyLocationEntry *lentry)
{
	EphyLocationEntryPrivate *priv = lentry->priv;
	GtkWidget *image;
	GtkWidget *clear_menuitem, *undo_menuitem, *redo_menuitem, *separator;
	GList *children, *item;
	int pos = 0, sep = 0;
	gboolean is_editable;

	/* Clear and Copy mnemonics conflict, make custom menuitem */
	image = gtk_image_new_from_stock (GTK_STOCK_CLEAR, GTK_ICON_SIZE_MENU);
	gtk_widget_show (image);

	/* Translators: the mnemonic shouldn't conflict with any of the
	 * standard items in the GtkEntry context menu (Cut, Copy, Paste, Delete,
	 * Select All, Input Methods and Insert Unicode control character.)
	 */
	clear_menuitem = gtk_image_menu_item_new_with_mnemonic (_("Cl_ear"));
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (clear_menuitem), image);
	g_signal_connect (clear_menuitem , "activate",
			  G_CALLBACK (entry_clear_activate_cb), lentry);
	is_editable = gtk_editable_get_editable (GTK_EDITABLE (priv->entry));
	gtk_widget_set_sensitive (clear_menuitem, is_editable);
	gtk_widget_show (clear_menuitem);

	/* search for the 2nd separator (the one after Select All) in the context
	 * menu, and insert this menu item before it.
	 * It's a bit of a hack, but there seems to be no better way to do it :/
	 */
	children = gtk_container_get_children (GTK_CONTAINER (menu));
	for (item = children; item != NULL && sep < 2; item = item->next, pos++)
	{
		if (GTK_IS_SEPARATOR_MENU_ITEM (item->data)) sep++;
	}

	gtk_menu_shell_insert (GTK_MENU_SHELL (menu), clear_menuitem, pos - 1);
	
	undo_menuitem = gtk_image_menu_item_new_from_stock (GTK_STOCK_UNDO, NULL);
	gtk_widget_set_sensitive (undo_menuitem, priv->user_changed);
	g_signal_connect (undo_menuitem, "activate",
			  G_CALLBACK (entry_undo_activate_cb), lentry);
	gtk_widget_show (undo_menuitem);
	gtk_menu_shell_insert (GTK_MENU_SHELL (menu), undo_menuitem, 0);
	
	redo_menuitem = gtk_image_menu_item_new_from_stock (GTK_STOCK_REDO, NULL);
	gtk_widget_set_sensitive (redo_menuitem, priv->can_redo);
	g_signal_connect (redo_menuitem, "activate",
			  G_CALLBACK (entry_redo_activate_cb), lentry);
	gtk_widget_show (redo_menuitem);
	gtk_menu_shell_insert (GTK_MENU_SHELL (menu), redo_menuitem, 1);
	
	separator = gtk_separator_menu_item_new ();
	gtk_widget_show (separator);
	gtk_menu_shell_insert (GTK_MENU_SHELL (menu), separator, 2);
}

static void
each_url_get_data_binder (EphyDragEachSelectedItemDataGet iteratee,
			  gpointer iterator_context,
			  gpointer return_data)
{
	EphyLocationEntry *entry = EPHY_LOCATION_ENTRY (iterator_context);
	char *title = NULL, *address = NULL;

	g_signal_emit (entry, signals[GET_LOCATION], 0, &address);
	g_signal_emit (entry, signals[GET_TITLE], 0, &title);
	g_return_if_fail (address != NULL && title != NULL);

	iteratee (address, title, return_data);

	g_free (address);
	g_free (title);
}

#define DRAG_ICON_LAYOUT_PADDING	2
#define DRAG_ICON_ICON_PADDING		4
#define DRAG_ICON_MAX_WIDTH_CHARS	32

static cairo_surface_t *
favicon_create_drag_surface (EphyLocationEntry *entry,
			     GtkWidget *widget)
{
	EphyLocationEntryPrivate *priv = entry->priv;
	char *title = NULL, *address = NULL;
	GString *text;
	GtkStyle *style;
	cairo_surface_t *surface;
	PangoContext *context;
	PangoLayout  *layout;
	PangoFontMetrics *metrics;
	int surface_height, surface_width;
	int layout_width, layout_height;
	int icon_width = 0, icon_height = 0, favicon_offset_x = 0;
	int char_width;
	cairo_t *cr;
	GtkStateType state;

	state = gtk_widget_get_state (widget);

	g_signal_emit (entry, signals[GET_LOCATION], 0, &address);
	g_signal_emit (entry, signals[GET_TITLE], 0, &title);
	if (address == NULL || title == NULL) return NULL;

	/* Compute text */
	title = g_strstrip (title);

	text = g_string_sized_new (strlen (address) + strlen (title) + 2);
	if (title[0] != '\0')
	{
		g_string_append (text, title);
		g_string_append (text, "\n");
	}

	if (address[0] != '\0')
	{
		g_string_append (text, address);
	}

	if (priv->favicon != NULL)
	{
		icon_width = gdk_pixbuf_get_width (priv->favicon);
		icon_height = gdk_pixbuf_get_height (priv->favicon);
	}

	context = gtk_widget_get_pango_context (widget);
	layout = pango_layout_new (context);

	style = gtk_widget_get_style (widget);
	metrics = pango_context_get_metrics (context,
					     style->font_desc,
					     pango_context_get_language (context));

	char_width = pango_font_metrics_get_approximate_digit_width (metrics);
	pango_font_metrics_unref (metrics);

	pango_layout_set_ellipsize (layout, PANGO_ELLIPSIZE_END);
	pango_layout_set_width (layout, char_width * DRAG_ICON_MAX_WIDTH_CHARS);
	pango_layout_set_text (layout, text->str, text->len);

	pango_layout_get_pixel_size (layout, &layout_width, &layout_height);

	if (priv->favicon != NULL)
	{
		favicon_offset_x = icon_width + (2 * DRAG_ICON_ICON_PADDING);
	}

	surface_width = layout_width + favicon_offset_x +
			(DRAG_ICON_LAYOUT_PADDING * 2);
	surface_height = MAX (layout_height, icon_height) +
			(DRAG_ICON_LAYOUT_PADDING * 2);

	surface = gdk_window_create_similar_surface (gtk_widget_get_window (widget),
						     CAIRO_CONTENT_COLOR,
						     surface_width + 2,
						     surface_height + 2);
	cr = cairo_create (surface);

	cairo_rectangle (cr, 1, 1, surface_width, surface_height);
	cairo_set_line_width (cr, 1.0);

	cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
	cairo_stroke_preserve (cr);

	gdk_cairo_set_source_color (cr, &style->bg[state]);
	cairo_fill (cr);

	if (priv->favicon != NULL)
	{
		double x;
		double y;

		x = 1 + DRAG_ICON_LAYOUT_PADDING + DRAG_ICON_ICON_PADDING;
		y = 1 + DRAG_ICON_LAYOUT_PADDING + (surface_height - icon_height) / 2;
		gdk_cairo_set_source_pixbuf (cr, priv->favicon, x, y);
		cairo_rectangle (cr, x, y, icon_width, icon_height);
		cairo_fill (cr);
	}

	cairo_move_to (cr,
		       1 + DRAG_ICON_LAYOUT_PADDING + favicon_offset_x,
		       1 + DRAG_ICON_LAYOUT_PADDING);
	gdk_cairo_set_source_color (cr, &style->text[state]);
	pango_cairo_show_layout (cr, layout);

	cairo_destroy (cr);
	g_object_unref (layout);

	g_free (address);
	g_free (title);
	g_string_free (text, TRUE);

	return surface;
}

static void
favicon_drag_begin_cb (GtkWidget *widget,
		       GdkDragContext *context,
		       EphyLocationEntry *lentry)
{
	cairo_surface_t *surface;
	GtkEntry *entry;
	gint index;

	entry = GTK_ENTRY (widget);
	
	index = gtk_entry_get_current_icon_drag_source (entry);
	if (index != GTK_ENTRY_ICON_PRIMARY)
		return;

	surface = favicon_create_drag_surface (lentry, widget);

	if (surface != NULL)
	{
		gtk_drag_set_icon_surface (context, surface);
		cairo_surface_destroy (surface);
	}
}

static void
favicon_drag_data_get_cb (GtkWidget *widget,
			  GdkDragContext *context,
			  GtkSelectionData *selection_data,
			  guint info,
			  guint32 time,
			  EphyLocationEntry *lentry)
{
	gint index;
	GtkEntry *entry;

	g_assert (widget != NULL);
	g_return_if_fail (context != NULL);

	entry = GTK_ENTRY (widget);

	index = gtk_entry_get_current_icon_drag_source (entry);
	if (index == GTK_ENTRY_ICON_PRIMARY)
	{
		ephy_dnd_drag_data_get (widget, context, selection_data,
					time, lentry, each_url_get_data_binder);
	}
}

static gboolean
icon_button_press_event_cb (GtkWidget *entry,
			    GtkEntryIconPosition position,
			    GdkEventButton *event,
			    EphyLocationEntry *lentry)
{
	guint state = event->state & gtk_accelerator_get_default_mod_mask ();

	if (event->type == GDK_BUTTON_PRESS && 
	    event->button == 1 &&
	    state == 0 /* left */)
	{
		if (position == GTK_ENTRY_ICON_PRIMARY)
		{
			GtkWidget *toplevel;
		
			toplevel = gtk_widget_get_toplevel (GTK_WIDGET (entry));
			gtk_window_set_focus (GTK_WINDOW (toplevel), entry);

			gtk_editable_select_region (GTK_EDITABLE (entry), 
						    0, -1);
		}
		else
		{
			g_signal_emit (lentry, signals[LOCK_CLICKED], 0);
		}

		return TRUE;
	}

	return FALSE;
}

static void
ephy_location_entry_construct_contents (EphyLocationEntry *lentry)
{
	EphyLocationEntryPrivate *priv = lentry->priv;
	GtkTargetList *targetlist;

	LOG ("EphyLocationEntry constructing contents %p", lentry);

	priv->entry = gtk_entry_new ();
	gtk_container_add (GTK_CONTAINER (lentry), priv->entry);

	/* Favicon */
	targetlist = gtk_target_list_new (url_drag_types,
					  G_N_ELEMENTS (url_drag_types));
	gtk_entry_set_icon_drag_source (GTK_ENTRY (priv->entry),
					GTK_ENTRY_ICON_PRIMARY,
					targetlist,
					GDK_ACTION_ASK | GDK_ACTION_COPY | GDK_ACTION_LINK);
	gtk_target_list_unref (targetlist);

	gtk_entry_set_icon_tooltip_text (GTK_ENTRY (priv->entry),
					 GTK_ENTRY_ICON_PRIMARY,
					 _("Drag and drop this icon to create a link to this page"));

	gtk_entry_set_icon_activatable (GTK_ENTRY (priv->entry),
					GTK_ENTRY_ICON_PRIMARY,
					TRUE);
	gtk_entry_set_icon_from_stock (GTK_ENTRY (priv->entry),
				       GTK_ENTRY_ICON_SECONDARY,
				       STOCK_LOCK_INSECURE);
	gtk_entry_set_icon_activatable (GTK_ENTRY (priv->entry),
					GTK_ENTRY_ICON_SECONDARY,
					TRUE);

	g_object_connect (priv->entry,
			  "signal::icon-press", G_CALLBACK (icon_button_press_event_cb), lentry,
			  "signal::populate-popup", G_CALLBACK (entry_populate_popup_cb), lentry,
			  "signal::key-press-event", G_CALLBACK (entry_key_press_cb), lentry,
			  "signal::changed", G_CALLBACK (editable_changed_cb), lentry,
			  "signal::drag-motion", G_CALLBACK (entry_drag_motion_cb), lentry,
			  "signal::drag-drop", G_CALLBACK (entry_drag_drop_cb), lentry,
			  "signal::drag-data-get", G_CALLBACK (favicon_drag_data_get_cb), lentry,
			  NULL);
			  
	g_signal_connect_after (priv->entry, "key-press-event",
				G_CALLBACK (entry_key_press_after_cb), lentry);
	g_signal_connect_after (priv->entry, "activate",
				G_CALLBACK (entry_activate_after_cb), lentry);
	g_signal_connect_after (priv->entry, "drag-begin",
				G_CALLBACK (favicon_drag_begin_cb), lentry);

	gtk_widget_show (priv->entry);
}

static void
ephy_location_entry_init (EphyLocationEntry *le)
{
	EphyLocationEntryPrivate *p;

	LOG ("EphyLocationEntry initialising %p", le);

	p = EPHY_LOCATION_ENTRY_GET_PRIVATE (le);
	le->priv = p;

	p->user_changed = FALSE;
	p->block_update = FALSE;
	p->saved_text = NULL;
	p->search_terms = NULL;
	p->show_lock = FALSE;
	
	ephy_location_entry_construct_contents (le);

	gtk_tool_item_set_expand (GTK_TOOL_ITEM (le), TRUE);
}

GtkWidget *
ephy_location_entry_new (void)
{
	return GTK_WIDGET (g_object_new (EPHY_TYPE_LOCATION_ENTRY, NULL));
}

static gboolean
cursor_on_match_cb  (GtkEntryCompletion *completion,
		     GtkTreeModel *model,
		     GtkTreeIter *iter,
		     EphyLocationEntry *le)
{
	char *item = NULL;
	GtkWidget *entry;

	gtk_tree_model_get (model, iter,
			    le->priv->url_col,
			    &item, -1);
	entry = gtk_entry_completion_get_entry (completion);

	/* Prevent the update so we keep the highlight from our input.
	 * See textcell_data_func().
	 */
	le->priv->block_update = TRUE;
	gtk_entry_set_text (GTK_ENTRY (entry), item);
	gtk_editable_set_position (GTK_EDITABLE (entry), -1);
	le->priv->block_update = FALSE;

	g_free (item);

	return TRUE;
}

static void
textcell_data_func (GtkCellLayout *cell_layout,
			GtkCellRenderer *cell,
			GtkTreeModel *tree_model,
			GtkTreeIter *iter,
			gpointer data)
{
	EphyLocationEntryPrivate *priv;
	PangoAttrList *list;
	PangoAttribute *att;
	GMatchInfo *match;

	int start, end;

	char *ctext;
	char *title;
	char *url;

	GtkStyle *style;
	GdkColor color;

	GValue text = { 0, };

	priv = EPHY_LOCATION_ENTRY (data)->priv;
	gtk_tree_model_get (tree_model, iter,
			priv->text_col, &title,
			priv->url_col, &url,
			-1);

	list = pango_attr_list_new ();

	if (url)
	{
		ctext = g_strdup_printf ("%s\n%s", title, url);

		style = gtk_widget_get_style (priv->entry);
		color = style->text[GTK_STATE_INSENSITIVE];

		att = pango_attr_foreground_new
			(color.red, color.green, color.blue);
		att->start_index = strlen (title)+1;

		pango_attr_list_insert (list, att);
	        g_free (title);
	}
	else
	{
		ctext = title;
	}

	if (priv->search_terms)
	{
		GSList *iter;
		GRegex *regex;

		for (iter = priv->search_terms; iter != NULL; iter = iter->next)
		{
			regex = (GRegex*) iter->data;
			g_regex_match (regex, ctext, G_REGEX_MATCH_NOTEMPTY, &match);

			while (g_match_info_matches (match))
			{
				g_match_info_fetch_pos (match, 0, &start, &end);

				att = pango_attr_weight_new (PANGO_WEIGHT_BOLD);
				att->start_index = start;
				att->end_index = end;

				pango_attr_list_insert (list, att);
				g_match_info_next (match, NULL);
			}

			g_match_info_free (match);
			match = NULL;
		}

	}

	g_object_set (cell,
		      "attributes", list,
		      NULL);

	g_value_init (&text, G_TYPE_STRING);
	g_value_take_string (&text, ctext);
	g_object_set_property (G_OBJECT (cell), "text", &text);
	g_value_unset (&text);

	pango_attr_list_unref (list);

	g_free (url);
}

static void
extracell_data_func (GtkCellLayout *cell_layout,
			GtkCellRenderer *cell,
			GtkTreeModel *tree_model,
			GtkTreeIter *iter,
			gpointer data)
{
	EphyLocationEntryPrivate *priv;
	gboolean is_bookmark = FALSE;
	GValue visible = { 0, };

	priv = EPHY_LOCATION_ENTRY (data)->priv;
	gtk_tree_model_get (tree_model, iter,
			    priv->extra_col, &is_bookmark,
			    -1);

	if (is_bookmark)
		g_object_set (cell,
			      "stock-id", EPHY_STOCK_BOOKMARK,
			      NULL);

	g_value_init (&visible, G_TYPE_BOOLEAN);
	g_value_set_boolean (&visible, is_bookmark);
	g_object_set_property (G_OBJECT (cell), "visible", &visible);
	g_value_unset (&visible);
}

/**
 * ephy_location_entry_set_match_func:
 * @entry: an #EphyLocationEntry widget
 * @match_func: a #GtkEntryCompletionMatchFunc
 * @user_data: user_data to pass to the @match_func
 * @notify: a #GDestroyNotify, like the one given to
 * gtk_entry_completion_set_match_func
 *
 * Sets the match_func for the internal #GtkEntryCompletion to @match_func.
 *
 **/
void
ephy_location_entry_set_match_func (EphyLocationEntry *entry, 
				GtkEntryCompletionMatchFunc match_func,
				gpointer user_data,
				GDestroyNotify notify)
{
	EphyLocationEntryPrivate *priv = entry->priv;
	GtkEntryCompletion *completion;
	
	completion = gtk_entry_get_completion (GTK_ENTRY (priv->entry));
	gtk_entry_completion_set_match_func (completion, match_func, user_data, notify);
}

/**
 * ephy_location_entry_set_completion:
 * @entry: an #EphyLocationEntry widget
 * @model: the #GtkModel for the completion
 * @text_col: column id to access #GtkModel relevant data
 * @action_col: column id to access #GtkModel relevant data
 * @keywords_col: column id to access #GtkModel relevant data
 * @relevance_col: column id to access #GtkModel relevant data
 * @url_col: column id to access #GtkModel relevant data
 * @extra_col: column id to access #GtkModel relevant data
 * @favicon_col: column id to access #GtkModel relevant data
 *
 * Initializes @entry to have a #GtkEntryCompletion using @model as the
 * internal #GtkModel. The *_col arguments are for internal data retrieval from
 * @model, like when setting the text property of one of the #GtkCellRenderer
 * of the completion.
 *
 **/
void
ephy_location_entry_set_completion (EphyLocationEntry *entry,
				    GtkTreeModel *model,
				    guint text_col,
				    guint action_col,
				    guint keywords_col,
				    guint relevance_col,
				    guint url_col,
				    guint extra_col,
				    guint favicon_col)
{
	EphyLocationEntryPrivate *priv = entry->priv;
	GtkTreeModel *sort_model;
	GtkEntryCompletion *completion;
	GtkCellRenderer *cell;

	entry->priv->text_col = text_col;
	entry->priv->action_col = action_col;
	entry->priv->keywords_col = keywords_col;
	entry->priv->relevance_col = relevance_col;
	entry->priv->url_col = url_col;
	entry->priv->extra_col = extra_col;
	entry->priv->favicon_col = favicon_col;

	sort_model = gtk_tree_model_sort_new_with_model (model);

	gtk_tree_sortable_set_sort_column_id 
			(GTK_TREE_SORTABLE (sort_model),
			 entry->priv->relevance_col,
			 GTK_SORT_DESCENDING);

	completion = gtk_entry_completion_new ();
	gtk_entry_completion_set_model (completion, sort_model);
	g_object_unref (sort_model);
	g_signal_connect (completion, "match-selected",
			  G_CALLBACK (match_selected_cb), entry);
	g_signal_connect_after (completion, "action-activated",
				G_CALLBACK (action_activated_after_cb), entry);

	cell = gtk_cell_renderer_pixbuf_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (completion),
				    cell, FALSE);
	gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (completion),
				       cell, "pixbuf", favicon_col);

	cell = gtk_cell_renderer_text_new ();
	g_object_set (cell,
		      "ellipsize", PANGO_ELLIPSIZE_END,
		      "ellipsize-set", TRUE,
		      NULL);
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (completion),
				    cell, TRUE);
	gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (completion),
				       cell, "text", text_col);
        /*
         * As the width of the entry completion is known in advance 
         * (as big as the entry you are completing on), we can set 
         * any fixed width (the 1 is just this random number here). 
         * Since the height is known too, we avoid computing the actual 
         * sizes of the cells, which takes a lot of CPU time and does
         * not get used anyway.
         */
	gtk_cell_renderer_set_fixed_size (cell, 1, -1);
	gtk_cell_renderer_text_set_fixed_height_from_font (GTK_CELL_RENDERER_TEXT (cell), 2);

	gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (completion),
					cell, textcell_data_func,
					entry,
					NULL);

	cell = gtk_cell_renderer_pixbuf_new ();
	gtk_cell_layout_pack_end (GTK_CELL_LAYOUT (completion),
				  cell, FALSE);
	gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (completion),
					    cell, extracell_data_func,
					    entry,
					    NULL);

	g_object_set (completion, "inline-selection", TRUE, NULL);
	g_signal_connect (completion, "cursor-on-match",
			  G_CALLBACK (cursor_on_match_cb), entry);

	gtk_entry_set_completion (GTK_ENTRY (priv->entry), completion);
	g_object_unref (completion);
}

/**
 * ephy_location_entry_set_location:
 * @entry: an #EphyLocationEntry widget
 * @address: new location address
 *
 * Sets the current address of @entry to @address.
 **/
void
ephy_location_entry_set_location (EphyLocationEntry *entry,
				  const char *address)
{
	EphyLocationEntryPrivate *priv = entry->priv;
	GtkClipboard *clipboard;
	const char *text;
	char* selection = NULL;
	int start, end;

	/* Setting a new text will clear the clipboard. This makes it impossible
	 * to copy&paste from the location entry of one tab into another tab, see
	 * bug #155824. So we save the selection iff the clipboard was owned by
	 * the location entry.
	 */
	if (gtk_widget_get_realized (GTK_WIDGET (priv->entry)))
	{
		clipboard = gtk_widget_get_clipboard (priv->entry,
						      GDK_SELECTION_PRIMARY);
		g_return_if_fail (clipboard != NULL);

		if (gtk_clipboard_get_owner (clipboard) == G_OBJECT (priv->entry) &&
		    gtk_editable_get_selection_bounds (GTK_EDITABLE (priv->entry),
	     					       &start, &end))
		{
			selection = gtk_editable_get_chars (GTK_EDITABLE (priv->entry),
							    start, end);
		}
	}

	if (address != NULL && strcmp (address, "about:blank") != 0)
	{
		text = address;
	}
	else
	{
		text = "";
	}

	/* First record the new hash, then update the entry text */
	priv->hash = g_str_hash (text);

	priv->block_update = TRUE;
	gtk_entry_set_text (GTK_ENTRY (priv->entry), text);
	priv->block_update = FALSE;

	/* We need to call update_address_state() here, as the 'changed' signal
	 * may not get called if the user has typed in the exact correct url */
	update_address_state (entry);
	update_favicon (entry);

	/* Now restore the selection.
	 * Note that it's not owned by the entry anymore!
	 */
	if (selection != NULL)
	{
		gtk_clipboard_set_text (gtk_clipboard_get (GDK_SELECTION_PRIMARY),
					selection, strlen (selection));
		g_free (selection);
	}
}

/**
 * ephy_location_entry_get_can_undo:
 * @entry: an #EphyLocationEntry widget
 *
 * Wheter @entry can restore the displayed user modified text to the unmodified 
 * previous text.
 *
 * Return value: TRUE or FALSE indicating if the text can be restored
 *
 **/
gboolean
ephy_location_entry_get_can_undo (EphyLocationEntry *entry)
{
	EphyLocationEntryPrivate *priv = entry->priv;
	
	return priv->user_changed;
}

/**
 * ephy_location_entry_get_can_redo:
 * @entry: an #EphyLocationEntry widget
 *
 * Wheter @entry can restore the displayed text to the user modified version
 * before the undo.
 *
 * Return value: TRUE or FALSE indicating if the text can be restored
 *
 **/
gboolean
ephy_location_entry_get_can_redo (EphyLocationEntry *entry)
{
	EphyLocationEntryPrivate *priv = entry->priv;
	
	return priv->can_redo;
}

/**
 * ephy_location_entry_get_location:
 * @entry: an #EphyLocationEntry widget
 *
 * Retrieves the text displayed by the internal #GtkEntry of @entry. This is
 * the currently displayed text, like in any #GtkEntry.
 *
 * Return value: the text inside the inner #GtkEntry of @entry, owned by GTK+
 *
 **/
const char *
ephy_location_entry_get_location (EphyLocationEntry *entry)
{
	EphyLocationEntryPrivate *priv = entry->priv;

	return gtk_entry_get_text (GTK_ENTRY (priv->entry));
}

static gboolean
ephy_location_entry_reset_internal (EphyLocationEntry *entry,
				    gboolean notify)
{
	EphyLocationEntryPrivate *priv = entry->priv;
	const char *text, *old_text;
	char *url = NULL;
	gboolean retval;

	g_signal_emit (entry, signals[GET_LOCATION], 0, &url);
	text = url != NULL ? url : "";
	old_text = gtk_entry_get_text (GTK_ENTRY (priv->entry));
	old_text = old_text != NULL ? old_text : "";

	g_free (priv->saved_text);
	priv->saved_text = g_strdup (old_text);
	priv->can_redo = TRUE;

	retval = g_str_hash (text) != g_str_hash (old_text);

	ephy_location_entry_set_location (entry, text);
	g_free (url);

	if (notify)
	{
		g_signal_emit (entry, signals[USER_CHANGED], 0);
	}
	
	priv->user_changed = FALSE;

	return retval;
}

/**
 * ephy_location_entry_undo_reset:
 * @entry: an #EphyLocationEntry widget
 *
 * Undo a previous ephy_location_entry_reset.
 *
 **/
void
ephy_location_entry_undo_reset (EphyLocationEntry *entry)
{
	EphyLocationEntryPrivate *priv = entry->priv;
	
	gtk_entry_set_text (GTK_ENTRY (priv->entry), priv->saved_text);
	priv->can_redo = FALSE;
	priv->user_changed = TRUE;
}

/**
 * ephy_location_entry_reset:
 * @entry: an #EphyLocationEntry widget
 *
 * Restore the @entry to the text corresponding to the current location, this
 * does not fire the user_changed signal. This is called each time the user
 * presses Escape while the location entry is selected.
 *
 * Return value: TRUE on success, FALSE otherwise
 *
 **/
gboolean
ephy_location_entry_reset (EphyLocationEntry *entry)
{
	return ephy_location_entry_reset_internal (entry, FALSE);
}

/**
 * ephy_location_entry_activate:
 * @entry: an #EphyLocationEntry widget
 *
 * Set focus on @entry and select the text whithin. This is called when the
 * user hits Control+L.
 *
 **/
void
ephy_location_entry_activate (EphyLocationEntry *entry)
{
	EphyLocationEntryPrivate *priv = entry->priv;
	GtkWidget *toplevel;

	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (entry));

	gtk_editable_select_region (GTK_EDITABLE (priv->entry),
				    0, -1);
	gtk_window_set_focus (GTK_WINDOW (toplevel),
			      priv->entry);
}

/**
 * ephy_location_entry_get_entry:
 * @entry: an #EphyLocationEntry widget
 *
 * Retrieve the internal #GtkEntry of the #EphyIconEntry inside @entry.
 *
 * Return value: a pointer to the internal #GtkEntry of @entry
 *
 **/
GtkWidget *
ephy_location_entry_get_entry (EphyLocationEntry *entry)
{
	EphyLocationEntryPrivate *priv = entry->priv;

	return priv->entry;
}

/**
 * ephy_location_entry_set_favicon:
 * @entry: an #EphyLocationEntry widget
 * @pixbuf: a #GdkPixbuf to be set as the icon of the entry
 *
 * Sets the icon in the internal #EphyIconEntry of @entry
 *
 **/
void
ephy_location_entry_set_favicon (EphyLocationEntry *entry,
				 GdkPixbuf *pixbuf)
{
	EphyLocationEntryPrivate *priv = entry->priv;

	if (priv->favicon != NULL)
	{
		g_object_unref (priv->favicon);
	}

	priv->favicon = pixbuf ? g_object_ref (pixbuf) : NULL;

	update_favicon (entry);
}

/**
 * ephy_location_entry_set_secure:
 * @entry: an #EphyLocationEntry widget
 * @secure: whether the page is secure and thus the location bar should reflect
 * that
 *
 * Set @entry to give visual feedback if the page is @secure. If it is, the
 * location bar will have its background painted differently (yellow by
 * default).
 *
 **/
void
ephy_location_entry_set_secure (EphyLocationEntry *entry,
				gboolean secure)
{
	EphyLocationEntryPrivate *priv = entry->priv;
	GtkWidget *widget = GTK_WIDGET (entry);
	GtkWidget *gentry = priv->entry;

	priv->secure = secure;

	/* We have to set the colour of the GtkEntry in the EphyIconEntry */
	if (priv->secure && priv->apply_colours)
	{
		gtk_widget_modify_text (gentry, GTK_STATE_NORMAL, &priv->secure_fg_colour);
		gtk_widget_modify_base (gentry, GTK_STATE_NORMAL, &priv->secure_bg_colour);
	}
	else
	{
		gtk_widget_modify_text (gentry, GTK_STATE_NORMAL, NULL);
		gtk_widget_modify_base (gentry, GTK_STATE_NORMAL, NULL);
	}

	gtk_widget_queue_draw (widget);
}

/**
 * ephy_location_entry_set_show_lock:
 * @entry: an #EphyLocationEntry widget
 * @show_lock: if @entry should show a lock icon indicating the security level
 * of the page
 *
 * If @show_lock is TRUE, the location bar will show an icon reflecting the
 * security level of the page, by default it's shown only in secure and
 * insecure pages (insecure meaning secure pages with something broken in such
 * security)
 *
 **/
void
ephy_location_entry_set_show_lock (EphyLocationEntry *entry,
				   gboolean show_lock)
{
	EphyLocationEntryPrivate *priv;

	g_return_if_fail (EPHY_IS_LOCATION_ENTRY (entry));

	priv = entry->priv;

	priv->show_lock = show_lock != FALSE;

	gtk_entry_set_icon_from_stock (GTK_ENTRY (priv->entry),
				       GTK_ENTRY_ICON_SECONDARY,
				       show_lock ? priv->lock_stock_id : NULL);
}

/**
 * ephy_location_entry_set_lock_stock:
 * @entry: an #EphyLocationEntry widget
 * @stock_id: a stock_id from GTK+ stock icons
 *
 * Set the lock icon to be displayed, to actually show the icon see 
 * ephy_location_entry_set_show_lock.
 *
 **/
void
ephy_location_entry_set_lock_stock (EphyLocationEntry *entry,
				    const char *stock_id)

{
	g_return_if_fail (EPHY_IS_LOCATION_ENTRY (entry));

	g_free (entry->priv->lock_stock_id);
	entry->priv->lock_stock_id = g_strdup (stock_id);

	if (entry->priv->show_lock)
		gtk_entry_set_icon_from_stock (GTK_ENTRY (entry->priv->entry),
					       GTK_ENTRY_ICON_SECONDARY,
					       stock_id);
}

/**
 * ephy_location_entry_set_lock_tooltip:
 * @entry: an #EphyLocationEntry widget
 * @tooltip: the text to be set in the tooltip for the lock icon
 *
 * Set the text to be displayed when hovering the lock icon of @entry.
 *
 **/
void
ephy_location_entry_set_lock_tooltip (EphyLocationEntry *entry,
				      const char *tooltip)
{
	EphyLocationEntryPrivate *priv = entry->priv;

	gtk_entry_set_icon_tooltip_text (GTK_ENTRY (priv->entry),
					 GTK_ENTRY_ICON_SECONDARY,
					 tooltip);
}

/**
 * ephy_location_entry_get_search_terms:
 * @entry: an #EphyLocationEntry widget
 *
 * Return the internal #GSList containing the search terms as #GRegex
 * instances, formed in @entry on user changes.
 *
 * Return value: the internal #GSList
 *
 **/
GSList *
ephy_location_entry_get_search_terms (EphyLocationEntry *entry)
{
	EphyLocationEntryPrivate *priv = entry->priv;
	
	return priv->search_terms;
}
