#include "applet-widget.h"
#include "css.h"
#include "definitions.h"
#include "toplevel.h"
#include "vala-panel-compat.h"

typedef struct
{
	GtkDialog *dialog;
	GtkWidget *background;
	ValaPanelToplevel *toplevel;
	GSettings *settings;
	char *uuid;
	GSimpleActionGroup *grp;

} ValaPanelAppletPrivate;

static inline void destroy_dialog(GtkDialog *p, gpointer user_data)
{
	gtk_widget_destroy0(((ValaPanelAppletPrivate *)user_data)->dialog);
}

G_DEFINE_TYPE_WITH_PRIVATE(ValaPanelApplet, vala_panel_applet, GTK_TYPE_BIN)

static void activate_configure(GSimpleAction *act, GVariant *param, gpointer self);
static void activate_menu(GSimpleAction *act, GVariant *param, gpointer self);
static void activate_remove(GSimpleAction *act, GVariant *param, gpointer self);

static const GActionEntry entries[] =
    { { VALA_PANEL_APPLET_ACTION_MENU, activate_menu, NULL, NULL, NULL },
      { VALA_PANEL_APPLET_ACTION_CONFIGURE, activate_configure, NULL, NULL, NULL },
      { "remove", activate_remove, NULL, NULL, NULL } };

enum
{
	VALA_PANEL_APPLET_DUMMY_PROPERTY,
	VALA_PANEL_APPLET_BACKGROUND_WIDGET,
	VALA_PANEL_APPLET_TOPLEVEL,
	VALA_PANEL_APPLET_SETTINGS,
	VALA_PANEL_APPLET_UUID,
	VALA_PANEL_APPLET_GRP,
	VALA_PANEL_APPLET_ALL
};

static GParamSpec *pspecs[VALA_PANEL_APPLET_ALL];

static bool release_event_helper(GtkWidget *_sender, GdkEventButton *b, gpointer obj)
{
	ValaPanelApplet *self =
	    G_TYPE_CHECK_INSTANCE_CAST(_sender, VALA_PANEL_TYPE_APPLET, ValaPanelApplet);
	ValaPanelAppletPrivate *p = vala_panel_applet_get_instance_private(self);
	if (b->button == 3 && ((b->state & gtk_accelerator_get_default_mod_mask()) == 0))
	{
		GtkMenu *m = vala_panel_toplevel_get_plugin_menu(p->toplevel, self);
		gtk_menu_popup_at_widget(m,
		                         GTK_WIDGET(self),
		                         GDK_GRAVITY_NORTH,
		                         GDK_GRAVITY_NORTH,
		                         (GdkEvent *)b);
		return true;
	}
	return false;
}

gpointer vala_panel_applet_construct(GType ex, ValaPanelToplevel *top, GSettings *settings,
                                     const char *uuid)
{
	return g_object_new(ex, "toplevel", top, "settings", settings, "uuid", uuid, NULL);
}

static GObject *vala_panel_applet_constructor(GType type, guint n_construct_properties,
                                              GObjectConstructParam *construct_properties)
{
	GObjectClass *parent_class = G_OBJECT_CLASS(vala_panel_applet_parent_class);
	GObject *obj =
	    parent_class->constructor(type, n_construct_properties, construct_properties);
	ValaPanelApplet *self =
	    G_TYPE_CHECK_INSTANCE_CAST(obj, VALA_PANEL_TYPE_APPLET, ValaPanelApplet);
	ValaPanelAppletPrivate *p = vala_panel_applet_get_instance_private(self);
	gtk_widget_set_has_window((GtkWidget *)self, false);
	gtk_widget_insert_action_group(self, "applet", p->grp);
	g_signal_connect(self, "button-release-event", G_CALLBACK(release_event_helper), NULL);
	return G_OBJECT(self);
}

void vala_panel_applet_init_background(ValaPanelApplet *self)
{
	ValaPanelAppletPrivate *p = vala_panel_applet_get_instance_private(self);
	GdkRGBA color;
	gdk_rgba_parse(&color, "rgba(0,0,0,0)");
	g_autofree char *css = css_generate_background(NULL, &color);
	css_apply_with_class(p->background, css, "-vala-panel-background", false);
}

