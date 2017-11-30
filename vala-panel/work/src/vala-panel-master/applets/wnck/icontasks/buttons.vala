/*
 * This file is part of budgie-desktop
 *
 * Copyright © 2014-2016 Ikey Doherty <ikey@solus-project.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

const string BUDGIE_STYLE_CLASS_BUTTON = "launcher";

/**
 * Maximum number of full flash cycles for urgency before giving up. Note
 * that the background colour will remain fully opaque until the calling
 * application then resets whatever caused the urgency/attention demand
 */
const int MAX_CYCLES = 12;

/**
 * Default opacity when beginning urgency cycles in the launcher
 */
const double DEFAULT_OPACITY = 0.1;

public class ButtonWrapper : Gtk.Revealer
{
    unowned IconButton? button;

    public ButtonWrapper(IconButton? button)
    {
        this.button = button;

        this.set_transition_type(Gtk.RevealerTransitionType.SLIDE_RIGHT);

        this.add(button);
        this.set_reveal_child(false);
        this.show_all();
    }

    public void gracefully_die()
    {
        if (!get_settings().gtk_enable_animations) {
            this.destroy();
            return;
        }

        this.set_transition_type(Gtk.RevealerTransitionType.SLIDE_LEFT);
        this.notify["child-revealed"].connect_after(()=> {
            this.destroy();
        });
        this.set_reveal_child(false);
    }
}

public class IconButton : Gtk.ToggleButton
{

    public new Gtk.Image image;
    public unowned Wnck.Window? window;
    protected Wnck.ActionMenu menu;
    public int icon_size;
    public GLib.DesktopAppInfo? ainfo;
    private Gtk.MenuItem pinnage;
    private Gtk.MenuItem unpinnage;
    private Gtk.SeparatorMenuItem sep_item;

    public bool requested_pin = false;

    private bool we_urgent = false;
    private double urg_opacity = DEFAULT_OPACITY;
    protected bool should_fade_in = true;
    private uint source_id;
    protected Gtk.Allocation our_alloc;

    protected int current_cycles = 0;

    unowned Settings? settings;

    public int panel_size = 10;

    protected Gdk.AppLaunchContext launch_context;

    private void update_app_info()
    {
        // Actions menu
        menu = new Wnck.ActionMenu(window);

        var sep = new Gtk.SeparatorMenuItem();
        menu.append(sep);
        sep_item = sep;
        pinnage = new Gtk.MenuItem.with_label(_("Pin to panel"));
        unpinnage = new Gtk.MenuItem.with_label(_("Unpin from panel"));
        sep.show();
        menu.append(pinnage);
        menu.append(unpinnage);

        /* Handle running instance pin/unpin */
        pinnage.activate.connect(()=> {
            requested_pin = true;
            DesktopHelper.set_pinned(settings, ainfo, true);
        });

        unpinnage.activate.connect(()=> {
            if (this is /*Sparta*/ PinnedIconButton) {
                var p = this as PinnedIconButton;
                DesktopHelper.set_pinned(settings, p.app_info, false);
            }
        });

        if (ainfo != null) {
            // Desktop app actions =)
            unowned string[] actions = ainfo.list_actions();
            if (actions.length == 0) {
                return;
            }
            sep = new Gtk.SeparatorMenuItem();
            menu.append(sep);
            sep.show_all();
            foreach (var action in actions) {
                var display_name = ainfo.get_action_name(action);
                var item = new Gtk.MenuItem.with_label(display_name);
                item.set_data("__aname", action);
                item.activate.connect(()=> {
                    string? act = item.get_data("__aname");
                    if (act == null) {
                        return;
                    }
                    // Never know.
                    if (ainfo == null) {
                        return;
                    }
                    launch_context.set_screen(get_screen());
                    launch_context.set_timestamp(Gdk.CURRENT_TIME);
                    ainfo.launch_action(act, launch_context);
                });
                item.show_all();
                menu.append(item);
            }
        }
        this.update_icon();
    }

    public void update_from_window()
    {
        we_urgent = false;
        if (source_id > 0) {
            remove_tick_callback(source_id);
            source_id = 0;
        }

        if (window == null) {
            if (this is PinnedIconButton) {
                this.get_style_context().remove_class("running");
            }
            return;
        }

        if (this is PinnedIconButton) {
            this.get_style_context().add_class("running");
        }
        set_tooltip_text(window.get_name());

        // Things we can happily handle ourselves
        window.icon_changed.connect(update_icon);
        window.name_changed.connect(()=> {
            set_tooltip_text(window.get_name());
        });
        update_icon();
        set_active(window.is_active());
        window.state_changed.connect(on_state_changed);

        this.update_app_info();
        queue_draw();
    }

    protected void on_state_changed(Wnck.WindowState changed, Wnck.WindowState state)
    {
        if (!window.needs_attention() && we_urgent) {
            we_urgent = false;
            if (source_id > 0) {
                remove_tick_callback(source_id);
                source_id = 0;
            }
            queue_draw();
            return;
        } else if (window.needs_attention() && !we_urgent) {
            we_urgent = true;
            should_fade_in = true;
            urg_opacity = DEFAULT_OPACITY;
            current_cycles = 0;
            source_id = add_tick_callback(on_tick);
        }
    }

