// ------------------------------------------------------------
// Docklight 6.0
//
// Implements the settings dialog and configuration persistence.
// ------------------------------------------------------------

#include "dock_settings_dialog.h"
#include "config/dock_configuration_manager.h"
#include "monitors/dock_monitor_manager.h"

#include <glibmm/i18n.h>
#include <gtk-layer-shell.h>
#include <gtkmm.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
void keep_dialog_above(
    Gtk::Window &dialog,
    Gtk::Window &parent,
    const char *name_space)
{
    dialog.set_keep_above(true);

    if (!gtk_layer_is_supported())
        return;

    auto *window = GTK_WINDOW(dialog.gobj());
    gtk_layer_init_for_window(window);
    gtk_layer_set_namespace(window, name_space);
    gtk_layer_set_layer(
        window,
        GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_exclusive_zone(window, 0);
    gtk_layer_set_keyboard_mode(
        window,
        GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);

    const auto parent_window = parent.get_window();
    if (!parent_window)
        return;

    auto *display =
        gdk_window_get_display(parent_window->gobj());
    auto *monitor =
        gdk_display_get_monitor_at_window(
            display,
            parent_window->gobj());
    gtk_layer_set_monitor(window, monitor);
}
}

void DockSettingsDialog::show(
    Gtk::Window &parent,
    const Glib::RefPtr<Gdk::Pixbuf> &icon)
{
    DockConfigurationManager configuration;
    const auto current =
        configuration.current();

    Gtk::Dialog dialog(
        _("DockLight Settings"),
        parent,
        true);

    dialog.set_type_hint(
        Gdk::WINDOW_TYPE_HINT_DIALOG);
    keep_dialog_above(
        dialog,
        parent,
        "docklight6-settings");
    dialog.set_decorated(true);
    dialog.set_resizable(false);
    dialog.property_destroy_with_parent() =
        true;
    dialog.set_skip_taskbar_hint(true);
    dialog.set_skip_pager_hint(true);
    dialog.set_position(
        Gtk::WIN_POS_CENTER_ON_PARENT);
    dialog.set_default_size(460, -1);
    dialog.set_size_request(460, -1);

    Gtk::HeaderBar header;
    Gtk::Image header_icon;

    header.set_title(
        _("DockLight Settings"));
    header.set_show_close_button(true);
    header.set_decoration_layout(
        ":close");

    if (icon)
    {
        dialog.set_icon(
            icon);

        const auto small_home_icon =
            icon->scale_simple(
                20,
                20,
                Gdk::INTERP_BILINEAR);

        if (small_home_icon)
        {
            header_icon.set(
                small_home_icon);
            header.pack_start(
                header_icon);
        }
    }

    dialog.set_titlebar(header);

    dialog.add_button(
        _("_Close"),
        Gtk::RESPONSE_CLOSE);

    Gtk::Grid grid;
    grid.set_hexpand(true);
    grid.set_vexpand(false);
    grid.set_border_width(14);
    grid.set_row_spacing(10);
    grid.set_column_spacing(16);
    grid.set_column_homogeneous(false);

    Gtk::Label monitor_label(
        _("Monitor"));
    Gtk::Label hover_label(
        _("Hover Effect"));
    Gtk::Label indicator_label(
        _("Indicator"));
    Gtk::Label indicator_color_label(
        _("Indicator Color"));
    Gtk::Label home_icon_enabled_label(
        _("Display Home Icon"));
    Gtk::Label home_icon_path_label(
        _("Home Icon"));
    Gtk::Label display_tooltips_label(
        _("Display Tooltips"));
    Gtk::Label display_preview_label(
        _("Display Preview"));
    Gtk::Label manage_all_workspaces_label(
        _("Manage All Workspaces"));
    Gtk::Label icon_size_label(
        _("Icon Size"));
    Gtk::Label preview_card_height_label(
        _("Preview Card Height"));
    Gtk::Label preview_show_delay_label(
        _("Preview Show Delay (ms)"));
    Gtk::Label location_label(
        _("Location"));
    Gtk::Label rounded_corners_label(
        _("Rounded Corners"));
    Gtk::Label corner_radius_label(
        _("Corner Radius"));
    Gtk::Label alignment_label(
        _("Alignment"));
    Gtk::Label autohide_label(
        _("Autohide"));

    const std::vector<Gtk::Label *>
        labels = {
            &monitor_label,
            &hover_label,
            &indicator_label,
            &indicator_color_label,
            &home_icon_enabled_label,
            &home_icon_path_label,
            &display_tooltips_label,
            &display_preview_label,
            &manage_all_workspaces_label,
            &icon_size_label,
            &preview_card_height_label,
            &preview_show_delay_label,
            &location_label,
            &rounded_corners_label,
            &corner_radius_label,
            &alignment_label,
            &autohide_label};

    for (auto *field_label : labels)
    {
        field_label->set_halign(
            Gtk::ALIGN_START);
        field_label->set_valign(
            Gtk::ALIGN_CENTER);
    }

    std::vector<std::string>
        monitor_identifiers{"primary"};

    DockMonitorManager monitor_manager;

    for (const auto &monitor :
         monitor_manager.available_monitors())
    {
        if (!monitor.identifier.empty() &&
            std::find(
                monitor_identifiers.begin(),
                monitor_identifiers.end(),
                monitor.identifier) ==
                monitor_identifiers.end())
        {
            monitor_identifiers.push_back(
                monitor.identifier);
        }
    }

    Gtk::ScrolledWindow monitor_scroller;
    Gtk::ListBox monitor_list;

    monitor_scroller.set_policy(
        Gtk::POLICY_NEVER,
        Gtk::POLICY_AUTOMATIC);
    monitor_scroller.set_shadow_type(
        Gtk::SHADOW_IN);
    monitor_scroller.set_size_request(
        -1,
        std::min(
            130,
            34 * static_cast<int>(
                     monitor_identifiers
                         .size())));
    monitor_list.set_selection_mode(
        Gtk::SELECTION_SINGLE);
    monitor_list.set_activate_on_single_click(
        true);

    Gtk::ListBoxRow *selected_monitor_row =
        nullptr;

    for (const auto &identifier :
         monitor_identifiers)
    {
        auto *row =
            Gtk::manage(
                new Gtk::ListBoxRow());

        auto *name =
            Gtk::manage(
                new Gtk::Label(
                    identifier == "primary"
                        ? C_(
                              "monitor identifier",
                              "Primary")
                        : identifier));

        name->set_halign(
            Gtk::ALIGN_START);
        name->set_margin_start(8);
        name->set_margin_end(8);
        name->set_margin_top(5);
        name->set_margin_bottom(5);

        row->add(*name);
        monitor_list.append(*row);

        if (identifier ==
            current.settings.monitor())
        {
            selected_monitor_row = row;
        }
    }

    monitor_scroller.add(
        monitor_list);

    if (!selected_monitor_row)
    {
        selected_monitor_row =
            monitor_list.get_row_at_index(0);
    }

    if (selected_monitor_row)
    {
        monitor_list.select_row(
            *selected_monitor_row);
    }

    Gtk::Box hover_choices(
        Gtk::ORIENTATION_HORIZONTAL,
        6);
    Gtk::RadioButton hover_standard(
        C_("hover effect", "Standard"));
    Gtk::RadioButton hover_zoom(
        C_("hover effect", "Zoom"));
    Gtk::RadioButton hover_blur(
        C_("hover effect", "Blur"));

    hover_zoom.join_group(
        hover_standard);
    hover_blur.join_group(
        hover_standard);

    hover_choices.pack_start(
        hover_standard,
        false,
        false);
    hover_choices.pack_start(
        hover_zoom,
        false,
        false);
    hover_choices.pack_start(
        hover_blur,
        false,
        false);

    switch (current.settings.hover_effect())
    {
    case DockHoverEffect::standard:
        hover_standard.set_active(true);
        break;
    case DockHoverEffect::zoom:
        hover_zoom.set_active(true);
        break;
    case DockHoverEffect::blur:
        hover_blur.set_active(true);
        break;
    }

    Gtk::Box indicator_choices(
        Gtk::ORIENTATION_HORIZONTAL,
        6);
    Gtk::RadioButton indicator_lines(
        C_("running indicator style", "Lines"));
    Gtk::RadioButton indicator_dots(
        C_("running indicator style", "Dots"));

    indicator_dots.join_group(
        indicator_lines);

    indicator_choices.pack_start(
        indicator_lines,
        false,
        false);
    indicator_choices.pack_start(
        indicator_dots,
        false,
        false);

    if (current.settings.indicator() ==
        DockIndicator::dots)
    {
        indicator_dots.set_active(true);
    }
    else
    {
        indicator_lines.set_active(true);
    }

    Gtk::Button indicator_color;
    Gtk::DrawingArea indicator_color_preview;
    Gdk::RGBA parsed_indicator_color;

    if (!parsed_indicator_color.set(
            current.settings
                .indicator_color()))
    {
        parsed_indicator_color.set(
            "#69aaff");
    }

    indicator_color_preview.set_size_request(
        96,
        24);
    indicator_color_preview
        .signal_draw()
        .connect(
            [&parsed_indicator_color](
                const Cairo::RefPtr<
                    Cairo::Context> &context)
            {
                context->set_source_rgba(
                    parsed_indicator_color
                        .get_red(),
                    parsed_indicator_color
                        .get_green(),
                    parsed_indicator_color
                        .get_blue(),
                    parsed_indicator_color
                        .get_alpha());
                context->paint();

                return true;
            });

    indicator_color.add(
        indicator_color_preview);
    indicator_color.set_tooltip_text(
        _("Choose the indicator color"));

    Gtk::CheckButton home_icon_enabled;
    home_icon_enabled.set_active(
        current.settings
            .home_icon_enabled());

    Gtk::Box home_icon_controls(
        Gtk::ORIENTATION_HORIZONTAL,
        6);
    Gtk::Entry home_icon_path;
    Gtk::Button select_home_icon(
        _("Select…"));
    Gtk::Button use_default_home_icon(
        _("Use Default"));

    const auto configured_home_icon_path =
        current.settings.home_icon_path();

    home_icon_path.set_editable(false);
    home_icon_path.set_hexpand(true);
    home_icon_path.set_placeholder_text(
        _("Built-in DockLight icon"));
    home_icon_path.set_text(
        configured_home_icon_path);
    home_icon_path.set_tooltip_text(
        configured_home_icon_path);

    home_icon_controls.pack_start(
        home_icon_path,
        true,
        true);
    home_icon_controls.pack_start(
        select_home_icon,
        false,
        false);
    home_icon_controls.pack_start(
        use_default_home_icon,
        false,
        false);

    Gtk::CheckButton display_tooltips;
    display_tooltips.set_active(
        current.settings
            .display_tooltips());

    Gtk::CheckButton display_preview;
    display_preview.set_active(
        current.settings
            .display_preview());

    Gtk::CheckButton manage_all_workspaces;
    manage_all_workspaces.set_active(
        current.settings
            .manage_all_workspaces());
    manage_all_workspaces.set_tooltip_text(
        _("Apply icon actions and mouse-wheel cycling across all workspaces"));

    auto icon_size_adjustment =
        Gtk::Adjustment::create(
            current.settings.icon_size(),
            32.0,
            128.0,
            1.0,
            4.0);

    Gtk::SpinButton icon_size_spin(
        icon_size_adjustment,
        1.0,
        0);
    icon_size_spin.set_numeric(true);

    auto preview_card_height_adjustment =
        Gtk::Adjustment::create(
            current.settings
                .preview_card_height(),
            0.0,
            512.0,
            1.0,
            16.0);

    Gtk::SpinButton preview_card_height_spin(
        preview_card_height_adjustment,
        1.0,
        0);
    preview_card_height_spin.set_numeric(true);
    preview_card_height_spin.set_tooltip_text(
        _("0 selects automatic sizing; otherwise use 64 to 512 pixels"));

    auto preview_show_delay_adjustment =
        Gtk::Adjustment::create(
            current.settings
                .preview_show_delay(),
            0.0,
            1000.0,
            10.0,
            100.0);

    Gtk::SpinButton preview_show_delay_spin(
        preview_show_delay_adjustment,
        1.0,
        0);
    preview_show_delay_spin.set_numeric(true);
    preview_show_delay_spin.set_tooltip_text(
        _("Delay before a window preview appears, "
          "from 0 to 1000 milliseconds"));

    Gtk::Box location_choices(
        Gtk::ORIENTATION_HORIZONTAL,
        6);
    Gtk::RadioButton location_bottom(
        C_("dock location", "Bottom"));
    Gtk::RadioButton location_left(
        C_("dock location", "Left"));
    Gtk::RadioButton location_top(
        C_("dock location", "Top"));
    Gtk::RadioButton location_right(
        C_("dock location", "Right"));

    location_left.join_group(
        location_bottom);
    location_top.join_group(
        location_bottom);
    location_right.join_group(
        location_bottom);

    location_choices.pack_start(
        location_bottom,
        false,
        false);
    location_choices.pack_start(
        location_left,
        false,
        false);
    location_choices.pack_start(
        location_top,
        false,
        false);
    location_choices.pack_start(
        location_right,
        false,
        false);

    switch (current.layout_request.location)
    {
    case DockLocation::bottom:
        location_bottom.set_active(true);
        break;
    case DockLocation::left:
        location_left.set_active(true);
        break;
    case DockLocation::top:
        location_top.set_active(true);
        break;
    case DockLocation::right:
        location_right.set_active(true);
        break;
    }

    Gtk::CheckButton rounded_corners;
    rounded_corners.set_active(
        current.layout_request
            .rounded_corners);

    auto corner_radius_adjustment =
        Gtk::Adjustment::create(
            current.layout_request
                .corner_radius,
            -1.0,
            current.settings.icon_size() /
                2.0,
            1.0,
            2.0);

    Gtk::SpinButton corner_radius_spin(
        corner_radius_adjustment,
        1.0,
        0);
    corner_radius_spin.set_numeric(true);
    corner_radius_spin.set_tooltip_text(
        _("-1 selects the automatic radius"));

    Gtk::Box alignment_choices(
        Gtk::ORIENTATION_HORIZONTAL,
        6);
    Gtk::RadioButton alignment_start(
        C_("dock alignment", "Start"));
    Gtk::RadioButton alignment_center(
        C_("dock alignment", "Center"));
    Gtk::RadioButton alignment_end(
        C_("dock alignment", "End"));
    Gtk::RadioButton alignment_fill(
        C_("dock alignment", "Fill"));

    alignment_center.join_group(
        alignment_start);
    alignment_end.join_group(
        alignment_start);
    alignment_fill.join_group(
        alignment_start);

    alignment_choices.pack_start(
        alignment_start,
        false,
        false);
    alignment_choices.pack_start(
        alignment_center,
        false,
        false);
    alignment_choices.pack_start(
        alignment_end,
        false,
        false);
    alignment_choices.pack_start(
        alignment_fill,
        false,
        false);

    switch (current.layout_request.alignment)
    {
    case DockAlignment::start:
        alignment_start.set_active(true);
        break;
    case DockAlignment::center:
        alignment_center.set_active(true);
        break;
    case DockAlignment::end:
        alignment_end.set_active(true);
        break;
    case DockAlignment::fill:
        alignment_fill.set_active(true);
        break;
    }

    Gtk::Box autohide_choices(
        Gtk::ORIENTATION_HORIZONTAL,
        6);
    Gtk::RadioButton autohide_none(
        C_("autohide mode", "None"));
    Gtk::RadioButton autohide_always(
        C_("autohide mode", "Autohide"));
    Gtk::RadioButton autohide_intelligent(
        C_("autohide mode", "Intellihide"));

    autohide_always.join_group(
        autohide_none);
    autohide_intelligent.join_group(
        autohide_none);

    autohide_choices.pack_start(
        autohide_none,
        false,
        false);
    autohide_choices.pack_start(
        autohide_always,
        false,
        false);
    autohide_choices.pack_start(
        autohide_intelligent,
        false,
        false);

    switch (current.layout_request.autohide)
    {
    case DockAutohide::none:
        autohide_none.set_active(true);
        break;
    case DockAutohide::autohide:
        autohide_always.set_active(true);
        break;
    case DockAutohide::intellihide:
        autohide_intelligent
            .set_active(true);
        break;
    }

    const std::vector<Gtk::Widget *>
        fields = {
            &monitor_scroller,
            &hover_choices,
            &indicator_choices,
            &indicator_color,
            &home_icon_enabled,
            &home_icon_controls,
            &display_tooltips,
            &display_preview,
            &manage_all_workspaces,
            &icon_size_spin,
            &preview_card_height_spin,
            &preview_show_delay_spin,
            &location_choices,
            &rounded_corners,
            &corner_radius_spin,
            &alignment_choices,
            &autohide_choices};

    for (auto *field : fields)
    {
        field->set_hexpand(true);
        field->set_halign(
            Gtk::ALIGN_FILL);
        field->set_valign(
            Gtk::ALIGN_CENTER);
    }

    rounded_corners.set_halign(
        Gtk::ALIGN_START);
    home_icon_enabled.set_halign(
        Gtk::ALIGN_START);
    display_tooltips.set_halign(
        Gtk::ALIGN_START);
    display_preview.set_halign(
        Gtk::ALIGN_START);
    manage_all_workspaces.set_halign(
        Gtk::ALIGN_START);

    grid.attach(
        monitor_label,
        0,
        0,
        1,
        1);
    grid.attach(
        monitor_scroller,
        1,
        0,
        1,
        1);
    grid.attach(
        hover_label,
        0,
        1,
        1,
        1);
    grid.attach(
        hover_choices,
        1,
        1,
        1,
        1);
    grid.attach(
        indicator_label,
        0,
        2,
        1,
        1);
    grid.attach(
        indicator_choices,
        1,
        2,
        1,
        1);
    grid.attach(
        indicator_color_label,
        0,
        3,
        1,
        1);
    grid.attach(
        indicator_color,
        1,
        3,
        1,
        1);
    grid.attach(
        home_icon_enabled_label,
        0,
        4,
        1,
        1);
    grid.attach(
        home_icon_enabled,
        1,
        4,
        1,
        1);
    grid.attach(
        home_icon_path_label,
        0,
        5,
        1,
        1);
    grid.attach(
        home_icon_controls,
        1,
        5,
        1,
        1);
    grid.attach(
        display_tooltips_label,
        0,
        6,
        1,
        1);
    grid.attach(
        display_tooltips,
        1,
        6,
        1,
        1);
    grid.attach(
        display_preview_label,
        0,
        7,
        1,
        1);
    grid.attach(
        display_preview,
        1,
        7,
        1,
        1);
    grid.attach(
        manage_all_workspaces_label,
        0,
        8,
        1,
        1);
    grid.attach(
        manage_all_workspaces,
        1,
        8,
        1,
        1);
    grid.attach(
        icon_size_label,
        0,
        9,
        1,
        1);
    grid.attach(
        icon_size_spin,
        1,
        9,
        1,
        1);
    grid.attach(
        preview_card_height_label,
        0,
        10,
        1,
        1);
    grid.attach(
        preview_card_height_spin,
        1,
        10,
        1,
        1);
    grid.attach(
        preview_show_delay_label,
        0,
        11,
        1,
        1);
    grid.attach(
        preview_show_delay_spin,
        1,
        11,
        1,
        1);
    grid.attach(
        location_label,
        0,
        12,
        1,
        1);
    grid.attach(
        location_choices,
        1,
        12,
        1,
        1);
    grid.attach(
        rounded_corners_label,
        0,
        13,
        1,
        1);
    grid.attach(
        rounded_corners,
        1,
        13,
        1,
        1);
    grid.attach(
        corner_radius_label,
        0,
        14,
        1,
        1);
    grid.attach(
        corner_radius_spin,
        1,
        14,
        1,
        1);
    grid.attach(
        alignment_label,
        0,
        15,
        1,
        1);
    grid.attach(
        alignment_choices,
        1,
        15,
        1,
        1);
    grid.attach(
        autohide_label,
        0,
        16,
        1,
        1);
    grid.attach(
        autohide_choices,
        1,
        16,
        1,
        1);

    std::vector<sigc::connection>
        settings_connections;

    settings_connections.push_back(
        monitor_list
            .signal_row_selected()
            .connect(
            [&configuration,
             &monitor_identifiers](
                Gtk::ListBoxRow *row)
            {
                if (!row)
                    return;

                const int index =
                    row->get_index();

                if (index < 0 ||
                    index >=
                        static_cast<int>(
                            monitor_identifiers
                                .size()))
                {
                    return;
                }

                configuration.save_setting(
                    "monitor",
                    monitor_identifiers[
                        static_cast<
                            std::size_t>(
                            index)]);
            }));

    const auto connect_radio =
        [&configuration,
         &settings_connections](
            Gtk::RadioButton &button,
            const std::string &key,
            const std::string &value)
    {
        settings_connections.push_back(
            button
                .signal_toggled()
                .connect(
                [&configuration,
                 &button,
                 key,
                 value]()
                {
                    if (button.get_active())
                    {
                        configuration.save_setting(
                            key,
                            value);
                    }
                }));
    };

    connect_radio(
        hover_standard,
        "hover_effect",
        "standard");
    connect_radio(
        hover_zoom,
        "hover_effect",
        "zoom");
    connect_radio(
        hover_blur,
        "hover_effect",
        "blur");
    connect_radio(
        indicator_lines,
        "indicator",
        "lines");
    connect_radio(
        indicator_dots,
        "indicator",
        "dots");
    connect_radio(
        location_bottom,
        "location",
        "bottom");
    connect_radio(
        location_left,
        "location",
        "left");
    connect_radio(
        location_top,
        "location",
        "top");
    connect_radio(
        location_right,
        "location",
        "right");
    connect_radio(
        alignment_start,
        "alignment",
        "start");
    connect_radio(
        alignment_center,
        "alignment",
        "center");
    connect_radio(
        alignment_end,
        "alignment",
        "end");
    connect_radio(
        alignment_fill,
        "alignment",
        "fill");
    connect_radio(
        autohide_none,
        "autohide",
        "none");
    connect_radio(
        autohide_always,
        "autohide",
        "autohide");
    connect_radio(
        autohide_intelligent,
        "autohide",
        "intellihide");

    settings_connections.push_back(
        home_icon_enabled
            .signal_toggled()
            .connect(
            [&configuration,
             &home_icon_enabled]()
            {
                configuration.save_setting(
                    "home_icon_enabled",
                    home_icon_enabled
                            .get_active()
                        ? "true"
                        : "false");
            }));

    settings_connections.push_back(
        display_tooltips
            .signal_toggled()
            .connect(
            [&configuration,
             &display_tooltips]()
            {
                configuration.save_setting(
                    "display_tooltips",
                    display_tooltips
                            .get_active()
                        ? "true"
                        : "false");
            }));

    settings_connections.push_back(
        display_preview
            .signal_toggled()
            .connect(
            [&configuration,
             &display_preview]()
            {
                configuration.save_setting(
                    "display_preview",
                    display_preview
                            .get_active()
                        ? "true"
                        : "false");
            }));

    settings_connections.push_back(
        manage_all_workspaces
            .signal_toggled()
            .connect(
            [&configuration,
             &manage_all_workspaces]()
            {
                configuration.save_setting(
                    "manage_all_workspaces",
                    manage_all_workspaces
                            .get_active()
                        ? "true"
                        : "false");
            }));

    settings_connections.push_back(
        select_home_icon
            .signal_clicked()
            .connect(
            [&configuration,
             &dialog,
             &home_icon_path,
             &icon]()
            {
                Gtk::Dialog
                    icon_dialog(
                        _("Select Home Icon"),
                        dialog,
                        true);

                icon_dialog.add_button(
                    _("_Cancel"),
                    Gtk::RESPONSE_CANCEL);
                icon_dialog.add_button(
                    _("_Open"),
                    Gtk::RESPONSE_OK);
                icon_dialog.set_type_hint(
                    Gdk::WINDOW_TYPE_HINT_DIALOG);
                keep_dialog_above(
                    icon_dialog,
                    dialog,
                    "docklight6-icon-chooser");
                icon_dialog.set_decorated(true);
                icon_dialog.set_resizable(true);
                icon_dialog
                    .property_destroy_with_parent() =
                    true;
                icon_dialog
                    .set_skip_taskbar_hint(true);
                icon_dialog
                    .set_skip_pager_hint(true);
                icon_dialog.set_position(
                    Gtk::WIN_POS_CENTER_ON_PARENT);
                icon_dialog.set_default_size(
                    760,
                    520);

                Gtk::HeaderBar icon_header;
                Gtk::Image icon_header_icon;

                icon_header.set_title(
                    _("Select Home Icon"));
                icon_header
                    .set_show_close_button(true);
                icon_header.set_decoration_layout(
                    ":close");

                if (icon)
                {
                    icon_dialog.set_icon(
                        icon);

                    const auto small_home_icon =
                        icon->scale_simple(
                            20,
                            20,
                            Gdk::INTERP_BILINEAR);

                    if (small_home_icon)
                    {
                        icon_header_icon.set(
                            small_home_icon);
                        icon_header.pack_start(
                            icon_header_icon);
                    }
                }

                icon_dialog.set_titlebar(
                    icon_header);

                Gtk::FileChooserWidget
                    icon_chooser(
                        Gtk::
                            FILE_CHOOSER_ACTION_OPEN);

                auto image_filter =
                    Gtk::FileFilter::create();
                image_filter->set_name(
                    _("Image Files"));
                image_filter
                    ->add_pixbuf_formats();
                icon_chooser.add_filter(
                    image_filter);

                const auto current_path =
                    home_icon_path.get_text();

                if (!current_path.empty())
                {
                    icon_chooser.set_filename(
                        current_path);
                }

                auto *icon_content =
                    icon_dialog.get_content_area();

                icon_content->pack_start(
                    icon_chooser,
                    true,
                    true);

                icon_dialog.show_all_children();
                icon_dialog.present();

                if (icon_dialog.run() ==
                    Gtk::RESPONSE_OK)
                {
                    const auto selected_path =
                        icon_chooser
                            .get_filename();

                    if (!selected_path.empty())
                    {
                        home_icon_path.set_text(
                            selected_path);
                        home_icon_path
                            .set_tooltip_text(
                                selected_path);
                        configuration.save_setting(
                            "home_icon_path",
                            selected_path);
                    }
                }

                icon_dialog.hide();
            }));

    settings_connections.push_back(
        use_default_home_icon
            .signal_clicked()
            .connect(
            [&configuration,
             &home_icon_path]()
            {
                home_icon_path.set_text("");
                home_icon_path
                    .set_tooltip_text("");
                configuration.save_setting(
                    "home_icon_path",
                    "");
            }));

    settings_connections.push_back(
        indicator_color
            .signal_clicked()
            .connect(
            [&configuration,
             &dialog,
             &indicator_color_preview,
             &parsed_indicator_color,
             &icon]()
            {
                Gtk::ColorChooserDialog
                    color_dialog(
                        _("Indicator Color"),
                        dialog);

                color_dialog.set_modal(true);
                color_dialog.set_type_hint(
                    Gdk::WINDOW_TYPE_HINT_DIALOG);
                keep_dialog_above(
                    color_dialog,
                    dialog,
                    "docklight6-color-chooser");
                color_dialog.set_decorated(true);
                color_dialog
                    .property_destroy_with_parent() =
                    true;
                color_dialog
                    .set_skip_taskbar_hint(true);
                color_dialog
                    .set_skip_pager_hint(true);
                color_dialog.set_position(
                    Gtk::WIN_POS_CENTER_ON_PARENT);
                color_dialog.set_use_alpha(true);
                color_dialog.set_rgba(
                    parsed_indicator_color);

                Gtk::HeaderBar color_header;
                Gtk::Image color_header_icon;

                color_header.set_title(
                    _("Indicator Color"));
                color_header
                    .set_show_close_button(true);
                color_header.set_decoration_layout(
                    ":close");

                if (icon)
                {
                    color_dialog.set_icon(
                        icon);

                    const auto small_home_icon =
                        icon->scale_simple(
                            20,
                            20,
                            Gdk::INTERP_BILINEAR);

                    if (small_home_icon)
                    {
                        color_header_icon.set(
                            small_home_icon);
                        color_header.pack_start(
                            color_header_icon);
                    }
                }

                color_dialog.set_titlebar(
                    color_header);
                color_dialog.show_all_children();
                color_dialog.present();

                if (color_dialog.run() !=
                    Gtk::RESPONSE_OK)
                {
                    color_dialog.hide();
                    return;
                }

                parsed_indicator_color =
                    color_dialog.get_rgba();
                indicator_color_preview
                    .queue_draw();

                configuration.save_setting(
                    "indicator_color",
                    parsed_indicator_color
                        .to_string());

                color_dialog.hide();
            }));

    settings_connections.push_back(
        icon_size_spin
        .signal_value_changed()
        .connect(
            [&configuration,
             &icon_size_spin,
             &corner_radius_spin,
             &corner_radius_adjustment]()
            {
                const int icon_size =
                    icon_size_spin
                        .get_value_as_int();

                const int maximum_radius =
                    icon_size / 2;

                corner_radius_adjustment
                    ->set_upper(
                        maximum_radius);

                const int corner_radius =
                    corner_radius_spin
                        .get_value_as_int();

                if (corner_radius != -1 &&
                    corner_radius >
                        maximum_radius)
                {
                    corner_radius_spin
                        .set_value(
                            maximum_radius);
                }

                // Saving triggers a monitored configuration reload. Finish
                // touching dialog-owned GTK objects before that reload can
                // reconfigure the dock and its transient settings dialog.
                configuration.save_setting(
                    "icon_size",
                    std::to_string(
                        icon_size));
            }));

    settings_connections.push_back(
        preview_card_height_spin
            .signal_value_changed()
            .connect(
            [&configuration,
             &preview_card_height_spin]()
            {
                int height =
                    preview_card_height_spin
                        .get_value_as_int();

                if (height > 0 && height < 64)
                {
                    height = 64;
                    preview_card_height_spin
                        .set_value(height);
                    return;
                }

                configuration.save_setting(
                    "preview_card_height",
                    std::to_string(height));
            }));

    settings_connections.push_back(
        preview_show_delay_spin
            .signal_value_changed()
            .connect(
            [&configuration,
             &preview_show_delay_spin]()
            {
                configuration.save_setting(
                    "preview_show_delay",
                    std::to_string(
                        preview_show_delay_spin
                            .get_value_as_int()));
            }));

    settings_connections.push_back(
        rounded_corners
            .signal_toggled()
            .connect(
            [&configuration,
             &rounded_corners]()
            {
                configuration.save_setting(
                    "rounded_corners",
                    rounded_corners
                            .get_active()
                        ? "true"
                        : "false");
            }));

    settings_connections.push_back(
        corner_radius_spin
        .signal_value_changed()
        .connect(
            [&configuration,
             &corner_radius_spin]()
            {
                configuration.save_setting(
                    "corner_radius",
                    std::to_string(
                        corner_radius_spin
                            .get_value_as_int()));
            }));

    auto *content =
        dialog.get_content_area();

    content->pack_start(
        grid,
        false,
        false);

    dialog.show_all_children();
    dialog.present();
    dialog.run();

    // Several GTK controls can emit signals while being torn down. Their
    // callbacks refer to other stack-owned dialog controls, whose destruction
    // order is different, so detach every callback before leaving this scope.
    for (auto &connection :
         settings_connections)
    {
        connection.disconnect();
    }

    dialog.hide();
}