void vala_panel_applet_show_config_dialog(ValaPanelApplet *self)
{
	ValaPanelAppletPrivate *p = vala_panel_applet_get_instance_private(self);
	if (p->dialog == NULL)
	{
		GtkWidget *dlg = gtk_dialog_new();
		GtkWidget *ui  = VALA_PANEL_APPLET_GET_CLASS(self)->get_settings_ui(self);
		gtk_widget_show(ui);
		gtk_widget_set_hexpand(ui, true);
		gtk_widget_set_vexpand(ui, true);
		gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dlg))), ui);
		gtk_window_set_transient_for(dlg, p->toplevel);
		p->dialog = dlg;
		g_signal_connect_after(dlg, "hide", G_CALLBACK(destroy_dialog), p);
		g_signal_connect_after(dlg, "destroy", G_CALLBACK(destroy_dialog), p);
	}
	gtk_window_present(p->dialog);
}
bool vala_panel_applet_is_configurable(ValaPanelApplet *self)
{
	ValaPanelAppletPrivate *p = vala_panel_applet_get_instance_private(self);
	return g_action_group_get_action_enabled(G_ACTION_GROUP(p->grp), "configure");
}
static void activate_configure(GSimpleAction *act, GVariant *param, gpointer self)
{
	vala_panel_applet_show_config_dialog(VALA_PANEL_APPLET(self));
}
static void activate_menu(GSimpleAction *act, GVariant *param, gpointer self)
{
	VALA_PANEL_APPLET_GET_CLASS(self)->show_menu(act, param, self);
}
static void activate_remove(GSimpleAction *act, GVariant *param, gpointer obj)
{
	ValaPanelApplet *self =
	    G_TYPE_CHECK_INSTANCE_CAST(obj, VALA_PANEL_TYPE_APPLET, ValaPanelApplet);
	ValaPanelAppletPrivate *p = vala_panel_applet_get_instance_private(self);
	/* If the configuration dialog is open, there will certainly be a crash if the
	 * user manipulates the Configured Plugins list, after we remove this entry.
     * Close the configuration dialog if it is open. */
	vala_panel_toplevel_destroy_pref_dialog(p->toplevel);
	vala_panel_layout_remove_applet(vala_panel_toplevel_get_layout(p->toplevel), self);
}
static GtkWidget *vala_panel_applet_get_config_dialog(ValaPanelApplet *self)
{
	return NULL;
}

static void vala_panel_applet_measure(ValaPanelApplet *self, GtkOrientation orient, int for_size,
                                      int *min, int *nat, int *base_min, int *base_nat)
{
	ValaPanelAppletPrivate *p = vala_panel_applet_get_instance_private(self);
	GtkOrientation panel_ori;
	g_object_get(p->toplevel, VALA_PANEL_KEY_ORIENTATION, &panel_ori, NULL);
	int height, icon_size;
	g_object_get(p->toplevel,
	             VALA_PANEL_KEY_HEIGHT,
	             &height,
	             VALA_PANEL_KEY_ICON_SIZE,
	             &icon_size,
	             NULL);
	if (panel_ori != orient)
	{
		*min = icon_size;
		*nat = height;
	}
	else
	{
		if (orient == GTK_ORIENTATION_HORIZONTAL)
			GTK_WIDGET_CLASS(vala_panel_applet_parent_class)
			    ->get_preferred_width_for_height(
			        (GtkWidget *)G_TYPE_CHECK_INSTANCE_CAST(self,
			                                                gtk_bin_get_type(),
			                                                GtkBin),
			        for_size,
			        min,
			        nat);
		else
			GTK_WIDGET_CLASS(vala_panel_applet_parent_class)
			    ->get_preferred_height_for_width(
			        (GtkWidget *)G_TYPE_CHECK_INSTANCE_CAST(self,
			                                                gtk_bin_get_type(),
			                                                GtkBin),
			        for_size,
			        min,
			        nat);
	}
	*base_min = *base_nat = -1;
}