    protected bool on_tick(Gtk.Widget widget, Gdk.FrameClock clock)
    {
        // Looks fine with 60hz. Might go nuts higher.
        var increment = 0.01;

        if (window == null) {
            urg_opacity = 0.0;
            we_urgent = false;
            return false;
        }

        if (should_fade_in) {
            urg_opacity += increment;
        } else {
            urg_opacity -= increment;
        }

        if (urg_opacity >= 1.0) {
            should_fade_in = false;
            urg_opacity = 1.0;
            current_cycles += 1;
        } else if (urg_opacity <= 0.0) {
            should_fade_in = true;
            urg_opacity = 0.0;
        }

        queue_draw();

        /* Stop flashing when we've fully cycled MAX_CYCLES */
        if (current_cycles >= MAX_CYCLES && urg_opacity >= 1.0) {
            return false;
        }

        return we_urgent;
    }

    public override bool draw(Cairo.Context cr)
    {
        if (!we_urgent) {
            return base.draw(cr);
        }

        /* Redundant right now but we might decide on something new in future. */
        int x = our_alloc.x;
        int y = our_alloc.y;
        int width = our_alloc.width;
        int height = our_alloc.height;

        Gdk.RGBA col = {};
        /* FIXME: I'M ON DRUGS */
        col.parse("#36689E");
        cr.set_source_rgba(col.red, col.green, col.blue, urg_opacity);
        cr.rectangle(x, y, width, height);
        cr.paint();

        return base.draw(cr);
    }

    private string wclass_name = null;
    private ulong wclass_id = 0;
    private unowned AppSystem? helper = null;

    public IconButton(Settings? settings, Wnck.Window? window, int size, DesktopAppInfo? ainfo, AppSystem? helper, int panel_size)
    {
        this.settings = settings;
        this.helper = helper;
        this.launch_context = get_display().get_app_launch_context();
        image = new Gtk.Image();
        image.pixel_size = size;
        icon_size = size;
        this.panel_size = panel_size;
        add(image);

        this.window = window;
        relief = Gtk.ReliefStyle.NONE;
        this.ainfo = ainfo;

        if (this.window != null) {
            this.wclass_name = this.window.get_class_instance_name();
        }

        /* No app info, no class name, probably spotify */
        if (this.wclass_name == null && this.ainfo == null) {
            this.wclass_id = this.window.class_changed.connect(()=> {
                string nclass_name = this.window.get_class_instance_name();
                if (nclass_name != null && this.wclass_name == null) {
                    this.window.disconnect(this.wclass_id);
                    this.wclass_id = 0;

                    this.wclass_name = nclass_name;
                    this.ainfo = helper.query_window(this.window);
                    this.update_app_info();
                    /* Request re-assess of pin move ? */
                }
            });
        }

        // Replace styling with our own
        PanelCSS.apply_from_resource(this,"/org/vala-panel/lib/style.css","-panel-icon-button");
        PanelCSS.apply_with_class(this,"",Gtk.STYLE_CLASS_BUTTON,true);
        update_from_window();

        // Handle clicking, etc.
        button_release_event.connect(on_button_release);

        set_can_focus(false);
    }

    /**
     * Enforce a 1:1.1 aspect ratio
     */
    public override void get_preferred_width(out int min, out int nat)
    {
        int norm = (int) ((double)panel_size * 1.1);
        min = norm;
        nat = norm;
    }


    /**
     * After allocation to ensure we go to the right place
     */
    public void icon_mapped()
    {
        if (window == null) {
            return;
        }
        int x, y;
        var toplevel = get_toplevel();
        translate_coordinates(toplevel, 0, 0, out x, out y);
        toplevel.get_window().get_root_coords(x, y, out x, out y);
        window.set_icon_geometry(x, y, our_alloc.width, our_alloc.height);
    }

    /**
     * This is for minimize animations, etc.
     */
    protected void on_size_allocate(Gtk.Allocation alloc)
    {
        if (window == null) {
            return;
        }
        int x, y;
        var toplevel = get_toplevel();
        translate_coordinates(toplevel, 0, 0, out x, out y);
        toplevel.get_window().get_root_coords(x, y, out x, out y);
        window.set_icon_geometry(x, y, alloc.width, alloc.height);

        our_alloc = alloc;
    }

    /**
     * Update the icon
     */
    public virtual void update_icon()
    {
        if (window == null) {
            return;
        }

        unowned GLib.Icon? aicon = null;
        if (ainfo != null) {
            aicon = ainfo.get_icon();
        }

        if (this.helper.has_derpy_icon(window) && aicon != null) {
            image.set_from_gicon(aicon, Gtk.IconSize.INVALID);
        } else {
            if (window.get_icon_is_fallback()) {
                if (ainfo != null && ainfo.get_icon() != null) {
                    image.set_from_gicon(ainfo.get_icon(), Gtk.IconSize.INVALID);
                } else {
                    image.set_from_pixbuf(window.get_icon());
                }
            } else {
                image.set_from_pixbuf(window.get_icon());
            }
        }
        image.pixel_size = icon_size;
        queue_resize();
    }

