/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2; -*- */
/*
 *  Copyright © 2007 Xan Lopez
 *  Copyright © 2009 Igalia S.L.
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

#ifndef EPHY_EMBED_H
#define EPHY_EMBED_H

#include <glib-object.h>
#include <gtk/gtk.h>

#include "ephy-web-view.h"

G_BEGIN_DECLS

#define EPHY_TYPE_EMBED               (ephy_embed_get_type ())
#define EPHY_EMBED(o)                 (G_TYPE_CHECK_INSTANCE_CAST ((o), EPHY_TYPE_EMBED, EphyEmbed))
#define EPHY_EMBED_CLASS(k)           (G_TYPE_CHECK_CLASS_CAST ((k), EPHY_TYPE_EMBED, EphyEmbedClass))
#define EPHY_IS_EMBED(o)              (G_TYPE_CHECK_INSTANCE_TYPE ((o), EPHY_TYPE_EMBED))
#define EPHY_IS_EMBED_CLASS(k)        (G_TYPE_CHECK_CLASS_TYPE ((k), EPHY_TYPE_EMBED))
#define EPHY_EMBED_GET_CLASS(o)       (G_TYPE_INSTANCE_GET_CLASS ((o), EPHY_TYPE_EMBED, EphyEmbedClass))

typedef struct _EphyEmbedClass EphyEmbedClass;
typedef struct _EphyEmbed EphyEmbed;
typedef struct _EphyEmbedPrivate EphyEmbedPrivate;

#define EPHY_EMBED_STATUSBAR_TAB_MESSAGE_CONTEXT_DESCRIPTION "tab_message"
#define EPHY_EMBED_STATUSBAR_HELP_MESSAGE_CONTEXT_DESCRIPTION "help_message"

struct _EphyEmbed {
  GtkBox parent_instance;

  /*< private >*/
  EphyEmbedPrivate *priv;
};

struct _EphyEmbedClass {
  GtkBoxClass parent_class;
};

GType        ephy_embed_get_type                 (void);
EphyWebView* ephy_embed_get_web_view             (EphyEmbed  *embed);
void         ephy_embed_add_top_widget           (EphyEmbed  *embed,
                                                  GtkWidget  *widget,
                                                  gboolean    destroy_on_transition);
void         ephy_embed_remove_top_widget        (EphyEmbed  *embed,
                                                  GtkWidget  *widget);
void         ephy_embed_auto_download_url        (EphyEmbed  *embed,
                                                  const char *url);
void         _ephy_embed_set_statusbar_label     (EphyEmbed  *embed,
                                                  const char *label);
void         ephy_embed_statusbar_pop            (EphyEmbed  *embed,
                                                  guint       context_id);
guint        ephy_embed_statusbar_push           (EphyEmbed  *embed,
                                                  guint       context_id,
                                                  const char *text);
guint        ephy_embed_statusbar_get_context_id (EphyEmbed  *embed,
                                                  const char *context_description);

G_END_DECLS

#endif
