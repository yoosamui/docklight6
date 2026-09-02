// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_session_item.cpp
//
// Implementation overview:
// Builds one editable, UI-only Session Item card.
//
// Important implementation decisions:
// - Copy and Launch are intentionally inert in this implementation step.
// - Remove emits a presentation signal; the containing editor decides which
//   card to remove.
// - Every value lives only in GTK widgets and is discarded with the dialog.
//
// ------------------------------------------------------------

#include "dock_session_item.h"

#include <glibmm/i18n.h>

DockSessionItem::DockSessionItem()
    : m_app_title_label(_("App Title")),
      m_actions(Gtk::ORIENTATION_HORIZONTAL, 6),
      m_copy_button(_("_Copy"), true),
      m_launch_button(_("_Launch"), true),
      m_remove_button(_("_Remove"), true),
      m_parameters_label(_("Parameters")),
      m_workspace_label(_("Workspace")),
      m_dimensions_label(_("Dimensions")),
      m_position_label(_("Position"))
{
    set_shadow_type(Gtk::SHADOW_ETCHED_IN);
    set_hexpand(true);

    m_layout.set_border_width(12);
    m_layout.set_row_spacing(8);
    m_layout.set_column_spacing(10);
    m_layout.set_hexpand(true);

    m_app_icon.set_from_icon_name(
        "application-x-executable",
        Gtk::ICON_SIZE_DIALOG);
    m_app_icon.set_pixel_size(40);
    m_app_icon.set_valign(Gtk::ALIGN_START);

    m_app_title_label.set_halign(Gtk::ALIGN_START);
    m_parameters_label.set_halign(Gtk::ALIGN_START);
    m_workspace_label.set_halign(Gtk::ALIGN_START);
    m_dimensions_label.set_halign(Gtk::ALIGN_START);
    m_position_label.set_halign(Gtk::ALIGN_START);

    m_app_title.set_hexpand(true);
    m_app_title.set_placeholder_text(
        _("Application title"));
    m_parameters.set_hexpand(true);
    m_parameters.set_placeholder_text(
        _("Command-line parameters"));
    m_workspace.set_placeholder_text(
        _("Workspace number"));
    m_dimensions.set_text("400x500");
    m_position.set_text("120x200");

    m_actions.pack_start(
        m_copy_button,
        false,
        false);
    m_actions.pack_start(
        m_launch_button,
        false,
        false);
    m_actions.pack_start(
        m_remove_button,
        false,
        false);

    m_layout.attach(m_app_icon, 0, 0, 1, 2);
    m_layout.attach(m_app_title_label, 1, 0, 1, 1);
    m_layout.attach(m_app_title, 2, 0, 1, 1);
    m_layout.attach(m_actions, 3, 0, 1, 1);
    m_layout.attach(m_parameters_label, 1, 1, 1, 1);
    m_layout.attach(m_parameters, 2, 1, 2, 1);
    m_layout.attach(m_workspace_label, 1, 2, 1, 1);
    m_layout.attach(m_workspace, 2, 2, 2, 1);
    m_layout.attach(m_dimensions_label, 1, 3, 1, 1);
    m_layout.attach(m_dimensions, 2, 3, 2, 1);
    m_layout.attach(m_position_label, 1, 4, 1, 1);
    m_layout.attach(m_position, 2, 4, 2, 1);

    add(m_layout);

    m_remove_button.signal_clicked().connect(
        [this]()
        {
            m_remove_requested.emit();
        });
}

sigc::signal<void> &DockSessionItem::signal_remove_requested()
{
    return m_remove_requested;
}