    /**
     * Either show the actions menu, or activate our window
     */
    public virtual bool on_button_release(Gdk.EventButton event)
    {
        var timestamp = Gtk.get_current_event_time();

        if (window != null) {
            if (this is /*Sparta*/ PinnedIconButton) {
                unpinnage.show();
                pinnage.hide();
            } else {
                unpinnage.hide();
                pinnage.show();
            }
        }

        if (ainfo == null) {
            unpinnage.hide();
            pinnage.hide();
            sep_item.hide();
        } else {
            if (sep_item != null) {
                sep_item.show();
            }
        }

        // Right click, i.e. actions menu
        if (event.button == 3) {
            menu.popup_at_widget(this, Gdk.Gravity.NORTH, Gdk.Gravity.NORTH, event);
            return true;
        }
        if (window == null) {
            return base.button_release_event(event);
        }

        // Normal left click, go handle the window
        if (window.is_minimized()) {
            window.unminimize(timestamp);
            window.activate(timestamp);
        } else {
            if (window.is_active()) {
                window.minimize();
            } else {
                window.activate(timestamp);
            }
        }

        return base.button_release_event(event);
    }

}

public class PinnedIconButton : IconButton
{
    public DesktopAppInfo app_info;
    public string? id = null;
    private Gtk.Menu alt_menu;

    unowned Settings? settings;

    public PinnedIconButton(Settings settings, DesktopAppInfo info, int size, AppSystem? helper, int panel_size)
    {
        base(settings, null, size, info, helper, panel_size);
        this.app_info = info;
        this.settings = settings;

        set_tooltip_text(info.get_display_name());
        image.set_from_gicon(info.get_icon(), Gtk.IconSize.INVALID);

        alt_menu = new Gtk.Menu();
        var item = new Gtk.MenuItem.with_label(_("Unpin from panel"));
        alt_menu.add(item);
        item.show_all();

        item.activate.connect(()=> {
            DesktopHelper.set_pinned(settings, this.app_info, false);
        });
        set_can_focus(false);

        // Drag and drop
        Gtk.drag_source_set(this, Gdk.ModifierType.BUTTON1_MASK, DesktopHelper.targets, Gdk.DragAction.MOVE);

        drag_begin.connect((context)=> {
            if(ainfo != null) {
                Gtk.drag_set_icon_gicon(context, this.app_info.get_icon(), 0, 0);
            } else {
                Gtk.drag_set_icon_default(context);
            }
        });

        launch_context.launched.connect(this.on_launched);
        launch_context.launch_failed.connect(this.on_launch_failed);

        drag_data_get.connect((widget, context, selection_data, info, time)=> {
            selection_data.set(selection_data.get_target(), 8, (uchar []) this.app_info.get_id().to_utf8());
        });
    }

    /**
     * Handle startup notification, set our own ID to the ID selected
     */
    private void on_launched(GLib.AppInfo info, Variant v)
    {
        Variant? elem;

        var iter = v.iterator();

        while ((elem = iter.next_value()) != null) {
            string? key = null;
            Variant? val = null;

            elem.get("{sv}", out key, out val);

            if (key == null) {
                continue;
            }

            if (!val.is_of_type(VariantType.STRING)) {
                continue;
            }

            if (key != "startup-notification-id") {
                continue;
            }

            // Force vala to ref this
            this.id = val.dup_string();
            get_display().notify_startup_complete(this.id);
        }
    }

    /**
     * Not much to do just set our ID to null
     */
    private void on_launch_failed(string id)
    {
        get_display().notify_startup_complete(id);
        this.id = null;
    }

    protected override bool on_button_release(Gdk.EventButton event)
    {
        if (window == null)
        {
            if (event.button == 3) {
                // Expose our own unpin option
                alt_menu.popup_at_widget(this, Gdk.Gravity.NORTH, Gdk.Gravity.NORTH, event);
                return true;
            }
            if (event.button != 1) {
                return true;
            }
            /* Launch ourselves. */
            launch_context.set_screen(get_screen());
            launch_context.set_timestamp(event.time);
            MenuMaker.launch_with_context(app_info,launch_context,null);
            return base.on_button_release(event);
        } else {
            return base.on_button_release(event);
        }
    }

    public override void update_icon()
    {
        if (window != null) {
            base.update_icon();
            return;
        }
        image.pixel_size = icon_size;
        queue_resize();
    }

    public void reset()
    {
        image.set_from_gicon(app_info.get_icon(), Gtk.IconSize.INVALID);
        set_tooltip_text(app_info.get_display_name());
        get_style_context().remove_class("running");
        set_active(false);
        // Actions menu
        menu.destroy();
        menu = null;
        window = null;
        id = null;
    }
}
