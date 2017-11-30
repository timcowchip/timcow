/*
 * vala-panel
 * Copyright (C) 2015-2016 Konstantin Pugin <ria.freelander@gmail.com>
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

#include <glib/gi18n.h>
#include <inttypes.h>
#include <string.h>

#include "css.h"
#include "definitions.h"
#include "misc.h"
#include "toplevel.h"

static void set_widget_align(GtkWidget *user_data, gpointer data)
{
	if (GTK_IS_WIDGET(user_data))
	{
		gtk_widget_set_halign(GTK_WIDGET(user_data), GTK_ALIGN_FILL);
		gtk_widget_set_valign(GTK_WIDGET(user_data), GTK_ALIGN_FILL);
	}
}

/* Draw text into a label, with the user preference color and optionally bold. */
void vala_panel_setup_label(GtkLabel *label, const char *text, bool bold, double factor)
{
	gtk_label_set_text(label, text);
	g_autofree char *css = css_generate_font_label(factor, bold);
	css_apply_with_class(GTK_WIDGET(label), css, "-vala-panel-font-label", false);
}

/* Children hierarhy: button => alignment => box => (label,image) */
static void setup_button_notify_connect(GObject *_sender, GParamSpec *b, gpointer self)
{
	GtkButton *a = GTK_BUTTON(_sender);
	if (!strcmp(b->name, "label") || !strcmp(b->name, "image"))
	{
		GtkWidget *w = gtk_bin_get_child(GTK_BIN(a));
		if (GTK_IS_CONTAINER(w))
		{
			GtkWidget *ch;
			if (GTK_IS_BIN(w))
				ch = gtk_bin_get_child(GTK_BIN(w));
			else
				ch = w;
			if (GTK_IS_CONTAINER(ch))
				gtk_container_forall(GTK_CONTAINER(ch), set_widget_align, NULL);
			gtk_widget_set_halign(ch, GTK_ALIGN_FILL);
			gtk_widget_set_valign(ch, GTK_ALIGN_FILL);
		}
	}
}

void vala_panel_setup_button(GtkButton *b, GtkImage *img, const char *label)
{
	css_apply_from_resource(GTK_WIDGET(b), "/org/vala-panel/lib/style.css", "-panel-button");
	g_signal_connect(b, "notify", setup_button_notify_connect, NULL);
	if (img != NULL)
	{
		gtk_button_set_image(b, GTK_WIDGET(img));
		gtk_button_set_always_show_image(b, true);
	}
	if (label != NULL)
		gtk_button_set_label(b, label);
	gtk_button_set_relief(b, GTK_RELIEF_NONE);
}

void vala_panel_setup_icon(GtkImage *img, GIcon *icon, ValaPanelToplevel *top, int size)
{
	gtk_image_set_from_gicon(img, icon, GTK_ICON_SIZE_INVALID);
	if (top != NULL)
		g_object_bind_property(top,
		                       VALA_PANEL_KEY_ICON_SIZE,
		                       img,
		                       "pixel-size",
		                       G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);
	else if (size > 0)
		gtk_image_set_pixel_size(img, size);
}

void vala_panel_setup_icon_button(GtkButton *btn, GIcon *icon, const char *label,
                                  ValaPanelToplevel *top)
{
	css_apply_from_resource(btn, "/org/vala-panel/lib/style.css", "-panel-icon-button");
	css_toggle_class(btn, GTK_STYLE_CLASS_BUTTON, true);
	GtkImage *img = NULL;
	if (icon != NULL)
	{
		img = gtk_image_new();
		vala_panel_setup_icon(img, icon, top, -1);
	}
	vala_panel_setup_button(btn, img, label);
	gtk_container_set_border_width(btn, 0);
	gtk_widget_set_can_focus(btn, false);
	gtk_widget_set_has_window(btn, false);
}

inline void vala_panel_apply_window_icon(GtkWindow *win)
{
	g_autoptr(GdkPixbuf) icon =
	    gdk_pixbuf_new_from_resource("/org/vala-panel/lib/panel.png", NULL);
	gtk_window_set_icon(win, icon);
}