static void vala_panel_applet_get_preferred_height_for_width(GtkWidget *self, int width, int *min,
                                                             int *nat)
{
	int x, y;
	vala_panel_applet_measure(VALA_PANEL_APPLET(self),
	                          GTK_ORIENTATION_VERTICAL,
	                          width,
	                          min,
	                          nat,
	                          &x,
	                          &y);
}
static void vala_panel_applet_get_preferred_width_for_height(GtkWidget *self, int height, int *min,
                                                             int *nat)
{
	int x, y;
	vala_panel_applet_measure(self, GTK_ORIENTATION_HORIZONTAL, height, min, nat, &x, &y);
}
GtkSizeRequestMode vala_panel_applet_get_request_mode(GtkWidget *obj)
{
	ValaPanelApplet *self =
	    G_TYPE_CHECK_INSTANCE_CAST(obj, VALA_PANEL_TYPE_APPLET, ValaPanelApplet);
	ValaPanelAppletPrivate *p = vala_panel_applet_get_instance_private(VALA_PANEL_APPLET(self));
	GtkOrientation pos;
	g_object_get(p->toplevel, VALA_PANEL_KEY_ORIENTATION, &pos, NULL);
	return (pos == GTK_ORIENTATION_HORIZONTAL) ? GTK_SIZE_REQUEST_WIDTH_FOR_HEIGHT
	                                           : GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}
static void vala_panel_applet_get_preferred_width(GtkWidget *self, int *min, int *nat)
{
	ValaPanelAppletPrivate *p = vala_panel_applet_get_instance_private(VALA_PANEL_APPLET(self));
	int height, icon_size;
	g_object_get(p->toplevel,
	             VALA_PANEL_KEY_HEIGHT,
	             &height,
	             VALA_PANEL_KEY_ICON_SIZE,
	             &icon_size,
	             NULL);
	*min = icon_size;
	*nat = height;
}
static void vala_panel_applet_get_preferred_height(GtkWidget *self, int *min, int *nat)
{
	ValaPanelAppletPrivate *p = vala_panel_applet_get_instance_private(VALA_PANEL_APPLET(self));
	int height, icon_size;
	g_object_get(p->toplevel,
	             VALA_PANEL_KEY_HEIGHT,
	             &height,
	             VALA_PANEL_KEY_ICON_SIZE,
	             &icon_size,
	             NULL);
	*min = icon_size;
	*nat = height;
}

static void vala_panel_applet_parent_set(ValaPanelApplet *self, GtkWidget *prev_parent)
{
	ValaPanelAppletPrivate *p = vala_panel_applet_get_instance_private(VALA_PANEL_APPLET(self));
	if (prev_parent == NULL)
	{
		if (p->background == NULL)
			p->background = GTK_WIDGET(self);
		vala_panel_applet_init_background(self);
	}
}

void vala_panel_applet_update_context_menu(ValaPanelApplet *self, GMenu *parent_menu)
{
	VALA_PANEL_APPLET_GET_CLASS(self)->update_context_menu(self, parent_menu);
}

void vala_panel_applet_update_context_menu_private(ValaPanelApplet *self, GMenu *parent_menu)
{
}

static void vala_panel_applet_init(ValaPanelApplet *self)
{
	ValaPanelAppletPrivate *p = vala_panel_applet_get_instance_private(VALA_PANEL_APPLET(self));
	p->grp                    = g_simple_action_group_new();
	p->settings               = NULL;
	g_action_map_add_action_entries(G_ACTION_MAP(p->grp), entries, G_N_ELEMENTS(entries), self);
	GSimpleAction *cnf =
	    G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(p->grp), "configure"));
	g_simple_action_set_enabled(cnf, false);
	cnf = G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(p->grp), "menu"));
	g_simple_action_set_enabled(cnf, false);
}
GtkWidget *vala_panel_applet_get_background_widget(ValaPanelApplet *self)
{
	ValaPanelAppletPrivate *p = vala_panel_applet_get_instance_private(VALA_PANEL_APPLET(self));
	return p->background;
}

