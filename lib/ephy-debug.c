/*
 *  Copyright (C) 2003 Marco Pesenti Gritti
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

#include "ephy-debug.h"

#ifndef DISABLE_PROFILING

#include <glib/gbacktrace.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <execinfo.h>

static GHashTable *ephy_profilers_hash = NULL;
static const char *ephy_profile_modules = NULL;
static const char *ephy_debug_break = NULL;

#endif

#ifndef DISABLE_LOGGING

static const char *ephy_log_modules;

static void
log_module (const gchar *log_domain,
	    GLogLevelFlags log_level,
	    const gchar *message,
	    gpointer user_data)
{
	gboolean should_log = FALSE;

	if (!ephy_log_modules) return;

	if (strcmp (ephy_log_modules, "all") != 0)
	{
		char **modules;
		int i;

		modules = g_strsplit (ephy_log_modules, ":", 100);

		for (i = 0; modules[i] != NULL; i++)
		{
			if (strstr (message, modules [i]) != NULL)
			{
				should_log = TRUE;
				break;
			}
		}

		g_strfreev (modules);
	}
	else
	{
		should_log = TRUE;
	}

	if (should_log)
	{
		g_print ("%s\n", message);
	}
}

#define MAX_DEPTH 200

static void 
trap_handler (const char *log_domain,
	      GLogLevelFlags log_level,
	      const char *message,
	      gpointer user_data)
{
	g_log_default_handler (log_domain, log_level, message, user_data);

	if (ephy_debug_break != NULL &&
	    (log_level & (G_LOG_LEVEL_WARNING |
			  G_LOG_LEVEL_ERROR |
			  G_LOG_LEVEL_CRITICAL |
			  G_LOG_FLAG_FATAL)))
	{
		if (strcmp (ephy_debug_break, "stack") == 0)
		{
			void *array[MAX_DEPTH];
			size_t size;
			
			size = backtrace (array, MAX_DEPTH);
			backtrace_symbols_fd (array, size, 2);
		}
		else if (strcmp (ephy_debug_break, "trap") == 0)
		{
			G_BREAKPOINT ();
		}
		else if (strcmp (ephy_debug_break, "suspend") == 0)
		{
			g_print ("Suspending program; attach with the debugger.\n");

			raise (SIGSTOP);
		}
		else if (strcmp (ephy_debug_break, "abort") == 0)
		{
			raise (SIGABRT);
		}
	}
}

#endif

void
ephy_debug_init (void)
{
#ifndef DISABLE_LOGGING
	ephy_log_modules = g_getenv ("EPHY_LOG_MODULES");
	ephy_debug_break = g_getenv ("EPHY_DEBUG_BREAK");

	g_log_set_default_handler (trap_handler, NULL);

	g_log_set_handler (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, log_module, NULL);

#endif
#ifndef DISABLE_PROFILING
	ephy_profile_modules = g_getenv ("EPHY_PROFILE_MODULES");
#endif
}

#ifndef DISABLE_PROFILING

static EphyProfiler *
ephy_profiler_new (const char *name, const char *module)
{
	EphyProfiler *profiler;

	profiler = g_new0 (EphyProfiler, 1);
	profiler->timer = g_timer_new ();
	profiler->name  = g_strdup (name);
	profiler->module  = g_strdup (module);

	g_timer_start (profiler->timer);

	return profiler;
}

static gboolean
ephy_should_profile (const char *module)
{
	char **modules;
	int i;
	gboolean res = FALSE;

	if (!ephy_profile_modules) return FALSE;
	if (strcmp (ephy_profile_modules, "all") == 0) return TRUE;

	modules = g_strsplit (ephy_profile_modules, ":", 100);

	for (i = 0; modules[i] != NULL; i++)
	{
		if (strcmp (module, modules [i]) == 0)
		{
			res = TRUE;
			break;
		}
	}

	g_strfreev (modules);

	return res;
}

static void
ephy_profiler_dump (EphyProfiler *profiler)
{
	double seconds;

	g_return_if_fail (profiler != NULL);

	seconds = g_timer_elapsed (profiler->timer, NULL);

	g_print ("[ %s ] %s %f s elapsed\n",
		 profiler->module, profiler->name,
		 seconds);
}

static void
ephy_profiler_free (EphyProfiler *profiler)
{
	g_return_if_fail (profiler != NULL);

	g_timer_destroy (profiler->timer);
	g_free (profiler->name);
	g_free (profiler->module);
	g_free (profiler);
}

void
ephy_profiler_start (const char *name, const char *module)
{
	EphyProfiler *profiler;

	if (ephy_profilers_hash == NULL)
	{
		ephy_profilers_hash =
			g_hash_table_new_full (g_str_hash, g_str_equal,
					       g_free, NULL);
	}

	if (!ephy_should_profile (module)) return;

	profiler = ephy_profiler_new (name, module);

	g_hash_table_insert (ephy_profilers_hash, g_strdup (name), profiler);
}

void
ephy_profiler_stop (const char *name)
{
	EphyProfiler *profiler;

	profiler = g_hash_table_lookup (ephy_profilers_hash, name);
	if (profiler == NULL) return;
	g_hash_table_remove (ephy_profilers_hash, name);

	ephy_profiler_dump (profiler);
	ephy_profiler_free (profiler);
}

#endif
