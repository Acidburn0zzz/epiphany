/* vim: set sw=2 ts=2 sts=2 et: */
/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * ephy-search-entry-test.c
 * This file is part of Epiphany
 *
 * Copyright © 2008 - Diego Escalante Urrelo
 *
 * Epiphany is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Epiphany is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Epiphany; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301  USA
 */

#include "config.h"
#include "ephy-search-entry.h"
#include <glib.h>
#include <gtk/gtk.h>

static void
test_entry_new (void)
{

  EphySearchEntry *entry;
  entry = EPHY_SEARCH_ENTRY (ephy_search_entry_new ());

  g_assert (GTK_IS_WIDGET (entry));
  g_assert (GTK_IS_ENTRY (entry));
}

static void
test_entry_clear (void)
{
  const char *set = "test";
  const char *get = NULL;

  EphySearchEntry *entry;
  entry = EPHY_SEARCH_ENTRY (ephy_search_entry_new ());

  gtk_entry_set_text (GTK_ENTRY (entry), set);
  get = gtk_entry_get_text (GTK_ENTRY (entry));

  g_assert_cmpstr (set, ==, get);

  /* At this point, the text in the entry is either 'vanilla' or the
   * contents of 'set' char*
   */
  ephy_search_entry_clear (EPHY_SEARCH_ENTRY (entry));
  get = gtk_entry_get_text (GTK_ENTRY (entry));

  g_assert_cmpstr ("", ==, get);
}

int
main (int argc, char *argv[])
{
  gtk_test_init (&argc, &argv);

  g_test_add_func ("/lib/widgets/ephy-search-entry/new",
                   test_entry_new);
  g_test_add_func ("/lib/widgets/ephy-search-entry/clear",
                   test_entry_clear);

  return g_test_run ();
}