void vala_panel_applet_set_background_widget(ValaPanelApplet *self, GtkWidget *w)
{
	ValaPanelAppletPrivate *p = vala_panel_applet_get_instance_private(VALA_PANEL_APPLET(self));
	p->background             = w;
}

ValaPanelToplevel *vala_panel_applet_get_toplevel(ValaPanelApplet *self)
{
	ValaPanelAppletPrivate *p = vala_panel_applet_get_instance_private(VALA_PANEL_APPLET(self));
	return p->toplevel;
}

GSettings *vala_panel_applet_get_settings(ValaPanelApplet *self)
{
	ValaPanelAppletPrivate *p = vala_panel_applet_get_instance_private(VALA_PANEL_APPLET(self));
	return p->settings;
}
const char *vala_panel_applet_get_uuid(ValaPanelApplet *self)
{
	ValaPanelAppletPrivate *p = vala_panel_applet_get_instance_private(VALA_PANEL_APPLET(self));
	return p->uuid;
}

const char *vala_panel_applet_get_action_group(ValaPanelApplet *self)
{
	ValaPanelAppletPrivate *p = vala_panel_applet_get_instance_private(VALA_PANEL_APPLET(self));
	return p->grp;
}
static void vala_panel_applet_get_property(GObject *object, guint property_id, GValue *value,
                                           GParamSpec *pspec)
{
	ValaPanelApplet *self =
	    G_TYPE_CHECK_INSTANCE_CAST(object, VALA_PANEL_TYPE_APPLET, ValaPanelApplet);
	ValaPanelAppletPrivate *p = vala_panel_applet_get_instance_private(VALA_PANEL_APPLET(self));
	switch (property_id)
	{
	case VALA_PANEL_APPLET_BACKGROUND_WIDGET:
		g_value_set_object(value, p->background);
		break;
	case VALA_PANEL_APPLET_TOPLEVEL:
		g_value_set_object(value, p->toplevel);
		break;
	case VALA_PANEL_APPLET_SETTINGS:
		g_value_set_object(value, p->settings);
		break;
	case VALA_PANEL_APPLET_UUID:
		g_value_set_string(value, p->uuid);
		break;
	case VALA_PANEL_APPLET_GRP:
		g_value_set_object(value, p->grp);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
		break;
	}
}

static void vala_panel_applet_set_property(GObject *object, guint property_id, const GValue *value,
                                           GParamSpec *pspec)
{
	ValaPanelApplet *self;
	self = G_TYPE_CHECK_INSTANCE_CAST(object, VALA_PANEL_TYPE_APPLET, ValaPanelApplet);
	ValaPanelAppletPrivate *p = vala_panel_applet_get_instance_private(VALA_PANEL_APPLET(self));
	switch (property_id)
	{
	case VALA_PANEL_APPLET_BACKGROUND_WIDGET:
		p->background = GTK_WIDGET(g_value_get_object(value));
		g_object_notify_by_pspec(object, pspec);
		break;
	case VALA_PANEL_APPLET_TOPLEVEL:
		p->toplevel = VALA_PANEL_TOPLEVEL(g_value_get_object(value));
		g_object_notify_by_pspec(object, pspec);
		break;
	case VALA_PANEL_APPLET_SETTINGS:
		p->settings = G_SETTINGS(g_value_get_object(value));
		g_object_notify_by_pspec(object, pspec);
		break;
	case VALA_PANEL_APPLET_UUID:
		g_free0(p->uuid);
		p->uuid = g_value_dup_string(value);
		g_object_notify_by_pspec(object, pspec);
		break;
	case VALA_PANEL_APPLET_GRP:
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
		break;
	}
}