void vala_panel_scale_button_set_range(GtkScaleButton *b, gint lower, gint upper)
{
	gtk_adjustment_set_lower(gtk_scale_button_get_adjustment(b), lower);
	gtk_adjustment_set_upper(gtk_scale_button_get_adjustment(b), upper);
	gtk_adjustment_set_step_increment(gtk_scale_button_get_adjustment(b), 1);
	gtk_adjustment_set_page_increment(gtk_scale_button_get_adjustment(b), 1);
}

void vala_panel_scale_button_set_value_labeled(GtkScaleButton *b, gint value)
{
	gtk_scale_button_set_value(b, value);
	g_autofree gchar *str = g_strdup_printf("%d", value);
	gtk_button_set_label(GTK_BUTTON(b), str);
}

void vala_panel_add_prop_as_action(GActionMap *map, const char *prop)
{
	g_autoptr(GAction) action = G_ACTION(g_property_action_new(prop, map, prop));
	g_action_map_add_action(map, action);
}

void vala_panel_add_gsettings_as_action(GActionMap *map, GSettings *settings, const char *prop)
{
	vala_panel_bind_gsettings(map, settings, prop);
	g_autoptr(GAction) action = G_ACTION(g_settings_create_action(settings, prop));
	g_action_map_add_action(map, action);
}

int vala_panel_monitor_num_from_mon(GdkDisplay *disp, GdkMonitor *mon)
{
	int mons = gdk_display_get_n_monitors(disp);
	for (int i = 0; i < mons; i++)
	{
		if (mon == gdk_display_get_monitor(disp, i))
			return i;
	}
	return -1;
}

void vala_panel_reset_schema(GSettings *settings)
{
	g_autoptr(GSettingsSchema) schema = NULL;
	g_object_get(settings, "settings-schema", &schema, NULL);
	g_auto(GStrv) keys = g_settings_schema_list_keys(schema);
	for (int i = 0; keys[i]; i++)
		g_settings_reset(settings, keys[i]);
}

void vala_panel_reset_schema_with_children(GSettings *settings)
{
	g_settings_delay(settings);
	vala_panel_reset_schema(settings);
	g_auto(GStrv) children = g_settings_list_children(settings);
	for (int i = 0; children[i]; i++)
	{
		g_autoptr(GSettings) child = g_settings_get_child(settings, children[i]);
		vala_panel_reset_schema(child);
	}
	g_settings_apply(settings);
	g_settings_sync();
}

void vala_panel_generate_error_dialog(GtkWindow *parent, const char *error)
{
	GtkMessageDialog *dlg;
	g_warning("%s", error);
	dlg = (GtkMessageDialog *)gtk_message_dialog_new((GtkWindow *)parent,
	                                                 GTK_DIALOG_DESTROY_WITH_PARENT,
	                                                 GTK_MESSAGE_ERROR,
	                                                 GTK_BUTTONS_CLOSE,
	                                                 "%s",
	                                                 error);
	vala_panel_apply_window_icon(
	    G_TYPE_CHECK_INSTANCE_TYPE(dlg, gtk_window_get_type()) ? ((GtkWindow *)dlg) : NULL);
	gtk_window_set_title((GtkWindow *)dlg, _("Error"));
	gtk_dialog_run((GtkDialog *)dlg);
	gtk_widget_destroy((GtkWidget *)dlg);
}

bool vala_panel_generate_confirmation_dialog(GtkWindow *parent, const char *error)
{
	GtkMessageDialog *dlg;
	dlg = (GtkMessageDialog *)gtk_message_dialog_new((GtkWindow *)parent,
	                                                 GTK_DIALOG_MODAL,
	                                                 GTK_MESSAGE_QUESTION,
	                                                 GTK_BUTTONS_OK_CANCEL,
	                                                 "%s",
	                                                 error);
	vala_panel_apply_window_icon(
	    G_TYPE_CHECK_INSTANCE_TYPE(dlg, gtk_window_get_type()) ? ((GtkWindow *)dlg) : NULL);
	gtk_window_set_title((GtkWindow *)dlg, _("Error"));
	bool ret = (gtk_dialog_run((GtkDialog *)dlg) == GTK_RESPONSE_OK);
	gtk_widget_destroy((GtkWidget *)dlg);
	return ret;
}
