/*
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

#include "ephy-shlib-loader.h"
#include "ephy-loader.h"
#include "ephy-module.h"
#include "ephy-debug.h"

#include <string.h>

#define DATA_KEY "EphyShlibLoader::LoaderData"

typedef struct
{
	EphyModule *module;
	GObject *object;
} LoaderData;

static GQuark library_quark = 0;

#define EPHY_SHLIB_LOADER_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE ((object), EPHY_TYPE_SHLIB_LOADER, EphyShlibLoaderPrivate))

struct _EphyShlibLoaderPrivate
{
	GSList *data;
};

static void ephy_shlib_loader_class_init (EphyShlibLoaderClass *klass);
static void ephy_shlib_loader_iface_init (EphyLoaderIface *iface);
static void ephy_shlib_loader_init	  (EphyShlibLoader *loader);

static GObjectClass *parent_class = NULL;

GType
ephy_shlib_loader_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0))
	{
		static const GTypeInfo our_info =
		{
			sizeof (EphyShlibLoaderClass),
			NULL, /* base_init */
			NULL, /* base_finalize */
			(GClassInitFunc) ephy_shlib_loader_class_init,
			NULL,
			NULL, /* class_data */
			sizeof (EphyShlibLoader),
			0, /* n_preallocs */
			(GInstanceInitFunc) ephy_shlib_loader_init
		};

		static const GInterfaceInfo loader_info =
		{
			(GInterfaceInitFunc) ephy_shlib_loader_iface_init,
			NULL,
			NULL
		};

		type = g_type_register_static (G_TYPE_OBJECT,
					       "EphyShlibLoader",
					       &our_info, 0);

		g_type_add_interface_static (type,
					     EPHY_TYPE_LOADER,
					     &loader_info);
	}

	return type;
}

static void
free_loader_data (LoaderData *data)
{
	g_return_if_fail (data != NULL);

	/* data->module must NOT be unreffed! */

	if (data->object != NULL)
	{
		g_object_unref (data->object);
	}

	g_free (data);
}

static void
ephy_shlib_loader_init (EphyShlibLoader *loader)
{
	loader->priv = EPHY_SHLIB_LOADER_GET_PRIVATE (loader);

	LOG ("EphyShlibLoader initialising")
}

static void
ephy_shlib_loader_finalize (GObject *object)
{
	EphyShlibLoader *loader = EPHY_SHLIB_LOADER (object);

	LOG ("EphyShlibLoader finalising")

	g_slist_foreach (loader->priv->data, (GFunc) free_loader_data, NULL);
	g_slist_free (loader->priv->data);

	parent_class->finalize (object);
}

static int
find_library (const LoaderData *data,
	      const char *library)
{
	return strcmp (ephy_module_get_path (data->module), library);
}

static int
find_object (const LoaderData *data,
	     const GObject *object)
{
	return data->object != object;
}

static gboolean
idle_unref (GObject *object)
{
	g_object_unref (object);

	return FALSE;
}

static GObject *
impl_get_object (EphyLoader *eloader,
		 GData **attributes)
{
	EphyShlibLoader *loader = EPHY_SHLIB_LOADER (eloader);
	GSList *l;
	LoaderData *data = NULL;
	const char *library;

	library = g_datalist_id_get_data (attributes, library_quark);
	if (library == NULL)
	{
		g_warning ("NULL library name!\n");
		return NULL;
	}

	l = g_slist_find_custom (loader->priv->data, library,
				(GCompareFunc) find_library);

	if (l != NULL)
	{
		data = l->data;
		g_return_val_if_fail (data != NULL, NULL);

		if (data->object != NULL)
		{
			return g_object_ref (data->object);
		}
	}
	else
	{
		data = g_new0 (LoaderData, 1);
		loader->priv->data = g_slist_prepend (loader->priv->data, data);
	}

	if (data->module == NULL)
	{
		data->module = ephy_module_new (library);
	}

	g_return_val_if_fail (data->object == NULL, data->object);

	if (g_type_module_use (G_TYPE_MODULE (data->module)) == FALSE)
	{
		g_warning ("Could not load extension file at %s\n",
			   ephy_module_get_path (data->module));
		return NULL;
	}

	data->object = ephy_module_new_object (data->module);

	g_type_module_unuse (G_TYPE_MODULE (data->module));

	if (data->object != NULL)
	{
		g_object_set_data (G_OBJECT (data->object), DATA_KEY, data);
	}

	return data->object;
}

static void
impl_release_object (EphyLoader *eloader,
		     GObject *object)
{
	EphyShlibLoader *loader = EPHY_SHLIB_LOADER (eloader);
	GSList *l;
	LoaderData *data;

	l = g_slist_find_custom (loader->priv->data, object,
				(GCompareFunc) find_object);
	g_return_if_fail (l != NULL);
	data = l->data;

	/* FIXME: should we consider those extensions broken?
	 * Only unref the extension in the idle loop; if the extension has its
	 * own functions queued in the idle loop, the functions must exist in
	 * memory before being called.
	 */
	g_idle_add ((GSourceFunc) idle_unref, data->object);
	data->object = NULL;
}

static void
ephy_shlib_loader_iface_init (EphyLoaderIface *iface)
{
	iface->type = "shlib";
	iface->get_object = impl_get_object;
	iface->release_object = impl_release_object;
}

static void
ephy_shlib_loader_class_init (EphyShlibLoaderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = ephy_shlib_loader_finalize;

	g_type_class_add_private (object_class, sizeof (EphyShlibLoaderPrivate));

	library_quark = g_quark_from_string ("library");
}
