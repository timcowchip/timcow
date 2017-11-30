/*
 * vala-panel
 * Copyright (C) 2015-2017 Konstantin Pugin <ria.freelander@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "applet-widget.h"
#include "gio/gsettingsbackend.h"
#include "lib/definitions.h"
#include "lib/settings-manager.h"
//#include "lib/toplevel.h"
#include "vala-panel-compat.h"
#include "vala-panel-platform-standalone-x11.h"

struct _ValaPanelPlatformX11
{
	ValaPanelPlatform __parent__;
	GtkApplication *app;
	char *profile;
};

#define g_key_file_load_from_config(f, p)                                                          \
	g_key_file_load_from_file(f,                                                               \
	                          _user_config_file_name(GETTEXT_PACKAGE, p, NULL),                \
	                          G_KEY_FILE_KEEP_COMMENTS,                                        \
	                          NULL)

G_DEFINE_TYPE(ValaPanelPlatformX11, vala_panel_platform_x11, vala_panel_platform_get_type())

ValaPanelPlatformX11 *vala_panel_platform_x11_new(GtkApplication *app, const char *profile)
{
	ValaPanelPlatformX11 *pl =
	    VALA_PANEL_PLATFORM_X11(g_object_new(vala_panel_platform_x11_get_type(), NULL));
	pl->app     = app;
	pl->profile = g_strdup(profile);
	GSettingsBackend *backend =
	    g_keyfile_settings_backend_new(_user_config_file_name_new(pl->profile),
	                                   VALA_PANEL_OBJECT_PATH,
	                                   VALA_PANEL_CONFIG_HEADER);
	vala_panel_platform_init_settings(VALA_PANEL_PLATFORM(pl), backend);
	return pl;
}

typedef struct
{
	GtkApplication *app;
	ValaPanelPlatform *obj;
	int toplevels_count;
} DataStruct;

static void predicate_func(const char *key, ValaPanelUnitSettings *value, DataStruct *user_data)
{
	bool is_toplevel = vala_panel_unit_settings_is_toplevel(value);
	if (is_toplevel)
	{
		ValaPanelToplevel *unit =
		    vala_panel_toplevel_new(user_data->app, user_data->obj, key);
		gtk_application_add_window(user_data->app, GTK_WINDOW(unit));
		user_data->toplevels_count++;
	}
}

static bool vala_panel_platform_x11_start_panels_from_profile(ValaPanelPlatform *obj,
                                                              GtkApplication *app,
                                                              const char *profile)
{
	ValaPanelCoreSettings *core = vala_panel_platform_get_settings(obj);
	DataStruct user_data;
	user_data.app             = app;
	user_data.obj             = obj;
	user_data.toplevels_count = 0;
	g_hash_table_foreach(core->all_units, (GHFunc)predicate_func, &user_data);
	return user_data.toplevels_count;
}

static void vala_panel_platform_x11_move_to_coords(ValaPanelPlatform *f, GtkWindow *top, int x,
                                                   int y)
{
	gtk_window_move(top, x, y);
}

// TODO: Make more readable code without switch
static void vala_panel_platform_x11_move_to_side(ValaPanelPlatform *f, GtkWindow *top,
                                                 PanelGravity gravity, int monitor)
{
	GtkOrientation orient = vala_panel_orient_from_gravity(gravity);
	GdkDisplay *d         = gtk_widget_get_display(GTK_WIDGET(top));
	GdkMonitor *mon =
	    monitor < 0 ? gdk_display_get_primary_monitor(d) : gdk_display_get_monitor(d, monitor);
	GdkRectangle marea;
	int x = 0, y = 0;
	gdk_monitor_get_geometry(mon, &marea);
	GtkRequisition size, min;
	gtk_widget_get_preferred_size(GTK_WIDGET(top), &min, &size);
	int height = orient == GTK_ORIENTATION_HORIZONTAL ? size.height : size.width;
	int width  = orient == GTK_ORIENTATION_HORIZONTAL ? size.width : size.height;
	switch (gravity)
	{
	case NORTH_LEFT:
	case WEST_UP:
		x = marea.x;
		y = marea.y;
		break;
	case NORTH_CENTER:
		x = marea.x + (marea.width - width) / 2;
		y = marea.y;
		break;
	case NORTH_RIGHT:
		x = marea.x + marea.width - width;
		y = marea.y;
		break;
	case SOUTH_LEFT:
		x = marea.x;
		y = marea.y + marea.height - height;
		break;
	case SOUTH_CENTER:
		x = marea.x + (marea.width - width) / 2;
		y = marea.y + marea.height - height;
		break;
	case SOUTH_RIGHT:
		x = marea.x + marea.width - width;
		y = marea.y + marea.height - height;
		break;
	case WEST_CENTER:
		x = marea.x;
		y = marea.y + (marea.height - width) / 2;
		break;
	case WEST_DOWN:
		x = marea.x;
		y = marea.y + (marea.height - width);
		break;
	case EAST_UP:
		x = marea.x + marea.width - height;
		y = marea.y;
		break;
	case EAST_CENTER:
		x = marea.x + marea.width - height;
		y = marea.y + (marea.height - width) / 2;
		break;
	case EAST_DOWN:
		x = marea.x + marea.width - height;
		y = marea.y + (marea.height - width);
		break;
	}
	gtk_window_move(top, x, y);
}

static bool vala_panel_platform_x11_edge_can_strut(ValaPanelPlatform *f, GtkWindow *top)
{
	gboolean strut_set = false;
	g_object_get(top, VALA_PANEL_KEY_STRUT, &strut_set, NULL);
	if (!gtk_widget_get_mapped(GTK_WIDGET(top)))
		return false;
	return strut_set;
}

static void vala_panel_platform_x11_update_strut(ValaPanelPlatform *f, GtkWindow *top)
{
	bool autohide;
	PanelGravity gravity;
	int monitor;
	int size, len;
	g_object_get(top,
	             VALA_PANEL_KEY_AUTOHIDE,
	             &autohide,
	             VALA_PANEL_KEY_GRAVITY,
	             &gravity,
	             VALA_PANEL_KEY_MONITOR,
	             &monitor,
	             VALA_PANEL_KEY_HEIGHT,
	             &size,
	             VALA_PANEL_KEY_WIDTH,
	             &len,
	             NULL);
	GdkRectangle primary_monitor_rect;
	GtkPositionType edge = vala_panel_edge_from_gravity(gravity);
	long struts[12]      = { 0 };
	GdkDisplay *screen   = gtk_widget_get_display(GTK_WIDGET(top));
	GdkMonitor *mon      = monitor < 0 ? gdk_display_get_primary_monitor(screen)
	                              : gdk_display_get_monitor(screen, monitor);
	gdk_monitor_get_geometry(mon, &primary_monitor_rect);
	/*
	strut-left strut-right strut-top strut-bottom
	strut-left-start-y   strut-left-end-y
	strut-right-start-y  strut-right-end-y
	strut-top-start-x    strut-top-end-x
	strut-bottom-start-x strut-bottom-end-x
	*/

	if (!gtk_widget_get_realized(GTK_WIDGET(top)))
		return;
	int panel_size = autohide ? GAP : size;
	// Struts dependent on position
	switch (edge)
	{
	case GTK_POS_TOP:
		struts[2] = primary_monitor_rect.y + panel_size;
		struts[8] = primary_monitor_rect.x;
		struts[9] = (primary_monitor_rect.x + primary_monitor_rect.width / 100 * len);
		break;
	case GTK_POS_LEFT:
		struts[0] = panel_size;
		struts[4] = primary_monitor_rect.y;
		struts[5] = primary_monitor_rect.y + primary_monitor_rect.height / 100 * len;
		break;
	case GTK_POS_RIGHT:
		struts[1] = panel_size;
		struts[6] = primary_monitor_rect.y;
		struts[7] = primary_monitor_rect.y + primary_monitor_rect.height / 100 * len;
		break;
	case GTK_POS_BOTTOM:
		struts[3]  = primary_monitor_rect.y + panel_size;
		struts[10] = primary_monitor_rect.x;
		struts[11] = (primary_monitor_rect.x + primary_monitor_rect.width / 100 * len);
		break;
	}
	GdkAtom atom    = gdk_atom_intern_static_string("_NET_WM_STRUT_PARTIAL");
	GdkWindow *xwin = gtk_widget_get_window(GTK_WIDGET(top));
	if (vala_panel_platform_can_strut(f, top))
		// all relevant WMs support this, Mutter included
		gdk_property_change(xwin,
		                    atom,
		                    gdk_atom_intern_static_string("CARDINAL"),
		                    32,
		                    GDK_PROP_MODE_REPLACE,
		                    (unsigned char *)struts,
		                    12);
	else
		gdk_property_delete(xwin, atom);
}

static void vala_panel_platform_x11_finalize(GObject *obj)
{
	ValaPanelPlatformX11 *self = VALA_PANEL_PLATFORM_X11(obj);
	g_free(self->profile);
	(*G_OBJECT_CLASS(vala_panel_platform_x11_parent_class)->finalize)(obj);
}

static void vala_panel_platform_x11_init(ValaPanelPlatformX11 *self)
{
}

static void vala_panel_platform_x11_class_init(ValaPanelPlatformX11Class *klass)
{
	VALA_PANEL_PLATFORM_CLASS(klass)->move_to_coords = vala_panel_platform_x11_move_to_coords;
	VALA_PANEL_PLATFORM_CLASS(klass)->move_to_side   = vala_panel_platform_x11_move_to_side;
	VALA_PANEL_PLATFORM_CLASS(klass)->update_strut   = vala_panel_platform_x11_update_strut;
	VALA_PANEL_PLATFORM_CLASS(klass)->can_strut      = vala_panel_platform_x11_edge_can_strut;
	VALA_PANEL_PLATFORM_CLASS(klass)->start_panels_from_profile =
	    vala_panel_platform_x11_start_panels_from_profile;
	G_OBJECT_CLASS(klass)->finalize = vala_panel_platform_x11_finalize;
}
