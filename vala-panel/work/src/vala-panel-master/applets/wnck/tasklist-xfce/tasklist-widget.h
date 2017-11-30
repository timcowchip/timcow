/*
 * Copyright (C) 2008-2010 Nick Schermer <nick@xfce.org>
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef __XFCE_TASKLIST_H__
#define __XFCE_TASKLIST_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef enum _XfceTasklistGrouping XfceTasklistGrouping;
typedef enum _XfceTasklistSortOrder XfceTasklistSortOrder;
typedef enum _XfceTasklistMClick XfceTasklistMClick;

G_DECLARE_FINAL_TYPE(XfceTasklist, xfce_tasklist, XFCE, TASKLIST, GtkContainer)

#define XFCE_TYPE_TASKLIST (xfce_tasklist_get_type())

enum _XfceTasklistGrouping
{
	XFCE_TASKLIST_GROUPING_NEVER,
	XFCE_TASKLIST_GROUPING_ALWAYS,
	/*XFCE_TASKLIST_GROUPING_AUTO, */ /* when space is limited */

	XFCE_TASKLIST_GROUPING_MIN     = XFCE_TASKLIST_GROUPING_NEVER,
	XFCE_TASKLIST_GROUPING_MAX     = XFCE_TASKLIST_GROUPING_ALWAYS,
	XFCE_TASKLIST_GROUPING_DEFAULT = XFCE_TASKLIST_GROUPING_NEVER
};

enum _XfceTasklistSortOrder
{
	XFCE_TASKLIST_SORT_ORDER_TIMESTAMP,       /* sort by unique_id */
	XFCE_TASKLIST_SORT_ORDER_GROUP_TIMESTAMP, /* sort by group and then by timestamp */
	XFCE_TASKLIST_SORT_ORDER_TITLE,           /* sort by window title */
	XFCE_TASKLIST_SORT_ORDER_GROUP_TITLE,     /* sort by group and then by title */
	XFCE_TASKLIST_SORT_ORDER_DND,             /* append and support dnd */

	XFCE_TASKLIST_SORT_ORDER_MIN     = XFCE_TASKLIST_SORT_ORDER_TIMESTAMP,
	XFCE_TASKLIST_SORT_ORDER_MAX     = XFCE_TASKLIST_SORT_ORDER_DND,
	XFCE_TASKLIST_SORT_ORDER_DEFAULT = XFCE_TASKLIST_SORT_ORDER_GROUP_TIMESTAMP
};

enum _XfceTasklistMClick
{
	XFCE_TASKLIST_MIDDLE_CLICK_NOTHING,         /* do nothing */
	XFCE_TASKLIST_MIDDLE_CLICK_CLOSE_WINDOW,    /* close the window */
	XFCE_TASKLIST_MIDDLE_CLICK_MINIMIZE_WINDOW, /* minimize, never minimize with button 1 */

	XFCE_TASKLIST_MIDDLE_CLICK_MIN     = XFCE_TASKLIST_MIDDLE_CLICK_NOTHING,
	XFCE_TASKLIST_MIDDLE_CLICK_MAX     = XFCE_TASKLIST_MIDDLE_CLICK_MINIMIZE_WINDOW,
	XFCE_TASKLIST_MIDDLE_CLICK_DEFAULT = XFCE_TASKLIST_MIDDLE_CLICK_NOTHING
};

GType xfce_tasklist_get_type(void) G_GNUC_CONST;

XfceTasklist *xfce_tasklist_new();

void xfce_tasklist_set_orientation(XfceTasklist *tasklist, GtkOrientation mode);

void xfce_tasklist_set_size(XfceTasklist *tasklist, gint size);

void xfce_tasklist_set_nrows(XfceTasklist *tasklist, gint nrows);

void xfce_tasklist_update_monitor_geometry(XfceTasklist *tasklist);

void xfce_tasklist_set_include_all_workspaces(XfceTasklist *tasklist, gboolean all_workspaces);

void xfce_tasklist_set_include_all_monitors(XfceTasklist *tasklist, gboolean all_monitors);

void xfce_tasklist_set_button_relief(XfceTasklist *tasklist, GtkReliefStyle button_relief);

void xfce_tasklist_set_show_labels(XfceTasklist *tasklist, gboolean show_labels);

void xfce_tasklist_set_show_only_minimized(XfceTasklist *tasklist, gboolean only_minimized);

void xfce_tasklist_set_show_wireframes(XfceTasklist *tasklist, gboolean show_wireframes);

void xfce_tasklist_set_grouping(XfceTasklist *tasklist, XfceTasklistGrouping grouping);

void xfce_tasklist_update_edge(XfceTasklist *tasklist, GtkPositionType edge);

G_END_DECLS

#endif /* !__XFCE_TASKLIST_H__ */