static void vala_panel_applet_finalize(GObject *obj)
{
	ValaPanelApplet *self =
	    G_TYPE_CHECK_INSTANCE_CAST(obj, VALA_PANEL_TYPE_APPLET, ValaPanelApplet);
	ValaPanelAppletPrivate *p = vala_panel_applet_get_instance_private(VALA_PANEL_APPLET(self));
	g_object_unref0(p->grp);
	g_free0(p->uuid);
}
static void vala_panel_applet_class_init(ValaPanelAppletClass *klass)
{
	vala_panel_applet_parent_class = g_type_class_peek_parent(klass);
	((ValaPanelAppletClass *)klass)->update_context_menu =
	    vala_panel_applet_update_context_menu_private;
	((GtkWidgetClass *)klass)->parent_set =
	    (void (*)(GtkWidget *, GtkWidget *))vala_panel_applet_parent_set;
	((ValaPanelAppletClass *)klass)->show_menu = NULL;
	((GtkWidgetClass *)klass)->get_preferred_height_for_width =
	    vala_panel_applet_get_preferred_height_for_width;
	((GtkWidgetClass *)klass)->get_preferred_width_for_height =

	    vala_panel_applet_get_preferred_width_for_height;
	((GtkWidgetClass *)klass)->get_request_mode      = vala_panel_applet_get_request_mode;
	((GtkWidgetClass *)klass)->get_preferred_width   = vala_panel_applet_get_preferred_width;
	((GtkWidgetClass *)klass)->get_preferred_height  = vala_panel_applet_get_preferred_height;
	((ValaPanelAppletClass *)klass)->get_settings_ui = vala_panel_applet_get_config_dialog;
	G_OBJECT_CLASS(klass)->constructor               = vala_panel_applet_constructor;
	G_OBJECT_CLASS(klass)->get_property              = vala_panel_applet_get_property;
	G_OBJECT_CLASS(klass)->set_property              = vala_panel_applet_set_property;
	G_OBJECT_CLASS(klass)->finalize                  = vala_panel_applet_finalize;
	pspecs[VALA_PANEL_APPLET_BACKGROUND_WIDGET] =
	    g_param_spec_object(VALA_PANEL_KEY_BACKGROUND_WIDGET,
	                        "background-widget",
	                        "background-widget",
	                        gtk_widget_get_type(),
	                        G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB |
	                            G_PARAM_READABLE | G_PARAM_WRITABLE);
	pspecs[VALA_PANEL_APPLET_TOPLEVEL] =
	    g_param_spec_object(VALA_PANEL_KEY_TOPLEVEL,
	                        "toplevel",
	                        "toplevel",
	                        VALA_PANEL_TYPE_TOPLEVEL,
	                        G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB |
	                            G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);
	pspecs[VALA_PANEL_APPLET_UUID] =
	    g_param_spec_string(VALA_PANEL_KEY_UUID,
	                        "uuid",
	                        "uuid",
	                        NULL,
	                        G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB |
	                            G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);
	pspecs[VALA_PANEL_APPLET_SETTINGS] =
	    g_param_spec_object(VALA_PANEL_KEY_SETTINGS,
	                        "settings",
	                        "settings",
	                        g_settings_get_type(),
	                        G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB |
	                            G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);
	pspecs[VALA_PANEL_APPLET_GRP] =
	    g_param_spec_object(VALA_PANEL_KEY_ACTION_GROUP,
	                        "grp",
	                        "grp",
	                        g_simple_action_group_get_type(),
	                        G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB |
	                            G_PARAM_READABLE);
	g_object_class_install_property(G_OBJECT_CLASS(klass),
	                                VALA_PANEL_APPLET_BACKGROUND_WIDGET,
	                                pspecs[VALA_PANEL_APPLET_BACKGROUND_WIDGET]);
	g_object_class_install_property(G_OBJECT_CLASS(klass),
	                                VALA_PANEL_APPLET_TOPLEVEL,
	                                pspecs[VALA_PANEL_APPLET_TOPLEVEL]);
	g_object_class_install_property(G_OBJECT_CLASS(klass),
	                                VALA_PANEL_APPLET_SETTINGS,
	                                pspecs[VALA_PANEL_APPLET_SETTINGS]);
	g_object_class_install_property(G_OBJECT_CLASS(klass),
	                                VALA_PANEL_APPLET_UUID,
	                                pspecs[VALA_PANEL_APPLET_UUID]);
	g_object_class_install_property(G_OBJECT_CLASS(klass),
	                                VALA_PANEL_APPLET_GRP,
	                                pspecs[VALA_PANEL_APPLET_GRP]);
}
